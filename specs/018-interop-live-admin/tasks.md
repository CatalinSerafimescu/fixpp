---
description: "Task list — 018-interop-live-admin (gap-fill G1)"
---

# Tasks: Live Session-Admin Interop Round-Trips (gap-fill G1)

**Input**: Design documents from `specs/018-interop-live-admin/`
**Prerequisites**: plan.md ✓, spec.md ✓, research.md ✓, data-model.md ✓, contracts/ ✓, quickstart.md ✓ (Gate A converged round 3, 2026-06-03)

**Tests**: This is a **tests/harness-only** feature — the interop cells **are** the deliverable (TDD: each cell is a red-first witness that `skip`s without a live counterparty, never silent-passes). **ZERO production surface** is expected; if any task needs a `src/`/`include/` change it is a finding that re-triggers Gate A (`[const §XVII.1]`), not a silent edit.

**Organization**: by user story in priority order — US1 (P1) → US3 (P1) → US2 (P2) → US4 (P3).

**Boundary**: committed library deliverable lives under `tests/interop/`; the cross-engine orchestration (QFJ drive, gap/malformed injection, capture, `cell_results.yaml` emit) lives in the gitignored parent `phase-9-harness/` (`[const §XV.18]`). Parent-side tasks are marked **[PARENT]** — tracked here as dependencies, authored out-of-repo.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: parallelizable (different files, no incomplete-task dependency)
- **[Story]**: US1/US2/US3/US4; Setup/Foundational/Polish carry no story label
- **[PARENT]**: authored in the gitignored `phase-9-harness/`, not the submodule

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: bundle-fidelity + harness scaffolding prerequisites.

- [ ] T001 Fix the Gate-A round-3 residual **P3**: correct `data-model.md` E4 `CellResultRow` field names to match the inherited 016 schema verbatim (`id` not `cell_id`; drop `tier` (it is a `gate_verdict` field, not a row field); add the REQUIRED `config` + `kind` row fields) — verify against `tests/interop/cell_results_schema_check_test.py` in `specs/018-interop-live-admin/data-model.md`.
- [ ] T002 [P] Confirm the existing reuse surface is present and unchanged: `tests/interop/support/interop_fixture.hpp` (`run_until`/`stop_within`), `tests/interop/support/golden_diff.hpp` (`diff_transcripts(..., excluded_tags)` at :44-46; default `default_normalization_tags()` at :36-40), `tests/interop/support/counterparty_probe.hpp`, `tests/interop/support/scenario_descriptor.hpp`, `tests/interop/cell_results_schema_check_test.py` — record their current shape (no edits).

---

## Phase 2: Foundational (Blocking Prerequisites)

**⚠️ CRITICAL**: blocks ALL user stories. These are the shared harness extensions every admin cell reuses.

- [ ] T003 Extend `tests/interop/support/scenario_descriptor.hpp` with the G1 admin-descriptor fields per `contracts/admin-scenario-descriptor.md`: scenario-specific `induction` (`inbound_silence`/`idle_observation`/`withhold_frame`/`qfj_restart_resend`/`proxy_corrupt`, required by `scenario_group`), `ac_ref`/`acceptance_ids`, and the per-`(scenario_group, role)` AC-exact-set rule.
- [ ] T004 [P] Add the **`{52,10}` admin normalization profile** as a named constant/helper in `tests/interop/support/golden_diff.hpp` (e.g. `admin_profile_excluded_tags() → {52,10}`) for callers to pass into `diff_transcripts(...)`; do NOT alter `default_normalization_tags()` (FR-007).
- [ ] T005 [P] Add a reusable both-roles value-parameterization helper (or confirm the existing `TestWithParam<std::tuple<Counterparty, Role>>` pattern suffices) so every admin cell runs fixpp-initiator AND fixpp-acceptor (FR-005a) in `tests/interop/happy/hp_support.hpp`.
- [ ] T006 [P] [PARENT] Extend the parent harness `cell_results.yaml` emitter to add the G1 admin cells at **release-prep tier** and round-trip them through the **same** in-repo `interop_cell_results_schema_check` rules (no schema change) — `phase-9-harness/tools/emit_matrix.py` (FR-008). **Depends on T029**: the in-repo `EXPECTED_IDS` frozenset (`cell_results_schema_check_test.py:132`) is an exact-set allowlist — the parent emit will FAIL `test_per_cell_completeness_no_silent_absence` (`unexpected=…`) for any G1 id not added to `EXPECTED_IDS` first. Emit each G1 cell id verbatim as reconciled in T029.

**Checkpoint**: descriptor + admin-profile + both-roles plumbing + result emission ready — user stories can begin.

---

