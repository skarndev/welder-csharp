#pragma once

/** @file
    This rod's **consteval diagnostics** — the welder `diag.hpp` idiom
    (an exception struct carrying its canonical message as a `const char*`
    default member init, THROWN during constant evaluation, so the compiler
    reports ONE error with the type and prose verbatim), housed here because
    an out-of-tree rod's diagnostics do not belong in welder's core catalogue.

    The namespace is `welder::rods::csharp::diag`, which deliberately SHADOWS
    `welder::diag` inside the rod's own namespace: rod code spells
    `diag::csharp_unmarshallable` unqualified and gets these; core diagnostics
    remain reachable as `::welder::diag::…` (the rod currently needs none).

    Kept std-include-free, like welder's own diag catalogue.
*/

namespace welder::inline v0::rods::csharp::diag {

/** Thrown by the rod's marshalling gate
    (`<welder/rods/csharp/type_map.hpp>`) when a participating member's type is
    admitted by the bindability gate but cannot yet cross the C ABI — the
    marshalling families land phase by phase, and a silent `void*` would
    corrupt data rather than fail loudly. */
struct csharp_unmarshallable {
    /** What went wrong and how to fix it. */
    const char* what =
        "welder: this type cannot cross the C#/.NET C-ABI boundary yet (the "
        "marshalling for this type family is not implemented); exclude the "
        "member for the C# lang (mark::exclude(welder::rods::csharp::cs)), or "
        "wait for the family to land";
};

/** Thrown by the rod's member-lookup layer
    (`<welder/rods/csharp/type_map.hpp>` — `named_member` / `ctor_at` /
    `named_field`) when the generated shim's re-derivation finds no matching
    declaration: the welded header changed between generating the shim and
    compiling it, so splicing would bind the wrong member. Regenerate (the
    CMake helper re-runs the generator when the header is a DEPENDS). */
struct csharp_member_lookup_mismatch {
    /** What went wrong and how to fix it. */
    const char* what =
        "welder: the generated C# shim references a member the welded header "
        "no longer declares at that position - the header changed since the "
        "shim was generated; re-run the bindings generator (a stale build "
        "artifact), do not edit the generated shim";
};

} // namespace welder::inline v0::rods::csharp::diag
