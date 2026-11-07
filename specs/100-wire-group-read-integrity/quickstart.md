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
ctest --test-dir build/linux-clang-debug -L session --output-on-failure    # plus, by their own labels, the group-reading session entries with no `session` label (research census)
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

Lanes: a cell that owns a null-upstream arena, as 073's failloud tests do, asserts on every lane. Only the cells over
the library's arena upstream (`fixpp::detail::arena_upstream()`: the Q33 headroom rig, which branches on it, and the
live-engine cursor-shell port, which skips) assert their exhaustion arm only where that upstream is null; MSVC debug
forwards it to the heap (research D9, "Lanes"). The verify record names, per cell, the lanes that showed RED and those
that skipped.

MSVC: build the touched test targets on the sandbox (`.sandbox-lock` first) before the first labelled push.

## Issues to file

File each against fixpp with labels from `gh label list`, and name the issue in the verify record.

- **Dictionary layer: one count tag with two definitions at one context key** (Gate A round 5, N5-1; research D2,
  "Per-occurrence records"; disclosed by owner ruling, 2026-11-07, as plan Phase 6's L-row). Custom dictionaries only;
  shipped dictionaries reach none of these (census scripts in the parent repo's `research/G19-fix-fpml-iso20022/research/reviews/scripts_100_gate_a/`).
  The conditions: every shape research D2 lists under "What it does not fix" and "Not fixed, and not pre-existing in
  its ending", copied into the issue body from there (D2 is their one statement). Issue-only detail:
  - N5-1's key: `group_ctx_delim_less` leaves the delimiter out of the key, and `flush_group_ctx_delims` keeps one
    capture after an unstable `std::ranges::sort` and `unique`, so which delimiter survives is unspecified. The flush's
    comment, which says such captures "agree by construction", is false for this shape.
  - QuickFIX C++ keeps the definitions apart (`_header_`, `_trailer_`, a group `DataDictionary` per occurrence).
  - Candidate fixes: refuse the shape at `flush_group_ctx_delims` and at the section walk, with the definition in the
    comparison so a differing delimiter or member set is a clash; or carry a section discriminator and build
    `group_ctx_` per occurrence. Recipe to reach it: a custom XML with `<group name="NoT">` in a message body and in
    `<trailer>` with different children (the `seed_div.xml` shape under the "Census scripts" heading of
    `research/reviews/opus_100-wire-group-read-integrity_gate_a_5_adversarial_review.md`, parent repo).
- **Validator: group detection falls back to the bare store on a context miss** (spec Out of Scope; research D2,
  "Validator"). `consume_group` (`include/fixpp/wire/validator.hpp`) resolves the delimiter through
  `table_view::group_first_field(msg_type, parent_path, no_tag)`, which answers from the bare store when the context
  has no record. So a scalar-reuse tag (a tag registered as a group count elsewhere and declared a plain field here)
  holding `n > 0` makes the validator look for a delimiter, where QuickFIX `DataDictionary::getGroup(msg, field)`
  finds no group; and at the keys D2's per-occurrence records cover, strict validation can disagree with the read
  (the N5-1 issue's validator bullet is that case; this issue is the general fallback). Recipe: a custom XML in the
  shape of `kScalarReuseXml` (`tests/dictionary/loader_disposition_test.cpp`) with the reused tag holding `n > 0`,
  validated strictly. Not fixed by 100 (FR-009: the validator's acceptance is unchanged).
- **Wire: a nested sub-table is built with the default `OffsetTable::Config`, whatever the root's caps** (spec
  Clarifications and Out of Scope; research D7, "Alternative"). `build_nested_subview` builds each instance's
  sub-table with the default `Config`, so a root parsed with raised `max_offset_entries` and
  `max_group_entries_per_instance` reads a nested group under the default caps, and an instance larger than the
  default entry cap fails its sub-build (100 reports it as a failed read, `limit_exceeded`). Candidate fix: the
  sub-table inherits the root's `Config`; it changes the caps nested reads run under, and D1's budget can still fail a
  sub-build. Recipe: research D7's witness (i), a test-built `MessageView` with raised root caps over an instance
  larger than the default entry cap that carries a nested group.
