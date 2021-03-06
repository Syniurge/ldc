// Contributed by Elie Morisse, same license DMD uses

#ifndef DMD_CPP_CPPIMPORT_H
#define DMD_CPP_CPPIMPORT_H

#ifdef __DMC__
#pragma once
#endif /* __DMC__ */

#include "root/root.h"
#include "dsymbol.h"
#include "cpp/calypso.h"

#include "import.h"

namespace cpp {

class Package;

class Import final : public ::Import
{
public:
    CALYPSO_LANGPLUGIN

    Import(Loc loc, Identifiers *packages, Identifier *id, Identifier *aliasId, int isstatic);

    ::Module *loadModule(Scope* sc, Package* pparent);
    bool load(Scope* sc) override;
};

}

#endif /* DMD_CPP_CPPIMPORT_H */
