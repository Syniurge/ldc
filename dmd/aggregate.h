
/* Compiler implementation of the D programming language
 * Copyright (C) 1999-2018 by The D Language Foundation, All Rights Reserved
 * written by Walter Bright
 * http://www.digitalmars.com
 * Distributed under the Boost Software License, Version 1.0.
 * http://www.boost.org/LICENSE_1_0.txt
 * https://github.com/dlang/dmd/blob/master/src/dmd/aggregate.h
 */

#pragma once

#include "dsymbol.h"
#include "objc.h"

class Identifier;
class Type;
class TypeFunction;
class Expression;
class FuncDeclaration;
class CtorDeclaration;
class DtorDeclaration;
class NewDeclaration;
class DeleteDeclaration;
class InterfaceDeclaration;
class TypeInfoClassDeclaration;
class VarDeclaration;

enum Sizeok
{
    SIZEOKnone,         // size of aggregate is not yet able to compute
    SIZEOKfwd,          // size of aggregate is ready to compute
    SIZEOKdone          // size of aggregate is set correctly
};

enum Baseok
{
    BASEOKnone,         // base classes not computed yet
    BASEOKin,           // in process of resolving base classes
    BASEOKdone,         // all base classes are resolved
    BASEOKsemanticdone  // all base classes semantic done
};

enum StructPOD
{
    ISPODno,            // struct is not POD
    ISPODyes,           // struct is POD
    ISPODfwd            // POD not yet computed
};

enum Abstract
{
    ABSfwdref = 0,      // whether an abstract class is not yet computed
    ABSyes,             // is abstract class
    ABSno               // is not abstract class
};

FuncDeclaration *search_toString(StructDeclaration *sd);

struct ClassKind
{
    enum Type
    {
        /// the aggregate is a d(efault) struct/class/interface
        d,
        /// the aggregate is a C++ struct/class/interface
        ck_cpp, // CALYPSO renamed from "cpp" to preserve Calypso's cpp namespace
        /// the aggregate is an Objective-C class/interface
        objc
    };
};

class AggregateDeclaration : public ScopeDsymbol
{
public:
    Type *type;
    StorageClass storage_class;
    Prot protection;
    unsigned structsize;        // size of struct
    unsigned alignsize;         // size of struct for alignment purposes
    structalign_t alignment;    // alignment applied outside of the struct/class value // CALYPSO
    VarDeclarations fields;     // VarDeclaration fields
    Sizeok sizeok;              // set when structsize contains valid data
    Dsymbol *deferred;          // any deferred semantic2() or semantic3() symbol
    bool isdeprecated;          // true if deprecated

    ClassKind::Type classKind;  // specifies the linkage type

    /* !=NULL if is nested
     * pointing to the dsymbol that directly enclosing it.
     * 1. The function that enclosing it (nested struct and class)
     * 2. The class that enclosing it (nested class only)
     * 3. If enclosing aggregate is template, its enclosing dsymbol.
     * See AggregateDeclaraton::makeNested for the details.
     */
    Dsymbol *enclosing;
    VarDeclaration *vthis;      // 'this' parameter if this aggregate is nested
    // Special member functions
    FuncDeclarations invs;              // Array of invariants
    FuncDeclaration *inv;               // invariant
    NewDeclaration *aggNew;             // allocator
    DeleteDeclaration *aggDelete;       // deallocator

    Dsymbol *ctor;                      // CtorDeclaration or TemplateDeclaration

    // default constructor - should have no arguments, because
    // it would be stored in TypeInfo_Class.defaultConstructor
    CtorDeclaration *defaultCtor;

    Dsymbol *aliasthis;         // forward unresolved lookups to aliasthis
    bool noDefaultCtor;         // no default construction

    DtorDeclarations dtors;     // Array of destructors
    DtorDeclaration *dtor;      // aggregate destructor
    DtorDeclaration *primaryDtor; // non-deleting C++ destructor, same as dtor for D
    DtorDeclaration *tidtor;    // aggregate destructor used in TypeInfo (must have extern(D) ABI)
    FuncDeclaration *fieldDtor;   // aggregate destructor for just the fields

    Expression *getRTInfo;      // pointer to GC info generated by object.RTInfo(this)

    virtual void _key(); // CALYPSO force the C++ compiler to emit the vtable
    virtual Scope *newScope(Scope *sc);
    void setScope(Scope *sc);
    virtual bool determineFields(); // CALYPSO
    bool determineSize(Loc loc);
    virtual void finalizeSize() = 0;
    virtual d_uns64 size(const Loc &loc); // CALYPSO
    bool fit(const Loc &loc, Scope *sc, Expressions *elements, Type *stype); // CALYPSO
    bool fill(Loc loc, Expressions *elements, bool ctorinit);
    Type *getType();
    bool isDeprecated();         // is aggregate deprecated?
    bool isNested();
    virtual void makeNested();  // CALYPSO
    bool isExport() const;
    Dsymbol *searchCtor();