## Phase 3: User Story 1 — Bidirectional TestRequest→Heartbeat (Priority: P1) 🎯 MVP

**Goal**: prove the established-session TestRequest→Heartbeat `112` echo interoperates live, both directions, both roles.

**Independent Test**: drive a live cell to `Active`; hold inbound-silent so fixpp's liveness loop emits a `TestRequest` (engine-chosen `112`); assert the golden shows QFJ's `Heartbeat` echoing that observed `112`; then the reverse (QFJ-originated). (AC US1-1, US1-2, US1-3.)

- [ ] T007 [US1] Extend `tests/interop/happy/hp_fix44_testrequest_echo_test.cpp` to the bidirectional admin round-trip: (a) induce fixpp `TestRequest` via inbound silence + assert FSM stays `Active` + outbound-seqnum advances (in-process witness via the pre-existing `seqnum_mgr_test_access()` seam); (b) QFJ-originated `TestRequest` → fixpp `Heartbeat`. Value-parameterized over both roles. Self-deadlined (FR-010); `skip:counterparty-unavailable` when QFJ absent (FR-009). `ac_ref` = {US1-1, US1-2, US1-3}.
- [ ] T008 [P] [US1] [PARENT] Parent: drive QFJ to originate a `TestRequest` (reverse leg) and capture the enriched golden for both role cells — `phase-9-harness/tools/` + `configs/`.
- [ ] T009 [US1] Capture + commit the enriched goldens `tests/interop/happy/golden/HP-QFj-{init,acc}-fix44-testreq-echo.fix` (first paired run; engine-log seam; never hand-fabricated) and assert via `diff_transcripts(..., admin_profile_excluded_tags())` so `112` is matched verbatim.
- [ ] T010 [P] [US1] Add the SC-004 gate-bite negative test: mutate the echoed `112` in a golden copy → drift gate FAILs (a **compared** tag, never `52`/`10`) in `tests/interop/happy/hp_fix44_testrequest_echo_test.cpp`.

**Checkpoint**: US1 GREEN both roles — MVP. The induce/await fixture pattern every later story reuses is proven.

---

## Phase 4: User Story 3 — Recovery dialogue, both directions (Priority: P1)

**Goal**: prove fixpp's recovery sub-protocol (013/S-023) interoperates live in BOTH directions, both roles.

**Independent Test**: (i) withhold a QFJ→fixpp frame → fixpp emits `ResendRequest(7/16)`, QFJ replies (GapFill or replay), fixpp back to `Active`; (ii) drive QFJ-restart-resend → fixpp answers (replay/`SequenceReset-GapFill`), QFJ resyncs. One induction mechanism pinned per cell (New-3). (AC US3-1..US3-4.)

- [ ] T011 [US3] Extend `tests/interop/happy/hp_fix44_seqnum_recovery_test.cpp` for the **inbound-detect** half (`recovery_inbound`, induction=`withhold_frame`): assert fixpp's `ResendRequest(35=2,7,16)` (golden) + recovery-transition + return to `Active` with expected inbound seqnum, no prefix loss (in-process FSM/seqnum witness). Both roles; self-deadlined. `ac_ref` = {US3-1, US3-2, US3-4}.
- [ ] T012 [US3] Create `tests/interop/happy/hp_fix44_recovery_outbound_answer_test.cpp` for the **outbound-answer** half (`recovery_outbound`, induction=`qfj_restart_resend`, FR-004a): assert fixpp answers a QFJ `ResendRequest` with replay and/or `SequenceReset-GapFill(35=4,123=Y)` (013 `build_sequence_reset_gapfill`, golden) + QFJ resyncs + fixpp stays `Active`. Both roles; self-deadlined. `ac_ref` = {US3-3, US3-4}.
- [ ] T013 [P] [US3] [PARENT] Parent: implement the `recovery_inbound` frame-withhold (proxy drop) and the `recovery_outbound` QFJ-restart-resend choreography per `contracts/parent-harness-admin-contract.md` §3/§4 (preserved store, `ResetOnLogon=N`, expected `7/16`, per-role) — `phase-9-harness/`.
- [ ] T014 [US3] Capture + commit goldens `tests/interop/happy/golden/HP-QFj-{init,acc}-fix44-recovery-{inbound,outbound}.fix` (first paired run); assert under the `{52,10}` admin profile (`34`/`123`/`122`/`7`/`16` verbatim).
- [ ] T015 [P] [US3] Gate-bite negative test mutating a recovery seqnum tag (`7`/`16`/`123`) → drift gate FAILs, in the two recovery cells (SC-004).

**Checkpoint**: US3 GREEN both directions, both roles — the strongest interop signal.

---

