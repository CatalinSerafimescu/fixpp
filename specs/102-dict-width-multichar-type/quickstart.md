# Quickstart: validating 102

Worktree `~/Work/Programming/fixpp-third`; its build tree goes on F: (`/mnt/wsl/fixppbuild`), never the root file system.

```sh
cmake --build build/linux-clang-debug -j8 --target dictionary_pure_tests dictionary_overlay_tests codegen_tests session_pure_tests
ctest --test-dir build/linux-clang-debug -L dictionary --output-on-failure
ctest --test-dir build/linux-clang-debug -L codegen --output-on-failure
```

Target and label names are confirmed against the test `CMakeLists.txt` at tasks; a label matching nothing exits 0, so
record each binary's executed-test count.

Expected: the equivalence fixture with no wrap exclusions GREEN on every vendored base; the cap cell and control GREEN;
the MultiCharValue decode and builder cells GREEN; every golden and pin other than those D5 republishes byte-identical.
MSVC: build the touched targets in the sandbox (`.sandbox-lock` first) before the first labelled push.
