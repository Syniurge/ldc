/**
 * Compiler implementation of the
 * $(LINK2 http://www.dlang.org, D programming language).
 *
 * Copyright:   Copyright (c) 1999-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     $(LINK2 http://www.boost.org/LICENSE_1_0.txt, Boost License 1.0)
 * Source:      $(LINK2 https://github.com/dlang/dmd/blob/master/src/ddmd/aggregate.d, _aggregate.d)
 */

module ddmd.aggregate;

// Online documentation: https://dlang.org/phobos/ddmd_aggregate.html

import core.stdc.stdio;
import core.checkedint;

import ddmd.arraytypes;
import ddmd.gluelayer;
import ddmd.dclass;
import ddmd.declaration;
import ddmd.dscope;
import ddmd.dstruct;
import ddmd.dsymbol;
import ddmd.dtemplate;
import ddmd.errors;
import ddmd.expression;
import ddmd.func;
import ddmd.globals;
import ddmd.id;
import ddmd.identifier;
import ddmd.mtype;
import ddmd.target;
import ddmd.tokens;
import ddmd.semantic;
import ddmd.visitor;

enum Sizeok : int
{
    SIZEOKnone,             // size of aggregate is not yet able to compute
    SIZEOKfwd,              // size of aggregate is ready to compute
    SIZEOKdone,             // size of aggregate is set correctly
}

alias SIZEOKnone = Sizeok.SIZEOKnone;
alias SIZEOKdone = Sizeok.SIZEOKdone;
alias SIZEOKfwd = Sizeok.SIZEOKfwd;

enum Baseok : int
{
    BASEOKnone,             // base classes not computed yet
    BASEOKin,               // in process of resolving base classes
    BASEOKdone,             // all base classes are resolved
    BASEOKsemanticdone,     // all base classes semantic done
}

alias BASEOKnone = Baseok.BASEOKnone;
alias BASEOKin = Baseok.BASEOKin;
alias BASEOKdone = Baseok.BASEOKdone;
alias BASEOKsemanticdone = Baseok.BASEOKsemanticdone;

/***********************************************************
 */
