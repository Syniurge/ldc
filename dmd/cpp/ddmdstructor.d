// Automatically generated by gen_dmdstructor

import dmd.root.ctfloat;
import dmd.root.file;
import dmd.root.port;
import dmd.root.rootobject;
import dmd.aggregate;
import dmd.arraytypes;
import dmd.attrib;
import dmd.cond;
import dmd.complex;
import dmd.dclass;
import dmd.denum;
import dmd.declaration;
import dmd.dmodule;
import dmd.dscope;
import dmd.dstruct;
import dmd.dsymbol;
import dmd.expression;
import dmd.func;
import dmd.globals;
import dmd.identifier;
import dmd.dimport;
import dmd.init;
import dmd.mtype;
import dmd.statement;
import dmd.staticassert;
import dmd.dtemplate;
import dmd.tokens;
import dmd.visitor;

import core.stdc.string;

import dmd.utils : toDString;

extern(C++):

RootObject new_RootObject() { return new RootObject(); }
Tuple new_Tuple() { return new Tuple(); }
StoppableVisitor new_StoppableVisitor() { return new StoppableVisitor(); }
Dsymbol new_Dsymbol() { return new Dsymbol(); }
Dsymbol new_Dsymbol(Identifier arg0) { return new Dsymbol(arg0); }
ScopeDsymbol new_ScopeDsymbol() { return new ScopeDsymbol(); }
ScopeDsymbol new_ScopeDsymbol(Identifier id) { return new ScopeDsymbol(id); }
WithScopeSymbol new_WithScopeSymbol(WithStatement withstate) { return new WithScopeSymbol(withstate); }
ArrayScopeSymbol new_ArrayScopeSymbol(Scope *sc, Expression e) { return new ArrayScopeSymbol(sc, e); }
ArrayScopeSymbol new_ArrayScopeSymbol(Scope *sc, TypeTuple t) { return new ArrayScopeSymbol(sc, t); }
ArrayScopeSymbol new_ArrayScopeSymbol(Scope *sc, TupleDeclaration td) { return new ArrayScopeSymbol(sc, td); }
OverloadSet new_OverloadSet(Identifier ident, OverloadSet os) { return new OverloadSet(ident, os); }
DsymbolTable new_DsymbolTable() { return new DsymbolTable(); }
Identifier new_Identifier(const (char) *_string, ulong length, int value) { return new Identifier(_string, length, value); }
Expression new_Expression(const ref Loc loc, TOK op, int size) { return new Expression(loc, op, size); }
IntegerExp new_IntegerExp(const ref Loc loc, dinteger_t value, Type type) { return new IntegerExp(loc, value, type); }
IntegerExp new_IntegerExp(dinteger_t value) { return new IntegerExp(value); }
ErrorExp new_ErrorExp() { return new ErrorExp(); }
RealExp new_RealExp(const ref Loc loc, real_t value, Type type) { return new RealExp(loc, value, type); }
ComplexExp new_ComplexExp(const ref Loc loc, complex_t value, Type type) { return new ComplexExp(loc, value, type); }
IdentifierExp new_IdentifierExp(const ref Loc loc, Identifier ident) { return new IdentifierExp(loc, ident); }
DollarExp new_DollarExp(const ref Loc loc) { return new DollarExp(loc); }
DsymbolExp new_DsymbolExp(const ref Loc loc, Dsymbol s, bool hasOverloads) { return new DsymbolExp(loc, s, hasOverloads); }
ThisExp new_ThisExp(const ref Loc loc) { return new ThisExp(loc); }
SuperExp new_SuperExp(const ref Loc loc) { return new SuperExp(loc); }
NullExp new_NullExp(const ref Loc loc, Type t) { return new NullExp(loc, t); }
StringExp new_StringExp(const ref Loc loc, char *s) { return new StringExp(loc, s); }
StringExp new_StringExp(const ref Loc loc, void *s, size_t len) { return new StringExp(loc, s, len); }
StringExp new_StringExp(const ref Loc loc, void *s, size_t len, char postfix) { return new StringExp(loc, s, len, postfix); }
TupleExp new_TupleExp(const ref Loc loc, Expression e0, Expressions *exps) { return new TupleExp(loc, e0, exps); }
TupleExp new_TupleExp(const ref Loc loc, Expressions *exps) { return new TupleExp(loc, exps); }
TupleExp new_TupleExp(const ref Loc loc, TupleDeclaration tup) { return new TupleExp(loc, tup); }
ArrayLiteralExp new_ArrayLiteralExp(const ref Loc loc, Type type, Expressions *elements) { return new ArrayLiteralExp(loc, type, elements); }
ArrayLiteralExp new_ArrayLiteralExp(const ref Loc loc, Type type, Expression e) { return new ArrayLiteralExp(loc, type, e); }
ArrayLiteralExp new_ArrayLiteralExp(const ref Loc loc, Type type, Expression basis, Expressions *elements) { return new ArrayLiteralExp(loc, type, basis, elements); }
AssocArrayLiteralExp new_AssocArrayLiteralExp(const ref Loc loc, Expressions *keys, Expressions *values) { return new AssocArrayLiteralExp(loc, keys, values); }
StructLiteralExp new_StructLiteralExp(const ref Loc loc, AggregateDeclaration sd, Expressions *elements, Type stype) { return new StructLiteralExp(loc, sd, elements, stype); }
TypeExp new_TypeExp(const ref Loc loc, Type type) { return new TypeExp(loc, type); }
ScopeExp new_ScopeExp(const ref Loc loc, ScopeDsymbol sds) { return new ScopeExp(loc, sds); }
TemplateExp new_TemplateExp(const ref Loc loc, TemplateDeclaration td, FuncDeclaration fd) { return new TemplateExp(loc, td, fd); }
NewExp new_NewExp(const ref Loc loc, Expression thisexp, Expressions *newargs, Type newtype, Expressions *arguments) { return new NewExp(loc, thisexp, newargs, newtype, arguments); }
NewAnonClassExp new_NewAnonClassExp(const ref Loc loc, Expression thisexp, Expressions *newargs, ClassDeclaration cd, Expressions *arguments) { return new NewAnonClassExp(loc, thisexp, newargs, cd, arguments); }
SymbolExp new_SymbolExp(const ref Loc loc, TOK op, int size, Declaration var, bool hasOverloads) { return new SymbolExp(loc, op, size, var, hasOverloads); }
SymOffExp new_SymOffExp(const ref Loc loc, Declaration var, dinteger_t offset, bool hasOverloads) { return new SymOffExp(loc, var, offset, hasOverloads); }
VarExp new_VarExp(const ref Loc loc, Declaration var, bool hasOverloads) { return new VarExp(loc, var, hasOverloads); }
OverExp new_OverExp(const ref Loc loc, OverloadSet s) { return new OverExp(loc, s); }
FuncExp new_FuncExp(const ref Loc loc, Dsymbol s) { return new FuncExp(loc, s); }
DeclarationExp new_DeclarationExp(const ref Loc loc, Dsymbol declaration) { return new DeclarationExp(loc, declaration); }
TypeidExp new_TypeidExp(const ref Loc loc, RootObject obj) { return new TypeidExp(loc, obj); }
TraitsExp new_TraitsExp(const ref Loc loc, Identifier ident, Objects *args) { return new TraitsExp(loc, ident, args); }
HaltExp new_HaltExp(const ref Loc loc) { return new HaltExp(loc); }
IsExp new_IsExp(const ref Loc loc, Type targ, Identifier id, TOK tok, Type tspec, TOK tok2, TemplateParameters *parameters) { return new IsExp(loc, targ, id, tok, tspec, tok2, parameters); }
BinAssignExp new_BinAssignExp(const ref Loc loc, TOK op, int size, Expression e1, Expression e2) { return new BinAssignExp(loc, op, size, e1, e2); }
CompileExp new_CompileExp(const ref Loc loc, Expressions *exps) { return new CompileExp(loc, exps); }
ImportExp new_ImportExp(const ref Loc loc, Expression e) { return new ImportExp(loc, e); }
AssertExp new_AssertExp(const ref Loc loc, Expression e, Expression msg) { return new AssertExp(loc, e, msg); }
DotIdExp new_DotIdExp(const ref Loc loc, Expression e, Identifier ident) { return new DotIdExp(loc, e, ident); }
DotTemplateExp new_DotTemplateExp(const ref Loc loc, Expression e, TemplateDeclaration td) { return new DotTemplateExp(loc, e, td); }
DotVarExp new_DotVarExp(const ref Loc loc, Expression e, Declaration var, bool hasOverloads) { return new DotVarExp(loc, e, var, hasOverloads); }
DotTemplateInstanceExp new_DotTemplateInstanceExp(const ref Loc loc, Expression e, Identifier name, Objects *tiargs) { return new DotTemplateInstanceExp(loc, e, name, tiargs); }
DotTemplateInstanceExp new_DotTemplateInstanceExp(const ref Loc loc, Expression e, TemplateInstance ti) { return new DotTemplateInstanceExp(loc, e, ti); }
DelegateExp new_DelegateExp(const ref Loc loc, Expression e, FuncDeclaration func, bool hasOverloads) { return new DelegateExp(loc, e, func, hasOverloads); }
DotTypeExp new_DotTypeExp(const ref Loc loc, Expression e, Dsymbol sym) { return new DotTypeExp(loc, e, sym); }
CallExp new_CallExp(const ref Loc loc, Expression e, Expressions *exps) { return new CallExp(loc, e, exps); }
CallExp new_CallExp(const ref Loc loc, Expression e) { return new CallExp(loc, e); }
CallExp new_CallExp(const ref Loc loc, Expression e, Expression earg1) { return new CallExp(loc, e, earg1); }
CallExp new_CallExp(const ref Loc loc, Expression e, Expression earg1, Expression earg2) { return new CallExp(loc, e, earg1, earg2); }
AddrExp new_AddrExp(const ref Loc loc, Expression e) { return new AddrExp(loc, e); }
PtrExp new_PtrExp(const ref Loc loc, Expression e) { return new PtrExp(loc, e); }
PtrExp new_PtrExp(const ref Loc loc, Expression e, Type t) { return new PtrExp(loc, e, t); }
NegExp new_NegExp(const ref Loc loc, Expression e) { return new NegExp(loc, e); }
UAddExp new_UAddExp(const ref Loc loc, Expression e) { return new UAddExp(loc, e); }
ComExp new_ComExp(const ref Loc loc, Expression e) { return new ComExp(loc, e); }
NotExp new_NotExp(const ref Loc loc, Expression e) { return new NotExp(loc, e); }
DeleteExp new_DeleteExp(const ref Loc loc, Expression e, bool isRAII) { return new DeleteExp(loc, e, isRAII); }
CastExp new_CastExp(const ref Loc loc, Expression e, Type t) { return new CastExp(loc, e, t); }
CastExp new_CastExp(const ref Loc loc, Expression e, ubyte mod) { return new CastExp(loc, e, mod); }
VectorExp new_VectorExp(const ref Loc loc, Expression e, Type t) { return new VectorExp(loc, e, t); }
SliceExp new_SliceExp(const ref Loc loc, Expression e1, IntervalExp ie) { return new SliceExp(loc, e1, ie); }
SliceExp new_SliceExp(const ref Loc loc, Expression e1, Expression lwr, Expression upr) { return new SliceExp(loc, e1, lwr, upr); }
ArrayLengthExp new_ArrayLengthExp(const ref Loc loc, Expression e1) { return new ArrayLengthExp(loc, e1); }
IntervalExp new_IntervalExp(const ref Loc loc, Expression lwr, Expression upr) { return new IntervalExp(loc, lwr, upr); }
DelegatePtrExp new_DelegatePtrExp(const ref Loc loc, Expression e1) { return new DelegatePtrExp(loc, e1); }
DelegateFuncptrExp new_DelegateFuncptrExp(const ref Loc loc, Expression e1) { return new DelegateFuncptrExp(loc, e1); }
ArrayExp new_ArrayExp(const ref Loc loc, Expression e1, Expression index) { return new ArrayExp(loc, e1, index); }
ArrayExp new_ArrayExp(const ref Loc loc, Expression e1, Expressions *args) { return new ArrayExp(loc, e1, args); }
DotExp new_DotExp(const ref Loc loc, Expression e1, Expression e2) { return new DotExp(loc, e1, e2); }
CommaExp new_CommaExp(const ref Loc loc, Expression e1, Expression e2) { return new CommaExp(loc, e1, e2); }
IndexExp new_IndexExp(const ref Loc loc, Expression e1, Expression e2) { return new IndexExp(loc, e1, e2); }
PostExp new_PostExp(TOK op, const ref Loc loc, Expression e) { return new PostExp(op, loc, e); }
PreExp new_PreExp(TOK op, const ref Loc loc, Expression e) { return new PreExp(op, loc, e); }
AssignExp new_AssignExp(const ref Loc loc, Expression e1, Expression e2) { return new AssignExp(loc, e1, e2); }
ConstructExp new_ConstructExp(const ref Loc loc, Expression e1, Expression e2) { return new ConstructExp(loc, e1, e2); }
ConstructExp new_ConstructExp(const ref Loc loc, VarDeclaration v, Expression e2) { return new ConstructExp(loc, v, e2); }
BlitExp new_BlitExp(const ref Loc loc, Expression e1, Expression e2) { return new BlitExp(loc, e1, e2); }
BlitExp new_BlitExp(const ref Loc loc, VarDeclaration v, Expression e2) { return new BlitExp(loc, v, e2); }
AddAssignExp new_AddAssignExp(const ref Loc loc, Expression e1, Expression e2) { return new AddAssignExp(loc, e1, e2); }
MinAssignExp new_MinAssignExp(const ref Loc loc, Expression e1, Expression e2) { return new MinAssignExp(loc, e1, e2); }
MulAssignExp new_MulAssignExp(const ref Loc loc, Expression e1, Expression e2) { return new MulAssignExp(loc, e1, e2); }
DivAssignExp new_DivAssignExp(const ref Loc loc, Expression e1, Expression e2) { return new DivAssignExp(loc, e1, e2); }
ModAssignExp new_ModAssignExp(const ref Loc loc, Expression e1, Expression e2) { return new ModAssignExp(loc, e1, e2); }
AndAssignExp new_AndAssignExp(const ref Loc loc, Expression e1, Expression e2) { return new AndAssignExp(loc, e1, e2); }
OrAssignExp new_OrAssignExp(const ref Loc loc, Expression e1, Expression e2) { return new OrAssignExp(loc, e1, e2); }
XorAssignExp new_XorAssignExp(const ref Loc loc, Expression e1, Expression e2) { return new XorAssignExp(loc, e1, e2); }
PowAssignExp new_PowAssignExp(const ref Loc loc, Expression e1, Expression e2) { return new PowAssignExp(loc, e1, e2); }
ShlAssignExp new_ShlAssignExp(const ref Loc loc, Expression e1, Expression e2) { return new ShlAssignExp(loc, e1, e2); }
ShrAssignExp new_ShrAssignExp(const ref Loc loc, Expression e1, Expression e2) { return new ShrAssignExp(loc, e1, e2); }
UshrAssignExp new_UshrAssignExp(const ref Loc loc, Expression e1, Expression e2) { return new UshrAssignExp(loc, e1, e2); }
CatAssignExp new_CatAssignExp(const ref Loc loc, Expression e1, Expression e2) { return new CatAssignExp(loc, e1, e2); }
AddExp new_AddExp(const ref Loc loc, Expression e1, Expression e2) { return new AddExp(loc, e1, e2); }
MinExp new_MinExp(const ref Loc loc, Expression e1, Expression e2) { return new MinExp(loc, e1, e2); }
CatExp new_CatExp(const ref Loc loc, Expression e1, Expression e2) { return new CatExp(loc, e1, e2); }
MulExp new_MulExp(const ref Loc loc, Expression e1, Expression e2) { return new MulExp(loc, e1, e2); }
DivExp new_DivExp(const ref Loc loc, Expression e1, Expression e2) { return new DivExp(loc, e1, e2); }
ModExp new_ModExp(const ref Loc loc, Expression e1, Expression e2) { return new ModExp(loc, e1, e2); }
PowExp new_PowExp(const ref Loc loc, Expression e1, Expression e2) { return new PowExp(loc, e1, e2); }
ShlExp new_ShlExp(const ref Loc loc, Expression e1, Expression e2) { return new ShlExp(loc, e1, e2); }
ShrExp new_ShrExp(const ref Loc loc, Expression e1, Expression e2) { return new ShrExp(loc, e1, e2); }
UshrExp new_UshrExp(const ref Loc loc, Expression e1, Expression e2) { return new UshrExp(loc, e1, e2); }
AndExp new_AndExp(const ref Loc loc, Expression e1, Expression e2) { return new AndExp(loc, e1, e2); }
OrExp new_OrExp(const ref Loc loc, Expression e1, Expression e2) { return new OrExp(loc, e1, e2); }
XorExp new_XorExp(const ref Loc loc, Expression e1, Expression e2) { return new XorExp(loc, e1, e2); }
LogicalExp new_LogicalExp(const ref Loc loc, TOK op, Expression e1, Expression e2) { return new LogicalExp(loc, op, e1, e2); }
CmpExp new_CmpExp(TOK op, const ref Loc loc, Expression e1, Expression e2) { return new CmpExp(op, loc, e1, e2); }
InExp new_InExp(const ref Loc loc, Expression e1, Expression e2) { return new InExp(loc, e1, e2); }
RemoveExp new_RemoveExp(const ref Loc loc, Expression e1, Expression e2) { return new RemoveExp(loc, e1, e2); }
EqualExp new_EqualExp(TOK op, const ref Loc loc, Expression e1, Expression e2) { return new EqualExp(op, loc, e1, e2); }
IdentityExp new_IdentityExp(TOK op, const ref Loc loc, Expression e1, Expression e2) { return new IdentityExp(op, loc, e1, e2); }
CondExp new_CondExp(const ref Loc loc, Expression econd, Expression e1, Expression e2) { return new CondExp(loc, econd, e1, e2); }
DefaultInitExp new_DefaultInitExp(const ref Loc loc, TOK subop, int size) { return new DefaultInitExp(loc, subop, size); }
FileInitExp new_FileInitExp(const ref Loc loc, TOK tok) { return new FileInitExp(loc, tok); }
LineInitExp new_LineInitExp(const ref Loc loc) { return new LineInitExp(loc); }
ModuleInitExp new_ModuleInitExp(const ref Loc loc) { return new ModuleInitExp(loc); }
FuncInitExp new_FuncInitExp(const ref Loc loc) { return new FuncInitExp(loc); }
PrettyFuncInitExp new_PrettyFuncInitExp(const ref Loc loc) { return new PrettyFuncInitExp(loc); }
TypeError new_TypeError() { return new TypeError(); }
TypeBasic new_TypeBasic(TY ty) { return new TypeBasic(ty); }
TypeVector new_TypeVector(Type basetype) { return new TypeVector(basetype); }
TypeSArray new_TypeSArray(Type t, Expression dim) { return new TypeSArray(t, dim); }
TypeDArray new_TypeDArray(Type t) { return new TypeDArray(t); }
TypeAArray new_TypeAArray(Type t, Type index) { return new TypeAArray(t, index); }
TypePointer new_TypePointer(Type t) { return new TypePointer(t); }
TypeReference new_TypeReference(Type t) { return new TypeReference(t); }
TypeFunction new_TypeFunction(ParameterList pl, Type treturn, LINK linkage, StorageClass stc) { return new TypeFunction(pl, treturn, linkage, stc); }
TypeDelegate new_TypeDelegate(Type t) { return new TypeDelegate(t); }
TypeIdentifier new_TypeIdentifier(const ref Loc loc, Identifier ident) { return new TypeIdentifier(loc, ident); }
TypeInstance new_TypeInstance(const ref Loc loc, TemplateInstance tempinst) { return new TypeInstance(loc, tempinst); }
TypeTypeof new_TypeTypeof(const ref Loc loc, Expression exp) { return new TypeTypeof(loc, exp); }
TypeReturn new_TypeReturn(const ref Loc loc) { return new TypeReturn(loc); }
TypeStruct new_TypeStruct(StructDeclaration sym) { return new TypeStruct(sym); }
TypeEnum new_TypeEnum(EnumDeclaration sym) { return new TypeEnum(sym); }
TypeClass new_TypeClass(ClassDeclaration sym) { return new TypeClass(sym); }
TypeTuple new_TypeTuple(Parameters *arguments) { return new TypeTuple(arguments); }
TypeTuple new_TypeTuple(Expressions *exps) { return new TypeTuple(exps); }
TypeTuple new_TypeTuple() { return new TypeTuple(); }
TypeTuple new_TypeTuple(Type t1) { return new TypeTuple(t1); }
TypeTuple new_TypeTuple(Type t1, Type t2) { return new TypeTuple(t1, t2); }
TypeSlice new_TypeSlice(Type next, Expression lwr, Expression upr) { return new TypeSlice(next, lwr, upr); }
TypeNull new_TypeNull() { return new TypeNull(); }
Parameter new_Parameter(StorageClass storageClass, Type type, Identifier ident, Expression defaultArg, UserAttributeDeclaration userAttribDecl) { return new Parameter(storageClass, type, ident, defaultArg, userAttribDecl); }
TupleDeclaration new_TupleDeclaration(const ref Loc loc, Identifier ident, Objects *objects) { return new TupleDeclaration(loc, ident, objects); }
AliasDeclaration new_AliasDeclaration(const ref Loc loc, Identifier ident, Type type) { return new AliasDeclaration(loc, ident, type); }
AliasDeclaration new_AliasDeclaration(const ref Loc loc, Identifier ident, Dsymbol s) { return new AliasDeclaration(loc, ident, s); }
OverDeclaration new_OverDeclaration(Identifier ident, Dsymbol s, bool hasOverloads) { return new OverDeclaration(ident, s, hasOverloads); }
VarDeclaration new_VarDeclaration(const ref Loc loc, Type t, Identifier id, Initializer init) { return new VarDeclaration(loc, t, id, init); }
SymbolDeclaration new_SymbolDeclaration(const ref Loc loc, StructDeclaration dsym) { return new SymbolDeclaration(loc, dsym); }
TypeInfoDeclaration new_TypeInfoDeclaration(Type tinfo) { return new TypeInfoDeclaration(tinfo); }
TypeInfoStructDeclaration new_TypeInfoStructDeclaration(Type tinfo) { return new TypeInfoStructDeclaration(tinfo); }
TypeInfoClassDeclaration new_TypeInfoClassDeclaration(Type tinfo) { return new TypeInfoClassDeclaration(tinfo); }
TypeInfoInterfaceDeclaration new_TypeInfoInterfaceDeclaration(Type tinfo) { return new TypeInfoInterfaceDeclaration(tinfo); }
TypeInfoPointerDeclaration new_TypeInfoPointerDeclaration(Type tinfo) { return new TypeInfoPointerDeclaration(tinfo); }
TypeInfoArrayDeclaration new_TypeInfoArrayDeclaration(Type tinfo) { return new TypeInfoArrayDeclaration(tinfo); }
TypeInfoStaticArrayDeclaration new_TypeInfoStaticArrayDeclaration(Type tinfo) { return new TypeInfoStaticArrayDeclaration(tinfo); }
TypeInfoAssociativeArrayDeclaration new_TypeInfoAssociativeArrayDeclaration(Type tinfo) { return new TypeInfoAssociativeArrayDeclaration(tinfo); }
TypeInfoEnumDeclaration new_TypeInfoEnumDeclaration(Type tinfo) { return new TypeInfoEnumDeclaration(tinfo); }
TypeInfoFunctionDeclaration new_TypeInfoFunctionDeclaration(Type tinfo) { return new TypeInfoFunctionDeclaration(tinfo); }
TypeInfoDelegateDeclaration new_TypeInfoDelegateDeclaration(Type tinfo) { return new TypeInfoDelegateDeclaration(tinfo); }
TypeInfoTupleDeclaration new_TypeInfoTupleDeclaration(Type tinfo) { return new TypeInfoTupleDeclaration(tinfo); }
TypeInfoConstDeclaration new_TypeInfoConstDeclaration(Type tinfo) { return new TypeInfoConstDeclaration(tinfo); }
TypeInfoInvariantDeclaration new_TypeInfoInvariantDeclaration(Type tinfo) { return new TypeInfoInvariantDeclaration(tinfo); }
TypeInfoSharedDeclaration new_TypeInfoSharedDeclaration(Type tinfo) { return new TypeInfoSharedDeclaration(tinfo); }
TypeInfoWildDeclaration new_TypeInfoWildDeclaration(Type tinfo) { return new TypeInfoWildDeclaration(tinfo); }
TypeInfoVectorDeclaration new_TypeInfoVectorDeclaration(Type tinfo) { return new TypeInfoVectorDeclaration(tinfo); }
ThisDeclaration new_ThisDeclaration(const ref Loc loc, Type t) { return new ThisDeclaration(loc, t); }
FuncDeclaration new_FuncDeclaration(const ref Loc loc, const ref Loc endloc, Identifier id, StorageClass storage_class, Type type) { return new FuncDeclaration(loc, endloc, id, storage_class, type); }
FuncAliasDeclaration new_FuncAliasDeclaration(Identifier ident, FuncDeclaration funcalias, bool hasOverloads) { return new FuncAliasDeclaration(ident, funcalias, hasOverloads); }
FuncLiteralDeclaration new_FuncLiteralDeclaration(const ref Loc loc, const ref Loc endloc, Type type, TOK tok, ForeachStatement fes, Identifier id) { return new FuncLiteralDeclaration(loc, endloc, type, tok, fes, id); }
CtorDeclaration new_CtorDeclaration(const ref Loc loc, const ref Loc endloc, StorageClass stc, Type type) { return new CtorDeclaration(loc, endloc, stc, type); }
PostBlitDeclaration new_PostBlitDeclaration(const ref Loc loc, const ref Loc endloc, StorageClass stc, Identifier id) { return new PostBlitDeclaration(loc, endloc, stc, id); }
DtorDeclaration new_DtorDeclaration(const ref Loc loc, const ref Loc endloc) { return new DtorDeclaration(loc, endloc); }
DtorDeclaration new_DtorDeclaration(const ref Loc loc, const ref Loc endloc, StorageClass stc, Identifier id) { return new DtorDeclaration(loc, endloc, stc, id); }
StaticCtorDeclaration new_StaticCtorDeclaration(const ref Loc loc, const ref Loc endloc, StorageClass stc) { return new StaticCtorDeclaration(loc, endloc, stc); }
SharedStaticCtorDeclaration new_SharedStaticCtorDeclaration(const ref Loc loc, const ref Loc endloc, StorageClass stc) { return new SharedStaticCtorDeclaration(loc, endloc, stc); }
StaticDtorDeclaration new_StaticDtorDeclaration(const ref Loc loc, const ref Loc endloc, StorageClass stc) { return new StaticDtorDeclaration(loc, endloc, stc); }
SharedStaticDtorDeclaration new_SharedStaticDtorDeclaration(const ref Loc loc, const ref Loc endloc, StorageClass stc) { return new SharedStaticDtorDeclaration(loc, endloc, stc); }
UnitTestDeclaration new_UnitTestDeclaration(const ref Loc loc, const ref Loc endloc, StorageClass stc, char *codedoc) { return new UnitTestDeclaration(loc, endloc, stc, codedoc); }
DeleteDeclaration new_DeleteDeclaration(const ref Loc loc, const ref Loc endloc, StorageClass stc, Parameters *arguments) { return new DeleteDeclaration(loc, endloc, stc, arguments); }
StructDeclaration new_StructDeclaration(const ref Loc loc, Identifier id, bool inObject) { return new StructDeclaration(loc, id, inObject); }
UnionDeclaration new_UnionDeclaration(const ref Loc loc, Identifier id) { return new UnionDeclaration(loc, id); }
InterfaceDeclaration new_InterfaceDeclaration(const ref Loc loc, Identifier id, BaseClasses *baseclasses) { return new InterfaceDeclaration(loc, id, baseclasses); }
// DVCondition new_DVCondition(Module mod, uint level, Identifier ident) { return new DVCondition(mod, level, ident); }
DebugCondition new_DebugCondition(Module mod, uint level, Identifier ident) { return new DebugCondition(mod, level, ident); }
VersionCondition new_VersionCondition(Module mod, uint level, Identifier ident) { return new VersionCondition(mod, level, ident); }
StaticIfCondition new_StaticIfCondition(const ref Loc loc, Expression exp) { return new StaticIfCondition(loc, exp); }
EnumDeclaration new_EnumDeclaration(const ref Loc loc, Identifier id, Type memtype) { return new EnumDeclaration(loc, id, memtype); }
EnumMember new_EnumMember(const ref Loc loc, Identifier id, Expression value, Type origType) { return new EnumMember(loc, id, value, origType); }
Import new_Import(const ref Loc loc, Identifiers *packages, Identifier id, Identifier aliasId, int isstatic) { return new Import(loc, packages, id, aliasId, isstatic); }
VoidInitializer new_VoidInitializer(const ref Loc loc) { return new VoidInitializer(loc); }
ErrorInitializer new_ErrorInitializer() { return new ErrorInitializer(); }
StructInitializer new_StructInitializer(const ref Loc loc) { return new StructInitializer(loc); }
ArrayInitializer new_ArrayInitializer(const ref Loc loc) { return new ArrayInitializer(loc); }
ExpInitializer new_ExpInitializer(const ref Loc loc, Expression exp) { return new ExpInitializer(loc, exp); }
Package new_Package(const ref Loc loc, Identifier ident) { return new Package(loc, ident); }
ErrorStatement new_ErrorStatement() { return new ErrorStatement(); }
PeelStatement new_PeelStatement(Statement s) { return new PeelStatement(s); }
ExpStatement new_ExpStatement(const ref Loc loc, Expression exp) { return new ExpStatement(loc, exp); }
ExpStatement new_ExpStatement(const ref Loc loc, Dsymbol s) { return new ExpStatement(loc, s); }
DtorExpStatement new_DtorExpStatement(const ref Loc loc, Expression exp, VarDeclaration v) { return new DtorExpStatement(loc, exp, v); }
CompileStatement new_CompileStatement(const ref Loc loc, Expression exp) { return new CompileStatement(loc, exp); }
CompoundStatement new_CompoundStatement(const ref Loc loc, Statements *s) { return new CompoundStatement(loc, s); }
CompoundStatement new_CompoundStatement(const ref Loc loc, Statement s1) { return new CompoundStatement(loc, s1); }
CompoundStatement new_CompoundStatement(const ref Loc loc, Statement s1, Statement s2) { return new CompoundStatement(loc, s1, s2); }
CompoundDeclarationStatement new_CompoundDeclarationStatement(const ref Loc loc, Statements *s) { return new CompoundDeclarationStatement(loc, s); }
UnrolledLoopStatement new_UnrolledLoopStatement(const ref Loc loc, Statements *statements) { return new UnrolledLoopStatement(loc, statements); }
WhileStatement new_WhileStatement(const ref Loc loc, Expression c, Statement b, const ref Loc endloc) { return new WhileStatement(loc, c, b, endloc); }
ForStatement new_ForStatement(const ref Loc loc, Statement init, Expression condition, Expression increment, Statement _body, const ref Loc endloc) { return new ForStatement(loc, init, condition, increment, _body, endloc); }
ForeachStatement new_ForeachStatement(const ref Loc loc, TOK op, Parameters *parameters, Expression aggr, Statement _body, const ref Loc endloc) { return new ForeachStatement(loc, op, parameters, aggr, _body, endloc); }
ForeachRangeStatement new_ForeachRangeStatement(const ref Loc loc, TOK op, Parameter prm, Expression lwr, Expression upr, Statement _body, const ref Loc endloc) { return new ForeachRangeStatement(loc, op, prm, lwr, upr, _body, endloc); }
ConditionalStatement new_ConditionalStatement(const ref Loc loc, Condition condition, Statement ifbody, Statement elsebody) { return new ConditionalStatement(loc, condition, ifbody, elsebody); }
PragmaStatement new_PragmaStatement(const ref Loc loc, Identifier ident, Expressions *args, Statement _body) { return new PragmaStatement(loc, ident, args, _body); }
StaticAssertStatement new_StaticAssertStatement(StaticAssert sa) { return new StaticAssertStatement(sa); }
SwitchStatement new_SwitchStatement(const ref Loc loc, Expression c, Statement b, bool isFinal) { return new SwitchStatement(loc, c, b, isFinal); }
CaseStatement new_CaseStatement(const ref Loc loc, Expression exp, Statement s) { return new CaseStatement(loc, exp, s); }
CaseRangeStatement new_CaseRangeStatement(const ref Loc loc, Expression first, Expression last, Statement s) { return new CaseRangeStatement(loc, first, last, s); }
DefaultStatement new_DefaultStatement(const ref Loc loc, Statement s) { return new DefaultStatement(loc, s); }
GotoDefaultStatement new_GotoDefaultStatement(const ref Loc loc) { return new GotoDefaultStatement(loc); }
GotoCaseStatement new_GotoCaseStatement(const ref Loc loc, Expression exp) { return new GotoCaseStatement(loc, exp); }
SwitchErrorStatement new_SwitchErrorStatement(const ref Loc loc) { return new SwitchErrorStatement(loc); }
ReturnStatement new_ReturnStatement(const ref Loc loc, Expression exp) { return new ReturnStatement(loc, exp); }
BreakStatement new_BreakStatement(const ref Loc loc, Identifier ident) { return new BreakStatement(loc, ident); }
ContinueStatement new_ContinueStatement(const ref Loc loc, Identifier ident) { return new ContinueStatement(loc, ident); }
SynchronizedStatement new_SynchronizedStatement(const ref Loc loc, Expression exp, Statement _body) { return new SynchronizedStatement(loc, exp, _body); }
TryCatchStatement new_TryCatchStatement(const ref Loc loc, Statement _body, Catches *catches) { return new TryCatchStatement(loc, _body, catches); }
Catch new_Catch(const ref Loc loc, Type t, Identifier id, Statement handler) { return new Catch(loc, t, id, handler); }
TryFinallyStatement new_TryFinallyStatement(const ref Loc loc, Statement _body, Statement finalbody) { return new TryFinallyStatement(loc, _body, finalbody); }
ThrowStatement new_ThrowStatement(const ref Loc loc, Expression exp) { return new ThrowStatement(loc, exp); }
DebugStatement new_DebugStatement(const ref Loc loc, Statement statement) { return new DebugStatement(loc, statement); }
GotoStatement new_GotoStatement(const ref Loc loc, Identifier ident) { return new GotoStatement(loc, ident); }
LabelStatement new_LabelStatement(const ref Loc loc, Identifier ident, Statement statement) { return new LabelStatement(loc, ident, statement); }
LabelDsymbol new_LabelDsymbol(Identifier ident) { return new LabelDsymbol(ident); }
AsmStatement new_AsmStatement(const ref Loc loc, Token *tokens) { return new AsmStatement(loc, tokens); }
CompoundAsmStatement new_CompoundAsmStatement(const ref Loc loc, Statements *s, StorageClass stc) { return new CompoundAsmStatement(loc, s, stc); }
ImportStatement new_ImportStatement(const ref Loc loc, Dsymbols *imports) { return new ImportStatement(loc, imports); }
StaticAssert new_StaticAssert(const ref Loc loc, Expression exp, Expression msg) { return new StaticAssert(loc, exp, msg); }
TemplateDeclaration new_TemplateDeclaration(const ref Loc loc, Identifier id, TemplateParameters *parameters, Expression constraint, Dsymbols *decldefs, bool ismixin, bool literal) { return new TemplateDeclaration(loc, id, parameters, constraint, decldefs, ismixin, literal); }
TemplateTypeParameter new_TemplateTypeParameter(const ref Loc loc, Identifier ident, Type specType, Type defaultType) { return new TemplateTypeParameter(loc, ident, specType, defaultType); }
TemplateThisParameter new_TemplateThisParameter(const ref Loc loc, Identifier ident, Type specType, Type defaultType) { return new TemplateThisParameter(loc, ident, specType, defaultType); }
TemplateValueParameter new_TemplateValueParameter(const ref Loc loc, Identifier ident, Type valType, Expression specValue, Expression defaultValue) { return new TemplateValueParameter(loc, ident, valType, specValue, defaultValue); }
TemplateAliasParameter new_TemplateAliasParameter(const ref Loc loc, Identifier ident, Type specType, RootObject specAlias, RootObject defaultAlias) { return new TemplateAliasParameter(loc, ident, specType, specAlias, defaultAlias); }
TemplateTupleParameter new_TemplateTupleParameter(const ref Loc loc, Identifier ident) { return new TemplateTupleParameter(loc, ident); }
TemplateInstance new_TemplateInstance(const ref Loc loc, Identifier ident, Objects *tiargs) { return new TemplateInstance(loc, ident, tiargs); }
TemplateInstance new_TemplateInstance(const ref Loc loc, TemplateDeclaration td, Objects *tiargs) { return new TemplateInstance(loc, td, tiargs); }
TemplateMixin new_TemplateMixin(const ref Loc loc, Identifier ident, TypeQualified tqual, Objects *tiargs) { return new TemplateMixin(loc, ident, tqual, tiargs); }
LinkDeclaration new_LinkDeclaration(LINK p, Dsymbols *decl) { return new LinkDeclaration(p, decl); }

