# welder_csharp_generate_bindings(<name>
#   SOURCES      <gen.cpp>...   # required: the generator TU(s), one using
#                               #           WELDER_CSHARP_MAIN(<ns>, <header>, <lib>)
#   LIBRARY      <libname>      # required: the P/Invoke library base name — MUST match
#                               #           the `lib` argument of WELDER_CSHARP_MAIN
#   [OUTPUT_DIR  <dir>]         # where shim.cpp + Bindings.cs land (default: binary dir)
#   [SHARDS      <N>]           # split the shim into N parallel-compiling TUs
#                               #   (shim.0.cpp … shim.N-1.cpp; default 1 = shim.cpp)
#   [INCLUDE_DIRS <dir>...]     # extra include dirs (where the welded header lives)
#   [LINK        <targets>...]  # extra link targets whose headers the shim/gen need
#   [DEPENDS     <files>...])   # extra dependencies that retrigger generation
#
# The C#/.NET analogue of welder_luacats_generate_stub / welder_sol2_add_module. C#
# has no in-process class-registration C API, so welder binds it over a C ABI driven
# by P/Invoke — which means TWO generated artifacts from one reflection pass: an
# `extern "C"` shim (compiled into a shared library) and a `[LibraryImport]` C#
# wrapper. This helper:
#
#   1. builds a generator executable from SOURCES (linked against welder::csharp),
#   2. runs it to emit  <OUTPUT_DIR>/shim.cpp  and  <OUTPUT_DIR>/Bindings.cs,
#   3. compiles the shim into a SHARED library target `<name>`, named
#      lib<LIBRARY>.{so,dylib} / <LIBRARY>.dll so the default P/Invoke resolver finds
#      it from the managed app's base directory.
#
# The generated C# wrapper path is stored in `<name>`'s WELDER_CSHARP_BINDINGS
# property; a consumer compiles it into a .NET project (see tests/csharp) with the
# shared library alongside. The shim #includes the welded header (welder
# annotations) AND re-runs the generator's reflection queries (the splice layer in
# <welder/rods/csharp/shim_support.hpp>), so it is built with the reflection flag +
# the welded types' includes — both flow in via welder::csharp and
# INCLUDE_DIRS/LINK. Module scanning is kept OFF to match the other reflection TUs
# (the gcc-16 header-unit macro-visibility issue).
#
# Windows/MinGW: the library is emitted UNPREFIXED (<LIBRARY>.dll, not
# lib<LIBRARY>.dll) so .NET's default DllImport resolver finds it, and the gcc
# runtimes are linked statically so the dll loads under a native (non-MSYS)
# dotnet without libstdc++-6.dll/libgcc on PATH.
function(welder_csharp_generate_bindings name)
  cmake_parse_arguments(CS "" "LIBRARY;OUTPUT_DIR;SHARDS;CS_FILES"
    "SOURCES;INCLUDE_DIRS;LINK;DEPENDS" ${ARGN})
  if(NOT CS_SOURCES)
    message(FATAL_ERROR "welder_csharp_generate_bindings(${name}): SOURCES is required")
  endif()
  if(NOT CS_LIBRARY)
    message(FATAL_ERROR "welder_csharp_generate_bindings(${name}): LIBRARY is required")
  endif()
  if(NOT CS_OUTPUT_DIR)
    set(CS_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR})
  endif()

  set(_shim ${CS_OUTPUT_DIR}/shim.cpp)
  set(_cs ${CS_OUTPUT_DIR}/Bindings.cs)

  # SHARDS <N> (default 1): split the shim into N translation units
  # (shim.0.cpp … shim.N-1.cpp) that compile IN PARALLEL — one reflection-heavy
  # TU is the compile-time/memory bottleneck for a large welded surface (each
  # top-level class's thunks + director land whole in one shard, so the split
  # is always link-correct; the managed side binds symbols, not TUs). The
  # count is fixed here so the generated outputs are known at configure time.
  # Pair a big N with a RAM-bounded Ninja job pool if the TUs are memory-heavy.
  if(NOT CS_SHARDS)
    set(CS_SHARDS 1)
  endif()
  if(CS_SHARDS GREATER 1)
    set(_shim_files "")
    math(EXPR _last "${CS_SHARDS} - 1")
    foreach(_i RANGE 0 ${_last})
      list(APPEND _shim_files ${CS_OUTPUT_DIR}/shim.${_i}.cpp)
    endforeach()
  else()
    set(_shim_files ${_shim})
  endif()

  # CS_FILES <N> (default 1): split the MANAGED wrapper into N files
  # (Bindings.0.cs … Bindings.N-1.cs). Unlike SHARDS this is not a build-speed
  # measure — Roslyn compiles one big file and many small ones in the same time,
  # and a C# assembly has no translation-unit boundary. It exists for the tooling
  # around the artifact: editors that refuse to open a multi-megabyte source,
  # reviewable diffs, and per-part goldens. Always safe: names resolve
  # assembly-wide, NativeMethods is partial, and any file may reopen a namespace.
  if(NOT CS_CS_FILES)
    set(CS_CS_FILES 1)
  endif()
  if(CS_CS_FILES GREATER 1)
    set(_cs_files "")
    math(EXPR _cs_last "${CS_CS_FILES} - 1")
    foreach(_i RANGE 0 ${_cs_last})
      list(APPEND _cs_files ${CS_OUTPUT_DIR}/Bindings.${_i}.cs)
    endforeach()
  else()
    set(_cs_files ${_cs})
  endif()

  # 1) the generator executable
  set(_gen ${name}_gen)
  add_executable(${_gen} ${CS_SOURCES})
  target_link_libraries(${_gen} PRIVATE welder::csharp ${CS_LINK})
  # welder's own build applies its strict warning set; a consumer using this
  # helper won't have the target, so the link is skipped for them.
  if(TARGET welder_warnings)
    target_link_libraries(${_gen} PRIVATE welder_warnings)
  endif()
  target_include_directories(${_gen} PRIVATE ${CS_INCLUDE_DIRS})
  target_compile_features(${_gen} PRIVATE cxx_std_26)
  set_target_properties(${_gen} PROPERTIES CXX_SCAN_FOR_MODULES OFF)

  # 2) run it -> shim.cpp + Bindings.cs
  add_custom_command(
    OUTPUT ${_shim_files} ${_cs_files}
    COMMAND ${_gen} ${_shim} ${_cs} ${CS_SHARDS} ${CS_CS_FILES}
    DEPENDS ${_gen} ${CS_DEPENDS}
    VERBATIM
    COMMENT "welder: generating C#/.NET bindings for ${name} \
(${CS_SHARDS} shim TU(s), ${CS_CS_FILES} managed file(s))")

  # 3) the native shared library from the generated shim (NOT welder_warnings:
  # the generated file is machine-written; wire-width conversions are implicit
  # by design there)
  add_library(${name} SHARED ${_shim_files})
  target_link_libraries(${name} PRIVATE welder::csharp ${CS_LINK})
  target_include_directories(${name} PRIVATE ${CS_INCLUDE_DIRS})
  target_compile_features(${name} PRIVATE cxx_std_26)
  set_target_properties(${name} PROPERTIES
    CXX_SCAN_FOR_MODULES OFF
    OUTPUT_NAME ${CS_LIBRARY}
    WELDER_CSHARP_BINDINGS "${_cs_files}")
  if(WIN32)
    # .NET's resolver probes <LIBRARY>.dll; MinGW would emit lib<LIBRARY>.dll.
    set_target_properties(${name} PROPERTIES PREFIX "")
    if(MINGW)
      target_link_options(${name} PRIVATE
        -static-libstdc++ -static-libgcc)
    endif()
  endif()