    bool isBaseOf2(ClassDeclaration *cd); // CALYPSO
    #define OFFSET_RUNTIME 0x76543210
    virtual bool isBaseOf(ClassDeclaration *cd, int *poffset); // CALYPSO

    Prot prot();

    // 'this' type
    Type *handleType() { return type; }

    // Back end
    Symbol *stag;               // tag symbol for debug data
    Symbol *sinit;

    // CALYPSO
    virtual bool byRef() const { return false; }
    virtual bool mayBeAnonymous() { return false; }
    virtual Expression *defaultInit(Loc loc);
    Expression *defaultInitLiteral(Loc loc);
    size_t literalElemDim(); // returns the total number of fields of an aggregate literal (TODO: better name?)
    CtorDeclaration* hasImplicitCtor(Expression* farg);
    virtual CtorDeclaration* hasCopyCtor(Scope* sc) { return nullptr; }
    virtual Expression* buildVarInitializer(Scope* sc, VarDeclaration* vd, Expression* exp) { return NULL; }

    AggregateDeclaration *isAggregateDeclaration() { return this; }
    void accept(Visitor *v) { v->visit(this); }
};

struct StructFlags
{
    enum Type
    {
        none = 0x0,
        hasPointers = 0x1  // NB: should use noPointers as in ClassFlags
    };
};

class StructDeclaration : public AggregateDeclaration
{
public:
    bool zeroInit;              // !=0 if initialize with 0 fill
    bool hasIdentityAssign;     // true if has identity opAssign
    bool hasIdentityEquals;     // true if has identity opEquals
    bool hasNoFields;           // has no fields
    FuncDeclarations postblits; // Array of postblit functions
    FuncDeclaration *postblit;  // aggregate postblit

    FuncDeclaration *xeq;       // TypeInfo_Struct.xopEquals
    FuncDeclaration *xcmp;      // TypeInfo_Struct.xopCmp
    FuncDeclaration *xhash;     // TypeInfo_Struct.xtoHash
    static FuncDeclaration *xerreq;      // object.xopEquals
    static FuncDeclaration *xerrcmp;     // object.xopCmp

    StructPOD ispod;            // if struct is POD

    // For 64 bit Efl function call/return ABI
    Type *arg1type;
    Type *arg2type;

    // Even if struct is defined as non-root symbol, some built-in operations
    // (e.g. TypeidExp, NewExp, ArrayLiteralExp, etc) request its TypeInfo.
    // For those, today TypeInfo_Struct is generated in COMDAT.
    bool requestTypeInfo;

    virtual void _key(); // CALYPSO
    static StructDeclaration *create(Loc loc, Identifier *id, bool inObject);
    Dsymbol *syntaxCopy(Dsymbol *s);
    void semanticTypeInfoMembers();
    Dsymbol *search(const Loc &loc, Identifier *ident, int flags = SearchLocalsOnly);
    const char *kind() const;
    virtual bool buildLayout(); // CALYPSO
    void finalizeSize();
    bool isPOD();

    // CALYPSO
    virtual bool disableDefaultCtor() { return true; }
    Expression *defaultInit(Loc loc);

    StructDeclaration *isStructDeclaration() { return this; }
    void accept(Visitor *v) { v->visit(this); }
};

class UnionDeclaration : public StructDeclaration
{
public:
    virtual void _key(); // CALYPSO
    Dsymbol *syntaxCopy(Dsymbol *s);
    const char *kind() const;

    UnionDeclaration *isUnionDeclaration() { return this; }
    void accept(Visitor *v) { v->visit(this); }
};

struct BaseClass
{
    Type *type = nullptr;                         // (before semantic processing)

    AggregateDeclaration *sym = nullptr; // CALYPSO
    unsigned offset = 0;                    // 'this' pointer offset
    // for interfaces: Array of FuncDeclaration's
    // making up the vtbl[]
    FuncDeclarations vtbl;

    DArray<BaseClass> baseInterfaces;   // if BaseClass is an interface, these
                                        // are a copy of the InterfaceDeclaration::interfaces

    BaseClass();
    BaseClass(Type *type) { this->type = type; } // CALYPSO

    bool fillVtbl(ClassDeclaration *cd, FuncDeclarations *vtbl, int newinstance);
    void copyBaseInterfaces(BaseClasses *);
};

struct ClassFlags
{
    enum Type
    {
        none = 0x0,
        isCOMclass = 0x1,
        noPointers = 0x2,
        hasOffTi = 0x4,
        hasCtor = 0x8,
        hasGetMembers = 0x10,
        hasTypeInfo = 0x20,
        isAbstract = 0x40,
        isCPPclass = 0x80,
        hasDtor = 0x100,
        byVal = 0x200  // CALYPSO
    };
};

