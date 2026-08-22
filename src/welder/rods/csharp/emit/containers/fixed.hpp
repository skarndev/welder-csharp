#pragma once
#include <cctype>
#include <cstddef>
#include <meta>
#include <string>

#include <welder/rods/csharp/document.hpp>
#include <welder/rods/csharp/emit/containers/element.hpp>
#include <welder/rods/csharp/emit/containers/generic.hpp>
#include <welder/rods/csharp/emit/params.hpp>
#include <welder/rods/csharp/emit/refs.hpp>
#include <welder/rods/csharp/emit/returns.hpp>
#include <welder/rods/csharp/emit/spellings.hpp>
#include <welder/rods/csharp/type_map.hpp>

/** @file
    The fixed-size sibling of `<welder/rods/csharp/emit/containers/vector.hpp>`:
    a **`std::array` of a welded class**.
    @ref welder::rods::csharp::fixed_wrapper_emitter is the component.

    The same live-view element protocol, minus every size-changing operation —
    `Count` is a compile-time constant, and there is no `Add` or `Clear`.
*/

namespace welder::inline v0::rods::csharp {

/** The component that generates the fixed-size sibling of
    @ref vector_wrapper_emitter's wrapper: `std::array<welded, N>` (or of a
    nested sequence) — the vector protocol minus the size-changing ops
    (constant `Count`, live-view indexer with write-through set).

    @ref ensure derives the spellings the three artifacts share (the extent
    folds into both the C symbol stem and the wrapper name) and then writes
    them as named steps: op thunks, P/Invokes, wrapper class. */
class fixed_wrapper_emitter {
  public:
    /** Bind the emitter to the growing document.
        @param doc the two-artifact document. */
    explicit fixed_wrapper_emitter(document& doc) : _doc{&doc} {}

    /** Generate the wrapper for array specialization @a C, if this is the
        first time it is seen (keyed by the display string): the rename
        registration, the op symbols, and the three artifacts.
        @tparam C a reflection of the `std::array` specialization. */
    template <std::meta::info C>
    void ensure() {
        static constexpr const char* key{
            std::define_static_string(std::meta::display_string_of(C))};
        if (!_doc->claim_container(key))
            return;
        constexpr std::size_t n{fixed_extent(C)};
        _extent_text = std::to_string(n);
        constexpr std::meta::info El{
            std::meta::remove_cvref(sequence_element(C))};
        constexpr bool welded_elem{classify(El) == marshal_kind::handle};
        _welded_elem = welded_elem;
        ensure_element_wrapper<El>(*_doc);
        _symbol_stem = std::string{"welder_arr"} + _extent_text + "_" + symtok_v<El>;
        _template_args = "^^" + element_cpp_spelling<El>() + ", " + _extent_text;
        _element_ref = welded_elem ? type_ref<El>() : container_ref<El>();
        _element_field_ref = welded_elem ? field_ref<El>() : container_ref<El>();
        if constexpr (!welded_elem)
            _element_ops_ref = container_ops_ref<El>();
        // The wrapper TYPE is the shared generic (the extent is ops data,
        // not a type-name suffix); this instantiation contributes only its
        // ops object (see containers/generic.hpp).
        _doc->record_type_name(key, "FixedArray<" + _element_ref + ">");
        _ops_holder = "WelderOps_" + _symbol_stem.substr(7); // strip "welder_"
        _doc->record_type_name("ops:" + std::string{key},
                               _ops_holder + ".Ops");
        for (const char* leaf : {"_new", "_destroy", "_get", "_set"})
            _doc->record_symbol(_symbol_stem + leaf);
        ensure_container_scaffolding(*_doc);
        emit_thunks();
        emit_pinvokes();
        emit_ops();
    }

  private:
    /** Write the native op thunks — one-line delegations into the compiled
        `shim::arr_*` support templates, parameterized by element and extent. */
    void emit_thunks() {
        code_writer t{_doc->current_shim(), 0};
        t.line("void* {}_new(welder_error* err) { return "
               "wcs::shim::arr_new<{}>(err); }",
               _symbol_stem, _template_args);
        t.blank();
        t.line("void {}_destroy(void* self) { wcs::shim::arr_destroy<{}>"
               "(self); }",
               _symbol_stem, _template_args);
        t.blank();
        t.line("void* {}_get(void* self, std::int64_t i, welder_error* "
               "err) { return wcs::shim::arr_get<{}>(self, i, err); }",
               _symbol_stem, _template_args);
        t.blank();
        t.line("void {}_set(void* self, std::int64_t i, void* elem, "
               "welder_error* err) { wcs::shim::arr_set<{}>"
               "(self, i, elem, err); }",
               _symbol_stem, _template_args);
        t.blank();
    }

