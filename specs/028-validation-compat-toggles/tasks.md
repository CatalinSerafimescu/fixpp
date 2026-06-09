# Tasks: Validation-compat toggles — CheckCompID & ValidateSequenceNumbers (G3 slice)

**Input**: Design documents from `specs/028-validation-compat-toggles/`
**Prerequisites**: plan.md (S1–S7 site model, Gate A converged round 3), spec.md (US1–US3, FR-001..013, SC-001..008), data-model.md (I-VCT-1..11 + touched-sites table S1–S7), contracts/validation-compat-toggles.md (C1..C3), research.md (D-1..D-7)
**Branch**: `028-validation-compat-toggles` | repository root = library submodule

**Tests**: REQUIRED (TDD / RED-first, `[const §VII]`). The data-model invariants (I-VCT-1..11) + the spec Success Criteria (SC-001..008) are the authoritative witness list; each test task below names the invariants/SC it covers. Write each cluster RED (failing) before its implementation.

## Format: `[ID] [P?] [Story] Description`

- **[P]** = parallelizable (different file, no incomplete-task dependency). Witnesses in the same shared file (`tests/session/test_validation_compat_toggles.cpp`) are NOT mutually [P]; impl edits in the same file (`src/session/session.cpp`) are NOT mutually [P].
- **[Story]** = US1 (CompID) / US2 (Seqnum) / US3 (default byte-identical). Setup, Foundational, Polish carry no story label.
- Line numbers (`:NNNN`) are from plan.md / data-model.md at Gate-A time; treat as anchors — confirm the site by its surrounding code, not the literal line, since edits shift them.

---

## Phase 1: Setup (shared infrastructure)

**Purpose**: Test scaffolding + ctest target registration for the new unit file and the live interop cell.

- [X] T001 [P] Create `tests/session/test_validation_compat_toggles.cpp` skeleton (GoogleTest, reusing the existing session-test harness / fixtures used by `tests/session/test_next_expected_msgseqnum.cpp` — specifically the `struct Fixture`, `struct OutboundCapture`, and a `CountingApp`-style `fixpp::session::Application` subclass, driven via free `TEST(...)` macros, not `TEST_F`) and register the ctest target `session_validation_compat_toggles` in `tests/session/CMakeLists.txt`.
- [X] T002 [P] Create `tests/interop/happy/hp_fix44_validation_compat_test.cpp` skeleton (skip-without-counterparty guard, per the 016/018/027 harness pattern) and register its interop cell in the `tests/interop` CMake wiring.

**Checkpoint**: both targets configure and build (empty/skipped); ctest discovers them.

---

## Phase 2: Foundational (blocking prerequisites — MUST complete before US1/US2)

**Purpose**: The two additive config knobs. Both default `true` (strict = current behaviour); they must exist before any handler site can read them. ⚠️ No story work begins until this phase is complete.

- [X] T003 Add two additive primitive `bool` fields to `include/fixpp/session/session_config.hpp` — `bool check_comp_id = true;` and `bool validate_sequence_numbers = true;` — each with an EXPLICIT per-field default `[const §XII.5]` and a one-line doc comment (strict = current behaviour; `false` = QuickFIX-compat relaxation). Primitive bools ⇒ no new include (§XV.9 N/A); preserve the 010 W-5 `SessionConfig` copy-constructible `static_assert`. — data-model E1 (Config fields), contract C1/C2, research D-1.

**Checkpoint**: both fields present, default-constructed `SessionConfig` yields `true`/`true`; existing build green.

---

## Phase 3: User Story 1 - Relax CompID matching for a compat counterparty (Priority: P1) 🎯 MVP

**Goal**: With `check_comp_id = false`, post-Logon inbound messages whose `SenderCompID(49)` / `TargetCompID(56)` do not equal the configured pair are accepted instead of disconnecting; `BeginString(8)`, the Logon-time CompID match, and the 013 authorization allow-list stay strict. (FR-001/002/003, FR-012)