class ClassDeclaration : public AggregateDeclaration
{
public:
    static ClassDeclaration *object;
    static ClassDeclaration *throwable;
    static ClassDeclaration *exception;
    static ClassDeclaration *errorException;
    static ClassDeclaration *cpp_type_info_ptr;

    AggregateDeclaration *baseClass;        // NULL only if this is Object // CALYPSO
    FuncDeclaration *staticCtor;
    FuncDeclaration *staticDtor;
    Dsymbols vtbl;                      // Array of FuncDeclaration's making up the vtbl[]
    Dsymbols vtblFinal;                 // More FuncDeclaration's that aren't in vtbl[]

    BaseClasses *baseclasses;           // Array of BaseClass's; first is super,
                                        // rest are Interface's

    DArray<BaseClass*> interfaces;      // interfaces[interfaces_dim] for this class
                                        // (does not include baseClass)

    BaseClasses *vtblInterfaces;        // array of base interfaces that have
                                        // their own vtbl[]

    TypeInfoClassDeclaration *vclassinfo;       // the ClassInfo object for this ClassDeclaration
    bool com;                           // true if this is a COM class (meaning it derives from IUnknown)
    bool stack;                         // true if this is a scope class
    int cppDtorVtblIndex;               // slot reserved for the virtual destructor [extern(C++)]
    bool inuse;                         // to prevent recursive attempts
    bool isActuallyAnonymous;           // true if this class has an identifier, but was originally declared anonymous
                                        // used in support of https://issues.dlang.org/show_bug.cgi?id=17371

    Abstract isabstract;                // 0: fwdref, 1: is abstract class, 2: not abstract
    Baseok baseok;                      // set the progress of base classes resolving
    ObjcClassDeclaration objc;          // Data for a class declaration that is needed for the Objective-C integration
    Symbol *cpp_type_info_ptr_sym;      // cached instance of class Id.cpp_type_info_ptr

    virtual void _key(); // CALYPSO
    static ClassDeclaration *create(Loc loc, Identifier *id, BaseClasses *baseclasses, Dsymbols *members, bool inObject);
    Dsymbol *syntaxCopy(Dsymbol *s);
    Scope *newScope(Scope *sc);

    #define OFFSET_RUNTIME 0x76543210
    #define OFFSET_FWDREF 0x76543211
    bool isAnonymous();

    bool isBaseInfoComplete();
    Dsymbol *search(const Loc &loc, Identifier *ident, int flags = SearchLocalsOnly);
    ClassDeclaration *searchBase(Identifier *ident);
    virtual bool buildLayout(); // CALYPSO
    void finalizeSize();
    bool isFuncHidden(FuncDeclaration *fd);
    FuncDeclaration *findFunc(Identifier *ident, TypeFunction *tf);
    bool isCOMclass() const;
    virtual bool isCOMinterface() const;
    bool isCPPclass() const;
    virtual bool isCPPinterface() const;
    bool isAbstract();
    virtual int vtblOffset() const;
    const char *kind() const;

    void addLocalClass(ClassDeclarations *);

    // CALYPSO
    virtual bool byRef() const { return true; }
    virtual bool allowMultipleInheritance(); // will allow more than one non-interface base
    virtual bool allowInheritFromStruct();  // even though C++ class types are value, we may want to keep mapping POD classes to D structs to keep init lists
    virtual bool needsInterfaceSemantic() const;
    virtual Expression *defaultInit(Loc loc);
    virtual void initVtbl();
    virtual void finalizeVtbl();
    AggregateDeclaration *foreignBase();

    // Back end
    Dsymbol *vtblsym;
    Dsymbol *vtblSymbol();

    ClassDeclaration *isClassDeclaration() { return (ClassDeclaration *)this; }
    void accept(Visitor *v) { v->visit(this); }
};

class InterfaceDeclaration : public ClassDeclaration
{
public:
    Dsymbol *syntaxCopy(Dsymbol *s);
    Scope *newScope(Scope *sc);
    bool isBaseOf(ClassDeclaration *cd, int *poffset);
    bool isBaseOf(BaseClass *bc, int *poffset);
    const char *kind() const;
    int vtblOffset() const;
    bool isCPPinterface() const;
    bool isCOMinterface() const;

    InterfaceDeclaration *isInterfaceDeclaration() { return this; }
    void accept(Visitor *v) { v->visit(this); }
};

// CALYPSO
StructDeclaration *isStructDeclarationOrNull(Dsymbol* s);
ClassDeclaration *isClassDeclarationOrNull(Dsymbol *s);
AggregateDeclaration *toAggregateBase(Dsymbol *s);
void markAggregateReferenced(AggregateDeclaration* ad);