---
description: "Task list for feature 069-v44-all-families (widen 067 writer-emitter to ALL v44 application messages)"
---

# Tasks: v44 all-families typed codegen coverage

**Input**: Design documents from `specs/069-v44-all-families/`
**Prerequisites**: plan.md ✓, spec.md ✓ (US1/US2 = P1, US3 = P2), research.md ✓ (R1–R9), data-model.md ✓, contracts/coverage-and-completeness.md ✓ (C1–C6), quickstart.md ✓

**Tests**: REQUIRED (Constitution Article VII — TDD). This feature ships tests-first: the msgcat fail-closed witness, the differential round-trip harness at breadth, the new-family required-field fail-closed witness, the exemplar-per-family external-golden anchor, the generalized exact-set completeness pin, and the mode-count assertion. New tests are authored to FAIL (RED) before the corresponding emitter/CMake change lands.

**Organization**: Grouped by user story (US1 coverage = P1, US2 verification = P1, US3 selection/cost = P2) so each is independently implementable and testable. All paths are relative to the library submodule root (`research/G19-fix-fpml-iso20022/library/`); run every command with cwd inside it.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no incomplete dependency).
- **[Story]**: US1 / US2 / US3 label — ONLY on user-story-phase tasks (not Setup / Foundational / Polish).
- Every task carries an exact file path.

## Path Conventions

Single-project C++ library + host codegen tool (the library submodule). Emitter source under `tools/codegen/fixpp-codegen/`; CMake codegen driver at `cmake/Codegen.cmake`; generated output into the build tree `build/<preset>/_codegen/include/fixpp/v44/Builders.hpp`; tests under `tests/session/` and `tests/codegen/`; QuickFIX goldens under `tests/session/golden/`; catalogue/limitations under `spec/`; the folded amendment in `.specify/constitution.md`.

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Capture the pre-069 green baseline the no-regression gates (FR-005/SC-003) compare against, and land the folded constitution amendment (amend-then-proceed, Article XX §1) before touching emitter code.

- [X] T001 [P] Build `fixpp-codegen` (`cmake --build build/linux-clang-debug --target fixpp-codegen`), then capture the pre-069 **OFFICIAL** `build/linux-clang-debug/_codegen/include/fixpp/v44/Builders.hpp` as the byte baseline for the 33-OFFICIAL byte-identity gate (T009/SC-003), and run the existing 067 suites (`ctest -L 067 --output-on-failure`) to record the GREEN baseline. No code edits.
- [X] T002 Land the folded **Article XVIII §7 amendment** into `.specify/constitution.md` verbatim from plan.md `## Constitution Amendment Payload` — prepend the Sync Impact Report line, replace §7 with the copy-ready replacement text, bump `v0.4 → v0.5`. Gate A converged (round 3, 2 rewrites) and user signed off 2026-07-11; the post-`/analyze` precision-fix (A-021/N-001/A-025 `v`/`w` named; operative 83-scope unchanged) is folded into that same payload and needs no re-Gate-A. This task only lands the reviewed+corrected text (amend-then-proceed, Article XX §1).

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: The IR `msgcat` extension every coverage predicate and the fail-closed witness depend on.

**⚠️ CRITICAL**: No user story work can begin until this phase is complete — US1's selection predicate keys on `msgcat`.

- [X] T003 [P] Add the message category field to the codegen IR in `tools/codegen/fixpp-codegen/ir.hpp` — `bool is_application` (or `enum class MsgCategory { Admin, App }`), one per `MessageIR` (data-model.md entity 1).
- [X] T004 [P] **(TDD — RED first)** Add the **msgcat fail-closed witness** to `tests/codegen/test_067_emit_builders_unit.cpp`: a synthetic `<message>` LACKING a `msgcat` attribute must make `build_ir` fail closed (loader error, NO default-guess), mirroring the existing synthetic-XML discriminating-witness pattern in that file (~line 331). Must FAIL before T005. Inherits this TU's existing grouping/label.
- [X] T005 Populate `msgcat` in `tools/codegen/fixpp-codegen/ir.cpp` — parse `msgcat='app'|'admin'` from each `<message>` into the IR field; **fail closed (loader error) on any `<message>` missing `msgcat`** (no default-to-app/admin). Makes T004 GREEN. (data-model.md entity 1 validation.)