**Independent Test**: configure a session, feed a steady-state inbound message whose `49`/`56` mismatch ⇒ accepted+delivered with the knob off, rejected/disconnected at default. (SC-001)

### Tests for User Story 1 (write RED first) ⚠️

- [X] T004 [US1] CompID witnesses in `tests/session/test_validation_compat_toggles.cpp` (write RED): `CompID_KnobOff_MismatchAccepted` (steady-state `49`/`56` mismatch ⇒ delivered, no reject/disconnect — SC-001 relaxed, C1.2), `CompID_Default_MismatchRejected` (identical frame at default ⇒ disconnect as today — SC-001 paired, C1.1), `CompID_KnobOff_MatchingPathUnchanged` (matching `49`/`56` ⇒ unchanged — US1 AS3), `CompID_KnobOff_BeginStringStillStrict` (mismatching `BeginString(8)` still disconnects with the knob off — I-VCT-1, C1.3, edge), `CompID_KnobOff_AuthzAllowListStillEnforced` (013 `compid_authorization_policy` non-allow-listed principal still refused at Logon with the knob off — I-VCT-2, SC-004, C1.3), `CompID_KnobOff_LogonTimeMismatchStillRefused` (Logon `49` ≠ configured `target_comp_id` still refused — steady-state-only, I-VCT-6, SC-007, FR-012). Confirm all FAIL before T005.

### Implementation for User Story 1

- [X] T005 [US1] Site S1 in `src/session/session.cpp` — the `case LogonReceived: case Active:` CompID/BeginString gate (`:1869-1874`): split the combined gate so the `begin_string` mismatch ALWAYS disconnects, but the two `49`/`56` equality clauses are guarded behind `cfg_.check_comp_id` (taken verbatim when `true`, skipped when `false`). Do NOT touch `interpret_logon`'s Logon-establishment CompID check, the 013 authz allow-list (`:1678`), or make `49`/`56` optional. — data-model S1, contract C1, FR-003, research D-2. Run T004 + the existing CompID/establishment regression ⇒ all green.

**Checkpoint**: US1 witnesses green; steady-state CompID mismatch tolerated only with the knob off; BeginString + Logon + authz still strict.

---

## Phase 4: User Story 2 - Tolerate out-of-order sequence numbers for a compat counterparty (Priority: P1)

**Goal**: With `validate_sequence_numbers = false`, a too-high `MsgSeqNum` emits no `ResendRequest` and a too-low does not disconnect — the frame is delivered (admin → `fromAdmin`, app → `fromApp`); the counter advances on exact match only; an inbound `SequenceReset(35=4)` never applies its `NewSeqNo`; PossDup handling and the `seq==0`/too-low-Heartbeat carve-outs are retained. (FR-004/005/006/013, FR-012)

**Independent Test**: configure a session with seqnum validation off, feed a too-high gap and a too-low replay ⇒ each accepted+processed, **zero `ResendRequest` on the wire**, no too-low disconnect; at default the same frames produce today's recovery/disconnect. (SC-002)

### Tests for User Story 2 (write RED first) ⚠️

