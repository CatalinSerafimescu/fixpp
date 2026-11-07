# Quickstart: validating 101

Main checkout, build tree `build/linux-clang-debug`. Targeted runs; the matrix is `/speckit-verify`'s.

```sh
# the binaries tasks.md names, e.g. session_pure_tests wire_dict_tests capi_pure_tests and the error_0NN_completeness
# executables; then the cells by ctest name
cmake --build build/linux-clang-debug -j8 --target session_pure_tests wire_dict_tests capi_pure_tests
ctest --test-dir build/linux-clang-debug --output-on-failure -R '<the ctest names tasks.md lists>'
```

Binary and suite names of the new cells are fixed in `tasks.md`. A filtered run is not the census: research D9's
mutate-and-run recipes rebuild every test target and run `ctest` **unfiltered**, after a `strings` check that the
binaries are fresh, and are re-run after the last fix commit.

Expected: the new cells GREEN, each RED on `fa778a8e` where today's behaviour differs and RED with its production change
reverted; the manifest's cells rewritten with their superseding requirement; `ctest -L alloc_guard` GREEN;
`git grep -n kMaxShippedMsgTypeLength -- src tests` empty.

Benchmarks (research D8, `[const §VIII.2]`): compute the base as `git merge-base HEAD origin/main` at measurement time;
build `scan_frame_header_bench`, `on_inbound_frame_bench` and `validator_bench` at the base in a worktree whose path has
the same length as the candidate's; run A-B-A-B on the same machine, four observations per tree; compare the
**minimum** per tree; a slowdown above +5% fails. Record the merge-base, every observation, the minima and the ratios.
`validator_bench`'s CI pairing is supplementary evidence only. Add the defined-tag store's bytes to
`table_view_footprint_bench` (FIX44, a FIXT route, FIX Latest, a high-tag overlay).

MSVC: build the touched test targets in the sandbox (`.sandbox-lock` first) before the first labelled push.
