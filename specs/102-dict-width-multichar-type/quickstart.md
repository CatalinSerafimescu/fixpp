# Quickstart: validating 102

Worktree `~/Work/Programming/fixpp-parallel`; its build tree goes on F: (`/mnt/wsl/fixppbuild`, the
`build/linux-clang-*` symlinks onto `build/parallel-clang-*` there), never the root file system.

```sh
cmake --build build/linux-clang-debug -j8   # or --target each entry `ctest -N -L 102` lists
ctest --test-dir build/linux-clang-debug -L 102 --no-tests=error --output-on-failure
```

Label `102` is appended to the entries tasks.md T004 lists (the 077 round-trips among them, standalone entries
outside `-L dictionary` and `-L codegen`); compare the selection with `ctest -N -L 102`. A label
matching nothing exits 0 without `--no-tests=error`, so record each binary's executed-test count.

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
- after the post-squash rebase onto `origin/main` (tasks.md T055), the above again over the base delta only, when that
  delta is not empty.

MSVC: build the touched targets in the sandbox (`.sandbox-lock` first) before the first labelled push.
