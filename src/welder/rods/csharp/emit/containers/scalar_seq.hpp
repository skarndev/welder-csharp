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
    The generated wrapper for a **scalar or enum sequence used as a live field**
    (`std::vector<int>` / `std::array<double, 3>` members).
    @ref welder::rods::csharp::scalar_seq_wrapper_emitter is the component.

    Params and returns of these types cross by value as `T[]` copies, which is
    the ergonomic choice; a non-const *field* cannot, because a snapshot would
    make `obj.Nums.Add(…)` silently mutate a temporary. So a field binds to this
    wrapper instead: `Count`, an indexer, `Add`/`Clear` (vector only), bulk
    `CopyFrom`, an implicit conversion from `T[]` so whole-property assignment
    still reads naturally — and, the point of the exercise, `AsSpan()`.

    `Span<T>` **is** C#'s buffer protocol, so `AsSpan()` over the C++ buffer is
    the .NET analogue of the Python rods' numpy view: zero-copy, and valid until
    a size-changing operation or `Dispose` — exactly a C++ iterator's rule.
*/

namespace welder::inline v0::rods::csharp {

/** The component that generates the reference-semantic wrapper for a
    SCALAR/ENUM sequence used as a live field (`std::vector<int>` /
    `std::array<double, 3>` members): Count, a bounds-checked indexer,
    `Add`/`Clear` (vector only), bulk `CopyFrom`, an implicit conversion from
    `T[]` (so whole-property assignment reads naturally), and — the zero-copy
    path — `AsSpan()` over the C++ buffer (`Span<T>` IS C#'s buffer protocol;
    valid until a size-changing operation or Dispose, exactly a C++ iterator's
    rule).

    @ref ensure derives the element spellings (C#, wire, symbol token) and the
    fixed/vector shape, then writes the three artifacts as named steps — the
    vector-only ops (`_size`/`_push`/`_clear`) branch inside each step. */
class scalar_seq_wrapper_emitter {
  public:
    /** Bind the emitter to the growing document.
        @param doc the two-artifact document. */
    explicit scalar_seq_wrapper_emitter(document& doc) : _doc{&doc} {}

    /** Generate the wrapper for sequence specialization @a C, if this is the
        first time it is seen (keyed by the display string): the rename
        registration, the op symbols, and the three artifacts.
        @tparam C a reflection of the `std::vector`/`std::array`
                  specialization (scalar or enum element). */
    template <std::meta::info C>
    void ensure() {
        static constexpr const char* key{
            std::define_static_string(std::meta::display_string_of(C))};
        if (!_doc->claim_container(key))
            return;
        constexpr std::meta::info E{
            std::meta::dealias(sequence_element(C))};
        constexpr bool fixed{is_fixed_sequence(C)};
        constexpr bool enum_elem{classify(E) == marshal_kind::enum_};
        _fixed = fixed;
        _enum_element = enum_elem;
        // the element's C# spelling / name token / wire spelling
        std::string ename{};
        if constexpr (enum_elem) {
            _element_cs = type_ref<bare(E)>();
            ename = field_ref<bare(E)>();
        } else {
            constexpr const char* c{scalar_spell(E).cs};
            _element_cs = c;
            ename = c;
            ename[0] = static_cast<char>(std::toupper(ename[0]));
        }
        static constexpr const char* tok{std::define_static_string(
            map_token(E))};
        static constexpr const char* wire{std::define_static_string(
            enum_elem ? std::string{enum_wire_spell(E).c_abi}
                      : std::string{scalar_spell(E).c_abi})};
        static constexpr const char* wire_cs{std::define_static_string(
            enum_elem ? std::string{enum_wire_spell(E).cs}
                      : std::string{scalar_spell(E).cs})};
        static constexpr const char* ecpp{
            std::define_static_string(leaf_cpp_spelling(E))};
        _element_wire = wire;
        _element_wire_cs = wire_cs;
        if constexpr (fixed)
            _extent_text = std::to_string(fixed_extent(C));
        _symbol_stem = fixed ? "welder_arrs_" + std::string{tok} + "_" + _extent_text
                     : "welder_vecs_" + std::string{tok};
        _template_args = fixed ? "^^" + std::string{ecpp} + ", " + _extent_text
                       : "^^" + std::string{ecpp};
        // The wrapper TYPE is one of the two generics; this instantiation
        // contributes only its ops object (see containers/generic.hpp).
        _doc->record_type_name(key, (fixed ? "FixedArray<" : "Vector<") +
                                        _element_cs + ">");
        _ops_holder = "WelderOps_" + _symbol_stem.substr(7); // strip "welder_"
        _doc->record_type_name("ops:" + std::string{key},
                               _ops_holder + ".Ops");
        _family_prefix = fixed ? "arr" : "vec";
        ensure_container_scaffolding(*_doc);
        emit_thunks();
        emit_pinvokes();
        emit_ops();
    }

