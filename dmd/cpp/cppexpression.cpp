// Contributed by Elie Morisse, same license DMD uses

#include "cpp/cppexpression.h"
#include "cpp/cppdeclaration.h"
#include "cpp/cpptemplate.h"
#include "cpp/cpptypes.h"
#include "cpp/ddmdstructor.h"
#include "aggregate.h"
#include "errors.h"
#include "id.h"
#include "identifier.h"
#include "template.h"
#include "tokens.h"

#include "clang/AST/DeclTemplate.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"

struct MatchAccumulator
{
    int count = 0;              // number of matches found so far
    MATCH last = MATCHnomatch; // match level of lastf
    FuncDeclaration* lastf = nullptr;  // last matching function we found
    FuncDeclaration* nextf = nullptr;  // if ambiguous match, this is the "other" function
};

Objects* opToArg(Scope* sc, TOK op);
void functionResolve(MatchAccumulator& m, Dsymbol* dstart, Loc loc, Scope* sc, Objects* tiargs,
    Type* tthis, Expressions* fargs, const char** pMessage = nullptr, int flags = 0);

namespace cpp
{

using llvm::cast;
using llvm::dyn_cast;
using llvm::isa;

static Type *getAPIntDType(const llvm::APSInt &i);

Expression *dotIdentOrInst(Loc loc, Expression *e1, RootObject *o)
{
    if (o->dyncast() == DYNCAST_IDENTIFIER)
    {
        auto ident = static_cast<Identifier*>(o);
        if (!e1)
            return new_IdentifierExp(loc, ident);
        else
            return new_DotIdExp(loc, e1, ident);
    }
    else
    {
        assert(o->dyncast() == DYNCAST_DSYMBOL && static_cast<Dsymbol*>(o)->isTemplateInstance());
        auto tempinst = static_cast<::TemplateInstance*>(o);
        if (!e1)
            return new_ScopeExp(loc, tempinst);
        else
            return new_DotTemplateInstanceExp(loc, e1, tempinst);
    }
}

RootObject *typeQualifiedRoot(TypeQualified *tqual)
{
    if (tqual->ty == Tident)
        return static_cast<TypeIdentifier*>(tqual)->ident;
    else if (tqual->ty == Tinstance)
        return static_cast<TypeInstance*>(tqual)->tempinst;

    llvm_unreachable("FIXME TypeOf");
}

Objects *fromASTTemplateArgumentListInfo(Loc loc,
            const clang::TemplateArgumentLoc *Args,
            unsigned NumTemplateArgs,
            DeclMapper &mapper)
{
    auto tiargs = new Objects;

    for (unsigned i = 0; i < NumTemplateArgs; i++)
    {
        auto Arg = &Args[i].getArgument();
        tiargs->append(DeclMapper::FromType(mapper, loc).fromTemplateArgument(Arg));
    }

    return tiargs;
}

Expression *ExprMapper::fixIntegerExp(IntegerExp *e, clang::QualType T)
{
    if (auto ET = T->getAs<clang::EnumType>())
    {
        // In Clang AST enum values in template arguments are resolved to integer literals
        // If the parameter has an enum type, we need to revert integer literals to DeclRefs pointing to enum constants
        // or else DMD won't find the template decl since from its point of view uint != Enum
        // Also useful for function default zero arguments

        for (auto ECD: ET->getDecl()->enumerators())
        {
            auto Val = ECD->getInitVal().getZExtValue();

            if (Val == e->getInteger())
                return fromExpressionDeclRef(e->loc, ECD);
        }

        return new_CastExp(e->loc, e, mapper.fromType(T, e->loc));
        //llvm_unreachable("Couldn't find the corresponding enum constant");
    }

    auto BT = T->getAs<clang::BuiltinType>();
    if (BT && BT->getKind() == clang::BuiltinType::Char_S)
    {
        // C++ char may be signed, unlike D's, yet mapping char to uns8 instead of char
        // would be very destructive since character and string literals wouldn't work
        // In special cases such as numeric_traits::min we convert negative values to unsigned during the mapping.
        // The value won't be correct but the implicit conversion won't fail.
        if (!e->type->isunsigned() && static_cast<sinteger_t>(e->getInteger()) < 0)
            e = new_IntegerExp(e->loc, static_cast<unsigned char>(e->getInteger()), Type::tchar);
    }

    return e;
}

inline bool isMemberFuncPtrCall(const clang::Expr* E)
{
    if (!isa<clang::BinaryOperator>(E))
        return false;

    auto Op = cast<clang::BinaryOperator>(E)->getOpcode();
    return Op == clang::BO_PtrMemD || Op == clang::BO_PtrMemD;
}

Expression *ExprMapper::fromUnaExp(clang::SourceLocation Loc,
                                   const clang::UnaryOperator::Opcode Op,
                                   const clang::Expr *SubExpr)
{
    auto loc = fromLoc(Loc);
    auto sub = fromExpression(SubExpr);

    if (!sub)
        return nullptr;

    switch (Op)
    {
        case clang::UO_Plus: return sub;
        case clang::UO_Minus: return new_NegExp(loc, sub);
        case clang::UO_Not: return new_NotExp(loc, sub);
        case clang::UO_LNot: return new_NotExp(loc, sub);
        case clang::UO_AddrOf: return new_AddrExp(loc, sub);
        case clang::UO_Deref: return new_PtrExp(loc, sub);

        case clang::UO_PreInc: return new_PreExp(TOKplusplus, loc, sub);
        case clang::UO_PostInc: return new_PreExp(TOKminusminus, loc, sub);
        case clang::UO_PreDec: return new_PostExp(TOKplusplus, loc, sub);
        case clang::UO_PostDec: return new_PostExp(TOKminusminus, loc, sub);
    }

    llvm::llvm_unreachable_internal("Unhandled C++ unary operation exp");
}

Expression* ExprMapper::fromUnaExp(const clang::UnaryOperator *E)
{
    return fromUnaExp(E->getBeginLoc(), E->getOpcode(), E->getSubExpr());
}

Expression *ExprMapper::fromBinExp(clang::SourceLocation Loc,
                                const clang::BinaryOperator::Opcode Op,
                                const clang::Expr *LHS, const clang::Expr *RHS)
{
    auto loc = fromLoc(Loc);
    auto lhs = fromExpression(LHS);
    auto rhs = fromExpression(RHS);

    if (!lhs || !rhs)
        return nullptr;

    switch (Op)
    {
        case clang::BO_Add: return new_AddExp(loc, lhs, rhs);
        case clang::BO_Sub: return new_MinExp(loc, lhs, rhs);
        case clang::BO_Mul: return new_MulExp(loc, lhs, rhs);
        case clang::BO_Div: return new_DivExp(loc, lhs, rhs);
        case clang::BO_Rem: return new_ModExp(loc, lhs, rhs);

        case clang::BO_And: return new_AndExp(loc, lhs, rhs);
        case clang::BO_Or: return new_OrExp(loc, lhs, rhs);
        case clang::BO_Xor: return new_XorExp(loc, lhs, rhs);

        case clang::BO_LAnd: return new_LogicalExp(loc, TOKandand, lhs, rhs);
        case clang::BO_LOr: return new_LogicalExp(loc, TOKoror, lhs, rhs);

        case clang::BO_Shl: return new_ShlExp(loc, lhs, rhs);
        case clang::BO_Shr: return new_ShrExp(loc, lhs, rhs);

        case clang::BO_LT: return new_CmpExp(TOKlt, loc, lhs, rhs);
        case clang::BO_GT: return new_CmpExp(TOKgt, loc, lhs, rhs);
        case clang::BO_LE: return new_CmpExp(TOKle, loc, lhs, rhs);
        case clang::BO_GE: return new_CmpExp(TOKge, loc, lhs, rhs);

        case clang::BO_EQ: return new_EqualExp(TOKequal, loc, lhs, rhs);
        case clang::BO_NE: return new_EqualExp(TOKnotequal, loc, lhs, rhs);

        case clang::BO_Assign: return new_AssignExp(loc, lhs, rhs);
        case clang::BO_MulAssign: return new_MulAssignExp(loc, lhs, rhs);
        case clang::BO_AddAssign: return new_AddAssignExp(loc, lhs, rhs);
        case clang::BO_SubAssign: return new_MinAssignExp(loc, lhs, rhs);
        case clang::BO_DivAssign: return new_DivAssignExp(loc, lhs, rhs);
        case clang::BO_RemAssign: return new_ModAssignExp(loc, lhs, rhs);

        case clang::BO_AndAssign: return new_AndAssignExp(loc, lhs, rhs);
        case clang::BO_OrAssign: return new_OrAssignExp(loc, lhs, rhs);
        case clang::BO_XorAssign: return new_XorAssignExp(loc, lhs, rhs);
        case clang::BO_ShlAssign: return new_ShlAssignExp(loc, lhs, rhs);
        case clang::BO_ShrAssign: return new_ShrAssignExp(loc, lhs, rhs);

        case clang::BO_Comma: return new_CommaExp(loc, lhs, rhs);

        // Member function pointers calls (.* and ->*) using UFCS
        // TODO Itanium virtual function pointers
        case clang::BO_PtrMemD: return new_CallExp(loc, new_PtrExp(loc, rhs), lhs);
        case clang::BO_PtrMemI: return new_CallExp(loc, new_PtrExp(loc, rhs), lhs);
    }

    llvm::llvm_unreachable_internal("Unhandled C++ binary operation exp");
}

Expression* ExprMapper::fromBinExp(const clang::BinaryOperator* E)
{
    return fromBinExp(E->getBeginLoc(), E->getOpcode(),
                    E->getLHS(), E->getRHS());
}

static const clang::Expr* skipIgnoredCast(const clang::CastExpr *E)
{
    auto Kind = E->getCastKind();
    auto SubExpr = E->getSubExpr();

    bool skipCast = false;
    if (isa<clang::ImplicitCastExpr>(E))
    {
        skipCast = true;

        if (Kind == clang::CK_NullToMemberPointer)
            skipCast = false; // tptrdiff_t is a signed int type, nullptr needs to be translated to 0

        // One exception being if the subexpr is an enum constant, in which case handling the cast to specify the signedness of the expression
        // will prevent some errors during function resolution which overloads for both signed and unsigned arguments.
        if (Kind == clang::CK_IntegralCast && SubExpr->getType()->isEnumeralType())
            skipCast = false;
    }

    auto CastDestTy = E->getType();

    switch(Kind)
    {
        case clang::CK_NullToPointer:
            skipCast = false;
            break;
        case clang::CK_NoOp:
        case clang::CK_ConstructorConversion:
        case clang::CK_LValueToRValue:
            skipCast = true;
            break;
        case clang::CK_Dependent:
            if (SubExpr->getType().getCanonicalType() == CastDestTy.getCanonicalType())
                skipCast = true;
            break;
        default:
            break;
    }

    return skipCast ? SubExpr : E;
}

static const clang::Expr* skipIgnored(const clang::Expr *E)
{
    const clang::Expr* SubExpr = E;

    if (auto CastExpr = dyn_cast<clang::CastExpr>(E))
        SubExpr = skipIgnoredCast(CastExpr);
    else if (auto ConstructExpr = dyn_cast<clang::CXXConstructExpr>(E))
    {
        if (ConstructExpr->getNumArgs())
        {
            auto Ctor = ConstructExpr->getConstructor();
            auto Arg0 = ConstructExpr->getArg(0);

            if (ConstructExpr->isElidable() ||
                    (Ctor->isCopyConstructor() && Ctor->isTrivial() && isa<clang::MaterializeTemporaryExpr>(Arg0)))
                SubExpr = Arg0;
        }
    }

    return (SubExpr != E) ? skipIgnored(SubExpr) : E;
}

Expression *ExprMapper::fromCastExpr(Loc loc, const clang::CastExpr *E)
{
    auto Kind = E->getCastKind();

    if (Kind == clang::CK_NullToPointer)
        return new_NullExp(loc);
    else if (Kind == clang::CK_NullToMemberPointer)
        return new_CallExp(loc,
            new_TypeExp(loc, mapper.fromType(E->getType(), loc))); // default __cpp_member_ptr value

    auto SubExpr = E->getSubExpr();
    auto CastDestTy = E->getType();
    auto e = fromExpression(SubExpr);

    if (skipIgnoredCast(E) != E)
        return e;

    assert(SubExpr->getType().getCanonicalType()
                    != CastDestTy.getCanonicalType()); // we should be ignoring all casts that do not alter the type

    return new_CastExp(loc, e, mapper.fromType(CastDestTy, loc));
}

Expression* ExprMapper::fromExpression(const clang::Expr *E, bool interpret)  // TODO implement interpret properly
{
    auto loc = fromLoc(E->getBeginLoc());
    E = skipIgnored(E);

    Expression *e = nullptr;
    Type *t = nullptr;
    clang::QualType Ty;

    if (auto Cast = dyn_cast<clang::CastExpr>(E))
        return fromCastExpr(loc, Cast);

    else if (auto PE = dyn_cast<clang::ParenExpr>(E))
        e = fromExpression(PE->getSubExpr());
    else if (auto CDA = dyn_cast<clang::CXXDefaultArgExpr>(E))
        e = fromExpression(CDA->getExpr());
    else if (auto EWC = dyn_cast<clang::ExprWithCleanups>(E))
        e = fromExpression(EWC->getSubExpr());

    else if (auto UO = dyn_cast<clang::UnaryOperator>(E))
        e = fromUnaExp(UO);
    else if (auto BO = dyn_cast<clang::BinaryOperator>(E))
        e = fromBinExp(BO);
    else if (auto CO = dyn_cast<clang::ConditionalOperator>(E))
    {
        auto econd = fromExpression(CO->getCond());
        auto e1 = fromExpression(CO->getTrueExpr());
        auto e2 = fromExpression(CO->getFalseExpr());

        if (!econd || !e1 || !e2)
            return nullptr;
        
        e = new_CondExp(loc, econd, e1, e2);
    }

    else if (auto IL = dyn_cast<clang::IntegerLiteral>(E))
    {
        auto Val = IL->getValue();
        Ty = E->getType();
        t = mapper.fromType(Ty, loc);

        e = new_IntegerExp(loc, Ty->hasSignedIntegerRepresentation() ?
                        Val.getSExtValue() : Val.getZExtValue(), t);

        // D won't be as lenient as C++ is towards signed constants overflowing into negative values,
        // so even if Type::implicitConvTo matches we should still check the evaluated expression
        // (see _ISwgraph in wctype.h for an example of this special case)
//         if (!E->isInstantiationDependent() && !destType->isunsigned())
//         {
//             llvm::APSInt V(Val, Ty->hasUnsignedIntegerRepresentation());
//             if (E->EvaluateAsInt(V, Context))
//                 if (V.isUnsigned() && V.getActiveBits() == Context.getIntWidth(Ty))
//                     e = new_CastExp(loc, e, destType);
//         }
    }
    else if (auto CL = dyn_cast<clang::CharacterLiteral>(E))
    {
        auto Char = CL->getValue();

        switch (CL->getKind())
        {
            case clang::CharacterLiteral::Ascii:
            case clang::CharacterLiteral::UTF8:
                t = Type::tchar;
                e = new_IntegerExp(loc, (d_uns8)Char, t);
                break;
            case clang::CharacterLiteral::Wide:
            case clang::CharacterLiteral::UTF16:
                t = Type::twchar;
                e = new_IntegerExp(loc, (d_uns16)Char, t);
                break;
            case clang::CharacterLiteral::UTF32:
                t = Type::tdchar;
                e = new_IntegerExp(loc, (d_uns32)Char, t);
                break;
        }
    }
    else if (auto BL = dyn_cast<clang::CXXBoolLiteralExpr>(E))
    {
        t = Type::tbool;
        e = new_IntegerExp(loc, BL->getValue() ? 1 : 0, t);
    }
    else if (auto FL = dyn_cast<clang::FloatingLiteral>(E))
    {
        auto APFVal = FL->getValue();
        e = fromAPFloat(loc, APFVal, &t);
    }
    else if (auto SL = dyn_cast<clang::StringLiteral>(E))
    {
        utf8_t postfix = 0;
        if (SL->getCharByteWidth() == 2)
        {
            postfix = 'w';
            t = new_TypeDArray(Type::tdchar->immutableOf());
                // WARNING: string literals with postfixes need to be explicitly cast to pointer types (DMD BUG?)
                // which is being done at the ImplicitCastExpr level.
                // See https://issues.dlang.org/show_bug.cgi?id=6032
        }
        else if (SL->getCharByteWidth() == 4)
        {
            postfix = 'd';
            t = new_TypeDArray(Type::twchar->immutableOf());
        }
        else assert(SL->getCharByteWidth() == 1);

        e = new_StringExp(loc, const_cast<char*>(SL->getBytes().data()),
                             SL->getLength(), postfix);
    }
    else if (isa<clang::CXXNullPtrLiteralExpr>(E) || isa<clang::GNUNullExpr>(E))
    {
        return new_NullExp(loc);
    }

    else if (auto TT = dyn_cast<clang::TypeTraitExpr>(E))
    {
        if (!TT->isValueDependent())
            e = new_IntegerExp(loc, TT->getValue() ? 1 : 0, Type::tbool);
        else
        {
            switch (TT->getTrait())
            {
                case clang::UTT_IsPOD:
                {
                    auto args = new Objects;
                    auto t = mapper.fromType(TT->getArg(0)->getType(), loc);
                    args->push(t);
                    e = new_TraitsExp(loc, Id::isPOD, args);
                    break;
                }
                default:
                    return new_NullExp(loc);  // TODO replace by D traits
            }
        }
    }
    else if (auto SOP = dyn_cast<clang::SizeOfPackExpr>(E))
    {
        if (!SOP->isValueDependent())
            e = new_IntegerExp(loc, SOP->getPackLength(), Type::tuns64);
        else
            return new_NullExp(loc);  // TODO replace by D traits
    }
    else if (auto NE = dyn_cast<clang::CXXNoexceptExpr>(E))
    {
        if (!NE->isValueDependent())
            e = new_IntegerExp(loc, NE->getValue() ? 1 : 0, Type::tbool);
        else
            return new_NullExp(loc);
    }
    else if (auto UEOTT = dyn_cast<clang::UnaryExprOrTypeTraitExpr>(E))
    {
        auto t = mapper.fromType(UEOTT->getTypeOfArgument(), loc);
        auto e1 = new_TypeExp(loc, t);

        switch (UEOTT->getKind())
        {
            case clang::UETT_SizeOf:
                e = new_DotIdExp(loc, e1, Id::__sizeof);
                break;
            case clang::UETT_AlignOf:
                e = new_DotIdExp(loc, e1, Id::__xalignof);
                break;
            default:
                assert(false && "Unsupported");
        }
    }

    else if (auto DR = dyn_cast<clang::DeclRefExpr>(E))
    {
        e = fromExpressionDeclRef(loc, const_cast<clang::ValueDecl*>(DR->getDecl()),
                        DR->getQualifier());
    }

    else if (auto PE = dyn_cast<clang::PackExpansionExpr>(E))
    {
        e = fromExpression(PE->getPattern());
    }
    else if (auto SNTTP = dyn_cast<clang::SubstNonTypeTemplateParmExpr>(E))
    {
        //if (SNTTP->isValueDependent())
        //    e = fromExpressionNonTypeTemplateParm(loc, SNTTP->getParameter());
        //else
        assert(SNTTP->getReplacement());
        e = fromExpression(SNTTP->getReplacement());
    }
    else if (auto MExpr = dyn_cast<clang::MemberExpr>(E))
    {
        return fromExpressionMemberExpr(loc, MExpr, MExpr->getMemberDecl()->getDeclName());
    }
    else if (auto CDSM = dyn_cast<clang::CXXDependentScopeMemberExpr>(E))
    {
        return fromExpressionMemberExpr(loc, CDSM, CDSM->getMember());
    }
    else if (auto DSDR = dyn_cast<clang::DependentScopeDeclRefExpr>(E))
    {
        Expression *e1 = nullptr;
        Identifier *ident;
        ::TemplateInstance *tempinst = nullptr;

        if (auto NNS = DSDR->getQualifier())
        {
            auto tqual = DeclMapper::FromType(mapper, loc).fromNestedNameSpecifier(NNS);
            e1 = new_TypeExp(loc, tqual);
        }

        if (DSDR->getDeclName().isIdentifier())
            ident = fromIdentifier(DSDR->getDeclName().getAsIdentifierInfo());
        else
            assert(false && "Unhandled DeclarationName kind");

        if (DSDR->hasExplicitTemplateArgs())
        {
            auto tiargs = fromASTTemplateArgumentListInfo(loc,
                        DSDR->getTemplateArgs(), DSDR->getNumTemplateArgs(), mapper);

            tempinst = new_TemplateInstance(loc, ident, tiargs);
        }

        if (e1)
        {
            if (tempinst)
                e = new_DotTemplateInstanceExp(loc, e1, tempinst);
            else
                e = new_DotIdExp(loc, e1, ident);
        }
        else
        {
            if (tempinst)
                e = new_TypeExp(loc, new_TypeInstance(loc, tempinst));
            else
                e = new_IdentifierExp(loc, ident);
        }
    }
    else if (auto C = dyn_cast<clang::CallExpr>(E))
    {
        if (auto OC = dyn_cast<clang::CXXOperatorCallExpr>(E))
        {
            // Since calling opBinary!"+"(...) won't work if there multiple opBinary templates, prefer the operator expression when possible so that overloaded operator resolution kicks in.
            // TODO: note that the mapping won't always result in correct values as long as non member operators don't take part in D's overloaded operator resolution
            // Additionally in dependent contexts operator calls aren't resolved yet to UnaryOperator and BinaryOperator, which are easier on the eyes

            auto OO = OC->getOperator();
            if (C->getNumArgs() == 2 && OO >= clang::OO_Plus && OO <= clang::OO_Arrow &&
                    OO != clang::OO_PlusPlus && OO != clang::OO_MinusMinus)
            {
                auto Op = clang::BinaryOperator::getOverloadedOpcode(OO);
                auto LHS = C->getArg(0), RHS = C->getArg(1);
                e = fromBinExp(E->getBeginLoc(), Op, LHS, RHS);
            }
            else if (C->getNumArgs() == 1 && OO < clang::OO_Call)
            {
                auto Op = clang::UnaryOperator::getOverloadedOpcode(OO, false); // WARNING: how to determine whether prefix or postfix?
                auto Sub = C->getArg(0);
                e = fromUnaExp(E->getBeginLoc(), Op, Sub);
            }
        }

        if (!e)
        {
            auto callee = fromExpression(C->getCallee());
            if (!callee)
                return nullptr;

            Expressions* args;
            if (isMemberFuncPtrCall(C->getCallee()))
            {
                assert(callee->op == TOKcall);
                args = static_cast<CallExp*>(callee)->arguments;
            }
            else
                args = new Expressions;

            for (auto Arg: C->arguments())
                args->push(fromExpression(Arg));

            e = new_CallExp(loc, callee, args);
        }
    }
    else if (auto UL = dyn_cast<clang::UnresolvedLookupExpr>(E))
    {
        if (!UL->getName().isIdentifier())
        {
            ::warning(loc, "Unsupported DeclarationName for UnresolvedLookupExpr");
            return new_NullExp(loc);
        }

        // FIXME NNS
        auto id = fromIdentifier(UL->getName().getAsIdentifierInfo());

        if (UL->hasExplicitTemplateArgs())
        {
            auto tiargs = fromASTTemplateArgumentListInfo(loc,
                        UL->getTemplateArgs(), UL->getNumTemplateArgs(), mapper);
            auto tempinst = new_TemplateInstance(loc, id, tiargs);
            e = new_ScopeExp(loc, tempinst);
        }
        else
            e = new_IdentifierExp(loc, id);
    }
    else if (isa<clang::CXXScalarValueInitExpr>(E))
    {
        if (E->getType()->getAs<clang::ReferenceType>())
            return new_NullExp(loc);

        t = mapper.fromType(E->getType().withoutLocalFastQualifiers(), loc);
        e = new_CallExp(loc, new_TypeExp(loc, t));
    }
    else if (auto MT = dyn_cast<clang::MaterializeTemporaryExpr>(E))
    {
        auto TempExpr = skipIgnored(MT->GetTemporaryExpr());  // ignore skipped casts, elidable constructs, etc. in order to not bias TempExpr->isLValue()
        e = fromExpression(TempExpr);

        if (!e)
            return nullptr;

//         if (Ty->getAs<clang::ReferenceType>() && e->op == TOKnull)
//             return nullptr; // FIXME once we may finally pass rvalues to ref arguments
//                     // for other types there are workarounds but for null class references
//                     // I couldn't find any way to turn them into lvalues.
//
//         if (!TempExpr->isLValue())
//         {
//             if (e->op == TOKcall &&
//                     static_cast<CallExp*>(e)->e1->op == TOKtype)
//             {
//                 auto call = static_cast<CallExp*>(e);
//                 e = new_NewExp(loc, nullptr, nullptr,
//                                call->e1->type, call->arguments);
//             }
//             else
//             {
//                 t = mapper.fromType(Ty.withoutLocalFastQualifiers(), loc);
//
//                 auto args = new Expressions;
//                 args->push(e);
//
//                 e = new_NewExp(loc, nullptr, nullptr, t, args);
//             }
//
//             e = new_PtrExp(loc, e);
//         }
    }
    else if (auto CBT = dyn_cast<clang::CXXBindTemporaryExpr>(E))
    {
        e = fromExpression(CBT->getSubExpr());
    }
    else if (auto CCE = dyn_cast<clang::CXXConstructExpr>(E))
    {
        if (CCE->isElidable())
            e = fromExpression(CCE->getArg(0));
        else
        {
            t = mapper.fromType(E->getType().withoutLocalFastQualifiers(), loc);

            auto args = new Expressions;
            for (auto Arg: CCE->arguments())
                args->push(fromExpression(Arg));

            e = new_CallExp(loc, new_TypeExp(loc, t), args);
        }
    }
    else if (auto CNE = dyn_cast<clang::CXXNewExpr>(E))
    {
        auto Ty = CNE->getAllocatedType();
        t = mapper.fromType(Ty.withoutLocalFastQualifiers(), loc);

        Expressions *args = nullptr;
        auto Construct = CNE->getConstructExpr();
        if (Construct && !Construct->isElidable())
        {
            args = new Expressions;
            for (auto Arg: Construct->arguments())
                args->push(fromExpression(Arg));
        }

        e = new_NewExp(loc, nullptr, nullptr, t, args);
    }

    else if (isa<clang::CXXThisExpr>(E))
        return new_ThisExp(loc);

    else if (isa<clang::InitListExpr>(E)) // TODO
        return new_NullExp(loc);

    else if (isa<clang::CXXUnresolvedConstructExpr>(E)
        || isa<clang::CXXPseudoDestructorExpr>(E))
        return new_NullExp(loc) /* nullptr */;

    else
        llvm::llvm_unreachable_internal("Unhandled C++ expression");

    return e;
}

Type *getAPIntDType(const llvm::APSInt &i)
{
    bool needs64bits = i.getBitWidth() > 32;

    if (i.isSigned())
        return needs64bits ? Type::tint64 : Type::tint32;
    else
        return needs64bits ? Type::tuns64 : Type::tuns32;
}

Expression *ExprMapper::fromAPValue(Loc loc, const clang::APValue &Val,
                        clang::QualType Ty, Expression **result)
{
    using clang::APValue;

    switch (Val.getKind())
    {
        case APValue::Int:
            return fromAPInt(loc, Val.getInt(), Ty);
        case APValue::Float:
            return fromAPFloat(loc, Val.getFloat());
        case APValue::Struct:
        {
            if (!result)
                return nullptr; // FIXME DeclMapper::VisitValueDecl relies on fromAPValue being limited, fix it

            assert((*result)->op == TOKstructliteral);
            auto sle = static_cast<StructLiteralExp*>(*result);

            size_t elem_i = 0;
            std::function<void(const APValue &Val, AggregateDeclaration* ad)>
                fromFields = [&] (const APValue &Val, AggregateDeclaration* ad)
            {
                auto cd = ad->isClassDeclaration();

                unsigned numBases = 0;
                if (cd && cd->baseclasses)
                    numBases = cd->baseclasses->dim;

                if (auto base = toAggregateBase(ad))  // FIXME numBases > 1
                    fromFields(Val.getStructBase(0), base);

                for (size_t i = 0; i < ad->fields.dim; i++, elem_i++) {
                    assert(elem_i < sle->elements->dim);
                    (*sle->elements)[elem_i] = fromAPValue(loc, Val.getStructField(i),
                                    clang::QualType(), &(sle->elements->data[i]));
                }

                return true;
            };

            fromFields(Val, sle->sd);
            return sle;
        }
        default:
            return nullptr;
    }
}

Expression* ExprMapper::fromAPInt(Loc loc, const llvm::APSInt &Val,
                                  clang::QualType Ty)
{
    auto e = new_IntegerExp(loc,
            Val.isSigned() ? Val.getSExtValue() : Val.getZExtValue(),
            getAPIntDType(Val));

    if (!Ty.isNull())
        return fixIntegerExp(e, Ty);
    else
        return e;
}

Expression* ExprMapper::fromAPFloat(Loc loc, const APFloat& Val, Type **pt)
{
    real_t val;
    Type *t = Type::tfloat32;

    if (Val.isZero())
        val = 0.0;
    else if (&Val.getSemantics() == &llvm::APFloat::IEEEsingle())
        val = Val.convertToFloat();
    else if (&Val.getSemantics() == &llvm::APFloat::IEEEdouble())
    {
        val = Val.convertToDouble();
        t = Type::tfloat64;
    }
    else
    {
        ::warning(loc, "Floating point semantics for non-zero APFloat handled by converting to string and strtold");

        llvm::SmallString<16> Str;
        Val.toString(Str, 0, llvm::APFloat::semanticsPrecision(Val.getSemantics()));
        val = strtold(Str.c_str(), nullptr);
        t = Type::tfloat80;
    }

    if (pt) *pt = t;
    return new_RealExp(loc, val, t);
}

Expression* ExprMapper::fromExpressionDeclRef(Loc loc, clang::NamedDecl* D,
                                    const clang::NestedNameSpecifier*, TypeQualifiedBuilderOpts tqualOpts)
{
    if (auto NTTP = dyn_cast<clang::NonTypeTemplateParmDecl>(D))
        return fromExpressionNonTypeTemplateParm(loc, NTTP); // can only happen in dependent contexts

    auto sym = mapper.dsymForDecl(D);
    if (sym) {
        if (auto d = sym->isDeclaration()) {
            if (d->isFuncDeclaration() || d->isVarDeclaration())
                return new_VarExp(loc, d);
            else if (auto t = d->getType())
                return new_TypeExp(loc, t);
        } else if (auto td = sym->isTemplateDeclaration())
                return new_TemplateExp(loc, td);
        else if (auto em = sym->isEnumMember())
            return em->getVarExp(loc, em->_scope);
    }

    auto tqual = DeclMapper::FromType(mapper, loc).typeQualifiedFor(D, nullptr, nullptr, tqualOpts);
    assert(tqual && "DeclRefExpr decl without a DeclarationName");

    // Convert the TypeQualified path to DotXXXExp because
    // NOTE: they are preferable because unlike TypeExp, DotXXXExps call semantic() from left to right
    Expression *e = dotIdentOrInst(loc, nullptr, typeQualifiedRoot(tqual));
    for (auto id: tqual->idents)
        e = dotIdentOrInst(loc, e, id);

    return e;
}

template<typename T>
 Expression *ExprMapper::fromExpressionMemberExpr(Loc loc, const T* E, const clang::DeclarationName MemberName)
{
    assert(E->getBase() && "Unhandled case");

    auto member = getIdentOrTempinst(loc, MemberName, mapper);
    auto e1 = fromExpression(E->getBase());

    if (!e1 || !member)
        return nullptr;

    if (E->hasExplicitTemplateArgs())
    {
        assert(member->dyncast() == DYNCAST_IDENTIFIER);

        auto tiargs = fromASTTemplateArgumentListInfo(loc,
            E->getTemplateArgs(), E->getNumTemplateArgs(), mapper);
        auto tempinst = new_TemplateInstance(loc,
            static_cast<Identifier*>(member), tiargs);

        member = tempinst;
    }

    if (auto NNS = E->getQualifier())
    {
        auto tqual = DeclMapper::FromType(mapper, loc).fromNestedNameSpecifier(NNS);
        e1 = dotIdentOrInst(loc, e1, typeQualifiedRoot(tqual));

        for (auto id : tqual->idents)
            e1 = dotIdentOrInst(loc, e1, id);
    }

    return dotIdentOrInst(loc, e1, member);
}

Expression* ExprMapper::fromExpressionNonTypeTemplateParm(Loc loc, const clang::NonTypeTemplateParmDecl* D)
{
    auto ident = DeclMapper::getIdentifierForTemplateNonTypeParm(D);
    return new_IdentifierExp(loc, ident);
}

bool ExprMapper::toAPValue(clang::APValue& Result, Expression* e)
{
    using clang::APValue;

    auto& Context = calypso.getASTContext();

    if (e->op == TOKvar)
        e = e->optimize(WANTvalue);

    switch (e->op)
    {
        case TOKnull:
        {
            auto PtrType = mapper.toType(e->loc, e->type, nullptr);
            auto TargetVal = Context.getTargetNullPointerValue(PtrType);
            Result = APValue((clang::Expr *)nullptr, clang::CharUnits::fromQuantity(TargetVal),
                             APValue::NoLValuePath(), /*IsNullPtr = */ true);

            return true;
        }
        case TOKint64:
        {
            auto exp = static_cast<IntegerExp*>(e);
            auto value = exp->getInteger();

            auto IntType = mapper.toType(e->loc, e->type, nullptr);
            unsigned IntSize = Context.getTypeSize(IntType);

            llvm::APInt IntVal(IntSize, value);
            Result = APValue(llvm::APSInt(IntVal, exp->type->isunsigned()));

            return true;
        }
        case TOKfloat64:
        {
            auto exp = static_cast<RealExp *>(e);

            auto tofltSemantics = [] (Type *t) {
                switch(t->ty) {
                    case Tfloat32: return &llvm::APFloat::IEEEsingle();
                    case Tfloat64: return &llvm::APFloat::IEEEdouble();
                    default: llvm_unreachable("Unhandled tofltSemantics");
                }
            };

            llvm::APFloat Val(*tofltSemantics(exp->type));

            switch(exp->type->ty) {
                case Tfloat32:
                    Val = llvm::APFloat((float) exp->value);
                    break;
                case Tfloat64:
                    Val = llvm::APFloat((double) exp->value);
                    break;
                default:
                    llvm_unreachable("Unhandled float type");
            }

            Result = APValue(Val);
            return true;
        }
        case TOKstructliteral:
        {
            auto sle = static_cast<StructLiteralExp*>(e);

            unsigned elem_i = 0;
            std::function<bool(APValue &Val, AggregateDeclaration* ad)>
                toFields = [&] (APValue &Val, AggregateDeclaration* ad)
            {
                auto cd = ad->isClassDeclaration();

                unsigned numBases = 0;
                if (cd && cd->baseclasses)
                    numBases = cd->baseclasses->dim;

                Val = APValue(APValue::UninitStruct(),
                                        numBases, ad->fields.dim);

                if (auto base = toAggregateBase(ad))  // FIXME numBases > 1
                    if (!toFields(Val.getStructBase(0), base))
                        return false;

                for (unsigned i = 0; i < ad->fields.dim; i++, elem_i++)
                    if (!toAPValue(Val.getStructField(i), (*sle->elements)[elem_i]))
                        return false;

                return true;
            };

            if (!toFields(Result, sle->sd))
                return false;

            assert(elem_i == sle->elements->dim);
            return true;
        }
        default:
            return false;
    }
}

clang::Expr* ExprMapper::toExpression(Expression* e)
{
    auto& Context = calypso.getASTContext();
    clang::SourceLocation Loc;

    if (e->op == TOKvar)
        e = e->optimize(WANTvalue);

    switch (e->op)
    {
        case TOKint64:
        {
            auto exp = static_cast<IntegerExp*>(e);
            auto value = exp->getInteger();

            if (e->type->ty == Tenum)
            {
                auto ed = static_cast<TypeEnum*>(e->type)->sym;
                if (isCPP(ed))
                {
                    auto ED = static_cast<cpp::EnumDeclaration*>(ed)->ED;
                    
                    for (auto ECD: ED->enumerators())
                    {
                        auto& AI = ECD->getInitVal();
                        auto Val = AI.isSigned() ? AI.getSExtValue() : AI.getZExtValue();

                        if (Val == value)
                            return clang::DeclRefExpr::Create(Context,
                                clang::NestedNameSpecifierLoc(), clang::SourceLocation(),
                                ECD, false, Loc, Context.getEnumType(ED), clang::VK_RValue);
                    }
                }
            }

            if (e->type->ty == Tbool)
                return new (Context) clang::CXXBoolLiteralExpr(value != 0,
                                                               Context.BoolTy, Loc);

            clang::APValue Value;
            toAPValue(Value, e);

            auto IntType = mapper.toType(e->loc, e->type, nullptr);
            return clang::IntegerLiteral::Create(Context,
                                            Value.getInt(),
                                            IntType, Loc);
        }
        case TOKfloat64:
        {
            clang::APValue Value;
            toAPValue(Value, e);

            auto FloatType = mapper.toType(e->loc, e->type, nullptr);
            return clang::FloatingLiteral::Create(Context,
                                            Value.getFloat(), true,
                                            FloatType, Loc);
        }
        default:
            llvm::llvm_unreachable_internal("Unhandled D -> Clang expression conversion");
    }
}

class OpOverloadCpp : public ::Visitor
{
public:
    Scope* sc;
    Expression* result = nullptr;

