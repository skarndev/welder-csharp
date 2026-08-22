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
    The generated wrapper for a **`std::vector` of a welded class**: welder's
    opaque-container model, managed-side.
    @ref welder::rods::csharp::vector_wrapper_emitter is the component.

    The wrapper holds a handle to the C++ vector, and its indexer hands out a
    **live view** of the element — pinned to the wrapper, so the vector cannot be
    collected while a view of one of its elements is alive. That is what makes
    `v[i].Field = x` write through, exactly as `bind_vector` does for the Python
    rods, instead of mutating a copy. Writes go the other way: `Add` and the
    indexer's setter copy-assign from a borrowed handle.
*/

namespace welder::inline v0::rods::csharp {

/** The component that generates the reference-semantic wrapper for a
    `std::vector<welded>` — or for a vector whose element is ITSELF a
    sequence — once per distinct instantiation.

    @ref ensure derives the spellings the three artifacts share (the C symbol
    stem, the shim's template argument, the wrapper/element names) and then
    writes them as named steps: the native op thunks (delegating into
    `shim::vec_*`), their P/Invokes, and the C# wrapper class (live element
    views pinned to the vector wrapper — welder's opaque-container model). */
class vector_wrapper_emitter {
  public:
    /** Bind the emitter to the growing document.
        @param doc the two-artifact document. */
    explicit vector_wrapper_emitter(document& doc) : _doc{&doc} {}

    /** Generate the wrapper for vector specialization @a C, if this is the
        first time it is seen (keyed by the display string): the rename
        registration, the op symbols, and the three artifacts.
        @tparam C a reflection of the vector specialization. */
    template <std::meta::info C>
    void ensure() {
        static constexpr const char* key{
            std::define_static_string(std::meta::display_string_of(C))};
        if (!_doc->claim_container(key))
            return;
        constexpr std::meta::info El{
            std::meta::remove_cvref(sequence_element(C))};
        // A container element is referred to by its own generic wrapper
        // type, a welded one by its type reference.
        constexpr bool welded_elem{classify(El) == marshal_kind::handle};
        _welded_elem = welded_elem;
        ensure_element_wrapper<El>(*_doc);
        _symbol_stem = std::string{"welder_vec_"} + symtok_v<El>;
        _element_anchor = "^^" + element_cpp_spelling<El>();
        _element_ref = welded_elem ? type_ref<El>() : container_ref<El>();
        _element_field_ref = welded_elem ? field_ref<El>() : container_ref<El>();
        if constexpr (!welded_elem)
            _element_ops_ref = container_ops_ref<El>();
        // The wrapper TYPE is the shared generic; this instantiation
        // contributes only its ops object (see containers/generic.hpp).
        _doc->record_type_name(key, "Vector<" + _element_ref + ">");
        _ops_holder = "WelderOps_" + _symbol_stem.substr(7); // strip "welder_"
        _doc->record_type_name("ops:" + std::string{key},
                               _ops_holder + ".Ops");
        for (const char* leaf : {"_new", "_destroy", "_size", "_get", "_set",
                                 "_add", "_clear"})
            _doc->record_symbol(_symbol_stem + leaf);
        ensure_container_scaffolding(*_doc);
        emit_thunks();
        emit_pinvokes();
        emit_ops();
    }

  private:
    /** Write the native op thunks — one-line delegations into the compiled
        `shim::vec_*` support templates, parameterized by the element type. */
    void emit_thunks() {
        code_writer t{_doc->current_shim(), 0};
        t.line("void* {}_new(welder_error* err) { return "
               "wcs::shim::vec_new<{}>(err); }",
               _symbol_stem, _element_anchor);
        t.blank();
        t.line("void {}_destroy(void* self) { wcs::shim::vec_destroy<{}>"
               "(self); }",
               _symbol_stem, _element_anchor);
        t.blank();
        t.line("std::int64_t {}_size(void* self, welder_error* err) { "
               "return wcs::shim::vec_size<{}>(self, err); }",
               _symbol_stem, _element_anchor);
        t.blank();
        t.line("void* {}_get(void* self, std::int64_t i, welder_error* "
               "err) { return wcs::shim::vec_get<{}>(self, i, err); }",
               _symbol_stem, _element_anchor);
        t.blank();
        t.line("void {}_set(void* self, std::int64_t i, void* elem, "
               "welder_error* err) { wcs::shim::vec_set<{}>"
               "(self, i, elem, err); }",
               _symbol_stem, _element_anchor);
        t.blank();
        t.line("void {}_add(void* self, void* elem, welder_error* err) { "
               "wcs::shim::vec_add<{}>(self, elem, err); }",
               _symbol_stem, _element_anchor);
        t.blank();
        t.line("void {}_clear(void* self, welder_error* err) { "
               "wcs::shim::vec_clear<{}>(self, err); }",
               _symbol_stem, _element_anchor);
        t.blank();
    }