  private:
    /** Write the native op thunks (registering their symbols in step): the
        shared `_new`/`_destroy`/`_data`/`_fill` quartet, then — vector only —
        `_size`/`_push`/`_clear`. */
    void emit_thunks() {
        for (const char* leaf : {"_new", "_destroy", "_data", "_fill"})
            _doc->record_symbol(_symbol_stem + leaf);
        code_writer t{_doc->current_shim(), 0};
        t.line("void* {}_new(welder_error* err) { return wcs::shim::{}"
               "_new<{}>(err); }",
               _symbol_stem, _family_prefix, _template_args);
        t.blank();
        t.line("void {}_destroy(void* self) { wcs::shim::{}_destroy<{}>"
               "(self); }",
               _symbol_stem, _family_prefix, _template_args);
        t.blank();
        t.line("void* {}_data(void* self, welder_error* err) { return "
               "wcs::shim::{}_data<{}>(self, err); }",
               _symbol_stem, _family_prefix, _template_args);
        t.blank();
        t.line("void {}_fill(void* self, const void* data, std::int64_t "
               "len, welder_error* err) { wcs::shim::{}_fill<{}>"
               "(self, data, len, err); }",
               _symbol_stem, _family_prefix, _template_args);
        t.blank();
        if (!_fixed) {
            for (const char* leaf : {"_size", "_push", "_clear"})
                _doc->record_symbol(_symbol_stem + leaf);
            t.line("std::int64_t {}_size(void* self, welder_error* err) "
                   "{ return wcs::shim::vec_size<{}>(self, err); }",
                   _symbol_stem, _template_args);
            t.blank();
            t.line("void {}_push(void* self, {} v, welder_error* err) { "
                   "wcs::shim::vec_push<{}>(self, v, err); }",
                   _symbol_stem, _element_wire, _template_args);
            t.blank();
            t.line("void {}_clear(void* self, welder_error* err) { "
                   "wcs::shim::vec_clear<{}>(self, err); }",
                   _symbol_stem, _template_args);
            t.blank();
        }
    }

    /** Write the `[LibraryImport]` declarations for the op thunks (the
        vector-only trio branches like the thunks). Every self parameter is
        the ONE shared container handle class. */
    void emit_pinvokes() {
        code_writer p{_doc->pinvoke, 2};
        p.line("[LibraryImport(Lib)] internal static partial IntPtr "
               "{}_new(out WelderError err);",
               _symbol_stem);
        p.line("[LibraryImport(Lib)] internal static partial void {}"
               "_destroy(IntPtr self);",
               _symbol_stem);
        p.line("[LibraryImport(Lib)] internal static partial IntPtr "
               "{}_data(WelderContainerHandle self, out WelderError err);",
               _symbol_stem);
        p.line("[LibraryImport(Lib)] internal static partial void {}"
               "_fill(WelderContainerHandle self, IntPtr data, long len, out "
               "WelderError err);",
               _symbol_stem);
        if (!_fixed) {
            p.line("[LibraryImport(Lib)] internal static partial long "
                   "{}_size(WelderContainerHandle self, out WelderError err);",
                   _symbol_stem);
            p.line("[LibraryImport(Lib)] internal static partial void "
                   "{}_push(WelderContainerHandle self, {} v, out WelderError "
                   "err);",
                   _symbol_stem, _element_wire_cs);
            p.line("[LibraryImport(Lib)] internal static partial void "
                   "{}_clear(WelderContainerHandle self, out WelderError err);",
                   _symbol_stem);
        }
    }