    MatchAccumulator m;

    OpOverloadCpp(Scope* sc) : sc(sc) {}

    using Visitor::visit;

    static Expression* build_overload(const Loc& loc, Scope* sc, Expression* ethis,
                                        Expression* earg, Declaration* decl)
    {
        assert(decl);

        auto args = new Expressions;
        args->push(earg);

        Expression* e = new_DotVarExp(loc, ethis, decl, false);
        e = new_CallExp(loc, e, args);
        e = expressionSemantic(e, sc);
        return e;
    }

    static Expression* build_nonmember_overload(const Loc& loc, Scope* sc, Expression* eleft,
                                        Expression* eright, Declaration* decl)
    {
        assert(decl);

        auto args = new Expressions;
        args->push(eleft);
        if (eright)
            args->push(eright);

        Expression* e = new_VarExp(loc, decl, false);
        e = new_CallExp(loc, e, args);
        e = expressionSemantic(e, sc);
        return e;
    }

    static Expression* eright(Expression* e) { return nullptr; }
    static Expression* eright(BinExp* e) { return e->e2; }

    static bool isMethod(::FuncDeclaration* fd)
    {
        return fd->isThis();
    }

    // Merge m with a new MatchAccumulator
    void collectMatches(Dsymbol* s, Expression* e, Expression* eleft,
                        Scope* sc, Objects* tiargs, Type* tthis, Expressions* fargs)
    {
        // Since there may be both foo(a, b) and a.foo(b) candidates,
        // we can't simply use functionResolve's way of disambiguating candidates

        MatchAccumulator m2;
        functionResolve(m2, s, e->loc, sc, tiargs, nullptr, fargs);

        if (m.last < m2.last)
            m = m2;
        else if (m.last == m2.last && m.lastf != m2.lastf)
        {
//             ::TemplateDeclaration* td1 = nullptr, *td2 = nullptr;
//
//             if (auto ti1 = m.lastf->isTemplateInstance())
//                 td1 = ti1->tempdecl->isTemplateDeclaration();
//
//             if (auto ti2 = m.lastf->isTemplateInstance())
//                 td2 = ti2->tempdecl->isTemplateDeclaration();
//
//             if (td1 && td2)
//             {
//                 // Disambiguate by picking the most specialized TemplateDeclaration
//                 Expressions args1;
//                 Expressions* fargs1 = fargs;
//
//                 if (isMethod(m.lastf) != isMethod(m2.lastf))
//                 {
//                     fargs1 = &args1;
//
//                     if (isMethod(m2.lastf))
//                     {
//                         args1.push_back(eleft);
//                         args1.append(fargs);
//                     }
//                     else
//                     {
//                         args1.append(fargs);
//                         args1.remove(0);
//                     }
//                 }
//
//                 MATCH c1 = td1->leastAsSpecialized(sc, td2, fargs);
//                 MATCH c2 = td2->leastAsSpecialized(sc, td1, fargs);
//             }

            // TODO: this is getting complex, either this needs special versions of leastAsSpecialized functions or we switch to Clang's candidate selection
        }
    }

