# Implementation Plan: Dialect overlay — venue additions layered on a base dictionary (D-009)

**Branch**: `098-dialect-overlay` | **Date**: 2026-10-31 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/098-dialect-overlay/spec.md`

## Summary

A user loads a base dictionary with one overlay and gets an ordinary `dict::Dictionary`, which they put in
`SessionConfig::dictionary`. The overlay is a fragment in the base's own format (QuickFIX XML or FIX
Orchestra) holding only additions.

- **Mechanism (research R-1).** Each loader parses the base and the overlay into pugixml documents,
  checks the overlay (conflicts R-4, caps and DOCTYPE R-5), splices it into the base document by
  "an edit lands on the definition it names" (R-3), checks the merged reference graph for cycles, and
  then runs its existing `build_handle_from_doc` unchanged. The spliced document *is* the hand-edited
  file, so the result equals the hand-edited load by construction, store for store.
- **Two formats (R-2).** The Orchestra overlay is an Orchestra fragment, because translating a
  QuickFIX-XML fragment onto Orchestra's id-referenced, shared-definition model is a second design of
  its own (measured reasons in R-2). Disclosed: Orchestra code sets and groups are shared, so an edit
  reaches every user (B-098-2).
- **Header on send (R-7, FR-022).** The dictionary gains its declared header set; the session's send
  partition treats a tag as header-class when it is in the fixed 34-tag set **or** the dictionary's
  header. Measured: for all ten vendored dictionaries the union equals the fixed set, so no existing
  session's frames change.
- **Surfaces.** C++ (`XmlLoader` / `OrchestraLoader::load_with_overlay[_from_string]`,
  `dict::load_any_with_overlay`); C ABI 1.13 (`fixpp_dict_load_from_xml_with_overlay`, the first
  producer of `FIXPP_ERR_DICT_LIMIT_EXCEEDED`); Python through SWIG; TOML `dialect_overlay` in
  `[default]` and `[[session]]`, with every TOML session now arriving with `config.dictionary` set
  (R-9).
- **Removed.** The never-read `SessionConfig::dialect_overlay` and the `DialectOverlay` forward
  declaration (pre-release breaking, public C++ API).
- **Records.** D-009 and COM-011 close; L-002-2, L-422-1 and the stale L-004-1 retire; 2c-codegen's
  overlay sections are annotated in place (data-model §D–§F). No constitution amendment (R-14).

## Technical Context

**Language/Version**: C++23 (library, tests); C (ABI header); Python 3 (bindings test); TOML fixtures
**Primary Dependencies**: pugixml 1.15 (already linked by `fixpp_dictionary`), GoogleTest, SWIG ≥ 4.5.1,
`fixpp_dictionary`, `fixpp_session`, `fixpp_config`, `fixpp_capi`
**Storage**: N/A. Inputs: the vendored `dictionaries/*.xml`, `dictionaries/orchestra/OrchestraFIXLatest.xml`,
and new fixtures under `tests/dictionary/fixtures/098/` and `tests/config/fixtures/098/`
**Testing**: existing gtest binaries (dictionary, session, config, capi), label `098`; one new libFuzzer
harness; one Python test
**Target Platform**: every CI platform (Linux clang/gcc/libc++, MSVC). No platform-specific code
**Project Type**: library — production change in `dict`, `session` (one send-path line), `config`,
`capi`; plus tests and records
**Performance Goals**: load with overlay ≤ 1.5 × base load (SC-005); the send-path lookup adds one
bounds check and bit test after a fixed-set miss, no allocation (`[const §XV.1]`), paired bench at
verify (`[const §VIII.2]`)
**Constraints**: the orchestrator writes no library code — `phase-implementer` writes every source,
test and CMake line. Existing `load` / `load_from_string` / `load_any` behaviour unchanged. The validator
and parser are not changed. Mutants never run in the PR's tree (contracts preamble)
**Scale/Scope**: the files under "Files changed"

## Constitution Check

| Clause | Status |
|---|---|
| `[const §I.1]` | Unchanged: no version is added; the overlay applies to the versions already loaded. |
| `[const §I.3]` | D-009 and COM-011 move to `done` with their witnesses (data-model §E). |
| `[const §VI.4]`, `§VI.5` | D-009 gains a coverage-index supplemental note in D-007's `[impl]` form; spec Normative References use `[DocAbbrev §X] Title`. |
| `[const §VII.3]`, `§VII.4` | TDD: every witness RED (unfixed tree or named mutant) before GREEN; the **U** arms run before any production edit (phase 1). |
| `[const §VII.7]` | Applies: the overlay loader parses untrusted XML. New harness `tests/fuzz/fuzz_dict_overlay.cpp` with seeds; CI replays them on `linux-clang-asan`; the ten-minute run is verify's fuzz step (research R-11). |
| `[const §VII.8]` | No new gtest executable: witnesses join the existing dictionary, session, config and capi binaries, selected by `ctest -L 098`. The fuzz target is outside §VII.8's scope (built only under `FIXPP_BUILD_FUZZ`). |
| `[const §VIII.2]` | The send-path change is measured by the paired bench at verify (W-12). |
| `[const §IX.1]` | Touched modules ≥ 95/85 or a per-line assessment, at verify. |
| `[const §IX.4]` | clang-tidy / clang-format / cppcheck on changed files, at verify. |
| `[const §X.1]`, `§X.7` | C ABI additive: MINOR 1.12 → 1.13, one new function, no change to an existing signature or code meaning (research R-12). |
| `[const §XV.1]` | The send-path lookup allocates nothing; the merge allocates only at load time (construction-time, `[arch §5.3]`). |
| `[const §XV.13]` | Delivered: the runtime XML path for custom content (D-007 + D-009). Text unchanged. |
| `[const §XVI.3]` | Complied: `/speckit-clarify` ran before this plan (spec Clarifications, `/speckit-clarify` session). |
| `[const §XVI.4]` | `/speckit-analyze` after `/speckit-tasks`, with an explicit plan↔spec drift pass over every Clarifications answer, including the `/speckit-plan` supersession of the clarify-time Orchestra-format reading. |
| `[const §XVI.6]` | Mutants in a third worktree on `/mnt/wsl/fixppbuild`, detached (contracts preamble). |
| `[const §XVII.1]` | Gate A runs: public C++ API change (new members, a removed field), C-ABI addition, config surface. |
| `[const §XVII.7]` | Local full build + ctest of `linux-clang-debug` at the final head. |
| `[const §XVII.8]` | `/speckit-verify` before Gate B. |
| `[const §XX]` | No amendment (research R-14); fixpp#642 goes to a docs PR. |

No deviation. Re-checked after Phase 1 design: unchanged.

## Project Structure

### Documentation (this feature)

```text
specs/098-dialect-overlay/
├── spec.md
├── plan.md              # this file
├── research.md          # R-1..R-15
├── data-model.md        # entities, stores, records delta
├── quickstart.md        # run order
├── contracts/
│   ├── surface.md       # C++ / C ABI / TOML shapes
│   └── witnesses.md     # W-0..W-13 and their RED arms
├── checklists/requirements.md
└── tasks.md             # /speckit-tasks
```

### Files changed

| Area | Files |
|---|---|
| Dictionary API | `include/fixpp/dict/{xml_loader,orchestra_loader,load_any,error,table_view,dictionary,field_ref}.hpp` |
| Dictionary impl | `src/dictionary/{xml_loader,orchestra_loader,load_any,dictionary}.cpp`, `src/dictionary/dictionary_internal.hpp`; a new `src/dictionary/overlay_merge.{hpp,cpp}` for the guards, the conflict checks and the two splice walks (shared helpers: bounded read, depth walk, cycle walk) |
| Session | `src/session/session.cpp` (the send classification line); `include/fixpp/session/session_config.hpp` (field and forward declaration removed) |
| Config | `src/config/toml_config_loader.cpp` (scope-aware `dialect_overlay`), `src/config/selector_resolver.cpp` (per-session dictionary step), `src/config/loader_internal.hpp` (doc) |
| C ABI | `include/fix/c_api/dict.h`, `include/fix/c_api/version.h`, `src/capi/dictionary.cpp`, `tests/abi/golden/fixpp_capi_symbols.txt`, `tools/capi_freeze.sha256` |
| Python | `bindings/python/fixpp.i` (GIL audit table), `bindings/python/tests/test_098_dialect_overlay.py`, `bindings/python/tests/wheel/test_import_surface.py` |
| Tests | `tests/dictionary/{dictionary_equivalence.hpp,dictionary_equivalence_test.cpp,overlay_equivalence_test.cpp,overlay_refusal_test.cpp,header_tags_test.cpp}`, `tests/dictionary/fixtures/098/**`, `tests/session/{test_098_dialect_overlay_inbound,test_098_header_send}.cpp`, `tests/config/test_098_dialect_overlay.cpp`, `tests/config/fixtures/098/**`, the rewritten T018/T020/T028 cells and their fixtures, `tests/capi/{dict_overlay_capi_test,version_test}.cpp`, `tests/session/test_resend_answer_field_order.cpp` (stale comment only), `tests/session/engine_establishment_timeout_test.cpp` (relies on the filled bundle), the CMakeLists of each binary |
| Fuzz | `tests/fuzz/fuzz_dict_overlay.cpp`, `tests/fuzz/corpus/dict_overlay/`, `tests/fuzz/CMakeLists.txt` |
| Records | `spec/{feature-catalogue,coverage-index,behaviors-and-limitations,behaviors-and-limitations-closed}.md`, `.specify/2c-codegen.md`, `brain/components/dictionary.md`, `CLAUDE.md` / `CLAUDE-history.md` at close-out |

## Phases

1. **Unmodified-code witnesses first** (quickstart §1): the **U** arms of W-5, W-6, W-8, W-10, and W-7's
   raw header walk with its union assertion. Build and run in `build/linux-clang-debug`; that run is the
   RED record. **Stop rule:** a **U** arm that passes on the unfixed tree means its premise is false;
   stop and decide, never adjust the test.
2. **Comparator** (test-only): `dictionary_equivalence.hpp` and W-0, every seeded row RED, the identical
   pair clean. Nothing reads W-1/W-2 before this is green.
3. **QuickFIX-XML overlay**: errors, the overlay guards (R-5), the conflict checks (R-4), the XML splice
   (R-3), the merged-graph cycle check; `XmlLoader::load_with_overlay[_from_string]`. W-1 (qf), W-2
   (FIX44, FIX50SP2), W-3 (XML rows). Mutants M-0..M-7. Full suite.
4. **Orchestra overlay**: the Orchestra splice and checks; `OrchestraLoader::load_with_overlay[_from_string]`;
   `dict::load_any_with_overlay`. W-1 (orch), W-2 (Orchestra), W-3 (Orchestra rows), W-4. Mutant M-8.
   **Stop rule:** if the Orchestra W-2 row shows a difference that comes from the loader's own
   order-dependent rules rather than the splice, record it and decide before continuing.
5. **Header on send**: handle member, `table_view::header_bits_` + `is_header_tag`, builder forwarder,
   `as_table_view` fill, the send classification. W-6, W-7. Mutant M-9. Full suite: the existing
   header-order witnesses unchanged.
6. **Config, C ABI, Python**: TOML scope and resolver step, the T018/T020/T028 rewrites; the C-ABI
   function, 1.13 pins, symbol golden, freeze hash; the SWIG audit row and Python test. W-8, W-9, W-10.
   Mutants M-10, M-11.
7. **Removal, fuzz, comments**: delete `SessionConfig::dialect_overlay` and the forward declaration;
   the fuzz harness and seeds (W-11, M-7 re-run against it); the comment fixes (research R-14). A
   rewritten comment states the rule and the derivation, never a count. Full suite.
8. **Records** (data-model §D–§F): B&L, closed B&L, catalogue, coverage index, 2c notes, brain entry;
   W-13 checks with their seeded negative. File the four issues (research R-15) with labels.
9. **Final head**: one full configure + build + ctest of `linux-clang-debug`; W-12 measured;
   `/speckit-verify` (with `--fuzz-duration=600`); `/gate-b`.

## Risks

- **A splice position that differs from a hand edit.** The loaders apply first-seen and
  declaration-order rules (component indices, `parent_of_`, the first-seen group node, enum order), so
  an addition placed elsewhere than a hand edit would put it yields a different dictionary. Mitigation:
  the splice only appends to the matched container; W-1/W-2 compare against hand-written references
  with the comparator proven on every store (W-0); M-1 proves a position change is caught.
- **A comparator that cannot see a store.** Mitigation: W-0's one seeded row per claimed store; keys
  from the union of both sides; context-store keys derived by walking both structures (research R-10).
- **The XML loader's silent duplicate rules.** A duplicate field name or msgtype would load without an
  error. Mitigation: the merge refuses both before the splice (R-4), with W-3 rows for each.
- **Orchestra shared definitions surprise a user.** An enum value added to a shared code set reaches
  other fields. By construction (it is what a hand edit does), disclosed in B-098-2 and the spec Edge
  Cases.
- **TOML hosts that overwrite `config.dictionary`.** After 098 a host that still copies
  `engine.dictionaries.front()` into every session discards that session's overlay silently. Disclosed in
  B-098-3 and the release note; the one in-tree example
  (`tests/session/engine_establishment_timeout_test.cpp`) is updated to rely on the filled bundle.
- **The send-path change moves an existing frame.** Measured not to for any vendored dictionary
  (research R-7, with a seeded probe); W-7 pins the measurement and W-6 keeps the existing header-order
  witnesses byte-identical.
- **A cycle mutant crashes the binary.** M-7's RED is a stack overflow, run under ASan where the report
  names it; the refusal row itself never recurses unboundedly once the check exists.

## Complexity Tracking

No constitution violation to justify. One design choice is recorded because it widens the work the owner
saw: an Orchestra base takes an Orchestra-format overlay (research R-2), superseding the clarify-time
reading; the alternative, translating a QuickFIX-XML fragment onto Orchestra, was rejected on the
measured facts in R-2.