    /** Write the `[LibraryImport]` declarations for the op thunks. The self
        parameter is the shared container handle class; the element crosses
        as the ABSTRACT SafeHandle (the erased-fields precedent), so the ops
        delegates need no per-element handle casts. */
    void emit_pinvokes() {
        code_writer p{_doc->pinvoke, 2};
        p.line("[LibraryImport(Lib)] internal static partial IntPtr "
               "{}_new(out WelderError err);",
               _symbol_stem);
        p.line("[LibraryImport(Lib)] internal static partial void {}"
               "_destroy(IntPtr self);",
               _symbol_stem);
        p.line("[LibraryImport(Lib)] internal static partial long {}"
               "_size(WelderContainerHandle self, out WelderError err);",
               _symbol_stem);
        p.line("[LibraryImport(Lib)] internal static partial IntPtr "
               "{}_get(WelderContainerHandle self, long i, out WelderError "
               "err);",
               _symbol_stem);
        p.line("[LibraryImport(Lib)] internal static partial void {}"
               "_set(WelderContainerHandle self, long i, SafeHandle elem, "
               "out WelderError err);",
               _symbol_stem);
        p.line("[LibraryImport(Lib)] internal static partial void {}"
               "_add(WelderContainerHandle self, SafeHandle elem, out "
               "WelderError err);",
               _symbol_stem);
        p.line("[LibraryImport(Lib)] internal static partial void {}"
               "_clear(WelderContainerHandle self, out WelderError err);",
               _symbol_stem);
    }

    /** Write the managed side: the instantiation's OPS HOLDER — a static
        class carrying the `VectorOps<T>` object whose delegates wrap the op
        P/Invokes and construct the live element views (a welded element's
        view is its wrapper class; a nested container's is the inner generic
        with ITS ops), plus the `[ModuleInitializer]` registering the ops so
        `new Vector<T>()` resolves them. No wrapper class: the TYPE is the
        shared generic. */
    void emit_ops() {
        code_writer w{_doc->containers, 1};
        w.line("internal static class {}", _ops_holder);
        {
            const auto cls{w.braces()};
            w.line("internal static readonly VectorOps<{}> Ops = new "
                   "VectorOps<{}>",
                   _element_ref, _element_ref);
            {
                const auto init{w.braces_semi()};
                w.line("New = () => { IntPtr _r = NativeMethods.{}_new(out "
                       "WelderError _e); WelderInterop.ThrowIfError(in _e); "
                       "return _r; },",
                       _symbol_stem);
                w.line("Destroy = NativeMethods.{}_destroy,", _symbol_stem);
                w.line("Size = (_h) => { var _r = NativeMethods.{}_size(_h, "
                       "out WelderError _e); WelderInterop.ThrowIfError(in "
                       "_e); return _r; },",
                       _symbol_stem);
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
                w.line("Add = (_h, _e2) => { NativeMethods.{}_add(_h, _e2, "
                       "out WelderError _e); WelderInterop.ThrowIfError(in "
                       "_e); },",
                       _symbol_stem);
                w.line("Clear = (_h) => { NativeMethods.{}_clear(_h, out "
                       "WelderError _e); WelderInterop.ThrowIfError(in _e); "
                       "},",
                       _symbol_stem);
            }
            w.line("[ModuleInitializer]");
            w.line("internal static void Register() => "
                   "WelderContainers.RegisterVector(Ops);");
        }
        w.blank();
    }

    document* _doc; /**< The shared document. */
    /** The C symbol stem (`welder_vec_<eltok>`). */
    std::string _symbol_stem{};
    /** The shim's template argument (`^^element`). */
    std::string _element_anchor{};
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

/** Generate the reference-semantic wrapper for `std::vector<welded>` — or for
    a vector whose element is ITSELF a sequence — container type @a C (once per
    distinct instantiation): the native op thunks (delegating into
    `shim::vec_*`), their P/Invokes, the rename registration and the C# wrapper
    class (live element views pinned to the vector wrapper — welder's
    opaque-container model). Forwards into
    @ref welder::rods::csharp::vector_wrapper_emitter.
    @tparam C a reflection of the vector specialization.
    @param doc the growing document. */
template <std::meta::info C>
void ensure_vector(document& doc) {
    vector_wrapper_emitter{doc}.ensure<C>();
}

} // namespace welder::inline v0::rods::csharp