    /** Write the `[LibraryImport]` declarations for the op thunks (shared
        container handle self, abstract SafeHandle element — the vector
        emitter's contract). */
    void emit_pinvokes() {
        code_writer p{_doc->pinvoke, 2};
        p.line("[LibraryImport(Lib)] internal static partial IntPtr "
               "{}_new(out WelderError err);",
               _symbol_stem);
        p.line("[LibraryImport(Lib)] internal static partial void {}"
               "_destroy(IntPtr self);",
               _symbol_stem);
        p.line("[LibraryImport(Lib)] internal static partial IntPtr "
               "{}_get(WelderContainerHandle self, long i, out WelderError "
               "err);",
               _symbol_stem);
        p.line("[LibraryImport(Lib)] internal static partial void {}"
               "_set(WelderContainerHandle self, long i, SafeHandle elem, "
               "out WelderError err);",
               _symbol_stem);
    }

    /** Write the managed side: the instantiation's OPS HOLDER — a static
        class carrying the `FixedArrayOps<T>` object (extent included as
        data) whose delegates wrap the op P/Invokes and construct the live
        element views. No registry entry: a fixed array is only ever reached
        as a member, never constructed standalone. */
    void emit_ops() {
        code_writer w{_doc->containers, 1};
        w.line("internal static class {}", _ops_holder);
        {
            const auto cls{w.braces()};
            w.line("internal static readonly FixedArrayOps<{}> Ops = new "
                   "FixedArrayOps<{}>",
                   _element_ref, _element_ref);
            {
                const auto init{w.braces_semi()};
                w.line("Count = {},", _extent_text);
                w.line("New = () => { IntPtr _r = NativeMethods.{}_new(out "
                       "WelderError _e); WelderInterop.ThrowIfError(in _e); "
                       "return _r; },",
                       _symbol_stem);
                w.line("Destroy = NativeMethods.{}_destroy,", _symbol_stem);
                w.line("GetAt = (_h, _i) => { IntPtr _r = NativeMethods.{}"
                       "_get(_h, _i, out WelderError _e); WelderInterop."
                       "ThrowIfError(in _e); return _r; },",
                       _symbol_stem);
                if (_welded_elem) {
                    w.line("View = (_p, _o) => { var _v = new {}(_p, false); "
                           "_v._owner = _o; return _v; },",
                           _element_ref);
                    w.line("HandleOf = (_e2) => _e2._h_{},",
                           _element_field_ref);
                } else {
                    w.line("View = (_p, _o) => { var _v = new {}(_p, false, "
                           "{}); _v._owner = _o; return _v; },",
                           _element_ref, _element_ops_ref);
                    w.line("HandleOf = (_e2) => _e2._h,");
                }
                w.line("SetAt = (_h, _i, _e2) => { NativeMethods.{}_set(_h, "
                       "_i, _e2, out WelderError _e); WelderInterop."
                       "ThrowIfError(in _e); },",
                       _symbol_stem);
            }
        }
        w.blank();
    }

    document* _doc; /**< The shared document. */
    /** The extent `N`, as text. */
    std::string _extent_text{};
    /** The C symbol stem (`welder_arr<N>_<eltok>`). */
    std::string _symbol_stem{};
    /** The shim's template arguments (`^^element, N`). */
    std::string _template_args{};
    /** The ops holder class's name (`WelderOps_<stem>`). */
    std::string _ops_holder{};
    /** Whether the element is a welded class (vs a nested container). */
    bool _welded_elem{false};
    /** The element's C# reference (view type). */
    std::string _element_ref{};
    /** The element's identifier-safe (field) form (welded elements). */
    std::string _element_field_ref{};
    /** A nested-container element's ops reference (else empty). */
    std::string _element_ops_ref{};
};

/** The fixed-size sibling of @ref ensure_vector — `std::array<welded, N>` (or
    of a nested sequence)
    — the vector protocol minus the size-changing ops (constant `Count`,
    live-view indexer with write-through set). Forwards into
    @ref welder::rods::csharp::fixed_wrapper_emitter.
    @tparam C a reflection of the `std::array` specialization.
    @param doc the growing document. */
template <std::meta::info C>
void ensure_fixed(document& doc) {
    fixed_wrapper_emitter{doc}.ensure<C>();
}

} // namespace welder::inline v0::rods::csharp
