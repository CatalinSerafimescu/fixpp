---
description: "Task list for 036-admin-emit-toadmin-coverage"
---

# Tasks: toAdmin/toApp observation coverage for engine-originated Reject and Logout emits

**Input**: Design documents from `specs/036-admin-emit-toadmin-coverage/`
**Prerequisites**: plan.md ✓, spec.md ✓, research.md ✓, data-model.md ✓, contracts/admin-emit-coverage.md ✓, quickstart.md ✓

**Tests**: REQUIRED. This feature is witness-driven — the durable regression is the exact-count
`toAdmin_calls == admin-frames-on-wire` invariant ([[feedback_half_restructure_symmetric_api]] §4,
[[feedback_completeness_gate_exact_set_not_subset]]). Per-site cells are written to FAIL first (the
10 sites bypass observation today), then made to pass by the call-site wiring.

**Organization**: by user story. All production edits land in one file (`src/session/session.cpp`),
so the per-site wiring tasks are **sequential** (no `[P]` across same-file edits); test-cell authoring
across distinct cells is `[P]`.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: parallelizable (different file, no dependency on an incomplete task)
- **[Story]**: US1 (admin toAdmin coverage, P1) · US2 (throwing-callback surfaced, P1) · US3 (BMR→toApp, P2)

## Path Conventions

Single library `fixpp`: production in `src/session/`, tests in `tests/session/`, docs in `spec/` and
`specs/019-app-callbacks/`. All paths below are repository-relative to the library submodule
(`research/G19-fix-fpml-iso20022/library/`).

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: the test-double + baseline capture every story depends on.

