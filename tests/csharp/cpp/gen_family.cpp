// The C#/.NET bindings generator over the version-family cases (family.hpp):
// the options::family_surface synthesis — welded per-era instantiations
// sharing a welded base gain a version-agnostic surface ON the base.
#include <welder/vocabulary.hpp>
#include "family.hpp"
#include <welder/rods/csharp/module.hpp>

WELDER_CSHARP_MAIN(family_ns, "family.hpp", "welder_test_cs_family")
