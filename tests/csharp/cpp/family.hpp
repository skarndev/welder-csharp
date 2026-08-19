#pragma once
// Version-FAMILY synthesis (options::family_surface): a class template welded
// per "era" through namespace-scope aliases, every instantiation deriving one
// welded base — the versioned-format shape wowlib welds. The render pass
// hoists the member INTERSECTION onto the base as dispatch members, so
// base-typed code reads and writes data without a downcast:
//   - identical spellings hoist with the exact type (scalar, string, the
//     shared scalar-sequence wrapper);
//   - a welded member hoists as the member types' common welded base;
//   - a sequence of welded elements hoists as a FamilyVector<ElementBase>
//     read view;
//   - identically-spelled methods hoist as forwarding dispatch;
//   - a member whose type varies per era stays on the concretes.
//
// #included by gen_family.cpp (the WELDER_CSHARP_MAIN generator) after the
// welder vocabulary, and by the generated shim.cpp.
#include <welder/rods/csharp/lang.hpp>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>
#include <welder/vocabulary.hpp>

namespace family_ns {

// The nested entity's own family: a welded base + per-era instantiations, so
// the outer family's `gadget` / `gadgets` members have a base to hoist to.
struct
[[=welder::weld]]
GadgetBase {};

template <int V>
struct
[[=welder::weld]]
Gadget : GadgetBase {
    [[=welder::doc("The gadget's power rating.")]]
    int power{V};
};
using GadgetV1 = Gadget<1>;
using GadgetV2 = Gadget<2>;

struct
[[=welder::weld]]
WidgetBase {};

template <int V>
struct
[[=welder::weld]]
Widget : WidgetBase {
    [[=welder::doc("A scalar every era binds identically.")]]
    int shared_scalar{V};
    std::string label{};
    // A non-const scalar sequence binds as the live wrapper — one generated
    // type for every era, so it hoists with that exact type.
    std::vector<std::uint16_t> nums{};
    // A welded member: hoists as GadgetBase (getter upcast, setter downcast).
    Gadget<V> gadget{};
    // A sequence of welded elements: hoists as FamilyVector<GadgetBase>.
    std::vector<Gadget<V>> gadgets{};
    // The era-gated shape: the C# type differs per era, so it stays on the
    // concretes (reached by pattern matching).
    std::conditional_t<V == 1, std::int32_t, double> era_gated{};

    [[=welder::doc("The era this instantiation is.")]]
    int era() const { return V; }
    int scaled(int k) const { return V * k; }
};
using WidgetV1 = Widget<1>;
using WidgetV2 = Widget<2>;

} // namespace family_ns
