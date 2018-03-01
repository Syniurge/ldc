/**
 * Compiler implementation of the
 * $(LINK2 http://www.dlang.org, D programming language).
 *
 * Copyright:   Copyright (c) 1999-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     $(LINK2 http://www.boost.org/LICENSE_1_0.txt, Boost License 1.0)
 * Source:      $(LINK2 https://github.com/dlang/dmd/blob/master/src/ddmd/dimport.d, _dimport.d)
 */

module ddmd.dimport;

// Online documentation: https://dlang.org/phobos/ddmd_dimport.html

import ddmd.arraytypes;
import ddmd.declaration;
import ddmd.dmodule;
import ddmd.dscope;
import ddmd.dsymbol;
import ddmd.errors;
import ddmd.expression;
import ddmd.globals;
import ddmd.identifier;
import ddmd.mtype;
import ddmd.root.outbuffer;
import ddmd.semantic;
import ddmd.visitor;

/***********************************************************
 */
extern (C++) class Import : Dsymbol // CALYPSO (made non final)
{
    /* static import aliasId = pkg1.pkg2.id : alias1 = name1, alias2 = name2;
     */
    Identifiers* packages;  // array of Identifier's representing packages
    Identifier id;          // module Identifier
    Identifier aliasId;
    int isstatic;           // !=0 if static import
    Prot protection;

    // Pairs of alias=name to bind into current namespace
    Identifiers names;
    Identifiers aliases;

    Module mod;
    Package pkg;            // leftmost package/module

    // corresponding AliasDeclarations for alias=name pairs
    AliasDeclarations aliasdecls;

    extern (D) this(Loc loc, Identifiers* packages, Identifier id, Identifier aliasId, int isstatic)
    {
        super(null);
        assert(id);
        version (none)
        {
            printf("Import::Import(");
            if (packages && packages.dim)
            {
                for (size_t i = 0; i < packages.dim; i++)
                {
                    Identifier id = (*packages)[i];
                    printf("%s.", id.toChars());
                }
            }
            printf("%s)\n", id.toChars());
        }
        this.loc = loc;
        this.packages = packages;
        this.id = id;
        this.aliasId = aliasId;
        this.isstatic = isstatic;
        this.protection = PROTprivate; // default to private
        setSymIdent(); // CALYPSO
    }

    final void setSymIdent() // CALYPSO
    {
        // Set symbol name (bracketed)
        if (aliasId)
        {
            // import [cstdio] = std.stdio;
            this.ident = aliasId;
        }
        else if (packages && packages.dim)
        {
            // import [std].stdio;
            this.ident = (*packages)[0];
        }
        else
        {
            // import [foo];
            this.ident = id;
        }
    }

    final void addAlias(Identifier name, Identifier _alias)
    {
        if (isstatic)
            error("cannot have an import bind list");
        if (!aliasId)
            this.ident = null; // make it an anonymous import
        names.push(name);
        aliases.push(_alias);
    }

    override const(char)* kind() const
    {
        return isstatic ? cast(char*)"static import" : cast(char*)"import";
    }

    override Prot prot()
    {
        return protection;
    }

    // copy only syntax trees
    override Dsymbol syntaxCopy(Dsymbol s)
    {
        assert(!s);
        auto si = new Import(loc, packages, id, aliasId, isstatic);
        for (size_t i = 0; i < names.dim; i++)
        {
            si.addAlias(names[i], aliases[i]);
        }
        return si;
    }

    void load(Scope* sc)
    {
        //printf("Import::load('%s') %p\n", toPrettyChars(), this);
        // See if existing module
        DsymbolTable dst = Package.resolve(packages, null, &pkg);
        version (none)
        {
            if (pkg && pkg.isModule())
            {
                .error(loc, "can only import from a module, not from a member of module %s. Did you mean `import %s : %s`?", pkg.toChars(), pkg.toPrettyChars(), id.toChars());
                mod = pkg.isModule(); // Error recovery - treat as import of that module
                return;
            }
        }
        Dsymbol s = dst.lookup(id);
        if (s)
        {
            if (s.isModule())
                mod = cast(Module)s;
            else
            {
                if (s.isAliasDeclaration())
                {
                    .error(loc, "%s %s conflicts with %s", s.kind(), s.toPrettyChars(), id.toChars());
                }
                else if (Package p = s.isPackage())
                {
                    if (p.isPkgMod == PKGunknown)
                    {
                        mod = loadModule(sc); // CALYPSO
                        if (!mod)
                            p.isPkgMod = PKGpackage;
                        else
                        {
                            // mod is a package.d, or a normal module which conflicts with the package name.
                            assert(mod.isPackageFile == (p.isPkgMod == PKGmodule));
                            if (mod.isPackageFile)
                                mod.tag = p.tag; // reuse the same package tag
                        }
                    }
                    else
                    {
                        mod = p.isPackageMod();
                    }
                    if (!mod)
                    {
                        .error(loc, "can only import from a module, not from package %s.%s", p.toPrettyChars(), id.toChars());
                    }
                }
                else if (pkg)
                {
                    .error(loc, "can only import from a module, not from package %s.%s", pkg.toPrettyChars(), id.toChars());
                }
                else
                {
                    .error(loc, "can only import from a module, not from package %s", id.toChars());
                }
            }
        }
        if (!mod)
        {
            // Load module
            mod = loadModule(sc); // CALYPSO
            if (mod)
            {
                // id may be different from mod.ident, if so then insert alias
                dst.insert(id, mod);
            }
        }
        if (mod && !mod.importedFrom)
            mod.importedFrom = sc ? sc._module.importedFrom : Module.rootModule;
        if (!pkg)
            pkg = mod;
        //printf("-Import::load('%s'), pkg = %p\n", toChars(), pkg);
    }

    override void importAll(Scope* sc)
    {
        if (!mod)
        {
            load(sc);
            if (mod) // if successfully loaded module
            {
                if (mod.md && mod.md.isdeprecated)
                {
                    Expression msg = mod.md.msg;
                    if (StringExp se = msg ? msg.toStringExp() : null)
                        mod.deprecation(loc, "is deprecated - %s", se.string);
                    else
                        mod.deprecation(loc, "is deprecated");
                }
                mod.importAll(null);
                if (sc.explicitProtection)
                    protection = sc.protection;
                if (!isstatic && !aliasId && !names.dim)
                {
                    sc.scopesym.importScope(mod, protection);
                }
            }
        }
    }

    override Dsymbol toAlias()
    {
        if (aliasId)
            return mod;
        return this;
    }

    /*****************************
     * Add import to sd's symbol table.
     */
    override void addMember(Scope* sc, ScopeDsymbol sd)
    {
        //printf("Import.addMember(this=%s, sd=%s, sc=%p)\n", toChars(), sd.toChars(), sc);
        if (names.dim == 0)
            return Dsymbol.addMember(sc, sd);
        if (aliasId)
            Dsymbol.addMember(sc, sd);
        /* Instead of adding the import to sd's symbol table,
         * add each of the alias=name pairs
         */
        for (size_t i = 0; i < names.dim; i++)
        {
            Identifier name = names[i];
            Identifier _alias = aliases[i];
            if (!_alias)
                _alias = name;
            auto tname = new TypeIdentifier(loc, name);
            auto ad = new AliasDeclaration(loc, _alias, tname);
            ad._import = this;
            ad.addMember(sc, sd);
            aliasdecls.push(ad);
        }
    }

    override void setScope(Scope* sc)
    {
        Dsymbol.setScope(sc);
        if (aliasdecls.dim)
        {
            if (!mod)
                importAll(sc);

            sc = sc.push(mod);
            sc.protection = protection;
            foreach (ad; aliasdecls)
                ad.setScope(sc);
            sc = sc.pop();
        }
    }

    override Dsymbol search(Loc loc, Identifier ident, int flags = SearchLocalsOnly)
    {
        //printf("%s.Import.search(ident = '%s', flags = x%x)\n", toChars(), ident.toChars(), flags);
        if (!pkg)
        {
            load(null);
            mod.importAll(null);
            mod.semantic(null);
        }
        // Forward it to the package/module
        return pkg.search(loc, ident, flags);
    }

    override bool overloadInsert(Dsymbol s)
    {
        /* Allow multiple imports with the same package base, but disallow
         * alias collisions
         * https://issues.dlang.org/show_bug.cgi?id=5412
         */
        assert(ident && ident == s.ident);
        Import imp;
        if (!aliasId && (imp = s.isImport()) !is null && !imp.aliasId)
            return true;
        else
            return false;
    }

    // CALYPSO
    Module loadModule(Scope *sc)
    {
        return Module.load(loc, packages, id);
    }

    override inout(Import) isImport() inout
    {
        return this;
    }

    override void accept(Visitor v)
    {
        v.visit(this);
    }
}