- [ ] T001 Capture the pre-036 baseline wire bytes for the affected emit scenarios (no `Application` registered) — provoke each of the 10 sites with no app and snapshot the wire frames/ordering, to be reused as the FR-006/SC-005 byte-identity oracle. Use the **mock-transport (in-memory byte sink) seam** (the approach used in 019's outbound-callback tests) so the oracle is captured without a live network and is deterministic under re-runs. Record the capture method in `tests/session/test_admin_emit_toadmin_coverage.cpp` header comment.
- [ ] T002 Create the new test file `tests/session/test_admin_emit_toadmin_coverage.cpp` with the `CoverageApp` test double (counting / throwing / vetoing — `toAdmin` void inspect-only `++toAdmin_calls` + optional throw; `toApp` returns `expected_t<void>` with `++toApp_calls`, optional throw, optional `app_do_not_send` veto; `fromApp` provokes the BMR), per quickstart.md §"The test double". Match the real `Application` signatures (use the 019 app-callback doubles as the shape reference).
- [ ] T003 Register `tests/session/test_admin_emit_toadmin_coverage.cpp` in the session test CMake target so it builds + is discovered by ctest.

**Checkpoint**: test double compiles and links against the real `Application` interface.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: none beyond Phase 1 — both reused helpers (`fire_to_admin_` at `session.cpp:331`, the
`send_impl` `toApp` block at `session.cpp:4149-4164`) already exist and are noexcept-safe at the
boundary (`parse_and_dispatch_`). No new entity/model/config. Proceed to user stories.

**Checkpoint**: Foundation ready — call-site wiring can begin.

---

## Phase 3: User Story 1 - Application observes every engine-originated Reject and Logout (Priority: P1) 🎯 MVP

**Goal**: `toAdmin` fires for every engine-originated administrative frame (the `Reject(35=3)` family
+ the initiator Guard-3 `Logout`) before it is transmitted — exact-count coverage of the 9 admin sites.

**Independent Test**: per-site `toAdmin_calls == admin-frames-on-wire` exact-count cells (quickstart
table); fails today by the 9-site gap, passes when all 9 admin sites are wired.

### Tests for User Story 1 (write FIRST — must FAIL before wiring) ⚠️

- [ ] T004 [P] [US1] Cell `EmitSessionReject_FromAdminVeto` (helper `:1728` via app-registered `:3026`): assert `toAdmin_calls == 1` and one `35=3` on wire — in `tests/session/test_admin_emit_toadmin_coverage.cpp`.
- [ ] T005 [P] [US1] Cell `EmitSessionReject_NoAppUnknownType_NoOp` (helper via `:3215`, `application == nullptr`): assert the `35=3` is emitted and wire bytes match the T001 baseline — **byte-identity no-op, NOT a `toAdmin` count** (excluded from exact-count) — in the test file.
- [ ] T006 [P] [US1] Cell `Reject_Q3SendingTimeAccuracy` (`:2400` reject + `:2421` paired logout): assert `toAdmin_calls == 2`, two admin frames (reject **and** paired logout) — in the test file.
- [ ] T007 [P] [US1] Cell `Reject_SequenceResetVeto` (`fromAdmin` veto on inbound `35=4` → `:2484`): assert `toAdmin_calls == 1` — in the test file.
- [ ] T008 [P] [US1] Cell `Reject_021ArmC_Malformed122` (inbound PossDup malformed `122`, Arm C → `:2599`): assert `toAdmin_calls == 1` — in the test file.
- [ ] T009 [P] [US1] Cell `Reject_021RC1_Malformed122` (RC#1 malformed-122 → `:2644`): assert `toAdmin_calls == 1` — in the test file.
- [ ] T010 [P] [US1] Cell `Reject_021ArmD` (`:2675` reject + `:2696` paired logout): assert `toAdmin_calls == 2` — in the test file.
- [ ] T011 [P] [US1] Cell `Reject_LogoutVeto` (`fromAdmin` veto on inbound `35=5` → `:2946`): assert `toAdmin_calls == 1` — in the test file.
- [ ] T012 [P] [US1] Cell `Reject_SeqResetNewSeqNoTooLow` (inbound `35=4` `NewSeqNo` < expected → `:4576`): assert `toAdmin_calls == 1` — in the test file.
- [ ] T013 [P] [US1] Cell `Logout_Guard3LogonAckSendingTime` (initiator LogonSent, peer Logon-ack with stale/absent `52=` → `:3368`): assert `toAdmin_calls == 1`, one `35=5` — in the test file.
- [ ] T014 [US1] Build + run T004–T013; confirm all the admin cells FAIL (the engine emits the frame but `toAdmin` is not called) — the RED gate. Read each test body to confirm the assertions are real (not FAIL-placeholders).

### Implementation for User Story 1 (sequential — all edits in `src/session/session.cpp`)

- [ ] T015 [US1] Wire `fire_to_admin_` inside the shared `emit_session_reject_` helper (`src/session/session.cpp:1728`): insert between `assign_outbound` (`:1742`) and `store_then_emit` (`:1747`), adopting the 1137-Reject `assign`-then-`fire` shape; on false → `record_state_transition_(Disconnected)` + `co_return std::unexpected(app_callback_threw)` (contract C1). This single fix covers both caller flows (`:3026` app-registered + `:3215` no-app).
- [ ] T016 [US1] Wire `fire_to_admin_` at inline Reject site `:2400` (established Q3 SendingTime-accuracy) in `src/session/session.cpp` — after `assign_outbound`, before `store_then_emit`, C1 shape.
- [ ] T017 [US1] Wire `fire_to_admin_` at inline Reject site `:2484` (inbound-SequenceReset veto) in `src/session/session.cpp` — C1 shape.
- [ ] T018 [US1] Wire `fire_to_admin_` at inline Reject site `:2599` (021 Arm C malformed-122) in `src/session/session.cpp` — C1 shape.
- [ ] T019 [US1] Wire `fire_to_admin_` at inline Reject site `:2644` (021 RC#1 malformed-122) in `src/session/session.cpp` — C1 shape.
- [ ] T020 [US1] Wire `fire_to_admin_` at inline Reject site `:2675` (021 Arm D) in `src/session/session.cpp` — C1 shape.
- [ ] T021 [US1] Wire `fire_to_admin_` at inline Reject site `:2946` (inbound-Logout veto) in `src/session/session.cpp` — C1 shape.
- [ ] T022 [US1] Wire `fire_to_admin_` at inline Reject site `:4576` (SequenceReset `NewSeqNo`-too-low) in `src/session/session.cpp` — C1 shape.
- [ ] T023 [US1] Wire `fire_to_admin_` at the initiator Guard-3 `build_logout` site `:3368` (Logon-ack SendingTime failure) in `src/session/session.cpp` — after `assign_outbound`, before `store_then_emit`, C1 shape.
- [ ] T024 [US1] Build + run T004–T013; confirm all admin cells now PASS (GREEN). Line numbers will have shifted as edits accumulate — re-anchor each subsequent site by its surrounding context, not the stale absolute line.

**Checkpoint**: all 9 admin sites fire `toAdmin`; the exact-count equality holds.

---

## Phase 4: User Story 2 - A throwing toAdmin during a Reject/Logout is surfaced (Priority: P1)

**Goal**: a throwing `toAdmin` on any newly-wired admin site → `app_callback_threw` + Disconnected,
identical to the 15 already-wired sites (no escape past the noexcept emit path).

**Independent Test**: per-site throwing variant asserts `app_callback_threw` + terminal/Disconnected.

### Tests for User Story 2 (write FIRST) ⚠️

- [ ] T025 [P] [US2] Throwing-variant cells: for each callback-reachable, registered-`Application` admin site from US1 (the **9** app-registered admin cells — T004 + T006–T013; `EmitSessionReject_NoAppUnknownType_NoOp` / T005 is EXCLUDED: its `:3215` caller is reachable only with no app), re-run with `mode = Throw` and assert `result.error() == app_callback_threw` and `session_in_terminal_or_disconnected_state()` — in `tests/session/test_admin_emit_toadmin_coverage.cpp`. (The shared-helper throw path is covered via T004's app-registered `:3026` `fromAdmin`-veto caller.)
- [ ] T026 [US2] Build + run T025; confirm they PASS as a direct consequence of the US1 wiring (the `if (!fire_to_admin_(...)) { record_state_transition_(Disconnected); co_return ... }` arm). Source-read to confirm the throw is caught inside `fire_to_admin_`'s `parse_and_dispatch_` (noexcept boundary respected), not at the call site.

**Checkpoint**: throw-handling parity verified across all newly-wired admin sites.

---

## Phase 5: User Story 3 - BusinessMessageReject is observed via toApp (Priority: P2)

**Goal**: engine-originated `35=j` fires `toApp` (NOT `toAdmin`) before transmission; veto suppresses
the frame but the inbound durable advance still persists; throw → terminal close.

**Independent Test**: `BMR_ToApp_Observed` + the BMR veto cell (the persist-on-veto discriminator).

### Tests for User Story 3 (write FIRST — must FAIL before wiring) ⚠️

- [ ] T027 [P] [US3] Cell `BMR_ToApp_Observed` (Active, `fromApp` rejects an inbound app msg → `:3249`): assert `toApp_calls == 1`, `toAdmin_calls == 0`, one `35=j` on wire — in `tests/session/test_admin_emit_toadmin_coverage.cpp`.
- [ ] T028 [P] [US3] BMR veto cell (`mode = Veto`, `toApp` → `app_do_not_send`): assert `toApp_calls == 1`, `!wire_contains_msgtype("j")`, `session_is_active()`, `outbound_seqnum_after == outbound_seqnum_before`, **and the discriminating** `store_persisted_inbound_seqnum() == inbound_durable_before + 1` (INV-COV-5 — the under-persist hazard witness). Use a persistent store so the durable counter is observable — in the test file.
- [ ] T029 [P] [US3] BMR throwing cell (`mode = Throw` on `toApp`): assert `app_callback_threw` + terminal close — in the test file.
- [ ] T030 [US3] Build + run T027–T029; confirm `BMR_ToApp_Observed` + veto + throw cells FAIL today (the `:3249` site fires neither callback). RED gate; source-read the veto cell to confirm the persist assertion is real.

### Implementation for User Story 3 (sequential — `src/session/session.cpp`)

- [ ] T031 [US3] Wire the `toApp` block at the BMR site `:3249` in `src/session/session.cpp` per contract C2: fire `toApp` **before `assign_outbound`** via `parse_and_dispatch_`; on `app_callback_threw` → `co_await close(close_mode::terminal)` + `co_return`; on veto → set a local `suppressed = true` and **fall through** (do NOT early-return); gate `assign_outbound` + `store_then_emit` on `!suppressed`; preserve the existing fall-through to `persist_inbound_advance_()` at `:3279` on BOTH the vetoed and non-vetoed paths.
- [ ] T032 [US3] Build + run T027–T029; confirm GREEN. Specifically re-confirm T028's `store_persisted_inbound_seqnum() == before + 1` passes (the veto did NOT skip the inbound persist).

**Checkpoint**: BMR routes through `toApp` with full veto/throw parity and no under-persist.

---

## Phase 6: Polish & Cross-Cutting Concerns (doc sweep + sanitizers)

- [ ] T033 Invert the stale comment at `src/session/session.cpp:2096-2098` — the 033 warning "do NOT route through the `fire_to_admin_`-less `emit_session_reject_` helper" is now false; rewrite to state the helper fires `toAdmin` after 036 (contract C4).
- [ ] T034 [P] Add a behaviors-and-limitations row to `spec/behaviors-and-limitations.md` (FR-008) scoping WHICH engine emits fire `toAdmin` vs `toApp` (full coverage post-036) — append per the catalogue convention.
- [ ] T035 [P] Add the 036 traceability entry to **both** `spec/feature-catalogue.md` and `spec/coverage-index.md` per their existing per-feature row conventions (both confirmed to carry per-feature rows).
- [ ] T036 [P] Add a dated note to the FR-008 anchor (`specs/019-app-callbacks/spec.md` and/or its tasks.md): coverage extended to the full Reject family + Guard-3 Logout + BMR-via-`toApp` — append-only, NO merged-history rewrite.
- [ ] T037 Run the no-`Application` no-op check (FR-006/SC-005): each provocation with no app registered, assert wire bytes + ordering byte-for-byte identical to the T001 baseline.
- [ ] T038 Run the full new binary under ASan, UBSan, TSan (one sanitizer at a time per the WSL2 build cap; session suite is the regression set). TSan must stay clean — callbacks run on the engine executor under 019 single-thread confinement (no new cross-thread surface). Treat any sanitizer finding as a real defect until disproven.
- [ ] T039 Run `tools/check_layers.py` (no new headers, but confirm clean) and the full session ctest suite to confirm no regression in the 15 already-wired sites or elsewhere.
- [ ] T040 Re-run `codegraph sync` (cwd = library submodule) so the index reflects the new call sites for Gate B.
- [ ] T041 **SC-002 static-enumeration gate** (the anti-half-restructure regression): re-run the research.md Decision-1 census at post-implementation HEAD — assert all 9 admin target sites now appear in the `fire_to_admin_` call-site list and the BMR site routes `toApp`, and that **no** engine-originated administrative `build_*` site remains absent from the observation wiring. Encode it as a grep-gate (or a compile-time counter `static_assert` if the implementer prefers) so a future unwired admin emit site fails the build/check, not just a scenario. This makes SC-002's "zero bypass" independently verifiable rather than inferred from the dynamic T004–T013 cells.
- [ ] T042 **§IX.1 coverage gate deferral (note, not a code task)**: the Art. IX §1 DA/BRDA thresholds (≥95% line / ≥85% branch on the touched `session.cpp` emit paths + the new test binary) are measured at `/speckit-verify` per Art. XVII §8 — the same deferral pattern used by 034/035 — NOT as a manual task here. plan.md's IX §1 "100% DA/BRDA" is a design-time claim discharged by the verify coverage preset; this bullet records the deferral so no Gate-B reviewer mistakes it for a dropped obligation.

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: no dependencies — start immediately.
- **Foundational (Phase 2)**: empty (helpers pre-exist).
- **US1 (Phase 3)**: depends on Phase 1. Tests (T004–T013) before wiring (T015–T023). MVP.
- **US2 (Phase 4)**: depends on US1 wiring (the throw arm is the same `if (!fire_to_admin_)` branch).
- **US3 (Phase 5)**: depends on Phase 1; independent of US1/US2 wiring (different emit site). Can be authored in parallel with US1 but its production edit (T031) is in the same file — sequence after US1 edits to avoid line-anchor churn.
- **Polish (Phase 6)**: after all wiring + cells GREEN.

### Within Each User Story

- Test cells (write-first, must FAIL) → wiring → re-run GREEN.
- All same-file `session.cpp` wiring tasks are **sequential** (line anchors shift as edits land — re-anchor by surrounding context).

### Parallel Opportunities

- T004–T013 (US1 cells), T025 (US2 cells), T027–T029 (US3 cells) are `[P]` — distinct test cells in the new file, authored independently before any wiring.
- T034–T036 (doc rows in distinct files) are `[P]`.
- Production `session.cpp` edits are NOT `[P]` with each other.

---

## Implementation Strategy

### MVP (US1)

1. Phase 1 Setup (T001–T003).
2. Phase 3 US1 RED cells (T004–T014) → wiring (T015–T023) → GREEN (T024).
3. **STOP and VALIDATE**: the exact-count admin coverage holds — the core of the feature.

### Incremental

1. Setup → US1 (MVP: admin observation restored) → US2 (throw parity, rides US1) → US3 (BMR→toApp).
2. Polish: doc sweep + sanitizers + codegraph sync.

---

## Notes

- `[P]` = different file / distinct test cell, no dependency.
- Every `session.cpp` line number in the wiring tasks is the **branch-HEAD anchor**; as edits accumulate, re-anchor by surrounding context (research.md Decision 1 is the authoritative inventory).
- Source-read every RED test body before trusting the fail (FAIL-placeholder trap, [[feedback_fail_placeholder_red_test]]).
- The BMR veto persist assertion (T028) is the single discriminating witness for the under-persist hazard — it must use a persistent store.
- Commit after each logical group; the exact-count witness is the durable regression that pins coverage closed.
