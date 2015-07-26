// Contributed by Elie Morisse, same license DMD uses

#include "cpp/astunit.h"
#include "cpp/modulemap.h"
#include "aggregate.h"
#include "attrib.h"
#include "declaration.h"
#include "enum.h"
#include "identifier.h"
#include "import.h"
#include "init.h"
#include "lexer.h"
#include "template.h"
#include "scope.h"
#include "statement.h"
#include "id.h"

#include "cpp/calypso.h"
#include "cpp/cppmodule.h"
#include "cpp/cppdeclaration.h"
#include "cpp/cppimport.h"
#include "cpp/cppaggregate.h"
#include "cpp/cppexpression.h"
#include "cpp/cpptemplate.h"

#include <stdlib.h>
#include <string>

#include "llvm/ADT/DenseMap.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Sema/Sema.h"
#include "clang/Sema/Lookup.h"

namespace cpp
{

using llvm::cast;
using llvm::dyn_cast;
using llvm::isa;

/********************************/

Package *Module::rootPackage;
Modules Module::amodules;

void Module::init()
{
    rootPackage = new Package(Lexer::idPool("cpp"));
    rootPackage->symtab = new DsymbolTable;

    modules->insert(rootPackage);
}

static void combine(char *&objfn, Identifier *id)
{
    auto prevobjfn = objfn;

    auto objlen = strlen(objfn);
    auto idlen = id->len;
    objfn = (char *)mem.malloc(objlen + 1 + idlen + 1);
    memcpy(objfn, prevobjfn, objlen);
    objfn[objlen] = '_';
    objlen++;
    memcpy(objfn + objlen, id->string, idlen + 1);

    mem.free(prevobjfn);
}

Module::Module(const char* filename, Identifier* ident, Identifiers *packages)
    : ::Module(nullptr, ident, 0, 0)
{
    srcfile = new File(filename);

    // Let's not create directories for the time being
    const char *objPrefix = "__cpp";
    char *objfn = strdup(objPrefix);

    // e.g __cpp_package_package_module.o
    for (size_t i = 1; i < packages->dim; i++)
    {
        Identifier *pid = (*packages)[i];
        combine(objfn, pid);
    }
    combine(objfn, ident);

    arg = objfn;
}

Dsymbol *Module::search(Loc loc, Identifier *ident, int flags)
{
    auto result = ::Module::search(loc, ident, flags);

    if ((flags & IgnorePrivateMembers) && result && result->isImport())
        return nullptr; // semi-HACK? this makes the imports inside an imported module invisible to D code,
                                // but also prevents conflicts caused by the choice to name C++ modules after record names.

    return result;
}

void Module::addPreambule()
{
    // Statically import object.d for object and size_t (used by buildXtoHash)

    // TODO This still makes "object" susceptible to collide with C++ names.
    // We could eventually choose a random unused alias name if necessary.
    if (members->dim == 0 || ((*members)[0])->ident != Id::object)
    {
        ::Import *im = new ::Import(Loc(), NULL, Id::object, NULL, true);
        members->shift(im);
    }
}

llvm::Module *Module::genLLVMModule(llvm::LLVMContext &context)
{
    auto M = ::Module::genLLVMModule(context);

    if (!global.errors)
        calypso.genModSet.add(this);

    return M;
}

/************************************/

// DeclMapper::DeclMapper(Module* mod)
//     : TypeMapper(mod)
// {
// }

inline PROT DeclMapper::toPROT(clang::AccessSpecifier AS)
{
    switch(AS) {
        case clang::AS_public:
            return PROTpublic;
        case clang::AS_protected:
            return PROTprotected;
        case clang::AS_private:
            return PROTprivate;
        case clang::AS_none:
            return PROTnone;
        default:
            return PROTundefined;
    }
}

/*****/

namespace
{

inline Dsymbols *oneSymbol(Dsymbol *s)
{
    auto decldefs = new Dsymbols;
    decldefs->push(s);
    return decldefs;
}

}

Dsymbols *DeclMapper::VisitDeclContext(const clang::DeclContext *DC)
{
    auto decldefs = new Dsymbols;

    for (auto D = DC->decls_begin(), DEnd = DC->decls_end();
        D != DEnd; ++D)
    {
        if (auto d = VisitDecl(*D))
            decldefs->append(d);
    }

    return decldefs;
}

Dsymbols *DeclMapper::VisitDecl(const clang::Decl *D, unsigned flags)
{
    if (!D->isCanonicalDecl())
        return nullptr;

    Dsymbols *s = nullptr;

#define DECL(BASE) \
    else if (const clang::BASE##Decl *BASE##D = \
                            dyn_cast<clang::BASE##Decl>(D)) \
        s = Visit##BASE##Decl(BASE##D);
#define DECLWF(BASE) \
    else if (const clang::BASE##Decl *BASE##D = \
                            dyn_cast<clang::BASE##Decl>(D)) \
        s = Visit##BASE##Decl(BASE##D, flags);

    if (0) ;
    DECL(TypedefName)
    DECL(ClassTemplateSpecialization)
    DECLWF(Record)
    DECL(Function)
    DECL(RedeclarableTemplate)
    DECL(Enum)
    DECL(Value)

#undef DECL
#undef DECLWF

    return s;
}

Dsymbols *DeclMapper::VisitValueDecl(const clang::ValueDecl *D)
{
    auto& Context = calypso.getASTContext();
    ExprMapper expmap(*this);

    if (isNonSupportedType(D->getType()))
        return nullptr;

    if (isa<clang::IndirectFieldDecl>(D)) // implicit fields injected from anon unions/structs, which are already mapped
        return nullptr;

    auto loc = fromLoc(D->getLocation());
    auto Ty = D->getType().getDesugaredType(Context);

    if (Ty->isUnionType() || Ty->isStructureType())
    {
        if (auto RT = Ty->getAs<clang::RecordType>()) // dyn_cast because it can also be a typedeftype, which always have names
        {
            auto RD = RT->getDecl();

            // NOTE: in union {...} myUnion isAnonymousStructOrUnion() will be false, see Decl.h
            if (RD->isAnonymousStructOrUnion() || !RD->getIdentifier())
                return nullptr;

            // TODO how to handle union {...} myUnion?
            // We could try adding the aggregate without an id, and assign a TypeClass/TypeStruct straightaway.
            // Ignore them for now.
        }
    }

    if (!D->getIdentifier())
    {
        auto FD = cast<clang::FieldDecl>(D);
        assert(FD->isBitField());
        return nullptr; // unnamed bitfield
    }

    auto id = fromIdentifier(D->getIdentifier());
    auto t = fromType(D->getType());

    if (!t)
        return nullptr;

    if (t->isConst())
        t = t->immutableOf();

    auto a = new VarDeclaration(loc, id, D, t);

    if (auto Var = dyn_cast<clang::VarDecl>(D))
    {
        if (Var->hasExternalStorage())
            a->storage_class |= STCextern;

        if (Var->getTLSKind() == clang::VarDecl::TLS_Dynamic)
            a->storage_class |= STCtls;

        if (Var->isStaticDataMember())
            a->storage_class |= STCstatic;

        if ((Var->isConstexpr() || t->isImmutable()) &&
                Var->getAnyInitializer())
        {
            // we avoid initializer expressions except for const/constexpr variables
            auto e = expmap.fromExpression(Var->getAnyInitializer(),
                                           clang::QualType(), true);
            if (e && e->op != TOKnull)
                a->init = new ExpInitializer(loc, e);
        }
    }

    return oneSymbol(a);
}

Dsymbols *DeclMapper::VisitRecordDecl(const clang::RecordDecl *D, unsigned flags)
{
    auto& S = calypso.pch.AST->getSema();
    auto Canon = D->getCanonicalDecl();

    if (D->isImplicit() && !(flags & MapImplicit))
        return nullptr;

    auto loc = fromLoc(D->getLocation());

    if (!D->isCompleteDefinition() && D->getDefinition())
        D = D->getDefinition();  // WARNING: if the definition isn't in the PCH this is going to submit an empty aggregate decl, there could be conflicts if several PCH or modules are used
    bool isDefined = D->isCompleteDefinition();

    auto TND = D->getTypedefNameForAnonDecl();
    bool isPOD = true;

    int anon = 0;
    if (D->isAnonymousStructOrUnion())
    {
        assert(!TND);

        anon = 1;
        if (D->isUnion())
            anon = 2;
    }

    if (!anon && !getIdentifierOrNull(D))
    {
        ::warning(loc, "Value with anon record type, no D equivalent (needs workaround), discarding");
        return nullptr;
    }

    auto CRD = dyn_cast<clang::CXXRecordDecl>(D);
        // NOTE: CXXRecordDecl will disappear in a future version of Clang and only
        // RecordDecl will remain to be used for both C and C++.

    if ((flags & ForceNonPOD) ||
            (CRD && isDefined && !CRD->isPOD()))
        isPOD = false;

    AggregateDeclaration *a;
    if (!anon)
    {
        auto id = getIdentifier(D);

        if (D->isUnion())
        {
            a = new UnionDeclaration(loc, id);
        }
        else if (isPOD)
        {
            a = new StructDeclaration(loc, id, D);
        }
        else
        {
            auto baseclasses = new BaseClasses;

            if (CRD)
            {
                for (auto B = CRD->bases_begin(),
                        BEnd = CRD->bases_end(); B != BEnd; ++B)
                {
                    auto brt = fromType(B->getType());

                    baseclasses->push(new BaseClass(brt,
                                                    toPROT(B->getAccessSpecifier())));
                }
            }

            auto cd = new ClassDeclaration(loc, id, baseclasses, CRD);
            a = cd;
        }

        declMap[D] = a;
    }

    CXXScope.push(D);

    // atm we're sortof mirroring parseAggregate()
    auto members = new Dsymbols;

    if (!isDefined)
        goto Ldeclaration;

    for (auto I = D->field_begin(), E = D->field_end();
            I != E; ++I)
    {
        if (I->getCanonicalDecl() != *I)
            continue;

        auto field = VisitValueDecl(*I);
        if (field)
            members->append(field);
    }

    if (CRD && !D->isUnion())
    {
        if (!isPOD && !CRD->isDependentType())
            // Clang declares and defines the implicit default constructor lazily, so do it here
            // before adding methods.
            S.LookupDefaultConstructor(const_cast<clang::CXXRecordDecl *>(CRD));

        for (auto I = CRD->method_begin(), E = CRD->method_end();
            I != E; ++I)
        {
            if (I->getCanonicalDecl() != *I)
                continue;

            auto CCD = dyn_cast<clang::CXXConstructorDecl>(*I);
            if (CCD && CCD->isDefaultConstructor() && isPOD)
                continue;

            // CALYPSO FIXME remove the null check once everything is implemented
            auto fd = VisitFunctionDecl(*I);
            if (fd)
                members->append(fd);
        }
    }

    // Add specific decls: vars, tags, templates, typedefs
#define SPECIFIC_ADD(DECL) \
    typedef clang::DeclContext::specific_decl_iterator<clang::DECL##Decl> DECL##_iterator; \
    for (DECL##_iterator I(D->decls_begin()), E(D->decls_end()); \
                I != E; I++) \
    { \
        if (cast<clang::Decl>((*I)->getDeclContext())->getCanonicalDecl() != Canon) \
            continue;  /* only map declarations that are semantically within the RecordDecl */ \
        if (auto s = VisitDecl(*I)) \
            members->append(s); \
    }

    SPECIFIC_ADD(Tag)
    SPECIFIC_ADD(Var)
    SPECIFIC_ADD(RedeclarableTemplate)
    SPECIFIC_ADD(TypedefName)

#undef SPECIFIC_ADD

Ldeclaration:
    CXXScope.pop();

    if (anon)
        return oneSymbol(new AnonDeclaration(loc, anon == 2, members));

    a->members = members;

    return oneSymbol(a);
}

Dsymbols *DeclMapper::VisitTypedefNameDecl(const clang::TypedefNameDecl* D)
{
    auto& Context = calypso.pch.AST->getASTContext();
    auto Ty = D->getUnderlyingType();

    if (isAnonTagTypedef(D))
        return nullptr;  // the anon tag will be mapped by VisitRecordDecl to an aggregate named after the typedef identifier

    if (isSameNameTagTypedef(D)) // e.g typedef union pthread_attr_t pthread_attr_t needs to be discarded
        return nullptr;

    if (D->isImplicit())
        if (Ty == Context.Int128Ty || Ty == Context.UnsignedInt128Ty)
            return nullptr; // any function using those typedefs will be discarded anyway

    auto loc = fromLoc(D->getLocation());
    auto id = fromIdentifier(D->getIdentifier());
    auto t = fromType(D->getUnderlyingType());

    if (!t)
        return nullptr;

    auto a = new AliasDeclaration(loc, id, t, D);
    return oneSymbol(a);
}

TemplateParameters *initTempParamsForOO(Loc loc, const char *op)
{
    auto dstringty = new TypeIdentifier(loc, Id::object);
    dstringty->addIdent(Lexer::idPool("string"));

    auto tpl = new TemplateParameters;
    auto tp_specvalue = new StringExp(loc, const_cast<char*>(op));
    tpl->push(new TemplateValueParameter(loc, Lexer::idPool("op"),
                                        dstringty, tp_specvalue, nullptr));

    return tpl;
}

struct IdleTypeDiagnoser : public clang::Sema::TypeDiagnoser
{
    IdleTypeDiagnoser(bool Suppressed = false) : clang::Sema::TypeDiagnoser(Suppressed) {}
    void diagnose(clang::Sema &S, clang::SourceLocation Loc, clang::QualType T) override {}
};

static bool RequireCompleteType(clang::SourceLocation Loc, clang::QualType T)
{
    auto& S = calypso.pch.AST->getSema();
    IdleTypeDiagnoser Diagnoser;

    if (!T->getAs<clang::TagType>())
        return false;

    return S.RequireCompleteType(Loc, T, Diagnoser);
}

namespace {
class FunctionReferencer : public clang::RecursiveASTVisitor<FunctionReferencer>
{
    DeclMapper &mapper;
    clang::Sema &S;
    clang::SourceLocation Loc;

    llvm::DenseSet<const clang::FunctionDecl *> Referenced;

    bool Reference(const clang::FunctionDecl *Callee);
    bool ReferenceRecord(const clang::RecordType *RT);
public:
    FunctionReferencer(DeclMapper &mapper,
                        clang::Sema &S,
                        clang::SourceLocation Loc) : mapper(mapper), S(S), Loc(Loc) {}
    bool VisitCallExpr(const clang::CallExpr *E);
    bool VisitCXXConstructExpr(const clang::CXXConstructExpr *E);
    bool VisitCXXNewExpr(const clang::CXXNewExpr *E);
    bool VisitCXXDeleteExpr(const clang::CXXDeleteExpr *E);
};

bool FunctionReferencer::Reference(const clang::FunctionDecl *D)
{
    auto Callee = const_cast<clang::FunctionDecl*>(D);
    if (!Callee || Callee->getBuiltinID() || Referenced.count(Callee->getCanonicalDecl()))
        return true;
    Referenced.insert(Callee->getCanonicalDecl());

    Callee->setTrivial(false);  // force its definition and Sema to resolve its exception spec
    S.MarkFunctionReferenced(Loc, Callee);

    if (Callee->isImplicitlyInstantiable())
        S.InstantiateFunctionDefinition(Loc, Callee);

    mapper.AddImplicitImportForDecl(Callee);

    const clang::FunctionDecl *Def;
    if (!Callee->hasBody(Def))
        return true;

    TraverseStmt(Def->getBody());
    return true;
}

bool FunctionReferencer::ReferenceRecord(const clang::RecordType *RT)
{
    auto RD = cast<clang::CXXRecordDecl>(RT->getDecl());
    if (!RD->hasDefinition())
        return true;

    mapper.AddImplicitImportForDecl(RD);

    if (!RD->isDependentType())
    {
        if (!RD->isPOD())
        {
            auto Ctor = S.LookupDefaultConstructor(
                            const_cast<clang::CXXRecordDecl *>(RD));
            Reference(Ctor);
        }
        Reference(RD->getDestructor());
    }
    return true;
}

bool FunctionReferencer::VisitCallExpr(const clang::CallExpr *E)
{
    return Reference(E->getDirectCallee());
}

bool FunctionReferencer::VisitCXXConstructExpr(const clang::CXXConstructExpr *E)
{
    auto ConstructedType = E->getType();
    if (!ConstructedType.isNull()) {
        if (const clang::RecordType *RT = ConstructedType->getAs<clang::RecordType>())
            ReferenceRecord(RT);
    }

    return Reference(E->getConstructor());
}

bool FunctionReferencer::VisitCXXNewExpr(const clang::CXXNewExpr *E)
{
    return Reference(E->getOperatorNew());
}

bool FunctionReferencer::VisitCXXDeleteExpr(const clang::CXXDeleteExpr *E)
{
    auto DestroyedType = E->getDestroyedType();
    if (!DestroyedType.isNull()) {
        if (const clang::RecordType *RT = DestroyedType->getAs<clang::RecordType>())
            ReferenceRecord(RT);
    }

    return Reference(E->getOperatorDelete());
}
}

Dsymbols *DeclMapper::VisitFunctionDecl(const clang::FunctionDecl *D)
{
    auto& S = calypso.pch.AST->getSema();

    if (D->isInvalidDecl())
        return nullptr;

    if (isa<clang::CXXConversionDecl>(D))
        return nullptr; // TODO

    if (isa<clang::FunctionNoProtoType>(D->getType()))
        return nullptr; // functions without prototypes are afaik builtins, and since D needs a prototype they can't be mapped

    if (!instantiating && D->isTemplateInstantiation())
        return nullptr;

    auto loc = fromLoc(D->getLocation());

    auto FPT = D->getType()->castAs<clang::FunctionProtoType>();
    auto MD = dyn_cast<clang::CXXMethodDecl>(D);

    // Since Sema never got the chance, do a final check that every type is complete
    // on functions that will be emitted.
    if (!D->getDescribedFunctionTemplate()
            && !D->getDeclContext()->isDependentContext())
    {
        if (RequireCompleteType(D->getLocation(), D->getReturnType()))
            return nullptr;

        for (auto Param: FPT->getParamTypes())
            if (RequireCompleteType(D->getLocation(), Param))
                return nullptr;
    }
    
    auto tf = FromType(*this).fromTypeFunction(FPT, D);
    if (!tf)
    {
        ::warning(loc, "Discarding %s, non-supported argument or return type (e.g int128_t)",
                            D->getDeclName().getAsString().c_str());
        return nullptr;
    }
    assert(tf->ty == Tfunction);

    if (!D->getDeclContext()->isDependentContext())
    {
        auto D_ = const_cast<clang::FunctionDecl*>(D);
        D_->setTrivial(false);  // force its definition and Sema to resolve its exception spec
        S.MarkFunctionReferenced(clang::SourceLocation(), D_);
    }

    const clang::FunctionDecl *Def;
    if (D->hasBody(Def))
        FunctionReferencer(*this, S, clang::SourceLocation()).TraverseStmt(Def->getBody());

    S.PerformPendingInstantiations();

    StorageClass stc = STCundefined;
    if (MD)
    {
        if (MD->isStatic())
            stc |= STCstatic;

        if (!MD->isVirtual())
            stc |= STCfinal;

        if (MD->isPure())
            stc |= STCabstract;

        if (MD->begin_overridden_methods()
                != MD->end_overridden_methods())
            stc |= STCoverride;

        if (isa<clang::CXXDestructorDecl>(D))
            stc &= ~(STCoverride|STCabstract); // dtors aren't added to D's VTBLs
    }
    tf->addSTC(stc);
    
    ::FuncDeclaration *fd;
    if (auto CD = dyn_cast<clang::CXXConstructorDecl>(D))
    {
        fd = new CtorDeclaration(loc, stc, tf, CD);
    }
    else if (auto DD = dyn_cast<clang::CXXDestructorDecl>(D))
    {
        // Destructors are a special case, Clang can only emit a destructor if it's not trivial.
        // The dtor is checked and added by buildDtor during the semantic pass.
        if (DD->isImplicit())
            return nullptr;

        fd = new DtorDeclaration(loc, stc, Id::dtor, DD);
    }
    else if (D->isOverloadedOperator())
    {
        const char *op;
        auto opIdent = getIdentifierOrNull(D, &op); // will return nullptr if the operator isn't supported by D
                            // TODO map the unsupported operators anyway

        if (D->isImplicit() || !opIdent)
            return nullptr;

        // NOTE: C++ overloaded operators might be virtual, unlike D which are always final (being templates)
        //   Mapping the C++ operator to opBinary()() directly would make D lose info and overriding the C++ method impossible

        bool wrapInTemp = (op != nullptr) &&
                    !D->getDescribedFunctionTemplate();  // if it's a templated overloaded operator then the template declaration is already taken care of

        Identifier *fullIdent;
        if (wrapInTemp)
            fullIdent = getExtendedIdentifier(D);
        else
            fullIdent = opIdent;

        // Add the overridable method (or the static function)
        auto a = new Dsymbols;
        fd = new FuncDeclaration(loc, fullIdent, stc, tf, D);
        a->push(fd);

        if (wrapInTemp)
        {
            // Add the opUnary/opBinary/... template declaration
            auto tpl = initTempParamsForOO(loc, op);

            auto a_fwd = new OverloadAliasDeclaration(loc, opIdent,
                                        new TypeIdentifier(loc, fullIdent), tf);

            // NOTE: the previous approach of making a small forwarding function calling fd had one important
            // issue as well in that LDC and the ABIs would need to know how to define function types
            // taking and returning class values. Simpler and safer to let Clang handle those.
            // Adding these specific overload aliases seemed like the cleanest way.

            // Enclose the forwarding function within the template declaration
            auto decldefs = new Dsymbols;
            decldefs->push(a_fwd);

            auto tempdecl = new ::TemplateDeclaration(loc, opIdent, tpl, nullptr, decldefs);
            a->push(tempdecl);
        }

        return a;
    }
    else
    {
        auto id = fromIdentifier(D->getIdentifier());
        fd = new FuncDeclaration(loc, id, stc, tf, D);
    }

    if (D->getTemplateSpecializationKind() == clang::TSK_ExplicitSpecialization &&
            D->getPrimaryTemplate()) // weird, but the explicit instantiation of basic_istream<char>::getline is considered an explicit specialization
    {
        auto tpl = new TemplateParameters;

        auto FT = D->getPrimaryTemplate();
        auto TPL = FT->getTemplateParameters();
        auto AI = D->getTemplateSpecializationArgs()->asArray().begin();

        TempParamScope.push_back(TPL);
        for (auto PI = TPL->begin(), PE = TPL->end();
            PI != PE; PI++)
        {
            auto tp = VisitTemplateParameter(*PI, AI);
            if (!tp)
                return nullptr;
            tpl->push(tp);

            if (AI) AI++;
        }
        TempParamScope.pop_back();

        auto decldefs = new Dsymbols;
        decldefs->push(fd);
        auto td = new TemplateDeclaration(loc, fd->ident, tpl, decldefs, D);
        return oneSymbol(td);
    }

    return oneSymbol(fd);
}

bool isTemplateParameterPack(const clang::NamedDecl *Param)
{
    if (auto NTTPD = dyn_cast<clang::NonTypeTemplateParmDecl>(Param))
        return NTTPD->isParameterPack();
    else if (auto TTPD = dyn_cast<clang::TemplateTypeParmDecl>(Param))
        return TTPD->isParameterPack();
    else if (auto TempTemp = dyn_cast<clang::TemplateTemplateParmDecl>(Param))
        return TempTemp->isParameterPack();

    llvm::llvm_unreachable_internal();
}

Dsymbols *DeclMapper::VisitRedeclarableTemplateDecl(const clang::RedeclarableTemplateDecl *D)
{
    if (!isa<clang::ClassTemplateDecl>(D) && !isa<clang::TypeAliasTemplateDecl>(D)
         && !isa<clang::FunctionTemplateDecl>(D))
        return nullptr; // temporary

    if (auto FTD = dyn_cast<clang::FunctionTemplateDecl>(D))
        if (isa<clang::CXXConversionDecl>(FTD->getTemplatedDecl()))
            return nullptr;

    auto loc = fromLoc(D->getLocation());
    const char *op = nullptr;
    auto id = getIdentifierOrNull(D, &op);

    if (!id)
        return nullptr; // TODO: map unsupported overloaded operators

    auto Def = D;
    if (auto CTD = dyn_cast<clang::ClassTemplateDecl>(D))
        Def = getDefinition(CTD);
    else if (auto FTD = dyn_cast<clang::FunctionTemplateDecl>(D))
        Def = getDefinition(FTD);

    auto tpl = !op ? new TemplateParameters : initTempParamsForOO(loc, op);
    auto TPL = Def->getTemplateParameters();

    TempParamScope.push_back(TPL);

    // FIXME: C++ templates may have multiple parameter packs, which isn't supported by D
    // Since this is a rare occurence ignore them for now
    bool packFound = false;

    for (auto P: *TPL)
    {
        if (packFound)
        {
            ::warning(loc, "Template %s has more than one parameter pack, ignore for now", id->toChars());
            return nullptr;
        }
        packFound = isTemplateParameterPack(P);

        auto tp = VisitTemplateParameter(P);
        if (!tp)
            return nullptr; // should be extremely rare, e.g if there's a int128_t value parameter
        tpl->push(tp);
    }

    auto s = VisitDecl(Def->getTemplatedDecl()->getCanonicalDecl());

    if (!s)
        return nullptr;

    auto decldefs = new Dsymbols;
    decldefs->append(s);

    TempParamScope.pop_back();

    auto a = new TemplateDeclaration(loc, id, tpl, decldefs, D);
    return oneSymbol(a);
}

Identifier *DeclMapper::getIdentifierForTemplateNonTypeParm(const clang::NonTypeTemplateParmDecl *T)
{
    if (auto Id = T->getIdentifier())
        return fromIdentifier(Id);
    else
    {
        ::warning(Loc(), "Generating identifier for anonymous C++ non-type template parameter");

        // This should only ever happen in template param decl mapping
        std::string str;
        llvm::raw_string_ostream OS(str);
        OS << "value_parameter_" << T->getDepth() << '_' << T->getIndex();

        return Lexer::idPool(OS.str().c_str());
    }
}

TemplateParameter *DeclMapper::VisitTemplateParameter(const clang::NamedDecl *Param,
                                                                      const clang::TemplateArgument *SpecArg)
{
    ExprMapper expmap(*this);
    TemplateParameter *tp;

    auto loc = fromLoc(Param->getLocation());
    Identifier *id;

    if (auto NTTPD =
            dyn_cast<clang::NonTypeTemplateParmDecl>(Param))
    {
        id = getIdentifierForTemplateNonTypeParm(NTTPD);
        auto valTy = fromType(NTTPD->getType());

        if (!valTy || isNonSupportedType(NTTPD->getType()))
            return nullptr;

        if (NTTPD->isParameterPack())
        {
            ::warning(Loc(), "%s", "C++ template non-type parameter packs do not strictly map to D tuple parameters");
            return new TemplateTupleParameter(loc, id);
        }
        else
        {
            Expression *tp_specvalue = nullptr;
            Expression *tp_defaultvalue = nullptr;

            if (SpecArg)
            {
                switch (SpecArg->getKind())
                {
                    case clang::TemplateArgument::Expression:
                        tp_specvalue = expmap.fromExpression(SpecArg->getAsExpr());
                        break;
                    case clang::TemplateArgument::Integral:
                        tp_specvalue = expmap.fromAPInt(SpecArg->getAsIntegral());
                        break;
                    case clang::TemplateArgument::NullPtr:
                        tp_specvalue = new NullExp(Loc()/*, fromType(SpecArg->getNullPtrType())*/);
                        break;
                    default:
                        assert(false && "Unsupported template specialization value");
                }

                tp_specvalue = isExpression(FromType(*this).fromTemplateArgument(SpecArg));
                assert(tp_specvalue);
            }

            if (NTTPD->hasDefaultArgument())
            {
//                 tp_defaultvalue = expmap.fromExpression(NTTPD->getDefaultArgument());

                // TEMPORARY HACK: we choose a simple default value to make defaultArg()'s life easier
                //  (there were obscure identifier errors, e.g in __iterator_traits<_normal_iterator>).
                //  This doesn't affect anything other than reflection since the default argument evaluation is done by Clang
                //  We could btw use Clang's evaluation while keeping the mapped expression for reflection
                tp_defaultvalue = new DotIdExp(loc, new TypeExp(loc, valTy), Id::init);
            }

            tp = new TemplateValueParameter(loc, id, valTy,
                                        tp_specvalue, tp_defaultvalue);
        }

    }
    else if (auto TTPD =
            dyn_cast<clang::TemplateTypeParmDecl>(Param))
    {
        id = getIdentifierForTemplateTypeParm(
                cast<clang::TemplateTypeParmType>(TTPD->getTypeForDecl()));

        if (TTPD->isParameterPack())
        {
            ::warning(Loc(), "%s", "C++ template type parameter packs do not strictly map to D tuple parameters");
            return new TemplateTupleParameter(loc, id);
        }
        else
        {
            Type *tp_spectype = nullptr;
            Type *tp_defaulttype = nullptr;

            if (SpecArg)
            {
                auto SpecTy = SpecArg->getAsType();
                auto specArg = FromType(*this).fromTemplateArgument(SpecArg);
                if (!specArg || isNonSupportedType(SpecTy))
                    return nullptr; // might be a non supported type
                tp_spectype = isType(specArg);
                assert(tp_spectype);
            }

            if (TTPD->hasDefaultArgument())
                tp_defaulttype = fromType(TTPD->getDefaultArgument());

            tp = new TemplateTypeParameter(loc, id, tp_spectype, tp_defaulttype);
        }
    }
    else if (auto TempTemp =
            dyn_cast<clang::TemplateTemplateParmDecl>(Param))
    {
        id = getIdentifierForTemplateTemplateParm(TempTemp);

        if (TempTemp->isParameterPack())
        {
            ::warning(Loc(), "%s", "C++ template template parameter packs do not strictly map to D tuple parameters");
            return new TemplateTupleParameter(loc, id);
        }
        else
        {
            Type *tp_spectype = nullptr;
            Type *tp_defaulttype = nullptr;

            if (SpecArg)
            {
                tp_spectype = isType(FromType(*this).fromTemplateArgument(SpecArg));
                assert(tp_spectype);
            }

            if (TempTemp->hasDefaultArgument())
                tp_defaulttype = FromType(*this).fromTemplateName(
                        TempTemp->getDefaultArgument().getArgument().getAsTemplate());

            tp = new TemplateAliasParameter(loc, id, nullptr, tp_spectype, tp_defaulttype);
        }
    }
    else assert(false && "unrecognized template parameter");

    return tp;
}

Dsymbol *DeclMapper::VisitInstancedClassTemplate(const clang::ClassTemplateSpecializationDecl *D,
                                                 unsigned flags)
{
    assert(!isa<clang::ClassTemplatePartialSpecializationDecl>(D));

    instantiating = true; // FIXME redundant with the DeclMapper ctor
    rebuildScope(cast<clang::Decl>(D->getDeclContext()));
    pushTempParamList(D);

    auto a = VisitRecordDecl(D, flags);
    assert(a->dim);
    return (*a)[0];
}

Dsymbol *DeclMapper::VisitInstancedFunctionTemplate(const clang::FunctionDecl *D)
{
    instantiating = true;
    rebuildScope(cast<clang::Decl>(D->getDeclContext()));
    pushTempParamList(D);

    auto a = VisitFunctionDecl(D);
    assert(a->dim);
    return (*a)[0];
}

// Explicit specializations only
Dsymbols *DeclMapper::VisitClassTemplateSpecializationDecl(const clang::ClassTemplateSpecializationDecl *D)
{   TemplateDeclaration *a;

    D = getDefinition(D);

    if (!D->isExplicitSpecialization())
        return nullptr;

    auto Partial = dyn_cast<clang::ClassTemplatePartialSpecializationDecl>(D);
        // NOTE: D's partial specializations != C++'s partial specializations
        // The mapping provides a "close" but not exact approximation of equivalent template specs in D (for reflection),
        // but TemplateDeclaration::findBestMatch is skipped since the choice is done by Clang anyway.

    auto loc = fromLoc(D->getLocation());
    auto id = fromIdentifier(D->getIdentifier());

    auto tpl = new TemplateParameters;

    auto CT = getDefinition(D->getSpecializedTemplate());
    auto TPL = CT->getTemplateParameters();
    auto AI = D->getTemplateArgs().asArray().begin();

    if (Partial)
    {
        TPL = Partial->getTemplateParameters(); // SEMI-HACK #1 because we alter the tiargs to match the partial spec params
        AI = nullptr; // SEMI-HACK #2 because partial spec args won't matter during semantic
    }
    TempParamScope.push_back(TPL);

    for (auto PI = TPL->begin(), PE = TPL->end();
        PI != PE; PI++)
    {
        auto tp = VisitTemplateParameter(*PI, AI);
        if (!tp)
            return nullptr;
        tpl->push(tp);

        if (AI) AI++;
    }

    auto decldefs = new Dsymbols;
    auto ad = VisitRecordDecl(D);
    decldefs->append(ad);

    TempParamScope.pop_back();

    a = new TemplateDeclaration(loc, id, tpl, decldefs, D);
    return oneSymbol(a);
}

Dsymbols *DeclMapper::VisitEnumDecl(const clang::EnumDecl* D)
{
    if (!D->isCompleteDefinition())
    {
        D = D->getDefinition();
        if (!D)
            return nullptr; // forward declaration without definition, skip
    }

    auto loc = fromLoc(D->getLocation());
    auto ident = getIdentifierOrNull(D);

    Type *memtype = nullptr;
    if (!D->isDependentType())
    {
        auto IntType = D->getIntegerType();
        if (IntType.isNull())
            IntType = D->getPromotionType();

        memtype = fromType(IntType);
    }

    auto e = new EnumDeclaration(loc, ident, memtype, D);
    CXXScope.push(D);

    for (auto ECD: D->enumerators())
    {
        auto memberLoc = fromLoc(ECD->getLocation());

        if (!e->members)
            e->members = new Dsymbols;

        auto ident = fromIdentifier(ECD->getIdentifier());
        Expression *value = nullptr;

        if (auto InitE = ECD->getInitExpr())
        {
            value = ExprMapper(*this).fromExpression(InitE);
            value = new CastExp(memberLoc, value, memtype); // SEMI-HACK (?) because the type returned by 1LU << ... will be ulong and we may need an int (see wctype.h)
        }

        auto em = new EnumMember(memberLoc, ident, value, nullptr);
        e->members->push(em);
    }

    CXXScope.pop();
    return oneSymbol(e);
}

/*****/

std::string moduleName(Identifiers *packages, Identifier *ident)
{
    std::string result = "__cpp/";
    for (size_t i = 1; i < packages->dim; i++)
    {
        Identifier *pid = (*packages)[i];
        result.append(pid->string, pid->len);
        result.append("/");
    }
    result.append(ident->string, ident->len);
    return result;
}

// Look into namespace redecls if there are any
static clang::DeclContext::lookup_const_result lookup(const clang::DeclContext *DC,
                                                   Identifier *id)
{
    auto& AST = calypso.pch.AST;
    auto& Table = AST->getPreprocessor().getIdentifierTable();

    const char prefix[] = u8"§";
    bool prefixed = strncmp(id->string, prefix, sizeof(prefix)-1) == 0;
    auto& II = Table.get(!prefixed ? id->string : id->string + sizeof(prefix)-1);

    return DC->lookup(clang::DeclarationName(&II));
}

const clang::NamedDecl *isOverloadedOperatorWithTagOperand(const clang::Decl *D,
                                const clang::NamedDecl *SpecificTag)
{
    auto& Context = calypso.getASTContext();

    auto Func = dyn_cast<clang::FunctionDecl>(D);
    if (auto FuncTemp = dyn_cast<clang::FunctionTemplateDecl>(D))
        Func = FuncTemp->getTemplatedDecl();

    if (!Func || !Func->isOverloadedOperator())
        return nullptr;

    if (Func->isFunctionTemplateSpecialization())
        Func = Func->getPrimaryTemplate()->getTemplatedDecl();

    assert(Func->getNumParams() > 0);
    if (Func->getNumParams() > 2)
        return nullptr; // [] and () cannot be non-member (FIXME: not entirely sure about (), couldn't find the source)

    const clang::NamedDecl *OpTyDecl = nullptr;

    for (unsigned I = 0; I < Func->getNumParams(); I++)
    {
        auto ParamTy = Func->getParamDecl(I)->getType().getNonReferenceType()
                                .getDesugaredType(Context).getCanonicalType();
        auto Ty = ParamTy.getTypePtr();

        if (auto TagTy = dyn_cast<clang::TagType>(Ty))
        {
            OpTyDecl = TagTy->getDecl();
            break;
        }

        if (auto TempSpec = dyn_cast<clang::TemplateSpecializationType>(Ty))
            if (auto Temp = TempSpec->getTemplateName().getAsTemplateDecl())
            {
                if (isa<clang::ClassTemplateDecl>(Temp))
                {
                    OpTyDecl = Temp;
                    break;
                }

                if (isa<clang::TypeAliasTemplateDecl>(Temp))
                    assert(false && "Wrongly assumed that it would desugared");
            }
    }

    if (OpTyDecl && // either LHS or RHS has a tag type
          (!SpecificTag || OpTyDecl->getCanonicalDecl() == SpecificTag->getCanonicalDecl())) // if we're looking for a specific type, compare it
        return OpTyDecl; 

    return nullptr;
}

static clang::Module *tryFindClangModule(Loc loc, Identifiers *packages, Identifier *id,
                                         Package *&p, size_t i)
{
#ifdef USE_CLANG_MODULES
    auto MMap = calypso.pch.MMap;

    if (!MMap)
        return nullptr;

    Package *pkg = p;
    clang::Module *M = nullptr;

    for (; i < packages->dim; i++)
    {
        Identifier *pid = (*packages)[i];

        llvm::StringRef name(pid->string, pid->len);
        M = MMap->lookupModuleQualified(name, M);
        if (!M)
            return nullptr;

        pkg = static_cast<Package*>(pkg->symtab->lookup(pid));
        assert(pkg);
    }

    llvm::StringRef name(id->string, id->len);
    M = MMap->lookupModuleQualified(name, M);
    if (!M)
        return nullptr;

    p = pkg;
    return M;
#else
    return nullptr;
#endif
}

static inline bool isTopLevelInNamespaceModule (const clang::Decl *D)
{
    auto Tag = dyn_cast<clang::TagDecl>(D);
    if (Tag && (Tag->getIdentifier() || Tag->getTypedefNameForAnonDecl()))
        return false; // anonymous tags are added as well

    auto Func = dyn_cast<clang::FunctionDecl>(D);
    if (Func && Func->getDescribedFunctionTemplate())
        return false; // the function template will get mapped instead

    if (!Tag && !Func && !isa<clang::VarDecl>(D) &&
            !isa<clang::TypedefNameDecl>(D) &&
            !isa<clang::FunctionTemplateDecl>(D) &&
            !isa<clang::TypeAliasTemplateDecl>(D))
        return false;

    if (isOverloadedOperatorWithTagOperand(D))
        return false;  // non-member overloaded operators with class/struct/enum operands are included in their own module

    return true;
}

static void mapNamespace(DeclMapper &mapper,
                             const clang::DeclContext *DC,
                             Dsymbols *members,
                             bool forClangModule = false)
{
    auto CanonDC = cast<clang::Decl>(DC)->getCanonicalDecl();
    auto MMap = calypso.pch.MMap;
    auto& SrcMgr = calypso.getASTUnit()->getSourceManager();

    auto D = DC->decls_begin(),
            DE = DC->decls_end();

    for (; D != DE; ++D)
    {
        if (cast<clang::Decl>(D->getDeclContext())->getCanonicalDecl()
                != CanonDC)
            continue;  // only map declarations that are semantically within the DeclContext

        auto DLoc = SrcMgr.getFileLoc((*D)->getLocation());
#ifdef USE_CLANG_MODULES
        if (!forClangModule && DLoc.isValid() && DLoc.isFileID()
                && MMap->findModuleForHeader(
                    SrcMgr.getFileEntryForID(SrcMgr.getFileID(DLoc))))
            continue;  // skip decls which are parts of a Clang module
#endif

        if (auto LinkSpec = dyn_cast<clang::LinkageSpecDecl>(*D))
        {
            mapNamespace(mapper, LinkSpec, members, forClangModule);
            continue;
        }
        else if (!isTopLevelInNamespaceModule(*D))
            continue;

        if (auto s = mapper.VisitDecl(*D))
            members->append(s);
    }
}

static void mapClangModule(DeclMapper &mapper,
                             const clang::Decl *Root,
                             clang::Module *M,
                             Dsymbols *members)
{
    auto AST = calypso.getASTUnit();
    auto& SrcMgr = AST->getSourceManager();

    llvm::SmallVector<clang::Decl*, 32> RegionDecls;

    // HACK-ish but Clang doesn't offer a straightforward way
    // SourceManager::translateFile() only offers the first FID of a FileEntry which doesn't contain all the decls,
    // so we need to loop over all the FID corresponding to Header.Entry.
    for (unsigned I = 0, N = SrcMgr.loaded_sloc_entry_size(); I != N; ++I)
    {
        auto& SLoc = SrcMgr.getLoadedSLocEntry(I);
        if (SLoc.isFile() && SLoc.getFile().getContentCache())
        {
            for (auto Header: M->Headers[clang::Module::HK_Normal])
            {
                if (SLoc.getFile().getContentCache()->OrigEntry != Header.Entry)
                    continue;

                auto Loc = clang::SourceLocation::getFromRawEncoding(SLoc.getOffset());
                auto FID = SrcMgr.getFileID(Loc); // NOTE: getting a FileID without a SourceLocation is impossible, it's locked tight
                auto SID = SrcMgr.getLocForStartOfFile(FID).getRawEncoding();
                AST->findFileRegionDecls(FID, 0, (1U << 31) - 1 - SID, RegionDecls); // passed Length is the maximum value before offset overflow kicks in
            }
        }
    }

    // Not forgetting namespace redecls
    llvm::SmallVector<clang::Decl*, 8> RootDecls, ParentDecls;

    std::function<bool(const clang::Decl *)>
        Corresponding = [&] (const clang::Decl *D)
    {
        if (isa<clang::TranslationUnitDecl>(D))
            return true;

        if (!Corresponding(cast<clang::Decl>(D->getDeclContext())->getCanonicalDecl()))
            return false;

        ParentDecls.swap(RootDecls);
        RootDecls.clear();

        if (!ParentDecls.empty())
            for (auto Parent: ParentDecls)
            {
                auto DC = cast<clang::DeclContext>(Parent);
                for (auto ModDecl: DC->decls())
                    if (D->getCanonicalDecl() == ModDecl->getCanonicalDecl())
                        RootDecls.push_back(ModDecl);
            }
        else
            for (auto ModDecl: RegionDecls)
                if (isa<clang::TranslationUnitDecl>(D->getDeclContext()))
                    if (D->getCanonicalDecl() == ModDecl->getCanonicalDecl())
                        RootDecls.push_back(ModDecl);

        return !RootDecls.empty();
    };

    if (!Corresponding(Root))
    {
        ::error(Loc(), "Incorrect import, Clang module doesn't contain requested namespaces");
        fatal();
    }

    std::function<void(const clang::Decl *)> Map = [&] (const clang::Decl *D)
    {
        if (auto LinkSpec = dyn_cast<clang::LinkageSpecDecl>(D))
        {
            for (auto LD: LinkSpec->decls())
                Map(LD);
            return;
        }

        auto DC = D->getDeclContext();
        assert(isa<clang::TranslationUnitDecl>(DC)
                || isa<clang::NamespaceDecl>(DC)
                || isa<clang::LinkageSpecDecl>(DC));

        if (!isTopLevelInNamespaceModule(D))
            return;

        if (auto s = mapper.VisitDecl(D))
            members->append(s);
    };

    if (!isa<clang::TranslationUnitDecl>(Root))
        for (auto R: RootDecls)
            mapNamespace(mapper, cast<clang::DeclContext>(R), members, true);
    else
        for (auto D: RegionDecls)
            if (isa<clang::TranslationUnitDecl>(D->getDeclContext()))
                Map(D);
}

Module *Module::load(Loc loc, Identifiers *packages, Identifier *id)
{
    auto& Context = calypso.getASTContext();
    auto& S = calypso.pch.AST->getSema();
    auto& Diags = calypso.pch.Diags;

    S.CurContext = Context.getTranslationUnitDecl(); // HACK? Needed for declaring implicit ctors and dtors
    if (!S.TUScope)
        // Clang BUG? TUScope isn't set when no Parser is used, but required by template instantiations (e.g LazilyCreateBuiltin)
        S.TUScope = new clang::Scope(nullptr, clang::Scope::DeclScope, *Diags);

    const clang::DeclContext *DC = Context.getTranslationUnitDecl();
    Package *pkg = rootPackage;

    assert(packages && packages->dim);

    clang::Module *M = nullptr;
    for (size_t i = 1; i < packages->dim; i++)
    {
        Identifier *pid = (*packages)[i];

        auto R = lookup(DC, pid);
        if (R.empty())
        {
            // Check if there's a Clang module matching the remaining packages.module.
            // Note that if there is a Clang module named QtCore, import Qt.QtCore is correct and
            // will import the declarations inside Qt:: and inside the headers listed in QtCore.
            M = tryFindClangModule(loc, packages, id, pkg, i);
            if (M)
                break;

            ::error(loc, "no C++ package named %s", pid->toChars());
            fatal();
        }

        auto NSN = dyn_cast<clang::NamespaceDecl>(R[0]);
        if (!NSN)
        {
            ::error(loc, "only namespaces can be C++ packages");
            fatal();
        }

        pkg = static_cast<Package*>(pkg->symtab->lookup(pid));
        assert(pkg);

        DC = NSN;
    }

    if (!M)
        M = tryFindClangModule(loc, packages, id, pkg, packages->dim);

    auto m = new Module(moduleName(packages, id).c_str(),
                        id, packages);
    m->members = new Dsymbols;
    m->parent = pkg;
    m->loc = loc;

    DeclMapper mapper(m);

    if (M)
    {
        auto D = cast<clang::Decl>(DC)->getCanonicalDecl();
        m->rootKey.first = D;
        m->rootKey.second = M;
        mapClangModule(mapper, D, M, m->members);
    }
    else if (strcmp(id->string, "_") == 0)  // Hardcoded module with all the top-level non-tag decls + the anonymous tags of a namespace which aren't in a Clang module
    {
        m->rootKey.first = cast<clang::Decl>(DC)->getCanonicalDecl();

        auto NS = dyn_cast<clang::NamespaceDecl>(DC);
        if (!NS)
        {
            assert(isa<clang::TranslationUnitDecl>(DC));

#if 1
            mapNamespace(mapper, DC, m->members);
#endif
        }
        else
        {
            auto I = NS->redecls_begin(),
                    E = NS->redecls_end();

            for (; I != E; ++I)
            {
                DC = *I;
                mapNamespace(mapper, DC, m->members);
            }
        }
    }
    else
    {
        clang::NamedDecl *D = nullptr;

        // Lookups can't find the implicit __va_list_tag record
        if (id == Lexer::idPool("__va_list_tag") && packages->dim == 1)
        {
            D = Context.getVaListTagType()
                ->getAs<clang::RecordType>()->getDecl();
        }
        else
        {
            auto R = lookup(DC, id);
            if (R.empty())
            {
                ::error(loc, "no C++ module named %s", id->toChars());
                fatal();
            }

            // Module must be a record or enum
            for (auto Match: R)
            {
                if (auto Typedef = dyn_cast<clang::TypedefNameDecl>(Match))
                {
                    auto UT = Typedef->getUnderlyingType().getDesugaredType(Context);
                    if (auto RT = dyn_cast<clang::TagType>(UT))
                        Match = RT->getDecl();
                }

                if (isa<clang::TagDecl>(Match) || isa<clang::ClassTemplateDecl>(Match))
                    D = Match;
            }

            if (!D)
            {
                ::error(loc, "C++ modules have to be records (class/struct, template or not) or enums");
                fatal();
            }
        }

        if (auto Spec = dyn_cast<clang::ClassTemplateSpecializationDecl>(D))
            D = Spec->getSpecializedTemplate();

        D = cast<clang::NamedDecl>(D->getCanonicalDecl());
        auto CTD = dyn_cast<clang::ClassTemplateDecl>(D);

        if (CTD)
            m->rootKey.first = CTD->getTemplatedDecl();
        else
            m->rootKey.first = D;

        if (auto s = mapper.VisitDecl(D, DeclMapper::MapImplicit))
            m->members->append(s);

        // Special case for class template, we need to add explicit specializations to the module as well
        if (CTD)
        {
            llvm::SmallVector<clang::ClassTemplatePartialSpecializationDecl *, 2> PS;
            CTD->getPartialSpecializations(PS);

            for (auto PartialSpec: PS)
                if (auto s = mapper.VisitDecl(PartialSpec->getCanonicalDecl()))
                    m->members->append(s);

            for (auto Spec: CTD->specializations())
                if (auto s = mapper.VisitDecl(Spec->getCanonicalDecl()))
                    m->members->append(s);
        }

        // Add the non-member overloaded operators that are meant to work with this record/enum
        for (int Op = 1; Op < clang::NUM_OVERLOADED_OPERATORS; Op++)
        {
            auto OpName = Context.DeclarationNames.getCXXOperatorName(
                        static_cast<clang::OverloadedOperatorKind>(Op));
            auto Operators = getDeclContextNamedOrTU(D)->lookup(OpName);

            // WARNING: lookups will only search in the top-most namespace for overloaded operators
            // Can non-member operators be located in a parent namespace or the TU?
            // Never seen that happening but if it's allowed then it's a FIXME

            for (auto OverOp: Operators)
                if (isOverloadedOperatorWithTagOperand(OverOp, D))
                    if (auto s = mapper.VisitDecl(OverOp))
                        m->members->append(s);
        }

//         srcFilename = AST->getSourceManager().getFilename(TD->getLocation());
    }
    
    amodules.push_back(m);
    pkg->symtab->insert(m);
    return m;
}

}