    /** Write the managed side: the instantiation's OPS HOLDER — a static
        class carrying the `VectorOps<T>`/`FixedArrayOps<T>` object whose
        delegates wrap the op P/Invokes (the enum-element push casts inside
        its lambda), plus — vector only — the `[ModuleInitializer]` that
        registers the ops so `new Vector<T>()` and the implicit `T[]`
        conversion resolve them. No wrapper class: the TYPE is the shared
        generic. */
    void emit_ops() {
        code_writer w{_doc->containers, 1};
        w.line("internal static class {}", _ops_holder);
        {
            const auto cls{w.braces()};
            const char* ops_t{_fixed ? "FixedArrayOps" : "VectorOps"};
            w.line("internal static readonly {}<{}> Ops = new {}<{}>", ops_t,
                   _element_cs, ops_t, _element_cs);
            {
                const auto init{w.braces_semi()};
                if (_fixed)
                    w.line("Count = {},", _extent_text);
                w.line("New = () => { IntPtr _r = NativeMethods.{}_new(out "
                       "WelderError _e); WelderInterop.ThrowIfError(in _e); "
                       "return _r; },",
                       _symbol_stem);
                w.line("Destroy = NativeMethods.{}_destroy,", _symbol_stem);
                if (!_fixed)
                    w.line("Size = (_h) => { var _r = NativeMethods.{}_size("
                           "_h, out WelderError _e); WelderInterop."
                           "ThrowIfError(in _e); return _r; },",
                           _symbol_stem);
                w.line("Data = (_h) => { IntPtr _r = NativeMethods.{}_data("
                       "_h, out WelderError _e); WelderInterop.ThrowIfError("
                       "in _e); return _r; },",
                       _symbol_stem);
                if (!_fixed)
                    w.line("Push = (_h, _v) => { NativeMethods.{}_push(_h, "
                           "{}, out WelderError _e); WelderInterop."
                           "ThrowIfError(in _e); },",
                           _symbol_stem,
                           _enum_element
                               ? "(" + _element_wire_cs + ")_v"
                               : std::string{"_v"});
                w.line("Fill = (_h, _d, _n) => { NativeMethods.{}_fill(_h, "
                       "_d, _n, out WelderError _e); WelderInterop."
                       "ThrowIfError(in _e); },",
                       _symbol_stem);
                if (!_fixed)
                    w.line("Clear = (_h) => { NativeMethods.{}_clear(_h, out "
                           "WelderError _e); WelderInterop.ThrowIfError(in "
                           "_e); },",
                           _symbol_stem);
            }
            w.line("[ModuleInitializer]");
            w.line("internal static void Register() => WelderContainers."
                   "Register{}(Ops);",
                   _fixed ? "FixedArray" : "Vector");
        }
        w.blank();
    }

    document* _doc;     /**< The shared document. */
    bool _fixed{false}; /**< `std::array` (fixed) vs `std::vector`. */
    /** Whether the element is an enum (push casts). */
    bool _enum_element{false};
    /** The element's C# spelling. */
    std::string _element_cs{};
    /** Fixed form: the extent `N`, as text. */
    std::string _extent_text{};
    /** The C symbol stem (`welder_vecs_…`/`welder_arrs_…`). */
    std::string _symbol_stem{};
    /** The shim's template arguments. */
    std::string _template_args{};
    /** The ops holder class's name (`WelderOps_<stem>`). */
    std::string _ops_holder{};
    /** The element's C-ABI wire spelling. */
    std::string _element_wire{};
    /** The element's C# wire spelling. */
    std::string _element_wire_cs{};
    /** The shim family prefix (`vec`/`arr`). */
    std::string _family_prefix{};
};

/** The reference-semantic wrapper for a SCALAR/ENUM sequence used as a
    live field (`std::vector<int>` / `std::array<double, 3>` members):
    Count, a bounds-checked indexer, `Add`/`Clear` (vector only), bulk
    `CopyFrom`, an implicit conversion from `T[]` (so whole-property
    assignment reads naturally), and — the zero-copy path — `AsSpan()`
    over the C++ buffer (`Span<T>` IS C#'s buffer protocol; valid until a
    size-changing operation or Dispose, exactly a C++ iterator's rule).
    Forwards into @ref welder::rods::csharp::scalar_seq_wrapper_emitter.
    @tparam C a reflection of the sequence specialization.
    @param doc the growing document. */
template <std::meta::info C>
void ensure_scalar_seq(document& doc) {
    scalar_seq_wrapper_emitter{doc}.ensure<C>();
}

} // namespace welder::inline v0::rods::csharp
