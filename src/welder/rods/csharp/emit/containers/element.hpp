#pragma once
#include <meta>

#include <welder/rods/csharp/document.hpp>

/** @file
    The one declaration the container generators share: how to make sure a
    container's ELEMENT has a wrapper of its own.

    It has to be a forward declaration, because the answer depends on the whole
    family — a nested sequence's element may be a scalar sequence, a welded
    vector, a fixed array — and each of those generators would otherwise have to
    include the others. The definition lives in
    `<welder/rods/csharp/emit/containers.hpp>`, next to the dispatch it belongs
    to.
*/

namespace welder::inline v0::rods::csharp {

/** Generate the wrapper the ELEMENT of a container needs, when that element is
    itself a container (a jagged `vector<vector<T>>`, a `vector<array<T, N>>`).

    A welded element already has its own binding; a container element does not,
    so the outer wrapper's live element views would have nothing to be views
    *of*. Ordering is not a concern — every generator claims its key first, so
    the recursion terminates and each wrapper is emitted exactly once.
    @tparam E a reflection of the (bare) element type.
    @param doc the growing document. */
template <std::meta::info E>
void ensure_element_wrapper(document& doc);

/** Write `foreach` support into an open wrapper class body: `GetEnumerator()`
    plus a nested allocation-free `struct Enumerator` walking `Count` and the
    indexer. C#'s `foreach` binds to this PATTERN, not to `IEnumerable<T>`, so
    the wrappers stay free of interface machinery (and of the boxing an
    interface enumerator would cost) while `foreach (var e in v)` compiles on
    every sequence wrapper. `Count` is re-read each step, mirroring the
    live-view semantics of the indexer it drives.
    @param w       the writer, positioned inside the wrapper class body.
    @param wrapper the wrapper class name (`VectorC3Vector`).
    @param element the element's C# reference (the indexer's type). */
inline void emit_foreach_enumerator(code_writer& w, const std::string& wrapper,
                                    const std::string& element) {
    w.line("public Enumerator GetEnumerator() => new Enumerator(this);");
    w.line("/// <summary>Duck-typed foreach support (allocation-free)."
           "</summary>");
    w.line("public struct Enumerator");
    {
        const auto body{w.braces()};
        w.line("private readonly {} _c;", wrapper);
        w.line("private int _i;");
        w.line("internal Enumerator({} c) { _c = c; _i = -1; }", wrapper);
        w.line("public bool MoveNext() => ++_i < _c.Count;");
        w.line("public {} Current => _c[_i];", element);
    }
}

} // namespace welder::inline v0::rods::csharp