extern (C++) abstract class AggregateDeclaration : ScopeDsymbol
{
    Type type;
    StorageClass storage_class;
    Prot protection;
    uint structsize;        // size of struct
    uint alignsize;         // size of struct for alignment purposes
    structalign_t alignment;    // alignment applied outside of the struct/class value // CALYPSO
    VarDeclarations fields; // VarDeclaration fields
    Sizeok sizeok;          // set when structsize contains valid data
    Dsymbol deferred;       // any deferred semantic2() or semantic3() symbol
    bool isdeprecated;      // true if deprecated

    /* !=null if is nested
     * pointing to the dsymbol that directly enclosing it.
     * 1. The function that enclosing it (nested struct and class)
     * 2. The class that enclosing it (nested class only)
     * 3. If enclosing aggregate is template, its enclosing dsymbol.
     * See AggregateDeclaraton::makeNested for the details.
     */
    Dsymbol enclosing;

    VarDeclaration vthis;   // 'this' parameter if this aggregate is nested

    // Special member functions
    FuncDeclarations invs;          // Array of invariants
    FuncDeclaration inv;            // invariant
    NewDeclaration aggNew;          // allocator
    DeleteDeclaration aggDelete;    // deallocator

    // CtorDeclaration or TemplateDeclaration
    Dsymbol ctor;

    // default constructor - should have no arguments, because
    // it would be stored in TypeInfo_Class.defaultConstructor
    CtorDeclaration defaultCtor;

    Dsymbol aliasthis;      // forward unresolved lookups to aliasthis
    bool noDefaultCtor;     // no default construction

    FuncDeclarations dtors; // Array of destructors
    FuncDeclaration dtor;   // aggregate destructor

    Expression getRTInfo;   // pointer to GC info generated by object.RTInfo(this)

    final extern (D) this(Loc loc, Identifier id)
    {
        super(id);
        this.loc = loc;
        protection = Prot(PROTpublic);
        sizeok = SIZEOKnone; // size not determined yet
    }

    /***************************************
     * Create a new scope from sc.
     * semantic, semantic2 and semantic3 will use this for aggregate members.
     */
    Scope* newScope(Scope* sc)
    {
        auto sc2 = sc.push(this);
        sc2.stc &= STCsafe | STCtrusted | STCsystem;
        sc2.parent = this;
        if (isUnionDeclaration())
            sc2.inunion = 1;
        sc2.protection = Prot(PROTpublic);
        sc2.explicitProtection = 0;
        sc2.aligndecl = null;
        sc2.userAttribDecl = null;
        return sc2;
    }

    override final void setScope(Scope* sc)
    {
        // Might need a scope to resolve forward references. The check for
        // semanticRun prevents unnecessary setting of _scope during deferred
        // setScope phases for aggregates which already finished semantic().
        // See https://issues.dlang.org/show_bug.cgi?id=16607
        if (semanticRun < PASSsemanticdone)
            ScopeDsymbol.setScope(sc);
    }

    /***************************************
     * Find all instance fields, then push them into `fields`.
     *
     * Runs semantic() for all instance field variables, but also
     * the field types can reamin yet not resolved forward references,
     * except direct recursive definitions.
     * After the process sizeok is set to SIZEOKfwd.
     *
     * Returns:
     *      false if any errors occur.
     */
    bool determineFields() // CALYPSO (made non-final)
    {
        if (sizeok != SIZEOKnone)
            return true;

        //printf("determineFields() %s, fields.dim = %d\n", toChars(), fields.dim);
        // determineFields can be called recursively from one of the fields's v.semantic
        fields.setDim(0);

        extern (C++) static int func(Dsymbol s, void* param)
        {
            auto v = s.isVarDeclaration();
            if (!v)
                return 0;
            if (v.storage_class & STCmanifest)
                return 0;

            auto ad = cast(AggregateDeclaration)param;

            if (v.semanticRun < PASSsemanticdone)
                v.semantic(null);
            // Return in case a recursive determineFields triggered by v.semantic already finished
            if (ad.sizeok != SIZEOKnone)
                return 1;

            if (v.aliassym)
                return 0;   // If this variable was really a tuple, skip it.

            if (v.storage_class & (STCstatic | STCextern | STCtls | STCgshared | STCmanifest | STCctfe | STCtemplateparameter))
                return 0;
            if (!v.isField() || v.semanticRun < PASSsemanticdone)
                return 1;   // unresolvable forward reference

            ad.fields.push(v);

            if (v.storage_class & STCref)
                return 0;
            auto tv = v.type.baseElemOf();
            if (tv.ty != Tstruct)
                return 0;
            if (ad == (cast(TypeStruct)tv).sym)
            {
                const(char)* psz = (v.type.toBasetype().ty == Tsarray) ? "static array of " : "";
                ad.error("cannot have field `%s` with %ssame struct type", v.toChars(), psz);
                ad.type = Type.terror;
                ad.errors = true;
                return 1;
            }
            return 0;
        }

        for (size_t i = 0; i < members.dim; i++)
        {
            auto s = (*members)[i];
            if (s.apply(&func, cast(void*)this))
            {
                if (sizeok != SIZEOKnone)
                {
                    // recursive determineFields already finished
                    return true;
                }
                return false;
            }
        }

        if (sizeok != SIZEOKdone)
            sizeok = SIZEOKfwd;

        return true;
    }

    /***************************************
     * Collect all instance fields, then determine instance size.
     * Returns:
     *      false if failed to determine the size.
     */
    final bool determineSize(Loc loc)
    {
        //printf("AggregateDeclaration::determineSize() %s, sizeok = %d\n", toChars(), sizeok);

        // The previous instance size finalizing had:
        if (type.ty == Terror)
            return false;   // failed already
        if (sizeok == SIZEOKdone)
            return true;    // succeeded

        if (!members)
        {
            error(loc, "unknown size");
            return false;
        }

        if (_scope)
            semantic(this, null);

        // Determine the instance size of base class first.
        if (AggregateDeclaration cd = isClassDeclaration()) // CALYPSO
        {
            cd = toAggregateBase(cd);
            if (cd && !cd.determineSize(loc))
                goto Lfail;
        }

        // Determine instance fields when sizeok == SIZEOKnone
        if (!determineFields())
            goto Lfail;
        if (sizeok != SIZEOKdone)
            finalizeSize();

        // this aggregate type has:
        if (type.ty == Terror)
            return false;   // marked as invalid during the finalizing.
        if (sizeok == SIZEOKdone)
            return true;    // succeeded to calculate instance size.

    Lfail:
        // There's unresolvable forward reference.
        if (type != Type.terror)
            error(loc, "no size because of forward reference");
        // Don't cache errors from speculative semantic, might be resolvable later.
        // https://issues.dlang.org/show_bug.cgi?id=16574
        if (!global.gag)
        {
            type = Type.terror;
            errors = true;
        }
        return false;
    }

    abstract void finalizeSize();

    override final d_uns64 size(Loc loc)
    {
        //printf("+AggregateDeclaration::size() %s, scope = %p, sizeok = %d\n", toChars(), _scope, sizeok);
        bool ok = determineSize(loc);
        //printf("-AggregateDeclaration::size() %s, scope = %p, sizeok = %d\n", toChars(), _scope, sizeok);
        return ok ? structsize : SIZE_INVALID;
    }

    /***************************************
     * Calculate field[i].overlapped and overlapUnsafe, and check that all of explicit
     * field initializers have unique memory space on instance.
     * Returns:
     *      true if any errors happen.
     */
    final bool checkOverlappedFields()
    {
        //printf("AggregateDeclaration::checkOverlappedFields() %s\n", toChars());
        assert(sizeok == SIZEOKdone);
        size_t nfields = fields.dim;
        if (isNested())
        {
            auto cd = isClassDeclaration();
            if (!cd || !cd.baseClass || !cd.baseClass.isNested())
                nfields--;
        }
        bool errors = false;

        // Fill in missing any elements with default initializers
        foreach (i; 0 .. nfields)
        {
            auto vd = fields[i];
            if (vd.errors)
            {
                errors = true;
                continue;
            }

            auto vx = vd;
            if (vd._init && vd._init.isVoidInitializer())
                vx = null;

            // Find overlapped fields with the hole [vd.offset .. vd.offset.size()].
            foreach (j; 0 .. nfields)
            {
                if (i == j)
                    continue;
                auto v2 = fields[j];
                if (v2.errors)
                {
                    errors = true;
                    continue;
                }
                if (!vd.isOverlappedWith(v2))
                    continue;

                // vd and v2 are overlapping.
                vd.overlapped = true;
                v2.overlapped = true;

                if (!MODimplicitConv(vd.type.mod, v2.type.mod))
                    v2.overlapUnsafe = true;
                if (!MODimplicitConv(v2.type.mod, vd.type.mod))
                    vd.overlapUnsafe = true;

                if (!vx)
                    continue;
                if (v2._init && v2._init.isVoidInitializer())
                    continue;

                if (vx._init && v2._init)
                {
                    .error(loc, "overlapping default initialization for field `%s` and `%s`", v2.toChars(), vd.toChars());
                    errors = true;
                }
            }
        }
        return errors;
    }

    /***************************************
     * Fit elements[] to the corresponding types of the struct's fields.
     *
     * Params:
     *      loc = location to use for error messages
     *      sc = context
     *      elements = explicit arguments used to construct object
     *      stype = the constructed object type.
     * Returns:
     *      false if any errors occur,
     *      otherwise true and elements[] are rewritten for the output.
     */
    final bool fit(Loc loc, Scope* sc, Expressions* elements, Type stype) // CALYPSO moved from dstruct.d
    {
        if (!elements)
            return true;

        size_t nfields = fields.dim - isNested();
        size_t offset = 0;
        for (size_t i = 0; i < elements.dim; i++)
        {
            Expression e = (*elements)[i];
            if (!e)
                continue;

            e = resolveProperties(sc, e);
            if (i >= nfields)
            {
                if (i == fields.dim - 1 && isNested() && e.op == TOKnull)
                {
                    // CTFE sometimes creates null as hidden pointer; we'll allow this.
                    continue;
                }
                .error(loc, "more initializers than fields (%d) of %s", nfields, toChars());
                return false;
            }
            VarDeclaration v = fields[i];
            if (v.offset < offset)
            {
                .error(loc, "overlapping initialization for %s", v.toChars());
                return false;
            }
            offset = cast(uint)(v.offset + v.type.size());

            Type t = v.type;
            if (stype)
                t = t.addMod(stype.mod);
            Type origType = t;
            Type tb = t.toBasetype();

            /* Look for case of initializing a static array with a too-short
             * string literal, such as:
             *  char[5] foo = "abc";
             * Allow this by doing an explicit cast, which will lengthen the string
             * literal.
             */
            if (e.op == TOKstring && tb.ty == Tsarray)
            {
                StringExp se = cast(StringExp)e;
                Type typeb = se.type.toBasetype();
                TY tynto = tb.nextOf().ty;
                if (!se.committed &&
                    (typeb.ty == Tarray || typeb.ty == Tsarray) &&
                    (tynto == Tchar || tynto == Twchar || tynto == Tdchar) &&
                    se.numberOfCodeUnits(tynto) < (cast(TypeSArray)tb).dim.toInteger())
                {
                    e = se.castTo(sc, t);
                    goto L1;
                }
            }

            while (!e.implicitConvTo(t) && tb.ty == Tsarray)
            {
                /* Static array initialization, as in:
                 *  T[3][5] = e;
                 */
                t = tb.nextOf();
                tb = t.toBasetype();
            }
            if (!e.implicitConvTo(t))
                t = origType; // restore type for better diagnostic

            e = e.implicitCastTo(sc, t);
        L1:
            if (e.op == TOKerror)
                return false;

            (*elements)[i] = doCopyOrMove(sc, e);
        }
        return true;
    }

    /***************************************
     * Fill out remainder of elements[] with default initializers for fields[].
     * Params:
     *      loc         = location
     *      elements    = explicit arguments which given to construct object.
     *      ctorinit    = true if the elements will be used for default initialization.
     * Returns:
     *      false if any errors occur.
     *      Otherwise, returns true and the missing arguments will be pushed in elements[].
     */
    final bool fill(Loc loc, Expressions* elements, bool ctorinit)
    {
        //printf("AggregateDeclaration::fill() %s\n", toChars());
        assert(sizeok == SIZEOKdone);
        assert(elements);
        size_t nelems = literalElemDim(); // CALYPSO fill() was generalized for class values (TODO: C++ multiple inheritance support)
        bool errors = false;

        size_t dim = elements.dim;
        elements.setDim(nelems);
        foreach (size_t i; dim .. nelems)
            (*elements)[i] = null;

        size_t elemoff;
        void fillAgg(AggregateDeclaration ad)
        {
            size_t nfields = fields.dim - isNested();

            if (auto base = toAggregateBase(ad))
                fillAgg(base);

            // Fill in missing any elements with default initializers
            foreach (i; 0 .. nfields)
            {
                if ((*elements)[elemoff + i])
                    continue;

                auto vd = fields[i];
                auto vx = vd;
                if (vd._init && vd._init.isVoidInitializer())
                    vx = null;

                // Find overlapped fields with the hole [vd.offset .. vd.offset.size()].
                size_t fieldi = i;
                foreach (j; 0 .. nfields)
                {
                    if (i == j)
                        continue;
                    auto v2 = fields[j];
                    if (!vd.isOverlappedWith(v2))
                        continue;

                    if ((*elements)[elemoff + j])
                    {
                        vx = null;
                        break;
                    }
                    if (v2._init && v2._init.isVoidInitializer())
                        continue;

                    version (all)
                    {
                        /* Prefer first found non-void-initialized field
                        * union U { int a; int b = 2; }
                        * U u;    // Error: overlapping initialization for field a and b
                        */
                        if (!vx)
                        {
                            vx = v2;
                            fieldi = j;
                        }
                        else if (v2._init)
                        {
                            .error(loc, "overlapping initialization for field `%s` and `%s`", v2.toChars(), vd.toChars());
                            errors = true;
                        }
                    }
                    else
                    {
                        // fixes https://issues.dlang.org/show_bug.cgi?id=1432 by enabling this path always

                        /* Prefer explicitly initialized field
                        * union U { int a; int b = 2; }
                        * U u;    // OK (u.b == 2)
                        */
                        if (!vx || !vx._init && v2._init)
                        {
                            vx = v2;
                            fieldi = j;
                        }
                        else if (vx != vd && !vx.isOverlappedWith(v2))
                        {
                            // Both vx and v2 fills vd, but vx and v2 does not overlap
                        }
                        else if (vx._init && v2._init)
                        {
                            .error(loc, "overlapping default initialization for field `%s` and `%s`",
                                v2.toChars(), vd.toChars());
                            errors = true;
                        }
                        else
                            assert(vx._init || !vx._init && !v2._init);
                    }
                }
                if (vx)
                {
                    Expression e;
                    if (vx.type.size() == 0)
                    {
                        e = null;
                    }
                    else if (vx._init)
                    {
                        assert(!vx._init.isVoidInitializer());
                        e = vx.getConstInitializer(false);
                    }
                    else
                    {
                        if ((vx.storage_class & STCnodefaultctor) && !ctorinit)
                        {
                            .error(loc, "field `%s.%s` must be initialized because it has no default constructor",
                                type.toChars(), vx.toChars());
                            errors = true;
                        }
                        /* https://issues.dlang.org/show_bug.cgi?id=12509
                         * Get the element of static array type.
                         */
                        Type telem = vx.type;
                        if (telem.ty == Tsarray)
                        {
                            /* We cannot use Type::baseElemOf() here.
                            * If the bottom of the Tsarray is an enum type, baseElemOf()
                            * will return the base of the enum, and its default initializer
                            * would be different from the enum's.
                            */
                            while (telem.toBasetype().ty == Tsarray)
                                telem = (cast(TypeSArray)telem.toBasetype()).next;
                            if (telem.ty == Tvoid)
                                telem = Type.tuns8.addMod(telem.mod);
                        }
                        if (telem.needsNested() && ctorinit)
                            e = telem.defaultInit(loc);
                        else
                            e = telem.defaultInitLiteral(loc);
                    }
                    (*elements)[elemoff + fieldi] = e;
                }
            }
            elemoff += nfields;
        }
        fillAgg(this);

        foreach (e; *elements)
        {
            if (e && e.op == TOKerror)
                return false;
        }

        return !errors;
    }

    /****************************
     * Do byte or word alignment as necessary.
     * Align sizes of 0, as we may not know array sizes yet.
     *
     * alignment: struct alignment that is in effect
     * size: alignment requirement of field
     */
    static void alignmember(structalign_t alignment, uint size, uint* poffset)
    {
        //printf("alignment = %d, size = %d, offset = %d\n",alignment,size,offset);
        switch (alignment)
        {
        case cast(structalign_t)1:
            // No alignment
            break;

        case cast(structalign_t)STRUCTALIGN_DEFAULT:
            // Alignment in Target::fieldalignsize must match what the
            // corresponding C compiler's default alignment behavior is.
            assert(size > 0 && !(size & (size - 1)));
            *poffset = (*poffset + size - 1) & ~(size - 1);
            break;

        default:
            // Align on alignment boundary, which must be a positive power of 2
            assert(alignment > 0 && !(alignment & (alignment - 1)));
            *poffset = (*poffset + alignment - 1) & ~(alignment - 1);
            break;
        }
    }

    /****************************************
     * Place a member (mem) into an aggregate (agg), which can be a struct, union or class
     * Returns:
     *      offset to place field at
     *
     * nextoffset:    next location in aggregate
     * memsize:       size of member
     * memalignsize:  natural alignment of member
     * alignment:     alignment in effect for this member
     * paggsize:      size of aggregate (updated)
     * paggalignsize: alignment of aggregate (updated)
     * isunion:       the aggregate is a union
     */
    static uint placeField(uint* nextoffset, uint memsize, uint memalignsize,
        structalign_t alignment, uint* paggsize, uint* paggalignsize, bool isunion)
    {
        uint ofs = *nextoffset;

        const uint actualAlignment =
            alignment == STRUCTALIGN_DEFAULT ? memalignsize : alignment;

        // Ensure no overflow
        bool overflow;
        const sz = addu(memsize, actualAlignment, overflow);
        const sum = addu(ofs, sz, overflow);
        if (overflow) assert(0);

        alignmember(alignment, memalignsize, &ofs);
        uint memoffset = ofs;
        ofs += memsize;
        if (ofs > *paggsize)
            *paggsize = ofs;
        if (!isunion)
            *nextoffset = ofs;

        if (*paggalignsize < actualAlignment)
            *paggalignsize = actualAlignment;

        return memoffset;
    }

    override final Type getType()
    {
        return type;
    }

    // is aggregate deprecated?
    override final bool isDeprecated()
    {
        return isdeprecated;
    }

    /****************************************
     * Returns true if there's an extra member which is the 'this'
     * pointer to the enclosing context (enclosing aggregate or function)
     */
    final bool isNested()
    {
        return enclosing !is null;
    }

    /* Append vthis field (this.tupleof[$-1]) to make this aggregate type nested.
     */
    void makeNested() // CALYPSO
    {
        if (enclosing) // if already nested
            return;
        if (sizeok == SIZEOKdone)
            return;
        if (isUnionDeclaration() || isInterfaceDeclaration())
            return;
        if (storage_class & STCstatic)
            return;

        // If nested struct, add in hidden 'this' pointer to outer scope
        auto s = toParent2();
        if (!s)
            return;
        Type t = null;
        if (auto fd = s.isFuncDeclaration())
        {
            enclosing = fd;

            /* https://issues.dlang.org/show_bug.cgi?id=14422
             * If a nested class parent is a function, its
             * context pointer (== `outer`) should be void* always.
             */
            t = Type.tvoidptr;
        }
        else if (auto ad = s.isAggregateDeclaration())
        {
            if (isClassDeclaration() && ad.isClassDeclaration())
            {
                enclosing = ad;
            }
            else if (isStructDeclaration())
            {
                if (auto ti = ad.parent.isTemplateInstance())
                {
                    enclosing = ti.enclosing;
                }
            }
            t = ad.handleType();
        }
        if (enclosing)
        {
            //printf("makeNested %s, enclosing = %s\n", toChars(), enclosing.toChars());
            assert(t);
            if (t.ty == Tstruct)
                t = Type.tvoidptr; // t should not be a ref type

            assert(!vthis);
            vthis = new ThisDeclaration(loc, t);
            //vthis.storage_class |= STCref;

            // Emulate vthis.addMember()
            members.push(vthis);

            // Emulate vthis.semantic()
            vthis.storage_class |= STCfield;
            vthis.parent = this;
            vthis.protection = Prot(PROTpublic);
            vthis.alignment = t.alignment();
            vthis.semanticRun = PASSsemanticdone;

            if (sizeok == SIZEOKfwd)
                fields.push(vthis);
        }
    }

    override final bool isExport()
    {
        return protection.kind == PROTexport;
    }

    /*******************************************
     * Look for constructor declaration.
     */
    final Dsymbol searchCtor()
    {
        auto s = search(Loc(), Id.ctor);
        if (s)
        {
            if (!(s.isCtorDeclaration() ||
                  s.isTemplateDeclaration() ||
                  s.isOverloadSet()))
            {
                s.error("is not a constructor; identifiers starting with __ are reserved for the implementation");
                errors = true;
                s = null;
            }
        }
        if (s && s.toParent() != this)
            s = null; // search() looks through ancestor classes
        if (s)
        {
            // Finish all constructors semantics to determine this.noDefaultCtor.
            struct SearchCtor
            {
                extern (C++) static int fp(Dsymbol s, void* ctxt)
                {
                    auto f = s.isCtorDeclaration();
                    if (f && f.semanticRun == PASSinit)
                        f.semantic(null);
                    return 0;
                }
            }

            for (size_t i = 0; i < members.dim; i++)
            {
                auto sm = (*members)[i];
                sm.apply(&SearchCtor.fp, null);
            }
        }
        return s;
    }

    // CALYPSO moved from dclass.d
    /*********************************************
     * Determine if 'this' is a base class of cd.
     * This is used to detect circular inheritance only.
     */
    final bool isBaseOf2(ClassDeclaration cd)
    {
        if (!cd)
            return false;
        //printf("ClassDeclaration.isBaseOf2(this = '%s', cd = '%s')\n", toChars(), cd.toChars());
        for (size_t i = 0; i < cd.baseclasses.dim; i++)
        {
            BaseClass* b = (*cd.baseclasses)[i];
            if (b.sym == this || isBaseOf2(isClassDeclarationOrNull(b.sym)))
                return true;
        }
        return false;
    }

    // CALYPSO moved from dclass.d
    /*******************************************
     * Determine if 'this' is a base class of cd.
     */
    bool isBaseOf(ClassDeclaration cd, int* poffset)
    {
        //printf("ClassDeclaration.isBaseOf(this = '%s', cd = '%s')\n", toChars(), cd.toChars());
        if (poffset)
            *poffset = 0;
        while (cd)
        {
            /* cd.baseClass might not be set if cd is forward referenced.
             */
            if (!cd.baseClass && cd.semanticRun < PASSsemanticdone && !cd.isInterfaceDeclaration())
            {
                cd.semantic(null);
                if (!cd.baseClass && cd.semanticRun < PASSsemanticdone)
                    cd.error("base class is forward referenced by %s", toChars());
            }

            if (this == cd.baseClass)
                return true;

            cd = isClassDeclarationOrNull(cd.baseClass);
        }
        return false;
    }

    override final Prot prot()
    {
        return protection;
    }

    // 'this' type
    final Type handleType()
    {
        return type;
    }

    // CALYPSO
    bool byRef() const
    {
        return false;
    }

    bool mayBeAnonymous()
    {
        return false;
    }

    Expression defaultInit(Loc loc)
    {
        assert(false);
    }

    final Expression defaultInitLiteral(Loc loc)
    {
        size(loc);
        if (sizeok != SIZEOKdone)
            return new ErrorExp();

        auto structelems = new Expressions();
        structelems.setDim(literalElemDim());

        size_t elem_i = 0;
        Expression addAgg(Loc loc, AggregateDeclaration ad) // returns null if ok, ErrorExp if not
        {
            if (auto cd = ad.isClassDeclaration())
                foreach (b; *cd.baseclasses)  // CALYPSO
                    if (auto e = addAgg(loc, b.sym))
                        return e;

            uint offset = 0;
            auto fieldsDim = ad.fields.dim - ad.isNested();
            for (size_t j = 0; j < fieldsDim; j++, elem_i++)
            {
                VarDeclaration vd = ad.fields[j];
                Expression e;
                if (vd.inuse)
                {
                    error(loc, "circular reference to '%s'", vd.toPrettyChars());
                    return new ErrorExp();
                }
                if (vd.offset < offset || vd.type.size() == 0)
                    e = null;
                else if (vd._init)
                {
                    if (vd._init.isVoidInitializer())
                        e = null;
                    else
                        e = vd.getConstInitializer(false);
                }
                else
                    e = vd.type.defaultInitLiteral(loc);
                if (e && e.op == TOKerror)
                    return e;
                if (e)
                    offset = vd.offset + cast(uint)vd.type.size();
                (*structelems)[elem_i] = e;
            }
            return null;
        }

        if (auto e = addAgg(loc, this))
            return e;
        assert(elem_i == structelems.dim);

        auto structinit = new StructLiteralExp(loc, this, structelems);

        /* Copy from the initializer symbol for larger symbols,
         * otherwise the literals expressed as code get excessively large.
         */
        if (size(loc) > Target.ptrsize * 4 && !type.needsNested())
            structinit.useStaticInit = true;

        structinit.type = type;
        return structinit;
    }

    final size_t literalElemDim()
    {
        size_t n = 0;
        auto cd = isClassDeclaration();
        if (cd && cd.baseclasses)
            foreach (b; *cd.baseclasses)
                n += b.sym.literalElemDim();
        n += fields.dim - isNested();
        return n;
    }

    final CtorDeclaration hasImplicitCtor(Expression farg)
    {
        if (farg.op == TOKcomment) // do not go beyond depth of 1
            return null;

        size(loc);
        assert(sizeok == SIZEOKdone); // forward ref

        if (!ctor)
            return null;

        auto e = new NoImplicitCtorExp(farg.loc, farg);
        e.type = farg.type;

        auto fargs = new Expressions;
        fargs.push(e);
        FuncDeclaration fd = resolveFuncCall(farg.loc, null, ctor, null, null, fargs, 1|4);
        if (fd && (fd.storage_class & STCimplicit)) // explicitly enabled @implicit constructor calls
        {
            assert(fd.isCtorDeclaration());
            return cast(CtorDeclaration) fd;
        }

        return null;
    }

    CtorDeclaration hasCopyCtor(Scope* sc)
    {
        return null;
    }

    Expression buildVarInitializer(Scope* sc, VarDeclaration vd, Expression exp)
    {
        return null;
    }

    // Back end
    Symbol* stag; // tag symbol for debug data
    Symbol* sinit;

    override final inout(AggregateDeclaration) isAggregateDeclaration() inout
    {
        return this;
    }

    override void accept(Visitor v)
    {
        v.visit(this);
    }
}

