# Implementation Plan: Dialect overlay — venue additions layered on a base dictionary (D-009)

**Branch**: `098-dialect-overlay` | **Date**: 2026-10-31 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/098-dialect-overlay/spec.md`

## Gate A

- Round 1 applied 2026-10-31: Codex P1=4 P2=8 P3=1; Opus post-judging P1=1 P2=7 P3=9; rewrite addresses root causes #1–#4 and A-001, A-002, A-003, A-004, A-005, A-006, A-007, A-008, A-009, A-010, A-011, A-012, A-013, N-1, N-2, N-3, N-4, N-5, N-6, N-7, N-8, N-9, N-10, N-11. Reviews: research/reviews/codex_098-dialect-overlay_gate_a_review.md, research/reviews/opus_098-dialect-overlay_gate_a_adversarial_review.md.
  - **Owner attention** (orchestrator defaults, spec Clarifications "Gate A round 1"): N-2 (an enum value
    on a field with no values closes its domain: accepted and disclosed, B-098-7); N-11 (a `[default]`
    overlay cannot be cleared for one session: disclosed, no "none" value); N-5 (the pre-existing
    `first_field_index` wrap is probed, filed and excluded from the census check, not fixed in 098).
    N-5 is superseded at round 2 (below).
- Round 2 applied 2026-10-31: Codex P1=4 P2=2 P3=0; Opus post-judging P1=2 P2=6 P3=4; rewrite addresses root causes A–D and G098-R2-001, G098-R2-002, G098-R2-003, G098-R2-004, G098-R2-005, G098-R2-006, N-1, N-2, N-3, N-4, N-5, N-6, N-7, N-8, N-9, N-10, and the round-1 residuals A-002 (Root cause C), A-006 (N-10), A-011 (N-6), N-1 (N-2), N-4 (Root cause A), N-5 (N-3) and N-10 (N-9). Reviews: research/reviews/codex_098-dialect-overlay_gate_a_2_review.md, research/reviews/opus_098-dialect-overlay_gate_a_2_adversarial_review.md.
  - **A (pairs):** one relation check, base against spliced, between splice and build, using each
    loader's own factored pair detection (research R-3, R-5 step 8a; W-3's pair table; M-16).
  - **B (budget):** the budget counts steps (child nodes visited), so empty-component chains are
    charged; a step counter in both `expand_field_list`s refuses on the overlay path once it passes the
    pre-walk's prediction with a distinct origin; tests assert no such refusal and counter = prediction;
    the header store is a visited-set walk (research R-5, R-7; M-17).
  - **C (comparator):** the compared stores are a declared list; each valid seed's reported set must
    equal its expected set, and the union of expected sets plus reason-only unchangeable entries equals
    the list (research R-10).
  - **D (formats):** W-3 is refusal kinds × formats with N/A reasons; the shared walk returns structured
    results and each loader throws its own type (research R-5, contracts W-3).
  - **Owner attention** (orchestrator default, spec Clarifications "Gate A round 2"): the side-table
    wrap. Round 1's premise ("pre-existing and not caused by an overlay") was false: an overlay can cause
    it within the budget on a base that does not wrap today. The default is now **refused at load** for a
    base whose side tables fit; a base that already does not fit stays out of scope (probed, filed if
    shown). N-2 and N-11 are unchanged.
- Codex fixer applied 2026-10-31 after round 3 (Step F, owner choice): Opus round-3 post-judging P1=0 P2=2 P3=3; fixes G098-R3-002, N-1, G098-R3-001, N-2, N-3. Reviews: research/reviews/codex_098-dialect-overlay_gate_a_3_review.md, research/reviews/opus_098-dialect-overlay_gate_a_3_adversarial_review.md.
- Loop 2 round 1 applied 2026-10-31: Codex P1=0 P2=2 P3=1; Opus post-judging P1=0 P2=2 P3=5; rewrite addresses root causes H (US4's observables written at the C++ session's level) and I (run-order text stating results instead of recipes), and G098-L2R1-001, G098-L2R1-002, G098-L2R1-003, N-1, N-2, N-3, N-4. Reviews: research/reviews/codex_098-dialect-overlay_gate_a_4_review.md, research/reviews/opus_098-dialect-overlay_gate_a_4_adversarial_review.md.
  - **H (C ABI and Python):** W-9 is split into pure rows in `capi_pure_tests` and a standalone live
    arm `capi_098_dict_overlay_live_test` (`capi;098`). The live arm and W-10's Python arm read one
    frame's custom Length+Data value with an SOH inside, which only the merged dictionary reads intact;
    mutant M-19 (the thunk returns the base) turns both RED. Python option (a) was taken: US4's promise
    is kept, not narrowed (contracts W-9, W-10; spec Clarifications, Gate A loop 2 round 1).
  - **I (recipes):** the bomb rows assert the budget origin instead of a time ratio, and the step
    counter is armed at the smaller of the prediction and the base's total plus the budget, so M-12's
    RED is bounded (research R-5); quickstart §1 configures the Python bindings, runs the import-surface
    test against the built `lib/` and states what counts as its RED; §3 adds `field-order` and checks
    each selection with `ctest -N`.
  - **N-4:** W-5 asserts each accepted scenario's read, not only accept/reject.
- Loop 2 round 2 converged 2026-11-01: Codex P1=0 P2=0 P3=0; Opus post-judging P1=0 P2=0 P3=3; the three P3s applied post-convergence (M-12 RED per row, W-9 RED sources and dispositions, W-9 raw-send sender). Reviews: research/reviews/codex_098-dialect-overlay_gate_a_5_review.md, research/reviews/opus_098-dialect-overlay_gate_a_5_adversarial_review.md.

### Round 1 — disagreements

Opus marked no Codex finding **Disagree**. These Codex counter-proposals were applied in a different
shape, following the Opus judgement:

- **A-004.** Codex offered checking in three complete edited vendored files. Not applied: FR-017's
  "checked in" came from an orchestrator clarification, not an owner ruling, so FR-017 was amended to
  name two reference kinds (complete small files; hand-written patches of the vendored files), and the
  patch anchor became a multi-line context block, because a one-line anchor cannot express "append to the
  container" (spec Clarifications, research R-10).
- **A-005.** Codex's premise that `resolve_selectors` "receives only merged session tables" is wrong: it
  also receives `root_tbl`, which holds the raw `[default]` table and `session` array. No new plumbing;
  origins are read there (research R-9).
- **A-006.** Codex asked for allocation-failure seams around path construction and handle allocation.
  Not applied: global OOM injection is isolation-sensitive under `[const §VII.8]`. Applied instead: the
  bad-alloc-first catch order, pugixml's `status_out_of_memory` mapped to `xml_oom_error`, and a factored
  mapping function fed each exception type (research R-6, W-9).
- **A-007.** Downgraded to P3. The CMake file joins Files changed; no build or link witness, because an
  unlisted `.cpp` fails loudly at link time.
- **A-008.** Codex offered "header when either half is declared header" or refusal of a split pair. Not
  applied: the existing rule, a counted Data value follows its Length's class, is kept and stated
  (FR-022, research R-7), since a split pair cannot be sent correctly either way.
- **A-011.** Codex asked for a document-level "Updated by 098" note in 2c. Applied in the Root cause #4
  shape instead: a grep recipe finds every hit across all records, and each hit is dispositioned
  (data-model §G).
- **A-012.** Downgraded to P3. Codex's alternative of a separate Orchestra row was not taken: both rows'
  scope cells widen (data-model §E).

### Round 2 — disagreements

Opus marked no Codex finding **Disagree**. Two were downgraded (G098-R2-004 P1→P2; G098-R2-006 P2→P3).
These Codex counter-proposals were applied in a different shape, following the Opus judgement:

- **G098-R2-002.** Codex asked the merge to model `detect_length_pairs` exactly. Not applied: that is a
  fourth hand copy of the loader's rule, the class that failed twice. The loader's own detection is
  factored and run on base and spliced documents (research R-3, step 8a).
- **G098-R2-003.** Codex offered charging one standalone header expansion in the budget. Not applied:
  the header store is a set, computed by a visited-set walk that no budget needs to charge, and the step
  counter refuses any expansion the pre-walk does not model (research R-5, R-7).
- **G098-R2-004.** Codex asked for a dedicated context-required seed and mutant, and a census of
  required membership per context. Applied as Root cause C instead: the seeded rows are derived from the
  declared store list, which includes context `required_members`; the census extension was not taken
  (research R-10).
- **G098-R2-005.** Codex offered a `(msgType, scenario)` identity for Orchestra messages. Not applied: it
  would change the loader contract. A lookup that hits a repeated base key is refused instead, for every
  key kind (research R-3).

### Loop 2 round 1 — disagreements

None: Opus marked no Codex finding **Disagree**. G098-L2R1-001 was downgraded P2→P3. These Codex
counter-proposals were applied in a different shape, following the Opus judgement:

- **G098-L2R1-001.** Codex offered putting the live arm in an existing live C-ABI loopback executable.
  Not applied: `[const §VII.8]` keeps a live, engine-starting test standalone, and adding 098's file to
  another feature's live executable would mix ownership of that binary. The live arm is a new standalone
  executable (contracts W-9).
- **G098-L2R1-002.** Codex offered delivering "a frame containing the overlay-only tag". Not applied as
  stated: a scalar overlay-only tag reads the same on a base-only handle, because C-ABI and Python
  sessions do not validate and `fixpp_msg_get_string` reads any top-level tag. The frame carries the
  overlay's custom Length+Data pair with an SOH inside the Data value (contracts W-9, W-10).
- **G098-L2R1-003.** Codex offered operation counters plus a generous absolute test timeout. Applied as
  the budget-origin assertion; the bound on M-12's run is the counter's arming at the smaller of the
  prediction and the base's total plus the budget (research R-5), not a timeout.

## Summary

A user loads a base dictionary with one overlay and gets an ordinary `dict::Dictionary`, which they put in
`SessionConfig::dictionary`. The overlay is a fragment in the base's own format (QuickFIX XML or FIX
Orchestra) holding only additions.

- **Mechanism (research R-1).** Each loader parses the base and the overlay into pugixml documents and
  runs the guards in research R-5's order: the overlay's shape (bytes, DOCTYPE, depth, entry and
  reference caps); one iterative, memoized walk of the merged reference graph that refuses a cycle, any
  overlay whose expansion would exceed a fixed budget of steps, and any overlay that would push a
  fitting side table past its 16-bit index; then the conflict checks (R-4). It then splices the overlay
  into the base document by "an edit lands on the definition it names" (R-3), compares the spliced
  document's Length+Data relation with the base's using the loader's own detection, and runs its
  existing `build_handle_from_doc` with a step counter armed at the walk's prediction (capped at the
  base's total plus the budget, research R-5). The spliced
  document *is* the equivalent edited file (each addition appended to its container, in overlay order),
  so the result equals that file's load by construction, store for store.
- **Two formats (R-2).** The Orchestra overlay is an Orchestra fragment, because translating a
  QuickFIX-XML fragment onto Orchestra's id-referenced, shared-definition model is a second design of
  its own (measured reasons in R-2). Disclosed: Orchestra code sets and groups are shared, so an edit
  reaches every user (B-098-2).
- **Header on send (R-7, FR-022).** The dictionary gains its declared header set; the session's send
  partition treats a tag as header-class when it is in the fixed 34-tag set **or** the dictionary's
  header, and a counted Data value keeps following its Length. Measured: for every file found by the
  vendored-dictionary census recipe the union equals the fixed set, so no existing session's frames
  change.
- **Surfaces.** C++ (`XmlLoader` / `OrchestraLoader::load_with_overlay[_from_string]`,
  `dict::load_any_with_overlay`); C ABI 1.13 (`fixpp_dict_load_from_xml_with_overlay`, the first
  producer of `FIXPP_ERR_DICT_LIMIT_EXCEEDED`); Python through SWIG; TOML `dialect_overlay` in
  `[default]` and `[[session]]`, with every TOML session now arriving with `config.dictionary` set
  (R-9).
- **Removed.** The never-read `SessionConfig::dialect_overlay` and the `DialectOverlay` forward
  declaration (pre-release breaking, public C++ API).
- **Records.** D-009 and COM-011 close, their scope widened to FIXT.1.1 and FIX Latest; L-002-2, L-422-1
  and the stale L-004-1 retire; every record data-model §G's grep recipe finds is annotated in place or
  dispositioned as history (data-model §D–§G). No constitution amendment (R-14).

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
**Performance Goals**: FIX50SP2 with the named at-cap fixture overlay loads in ≤ 1.5 × the base load (SC-005); the send-path lookup adds one
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
| `[const §VII.8]` | Per-witness dispositions are in contracts W-0–W-13. Both halves of W-7, exact censuses over the shipped dictionaries, are in the standalone `dictionary_098_header_census_test`, labelled `dictionary;098;census`, with `src/session` on its include directories for the self-contained `send_header_tags.hpp`; W-5/W-6 join `session_pure_tests`. W-9's pure rows join `capi_pure_tests`; its live arm, which starts C-ABI engines with their worker threads, is standalone `capi_098_dict_overlay_live_test`, labelled `capi;098`, with an explicit `TIMEOUT`. W-0 remains in `dictionary_pure_tests` because it is a self-test of test-local data with no isolation hazard. Other pure gtests join their named dictionary, config and capi buckets; W-10–W-13 are Python, fuzz, measurement or scripts rather than grouped gtests. All CTest selection is by label. |
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
| `[const §X.6]`, Appendix A | Triggered ("ABI surface change", "Codegen layout: dictionary loader"): all four controls. `/clarify` done; Codex Gate A converged loop 2 round 2; `/analyze` after `/speckit-tasks`; **user `/plan` sign-off: given 2026-11-01** (pre-authorised by the owner 2026-10-31 on Gate A convergence; Gate A converged at loop 2 round 2). |
| `[const §XVII.7]` | Local pre-PR build gate: before the PR opens, a Conan install + configure + build + `ctest` of `linux-clang-debug`, and `pytest bindings/python/tests/` because 098 touches `bindings/python/`, at the final head (tasks T096); the PR body carries `local build: green on linux-clang-debug @ <sha>` from that run. The clause's Resource-gate approval is the owner's standing instruction of 2026-10-31, quoted in tasks T002. |
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
| Dictionary impl | `src/dictionary/{xml_loader,orchestra_loader,load_any,dictionary}.cpp` (the factored pair detection, the step counter, the header-store walk), `src/dictionary/dictionary_internal.hpp`; a new `src/dictionary/overlay_merge.{hpp,cpp}` for the guards, the conflict checks and the two splice walks (shared helpers: overlay-only bounded read, shared parse-status mapping, depth walk, the match-index helper, the merged-graph cycle, budget and side-table walk, and the test-facing read seam for the step counter and refusal origin); `src/dictionary/CMakeLists.txt` (lists `fixpp_dictionary`'s sources explicitly, so `overlay_merge.cpp` is added there) |
| Session | `src/session/session.cpp` (the send classification line); a new private header `src/session/send_header_tags.hpp` (`kSendHeaderTags` and `is_send_header_tag`, moved out of `session.cpp`, research R-7; self-contained, standard headers only, so W-7's census executable includes it without linking `fixpp_session`); `include/fixpp/session/session_config.hpp` (field and forward declaration removed) |
| Config | `src/config/toml_config_loader.cpp` (scope-aware `dialect_overlay`), `src/config/selector_resolver.cpp` (per-session dictionary step), `src/config/loader_internal.hpp` (doc) |
| C ABI | `include/fix/c_api/dict.h`, `include/fix/c_api/version.h`, `src/capi/dictionary.cpp`, `src/capi/capi_internal.hpp` (the exception-to-code mapping function, research R-6), `tests/abi/golden/fixpp_capi_symbols.txt`, `tools/capi_freeze.sha256` |
| Python | `bindings/python/fixpp.i` (GIL audit table), `bindings/python/tests/test_098_dialect_overlay.py` (in-tree only, no wheel twin, so `bindings/python/tests/wheel/README.md` is unchanged), `bindings/python/tests/wheel/test_import_surface.py` |
| Tests | `tests/dictionary/{dictionary_equivalence.hpp,dictionary_equivalence_test.cpp,overlay_equivalence_test.cpp,overlay_refusal_test.cpp,header_tags_test.cpp}` (`header_tags_test.cpp` is standalone `dictionary_098_header_census_test`, labels `dictionary;098;census`, holding both W-7 halves, with `src/session` on its include directories; W-0–W-4 join `dictionary_pure_tests`), `tests/dictionary/fixtures/098/**`, `tests/session/{test_098_dialect_overlay_inbound,test_098_header_send}.cpp` (join `session_pure_tests`), `tests/config/test_098_dialect_overlay.cpp`, `tests/config/fixtures/098/**`, the three renamed and rewritten cells (`T018_SessionDialectOverlayFileMissing`, `T028_DialectOverlayFileMissing`, `T020_RootDialectOverlayRefused`, contracts W-8) in `tests/config/{test_load_negative_battery,test_load_selectors,test_load_deferred_surface}.cpp`, with `T020_DeferredSetExactCoverage`'s key set, and the header comments of their fixtures `tests/config/fixtures/{neg_step2_dialect_overlay,neg_deferred_toplevel}.toml`, `tests/capi/{dict_overlay_capi_test,dict_overlay_capi_live_test,version_test}.cpp` (the live arm is standalone `capi_098_dict_overlay_live_test`; `capi_pure_tests` gains `FIXPP_098_FIXTURE_DIR` and label `098` by `APPEND`), `tests/session/test_resend_answer_field_order.cpp` (stale comment only), `tests/session/engine_establishment_timeout_test.cpp` (only if its unedited run fails, or it relies on the host copy in a way research R-9's change makes wrong; tasks T061), the CMakeLists of each bucket and standalone executable |
| Fuzz | `tests/fuzz/fuzz_dict_overlay.cpp`, `tests/fuzz/corpus/dict_overlay/`, `tests/fuzz/CMakeLists.txt` |
| Records | `spec/{feature-catalogue,coverage-index,behaviors-and-limitations,behaviors-and-limitations-closed}.md`; every hit of data-model §G's inventory recipe (including `.specify/2c-codegen.md`, other design docs such as `.specify/2d-threading.md` and `.specify/2m-pybind.md`, and the brain pages whose `refs` name a changed file, such as `brain/components/{dictionary,session,c-api}.md`; `config.md` and `python-api.md` assessed); `CLAUDE.md` / `CLAUDE-history.md` at close-out |

## Phases

1. **Unmodified-code witnesses first** (quickstart §1): exactly the rows of the contracts arm table whose
   unfixed outcome is **fails** (W-6's edited-dictionary and replay arms, W-8's rows, W-9's version pin
   and symbol golden, W-10's import surface). Negative controls and characterization pins are not in this list. Build and run in
   `build/linux-clang-debug`; that run is the RED record. **Stop rule:** a listed
   arm that passes on the unfixed tree means its premise is false; stop and decide, never adjust the
   test.
2. **Comparator** (test-only): `dictionary_equivalence.hpp` and W-0, every seeded row reports exactly
   its declared expected set, the union of those sets plus reason-only unchangeable entries equals the
   declared store list, and the identical pair is clean, over every store except the header store
   (`is_header_tag` does not exist yet). Nothing
   reads W-1/W-2 before this is green.
3. **QuickFIX-XML overlay**: errors, the overlay guards in R-5's order (shape caps, the merged-graph
   cycle, budget and side-table walk, then the conflict checks of R-4), the XML splice (R-3), the step 8a
   pair-relation check over the factored `detect_length_pairs`, the step counter and its read seam;
   `XmlLoader::load_with_overlay[_from_string]`. W-1 and W-2: every row T018 writes, each with its
   seeded mismatch, and the wrap probe; W-3 (XML column). Mutants M-0..M-7, M-12..M-14,
   M-16..M-18, M-22 (step 4's depth walk removed), M-23 (step 4a's root checks removed) and M-24
   (`XmlLoader`'s overlay entry points do not forward `policy`). Full suite.
4. **Orchestra overlay**: the Orchestra splice and checks, the factored `resolve_length_pairs`, the
   Orchestra step counter; `OrchestraLoader::load_with_overlay[_from_string]`;
   `dict::load_any_with_overlay`. W-1 (orch), W-2 (Orchestra), W-3 (Orchestra column: every cell of the
   product that is not N/A), W-4, W-5 (its RED is M-8). Mutants M-8, M-1o and M-2o (the Orchestra
   splice's position and `fixr:code` mutants), M-24 at `OrchestraLoader`'s overlay entry points and at
   `dict::load_any_with_overlay`, and M-3..M-7, M-12..M-14, M-16..M-18, M-22, M-23 once
   more for the Orchestra rows.
   **Stop rule:** if the Orchestra W-2 row shows a difference that comes from the loader's own
   order-dependent rules rather than the splice, record it and decide before continuing.
5. **Header on send**: handle member filled by the visited-set header walk (research R-7, not an
   `expand_field_list` call), `table_view::header_bits_` + `is_header_tag`, builder forwarder,
   `as_table_view` fill, `src/session/send_header_tags.hpp`, the send classification; the comparator's
   `is_header_tag` comparison and its W-0 seeded row, then W-1/W-2 re-run with it; W-3's header-expansion
   row gains its header-store assertion. W-6, W-7. Mutant M-9. Full suite: the existing header-order
   witnesses unchanged.
6. **Config, C ABI, Python**: TOML scope and resolver step, the three renamed T018/T020/T028 cells; the C-ABI
   function and its mapping function, 1.13 pins, symbol golden, freeze hash; the SWIG audit row and
   Python test. W-8, W-9 (pure rows and the standalone live arm), W-10 (run by pytest; no ctest label
   reaches it). Mutants M-10, M-11, M-15, M-19, M-20, M-21 (M-11 and M-19 also against W-10).
7. **Removal, fuzz, comments**: delete `SessionConfig::dialect_overlay` and the forward declaration;
   the fuzz harness and seeds (W-11, M-7 and M-12 re-run against it per format, M-17 for the counter
   origin); the comment fixes (research R-14). A
   rewritten comment states the rule and the derivation, never a count. Full suite.
8. **Records** (data-model §D–§G): run §G's inventory recipe and disposition every hit; B&L, closed
   B&L, catalogue (scope cells widened), coverage index, the design-doc and brain notes; W-13 checks with
   their seeded negative. File the issues of research R-15 with labels.
9. **Final head**: one full configure + build + ctest of `linux-clang-debug`; W-12 measured;
   `/speckit-verify` (with `--fuzz-duration=600`); `/gate-b`.

## Risks

- **A splice position that differs from a hand edit.** The loaders apply first-seen and
  declaration-order rules (component indices, `parent_of_`, the first-seen group node, enum order), so
  an addition placed elsewhere than a hand edit would put it yields a different dictionary. Mitigation:
  the splice only appends to the matched container; W-1/W-2 compare against hand-written references
  with the comparator proven on every store (W-0); M-1 (XML) and M-1o (Orchestra) prove a position
  change is caught.
- **A comparator that cannot see a store.** Mitigation: the compared stores are a declared list; every
  valid seed declares the exact set invariants allow it to change, the comparator must report exactly
  that set, and the union of expected sets plus reason-only unchangeable entries must equal the list
  (research R-10); keys
  from a raw-document census, a test-only read of the handle's indexes and the public API; bare, exact
  and context stores compared separately (research R-10).
- **A store both sides get wrong.** Both sides go through the same `build_handle_from_doc`, so a loader
  defect is invisible side against side. Mitigation: `component_fields` / `group_fields` checked against
  the raw census; the FIX50SP2 wrap probe recorded (research R-10).
- **An overlay that makes the loader do unbounded work.** Definition caps and emitted entries do not
  bound expansion. Mitigation: the merged-graph budget walk in steps before any recursive step, and the
  step counter in both `expand_field_list`s, which refuses with a distinct counter origin once the
  build passes the walk's prediction (research R-5); W-3's bomb and empty-chain rows, M-12, M-17.
- **A model of a loader rule drifts from the loader.** The pair rule and the expansion count were each
  modelled by hand in earlier drafts and missed cases. Mitigation: the pair check calls the loader's
  own factored detection; the expansion model is checked against the counter by equality in W-1 and
  W-2, while the fuzz harness also aborts on the distinct counter-origin refusal.
- **The XML loader's silent duplicate rules.** A duplicate field name or msgtype would load without an
  error. Mitigation: the merge refuses both within the overlay and, for a repeated base key, on the
  overlay's lookup, before the splice (R-3, R-4), with W-3 rows for each.
- **Orchestra shared definitions surprise a user.** An enum value added to a shared code set reaches
  other fields. By construction (it is what a hand edit does), disclosed in B-098-2 and the spec Edge
  Cases.
- **TOML hosts that overwrite `config.dictionary`.** After 098 a host that still copies
  `engine.dictionaries.front()` into every session discards that session's overlay silently. Disclosed in
  B-098-3 and the release note; the one in-tree example
  (`tests/session/engine_establishment_timeout_test.cpp`) is run unedited first: it copies the same
  pointer the bundle now holds for a session without an overlay (research R-9), so it is edited only if
  that run fails or it relies on the host copy in a way R-9's change makes wrong (tasks T061).
- **The send-path change moves an existing frame.** Measured not to for any vendored dictionary
  (research R-7, with a seeded probe); W-7 pins the measurement and W-6 keeps the existing header-order
  witnesses byte-identical.
- **A cycle or budget mutant crashes or hangs the binary.** M-7's RED may be a stack overflow, run under
  ASan where the report names it. M-12's run is bounded by the counter's arming at the smaller of the
  prediction and the base's total plus the budget (research R-5): the counter refuses with its own
  origin, and the empty-chain rows report that origin; step 6's side-table fit refuses the doubling rows
  before the build (contracts W-3). The refusal rows themselves never recurse or expand once
  the walk exists, because it runs before every recursive step.

## Complexity Tracking

No constitution violation to justify. One design choice is recorded because it widens the work the owner
saw: an Orchestra base takes an Orchestra-format overlay (research R-2), superseding the clarify-time
reading; the alternative, translating a QuickFIX-XML fragment onto Orchestra, was rejected on the
measured facts in R-2.
