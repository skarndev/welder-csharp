# welder-csharp tests. The cross-rod consistency suite binds welder's SHARED
# backend-neutral cases (tests/common/cpp — bare-welded, so any rod binds
# them), which are not part of welder's installed package: the tests therefore
# need a welder SOURCE tree — the default FetchContent provides one, and local
# development points FETCHCONTENT_SOURCE_DIR_WELDER at a checkout.
if(NOT DEFINED welder_SOURCE_DIR)
  message(FATAL_ERROR
    "welder-csharp tests need welder's SOURCES (the shared test cases). "
    "Configure with the default FetchContent, or point "
    "-DFETCHCONTENT_SOURCE_DIR_WELDER=<welder-checkout>; to consume an "
    "installed welder without tests, set -DWELDER_CSHARP_BUILD_TESTS=OFF.")
endif()
set(WELDER_TEST_COMMON_CPP ${welder_SOURCE_DIR}/tests/common/cpp)

# The consteval locks over the rod's classification / spellings / mangling /
# lookup layer: compiling the TU IS the test (static_asserts + thrown
# consteval diagnostics), same shape as welder's compile.* checks.
add_library(welder_csharp_marshal OBJECT EXCLUDE_FROM_ALL csharp_marshal.cpp)
target_link_libraries(welder_csharp_marshal PRIVATE welder::csharp)
if(TARGET welder_warnings)
  target_link_libraries(welder_csharp_marshal PRIVATE welder_warnings)
endif()
add_test(NAME compile.csharp_marshal
  COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
          --target welder_csharp_marshal --config $<CONFIG>)

add_subdirectory(csharp)