// CALYPSO
// While looking for implicit constructors wrap the argument inside a tagged expression to prevent
// infinite recursion such as Type1(Type2(Type1(Type2(...
extern(C++) class NoImplicitCtorExp : TaggedExp
{
public:
    final extern(D) this(Loc loc, Expression e1)
    {
        super(loc, TOKcomment, NoImplicitCtorExp.sizeof, e1);
    }
}

// CALYPSO
extern(C++) void markAggregateReferenced(AggregateDeclaration ad)
{
    if (auto cd = ad.isClassDeclaration())
        foreach (baseClass; *cd.baseclasses)
            if (auto blp = baseClass.sym.langPlugin())
                blp.markSymbolReferenced(baseClass.sym);

    if (auto lp = ad.langPlugin())
    {
        if (ad.defaultCtor)
            lp.markSymbolReferenced(ad.defaultCtor);
        if (ad.dtor)
            lp.markSymbolReferenced(ad.dtor);
    }

    void visitAggSyms(Type t)
    {
        switch (t.ty) {
        case Taarray:
            visitAggSyms((cast(TypeAArray)t).index);
            visitAggSyms(t.nextOf());
            break;
        case Tarray:
        case Tsarray:
            visitAggSyms(t.nextOf());
            break;
        case Tvector:
            visitAggSyms((cast(TypeVector)t).basetype);
            break;
        default:
        {
            auto aggSym = getAggregateSym(t);
            if (aggSym && aggSym.langPlugin())
                aggSym.langPlugin().markSymbolReferenced(aggSym);
            break;
        }
        }
    }

    foreach (vd; ad.fields)
        visitAggSyms(vd.type);
}