## Phase 5: User Story 2 — Idle Heartbeat cadence (Priority: P2)

**Goal**: prove unsolicited Heartbeat cadence at `108=1`s interoperates both directions.

**Independent Test**: negotiate `108=1`s, hold idle ~5s, assert ≥3 beats each direction (±1), no `TestRequest`. (AC US2-1, US2-2.)

- [ ] T016 [US2] Create `tests/interop/happy/hp_fix44_idle_heartbeat_cadence_test.cpp` (`induction=idle_observation`): assert ≥3 `Heartbeat(35=0)` per direction over ~5s (±1 tolerance) and **no `TestRequest`**, fixpp stays `Active`. Size the `108`/grace relationship per FR-002 so a punctual peer beat cannot trip a spurious `TestRequest`. Both roles; self-deadlined. `ac_ref` = {US2-1, US2-2}.
- [ ] T017 [P] [US2] [PARENT] Parent: configure QFJ `HeartBtInt=1`s for the cadence cell + capture the windowed golden — `phase-9-harness/configs/`.
- [ ] T018 [US2] Capture + commit goldens `tests/interop/happy/golden/HP-QFj-{init,acc}-fix44-idle-cadence.fix`; assert beat counts under the `{52,10}` profile.

**Checkpoint**: US2 GREEN both roles.

---

## Phase 6: User Story 4 — Session-level Reject(35=3) survival (Priority: P3)

**Goal**: prove a parent-injected malformed admin frame elicits `Reject(35=3)` and the session survives.

**Independent Test**: parent corrupts an admin frame in flight; assert the receiver's `Reject(45/373[/371])` crosses the wire and the session stays `Active`. (AC US4-1, US4-2.)

- [ ] T019 [US4] Extend `tests/interop/happy/hp_fix44_reject_invalid_admin_test.cpp` (`induction=proxy_corrupt`): assert fixpp emits `Reject(35=3, 45, 373[, 371])` on a parent-corrupted inbound frame (S-007 path) + session stays `Active` (no disconnect) + subsequent heartbeats flow. fixpp never originates malformed bytes. Both roles; self-deadlined. `ac_ref` = {US4-1, US4-2}.
- [ ] T020 [P] [US4] [PARENT] Parent: implement the in-flight admin-frame corruption at the proxy (a controlled defect rejected per `[FIX-SL §4.5.4]`; pick an input that elicits Reject not disconnect) — `phase-9-harness/`.
- [ ] T021 [US4] Capture + commit goldens `tests/interop/happy/golden/HP-QFj-{init,acc}-fix44-session-reject.fix`; record any reject-vs-disconnect peer divergence in `tests/interop/KNOWN-LIMITATIONS.md` (edge case).

**Checkpoint**: all four scenario groups GREEN, both roles.

---

## Phase 7: Polish & Cross-Cutting Concerns

