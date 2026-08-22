#pragma once
#include <cstddef>
#include <meta>
#include <string>
#include <type_traits>

#include <welder/naming.hpp>
#include <welder/rods/csharp/document.hpp>
#include <welder/rods/csharp/emit/refs.hpp>
#include <welder/rods/csharp/emit/spellings.hpp>
#include <welder/rods/csharp/naming.hpp>
#include <welder/rods/csharp/type_map.hpp>

/** @file
    **Blittable value mirrors**: the bulk path for POD record data.

    A welded class whose layout is nothing but fixed-width leaves — scalars,
    enums, bools, fixed scalar arrays, and nested such records — gets a nested
    `public struct Data`: an explicit-layout blittable twin of the NATIVE
    layout, every field at the generator-computed offset, the struct sized to
    `sizeof(T)`. `Vector<T>.AsSpan<T.Data>()` / `FixedArray<T>.AsSpan<T.Data>()`
    then reinterpret the container's contiguous storage as ONE managed span —
    a single interop crossing for a whole record buffer, where the live-view
    indexer pays one or more per ELEMENT. This is the record-typed sibling of
    the scalar wrappers' zero-copy `AsSpan()` (C#'s buffer protocol), built
    for per-vertex parsing loops: a consumer profile showed normals decoding
    dominated by element-wise view construction.

    Layout safety is the erased-fields contract, extended: the shim carries a
    `static_assert` for the struct SIZE, for trivial copyability, and for
    every member offset, so a platform whose ABI disagrees fails the native
    build instead of misreading.

    Mirror field names use the dotnet style (the rod's default), matching the
    wrapper's properties; a member named `Data` on the class suppresses the
    mirror (diagnosed as a comment, not an error — the bulk path is an
    optimization, not a contract). */

