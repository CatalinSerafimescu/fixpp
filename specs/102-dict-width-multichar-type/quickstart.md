# Quickstart: validating 102

Worktree `~/Work/Programming/fixpp-third`; its build tree goes on F: (`/mnt/wsl/fixppbuild`), never the root file system.

```sh
cmake --build build/linux-clang-debug -j8 --target dictionary_pure_tests dictionary_overlay_tests codegen_tests session_pure_tests
ctest --test-dir build/linux-clang-debug -L dictionary --output-on-failure
ctest --test-dir build/linux-clang-debug -L codegen --output-on-failure
```

Target and label names are confirmed against the test `CMakeLists.txt` at tasks; a label matching nothing exits 0, so
record each binary's executed-test count.

Expected:

- the equivalence fixture, repaired and with no wrap exclusions, GREEN on every vendored base; the name-offset oracle
  GREEN;
- the component-identity cells (both formats, the Orchestra id-domain pair, the overlay-crossing rows, the Orchestra
  overlay and IR id cells) and rows (a)–(g), each with its alias pairs, GREEN; the re-pointed side-table rows GREEN;
  the reader cell GREEN;
- the MultiCharValue decode, group-entry, reified, builder and vlatest shape cells GREEN;
- every golden and pin other than those D5 republishes byte-identical, and D5's base-vs-head differential clean with its
  planted positive reported;
- on the head, the D1, D1b (parse and holder), D2, D7 and D8 recipes show no unclassified hit, and no 16-bit
  component-identity or surrogate-id hit;
- every D4 mutant applied alone turns its named row RED, and every container-width mutant fails to compile;
- after the D9 rebase onto 100 and 101, all of the above again at the integrated head.

MSVC: build the touched targets in the sandbox (`.sandbox-lock` first) before the first labelled push.
