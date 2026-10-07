# cmake/FixppMallocnesiaSanitizerMatch.cmake — the predicate cmake/FixppMallocnesia.cmake
# applies to the flags it reads before registering the allocation gates (fixpp#497).
#
# A module of its own so `cmake -P` can load it: FixppMallocnesia.cmake calls
# enable_language() and add_library(), which script mode refuses.
# ci/test-mallocnesia-sanitizer-match.sh runs it against fixture flag strings and
# against mutants of it.
#
# fixpp_mallocnesia_flags_name_sanitizer(<out-var> <flags>)
#   Sets <out-var> to the first `-fsanitize=<value>` in <flags>, or to "" when there is
#   none. Not anchored to a separator, so the switch matches inside a generator
#   expression (`$<$<COMPILE_LANGUAGE:CXX>:-fsanitize=leak>`), whose `>` ends the value.
#   Whether that expression's branch is active is not evaluated: any `-fsanitize=` keeps
#   the gates off. `-fno-sanitize=…` and the `-fsanitize-<option>` family
#   (`-fsanitize-coverage=`, `-fsanitize-recover=`, …) do not contain the text
#   `-fsanitize=`, so they do not match.
function(fixpp_mallocnesia_flags_name_sanitizer out flags)
  string(REGEX MATCH "-fsanitize=[^ ;>]*" _match "${flags}")
  set(${out} "${_match}" PARENT_SCOPE)
endfunction()
