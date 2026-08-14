// The C# bindings generator over the SHARED overridable cases
// (tests/common/cpp/overridable.hpp — Animal/Bird/Robot welded for welder::rods::csharp::cs):
// C# directors over the same virtual surfaces the Python trampolines cover
// (bind_flat, inherited slots, parameterful/noexcept/overloaded virtuals, the
// unbound NVI hook falling through to the base).
#include "shared_seam.hpp"
#include <welder/rods/csharp/lang.hpp>
#include <welder/rods/csharp/module.hpp>

WELDER_CSHARP_MAIN(overridable, "shared_seam.hpp", "welder_test_cs_overridable")
