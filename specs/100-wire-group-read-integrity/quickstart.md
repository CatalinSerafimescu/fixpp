# Quickstart: validating 100

Build tree: `build/linux-clang-debug` (this worktree's symlink to F:). Targeted runs only; the full matrix is
`/speckit-verify`'s.

**Which binary holds a cell.** Tests are grouped executables registered with ctest by bucket name (`[const §VII.8]`), so
a GoogleTest suite name is not a ctest name. For each touched or new test file, find its target:
`command grep -n '<file>.cpp' tests/*/CMakeLists.txt`, then the enclosing `add_executable` or helper call
(`add_threading_test(...)` etc.). The new cells' files and suites are fixed by `/speckit-tasks`.

**Grouped run (labels, as `[const §VII.8]` requires):**

```sh
cmake --build build/linux-clang-debug -j8 --target <each target found above>
ctest --test-dir build/linux-clang-debug -L wire --output-on-failure
ctest --test-dir build/linux-clang-debug -L capi --output-on-failure
ctest --test-dir build/linux-clang-debug -L session --output-on-failure
ctest --test-dir build/linux-clang-debug -L dictionary --output-on-failure   # loader section-group records, D2 sweep
ctest --test-dir build/linux-clang-debug -L alloc_guard --output-on-failure   # inbound zero-allocation gates
ctest --test-dir build/linux-clang-debug -L codegen --output-on-failure       # regenerated goldens (research census)
```

Check each label against the bucket's `LABELS` in its `CMakeLists.txt` before relying on it; a label that matches
nothing prints "No tests were found" and exits 0.

**Narrow run (per binary):**

```sh
build/linux-clang-debug/tests/<dir>/<binary> --gtest_filter='<Suite>.*:<Suite2>.<Cell>'
```

Record each binary's executed-test count in the verify record and require it to be at least the number of cells the
filter names; a filter that matches nothing runs 0 tests and passes.

Expected:
- Every cell named in research D9 GREEN; each RED with its production change reverted (mutation table in the verify
  record).
- The flipped cells listed in research.md's census assert the new endings, each naming its superseding requirement.
- The controls listed in research.md's census unchanged.
- Inbound zero-allocation gates GREEN.

ASan / UBSan on the C-ABI cells: the cursor-shell exhaustion path is where an escaping exception used to unwind
through `extern "C"`; run the C-ABI targets once in `build/linux-clang-asan`.

Lanes: the arena-exhaustion cells assert only where the arena's upstream is null; on MSVC debug they skip. The verify
record names, per cell, the lanes that showed RED and those that skipped.

MSVC: build the touched test targets on the sandbox (`.sandbox-lock` first) before the first labelled push.

## Issues to file

File each against fixpp with labels from `gh label list`, and name the issue in the verify record.

- **Dictionary layer: one count tag with two definitions at one context key** (Gate A round 5, N5-1; research D2,
  "Per-occurrence records"). The conditions:
  - In a declared message, one count tag in the body and in the trailer at the same parent path shares one key.
    `group_ctx_delim_less` leaves the delimiter out of the key. `flush_group_ctx_delims` keeps one capture after an
    unstable `std::ranges::sort` and `unique`, so which delimiter survives is unspecified. `group_ctx_`'s members for
    the key are the union of both definitions. The flush's comment, which says such captures "agree by construction",
    is false for this shape.
  - In the same issue: a declared message that reaches a count tag under two parents loses the nested count tag from
    the second parent's `group_ctx_` members.
  - In the same issue: a count tag a declared message reaches at two occurrences with different definitions has one
    `group_ctx_` key whose members are the union of both definitions, so overlapping member sets merge or mis-split
    instances.
  - In the same issue: one count tag with two definitions at one section key `(parent_path, no_tag)`. The section
    record keeps the first walked definition, so in an undeclared type the other occurrence reads `OK`/0.
  - QuickFIX C++ keeps the definitions apart (`_header_`, `_trailer_`, a group `DataDictionary` per occurrence).
  - Candidate fixes: refuse the shape at `flush_group_ctx_delims` and at the section walk, with the definition in the
    comparison so a differing delimiter or member set is a clash; or carry a section discriminator and build
    `group_ctx_` per occurrence. Recipe to reach it: a custom XML with `<group name="NoT">` in a message body and in
    `<trailer>` with different children (the `seed_div.xml` shape under the "Census scripts" heading of
    `research/reviews/opus_100-wire-group-read-integrity_gate_a_5_adversarial_review.md`, parent repo).
