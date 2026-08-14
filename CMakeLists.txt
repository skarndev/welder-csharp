# welder-csharp — the C#/.NET rod for welder, as an out-of-tree extension.
#
# The rod is header-only over `welder::headers`: pure C++26 reflection → text
# at generation time (.NET is only needed to CONSUME the generated wrapper).
# Like welder itself, this project propagates NO compiler flags to consumers —
# the C++ standard and -freflection are the consumer's to set; welder checks
# them (WelderRequirements + the #error guard in <welder/lang.hpp>).
cmake_minimum_required(VERSION 3.28)
project(welder-csharp VERSION 0.1.0 LANGUAGES CXX)

option(WELDER_CSHARP_BUILD_TESTS
  "Build the welder-csharp test suite (needs a welder SOURCE tree for the shared cases)"
  ${PROJECT_IS_TOP_LEVEL})

# Our own build (tests/generators) uses the standard welder toolchain; set
# before welder is obtained so its requirements check sees it.
if(PROJECT_IS_TOP_LEVEL)
  set(CMAKE_CXX_STANDARD 26)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

# --- welder ------------------------------------------------------------------
# Prefer a parent/installed package; else FetchContent. For local development
# against a checkout: -DFETCHCONTENT_SOURCE_DIR_WELDER=<path-to-welder>.
if(NOT TARGET welder::headers)
  find_package(welder QUIET)
endif()
if(NOT TARGET welder::headers)
  include(FetchContent)
  FetchContent_Declare(welder
    GIT_REPOSITORY https://github.com/skarndev/welder.git
    GIT_TAG main)
  FetchContent_MakeAvailable(welder)
endif()

# --- the rod -----------------------------------------------------------------
add_library(welder_csharp INTERFACE)
add_library(welder::csharp ALIAS welder_csharp)
set_target_properties(welder_csharp PROPERTIES EXPORT_NAME csharp)
target_include_directories(welder_csharp INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(welder_csharp INTERFACE welder::headers)

# welder_csharp_generate_bindings(): generator exe → shim.cpp + Bindings.cs →
# the shim compiled into a P/Invoke-named shared library.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/WelderCSharpModule.cmake)

# --- own build: flags, warnings, tests --------------------------------------
if(PROJECT_IS_TOP_LEVEL)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-freflection) # build-tree scoped, never exported
  endif()

  # welder's curated strict set (kept -Werror-clean on gcc-16). Defined under
  # welder's own guard name so WelderCSharpModule links it into the generator
  # targets exactly as in-tree welder builds did.
  option(WELDER_CSHARP_WARNINGS "Strict warnings on our own targets" ON)
  option(WELDER_CSHARP_WERROR "Promote warnings to errors (CI)" OFF)
  if(WELDER_CSHARP_WARNINGS AND NOT TARGET welder_warnings)
    add_library(welder_warnings INTERFACE)
    target_compile_options(welder_warnings INTERFACE
      -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
      -Wcast-qual -Wold-style-cast -Wnon-virtual-dtor -Woverloaded-virtual
      -Wdouble-promotion -Wformat=2 -Wimplicit-fallthrough -Wuseless-cast
      -Wextra-semi -Wmisleading-indentation -Wredundant-decls)
    if(WELDER_CSHARP_WERROR)
      target_compile_options(welder_warnings INTERFACE -Werror)
    endif()
  endif()

  if(WELDER_CSHARP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
  endif()
endif()
