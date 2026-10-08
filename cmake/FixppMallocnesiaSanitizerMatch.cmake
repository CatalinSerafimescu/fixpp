# cmake/FixppMallocnesiaSanitizerMatch.cmake — the predicate cmake/FixppMallocnesia.cmake
# applies to the flags it reads before registering the allocation gates (fixpp#497).
#
# A module of its own so `cmake -P` can load it: FixppMallocnesia.cmake calls
# enable_language() and add_library(), which script mode refuses.
# ci/test-mallocnesia-sanitizer-match.sh runs it against fixture flag strings and
# against mutants of it.
#
# fixpp_mallocnesia_flags_name_sanitizer(<out-var> <flags>)
#   Sets <out-var> to the first `-fsanitize=<value>` in <flags> whose value is not empty,
#   or to "" when there is none. An empty `-fsanitize=` enables no sanitizer (clang accepts
#   it, gcc rejects it), so it does not keep the gates off. The switch must start the
#   flags or follow an option boundary: whitespace, a list separator, a generator
#   expression branch separator, an IF arm separator, or a closed generator expression.
#   A switch embedded in another argument does not keep the gates off. A quoted string
#   with a space before the switch still matches, because this predicate does not parse
#   shell quoting. Whether a generator-expression branch is active is not evaluated.
function(fixpp_mallocnesia_flags_name_sanitizer out flags)
  string(REGEX MATCH "(^|[ \t;:,>])(-fsanitize=[^ ;>]+)" _match "${flags}")
  set(${out} "${CMAKE_MATCH_2}" PARENT_SCOPE)
endfunction()
