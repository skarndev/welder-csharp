# welder-csharp

The **C#/.NET rod** for [welder](https://github.com/skarndev/welder), packaged
as an out-of-tree extension: annotate C++ types with welder's vocabulary, and
this rod emits — at build time, from C++26 reflection — two coordinated
artifacts:

- **`shim.cpp`** — an `extern "C"` C-ABI shim, compiled (with reflection,
  against the same welded header) into a shared library; every thunk is a
  one-line delegation that *splices* the exact member reflection, so no C++
  type is ever respelled as text;
- **`Bindings.cs`** — a `[LibraryImport]` P/Invoke wrapper (net7+): a
  `SafeHandle` per class, properties, natural overloads, real nested types and
  namespaces, `enum : <underlying>`, XML docs, and a `WelderNativeException`
  error contract — C++ exceptions never unwind the C ABI.

Directors (C# subclasses overriding C++ virtuals), operators (including
`<=>` → the full relational set), containers (vectors/arrays/maps/smart
pointers, live reference-semantic wrappers, zero-copy `Span<T>` fields),
inheritance chains with exact upcast thunks — see the header docs under
`src/welder/rods/csharp/`.

## Language identity

welder's core does not name C#; this rod mints its language from welder's user
range: `welder::rods::csharp::cs` (default slot 0, re-point with
`WELDER_CSHARP_LANG_SLOT`). A bare `[[=welder::weld]]` already covers every
language, this one included; spell `weld(welder::rods::csharp::cs)` only to
*restrict* a type to C#.

## Use

```cmake
include(FetchContent)
FetchContent_Declare(welder_csharp
  GIT_REPOSITORY https://github.com/skarndev/welder-csharp.git GIT_TAG main)
FetchContent_MakeAvailable(welder_csharp)   # brings welder itself if absent

welder_csharp_generate_bindings(my_bindings
  SOURCES gen.cpp                  # a TU with WELDER_CSHARP_MAIN(ns, hdr, lib)
  LIBRARY my_native                # the P/Invoke library name
  INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR})
```

Requires gcc ≥ 16 with `-freflection` (welder's toolchain contract); a .NET
SDK (net7+) only to consume the generated wrapper.

**Large surfaces:** one reflection-heavy shim TU can dominate build time and
memory. `SHARDS <N>` splits the shim into `shim.0.cpp … shim.N-1.cpp` that
compile in parallel — each top-level class's thunks and director land whole in
one shard, so the split is always link-correct. Pair a big `N` with a
RAM-bounded Ninja job pool when the TUs are memory-heavy. One contract: with
N > 1 the welded header is included by several TUs, so its namespace-scope
function definitions must be `inline` (the ordinary header ODR rule; a
single-TU shim silently tolerated violations).

**Cross-platform builds: generate once.** The generator is a single
reflection-heavy TU and can dominate everything else — on a large surface it
runs to tens of minutes, serial, with nothing to overlap it — while its output
is platform-independent text. `PREGENERATED_DIR <dir>` compiles shim sources
produced by another job instead of building and running the generator, so a
release matrix generates on one platform and only compiles on the rest. Pass
the same `SHARDS`/`CS_FILES` counts as the generating run; the sources' currency
is the caller's responsibility.

`CS_FILES <N>` does the same to the managed side — `Bindings.0.cs …
Bindings.N-1.cs` — but for a different reason. It buys **no** build time:
Roslyn compiles one multi-megabyte file and many small ones in the same time
(measured on an 11 MB / 5894-type wrapper: 39 s vs 40 s). It is for the tooling
around the artifact — editors that will not open a file that large, reviewable
diffs, per-part goldens. The split is unconditionally safe: a C# assembly has
no translation-unit boundary, names resolve assembly-wide, `NativeMethods` is
`partial`, and any file may reopen a namespace. Parts are cut only at
boundaries the emitters record, so a declaration is never halved.

## Tests

`ctest` runs byte-exact goldens over a dedicated case set, consteval locks
over the marshalling layer, and — when `dotnet` is present — an xUnit
round-trip. The cross-rod consistency suite additionally binds welder's own
shared backend-neutral cases, fetched with welder's sources; point
`-DFETCHCONTENT_SOURCE_DIR_WELDER=<checkout>` at a local welder to develop
against it.

## Provenance

Extracted from welder's `feature/csharp` branch (developed there end-to-end:
goldens, native build, managed round-trip, Windows CI). Kept out of the main
welder tree as a third-party-shaped extension; the C-ABI shim layers
(`shim/`, `marshal/`, `reflect/`) deliberately contain no C#-specific
knowledge, as groundwork for future C-ABI rods (Java, Go, Swift).
