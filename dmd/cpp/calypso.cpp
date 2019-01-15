// Contributed by Elie Morisse, same license DMD uses

#include "cpp/calypso.h"
#include "cpp/cppdeclaration.h"
#include "cpp/cppimport.h"
#include "cpp/cppmodule.h"
#include "cpp/cppaggregate.h"
#include "cpp/cpptemplate.h"
#include "cpp/cpptypes.h"
#include "cpp/diagprinter.h"

#include "cpp/ddmdstructor.h"

#include "aggregate.h"
#include "cond.h"
#include "declaration.h"
#include "expression.h"
#include "id.h"
#include "scope.h"

#include "driver/tool.h"
#include "driver/cl_options.h"
#include "gen/irstate.h"

#include "clang/AST/DeclTemplate.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/Tool.h"
#include "clang/Driver/ToolChain.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Lex/ModuleMap.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Frontend/ASTUnit.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Frontend/MultiplexConsumer.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "clang/Parse/Parser.h"
#include "clang/Sema/Sema.h"
#include "clang/Serialization/ASTReader.h"
#include "clang/Serialization/ASTWriter.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Target/TargetMachine.h"

#include <fstream>
#include <string>
#include <cctype>
#include <cstring>

extern llvm::cl::opt<bool> linkDefaultLibDebug;
void codegenModules(Modules &modules, bool oneobj);

void log_verbose(const std::string& header, const std::string& msg) {
    // to look aligned with other -v printed lines
    int prefix_width = 9; // TODO: adjust upwards as needed
    fprintf(stderr, "%-*s %s\n", prefix_width, header.c_str(), msg.c_str());
}

