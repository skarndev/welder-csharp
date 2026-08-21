// The C#/.NET bindings generator over the version-family cases (family.hpp):
// the family-surface synthesis — welded per-era instantiations sharing a
// welded base MARKED [[=welder::mark::family_surface]] gain a
// version-agnostic surface ON the base (an unmarked base gains nothing).
#include <welder/vocabulary.hpp>
#include "family.hpp"
#include <welder/rods/csharp/module.hpp>

WELDER_CSHARP_MAIN(family_ns, "family.hpp", "welder_test_cs_family")