endfunction()

# welder_csharp_nuget_project(<name>              # a welder_csharp_generate_bindings target
#   PACKAGE_ID   <Acme.Geo>                       # required: the NuGet package id
#   [VERSION     <1.2.3>]                         # default: 0.1.0
#   [TFM         <net8.0>]                        # default: net8.0 ([LibraryImport] needs net7+)
#   [OUTPUT_DIR  <dir>])                          # default: <binary dir>/<PACKAGE_ID>
#
# The .NET distribution half: writes a PACKABLE SDK-style .csproj around the
# generated bindings, so
#
#   dotnet pack <OUTPUT_DIR>/<PACKAGE_ID>.csproj -c Release
#
# yields a standard NuGet package — the managed assembly under lib/<TFM>/ and the
# native shim library under runtimes/<rid>/native/ (the layout NuGet's runtime
# probing expects; the .NET host loads the right native library automatically).
# The RID is derived from the CMake target platform, so the package carries THIS
# build's native library; a multi-platform (fat) package is produced by packing on
# each platform and merging the runtimes/ trees — or by a CI matrix publishing
# per-RID packages (document either in your release pipeline).
#
# The csproj is written with file(GENERATE), so $<TARGET_FILE:...> resolves to the
# built shim; `dotnet pack` must run AFTER the native target is built.
function(welder_csharp_nuget_project name)
  cmake_parse_arguments(NG "" "PACKAGE_ID;VERSION;TFM;OUTPUT_DIR" "" ${ARGN})
  if(NOT NG_PACKAGE_ID)
    message(FATAL_ERROR "welder_csharp_nuget_project(${name}): PACKAGE_ID is required")
  endif()
  if(NOT NG_VERSION)
    set(NG_VERSION 0.1.0)
  endif()
  if(NOT NG_TFM)
    set(NG_TFM net8.0)
  endif()
  if(NOT NG_OUTPUT_DIR)
    set(NG_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/${NG_PACKAGE_ID})
  endif()

  # The NuGet runtime identifier of the CMake target platform.
  if(WIN32)
    set(_os win)
  elseif(APPLE)
    set(_os osx)
  else()
    set(_os linux)
  endif()
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
    set(_arch arm64)
  else()
    set(_arch x64)
  endif()
  set(_rid ${_os}-${_arch})

  # One <Compile> per generated wrapper file (CS_FILES may have split it).
  get_target_property(_bindings ${name} WELDER_CSHARP_BINDINGS)
  set(_compile_items "")
  foreach(_b IN LISTS _bindings)
    string(APPEND _compile_items "    <Compile Include=\"${_b}\" />\n")
  endforeach()
  string(STRIP "${_compile_items}" _compile_items)
  file(GENERATE OUTPUT ${NG_OUTPUT_DIR}/${NG_PACKAGE_ID}.csproj CONTENT
"<Project Sdk=\"Microsoft.NET.Sdk\">
  <PropertyGroup>
    <TargetFramework>${NG_TFM}</TargetFramework>
    <PackageId>${NG_PACKAGE_ID}</PackageId>
    <Version>${NG_VERSION}</Version>
    <Nullable>enable</Nullable>
    <AllowUnsafeBlocks>true</AllowUnsafeBlocks>
    <ImplicitUsings>disable</ImplicitUsings>
    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>
    <AppendTargetFrameworkToOutputPath>false</AppendTargetFrameworkToOutputPath>
    <!-- The rod emits an XML doc comment for every bound entity; without this
         the package ships no .xml sidecar and consumers get bare IntelliSense
         with no descriptions. CS1591 is silenced because the generated
         scaffolding (handles, wire structs) is deliberately undocumented, and
         CS0649 because a director/owner field is assigned only from native
         code, which the C# compiler cannot see. -->
    <GenerateDocumentationFile>true</GenerateDocumentationFile>
    <NoWarn>\$(NoWarn);CS1591;CS0649</NoWarn>
  </PropertyGroup>
  <ItemGroup>
    ${_compile_items}
    <None Include=\"$<TARGET_FILE:${name}>\" Pack=\"true\"
          PackagePath=\"runtimes/${_rid}/native/\"
          CopyToOutputDirectory=\"PreserveNewest\" />
  </ItemGroup>
</Project>
")
endfunction()