// CALYPSO
extern(C++) class Modmap : Dsymbol
{
public:
    StringExp arg;

    extern(D) this(Loc loc, StringExp arg)
    {
        this.loc = loc;
        this.arg = arg;
    }

    override void setScope(Scope* sc)
    {
        Dsymbol.setScope(sc);
        importAll(sc);
    }

    override void accept(Visitor v)
    {
        v.visit(this);
    }
}

import ddmd.aggregate;
import ddmd.dinterpret;
import ddmd.dstruct;
import ddmd.func;
import ddmd.statement;

alias ForeignCodeGen = void*;

extern(C++) interface LangPlugin
{
public:
    void _init();

    // ===== - - - - - ===== //

    // returns -1 if said lang isn't handled by this plugin, or its id number
    // to be passed to createImport otherwise
    int doesHandleModmap(const (char)* lang);

    Modmap createModmap(int langId,
        Loc loc, Expression arg);

    // returns -1 if said tree isn't handled by this plugin, or its id number
    // to be passed to createImport otherwise
    int doesHandleImport(const (char)* tree);

    Import createImport(int treeId,
        Loc loc, Identifiers *packages, Identifier id,
        Identifier aliasId, int isstatic);

    // foreign exceptions
    bool doesHandleCatch(LINK lang);
    Catch createCatch(Loc loc, Type t, Identifier id,
                               Statement handler, StorageClass stc);

    // ===== - - - - - ===== //

    const char *mangle(Dsymbol s); // TODO replace by getForeignMangler

    // create a mangler for types and symbols specific to this plugin
    // base is the D mangler
    Visitor getForeignMangler(OutBuffer *buf, bool forEquiv, Visitor base);

    // ===== - - - - - ===== //

    Expression getRightThis(Loc loc, Scope *sc, AggregateDeclaration ad,
        Expression e1, Declaration var, int flag = 0);
    Expression callCpCtor(Scope *sc, Expression e);

    FuncDeclaration buildDtor(AggregateDeclaration ad, Scope *sc);
    FuncDeclaration buildOpAssign(StructDeclaration sd, Scope *sc);
    FuncDeclaration searchOpEqualsForXopEquals(StructDeclaration sd, Scope *sc);

    // ===== - - - - - ===== //

    bool isSymbolReferenced(Dsymbol s);
    void markSymbolReferenced(Dsymbol s);

    // ===== - - - - - ===== //

    Expression semanticTraits(TraitsExp e, Scope *sc);

    // ===== - - - - - ===== //

    Expression interpret(FuncDeclaration fd, InterState *istate, Expressions *arguments,
                                  Expression thisarg);
    bool canInterpret(FuncDeclaration fd);

    // ===== - - - - - ===== //

    void adjustLinkerArgs(const (char)** args); // FIXME: not the original type

    // ===== - - - - - ===== //
    // mars_mainBody hooks

    void semanticModules();
    void codegenModules();

    // ===== - - - - - ===== //

    ForeignCodeGen codegen();
    bool needsCodegen(Module m);
}

import ddmd.root.array;
alias LangPlugins = Array!(LangPlugin);
extern (C++) __gshared LangPlugins langPlugins; // CALYPSO (1.1 NOTE: moved from globals.d to prevent cycles)