namespace cpp
{

using llvm::cast;
using llvm::dyn_cast;
using llvm::isa;

LangPlugin calypso;
BuiltinTypes builtinTypes;

RootObject *SpecValue::toTemplateArg(Loc loc)
{
    assert(op || t);
    if (op)
        return new_StringExp(loc, const_cast<char*>(op));
    else
        return t;
}

Identifier *prefixConflictingIdentifier(Identifier *ident)
{
    llvm::SmallString<48> s(u8"ℂ"); // non-ASCII and unavailable on most keyboards, but pretty
    s += llvm::StringRef(ident->toChars(), ident->length());
    return Identifier::idPool(s.c_str(), s.size());
}

Identifier *fromIdentifier(const clang::IdentifierInfo *II)
{
    return Identifier::idPool(II->getNameStart(), II->getLength());
        // NOTE: Every C++ identifier passing through DMD gets its own redundant copy in memory
        // Is this the cost of interfacing with Clang or is there another way? (probably not an easy one)
}

static const char *getOperatorName(const clang::OverloadedOperatorKind OO)
{
    switch (OO)
    {
#   define OVERLOADED_OPERATOR(Name,Spelling,Token,Unary,Binary,MemberOnly) \
        case clang::OO_##Name: return #Name;
#   include "clang/Basic/OperatorKinds.def"
        default: return "None";
    }
}

static const char *getDOperatorSpelling(const clang::OverloadedOperatorKind OO)
{
    switch (OO)
    {
        case clang::OO_PlusEqual: return "+";
        case clang::OO_MinusEqual: return "-";
        case clang::OO_StarEqual: return "*";
        case clang::OO_SlashEqual: return "/";
        case clang::OO_PercentEqual: return "%";
        case clang::OO_CaretEqual: return "^";
        case clang::OO_AmpEqual: return "&";
        case clang::OO_PipeEqual: return "|";
        case clang::OO_LessLessEqual: return "<<";
        case clang::OO_GreaterGreaterEqual: return ">>";
        default:
            return clang::getOperatorSpelling(OO);
    }
}

static Identifier *fullOperatorMapIdent(Identifier *baseIdent,
                                       clang::OverloadedOperatorKind OO)
{
    std::string fullName(baseIdent->toChars(), baseIdent->length());
    fullName += "_";
    fullName += getOperatorName(OO);

    return Identifier::idPool(fullName.c_str(), fullName.size());
}

static Identifier *getOperatorIdentifier(const clang::FunctionDecl *FD,
                const char *&op, clang::OverloadedOperatorKind OO = clang::OO_None)
{
    if (FD)
        OO = FD->getOverloadedOperator();

    Identifier *opIdent = nullptr;
    bool wrapInTemp = false;

    auto MD = llvm::dyn_cast_or_null<clang::CXXMethodDecl>(FD);
    bool isNonMember = !MD || MD->isStatic();

    unsigned NumParams = 0;
    if (FD) {
        NumParams = FD->getNumParams();
        if (!isNonMember)
            NumParams++;
    }

    if (OO == clang::OO_Call)
        opIdent = Id::call;
    else if(OO == clang::OO_Subscript)
        opIdent = Id::index;
    else
    {
        bool isUnary = NumParams != 2;
        bool isBinary = NumParams != 1;

        wrapInTemp = true; // except for opAssign and opCmp

        if (isUnary)
        {
            switch (OO)
            {
                case clang::OO_Plus:
                case clang::OO_Minus:
                case clang::OO_Star:
                case clang::OO_Tilde:
                case clang::OO_PlusPlus:
                case clang::OO_MinusMinus:
                    // operators without D equivalent need to be mapped for linking
                case clang::OO_Exclaim:
                case clang::OO_Arrow:
                case clang::OO_ArrowStar:
                    opIdent = Id::opUnary;
                    break;
                default:
                    if (FD) {
                        if (opts::cppVerboseDiags)
                            ::warning(Loc(), "Ignoring C++ unary operator %s", clang::getOperatorSpelling(OO));
                        return nullptr;
                    }
            }
        }
        if (isBinary)
        {
            switch (OO)
            {
                case clang::OO_Plus:
                case clang::OO_Minus:
                case clang::OO_Star:
                case clang::OO_Slash:
                case clang::OO_Percent:
                case clang::OO_Caret:
                case clang::OO_Amp:
                case clang::OO_Pipe:
                case clang::OO_Tilde:
                case clang::OO_LessLess:
                case clang::OO_GreaterGreater:
                    // operators without D equivalent need to be mapped for linking
                case clang::OO_PlusPlus:
                case clang::OO_MinusMinus:
                case clang::OO_Comma:
                    opIdent = Id::opBinary;
                    break;
                case clang::OO_EqualEqual:
                    opIdent = Id::eq;
                    wrapInTemp = false;
                    break;
                case clang::OO_ExclaimEqual:
                    opIdent = idPool("opEqualsNot"); // TODO?
                    wrapInTemp = false;
                    break;
                case clang::OO_Less:
                case clang::OO_LessEqual:
                case clang::OO_Greater:
                case clang::OO_GreaterEqual:
                    opIdent = fullOperatorMapIdent(Id::cmp, OO);
                    wrapInTemp = false;
                    break;
                case clang::OO_Equal:
                    // NOTE: C++ assignment operators can't be non-members.
                    opIdent = Id::assign;
                    wrapInTemp = false;
                    break;
                case clang::OO_PlusEqual:
                case clang::OO_MinusEqual:
                case clang::OO_StarEqual:
                case clang::OO_SlashEqual:
                case clang::OO_PercentEqual:
                case clang::OO_CaretEqual:
                case clang::OO_AmpEqual:
                case clang::OO_PipeEqual:
                case clang::OO_LessLessEqual:
                case clang::OO_GreaterGreaterEqual:
                    opIdent = Id::opOpAssign;
                    break;
                default:
                    if (FD) {
                        if (opts::cppVerboseDiags)
                            ::warning(Loc(), "Ignoring C++ binary operator %s", clang::getOperatorSpelling(OO));
                        return nullptr;
                    }
            }
        }
        // operator new or delete (TODO linking)
    }

    if (!opIdent)
        return nullptr;

    op = wrapInTemp ? getDOperatorSpelling(OO) : nullptr;
    return opIdent;
}

static Identifier *fullConversionMapIdent(Identifier *baseIdent,
                                       const clang::CXXConversionDecl *D)
{
    auto& Context = calypso.getASTContext();

    TypeMapper mapper;
    mapper.addImplicitDecls = false;

    auto T = D->getConversionType().getDesugaredType(Context);
    auto t = mapper.fromType(T, Loc());

    std::string fullName(baseIdent->toChars(), baseIdent->length());
    fullName += "_";
    if (t->isTypeBasic()) // not too complex, use a readable suffix
    {
        auto TypeQuals = T.getCVRQualifiers();
        if (TypeQuals & clang::Qualifiers::Const) fullName += "const_";
        if (TypeQuals & clang::Qualifiers::Volatile) fullName += "volatile_";
        if (TypeQuals & clang::Qualifiers::Restrict) fullName += "restrict_";
        fullName += t->kind();
    }
    else // generate a name, rare occurrence anyway and only ever matters for virtual conversion operators
    {
        // FIXME: *might* collide
        fullName += T.getAsString();
        fullName.erase(std::remove_if(fullName.begin(), fullName.end(),
            [](char c) { return !isalnum(c); }), fullName.end());
    }

    return Identifier::idPool(fullName.c_str(), fullName.size());
}

static Identifier *getConversionIdentifier(const clang::CXXConversionDecl *D,
                TypeMapper &mapper, Type *&t, clang::QualType T = clang::QualType(),
                bool wantCanonicalType = false)
{
    if (D)
        T = D->getConversionType();

    if (wantCanonicalType)
        T = T.getCanonicalType();

    t = mapper.fromType(T, Loc());
    return Id::_cast;
}

static Identifier *getLambdaIdentifier(const clang::CXXRecordDecl *D)
{
    // NOTE: the mangling number differentiates lambda classes *per* lambda function signature,
    // so both have to enter into account while naming a lambda.
    auto& MangleCtx = calypso.pch.MangleCtx;
    std::string name("__lambda_");

    auto ManglingNumber = D->getLambdaManglingNumber();
    name += std::to_string(ManglingNumber);
    name += "_";

    assert(D->isLambda());

    {
        llvm::raw_string_ostream OS(name);
        MangleCtx->mangleTypeName(
                clang::QualType(D->getLambdaCallOperator()->getFunctionType(), 0), OS);
    }

    return Identifier::idPool(name.c_str(), name.size());
}

Identifier *fromDeclarationName(const clang::DeclarationName N,
                                    SpecValue *spec)
{
    switch (N.getNameKind())
    {
        case clang::DeclarationName::Identifier:
            return fromIdentifier(N.getAsIdentifierInfo());
        case clang::DeclarationName::CXXConstructorName:
            return Id::ctor;
        case clang::DeclarationName::CXXDestructorName:
            return Id::dtor; // NOTE: Id::dtor is the user-provided destructor code, "aggrDtor" the "true" destructor
        case clang::DeclarationName::CXXOperatorName:
        {
            assert(spec && "Operator name and spec isn't set");
            return getOperatorIdentifier(nullptr, spec->op,
                    N.getCXXOverloadedOperator());
        }
        case clang::DeclarationName::CXXConversionFunctionName:
        {
            assert(spec && "Conversion name and spec isn't set");
            return getConversionIdentifier(nullptr, spec->mapper,
                    spec->t, N.getCXXNameType());
        }
        default:
//             break;
            return nullptr;
    }

    llvm_unreachable("Unhandled DeclarationName");
}

Identifier *getIdentifierOrNull(const clang::NamedDecl *D, SpecValue *spec, bool useCanonicalType)
{
    if (auto FTD = dyn_cast<clang::FunctionTemplateDecl>(D))
        D = FTD->getTemplatedDecl(); // same ident, can dyn_cast

    if (isa<clang::CXXConstructorDecl>(D))
        return Id::ctor;
    else if (isa<clang::CXXDestructorDecl>(D))
        return Id::dtor;
    else if (auto Conv = dyn_cast<clang::CXXConversionDecl>(D))
    {
        assert(spec);
        return getConversionIdentifier(Conv, spec->mapper, spec->t,
                                       clang::QualType(), useCanonicalType);
    }
    else if (auto FD = dyn_cast<clang::FunctionDecl>(D))
        if (FD->isOverloadedOperator())
        {
            assert(spec);
            return getOperatorIdentifier(FD, spec->op);
        }

    clang::IdentifierInfo *II = nullptr;

    if (D->getIdentifier())
        II = D->getIdentifier();
    else if (auto Tag = dyn_cast<clang::TagDecl>(D)) {
        if (auto Typedef = Tag->getTypedefNameForAnonDecl())
            II = Typedef->getIdentifier();
        else if (auto Record = dyn_cast<clang::CXXRecordDecl>(D))
            if (Record->isLambda())
                if (auto lambdaIdent = getLambdaIdentifier(Record))
                    return lambdaIdent;
    }

    if (!II)
        return nullptr;

    auto ident = fromIdentifier(II);
    bool needsPrefixing = false;

    auto ParentTag = dyn_cast<clang::TagDecl>(D->getDeclContext());
    if (II && isa<clang::TagDecl>(D) && ParentTag)
    {
        auto R = ParentTag->lookup(D->getDeclName());
        for (auto Match: R)
        {
            if (!isa<clang::ValueDecl>(*Match))
                continue;

            // The nested tag shares a common name with a field, mapping it directly would cause a symbol conflict
            needsPrefixing = true;
            break;
        }
    }

    if (isa<clang::RecordDecl>(D))
    {
        // Prefix reserved class names with 'ℂ'
        if (ident == Id::Object || ident == Id::Throwable || ident == Id::Exception || ident == Id::Error ||
            ident == Id::TypeInfo || ident == Id::TypeInfo_Class || ident == Id::TypeInfo_Interface ||
            ident == Id::TypeInfo_Struct || ident == Id::TypeInfo_Pointer ||
            ident == Id::TypeInfo_Array || ident == Id::TypeInfo_StaticArray || ident == Id::TypeInfo_AssociativeArray ||
            ident == Id::TypeInfo_Enum || ident == Id::TypeInfo_Function || ident == Id::TypeInfo_Delegate ||
            ident == Id::TypeInfo_Tuple || ident == Id::TypeInfo_Const || ident == Id::TypeInfo_Invariant ||
            ident == Id::TypeInfo_Shared || ident == Id::TypeInfo_Wild || ident == Id::TypeInfo_Vector) // thanks C++...
            needsPrefixing = true;
    }

    if (needsPrefixing)
        ident = prefixConflictingIdentifier(ident);

    return ident;
}

Identifier *idPool(const char *s)
{
    return Identifier::idPool(s, std::strlen(s));
}

Identifier *getIdentifier(const clang::NamedDecl *D, SpecValue *spec, bool useCanonicalType)
{
    auto result = getIdentifierOrNull(D, spec, useCanonicalType);
    assert(result);

    return result;
}

Identifier *getExtendedIdentifierOrNull(const clang::NamedDecl *D,
                                  TypeMapper &mapper)
{
    SpecValue spec(mapper);
    auto ident = getIdentifierOrNull(D, &spec);
    if (!ident)
        return nullptr;

    auto FD = dyn_cast<clang::FunctionDecl>(D);
    if (spec.op && FD)
        ident = fullOperatorMapIdent(ident,
                            FD->getOverloadedOperator());
    else if (spec.t)
        ident = fullConversionMapIdent(ident,
                    cast<clang::CXXConversionDecl>(D));

    return ident;
}

Identifier *getExtendedIdentifier(const clang::NamedDecl *D,
                                  TypeMapper &mapper)
{
    auto result = getExtendedIdentifierOrNull(D, mapper);
    assert(result);

    return result;
}

RootObject *getIdentOrTempinst(Loc loc, const clang::DeclarationName N,
                               TypeMapper &mapper)
{
    SpecValue spec(mapper);
    auto ident = fromDeclarationName(N, &spec);
    if (!ident)
        return nullptr;

    if (spec)
    {
        auto tiargs = new Objects;
        tiargs->push(spec.toTemplateArg(loc));
        auto tempinst = new cpp::TemplateInstance(loc, ident, tiargs);
        return tempinst;
    }
    else
        return ident;
}

Loc fromLoc(clang::SourceLocation L)
{
    auto& SrcMgr = calypso.getSourceManager();
    Loc loc;

    if (L.isInvalid())
        return loc;

    if (L.isMacroID())
        L = SrcMgr.getExpansionLoc(L);

    auto S = SrcMgr.getFilename(L);
    loc.filename = S.data();
    assert(!S.data() || *(S.data() + S.size()) == '\0'); // TEMPORARY assert to confirm that StringRef isn't needed anymore
    loc.linnum = SrcMgr.getSpellingLineNumber(L);

    return loc;
}

const clang::Decl *getDecl(Dsymbol *s)
{
    assert(isCPP(s));

#define RETRIEVE(DECL, MEMBER) \
    if (s->is##DECL()) return static_cast<cpp::DECL*>(s)->MEMBER;

    RETRIEVE(StructDeclaration, RD)
    RETRIEVE(ClassDeclaration, RD)
    RETRIEVE(EnumDeclaration, ED)
    RETRIEVE(EnumMember, ECD)
    RETRIEVE(CtorDeclaration, CCD)
    RETRIEVE(DtorDeclaration, CDD)
    RETRIEVE(FuncDeclaration, FD)
    RETRIEVE(VarDeclaration, VD)

#undef RETRIEVE
    llvm_unreachable("Unhandled getDecl");
}

/***********************/

// see CodeGenModule::getMangledName()
const char *LangPlugin::mangle(Dsymbol *s)
{
    assert(isCPP(s));

    auto ND = cast<clang::NamedDecl>(getDecl(s));

    auto &FoundStr = MangledDeclNames[getCanonicalDecl(ND)];
    if (!FoundStr.empty())
        return FoundStr.c_str();

    auto MangleCtx = pch.MangleCtx;

    llvm::SmallString<256> Buffer;
    llvm::StringRef Str;
    if (MangleCtx->shouldMangleDeclName(ND)) {
        llvm::raw_svector_ostream Out(Buffer);
        if (const auto *D = dyn_cast<clang::CXXConstructorDecl>(ND))
            MangleCtx->mangleCXXCtor(D, clang::Ctor_Complete, Out);
        else if (const auto *D = dyn_cast<clang::CXXDestructorDecl>(ND))
            MangleCtx->mangleCXXDtor(D, clang::Dtor_Complete, Out);
        else
            MangleCtx->mangleName(ND, Out);
        Str = Out.str();
    } else {
        auto II = ND->getIdentifier();
        assert(II && "Attempt to mangle unnamed decl.");
        Str = II->getName();
    }

    Str.str().swap(FoundStr);
    return FoundStr.c_str();
}

/***********************/

void PCH::init()
{
    pchHeader = calypso.getCacheFilename(".h");
    pchFilename = calypso.getCacheFilename(".h.pch");

    clang::IntrusiveRefCntPtr<clang::DiagnosticOptions> DiagOpts(new clang::DiagnosticOptions);
    clang::IntrusiveRefCntPtr<clang::DiagnosticIDs> DiagID(new clang::DiagnosticIDs);
    DiagClient = new DiagnosticPrinter(llvm::errs(), &*DiagOpts);
    DiagClient->muted = !opts::cppVerboseDiags;
    Diags = new clang::DiagnosticsEngine(DiagID,
                                         &*DiagOpts, DiagClient);
}

void PCH::add(const char* header, ::Module *from)
{
    // First check whether the path points towards a file relative to the module directory or a header from -I options or system include dirs
    // In the first case we need to make the path absolute since the calypso_cache directory is most of the time different.
    if (header[0] != '<')
    {
        using namespace llvm::sys::fs;
        using namespace llvm::sys::path;

        auto headerFn = new llvm::SmallString<64>(
                            parent_path(from->srcfile->name->str));
        append(*headerFn, llvm::StringRef(header));

        file_status result;
        status(llvm::Twine(*headerFn), result);
        if (is_regular_file(result))
        {
            make_absolute(*headerFn);
            headerFn->push_back('\0');
            header = headerFn->c_str();
        }
    }

    for (unsigned i = 0; i < headers.dim; i++)
        if (strcmp(header, headers[i]) == 0)
            return;

    headers.push(header);
}

void PCH::loadFromHeaders()
{
    // Re-emit the source file with #include directives
    {
        std::ofstream fmono(pchHeader);
        if (!fmono) {
            ::error(Loc(), "C++ monolithic header couldn't be created");
            fatal();
        }

        for (auto header: headers) {
            fmono << "#include ";
            if (header[0] != '<')
                fmono << "\"";
            fmono << header;
            if (header[0] != '<')
                fmono << "\"";
            fmono << "\n";
        }
    }

    std::unique_ptr<clang::driver::Compilation> C(calypso.buildClangCompilation());

    // We use a trick from clang-interpreter to extract -cc1 flags from "puny human" flags
    // We expect to get back exactly one command job, if we didn't something
    // failed. Extract that job from the compilation.
    const clang::driver::JobList &Jobs = C->getJobs();
    assert(Jobs.size() == 1 && isa<clang::driver::Command>(*Jobs.begin()));
    const clang::driver::Command &Cmd = cast<clang::driver::Command>(*Jobs.begin());
    assert(llvm::StringRef(Cmd.getCreator().getName()) == "clang");

    // Initialize a compiler invocation object from the clang (-cc1) arguments.
    const clang::driver::ArgStringList &CCArgs = Cmd.getArguments();
    auto CI = std::make_shared<clang::CompilerInvocation>();
    clang::CompilerInvocation::CreateFromArgs(*CI, CCArgs.begin(), CCArgs.end(), *Diags);

    // Parse the headers
    DiagClient->muted = false;

    llvm::IntrusiveRefCntPtr<clang::vfs::OverlayFileSystem> OverlayFileSystem(
        new clang::vfs::OverlayFileSystem(clang::vfs::getRealFileSystem()));
    auto Files = new clang::FileManager(clang::FileSystemOptions(), OverlayFileSystem);

    PCHContainerOps.reset(new clang::PCHContainerOperations);

    AST = clang::ASTUnit::LoadFromCompilerInvocation(CI, PCHContainerOps, Diags, Files, false, false, false,
                                              clang::TU_Complete, false, false, false);
    AST->getSema();
    Diags->getClient()->BeginSourceFile(AST->getLangOpts(), &AST->getPreprocessor());

    needSaving = true;
    DiagClient->muted = !opts::cppVerboseDiags;

    if (Diags->hasErrorOccurred())
    {
        ::error(Loc(), "Invalid C/C++ header(s)");
        fatal();
    }

    nextHeader = headers.size();

    /* Mark every C++ module object file dirty */

    auto genListFilename = calypso.getCacheFilename(".gen");
    llvm::sys::fs::remove(genListFilename, true);
}

void PCH::loadFromPCH()
{
    std::unique_ptr<clang::driver::Compilation> C(calypso.buildClangCompilation());

    clang::FileSystemOptions FileSystemOpts;
//     clang::ASTReader::ASTReadResult ReadResult;

    DiagClient->muted = false;

    PCHContainerOps.reset(new clang::PCHContainerOperations);
    auto *Reader = PCHContainerOps->getReaderOrNull("raw");

    AST = clang::ASTUnit::LoadFromASTFile(pchFilename, *Reader, clang::ASTUnit::LoadEverything,
                            Diags, FileSystemOpts, false, false, llvm::None, false,
                            /* AllowPCHWithCompilerErrors = */ true, false/*,
                            &ReadResult*/);

    DiagClient->muted = !opts::cppVerboseDiags;

//     switch (ReadResult) {
//         case clang::ASTReader::Success:
//             break;
//
//         case clang::ASTReader::Failure:
//         case clang::ASTReader::Missing:
//         case clang::ASTReader::OutOfDate:
//         case clang::ASTReader::VersionMismatch:
//         case clang::ASTReader::ConfigurationMismatch:
//             delete AST;
//             Diags->Reset();
//
//             // Headers or flags may have changed since the PCH was generated, fall back to headers.
//             loadFromHeaders(C);
//             return;
//
//         default:
//             fatal();
//             return;
//     }

    auto fallbackToHeaders = [&] () {
        Diags->Reset();
        loadFromHeaders();
    };

    if (!AST) {
        // Headers or flags may have changed since the PCH was generated, fall back to headers.
        fallbackToHeaders();
        return;
    }

    const clang::DirectoryLookup *CurDir;

    // Check whether every cppmap'd header is present within the AST, otherwise trigger a full reload.
    // TODO: this isn't strictly necessary and should be made configurable.
    for (auto header: headers) {
        auto fileEntry = lookupHeader(header, CurDir);
        if (!fileEntry || !fileEntry->isInPCH()) {
            fallbackToHeaders();
            return;
        }
    }

    nextHeader = headers.size();
}

void PCH::loadNewHeaders()
{
    auto& SrcMgr = AST->getSourceManager();
    auto& PP = AST->getPreprocessor();
    auto& S = AST->getSema();

    auto* Consumer = &S.getASTConsumer();

    if (!S.OpaqueParser)
        S.OpaqueParser = new clang::Parser(PP, S, /*SkipFunctionBodies=*/ false);
    auto& P = *static_cast<clang::Parser*>(S.OpaqueParser);

    if (!P.getCurScope()) {
        P.EnterScope(clang::Scope::DeclScope);
        S.CurContext = nullptr;
        S.ActOnTranslationUnitScope(P.getCurScope());
    }

    for (; nextHeader < headers.size(); nextHeader++) {
        const clang::DirectoryLookup *CurDir;

        auto File = lookupHeader(headers[nextHeader], CurDir);
        if (!File) {
            ::error(Loc(), "'%s'  file not found", headers[nextHeader]);
            fatal();
        }

        auto FileCharacter = PP.getHeaderSearchInfo().getFileDirFlavor(File);
        auto FID = SrcMgr.getOrCreateFileID(File, FileCharacter);

        if (PP.EnterSourceFile(FID, CurDir, clang::SourceLocation()))
            fatal();
        P.ConsumeToken();

        clang::Parser::DeclGroupPtrTy ADecl;
        for (bool AtEOF = P.ParseTopLevelDecl(ADecl); !AtEOF;
             AtEOF = P.ParseTopLevelDecl(ADecl)) {
            if (ADecl && !Consumer->HandleTopLevelDecl(ADecl.get()))
                fatal();
        }

        for (clang::Decl *D : S.WeakTopLevelDecls())
            Consumer->HandleTopLevelDecl(clang::DeclGroupRef(D));
    }
}

const clang::FileEntry* PCH::lookupHeader(const char* header, const clang::DirectoryLookup*& CurDir)
{
    auto& FileMgr = AST->getFileManager();
    auto& PP = AST->getPreprocessor();

    auto FromFile = FileMgr.getFile(pchHeader, false);
    bool IsMapped;
    clang::ModuleMap::KnownHeader SuggestedModule;

    bool isAngled = header[0] == '<';
    llvm::StringRef headerStr(header);
    return PP.LookupFile(clang::SourceLocation(),
                  isAngled ? headerStr.slice(1, headerStr.size() - 1) : headerStr,
                  isAngled, nullptr, FromFile, CurDir,
                  nullptr, nullptr, &SuggestedModule, &IsMapped);
}

void PCH::update()
{
    if (nextHeader >= headers.size())
        return;

    if (AST) {
        loadNewHeaders();
        return;
    }

    bool needHeadersReload = false;

    auto CheckCacheFile = [&] (std::string& fn) {
        using namespace llvm::sys::fs;

        file_status result;
        status(fn, result);
        if (is_directory(result)) {
            ::error(Loc(), "%s is a directory\n", fn.c_str());
            fatal();
        }

        if (!exists(result))
            needHeadersReload = true;
    };

    CheckCacheFile(pchHeader);
    CheckCacheFile(pchFilename);

    if (needHeadersReload) {
        // One of the cache files doesn't exist, so reparse the header files
        loadFromHeaders();
    } else {
        // Give the existing PCH a try
        loadFromPCH();
    }

    auto& SrcMgr = AST->getSourceManager();
    auto& PP = AST->getPreprocessor();

//     PP.enableIncrementalProcessing();

    /* Collect Clang module map files */
    MMap = new ModuleMap(AST->getSourceManager(), *Diags,
                            PP.getLangOpts(), &PP.getTargetInfo(), PP.getHeaderSearchInfo());

    llvm::DenseSet<const clang::DirectoryEntry*> CheckedDirs;
    auto lookForModuleMap = [&] (const clang::SrcMgr::SLocEntry& SLoc) {
        if (SLoc.isExpansion())
            return;

        auto OrigEntry = SLoc.getFile().getContentCache()->OrigEntry;
        if (!OrigEntry)
            return;

        auto Dir = OrigEntry->getDir();

        if (CheckedDirs.count(Dir))
            return;
        CheckedDirs.insert(Dir);

        std::error_code err;
        llvm::sys::fs::directory_iterator DirIt(llvm::Twine(Dir->getName()), err), DirEnd;

        for (; DirIt != DirEnd && !err; DirIt.increment(err))
        {
            auto path = DirIt->path();
            auto extension = llvm::sys::path::extension(path);

            if (extension.equals(".modulemap_d"))
            {
                auto MMapFile = AST->getFileManager().getFile(path);
                assert(MMapFile);

                if (MMap->parseModuleMapFile(MMapFile, false, Dir))
                {
                    ::error(Loc(), "Clang module map '%s/%s' file parsing failed",
                            MMapFile->getDir()->getName().str().c_str(), MMapFile->getName().str().c_str());
                    fatal();
                }
            }
        }
    };

    for (size_t i = 0; i < SrcMgr.local_sloc_entry_size(); i++)
        lookForModuleMap(SrcMgr.getLocalSLocEntry(i));
    for (size_t i = 0; i < SrcMgr.loaded_sloc_entry_size(); i++)
        lookForModuleMap(SrcMgr.getLoadedSLocEntry(i));

    // Since the out-of-dateness of headers are checked lazily for most of them, it might only be detected
    // by walking through all the SLoc entries. If an error occurred start over and trigger a loadFromHeaders.
    if (Diags->hasErrorOccurred())
    {
        Diags->Reset();

        delete MMap;

        needHeadersReload = true;
        return update();
    }

    // Build the builtin type map
    calypso.builtinTypes.build(AST->getASTContext());

    // Since macros aren't sorted by file (unlike decls) we build a map of macros in order to only go through every macro once
    calypso.buildMacroMap();

    // Initialize the mangling context
    MangleCtx = AST->getASTContext().createMangleContext();
}

void LangPlugin::buildMacroMap()
{
    auto& MMap = pch.MMap;
    auto& PP = getPreprocessor();
    auto& Context = getASTContext();
    auto& Sema = getSema();
    auto& SM = getSourceManager();

    for (auto I = PP.macro_begin(), E = PP.macro_end(); I != E; I++)
    {
        auto II = (*I).getFirst();
        if (!II->hasMacroDefinition())
            continue;

        auto MDir = (*I).getSecond().getLatest();
        auto MInfo = MDir->getMacroInfo();

        if (!MInfo->isObjectLike() || MInfo->isUsedForHeaderGuard() || MInfo->getNumTokens() > 1)
            continue;

        // Find the corresponding module header this macro is from
        auto MLoc = MDir->getLocation();
        auto MFileID = SM.getFileID(MLoc);
        auto MFileEntry = SM.getFileEntryForID(MFileID);

        const clang::Module::Header *FoundHeader = nullptr;
        for (auto ModI = MMap->module_begin(), ModE = MMap->module_end(); ModI != ModE; ModI++) {
            for (auto& Header: ModI->getValue()->Headers[clang::Module::HK_Normal])
                if (MFileEntry == Header.Entry) {
                    FoundHeader = &Header; break;
                }
            if (FoundHeader) break;
        }

        auto& MacroMapEntry = MacroMap[FoundHeader];
        if (!MacroMapEntry)
            MacroMapEntry = new MacroMapEntryTy;

        clang::Expr* Expr = nullptr;

        if (MInfo->getNumTokens() == 0) {
            unsigned BoolSize = Context.getIntWidth(Context.BoolTy);
            Expr = clang::IntegerLiteral::Create(Context, llvm::APInt(BoolSize, 1),
                                        Context.BoolTy, MLoc);
        } else {
            auto& Tok = MInfo->getReplacementToken(0);
            if (Tok.getKind() != clang::tok::numeric_constant)
                continue;

            auto ResultExpr = Sema.ActOnNumericConstant(Tok);
            if (!ResultExpr.isInvalid())
                Expr = ResultExpr.get(); // numeric_constant tokens might not be valid numerical expressions, e.g #define _SDT_ASM_ADDR .8byte
        }

        if (Expr)
            MacroMapEntry->emplace_back(II, Expr);
    }
}

void PCH::save()
{
    if (!needSaving)
        return;

    if (AST->getASTContext().getExternalSource() != nullptr) // FIXME: Clang makes it hard to save a new PCH when an external source like another PCH is loaded by the ASTContext
        return;

    auto& PP = AST->getPreprocessor();

    std::error_code EC;
    std::unique_ptr<llvm::raw_fd_ostream> OS(
                new llvm::raw_fd_ostream(pchFilename, EC, llvm::sys::fs::F_None));

    auto& Sysroot = PP.getHeaderSearchInfo().getHeaderSearchOpts().Sysroot;
    auto Buffer = std::make_shared<clang::PCHBuffer>();
    auto *Writer = PCHContainerOps->getWriterOrNull("raw");

    auto GenPCH = new clang::PCHGenerator(PP, pchFilename, Sysroot, Buffer,
                                          llvm::ArrayRef<std::shared_ptr<clang::ModuleFileExtension>>(),
                                          true);
    GenPCH->InitializeSema(AST->getSema());

    std::vector<std::unique_ptr<clang::ASTConsumer>> Consumers;
    Consumers.push_back(std::unique_ptr<clang::ASTConsumer>(GenPCH));
    Consumers.push_back(Writer->CreatePCHContainerGenerator(
        *static_cast<clang::CompilerInstance*>(nullptr), pchHeader, pchFilename, std::move(OS), Buffer));

    auto Mutiplex = llvm::make_unique<clang::MultiplexConsumer>(std::move(Consumers));
    Mutiplex->HandleTranslationUnit(AST->getASTContext());

    needSaving = false;
}

void LangPlugin::GenModSet::parse()
{
    if (parsed)
        return;

    parsed = true;
    clear();

    auto genFilename = calypso.getCacheFilename(".gen");
    if (!llvm::sys::fs::exists(genFilename))
        return;

    std::ifstream fgenList(genFilename);
    if (!fgenList)
    {
        ::error(Loc(), "Reading .gen file failed");
        fatal();
    }

    std::string line;
    while (std::getline(fgenList, line))
        if (llvm::sys::fs::exists(line))
            insert(strdup(line.c_str()));
}

void LangPlugin::GenModSet::add(::Module *m)
{
    auto& objName = m->objfile->name->str;
    assert(parsed);
    if (count(objName))
        return;

    auto genFilename = calypso.getCacheFilename(".gen");
    std::ofstream fgenList(genFilename, std::ios_base::out | std::ios_base::app);
    if (!fgenList)
    {
        ::error(Loc(), "Writing .gen file failed");
        fatal();
    }

    fgenList << objName << "\n";

    insert(objName);
}

void LangPlugin::semanticModules()
{
    // Do pass 2 semantic analysis
    for (size_t i = 0; i < cpp::Module::amodules.dim; i++)
    {
        auto m = cpp::Module::amodules[i];
        if (global.params.verbose)
            fprintf(stderr, "semantic2 %s\n", m->toChars());
        semantic2(m, nullptr);
    }
    if (global.errors)
        fatal();
    // Do pass 3 semantic analysis
    for (size_t i = 0; i < cpp::Module::amodules.dim; i++)
    {
        auto m = cpp::Module::amodules[i];
        if (global.params.verbose)
            fprintf(stderr, "semantic3 %s\n", m->toChars());
        semantic3(m, nullptr);
    }
    Module::runDeferredSemantic3();
    if (global.errors)
        fatal();
}

void LangPlugin::codegenModules()
{
    for (auto m: cpp::Module::amodules) {
        m->checkAndAddOutputFile(m->objfile);
        global.params.objfiles.push(m->objfile->name->str);
    }
    ::codegenModules(cpp::Module::amodules, false);
}

bool LangPlugin::needsCodegen(::Module *m)
{
    assert(isCPP(m));
    auto c_m = static_cast<cpp::Module*>(m);

    genModSet.parse();

    auto& objName = m->objfile->name->str;
    return c_m->needGen || !genModSet.count(objName);
}

int LangPlugin::doesHandleImport(const char* lang)
{
    if (strcmp((const char *) lang, "C") == 0
        || strcmp((const char *) lang, "C++") == 0)
        return 0;

    return -1;
}

::Import* LangPlugin::createImport(int langId, Loc loc, Identifiers* packages,
                                   Identifier* id, Identifier* aliasId, int isstatic)
{
    return new Import(loc,
                packages, id, aliasId, isstatic);
}

static bool parseStringExp(Expression *e, const char *&res) {
    e = e->optimize(WANTvalue);
    if (e->op != TOKstring) {
        return false;
    }
    auto s = static_cast<StringExp *>(e);
    res = s->toStringz();
    return true;
}

int LangPlugin::getPragma(Scope* sc, PragmaDeclaration* decl)
{
    if (decl->ident == id_cppmap) {
        Expressions *args = decl->args;
        Expression *expr =
            (args && args->dim > 0) ? expressionSemantic((*args)[0], sc) : nullptr;

        const char *arg1str;
        if (!args || args->dim != 1 || !parseStringExp(expr, arg1str)) {
            error(decl->loc, "must specify a C/C++ header (<xyz.h> for system includes)");
            fatal();
        }

        pch.add(arg1str, sc->_module);
        return 1;
    }
    return 0;
}

void LangPlugin::pragmaSemantic(Scope* sc, PragmaDeclaration* decl)
{
}

void LangPlugin::adjustLinkerArgs(std::vector<std::string>& args)
{
    if (!getASTUnit())
        return;

    auto argsFindSubstr = [&] (const char* substr) {
        return std::find_if(args.begin(), args.end(),
                [&] (const std::string& str) { return str.find(substr) != std::string::npos; });
    };

    if (argsFindSubstr("calypso-ldc") == args.end()) {
        // Insert -lcalypso-ldc before -ldruntime-ldc if not already passed yet (e.g to link against the -shared version)
        auto it_druntime = argsFindSubstr("druntime-ldc"); // FIXME: too heuristical
        assert(it_druntime != args.end());

        std::string calypso_ldc = "-lcalypso-ldc",
            calypso_ldc_debug = "-lcalypso-ldc-debug";

        if (global.params.targetTriple->isWindowsMSVCEnvironment()) {
            calypso_ldc = calypso_ldc.substr(2) + ".lib";
            calypso_ldc_debug = calypso_ldc_debug.substr(2) + ".lib";
        }

        if (linkDefaultLibDebug)
            args.insert(it_druntime, calypso_ldc_debug);
        else
            args.insert(it_druntime, calypso_ldc);
    }

    if (global.params.targetTriple->isWindowsMSVCEnvironment()) {
        // auto it_kernel32 = std::find(args.begin(), args.end(), "kernel32.lib");
        // NOTE: the choice of the MSVC CRT library (LIBCPMT.LIB, MSVCPRT.LIB, etc.) should be left either to the linker
        // (if linking against 3rd-party C++ libraries, link.exe picks up the runtime library expected by that library)
        // or to the user.
        // The right -D flags must also be manually passed: -cpp-args -D_DEBUG, -DMD(d), -DMT(d)
    } else {
        // Insert -lstdc++ or -lc++
        const char* cxxstdlib = (cxxStdlibType ==
            clang::driver::ToolChain::CST_Libcxx) ? "-lc++" : "-lstdc++";

        auto it_pthread = std::find(args.begin(), args.end(), "-lpthread");
        auto it_m = std::find(args.begin(), args.end(), "-lm");

        if (it_pthread != args.end())
            args.insert(it_pthread, cxxstdlib);
        else if (it_m != args.end())
            args.insert(it_m, cxxstdlib); // Solaris
    }
}

std::string GetExecutablePath(const char *Argv0) {
  // This just needs to be some symbol in the binary; C++ doesn't
  // allow taking the address of ::main however.
  void *MainAddr = (void*) (intptr_t) GetExecutablePath;
  return llvm::sys::fs::getMainExecutable(Argv0, MainAddr);
}

LangPlugin::LangPlugin()
    : builtinTypes(cpp::builtinTypes)
{
}

void LangPlugin::_init()
{
    if (!llvm::sys::fs::exists(opts::cppCacheDir))
        llvm::sys::fs::create_directory(opts::cppCacheDir);
    if (!llvm::sys::fs::is_directory(opts::cppCacheDir)) {
        ::error(Loc(), "%s isn't a directory", opts::cppCacheDir.c_str());
        fatal();
    }

    id_cppmap = idPool("cppmap");

    id_cpp = idPool("cpp");
    id_core = idPool("core");
    id__ = idPool("_");
    id_Scpp = idPool(u8"ℂcpp");

    id_isCpp = idPool("isCpp");
    id_getCppVirtualIndex = idPool("getCppVirtualIndex");
    id_getBaseOffset = idPool("getBaseOffset");
    id_getMemberPointerExtraSlots = idPool("getMemberPointerExtraSlots");

    id___cpp_type_info_ptr = idPool("__calypso_type_info_ptr"); // FIXME
    id_cpp_member_ptr = idPool("__cpp_member_ptr");

    id_op = idPool("op");
    id_type = idPool("type");

    id___va_list_tag = idPool("__va_list_tag");
    id___NSConstantString_tag = idPool("__NSConstantString_tag");

    Module::init();
    pch.init();

    auto TargetFS = gTargetMachine->getTargetFeatureString();
    if (!TargetFS.empty()) { // always empty for MSVC
        llvm::SmallVector<StringRef, 1> AttrFeatures;
        TargetFS.split(AttrFeatures, ",");

        for (auto &Feature : AttrFeatures) {
            Feature = Feature.trim();

            if (Feature.startswith("-"))
                continue;

            TargetFeatures.insert(Feature);
        }
    }

    // The driver doesn't do anything except computing the flags and informing us of the toolchain's C++ standard lib.
    // NOTE: Calypso doesn't call any clang executable
    clangArgv.push_back("clang");

    // eg: handle line=" -Ifoo -v " => "-Ifoo", "-v"
    static std::vector<std::string> args; // FIXME
    for (auto& line: opts::cppArgs) {
        std::string arg;
        if (line.empty())
            continue;

        // NOTE: an alternative would be to use "foo\ bar" for escaping space, but it has other drawbacks, eg for windows, or for escaping '\' itself; also it adds complexity on user shell command. This seems simpler.

        // TODO: is another character more standard and cross-platform (and less susceptible to bash substitution)?
        char single_arg='$';

        if (line[0] == single_arg) {
            if (line.size() > 1)
                args.push_back(line.substr(1));
            continue;
        }

        for (char c: line)
            if (isspace(c)) {
                if (!arg.empty()) {
                    args.push_back(arg);
                    arg.clear();
                }
            } else
                arg.push_back(c);

        if (!arg.empty())
            args.push_back(arg);
    }
    for (const auto& argi: args)
        clangArgv.push_back(argi.c_str());

    clangArgv.push_back("-c");
    clangArgv.push_back("-x");
    clangArgv.push_back("c++-header");
    clangArgv.push_back("-");

    // Build a dummy compilation to extract the standard library type
    std::unique_ptr<clang::driver::Compilation> C(buildClangCompilation());
    cxxStdlibType = C->getDefaultToolChain().GetCXXStdlibType(C->getArgs());

    switch (cxxStdlibType)
    {
        case clang::driver::ToolChain::CST_Libcxx:
            VersionCondition::addPredefinedGlobalIdent("CppStdLib_libcxx");
            break;
        case clang::driver::ToolChain::CST_Libstdcxx:
            VersionCondition::addPredefinedGlobalIdent("CppStdLib_libstdcxx");
            break;
        default:
            break;
    }

    clangArgv.pop_back();
    clangArgv.push_back(pch.pchHeader.c_str());

    if (global.params.verbose) {
        std::string msg = "driver args: ";
        for (std::string arg : clangArgv) {
            for (auto i = std::find(arg.begin(), arg.end(), ' '); i != arg.end();
                        i = std::find(i, arg.end(), ' '))
                i = ++++arg.insert(i, '\\');
            msg += arg;
            msg += " ";
        }
        log_verbose("calypso", msg);
    }
}

clang::driver::Compilation* LangPlugin::buildClangCompilation()
{
    std::string TripleStr = llvm::sys::getProcessTriple();
    llvm::Triple T(TripleStr);

    clang::IntrusiveRefCntPtr<clang::DiagnosticOptions> CC1DiagOpts(new clang::DiagnosticOptions);
    clang::IntrusiveRefCntPtr<clang::DiagnosticIDs> CC1DiagID(new clang::DiagnosticIDs);
    auto CC1DiagClient = new clang::TextDiagnosticPrinter(llvm::errs(), &*CC1DiagOpts);
    clang::IntrusiveRefCntPtr<clang::DiagnosticsEngine> CC1Diags = new clang::DiagnosticsEngine(CC1DiagID,
                                        &*CC1DiagOpts, CC1DiagClient);

    // FIXME Clang 4+: We currently reconstruct a Driver every time we want a new compilation because things break if we don't, but this shoudn't be so
    TheDriver.reset(new clang::driver::Driver(GetExecutablePath(Argv0), T.str(), *CC1Diags));
    TheDriver->setTitle("Calypso");

    auto C = TheDriver->BuildCompilation(clangArgv);
    assert(C);
    return C;
}

clang::ASTContext& LangPlugin::getASTContext()
{
    return getASTUnit()->getASTContext();
}

clang::Sema& LangPlugin::getSema()
{
    return getASTUnit()->getSema();
}

clang::DiagnosticsEngine& LangPlugin::getDiagnostics()
{
    return getASTUnit()->getDiagnostics();
}

clang::Preprocessor& LangPlugin::getPreprocessor()
{
    return getASTUnit()->getPreprocessor();
}

clang::SourceManager& LangPlugin::getSourceManager()
{
    return getASTUnit()->getSourceManager();
}

clang::FileManager& LangPlugin::getFileManager()
{
    return getASTUnit()->getFileManager();
}

std::string LangPlugin::getCacheFilename(const char *suffix)
{
    using namespace llvm::sys::path;

    std::string fn(calypso.cachePrefix);
    llvm::SmallString<64> fullpath(opts::cppCacheDir);

    if (suffix)
        fn += suffix;
    append(fullpath, fn);

    return fullpath.str().str();
}

bool isCPP(Type* t) { return t->langPlugin() == &calypso; }
bool isCPP(Dsymbol* s) { return s->langPlugin() == &calypso; }

cpp::ClassDeclaration *isDCXX(Dsymbol* s)
{
    auto cd = s->isClassDeclaration();
    assert(cd);

    if (isCPP(cd))
        return nullptr;  // Pure C++ class

    auto base = cd->baseClass;
    while (base && !isCPP(base))
        base = toAggregateBase(base);
    if (!base)
        return nullptr;  // Pure D class

    return static_cast<cpp::ClassDeclaration*>(base);
}

}
