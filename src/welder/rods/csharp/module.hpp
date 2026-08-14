#pragma once
/** @file
    Full-automation entry point for the C#/.NET backend: the `WELDER_CSHARP_MAIN`
    generator-`main()` macro.

    Include this (instead of `rod.hpp`) in a generator TU so the whole executable is
    one macro line; `welder_csharp_generate_bindings()` (CMake) builds and runs it to
    produce `shim.cpp` + `Bindings.cs`, then compiles the shim into a shared library
    and hands the `.cs` to `dotnet`.
*/
#include <cstdlib>
#include <fstream>
#include <iostream>

#include <welder/rods/csharp/rod.hpp>

/** @def WELDER_CSHARP_MAIN
    Define a `main()` that emits the C#/.NET bindings for namespace @a ns.

    Writes the native shim to `argv[1]` and the C# wrapper to `argv[2]` (falling back
    to `shim.cpp` / `Bindings.cs`). An optional `argv[3]` is the shim's SHARD count
    (default 1): with N > 1 the shim is emitted as `<stem>.<i>.cpp` siblings of
    `argv[1]`, one independently compilable TU per shard, so a large welded surface
    builds in parallel instead of as one reflection-heavy compile (the CMake helper's
    `SHARDS` argument drives this). An optional `argv[4]` is the MANAGED file count
    (default 1): with N > 1 the wrapper is emitted as `<stem>.<i>.cs` siblings of
    `argv[2]` (the CMake helper's `CS_FILES` argument). That split is for tooling,
    not compilation — a C# assembly has no translation-unit boundary. The build-time
    analogue of a backend entry point.
    @param ns     the top-level namespace / module token.
    @param header the header the emitted shim `#include`s to see the welded types.
    @param lib    the P/Invoke library name (the shared-lib base name, e.g.
                  `"mymod_native"` → `libmymod_native.{so,dylib}` / `mymod_native.dll`). */
#define WELDER_CSHARP_MAIN(ns, header, lib)                                    \
    int main(int argc, char** argv) {                                          \
        ::welder::rods::csharp::options welder_opts_{};                        \
        welder_opts_.library = (lib);                                          \
        welder_opts_.shim_include = (header);                                  \
        if (argc > 3)                                                          \
            welder_opts_.shards =                                              \
                static_cast<::std::size_t>(::std::atoi(argv[3]));              \
        if (argc > 4)                                                          \
            welder_opts_.cs_files =                                            \
                static_cast<::std::size_t>(::std::atoi(argv[4]));              \
        ::welder::rods::csharp::rod::generate_files<^^ns>(                     \
            argc > 1 ? argv[1] : "shim.cpp",                                   \
            argc > 2 ? argv[2] : "Bindings.cs", welder_opts_);                 \
        return 0;                                                              \
    }
