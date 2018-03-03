// Contributed by Elie Morisse, same license DMD uses

import ddmd.dsymbolsem;
import ddmd.dmangle;

shared static this()
{
    TI_DsymbolSem1Visitor = cast(void*) typeid(DsymbolSemanticVisitor);
    TI_DsymbolSem2Visitor = cast(void*) typeid(Semantic2Visitor);
    TI_DsymbolSem3Visitor = cast(void*) typeid(Semantic3Visitor);

    TI_Mangler = cast(void*) typeid(Mangler);
}

extern (C++) __gshared:

const(void*) TI_DsymbolSem1Visitor;
const(void*) TI_DsymbolSem2Visitor;
const(void*) TI_DsymbolSem3Visitor;

const(void*) TI_Mangler;