#pragma once
#include <meta>

/** @file
    The C# rod's OWN annotation vocabulary — opt-ins that are meta to the C#
    binding rather than to the welded C++ surface, so they live in the rod and
    never touch welder core. One mark today: @ref
    welder::rods::csharp::family_surface, the family-surface opt-in.

    A consumer whose welded headers must also parse WITHOUT this rod (a
    Python-only build of the same project, where welder-csharp is not even
    fetched) spells the mark behind a macro that expands to nothing when the
    rod is absent — the annotation then simply never exists in those builds:
    @code
    #if defined(MYPROJ_WITH_CSHARP_ROD)
    #include <welder/rods/csharp/marks.hpp>
    #define MYPROJ_CS_FAMILY_SURFACE =welder::rods::csharp::family_surface,
    #else
    #define MYPROJ_CS_FAMILY_SURFACE
    #endif

    struct [[
      =welder::weld,
      MYPROJ_CS_FAMILY_SURFACE
      =welder::doc("...")
    ]] EntityBase {};
    @endcode
    The macro (with its trailing comma) must expand consistently across every
    TU of one build tree — drive it from the build system, not from
    `__has_include`, so the generator, the shim and the library agree on the
    class's annotation list.
*/

namespace welder::inline v0::rods::csharp {

/** The stored form of the @ref family_surface mark. */
struct family_surface_spec {};

/** The family-surface OPT-IN: placed on a welded BASE class, it opts the
    base's family — two or more welded classes deriving it, the shape a
    versioned class template welded per instantiation makes — into the
    rod-synthesized version-agnostic surface ON the base (the member
    intersection the derived classes bind identically, as dispatch members;
    see the document assembler's family synthesis). Synthesizing members onto
    a base is too intrusive to infer from structure alone, so the mark is
    strictly required: an unmarked base is never touched.
    @code
    struct [[=welder::weld, =welder::rods::csharp::family_surface]] Base {};
    @endcode */
inline constexpr family_surface_spec family_surface{};

/** Does @a type carry the @ref family_surface opt-in?
    @param type a reflection of the welded base class to test.
    @return `true` iff the mark is present. */
consteval bool family_surface_marked(std::meta::info type) {
    return !std::meta::annotations_of_with_type(type, ^^family_surface_spec)
                .empty();
}

} // namespace welder::inline v0::rods::csharp
