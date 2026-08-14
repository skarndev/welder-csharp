#pragma once
#include <welder/lang.hpp> // lang + user_lang (std-free)

/** @file
    The C#/.NET **language identity** of this out-of-tree rod.

    welder's core does not name C# — the lang value space is open, and this
    extension mints its identity from the user range (`welder::user_lang`,
    bits 16–31), exactly as the "Extending welder" guide prescribes for
    third-party rods. Annotations in a consumer's headers spell it as
    `welder::rods::csharp::cs`:

    @code
    struct [[=welder::weld]] Point { … };                       // every rod
    struct [[=welder::weld(welder::rods::csharp::cs)]] Db { … };// this rod only
    @endcode

    (A bare `[[=welder::weld]]` already covers every language, this one
    included — the scoped spelling is only needed to *restrict* to C#.)

    **Slot collisions.** The default slot is 0. If another out-of-tree rod in
    the same program also claims slot 0, re-point this one by defining
    `WELDER_CSHARP_LANG_SLOT` (0–15) before any of its headers are included —
    every spelling in the rod routes through @ref welder::rods::csharp::cs, so
    the slot is defined in exactly one place.

    Kept std-include-free, mirroring welder's vocabulary-header constraint.
*/

#ifndef WELDER_CSHARP_LANG_SLOT
/** The user-lang slot this rod claims (bit `16 + slot` of the language mask).
    Redefine (0–15) before including any rod header to re-point a collision. */
#define WELDER_CSHARP_LANG_SLOT 0
#endif

namespace welder::inline v0::rods::csharp {

/** The C#/.NET language: this rod's identity in every annotation and
    resolution query (`weld(cs)`, `mark::exclude(cs)`, `member_bound(m, cs)`,
    …). A `welder::lang` value from the user range — see the file note. */
inline constexpr lang cs{user_lang<WELDER_CSHARP_LANG_SLOT>};

} // namespace welder::inline v0::rods::csharp