- [ ] T022 [P] Update `tests/interop/happy/MATRIX.md` with the G1 admin-chain rows (per-cell completeness, exact-set over `{scenario_group × role}` AND acceptance ids; each row cites its `[FIX-SL §…]`).
- [ ] T023 [P] [PARENT] Update `phase-9-harness/INTEROP-016-ROADMAP.md` G1 status → done and the parent `phase-9-harness/BADGE.md` scope note to reflect newly asserted session-admin coverage **without overstating scope** (still not app-message interop) — FR-011 / SC-006. (Parent-authored; in-repo doc surface is T028.)
- [ ] T024 **[const §IX.2] sanitizer pass** (discharges the 016 verify-YELLOW waiver): run the interop ctest under ASan/UBSan/TSan against live QFJ admin traffic, one preset at a time `-j2` (`[[feedback_build_resource_cap_oom]]`); fixpp-only instrumentation. Record in `/speckit-verify`.
- [ ] T025 Run the full G1 matrix + schema-check round-trip (`interop_cell_results_schema_check`, which now validates the T029-extended `EXPECTED_IDS` exact-set), repeat count = **3** (per SC-005; self-contained, no ROADMAP figure to cite), assert deterministic / no flakes within the per-cell self-deadline budget (SC-002, SC-005); run quickstart.md (requires T006 [PARENT] for `emit_matrix.py --include-g1`).
- [ ] T026 **Feature-completeness audit** (`/gate-b` precondition, `[[feedback_feature_completeness_gate]]` + `[[feedback_completeness_gate_exact_set_not_subset]]`): assert `{tasks} ↔ {FR-001..FR-011, FR-004a, FR-005a} ↔ {SC-001..SC-006} ↔ {AC US1-1..US4-2}` is 100% covered as an **exact set** (missing/unexpected diff), or waived with rationale.
- [ ] T027 Update `spec/feature-catalogue.md` + `spec/coverage-index.md` with the G1 interop coverage (the admin behaviours S-003/004/005/006/007/014/023 now have live-QFJ interop witnesses) — feature-catalogue/coverage-index update task.
- [ ] T028 [P] Update the in-repo `tests/interop/KNOWN-LIMITATIONS.md` (and any in-repo badge/scope note) to reflect newly asserted session-admin coverage **without overstating scope** (still not app-message interop) — the in-repo half of FR-011 / SC-006 (parent ROADMAP/BADGE is T023).
- [ ] T029 **Exact-set manifest + `EXPECTED_IDS` reconciliation (Setup-class; blocks T006/T025)**: the in-repo `cell_results_schema_check_test.py:132` `EXPECTED_IDS` frozenset enforces **exact-set** equality (`test_per_cell_completeness_no_silent_absence`: both `missing` AND `unexpected` must be empty) — the `[[feedback_completeness_gate_exact_set_not_subset]]` mechanism this feature's `data-model.md` E1 cites. (a) **Decide the G1 cell-id naming** against the existing 016 ids (reuse-and-enrich `HP-QFj-{init,acc}-fix44-testrequest-echo` / `-seqnum-recovery` / `-reject-invalid-admin`, OR introduce new ids `…-testreq-echo` / `-recovery-{inbound,outbound}` / `-idle-cadence` / `-session-reject` and add them) — the genuinely-new cells are at minimum `idle-cadence` (×2 roles) and the `recovery-inbound`/`recovery-outbound` split (T011 vs T012 are two test files). (b) Add the resolved G1 cell-stub rows to the committed `tests/interop/cell_results.yaml` as `status: skip:counterparty-unavailable, matrix_disposition: live` (the 016 local-declaration pattern). (c) Extend `EXPECTED_IDS` so the frozenset equals the manifest id-set exactly — **default to reuse-and-enrich** (the 016 ids are shipped-badge cells; keep them, only ADD genuinely-new ids); remove a 016 id ONLY if option (a) deliberately renames it, in which case migrate manifest + `EXPECTED_IDS` together. **Invariant** (pin, applies to T009/T014/T018/T021): `golden_ref` basename == cell_id == `EXPECTED_IDS` member — the golden filenames in those tasks are provisional until reconciled here. (FR-008.)

---

## Dependencies & Execution Order

- **Setup (P1)** → **Foundational (P2, blocks all stories)** → user stories.
- **US1 (P1, MVP)** first — proves the induce/await fixture pattern the others reuse.
- **US3 (P1)** next — highest correctness value; depends only on Foundational (reuses US1's pattern but independently testable).
- **US2 (P2)**, **US4 (P3)** — independent; can run after Foundational in parallel with US3 if staffed.
- **Polish (P7)** after all desired stories; T024/T025/T026 are `/speckit-verify` + `/gate-b` preconditions.
- Within a story: cell driver (RED witness) → [PARENT] choreography → golden capture → gate-bite negative test.
- **T029 is Setup-class despite its number** — its cell-id naming decision (a) must be made BEFORE the golden-capture tasks (T009/T014/T018/T021) so their `golden_ref` basenames are final, and its `EXPECTED_IDS`/manifest extension (b/c) must complete BEFORE T006 (parent emit) and T025 (schema-check round-trip) or both FAIL the exact-set assertion. Execute T029(a) alongside T001 in Phase 1; T029(b/c) once cell ids are final.

### Parallel opportunities
- T002/T004/T005/T006 (Foundational, different files) in parallel — but T006 depends on T029(b/c).
- [PARENT] tasks (T008/T013/T017/T020/T023) parallel to their sibling in-repo cell tasks (different repos).
- Gate-bite negative tests (T010/T015) and in-repo doc updates (T022/T028) marked [P].

---

## Implementation Strategy

**MVP** = Phase 1 + 2 + US1 (T001–T010): one bidirectional admin round-trip GREEN both roles, de-risking the fixture extension. Then US3 (highest value), then US2, then US4. Each story is an independently testable, badge-eligible increment. Goldens are captured at first paired run (never hand-fabricated); cells `skip:counterparty-unavailable` without live QFJ.

## Notes
- ZERO production surface expected; a needed `src/`/`include/` change is a Gate-A-re-triggering finding (R-prod).
- `[P]` = different files, no incomplete-task dependency; `[PARENT]` = gitignored `phase-9-harness/`.
- Goldens assert under the explicit `{52,10}` admin profile — NEVER the 016 default (drops `112`/`34`/`122`/`123`).
- Commit after each task or logical group; `skip`/`fail` on a live cell ⇒ badge-ineligible.