namespace welder::inline v0::rods::csharp {

/** Whether welded class @a T qualifies for a blittable `Data` mirror:
    trivially copyable, no virtual/bit-field/const/non-public members, only
    member-less bases, and every member a fixed-width leaf — scalar, enum,
    bool, a fixed SCALAR array (a C# fixed buffer), or a nested class that
    qualifies itself.
    @param T     a reflection of the (dealiased) class.
    @param depth the nesting recursion guard.
    @return whether the mirror can be emitted. */
consteval bool pod_mirror_eligible(std::meta::info T, int depth = 0) {
    namespace m = std::meta;
    T = m::dealias(T);
    if (depth > 8 || !m::is_class_type(T))
        return false;
    if (!type_trait(^^std::is_trivially_copyable_v, T))
        return false;
    for (auto b : m::bases_of(T, m::access_context::unchecked())) {
        if (m::is_virtual(b))
            return false;
        if (!m::nonstatic_data_members_of(m::dealias(m::type_of(b)),
                                          m::access_context::unchecked())
                 .empty())
            return false;
    }
    auto ms{m::nonstatic_data_members_of(T, m::access_context::unchecked())};
    if (ms.empty())
        return false; // an empty mirror mirrors nothing
    for (auto mem : ms) {
        if (m::is_bit_field(mem) || !m::is_public(mem))
            return false;
        if (m::is_const_type(m::type_of(mem)))
            return false;
        const m::info mt{m::remove_cvref(m::type_of(mem))};
        const marshal_kind k{classify(mt)};
        if (k == marshal_kind::scalar || k == marshal_kind::enum_ ||
            k == marshal_kind::boolean)
            continue;
        if (k == marshal_kind::seq_value && is_fixed_sequence(bare(mt)) &&
            classify(sequence_element(bare(mt))) == marshal_kind::scalar)
            continue;
        if (k == marshal_kind::handle &&
            pod_mirror_eligible(mt, depth + 1))
            continue;
        return false;
    }
    return true;
}

/** The nested `public struct Data` text for eligible class @a T (append into
    the wrapper class's members): explicit layout, one field per member at its
    native offset, fixed buffers for fixed scalar arrays, nested `X.Data` for
    nested records.
    @tparam T a reflection of the (dealiased) eligible class.
    @return the struct text, at member depth. */
template <std::meta::info T>
std::string mirror_struct_text() {
    namespace m = std::meta;
    std::string out{};
    code_writer w{out, 2};
    w.line("/// <summary>A blittable value twin of the NATIVE layout (size "
           "{}): read or write whole\n"
           "        /// record buffers through Vector&lt;T&gt;.AsSpan&lt;"
           "Data&gt;() / FixedArray&lt;T&gt;.AsSpan&lt;Data&gt;() —\n"
           "        /// one interop crossing for the buffer, where the "
           "live-view indexer pays per\n"
           "        /// element. Field offsets are the native ABI's, "
           "asserted in the shim.</summary>",
           m::size_of(T));
    w.line("[StructLayout(LayoutKind.Explicit, Size = {})]", m::size_of(T));
    w.line("public unsafe struct Data");
    {
        const auto body{w.braces()};
        template for (constexpr auto mem : std::define_static_array(
                          m::nonstatic_data_members_of(
                              T, m::access_context::unchecked()))) {
            constexpr m::info mt{m::remove_cvref(m::type_of(mem))};
            constexpr std::size_t off{m::offset_of(mem).bytes};
            const std::string fname{::welder::name_of<
                mem, cs, dotnet, ::welder::ent_kind::field>()};
            constexpr marshal_kind k{classify(mt)};
            if constexpr (k == marshal_kind::scalar) {
                w.line("[FieldOffset({})] public {} {};", off,
                       scalar_spell(mt).cs, fname);
            } else if constexpr (k == marshal_kind::boolean) {
                w.line("[FieldOffset({})] public bool {};", off, fname);
            } else if constexpr (k == marshal_kind::enum_) {
                w.line("[FieldOffset({})] public {} {};", off,
                       type_ref<bare(mt)>(), fname);
            } else if constexpr (k == marshal_kind::seq_value) {
                w.line("[FieldOffset({})] public fixed {} {}[{}];", off,
                       scalar_spell(sequence_element(bare(mt))).cs, fname,
                       fixed_extent(bare(mt)));
            } else { // nested eligible record
                w.line("[FieldOffset({})] public {}.Data {};", off,
                       type_ref<bare(mt)>(), fname);
            }
        }
    }
    w.blank();
    return out;
}

/** Emit the shim-side layout contract for @a T's mirror into the current
    shard: size, trivial copyability, and every member offset — so ABI drift
    fails the native build where the cause is legible.
    @tparam T a reflection of the (dealiased) eligible class.
    @param doc    the growing document.
    @param anchor the class's `^^…` shim anchor expression. */
template <std::meta::info T>
void emit_mirror_asserts(document& doc, const std::string& anchor) {
    namespace m = std::meta;
    code_writer t{doc.current_shim(), 0};
    t.line("static_assert(std::meta::size_of({}) == {}, "
           "\"welder: Data-mirror size drift\");",
           anchor, m::size_of(T));
    t.line("static_assert(std::is_trivially_copyable_v<typename [: {} :]>, "
           "\"welder: Data-mirror on a non-trivially-copyable type\");",
           anchor);
    template for (constexpr auto mem : std::define_static_array(
                      m::nonstatic_data_members_of(
                          T, m::access_context::unchecked()))) {
        static constexpr const char* id{
            std::define_static_string(m::identifier_of(mem))};
        t.line("static_assert(std::meta::offset_of(wcs::named_field({}, "
               "\"{}\")).bytes == {}, \"welder: Data-mirror offset drift: "
               "{}\");",
               anchor, std::string_view{id}, m::offset_of(mem).bytes,
               std::string_view{id});
    }
    t.blank();
}

} // namespace welder::inline v0::rods::csharp