- [X] T006 [US2] Ordinary-seqnum witnesses in `tests/session/test_validation_compat_toggles.cpp` (write RED): `Seq_KnobOff_TooHigh_NoResendRequest` (forward gap ⇒ delivered, zero `ResendRequest`, no AwaitingResend — SC-002, C2.2, I-VCT-3), `Seq_KnobOff_TooLow_NoDisconnect_Delivered` (too-low ⇒ delivered, session stays Active — SC-002, C2.2), `Seq_Default_OutOfOrder_RecoveryAndDisconnect` (paired: too-high ⇒ `ResendRequest`, too-low ⇒ disconnect as today — SC-002 paired, C2.1), `Seq_KnobOff_ExactMatchOnlyAdvance` (out-of-order frame does NOT move the counter; a following exact-expected frame advances it — SC-006, I-VCT-4, C2.3), `Seq_KnobOff_AdminVsAppFanOut` (out-of-order admin msgtype → `fromAdmin`, app msgtype → `fromApp`, via the existing `is_admin_msgtype` helper — C2.2, S4), `Seq_KnobOff_PossDupRetained` (PossDup(43)-flagged frame still flows through the 021 Stage-1/Stage-2 disposition — I-VCT-5, C2.4, FR-006), `Seq_KnobOff_SeqZeroStillFatal` (unparseable `MsgSeqNum`→0 still fatal — I-VCT-10, C2.5), `Seq_KnobOff_TooLowHeartbeatStillDropped` (too-low `Heartbeat(35=0)` still silently dropped at `:2234`, NOT delivered — N3 carve-out, C2.2), `Seq_KnobOff_LogonTimeTooHighStillRefused` (Logon-time too-high still takes the strict path — steady-state-only, I-VCT-6, SC-007, FR-012). Confirm all FAIL before T008.
- [X] T007 [US2] `SequenceReset(35=4)` witnesses in `tests/session/test_validation_compat_toggles.cpp` (write RED): `SeqReset_KnobOff_ResetMode_NewSeqNoNotApplied` (reset-mode `GapFillFlag≠Y`, S6 `:1966` ⇒ `apply_inbound_sequence_reset` NOT called, delivered to `fromAdmin`, counter **unchanged** — I-VCT-11, SC-008, C2.7), `SeqReset_KnobOff_GapfillOutOfOrder_NewSeqNoNotApplied` (gapfill-mode `123=Y` out-of-order, S7 `:2294` ⇒ `NewSeqNo` NOT applied, delivered to `fromAdmin`, counter **unchanged** — I-VCT-11, SC-008, C2.7), `SeqReset_KnobOff_GapfillExactMatch_AdvancesByOne_NewSeqNoNotApplied` (exact-match gapfill `35=4` passes `check_inbound` first ⇒ counter **+1** via the ordinary exact-match path, then S7 bypassed so `NewSeqNo` still NOT applied — the +1 is a fixpp ordering artifact, NOT QFJ-parity; SC-008 special case, I-VCT-11), `SeqReset_Default_NewSeqNoApplied` (paired strict: both modes apply `NewSeqNo` exactly as today — SC-008 paired, C2.7). Confirm all FAIL before T008.

### Implementation for User Story 2

- [X] T008 [US2] Site S2 in `src/session/session.cpp` — too-high arm (`:2037`): add `&& cfg_.validate_sequence_numbers` to the too-high guard so a `seq>next_expected` frame is NOT routed to AwaitingResend / `ResendRequest` when the knob is off (it falls through to S4). — data-model S2, contract C2.2, FR-006, research D-3.
- [X] T009 [US2] Site S4 in `src/session/session.cpp` — `check_inbound` failure Arm B (`:2273-2276`): when `!cfg_.validate_sequence_numbers`, replace the fatal `Disconnected` with deliver-without-advance — discriminate via the existing `is_admin_msgtype` helper (`src/session/msgtype_classifier.hpp:43`): admin → `fromAdmin`, app → `fromApp`; reuse `parse_and_dispatch_`; do NOT advance the counter; stay `Active`. This branch must catch BOTH too-low AND the too-high frame that fell through from S2. Preserve the pre-existing too-low `Heartbeat(35=0)` silent-drop at `:2234` (it never reaches this else) and the `seq==0` fatal (`:2017`). — data-model S4 + I-VCT-10, contract C2.2/C2.5, FR-006, research D-3. NB: the `:2273-2276` numbers drift — locate the insertion anchor by the `record_state_transition_(fsm_state::Disconnected)` / `co_return` pair that follows the Stage-2 Arm-A/Row-7 block inside the `check_inbound`-failure branch, NOT by the literal line.
- [X] T010 [US2] Site S6 in `src/session/session.cpp` — reset-mode `SequenceReset(35=4)` intercept (`:1966`, BEFORE the seqnum gate): when `!cfg_.validate_sequence_numbers`, BYPASS the intercept so `apply_inbound_sequence_reset` is NOT called — deliver the frame to `fromAdmin` without advancing/jumping the counter. Leave the shared `apply_inbound_sequence_reset` UNCHANGED (still used by the strict 013/024/027 paths). — data-model S6 + I-VCT-11, contract C2.7, FR-013.
- [X] T011 [US2] Site S7 in `src/session/session.cpp` — gapfill-mode `SequenceReset(35=4, 123=Y)` intercept (`:2294`, AFTER the seqnum gate): same knob gate — when `!cfg_.validate_sequence_numbers`, BYPASS so `NewSeqNo` is NOT applied; an out-of-order gapfill `35=4` is delivered-without-advance via `fromAdmin`; an exact-match gapfill that already advanced via S5 is still not `NewSeqNo`-applied. — data-model S7 + I-VCT-11, contract C2.7, FR-013. Confirm S3 PossDup (`:2087`/`:2243`) and S5 exact-match advance (`:2232`) remain UNTOUCHED. Run T006 + T007 ⇒ all green.

