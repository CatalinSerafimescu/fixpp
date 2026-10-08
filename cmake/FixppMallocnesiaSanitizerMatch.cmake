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
#   it, gcc rejects it), so it does not keep the gates off. The switch counts when it starts
#   the flags or follows whitespace, `;`, `:`, `,` or `>`: where a switch starts in a flag
#   string, a CMake list, a `SHELL:` option and a generator expression's branches. The
#   match is lexical and parses neither arguments nor shell quoting, so a switch after
#   `:` or `,` inside another argument, or after a space inside a quoted value, still
#   matches, and a switch right after a quote does not. Whether a generator-expression
#   branch is active is not evaluated. ci/test-mallocnesia-sanitizer-match.sh pins each edge.
function(fixpp_mallocnesia_flags_name_sanitizer out flags)
  string(REGEX MATCH "(^|[ \t;:,>])(-fsanitize=[^ ;>]+)" _match "${flags}")
  set(${out} "${CMAKE_MATCH_2}" PARENT_SCOPE)
endfunction()
