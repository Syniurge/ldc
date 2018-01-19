## proposal: customizable imports with pragma and CppImport option struct

### summary
* no new syntax required (no modmap(C++) or import(C++)); instead uses pragma + CppImport option struct
* self-documenting and trivially extensible to allowing precisely customizing what symbols get imported, how (UDA, attributes), and where (specifying qualified names)

### benefits

* D20180118T190059: automatically benefit from pragma syntax which supports applying pragma to one, all remaining, or a scope of statements.
* non-calypso-aware compilers can still compile calypso code with proper `version(Calypso)`
* exising parsers (libdparse, dscanner, sublime text syntax highlight, etc) understand calypso out of the box; 
* will be hard to convince any of them to support calypso special syntax until calypso gets merged into LDC (and even then, calypso would likely only ever be LDC, not DMD or GDC so others might not care about supporting calypso) 
* allows arbitrarily customization of what symbols we import, add annotations/UDAs, and bulk-rename; not clear how to do without this proposal!
* allows scoping what modmap applies to

### example
```
// in builtin module `cpp.core`
struct CppImport{
	// whether to recursively import namespaces, requires `static import` (and therefore fully qualified names) to avoid name clashes
	bool namespace_recursive=false;

	// whether to import structs, classes, enum's, unions, etc; NOTE: this field could be trivially split-up
	bool import_aggregates=true;

	// whether to import macros
	bool import_macros=true;

	// namespace where macros are imported; `null` to import in top-level namespace
	bool namespace_macros=""; // CHECKME: should we use a different default, eg: __macros? (more sanitary)

	// add these annotation to declarations: [`@nogc`, `nothrow`] (stringified; they'll be mixed-in)
	string[] annotations = [];


	// optionally apply a filter on symbols, eg: `name => name.startsWith("fftw_")`
	// especially useful for importing C libraries, which lack namespaces
	bool function(string) name_filter = null;

  // optionally map symbols, eg: `name => "_cpp_"~name`
  // especially useful for renaming C/C++ symbols that could cause name clashes
	bool function(string) name_map = null;

	// if non-null, will add a UDA named with that field containing C++ comment for that declaration
	string uda_cpp_comment;

	// ditto with file
	string uda_cpp_file;

	// ditto with line
	string uda_cpp_line;

	// eg: ["opencv2.h"]
	// this one is controversial, could be keps as a `pragma(modmap, C++, "fun.h")` or not
	string[] modmaps;
}
```

```
// fun.h:
namespace foo{
  struct Bar{};
  namespace sub {}
}

// in user code:
import core.cpp; // or put it in object.d to make it implicit

enum CppImport custom = {
	modmaps: ["fun.h"],
	namespace_macros: "foobar",
	name_filter: a=>a.startsWith("fftw_"),
	annotations: [`@nogc`, `nothrow`],
};

// NOTE: all pragma scopes can be used, cf D20180118T190059
pragma(import, C++){
	pragma(customize_import, custom)
	import foo;

	enum CppImport custom_rec = { modmaps: ["fun.h"], namespace_recursive:true };
	pragma(customize_import, custom_rec)
	static import foo; // static required with `namespace_recursive`; allows accessing ℂcpp.foo.sub
}

void main(){
	Bar bar;
	ℂcpp.foo.Bar bar2;  // instead of ℂcpp.foo.Bar.Bar => more intuitive
}
```


NOTE: current calypso behavior is achieved with:
```
enum CppImport custom = {
	import_aggregates: false,
};
```


## other extensions that easily fit in same design:
* add another field with a delegate that gets called when a C++ declaration is considered for import (eg could print the clang AST etc)
* make CppImport a subclass of CustomImport, and add subclasses ObjectivecImport, etc (+ other languages supported by clang)