**Checkpoint**: IR carries authoritative app/admin category; missing-msgcat is a hard loader error.

---

## Phase 3: User Story 1 - Typed build/validate for any FIX44 application message (Priority: P1) 🎯 MVP

**Goal**: Widen the emitter selection so it generates `build_/validate_/Args`/registry for all **83 in-scope** `msgcat='app'` FIX44 messages (33 OFFICIAL + 50 new), default-on, with the 33 OFFICIAL output byte-identical.

**Independent Test**: Regenerate `Builders.hpp` under default coverage; pick a previously-uncovered family (TradeCaptureReport `35=AE`), populate its typed `Args`, call `build_TradeCaptureReport` and `validate_TradeCaptureReport`, and confirm well-formed wire bytes carrying the seeded fields + required-field enforcement. (This smoke — T008 — also discharges SC-005: the typed API is reachable without the generic runtime tag/value path.)

### Implementation for User Story 1

- [X] T006 [US1] In `tools/codegen/fixpp-codegen/emit_builders.cpp`: add the N-002/N-003 exclusion set as a `constexpr` array `{BE, BF, BW, BX, BY}` alongside (not replacing) `kOfficial33` (data-model.md entity 3), and replace the `kOfficial33`-only emission gate with a **coverage-mode predicate**: `official` → emit iff `msg_type ∈ kOfficial33` (unchanged output); `all` → emit iff `is_application && msg_type ∉ exclusion set`. Keep the existing `ir.ns == "v44"` namespace gate unchanged (FR-004). Thread a coverage-mode parameter into the `emit_builders(ir, mode)` signature.
- [X] T007 [US1] In `tools/codegen/fixpp-codegen/main.cpp`: parse `--families all|official` (**default `all`**) and pass the mode to `emit_builders(ir, mode)`. (CMake wiring of the flag is T015/US3; the flag itself lands here so US1 is independently exercisable.)
- [X] T008 [US1] **(TDD — per-family smoke, RED first)** Regenerate `Builders.hpp` (forced) under default `all`; author a witness that a newly-covered family compiles + builds + validates (TradeCaptureReport `35=AE`: populate `TradeCaptureReportArgs`, `build_`/`validate_` round-trip to seeded values) — RED against pre-widening `Builders.hpp`, GREEN after T006/T007. Confirm `grep -c 'build_' …/v44/Builders.hpp == 83`. Discharges SC-005.
- [X] T009 [US1] Assert the **33 OFFICIAL messages are byte-identical** (FR-005/SC-003, AS4): regenerate under `--families official` and diff the OFFICIAL builder subset against the T001 baseline `Builders.hpp` — expect empty diff.

**Checkpoint**: All 83 in-scope builders generate by default; the 33 OFFICIAL stay byte-for-byte unchanged. US1 is independently demonstrable.

---

## Phase 4: User Story 2 - Every generated builder is verified, not just emitted (Priority: P1)

**Goal**: Prove all 83 mechanically-generated builders correct — differential round-trip at breadth, required-field fail-closed for new families, an external-golden anchor for the fixed exemplar set (non-tautology), and an exact-set completeness pin that catches any silent drop/add.