    template<typename ExpTy>
    void checkMatches(ExpTy* e)
    {
        if (m.lastf && (m.lastf->errors || m.lastf->semantic3Errors))
        {
            result = new_ErrorExp();
            return;
        }

        if (m.count > 1)
        {
            // Error, ambiguous
            e->error("overloads `%s` and `%s` both match argument list for `%s`", m.lastf->type->toChars(), m.nextf->type->toChars(), m.lastf->toChars());
        }
        else if (m.last <= MATCHnomatch)
        {
            return;
        }

        if (m.lastf->isThis())
            result = build_overload(e->loc, sc, e->e1, eright(e), m.lastf);
        else
            result = build_nonmember_overload(e->loc, sc, e->e1, eright(e), m.lastf);

        return;
    }

    void visit(UnaExp* e) override
    {
        auto ad1 = isAggregate(e->e1->type);

        if (!ad1 || isCPP(ad1))
            return;

        if (auto s = search_function(ad1, Id::opUnary))
        {
            auto tiargs = opToArg(sc, e->op);
            functionResolve(m, s, e->loc, sc, tiargs, e->e1->type, nullptr);
        }

        auto parent = ad1->toParent2();
        if (parent->isModule())
        {
            auto mod1 = static_cast<cpp::Module*>(parent);
            if (auto s = search_function(mod1, Id::opUnary))
            {
                auto tiargs = opToArg(sc, e->op);

                Expressions args1;
                args1.setDim(1);
                args1[0] = e->e1;
                expandTuples(&args1);

                functionResolve(m, s, e->loc, sc, tiargs, nullptr, &args1);
            }
        }

        checkMatches(e);
    }