**Checkpoint**: US2 witnesses green; out-of-order tolerated only with the knob off; exact-match-only advance holds across ALL inbound paths (incl. `35=4`); PossDup + `seq==0` + too-low-Heartbeat carve-outs intact.

---

## Phase 5: User Story 3 - Default strict, byte-identical no-op (Priority: P1)

**Goal**: With both knobs at default (`true`), every inbound CompID match and sequence validation is byte-for-byte as today and the outbound wire is unchanged; the four knob combinations behave per their axes. (FR-007/008/009, SC-003/005)

**Independent Test**: run the full existing session/establishment/recovery/CompID/sequence regression suite at default config ⇒ every witness green; assert both new fields default to the strict value.

### Tests for User Story 3 (write RED first) ⚠️

- [X] T012 [US3] Default/combination/no-op witnesses in `tests/session/test_validation_compat_toggles.cpp` (write RED): `Default_FieldsAreStrict` (freshly default-constructed `SessionConfig` ⇒ `check_comp_id == true && validate_sequence_numbers == true` — US3 AS2), `Default_ByteIdenticalBaseline` (both default ⇒ a representative CompID-mismatch + out-of-order scenario produces byte-identical disposition + wire to the pre-feature baseline — SC-003, I-VCT-7, C3.3), `Combination_Matrix_FourCells` (both-default / CompID-only-off / seqnum-only-off / both-off each behave per FR-002/003/005/006 on the respective axis; the two knobs are independent — SC-005, FR-007, I-VCT-8, C3.1), `Inbound_Only_OutboundUnchanged` (neither knob is read on any outbound construction path; our own `49`/`56` + outgoing seqnums unchanged — FR-008, I-VCT-9, C3.2), `NoHeap_RelaxedDeliverPath` (the S4 deliver-without-advance path ⇒ no new allocation — `[const §VIII.5]`, plan IX.1; the BINDING no-heap gate is the **mallocnesia LD_PRELOAD interceptor** `tools/mallocnesia/libmallocnesia.so` wrapping the session binary — a PMR `counting_resource` alone is a false-pass since non-PMR `std::vector`/global-`new` escapes it, per [[feedback_tracking_pmr_resource_false_pass]]; precedent `tests/alloc_guard/decimal_alloc_guard_test.cpp`). Confirm the default/no-op assertions hold and the combination cells map to US1/US2 behaviour.

### Implementation / Verification for User Story 3

- [X] T013 [US3] Default-off regression gate: build + run the full existing session / establishment / recovery (013/021/024/027) / CompID / sequence regression suites at default config and confirm 100% green + byte-identical to the pre-feature baseline (SC-003, FR-009). No production code change expected here — US3 is the zero-regression guarantee that US1/US2 (default-true guards) already satisfy; if any baseline witness moved, fix the guard that caused it.

