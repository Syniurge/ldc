#include "cpp/calypso.h"
#include "cpp/cppexpression.h"
#include "cpp/cppimport.h"
#include "cpp/cppmodule.h"
#include "cpp/cpptemplate.h"
#include "module.h"
#include "template.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/Type.h"
#include "clang/Frontend/ASTUnit.h"
#include "clang/Sema/Sema.h"

namespace cpp
{

using llvm::cast;
using llvm::dyn_cast;
using llvm::isa;

void BuiltinTypes::map(clang::CanQualType &CQT, Type* t)
{
    auto T = CQT.getTypePtr()->castAs<clang::BuiltinType>();

    toD[T] = t;
    toClang[t] = T;
}

void BuiltinTypes::build(clang::ASTContext &Context)
{
    auto& targetInfo = Context.getTargetInfo();

        //===- Void -----------------------------------------------------===//
    map(Context.VoidTy, Type::tvoid);

        //===- Unsigned Types -----------------------------------------------------===//
    map(Context.BoolTy, Type::tbool);     // Clang assumes that bool means unsigned 8 bits
    map(Context.CharTy, Type::tchar);
    map(Context.UnsignedCharTy, Type::tuns8);    // getCharWidth() always returns 8

    clang::TargetInfo::IntType wcharTy = targetInfo.getWCharType();
    if (targetInfo.getTypeWidth(wcharTy) == 16)
        map(Context.WCharTy, Type::twchar);
    else
        map(Context.WCharTy, Type::tdchar);

    map(Context.Char16Ty, toInt(targetInfo.getChar16Type()));
    map(Context.Char32Ty, toInt(targetInfo.getChar32Type()));
    map(Context.UnsignedShortTy, toInt(clang::TargetInfo::UnsignedShort));
    map(Context.UnsignedIntTy, toInt(clang::TargetInfo::UnsignedInt));
    map(Context.UnsignedLongTy, toInt(clang::TargetInfo::UnsignedLong));
    map(Context.UnsignedLongLongTy, toInt(clang::TargetInfo::UnsignedLongLong));
    map(Context.UnsignedInt128Ty, Type::tuns128);

        //===- Signed Types -------------------------------------------------------===//
//     map(Context.CharTy, Type::tint8);
    map(Context.SignedCharTy, Type::tint8);
//     map(Context.WCharTy, toInt(targetInfo.getWIntType()));
    map(Context.ShortTy, toInt(clang::TargetInfo::SignedShort));
    map(Context.IntTy, toInt(clang::TargetInfo::SignedInt));
    map(Context.LongTy, toInt(clang::TargetInfo::SignedLong));
    map(Context.LongLongTy, toInt(clang::TargetInfo::SignedLongLong));
    map(Context.Int128Ty, Type::tint128);

        //===- Floating point types -----------------------------------------------===//
    map(Context.FloatTy, Type::tfloat32);
    map(Context.DoubleTy, Type::tfloat64);
    map(Context.LongDoubleTy, Type::tfloat80);

        //===- Language-specific types --------------------------------------------===//
    map(Context.NullPtrTy, Type::tnull); // or is tvoidptr?
}

// Most reliable way to determine target-dependent int type correspondances (except for char)
Type *BuiltinTypes::toInt(clang::TargetInfo::IntType intTy)
{
    auto& targetInfo = calypso.getASTContext().getTargetInfo();

    auto width = targetInfo.getTypeWidth(intTy);
    if (clang::TargetInfo::isTypeSigned(intTy))
    {
        switch(width)
        {
            case 8:
                return Type::tint8;
            case 16:
                return Type::tint16;
            case 32:
                return Type::tint32;
            case 64:
                return Type::tint64;
            case 128:
                return Type::tint128;
        }
    }
    else
    {
        switch(width)
        {
            case 8:
                return Type::tuns8;
            case 16:
                return Type::tuns16;
            case 32:
                return Type::tuns32;
            case 64:
                return Type::tuns64;
            case 128:
                return Type::tuns128;
        }
    }

    assert(false && "unexpected int type size");
    return nullptr;
}

/***** Type mapping *****/

Type *TypeMapper::toType(const clang::QualType T)
{
    Type *t = toTypeUnqual(T.getTypePtr());

    if (T.isConstQualified())
        t = t->makeConst();

    if (T.isVolatileQualified())
    {
        ::warning(Loc(), "volatile qualifier found, declaration won't be exposed (fixme?)");
        return nullptr;
    }

    // restrict qualifiers are inconsequential

    return t;
}

Type *TypeMapper::toTypeUnqual(const clang::Type *T)
{
    if (auto BT = dyn_cast<clang::BuiltinType>(T))
        return toTypeBuiltin(BT);
    else if (auto CT = T->getAs<clang::ComplexType>())
        return toTypeComplex(CT);

    if (auto FT = dyn_cast<clang::FunctionProtoType>(T))
        return toTypeFunction(FT);

    // Purely cosmetic sugar types
    if (auto PT = dyn_cast<clang::ParenType>(T))
        return toType(PT->desugar());

#define TYPEMAP(Ty) \
    if (auto Ty##T = dyn_cast<clang::Ty##Type>(T)) \
        return toType##Ty(Ty##T);

    TYPEMAP(Typedef)
    TYPEMAP(Enum)
    TYPEMAP(Record)
    TYPEMAP(Elaborated)
    TYPEMAP(TemplateSpecialization)
    TYPEMAP(TemplateTypeParm)
    TYPEMAP(SubstTemplateTypeParm)
    TYPEMAP(InjectedClassName)
    TYPEMAP(Adjusted)
    TYPEMAP(DependentName)
    TYPEMAP(DependentTemplateSpecialization)
    TYPEMAP(Decltype)
#undef TYPEMAP

        // NOTE: the C++ classes don't exactly map to D classes, but we can work
        // around that:
        //  - if a C++ function has an argument taking a class, the value will be dereferenced
        //  - if a variable of a class type is exposed, it's ok to use DotVarExp, but DtoLoad will be skipped.
        //  - if a C++ function returns an object of a class, make the GC acquire it somehow.

    // Array types
    if (auto AT = dyn_cast<clang::ArrayType>(T))
        return toTypeArray(AT);

    // Pointer and reference types
    bool isPointer = isa<clang::PointerType>(T),
            isReference = isa<clang::ReferenceType>(T);

    if (isPointer || isReference)
    {
        auto pointeeT = T->getPointeeType();
        auto pt = toType(pointeeT);

        if (isPointer)
            return pt->pointerTo();
        else
            return isNonPODRecord(pointeeT) ? pt : pt->referenceTo();  // special case for classes
    }

    llvm::llvm_unreachable_internal("Unrecognized C++ type");
}


Type *TypeMapper::toTypeBuiltin(const clang::BuiltinType *T)
{
    auto t = calypso.builtinTypes.toD[T];

    assert(t && "missing built-in type correspondance");
    return t;
}

Type *TypeMapper::toTypeComplex(const clang::ComplexType *T)
{
    auto& Context = calypso.pch.AST->getASTContext();
    auto dT = T->desugar();

    if (dT == Context.FloatComplexTy)
        return Type::tcomplex32;
    else if (dT == Context.DoubleComplexTy)
        return Type::tcomplex64;
    else if (dT == Context.LongDoubleComplexTy)
        return Type::tcomplex80;

    assert(false && "unknown complex number type");
    return nullptr;
}

Type* TypeMapper::toTypeArray(const clang::ArrayType* T)
{
    auto t = toType(T->getElementType());

    if (auto CAT = dyn_cast<clang::ConstantArrayType>(T))
    {
        auto dim = new IntegerExp(CAT->getSize().getLimitedValue());
        return new TypeSArray(t, dim);
    }
    else if (auto DSAT = dyn_cast<clang::DependentSizedArrayType>(T))
    {
        auto dim = ExprMapper(*this).toExpression(DSAT->getSizeExpr());
        return new TypeSArray(t, dim);
    }
    else if (auto IAT = dyn_cast<clang::IncompleteArrayType>(T))
    {
        return t->pointerTo();
    }

    llvm::llvm_unreachable_internal("Unrecognized C++ array type");
}

RootObject* TypeMapper::toTemplateArgument(const clang::TemplateArgument* Arg,
                const clang::NamedDecl *Param)
{
    ExprMapper expmap(*this);

    RootObject *tiarg = nullptr;
    switch (Arg->getKind())
    {
        case clang::TemplateArgument::Expression:
            tiarg = expmap.toExpression(Arg->getAsExpr());
            break;
        case clang::TemplateArgument::Integral:
        {
            auto e = APIntToExpression(Arg->getAsIntegral());

            // In Clang AST enum values in template arguments are resolved to integer literals
            // If the parameter has an enum type, we need to revert integer literals to DeclRefs pointing to enum constants
            // or else DMD won't find the template decl since from its point of view uint != Enum
            if (auto NTTP = llvm::dyn_cast_or_null<clang::NonTypeTemplateParmDecl>(Param))
            {
                if (auto ET = dyn_cast<clang::EnumType>(NTTP->getType()))
                {
                    bool found = false;
                    for (auto ECD: ET->getDecl()->enumerators())
                    {
                        auto Val = ECD->getInitVal().getZExtValue();

                        if (Val == ((IntegerExp *)e)->getInteger())
                        {
                            found = true;
                            e = expmap.toExpressionDeclRef(Loc(), ECD);
                        }
                    }

                    assert(found && "Couldn't find the corresponding enum constant for template argument");
                }
            }
            tiarg = e;
            break;
        }
        case clang::TemplateArgument::NullPtr:
            tiarg = new NullExp(Loc()/*, toType(Arg->getNullPtrType())*/);
            break;
        case clang::TemplateArgument::Type:
            tiarg = toType(Arg->getAsType());
            break;
        default:
            assert(false && "Unsupported template arg kind");
    }

    assert(tiarg && "Template argument not supported");
    return tiarg;
}

Objects* TypeMapper::toTemplateArguments(const clang::TemplateArgument *First,
                                        const clang::TemplateArgument *End,
                                        const clang::TemplateDecl *TD)
{
    auto tiargs = new Objects;
    auto Param = TD ? TD->getTemplateParameters()->begin() : nullptr;

    for (auto Arg = First; Arg != End; Arg++)
    {
        auto P = Param ? *Param : nullptr;
        tiargs->push(toTemplateArgument(Arg, P));

        if (TD)
            Param++;
    }

    return tiargs;
}

void TypeQualifiedBuilder::addInst(TypeQualified *&tqual,
                clang::NamedDecl* D,
                const clang::TemplateArgument *TempArgBegin,
                const clang::TemplateArgument *TempArgEnd)
{
    auto& Context = calypso.pch.AST->getASTContext();
    auto& S = calypso.pch.AST->getSema();

    auto ident = getIdentifier(D);
    auto loc = toLoc(D->getLocation());
    ::Module *instantiatingModuleCpp = nullptr;
    auto CTD = dyn_cast<clang::ClassTemplateDecl>(D);
    auto CTSD = dyn_cast<clang::ClassTemplateSpecializationDecl>(D);

    if (CTSD && !CTSD->hasDefinition())
    {
        auto Ty = Context.getRecordType(CTSD);

        if (S.RequireCompleteType(CTSD->getLocation(), Ty, 0))
            assert(false && "Sema::RequireCompleteType() failed on template specialization");

        instantiatingModuleCpp = tm.mod;  // if the definition of the class template specialization wasn't present in the PCH there's a chance the code wasn't emitted in the C++ libraries, so we do it ourselves
        assert(instantiatingModuleCpp);
    }

    auto tiargs = tm.toTemplateArguments(TempArgBegin, TempArgEnd,
            CTSD ? CTSD->getSpecializedTemplate() : CTD);

    auto tempinst = new cpp::TemplateInstance(loc, ident,
                                        CTSD, instantiatingModuleCpp); // " HACK ": 3rd arg is the temporary trick to avoid begging Sema to instanciate not declared template specs
    tempinst->tiargs = tiargs;

    if (!tqual)
        tqual = new TypeInstance(Loc(), tempinst);
    else
        tqual->addInst(tempinst);
}

TypeQualified *TypeQualifiedBuilder::get(clang::NamedDecl* ND)
{
    const clang::Decl *DCDecl = ND;
    if (auto CTSD = dyn_cast<clang::ClassTemplateSpecializationDecl>(ND))
        DCDecl = CTSD->getSpecializedTemplate();
    else if (!isa<clang::TagDecl>(ND) && !isa<clang::ClassTemplateDecl>(ND))
        DCDecl = cast<clang::Decl>(ND->getDeclContext());
    DCDecl = DCDecl->getCanonicalDecl();

    TypeQualified *tqual;
    if (DCDecl == Root)
        tqual = nullptr;
    else
    {
        auto ParentDecl = cast<clang::Decl>(
                ND->getDeclContext())->getCanonicalDecl();
        tqual = get(
                cast<clang::NamedDecl>(ParentDecl));
    }

    auto ident = getIdentifierOrNull(ND);
    if (!ident)
        return tqual;

    if (auto CTD = dyn_cast<clang::ClassTemplateDecl>(ND))
        addInst(tqual, CTD, TopTempArgBegin, TopTempArgEnd);
    if (auto CTSD = dyn_cast<clang::ClassTemplateSpecializationDecl>(ND))
    {
        auto TempArgs = CTSD->getTemplateArgs().asArray();
        addInst(tqual, CTSD, TempArgs.begin(), TempArgs.end());
    }
    else
    {
        if (!tqual)
            tqual = new TypeIdentifier(Loc(), ident);
        else
            tqual->addIdent(ident);
    }

    return tqual;
}

TypeQualified* TypeMapper::typeQualifiedFor(clang::NamedDecl* ND,
    const clang::TemplateArgument *TempArgBegin,
    const clang::TemplateArgument *TempArgEnd)
{
    AddImplicitImportForDecl(ND);

    auto Root = GetImplicitImportKeyForDecl(ND);
    return TypeQualifiedBuilder(*this, Root, TempArgBegin, TempArgEnd).get(ND);
}

Type* TypeMapper::toTypeTypedef(const clang::TypedefType* T)
{
    // Temporary HACK to avoid importing "_" just because of typedefs (eg size_t)
    // which doesn't even work atm
    auto TD = T->getDecl();
//     if (TD->getDeclContext()->isTranslationUnit())
        return toType(T->desugar());

//     return typeQualifiedFor(T->getDecl());
}

Type* TypeMapper::toTypeEnum(const clang::EnumType* T)
{
    return typeQualifiedFor(T->getDecl());
}

Type *TypeMapper::toTypeRecord(const clang::RecordType *T)
{
    return typeQualifiedFor(T->getDecl());
}

Type* TypeMapper::toTypeElaborated(const clang::ElaboratedType* T)
{
    return toType(T->getNamedType());
}

Type* TypeMapper::toTypeTemplateSpecialization(const clang::TemplateSpecializationType* T)
{
    if (T->isSugared())
        return toType(T->desugar());

    // T is a partial specialization
    return typeQualifiedFor(T->getTemplateName().getAsTemplateDecl(),
        T->begin(), T->end());
}

Type* TypeMapper::toTypeTemplateTypeParm(const clang::TemplateTypeParmType* T)
{
    if (T->getIdentifier())
        return new TypeIdentifier(Loc(), toIdentifier(T->getIdentifier()));
    else
        return new TypeNull; // FIXME
}

Type* TypeMapper::toTypeSubstTemplateTypeParm(const clang::SubstTemplateTypeParmType* T)
{
    return toType(T->desugar());
}

Type* TypeMapper::toTypeInjectedClassName(const clang::InjectedClassNameType* T)
{
    return toType(T->getInjectedSpecializationType());
}

Type* TypeMapper::toTypeAdjusted(const clang::AdjustedType* T)
{
    return toType(T->getAdjustedType());
}

TypeQualified* TypeMapper::fromNestedNameSpecifier(const clang::NestedNameSpecifier* NNS)
{
    TypeQualified *result = nullptr;

    switch (NNS->getKind())
    {
        case clang::NestedNameSpecifier::Identifier:
        {
            auto ident = toIdentifier(NNS->getAsIdentifier());
            if (auto Prefix = NNS->getPrefix())
            {
                result = fromNestedNameSpecifier(Prefix);
                result->addIdent(ident);
            }
            else
                result = new TypeIdentifier(Loc(), ident);

            break;
        }

        case clang::NestedNameSpecifier::TypeSpec:
        case clang::NestedNameSpecifier::TypeSpecWithTemplate:
        {
            auto t = toTypeUnqual(NNS->getAsType());

            if (t->ty == Tinstance)
                result = (TypeInstance*) t;
            else if (t->ty == Tident)
                result = (TypeIdentifier*) t;
            break;
        }

        default:
            assert(false && "Unexpected nested name specifier kind for a Dependent*****Type");
    }

    return result;
}

// NOTE: Dependent***Type are not mandatory to get templates working because the instantiation is done by Sema
// and then DMD simply maps the resulting class or function specialization, so we could return TypeNull and it would still work.
// Still good for reflection.
Type* TypeMapper::toTypeDependentName(const clang::DependentNameType* T)
{
    TypeQualified *tqual = nullptr;

    if (auto NNS = T->getQualifier())
        tqual = fromNestedNameSpecifier(NNS);

    auto ident = toIdentifier(T->getIdentifier());
    if (!tqual)
        tqual = new TypeIdentifier(Loc(), ident);
    else
        tqual->addIdent(ident);

    return tqual;
}

Type* TypeMapper::toTypeDependentTemplateSpecialization(const clang::DependentTemplateSpecializationType* T)
{
    TypeQualified *tqual = nullptr;

    if (auto NNS = T->getQualifier())
        tqual = fromNestedNameSpecifier(NNS);

    auto ident = toIdentifier(T->getIdentifier());
    auto tiargs = toTemplateArguments(T->begin(), T->end());

    auto tempinst = new ::TemplateInstance(Loc(), ident);
    tempinst->tiargs = tiargs;

    if (!tqual)
        tqual = new TypeInstance(Loc(), tempinst);
    else
        tqual->addInst(tempinst);

    return tqual;
}

Type* TypeMapper::toTypeDecltype(const clang::DecltypeType* T)
{
    if (T->isSugared())  // TODO: remove this for reflection
        return toType(T->desugar());

    auto exp = ExprMapper(*this).toExpression(T->getUnderlyingExpr());
    if (exp) // temporary? some decltype use CallExpr, which I feel would make getting things working much harder for little gain since templates are instantiated by Sema
        return new TypeTypeof(Loc(), exp);
    else
        return new TypeNull;
}

bool TypeMapper::isNonPODRecord(const clang::QualType T)
{
    auto RT = T->getAs<clang::RecordType>();
    if (!RT)
        return false;

    auto CRD = dyn_cast<clang::CXXRecordDecl>(RT->getDecl());
    if (!CRD)
        return false;

    if (isa<clang::ClassTemplateSpecializationDecl>(CRD))
        return true;

    // FIXME!!! If this is a forward decl, what do?? We can only know if an agg is POD if the definition is known :(
    if (!CRD->hasDefinition())
//     {
//         ::warning(Loc(), "FIXME: Assuming that CXXRecordDecl %s without a definition is non-POD",
//                   CRD->getQualifiedNameAsString().c_str());
//         return true;
//     }
        return false; // a reference of a reference is ok right?

    return !CRD->isPOD();
}

TypeFunction *TypeMapper::toTypeFunction(const clang::FunctionProtoType* T)
{
    auto params = new Parameters;
    params->reserve(T->getNumParams());

    for (auto I = T->param_type_begin(), E = T->param_type_end();
                I != E; I++)
    {
        // FIXME we're ignoring functions with unhandled types i.e class values
        if (isNonPODRecord(*I))
            return nullptr;

        params->push(new Parameter(STCundefined, toType(*I), nullptr, nullptr));
    }

    return new TypeFunction(params, toType(T->getReturnType()), 0, LINKd);  // does LINK matter?
}

// In D if a class is inheriting from another module's class, then its own module has to import the base class' module.
// So we need to populate the beginning of our virtual module with imports for derived classes.
void TypeMapper::AddImplicitImportForDecl(const clang::NamedDecl* ND)
{
    if (!addImplicitDecls)
        return;

    assert(mod);

    auto D = GetImplicitImportKeyForDecl(ND);

    if (D == mod->rootDecl)
        return; // do not import self

    if (implicitImports[D])
        return;

    auto im = BuildImplicitImport(ND);
    implicitImports[D] = im;
    mod->members->insert(0, im);
}

// Record -> furthest parent tagdecl
// Other decl in namespace -> the canonical namespace decl
// Other decl in TU -> the TU
const clang::Decl* TypeMapper::GetImplicitImportKeyForDecl(const clang::NamedDecl* D)
{
    auto ParentDC = D->getDeclContext();

    if (auto ParentTag = dyn_cast<clang::TagDecl>(ParentDC))
        return GetImplicitImportKeyForDecl(ParentTag);

    if (auto CSD = dyn_cast<clang::ClassTemplateSpecializationDecl>(D))
        return GetImplicitImportKeyForDecl(CSD->getSpecializedTemplate());

    const clang::Decl *result;
    if (isa<clang::ClassTemplateDecl>(D) || isa<clang::TagDecl>(D))
        result = D;
    else if (auto NS = dyn_cast<clang::NamespaceDecl>(ParentDC))
        result = NS;
    else
        result = cast<clang::TranslationUnitDecl>(ParentDC);

    return result->getCanonicalDecl();
}

::Import *TypeMapper::BuildImplicitImport(const clang::Decl *ND)
{
    auto loc = toLoc(ND->getLocation());

    auto sPackages = new Identifiers;
    Identifier *sModule = nullptr;

    auto DC = dyn_cast<clang::DeclContext>(ND);
    if (!DC)
        DC = ND->getDeclContext();

    if (!BuildImplicitImportInternal(DC, loc, sPackages, sModule))
    {
        if (isa<clang::ClassTemplateDecl>(ND))
            sModule = getIdentifier(cast<clang::NamedDecl>(ND));
        else
            // ND is neither a tag nor a class template, we need to import the namespace's functions and vars
            sModule = Lexer::idPool("_");
    }

    return new cpp::Import(loc, sPackages, sModule, nullptr, 0);
}

bool TypeMapper::BuildImplicitImportInternal(const clang::DeclContext *DC, Loc loc,
            Identifiers *sPackages, Identifier *&sModule)
{
    if (DC->isTranslationUnit()) return false;
    assert(!DC->isFunctionOrMethod() && "Building import for a decl nested inside a func?");

    if (BuildImplicitImportInternal(DC->getParent(), loc, sPackages, sModule))
        return true;

    if (auto NS = dyn_cast<clang::NamespaceDecl>(DC))
    {
        if (NS->isAnonymousNamespace())
            error(loc, "Cannot import symbols from anonymous namespaces");

        if (!NS->isInline())
            sPackages->push(toIdentifier(NS->getIdentifier()));

        return false;
    }
    else if (isa<clang::TagDecl>(DC))
    {
        sModule = getIdentifier(cast<clang::NamedDecl>(DC));
        return true;
    }
    else if (isa<clang::LinkageSpecDecl>(DC))
        return false;

    assert(false && "Unhandled case");
    return false;
}

TypeMapper::TypeMapper(cpp::Module* mod)
    : mod(mod)
{
}

}