**Independent Test**: `ctest -L roundtrip` green with 0 skips (every builder's output re-parses through the runtime-XML path with each seeded field exact); `ctest -L family_golden` green (exemplar bytes == QuickFIX goldens); the fail-closed witness REDs a builder that omits a required field; the completeness pin passes with the emitted registry equal to the intended set.

### Implementation for User Story 2

- [X] T010 [P] [US2] **(TDD — RED first)** NEW `tests/session/test_069_all_families_roundtrip.cpp` — table-driven **differential round-trip** over **every** emitted application builder: seed `Args`, `build_<Msg>`, parse the bytes back through the independent runtime-XML path (`dict::Dictionary::as_table_view`), assert each seeded field (at each group level) reads back exactly (C3). 100% of emitted builders in-harness, 0 skips; a non-round-tripping family is a **named failing test**. Labels `069;all_families;roundtrip`; joins an existing whole-binary grouped executable (§8, `gtest_discover_tests` prohibited). Exercise ≥1 group-heavy/nested family (TradeCaptureReport NoSides/NoLegs).
- [X] T011 [P] [US2] Generate the **8 fixed exemplar QuickFIX goldens** under `tests/session/golden/` per contract C4 (basenames exact): `069_tradecapturereport_ae` (nested NoSides/NoLegs), `069_positionreport_ap`, `069_collateralinquiry_bb`, `069_securitylist_y`, `069_confirmation_ak`, `069_registrationinstructions_o`, `069_liststatus_n`, `069_businessmessagereject_j`. Reuse the existing 067/061 reference-engine golden-capture approach.
- [X] T012 [US2] **(TDD — RED first)** NEW `tests/session/test_069_family_exemplar_golden.cpp` — for each of the 8 C4 exemplars assert `build_<Msg>(seed)` bytes **equal** the checked-in QuickFIX golden bytes (external oracle so builder+parser cannot be co-wrong, FR-010/SC-006). Test names track the golden basenames. Labels `069;family_golden`; joins a grouped executable (§8). Depends on T011.
- [X] T013 [US2] **(TDD — RED first)** NEW-family **required-field fail-closed witness** (FR-006, closes analyze E2): assert `validate_<Msg>` returns the required-field-missing disposition when a required field is omitted, for ≥1 nested (TradeCaptureReport `AE`, omit a required `NoSides` entry field) AND ≥1 flat (BusinessMessageReject `j`, omit `RefMsgType`) newly-covered family — behavior identical to the 33's `test_067_builder_failclosed.cpp`. RED before T006 widens emission. Add to the differential-harness TU (`test_069_all_families_roundtrip.cpp`) or a sibling `test_069_all_families_failclosed.cpp`; join a grouped executable, label `069;all_families;failclosed`.
- [X] T014 [US2] **Generalize the completeness pin** `tests/session/test_067_completeness.cpp` (the REAL FR-011 emitted-set pin — today hardcodes `builder_registry == {kExpectedOfficial33}` + `== 33U`, so it REDs under default `all`): assert the emitted registry equals the **mode's intended set** (`official` → 33; `all` → 83). Compute the `all`-mode expected set from an **independent raw `FIX44.xml` census** (pugixml/grep walk of `<message msgcat='app'>` minus present `{BE, BF}`, asserting cardinality 83) — **NOT** re-derived from the `VersionIR`/`build_ir` the emitter consumes (else a mis-parsed `msgcat` drops from both sides and passes vacuously; C2 non-circular). Reuse the N3-census raw-pugixml precedent (`test_067_emit_builders_unit.cpp` lines 36/221). Stays **STANDALONE** (§8 exempts exact-set/completeness gates from grouping).

**Checkpoint**: Breadth is trustworthy — every builder round-trips, new families fail closed on omitted required fields, the exemplars match an external oracle, and the exact-set pin blocks silent drift.

---

## Phase 5: User Story 3 - Build cost of wide coverage is bounded and selectable (Priority: P2)

**Goal**: A build-time `CACHE STRING` selects `all` (default) vs `official` (opt-down to today's 33-builder cost), CI still verifies the full set, and a count assertion proves each mode's cardinality.

**Independent Test**: Configure with each value; confirm generated coverage matches (`all` → 83, `official` → 33 byte-identical to pre-069); confirm at least one CI preset stays on `all`.

### Implementation for User Story 3

- [X] T015 [US3] In `cmake/Codegen.cmake`: add `FIXPP_CODEGEN_V44_FAMILIES` as a **`CACHE STRING`** (default `all`), `set_property(CACHE … PROPERTY STRINGS all official)`, and a configure-time `FATAL_ERROR` on any value ∉ `{all, official}` (before codegen runs) — **not** `option()`. Pass `--families ${FIXPP_CODEGEN_V44_FAMILIES}` to the `fixpp-codegen` invocation (C1). Regen-guard: an INTERNAL `FIXPP_CODEGEN_V44_FAMILIES_LAST_USED` cache var forces `_need_generate` when the value changes between configures (empirically verified same-dir all→official: 83→33, byte-identical to the 067 baseline).
- [X] T016 [US3] **(TDD — RED first)** NEW `tests/session/test_069_mode_count.cpp` — mode-count / build-graph assertion: `all` → 83 emitted builders, `official` → 33 (SC-004). Labels `069;mode_count`; joins `test_067_completeness` (the mode-agnostic standalone completeness binary — the builder-suite grouped binaries cannot compile under `official`, since they reference specific non-OFFICIAL typed symbols by name).
- [X] T017 [US3] Ensure the CI matrix keeps **≥1 preset on `FIXPP_CODEGEN_V44_FAMILIES=all`** that generates AND verifies the full set (runs `-L roundtrip` / `-L family_golden` / `-L mode_count`) so wide coverage is continuously proven irrespective of any cost-sensitive opt-down (FR-008). Assert no verifying preset overrides to `official`. Confirmed by inspection: no preset in `CMakePresets.json` sets this variable, so every tier1.yml preset (incl. the unfiltered `ctest --preset` runs) defaults to `all`. Made durable via a new "Verify full v44 coverage preset (FR-008)" CI step (both the `linux` matrix job and the `coverage` job) that reads `CMakeCache.txt` post-Configure and fails the job if the value isn't `all`.

**Checkpoint**: Coverage is selectable and cost-bounded; the full set stays continuously verified in CI.

---

## Phase 6: Polish & Cross-Cutting Concerns

**Purpose**: Record the carried limitation, the Article VI references, and the codegen no-regression / verify gates; then the mandatory close-out.

- [X] T018 [P] Record the carried enum-domain limitation in `spec/behaviors-and-limitations.md` — add `L-069-*`: generated validators enforce required-presence + type conformance only; enum value-domain is unbacked (FR-013/C5/SC-007). Add any nested-family limitation surfaced by T010.
- [X] T019 [P] Article VI §5 normative-refs pass: regenerate/confirm the emitter's normative-refs output (`emit_normative_refs.cpp` product) and that the widened set carries the message-level `[FIX44]` refs (per spec `## Normative References`, incl. the analyze-corrected A-021/N-001/A-025 `v`/`w`). No stale/pinned version propagated.
- [X] T020 Forced-regen of **all 4 codegen namespaces**; confirm the `codegen-build-graph-check` git-cleanliness gate is GREEN (only `v44/Builders.hpp` grows; no other generated output drifts) (C6). Regenerate before any sanitizer/coverage run (non-debug dirs compile a fresh `_codegen` — `project_codegen_emitter_staleness`).
- [X] T021 Run `/speckit-verify` (Tier-1 serial sanitizer matrix — ASan/UBSan/TSan + debug) on the codegen consumers + differential harness; confirm `capi_freeze.sha256` / `c_api.h` unchanged (FR-012/C6). Writes `.specify/decisions/069-v44-all-families-verify.md`.

### Mandatory close-out tasks (ALWAYS emit — Gate-B preconditions, Article XVII §8)

- [X] T022 [P] **Catalogue close-out**: flip every feature-owned OFFICIAL row in `spec/feature-catalogue.md` → `done` (with the 069 PR / evidence ref), AND flip the matching `spec/coverage-index.md` write-column rows from `—` → `069` for the v44-present MsgTypes only. Derive the flip-list from the **same independent raw `FIX44.xml` census** T014 computes (`msgcat='app'` minus `{BE,BF}`) — do NOT hand-maintain a second parallel list (analyze C1). The list is the 50 new MsgTypes: A-014/015/019/**021 (AH/AI/AJ)**/025 (incl. **v/w**/x/y) + A-016/017/020 + A-022(`AW`) + A-026(z,AA) + **N-001 (BC/BD)** + C-001 + C-002(`AL`/`AM`/`AN`/`AO`/`AP`) + R-001..005 + P-004..008. **A-014 (BusinessMessageReject) is already `done` via 019 — APPEND 069 evidence, do not clobber the 019 attribution (mirror the A-001/A-006 dual-attribution pattern; analyze C3).** Do NOT flip the FIX50-only rows/siblings (A-018/023/027/028/029/030/031/032/033, C-003, BO/BR/BL) — no v44 row exists to flip (per the folded amendment carve-out). Also correct N-001's `feature-catalogue.md` FIX-version column if it still reads `5.0–5.0SP2` (analyze F2).
- [X] T023 **Feature-completeness audit (MUST be the FINAL task)**: assert against the merged tree that (i) every `tasks.md` row is `[X]` or carries an explicit waiver rationale; (ii) every spec FR-001..013 and SC-001..007 maps to a landed test AND a landed implementation; (iii) every feature-owned OFFICIAL catalogue row is `done` with a matching `coverage-index.md` entry. **Cross-check the emitted `builder_registry` (83 entries) directly against `coverage-index.md`'s v44 app-message rows — NOT against T022's hand-list — so the audit self-corrects for any under-reported row (analyze E1).** Record the verdict (100% or fully-waived) in `.specify/decisions/069-v44-all-families-verify.md` (`## Completeness` section) OR a sibling `.specify/decisions/069-v44-all-families-completeness.md`. Hard `/gate-b` precondition (Article XVII §8 / pre-flight 4d).

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: T001 ∥ T002 — no code dependency (baseline capture ∥ amendment landing).
- **Foundational (Phase 2)**: after Setup. T003 ∥ T004 (distinct files); T005 after both (makes T004 GREEN). **BLOCKS all user stories** (predicate keys on `msgcat`).
- **US1 (Phase 3, P1)**: after Foundational. Delivers the MVP write surface.
- **US2 (Phase 4, P1)**: after US1 (its harness/pin reference the emitted symbols). Note: T014 (old completeness pin) is momentarily RED between US1 emitting 83 and T014 generalizing it — expected within-branch, closed by T014.
- **US3 (Phase 5, P2)**: after US1 (needs the `--families` flag from T007). Independently testable.
- **Polish (Phase 6)**: after all desired stories; close-out T022 then completeness audit T023 last.

### Within Each Story

- Tests authored and FAILING before the code that satisfies them (TDD, Article VII): T004→T005; T008 RED→T006/T007; T010/T012/T013/T014/T016 RED before their emitter/CMake support.
- Regenerate `Builders.hpp` (forced) after each emitter change before running dependent tests.
- T011 (goldens) before T012 (golden assertion).

### Parallel Opportunities

- Setup: T001 ∥ T002.
- Foundational: T003 ∥ T004 (`ir.hpp` vs test file).
- US2: T010 ∥ T011 (harness authoring ∥ golden capture — distinct files); T013 shares T010's TU (or a sibling); T014 is standalone.
- Polish: T018 ∥ T019 ∥ T022 (distinct docs).

---

## Parallel Example: User Story 2

```bash
# Author the round-trip harness while capturing the exemplar goldens (distinct files):
Task: "test_069_all_families_roundtrip.cpp differential harness over all 83 (T010)"
Task: "8 fixed QuickFIX exemplar goldens under tests/session/golden/ (T011)"
```

---

## Implementation Strategy

### MVP First (User Story 1 only)

1. Phase 1 Setup → baseline recorded, amendment landed.
2. Phase 2 Foundational (CRITICAL — blocks all stories): IR `msgcat` + fail-closed loader.
3. Phase 3 US1 → 83 builders generate by default; 33 OFFICIAL byte-identical.
4. **STOP and VALIDATE**: US1 independently (T008/T009 green).
5. Demo the widened typed write surface.

### Incremental Delivery

1. Setup + Foundational → foundation ready.
2. US1 → the 83-builder write surface (MVP), 33 OFFICIAL unchanged.
3. US2 → the trust layer: differential round-trip at breadth + new-family fail-closed + external-golden anchor + generalized exact-set pin.
4. US3 → build-time selection control + mode-count + CI full-set verification.
5. Polish → limitation record + Art VI + codegen-clean + `/speckit-verify` + close-out.

### Parallel Team Strategy

After Foundational: Developer A → US1 (emitter predicate + flag); Developer B → US2 (differential harness + fail-closed + goldens + completeness pin, consumes A's `Builders.hpp`); Developer C → US3 (CMake `CACHE STRING` + mode-count + CI). Integrate through `emit_builders.cpp` / `main.cpp` with the forced-regen `codegen-build-graph-check` gate as the shared guard.

---

## Independent-Test Criteria per Story

- **US1**: default-`all` regeneration yields 83 `build_` symbols; a previously-uncovered family (TradeCaptureReport `35=AE`) builds + validates from its typed `Args` (also discharges SC-005); `--families official` output is byte-identical to the pre-069 baseline (T008/T009).
- **US2**: `ctest -L roundtrip` green with 0 skips (every seeded field round-trips through the runtime path); `ctest -L family_golden` green (8 exemplars == QuickFIX goldens); the new-family fail-closed witness REDs a builder omitting a required field (T013); the generalized completeness pin equals the mode's independently-censused intended set (T010/T012/T013/T014).
- **US3**: configuring `FIXPP_CODEGEN_V44_FAMILIES=official` yields exactly 33 builders (byte-identical) and `=all` yields 83; an out-of-domain value fails configure; ≥1 CI preset verifies the full set (T015/T016/T017).

---

## Notes

- [P] = different files, no incomplete dependency.
- The 33 OFFICIAL builders stay byte-identical under either mode — fix the predicate, never the frozen OFFICIAL output.
- No new runtime / C-ABI / Python / link-ABI surface (FR-012). The generated C++ header `v44/Builders.hpp` **intentionally grows** ~50 symbols (outside `capi_freeze.sha256`); no-regression checks target `capi_freeze.sha256` / `c_api.h`, NOT the absence of new C++ builder names (C6).
- IR `msgcat` is codegen-host-tool-local — no runtime `Dictionary`/`GroupRef`/C-ABI/Python change.
- **The delivered `all` set is the full 83-message `msgcat='app'` scope minus {BE,BF} — the family enumeration in spec/plan is illustrative, not the operative bound. The `/analyze` precision-fix named the 7 previously-unenumerated in-scope messages (A-021 AH/AI/AJ, N-001 BC/BD, A-025 v/w); T022's flip-list must not silently drop them.**
- New isolation-safe TUs join a whole-binary grouped executable; select by `ctest -L <label>`, never `-R <exe-name>` (§8). The completeness pin stays standalone (§8 exemption).
- The all-mode completeness expected set AND the T022 flip-list MUST come from an independent raw `FIX44.xml` census, never from the emitter's own `VersionIR` (non-circular, C2/C1).
- Regenerate `Builders.hpp` (forced) before sanitizer/coverage runs; verify new tests FAIL before implementing; commit after each task or logical group.