**Checkpoint**: both fields default strict; default-off byte-identical; all four combinations correct; all three stories independently functional.

---

## Phase 6: Polish & Cross-Cutting Concerns

- [X] T014 [US2] Live interop cell `tests/interop/happy/hp_fix44_validation_compat_test.cpp` (both roles): fixpp with each knob relaxed vs a QFJ/QFcpp peer (in the parent `phase-9-harness/`) configured `CheckCompID=N` / `ValidateSequenceNumbers=N`, driving mismatching CompIDs + out-of-order seqnums ⇒ accepted, no reject/disconnect, no `ResendRequest`; skip-without-counterparty. — contract C1/C2, SC-001/002, `[const §VII.6]`.
  - Sub-deliverable: configure the QFJ/QFcpp counterparty in the parent `phase-9-harness/` with `CheckCompID=N` / `ValidateSequenceNumbers=N` — a harness-side config delta (cross-repo), NOT a fixpp source change.
  - Acceptance (counterparty-PRESENT path — the skip guard must not make a zero-assertion cell "pass"): assert ≥1 mismatching-CompID / out-of-order frame is accepted+delivered, assert **zero `ResendRequest` observed on the wire**, and assert the session stays `Active` (no Logout/disconnect) for the run's duration — mirror the in-cell assertion pattern of `tests/interop/happy/hp_fix44_next_expected_test.cpp`. The skip-without-counterparty guard covers only counterparty-ABSENT; it must never short-circuit these assertions when the peer is up.
- [X] T015 [P] `spec/feature-catalogue.md`: add **`S-040`** (`CheckCompID — skip steady-state SenderCompID/TargetCompID match; QuickFIX-compat`) and **`S-041`** (`ValidateSequenceNumbers — accept out-of-order inbound without gap-fill recovery; QuickFIX-compat`), both status `done` (FIX 4.4), cite `028-validation-compat-toggles`, evidence_pr `(pending merge)`, Tests `tests/session/test_validation_compat_toggles.cpp` + the interop cell. Normative refs per plan §VI (QuickFIX `CheckCompID` / `ValidateSequenceNumbers` + `[FIX-SL §4.2.2]` / `[FIX-SL §4.8]`). — plan §VI delta.
- [X] T016 [P] `spec/coverage-index.md`: add the two coverage entries for S-040 (`§4.2.2`) and S-041 (`§4.8` / `§4.8.2` / `§4.8.5` / `§4.8.6`) as an exact-set diff. — plan §VI delta, [[feedback_completeness_gate_exact_set_not_subset]].
- [X] T017 [P] `spec/behaviors-and-limitations.md`: add **B-028-1** (CompID-check knob: steady-state `49`/`56` match skipped when off; BeginString/Logon/013-authz still strict; default byte-identical), **B-028-2** (seqnum knob: out-of-order tolerated, no `ResendRequest`/no too-low-disconnect, exact-match-only advance, `35=4` `NewSeqNo` not applied; PossDup + `seq==0` + too-low-Heartbeat retained; default byte-identical), **L-028-1** (`validate_sequence_numbers=false` disables gap detection — real gaps silently accepted, possible out-of-order processing), **L-028-2** (`check_comp_id=false` removes the steady-state mis-routing guard — rely on 013 authz + transport binding), **L-028-3** (steady-state only — Logon establishment unaffected by either knob). — plan §VI delta, research D-7.
- [X] T018 Run `quickstart.md` validation: confirm each quickstart scenario (relax CompID / tolerate out-of-order / combine / caveats) maps 1:1 to a landed witness; reconcile any drifted witness names back into quickstart + data-model.
- [X] T019 Feature-completeness audit (Gate B precondition, [[feedback_feature_completeness_gate]]): every tasks.md row `[X]` or waived; every FR-001..013 and SC-001..008 maps to a landed test AND landed impl; S-040/S-041 catalogue rows + coverage-index entries consistent (exact-set); record the result in `.specify/decisions/028-validation-compat-toggles-completeness.md`.