LangPlugins* new_LangPlugins() { return new LangPlugins; }

import core.memory : GC;

void delete_Object(RootObject o) { GC.free(cast(void*)o); } // extern(C++) classes allocated by D cannot be destroy()'d, since rt_finalize expects a D vtbl with a ClassInfo* pointer as first entry
void delete_File(File* o) { destroy(o); }

private extern(D) immutable offset = (void*).sizeof; // construct_ functions are called inside cpp:: classes ctors to call the base D ctor, but the derived vptr is already set so skip it

void construct_TypeBasic(TypeBasic _this, TY ty)
{
    auto postvtblThis = &(cast(byte*)_this)[offset];
    memcpy(postvtblThis, &TypeBasic.classinfo.m_init[offset], TypeBasic.classinfo.m_init.length - offset);
    _this.__ctor(ty);
}
void construct_TypePointer(TypePointer _this, Type t)
{
    auto postvtblThis = &(cast(byte*)_this)[offset];
    memcpy(postvtblThis, &TypePointer.classinfo.m_init[offset], TypePointer.classinfo.m_init.length - offset);
    _this.__ctor(t);
}
void construct_TypeReference(TypeReference _this, Type t)
{
    auto postvtblThis = &(cast(byte*)_this)[offset];
    memcpy(postvtblThis, &TypeReference.classinfo.m_init[offset], TypeReference.classinfo.m_init.length - offset);
    _this.__ctor(t);
}
void construct_VarDeclaration(VarDeclaration _this, const ref Loc loc, Type t, Identifier id, Initializer init)
{
    auto postvtblThis = &(cast(byte*)_this)[offset];
    memcpy(postvtblThis, &VarDeclaration.classinfo.m_init[offset], VarDeclaration.classinfo.m_init.length - offset);
    _this.__ctor(loc, t, id, init);
}
void construct_FuncDeclaration(FuncDeclaration _this, const ref Loc loc, const ref Loc endloc, Identifier id, StorageClass storage_class, Type type)
{
    auto postvtblThis = &(cast(byte*)_this)[offset];
    memcpy(postvtblThis, &FuncDeclaration.classinfo.m_init[offset], FuncDeclaration.classinfo.m_init.length - offset);
    _this.__ctor(loc, endloc, id, storage_class, type);
}
void construct_CtorDeclaration(CtorDeclaration _this, const ref Loc loc, const ref Loc endloc, StorageClass stc, Type type)
{
    auto postvtblThis = &(cast(byte*)_this)[offset];
    memcpy(postvtblThis, &CtorDeclaration.classinfo.m_init[offset], CtorDeclaration.classinfo.m_init.length - offset);
    _this.__ctor(loc, endloc, stc, type);
}
void construct_DtorDeclaration(DtorDeclaration _this, const ref Loc loc, const ref Loc endloc, StorageClass stc, Identifier id)
{
    auto postvtblThis = &(cast(byte*)_this)[offset];
    memcpy(postvtblThis, &DtorDeclaration.classinfo.m_init[offset], DtorDeclaration.classinfo.m_init.length - offset);
    _this.__ctor(loc, endloc, stc, id);
}
void construct_EnumDeclaration(EnumDeclaration _this, const ref Loc loc, Identifier id, Type memtype)
{
    auto postvtblThis = &(cast(byte*)_this)[offset];
    memcpy(postvtblThis, &EnumDeclaration.classinfo.m_init[offset], EnumDeclaration.classinfo.m_init.length - offset);
    _this.__ctor(loc, id, memtype);
}
void construct_EnumMember(EnumMember _this, const ref Loc loc, Identifier id, Expression value, Type origType)
{
    auto postvtblThis = &(cast(byte*)_this)[offset];
    memcpy(postvtblThis, &EnumMember.classinfo.m_init[offset], EnumMember.classinfo.m_init.length - offset);
    _this.__ctor(loc, id, value, origType);
}
void construct_AliasDeclaration(AliasDeclaration _this, const ref Loc loc, Identifier ident, Type type)
{
    auto postvtblThis = &(cast(byte*)_this)[offset];
    memcpy(postvtblThis, &AliasDeclaration.classinfo.m_init[offset], AliasDeclaration.classinfo.m_init.length - offset);
    _this.__ctor(loc, ident, type);
}
void construct_Import(Import _this, const ref Loc loc, Identifiers *packages, Identifier id, Identifier aliasId, int isstatic)
{
    auto postvtblThis = &(cast(byte*)_this)[offset];
    memcpy(postvtblThis, &Import.classinfo.m_init[offset], Import.classinfo.m_init.length - offset);
    _this.__ctor(loc, packages, id, aliasId, isstatic);
}
void construct_StructDeclaration(StructDeclaration _this, const ref Loc loc, Identifier id, bool inObject)
{
    auto postvtblThis = &(cast(byte*)_this)[offset];
    memcpy(postvtblThis, &StructDeclaration.classinfo.m_init[offset], StructDeclaration.classinfo.m_init.length - offset);
    _this.__ctor(loc, id, inObject);
}
void construct_UnionDeclaration(UnionDeclaration _this, const ref Loc loc, Identifier id)
{
    auto postvtblThis = &(cast(byte*)_this)[offset];
    memcpy(postvtblThis, &UnionDeclaration.classinfo.m_init[offset], UnionDeclaration.classinfo.m_init.length - offset);
    _this.__ctor(loc, id);
}
void construct_ClassDeclaration(ClassDeclaration _this, const ref Loc loc, Identifier id, BaseClasses *baseclasses, Dsymbols* members, bool inObject)
{
    auto postvtblThis = &(cast(byte*)_this)[offset];
    memcpy(postvtblThis, &ClassDeclaration.classinfo.m_init[offset], ClassDeclaration.classinfo.m_init.length - offset);
    _this.__ctor(loc, id, baseclasses, members, inObject);
}
void construct_AnonDeclaration(AnonDeclaration _this, const ref Loc loc, bool isunion, Dsymbols* decl)
{
    auto postvtblThis = &(cast(byte*)_this)[offset];
    memcpy(postvtblThis, &AnonDeclaration.classinfo.m_init[offset], AnonDeclaration.classinfo.m_init.length - offset);
    _this.__ctor(loc, isunion, decl);
}
void construct_Catch(Catch _this, const ref Loc loc, Type t, Identifier id, Statement handler)
{
    auto postvtblThis = &(cast(byte*)_this)[offset];
    memcpy(postvtblThis, &Catch.classinfo.m_init[offset], Catch.classinfo.m_init.length - offset);
    _this.__ctor(loc, t, id, handler);
}
void construct_Module(Module _this, const ref Loc loc, const(char) *filename, Identifier ident, int doDocComment, int doHdrGen)
{
    auto postvtblThis = &(cast(byte*)_this)[offset];
    memcpy(postvtblThis, &Module.classinfo.m_init[offset], Module.classinfo.m_init.length - offset);
    _this.__ctor(loc, filename.toDString, ident, doDocComment, doHdrGen);
}
void construct_Package(Package _this, const ref Loc loc, Identifier ident)
{
    auto postvtblThis = &(cast(byte*)_this)[offset];
    memcpy(postvtblThis, &Package.classinfo.m_init[offset], Package.classinfo.m_init.length - offset);
    _this.__ctor(loc, ident);
}
void construct_TemplateDeclaration(TemplateDeclaration _this, const ref Loc loc, Identifier id, TemplateParameters *parameters, Expression constraint, Dsymbols *decldefs, bool ismixin = false, bool literal = false)
{
    auto postvtblThis = &(cast(byte*)_this)[offset];
    memcpy(postvtblThis, &TemplateDeclaration.classinfo.m_init[offset], TemplateDeclaration.classinfo.m_init.length - offset);
    _this.__ctor(loc, id, parameters, constraint, decldefs, ismixin, literal);
}
void construct_TemplateInstance(TemplateInstance _this, const ref Loc loc, Identifier ident, Objects *tiargs)
{
    auto postvtblThis = &(cast(byte*)_this)[offset];
    memcpy(postvtblThis, &TemplateInstance.classinfo.m_init[offset], TemplateInstance.classinfo.m_init.length - offset);
    _this.__ctor(loc, ident, tiargs);
}
void construct_TemplateInstance(TemplateInstance _this, const ref Loc loc, TemplateDeclaration td, Objects *tiargs)
{
    auto postvtblThis = &(cast(byte*)_this)[offset];
    memcpy(postvtblThis, &TemplateInstance.classinfo.m_init[offset], TemplateInstance.classinfo.m_init.length - offset);
    _this.__ctor(loc, td, tiargs);
}
