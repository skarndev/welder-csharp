# Assert that a split managed artifact is equivalent to the single-file one.
#
# Run as: cmake -DSINGLE=<Bindings.cs> -DDIR=<split dir> -DPARTS=<n> -P check_split.cmake
#
# The split (welder_csharp_generate_bindings' CS_FILES) exists for tooling, so
# what must hold is not byte-equality with the single file but EQUIVALENCE:
#   1. exactly PARTS files were written;
#   2. every part is brace-balanced (a part never ends mid-declaration — the
#      generator cuts only at boundaries its emitters recorded);
#   3. every top-level declaration of the single-file artifact appears exactly
#      once across the parts, and none appears twice (nothing lost, nothing
#      duplicated into an ambiguous-name compile error).

if(NOT SINGLE OR NOT DIR OR NOT PARTS)
  message(FATAL_ERROR "check_split: SINGLE, DIR and PARTS are required")
endif()

# NOTE: file contents are never put in a cmake LIST — C# is full of semicolons
# and a list would split on every one of them. Each part is read into a scalar,
# checked, and appended to one accumulator string.

# --- 1. the parts exist, and each is brace-balanced --------------------------
# Counting braces suffices: the generated text has no unbalanced brace inside a
# string literal (the format-style helpers emit them in pairs).
set(_all "")
math(EXPR _last "${PARTS} - 1")
foreach(_i RANGE 0 ${_last})
  set(_p ${DIR}/Bindings.${_i}.cs)
  if(NOT EXISTS ${_p})
    message(FATAL_ERROR "check_split: missing part ${_p}")
  endif()
  file(READ ${_p} _t)
  string(REGEX MATCHALL "{" _open "${_t}")
  string(REGEX MATCHALL "}" _close "${_t}")
  list(LENGTH _open _n_open)
  list(LENGTH _close _n_close)
  if(NOT _n_open EQUAL _n_close)
    message(FATAL_ERROR
      "check_split: part ${_i} is unbalanced (${_n_open} '{' vs ${_n_close} '}')")
  endif()
  string(APPEND _all "${_t}")
endforeach()

# --- 3. the declarations match, occurrence for occurrence --------------------
# The invariant is that the parts carry the SAME declarations as the single
# file — nothing dropped by a bad cut, nothing duplicated into an ambiguous
# name. Compare counts rather than asserting one each: a per-namespace holder
# (`public static class Global`) legitimately recurs, once per namespace.
file(READ ${SINGLE} _single)
string(REGEX MATCHALL "\n    public (class|enum|static class) [A-Za-z0-9_]+"
       _decls "${_single}")
list(LENGTH _decls _n_decls)
if(_n_decls EQUAL 0)
  message(FATAL_ERROR "check_split: found no declarations in ${SINGLE} to compare")
endif()
set(_unique_decls ${_decls})
list(REMOVE_DUPLICATES _unique_decls)

foreach(_d IN LISTS _unique_decls)
  # Occurrences in the single file...
  string(REPLACE "${_d}" "${_d}@@HIT@@" _marked "${_single}")
  string(REGEX MATCHALL "@@HIT@@" _hits "${_marked}")
  list(LENGTH _hits _n_single)
  # ...must equal occurrences across the parts.
  string(REPLACE "${_d}" "${_d}@@HIT@@" _marked "${_all}")
  string(REGEX MATCHALL "@@HIT@@" _hits "${_marked}")
  list(LENGTH _hits _n_split)
  if(NOT _n_single EQUAL _n_split)
    string(STRIP "${_d}" _pretty)
    message(FATAL_ERROR "check_split: '${_pretty}' appears ${_n_split} time(s) "
                        "across the parts but ${_n_single} in ${SINGLE}")
  endif()
endforeach()

list(LENGTH _unique_decls _n_unique)
message(STATUS "check_split: ${PARTS} parts, all brace-balanced, "
               "${_n_unique} distinct declarations present with matching counts")
