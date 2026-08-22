#pragma once
#include <cstddef>
#include <meta>
#include <string>

#include <welder/rods/csharp/document/artifacts.hpp>
#include <welder/rods/csharp/marshal/classify.hpp>
#include <welder/rods/csharp/marshal/spellings.hpp>

/** @file
    **Erased data-member access**: the eligibility taxonomy behind
    @ref welder::rods::csharp::field_emitter's shared-stub path.

    The bespoke field path emits one `extern "C"` thunk + one `[LibraryImport]`
    per accessor per member. On a surface dominated by flat data records that
    is the whole cost of the build: a record-heavy consumer surface (measured)
    put ~110k P/Invokes through the interop source generator and the same
    number of thunk instantiations through gcc. But a data member at a KNOWN OFFSET
    needs no per-member code at all — a typed load/store at `self + off` is
    exactly what the bespoke thunk compiles to. So the document carries ~25
    fixed entry points (one per scalar width, plus address/string forms), the
    managed property passes the offset the GENERATOR computed, and per member
    the artifacts gain only a property body and one `static_assert`.

    The layout contract that makes the baked offset sound: the generated shim
    re-derives every erased member's offset on the platform IT compiles on
    (@ref welder::rods::csharp::shim::nsdm_offset) and `static_assert`s it
    against the generator's number, so an ABI whose layout disagrees (a
    `long` member crossing an LP64/LLP64 boundary, say) fails the shim build
    instead of reading the wrong bytes at runtime.

    Eligibility is deliberately narrower than "everything classify accepts":
    - only a member DECLARED by the bound type itself (a member flattened in
      from a non-welded base sits at a base-subobject offset this layer does
      not compute — it keeps the bespoke path);
    - no bitfields (no byte offset), no reference-typed members (the load
      would read the reference, not the referent);
    - `utf8_string` only for exactly `std::string` (the stubs reinterpret the
      member as one — a `string_view`/`path` member keeps the bespoke path);
    - handle-like kinds only when non-const (the live-view `self + off`; a
      const class member crosses by VALUE, a copy the address stub cannot
      make).
    Setters for handle-like members stay bespoke too (`field_set` /
    `field_assign` splice the member's own assignment); only their getter
    erases.
*/

namespace welder::inline v0::rods::csharp {

/** Which shared entry-point family serves a data member, if any. */
enum class erased_way {
    none,    /**< Not eligible — the bespoke per-member path. */
    scalar,  /**< `welder__field_get_<cs>` / `_set_<cs>` typed load/store. */
    boolean, /**< The `bool` pair (`[MarshalAs(U1)]` both ways). */
    enum_,   /**< The underlying scalar's pair + a managed cast. */
    string,  /**< The `std::string` pair (dup out, assign in). */
    addr     /**< `welder__field_addr` — the live-view getter for
                  handle-like members (setter stays bespoke). */
};

/** Classify data member @a Mem for the erased path (see the file note for the
    deliberate exclusions).
    @param Mem a reflection of the data member.
    @return the stub family, or @ref erased_way::none for the bespoke path. */
consteval erased_way erased_field_way(std::meta::info Mem) {
    namespace m = std::meta;
    if (m::is_bit_field(Mem))
        return erased_way::none;
    const m::info MT{m::type_of(Mem)};
    if (m::is_reference_type(MT) || m::is_volatile_type(m::remove_cv(MT)))
        return erased_way::none;
    switch (classify(MT)) {
        case marshal_kind::scalar:  return erased_way::scalar;
        case marshal_kind::boolean: return erased_way::boolean;
        case marshal_kind::enum_:   return erased_way::enum_;
        case marshal_kind::utf8_string:
            return bare(MT) == m::dealias(^^std::string) ? erased_way::string
                                                         : erased_way::none;
        case marshal_kind::handle:
        case marshal_kind::seq_ref:
        case marshal_kind::map_ref:
            return m::is_const_type(MT) ? erased_way::none : erased_way::addr;
        default:
            return erased_way::none;
    }
}

/** Whether a seq_value member may take the erased ADDRESS getter (the live
    scalar-sequence wrapper's target) — the same structural rules as
    @ref erased_field_way, minus the kind switch its caller already decided.
    @param Mem a reflection of the sequence data member.
    @return true when `self + offset` IS the member's address. */
consteval bool erased_seq_eligible(std::meta::info Mem) {
    namespace m = std::meta;
    return !m::is_bit_field(Mem) &&
           !m::is_reference_type(m::type_of(Mem));
}

/** The member's index among ALL of its declaring class's nonstatic data
    members (unchecked access context) — the enumeration the shim-side
    @ref welder::rods::csharp::shim::nsdm_offset indexes with.
    @param Mem a reflection of the data member.
    @return the index. */
consteval std::size_t nsdm_index(std::meta::info Mem) {
    const auto ms{std::meta::nonstatic_data_members_of(
        std::meta::parent_of(Mem), std::meta::access_context::unchecked())};
    for (std::size_t i{0}; i < ms.size(); ++i)
        if (ms[i] == Mem)
            return i;
    return static_cast<std::size_t>(-1); // unreachable for a real member
}

/** Flip the document's erased-stubs flag, registering the fixed entry-point
    symbols exactly once — so a welded entity whose underscore path happens to
    spell one of them collides LOUDLY (the record_symbol diagnostic) instead
    of linking two definitions.
    @param doc the two-artifact document. */
inline void claim_erased_stubs(document& doc) {
    if (doc.erased_used)
        return;
    doc.erased_used = true;
    static constexpr const char* names[]{
        "welder__field_get_sbyte",  "welder__field_set_sbyte",
        "welder__field_get_byte",   "welder__field_set_byte",
        "welder__field_get_short",  "welder__field_set_short",
        "welder__field_get_ushort", "welder__field_set_ushort",
        "welder__field_get_int",    "welder__field_set_int",
        "welder__field_get_uint",   "welder__field_set_uint",
        "welder__field_get_long",   "welder__field_set_long",
        "welder__field_get_ulong",  "welder__field_set_ulong",
        "welder__field_get_float",  "welder__field_set_float",
        "welder__field_get_double", "welder__field_set_double",
        "welder__field_get_bool",   "welder__field_set_bool",
        "welder__field_addr",
        "welder__field_get_str",    "welder__field_set_str"};
    for (const char* n : names)
        doc.record_symbol(n);
}

} // namespace welder::inline v0::rods::csharp