    static Identifier* binaryOpId(TOK op)
    {
        switch (op)
        {
            case TOKassign:
                return Id::assign;
            case TOKequal:
                return Id::eq;
            default:
                return Id::opBinary;
        }
    }

    void visitBinExp(BinExp* e, Identifier* opId = nullptr)
    {
        auto ad1 = isAggregate(e->e1->type);
        auto ad2 = isAggregate(e->e2->type);

        if (!opId)
            opId = binaryOpId(e->op);
        bool needsExplicitArg = opId == Id::opBinary || opId == Id::opOpAssign;

        auto tryModuleLevelOverOp = [&] (::Module* mod)
        {
            if (auto s = search_function(mod, opId))
            {
                auto tiargs = needsExplicitArg ? opToArg(sc, e->op) : nullptr;

                Expressions args2;
                args2.setDim(2);
                args2[0] = e->e1;
                args2[1] = e->e2;
                expandTuples(&args2);

                functionResolve(m, s, e->loc, sc, tiargs, nullptr, &args2);
            }
        };

        if (ad1 && isCPP(ad1))
        {
            if (auto s = search_function(ad1, opId))
            {
                auto tiargs = needsExplicitArg ? opToArg(sc, e->op) : nullptr;

                Expressions args1;
                args1.setDim(1);
                args1[0] = e->e2;
                expandTuples(&args1);

                functionResolve(m, s, e->loc, sc, tiargs, e->e1->type, &args1);
            }

            if (auto mod1 = ad1->toParent2()->isModule())
                tryModuleLevelOverOp(mod1);
        }

        if (ad2 && isCPP(ad2) && m.last == MATCHnomatch)
        {
            if (auto mod2 = ad2->toParent2()->isModule())
                tryModuleLevelOverOp(mod2);
        }

        checkMatches(e);
    }

    void visit(BinExp* e) override
    {
        visitBinExp(e);
    }

    void visit(BinAssignExp* e) override
    {
        visitBinExp(e, Id::opOpAssign);
    }
};

Expression* LangPlugin::op_overload(Expression* e, Scope* sc, TOK* pop)
{
    // NOTE: Simple UFCS cannot be used, the right opBinary specialization needs to be found,
    // not the first occurrence in the current scope

    OpOverloadCpp v(sc);
    e->accept(&v);
    return v.result;
}

}