---

## Dependencies & Execution Order

- **Setup (T001–T002)**: no dependencies — start immediately; T001 ‖ T002 (different files).
- **Foundational (T003)**: BLOCKS US1 and US2 (both sites read `cfg_.check_comp_id` / `cfg_.validate_sequence_numbers`).
- **US1 (T004–T005)**: after Foundational. T004 (RED) before T005 (impl). Single `session.cpp` site (S1).
- **US2 (T006–T011)**: after Foundational. T006 + T007 (RED) before the impl tasks; **independent of US1** (disjoint sites). T008–T011 all edit `session.cpp` ⇒ sequential (not mutually [P]); order S2→S4→S6→S7 (S2 must guard-off before S4 catches the fallen-through too-high frame).
- **US3 (T012–T013)**: after US1 AND US2 (audits both knobs' default-true byte-identity + the 4-combination matrix).
- **Polish (T014–T019)**: after all stories. T015/T016/T017 [P] (distinct doc files). T019 (completeness audit) **after T015 AND T016 AND T017** (and after T014/T018) — it audits the S-040/S-041 catalogue rows + coverage-index entries as an exact-set, so the §VI delta MUST already be landed or T019 audits a pre-delta baseline and passes against the wrong set.

## Parallel Opportunities

- T001 ‖ T002 (Setup, different files).
- US1 (T004–T005) ‖ US2 (T006–T011) once Foundational lands — disjoint sites, but each story's own tests-before-impl ordering holds, and within US2 the four `session.cpp` impl edits are sequential.
- T015 ‖ T016 ‖ T017 (Polish docs, different files).

## Implementation Strategy

**MVP = Setup + Foundational + US1 + US2.** Unlike a single-headline feature, the two relaxations are co-equal P1 (each is one of the two requested knobs) and independently valuable; either alone is a shippable increment. US3 is the zero-regression guarantee that the default-true guards already satisfy.

1. Setup → Foundational (the two default-true bools).
2. US1 RED → impl (S1 CompID gate split) → **STOP & VALIDATE** (mismatch tolerated only with the knob off; BeginString/Logon/authz strict).
3. US2 RED → impl (S2 guard, S4 deliver-without-advance, S6/S7 `35=4` gates) → **STOP & VALIDATE** (zero `ResendRequest`, no too-low disconnect, exact-match-only advance, `NewSeqNo` not applied off).
4. US3 (default fields strict + 4-combination matrix + full-regression byte-identity + no-heap).
5. Polish (interop cell + catalogue S-040/S-041 / coverage / B&L + quickstart + completeness audit).

## Notes

- The data-model invariants I-VCT-1..11 + spec SC-001..008 are the authoritative witness list; T004 (CompID), T006/T007 (seqnum), T012 (default/combo) cover all of them + T014 the live cell.
- Exact-match advance holds across ALL inbound paths only because S6/S7 are ALSO gated (the `35=4` family otherwise advances outside the seqnum gate via `apply_inbound_sequence_reset`); do not assume `check_inbound` alone (S5) carries it (I-VCT-4 ↔ I-VCT-11).
- The S4 deliver-without-advance branch must catch BOTH too-low AND the too-high frame that fell through from the S2 guard — without advancing or disconnecting (plan Complexity Tracking hazard 1).
- The CompID split must keep `BeginString(8)` strict and must NOT touch the Logon-establishment CompID check or the 013 authz allow-list (plan Complexity Tracking hazard 2; I-VCT-1/I-VCT-2).
- Default-true ⇒ each guard takes the existing strict branch verbatim; no wire delta, no new alloc/suspension (I-VCT-7). After code-changing tasks: `codegraph sync` (per project CLAUDE.md); commit after each task or logical group.
