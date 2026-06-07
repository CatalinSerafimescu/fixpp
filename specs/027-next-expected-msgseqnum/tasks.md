# Tasks: NextExpectedMsgSeqNum(789) fast session resume (G3 slice)

**Input**: Design documents from `specs/027-next-expected-msgseqnum/`
**Prerequisites**: plan.md, spec.md, research.md (D-0..D-12), data-model.md (I-NEX-1..12 + Reset table + RED-witness table), contracts/next-expected-msgseqnum.md (C1..C10)
**Branch**: `027-next-expected-msgseqnum` | repository root = library submodule

**Tests**: REQUIRED (TDD/RED-first, `[const §VII]`). The data-model RED-witness table is the authoritative test list; each test task below names its witnesses. Write each cluster RED (failing) before its implementation.

## Format: `[ID] [P?] [Story] Description`

- **[P]** = parallelizable (different file, no incomplete-task dependency). Witnesses in the same shared file (`tests/session/test_next_expected_msgseqnum.cpp`) are NOT mutually [P].
- **[Story]** = US1 / US2 / US3 (Setup, Foundational, Polish carry no story label).

---

## Phase 1: Setup (shared infrastructure)

**Purpose**: Test scaffolding + ctest target registration for the two new test files.

- [X] T001 [P] Create `tests/session/test_next_expected_msgseqnum.cpp` skeleton (GoogleTest, feature fixtures reusing the existing session-test harness) and register the ctest target `session_next_expected_msgseqnum` in `tests/session/CMakeLists.txt`.
- [X] T002 [P] Create `tests/interop/happy/hp_fix44_next_expected_test.cpp` skeleton (skip-without-counterparty guard) and register its interop cell in the `tests/interop` CMake wiring per the 016/018 harness pattern.

**Checkpoint**: both targets configure and build (empty/skipped); ctest discovers them.

---

## Phase 2: Foundational (blocking prerequisites — MUST complete before US1)

**Purpose**: The config knob + the `replay_outbound_range_` walk extraction. The extraction is shared infrastructure: the existing 013 ResendRequest handler and the new 789 honor path both call it, and it must not regress 013. ⚠️ No story work begins until this phase is complete.

- [X] T003 [P] Add additive `bool enable_next_expected_msg_seq_num = false;` to `include/fixpp/session/session_config.hpp` (primitive, explicit default-off per `[const §XII.5]`; no new include, §XV.9 N/A) — data-model E1, contract C1.
- [X] T004 Write RED walk-extraction safety witnesses in `tests/session/test_next_expected_msgseqnum.cpp`: `WalkExtraction_SingleImplementation` (one store-walk body; ResendRequest handler + future 789 path both call the helper) and the **ResendRequest-caller** halves of `WalkExtraction_TwoValueEnd_ExplicitEndBeyondStore` (request `[10,20]`, store through 5 ⇒ GapFill `NewSeqNo=21`, not 6) and `WalkExtraction_TwoValueEnd_EndSeqNo0_EmptyStore` (`EndSeqNo=0`/through-current empty store ⇒ `NewSeqNo=peek_outbound()`) — research D-5, contract C3, I-NEX-3. NOTE (analyze F2): the two `WalkExtraction_TwoValueEnd_*` data-model witnesses are intentionally **split by caller** — the ResendRequest-caller half lands here (Foundational); the 789-caller half lands in T008 (US1). Foundational is "done" when the ResendRequest-caller halves + single-implementation are green; the 789-caller halves complete in US1.
- [X] T005 Extract `asio::awaitable<expected_t<void>> replay_outbound_range_(seqnum_t begin, seqnum_t requested_end, bool end_is_through_current)` from the inline `:2485-2635` ResendRequest-reply block in `src/session/session.cpp` (declare in `src/session/session.hpp`). PRESERVE the two-value end model: helper owns `eff_end = (end_is_through_current || requested_end > our_last) ? our_last : requested_end` and the GapFill `NewSeqNo = end_is_through_current ? peek_outbound() : requested_end+1`; move the two lambdas + `gapfill_callback_threw` (surfaced via `expected_t`/`app_callback_threw`) + `our_last` clamp + empty-store/per-slot/trailing-flush inside; **re-home the embedded `record_state_transition_(Disconnected)` early-returns to the CALLER** — research D-5, contract C3.
- [X] T006 Repoint the existing ResendRequest handler to call `replay_outbound_range_(begin=rr_begin, requested_end=rr_end, end_is_through_current=(rr_end==0))` and own its `Disconnected` disposition on `unexpected`. Build + run the 013/021 recovery regression suite and T004 witnesses; confirm zero regression (single-implementation + two-value-end ResendRequest-caller green).

**Checkpoint**: one walk implementation, ResendRequest path behaviour-preserved, knob field present.

---

## Phase 3: User Story 1 - Fast bidirectional resume after a gap (Priority: P1) 🎯 MVP

**Goal**: Both peers advertise next-expected-inbound in Logon and proactively resend exactly the peer's missing range within the Logon exchange — no `ResendRequest` round-trip. (FR-001/002/003/004/007/008)

**Independent Test**: two knob-on sessions, force a gap, reconnect ⇒ all missed messages delivered in order, zero `ResendRequest` on the wire, session reaches established. (SC-001/003)

### Tests for User Story 1 (write RED first) ⚠️

- [X] T007 [US1] Emit witnesses in `tests/session/test_next_expected_msgseqnum.cpp`: `Emit_Initiator_AdvertisesNextExpectedInbound`, `Emit_AcceptorReply_AdvertisesNextInboundNoPlusOne` (acceptor reply 789 == post-`check_inbound` `next_inbound_unsafe()`, no `+1`, == peer's actual next send) — I-NEX-1, E-OBO.
- [X] T008 [US1] Honor-resume witnesses (same file): `Honor_Acceptor_XltN_ResendsExactRange_AfterReply_NoResendRequest` (resend `[X,N-1]` AFTER reply emit `:1766`, PossDup app + GapFill admin, zero ResendRequest), `Honor_Initiator_XltN_ResendsExactRange_NoResendRequest`, `Honor_XeqN_NoResend`, and the **789-caller** halves of `WalkExtraction_TwoValueEnd_ExplicitEndBeyondStore` / `WalkExtraction_TwoValueEnd_EndSeqNo0_EmptyStore` (789 caller passes `requested_end=N-1, through_current=true`) — I-NEX-2/3, RC#4.
- [X] T009 [US1] Behind-side / bidirectional / self-heal witnesses (same file): `BehindSide_KnobOn_AdmitsPeerResend_NoFatalDisconnect` (knob on + peer Logon MsgSeqNum too-high ⇒ NOT fatal; `next_inbound_` left at X, no `set_next_inbound`; admits `[X,peer_N-1]` in-sequence; **final `next_inbound_ == peer_N`** with `peer_N` set ≥1 frame beyond the Logon so it diverges from `X_logon+1`; no at-logon ResendRequest, both roles), `Bidirectional_BothGaps_RecoverNoDoubleRecovery`, `LostResend_SelfHealsViaActiveArm` — I-NEX-5/10/12, D-7/11/12.
- [X] T010 [US1] Suppression + reset-table + no-heap witnesses (same file): `Suppression_KnobOn_NoAtLogonResendRequest_KnobOff_Yes` (013 regression guard), `Reset_InitiatorResetLogon_Advertises1`, `Reset_AcceptorReplyResetOnLogon_Advertises2`, `Reset_AcceptorReplyReceived141_Advertises1`, `NoHeap_Emit789Append` (mallocnesia markers bracket the synchronous `build_logon` call with `next_expected_seq` set; proactive resend no-heap inherited from the recovery alloc-guard witness on `replay_outbound_range_`) — I-NEX-5/8, D-8, `[const §VIII.5]`.

### Implementation for User Story 1

- [X] T011 [US1] Header capture in `src/session/session.cpp`: add `std::string_view next_expected_msg_seq_num;` to the `FrameHeader` struct (`:1152`) and a `case 789:` to the `scan_frame_header` switch (`:1213`); `interpret_logon` unchanged (tolerates the optional field) — data-model E3, D-1/D-3.
- [X] T012 [P] [US1] `build_logon` in `include/fixpp/session/admin_messages.hpp` + `src/session/admin_messages.cpp`: add `std::optional<seqnum_t> next_expected_seq = std::nullopt`; when present append `789=<value>` (`render_u32` + `w.append_raw`) after the `141` block (`:150-155`) — contract C2, D-3.
- [X] T013 [US1] Emit call sites in `src/session/session.cpp`: initiator `emit_initiator_logon_` (`:601`) and acceptor reply (`:1745`) pass `next_expected_seq = seqnum_mgr_.next_inbound_unsafe()` (plain, NO `+1`) only when the knob is on — I-NEX-1, E-OBO (acceptor reply is post-`check_inbound` `:1571`).
- [X] T014 [US1] Acceptor honor in the `NotConnected` handler (`:1508`): parse `X = parse_seqnum(789)`, `N = peek_outbound()`; route invalid-X (==0) and X>N to the integrity-error disposition (C6, implemented in US3 — interim-safe: route to the existing fatal disconnect until T021 lands), `X<N` ⇒ `replay_outbound_range_(X, N-1, /*through_current=*/true)` AFTER the reply `store_then_emit` (`:1766`, RC#4 ordering), `X==N` ⇒ no resend — contract C4, I-NEX-2/3.
- [X] T015 [US1] Initiator honor in the `LogonSent` handler (`:2755`): symmetric decision on the peer's Logon-ack 789; `X<N` ⇒ `replay_outbound_range_` after processing the ack (own Logon already sent @ `:601`) — contract C4/C8.
- [X] T016 [US1] Behind-side tolerance in BOTH handlers (`src/session/session.cpp`): when the knob is on AND the peer's Logon `MsgSeqNum` is too-high, do NOT take the fatal `check_inbound` path (acceptor `:1571`, initiator `:2842`); formulation A — leave `next_inbound_` at X (no `set_next_inbound`), emit no at-logon `ResendRequest`, proceed to Active so the peer's `[X,peer_N-1]` resend is admitted in-sequence to `peer_N` — contract C5, I-NEX-5/12, D-7 (NOT `set_next_inbound(X_logon+1)`).
- [X] T017 [US1] Active too-high arm (`:1968-2009`): **confirm/review step** — verify the arm is NOT the primary 789 honor site and stays active as recovery-of-last-resort (knob-off path byte-identical). Deliverable is a one-line code-comment annotation at `:1968` (no behavioural change); touches `session.cpp` so non-[P] with T011/T013–T016 — research D-11, I-NEX-10.

**Checkpoint**: US1 witnesses (T007–T010) green; gap recovers within the Logon exchange with zero `ResendRequest`, both roles.

---

## Phase 4: User Story 2 - Default off, byte-identical no-op (Priority: P1)

**Goal**: Knob default-off ⇒ outbound Logon byte-identical to baseline (no 789), inbound 789 ignored, existing `ResendRequest` recovery (013) untouched. (FR-006/SC-002)

**Independent Test**: default config ⇒ captured Logon byte-identical to pre-feature baseline; inbound Logon carrying 789 recovers via the existing `ResendRequest` path.

### Tests for User Story 2 (write RED first) ⚠️

- [X] T018 [US2] `DefaultOff_ByteIdenticalLogon_InboundIgnored` in `tests/session/test_next_expected_msgseqnum.cpp`: knob off ⇒ no 789 emitted (byte-identical Logon) + inbound 789 ignored + ResendRequest still used — I-NEX-7, contract C9.

### Implementation for User Story 2

- [X] T019 [US2] Audit every emit/honor/suppression/behind-side site (T013–T016) for the knob guard so the default-off path is a pure no-op. **Completion criterion (analyze C1, measurable):** T018 (`DefaultOff_ByteIdenticalLogon_InboundIgnored`) is green AND the full existing session/recovery/logon regression suite (013/021) passes unchanged. No new witness required — this is a guard-completeness audit, not new behaviour — FR-006, SC-002.

**Checkpoint**: US2 witness green; full regression suite green; outbound Logon byte-identical when off.

---

## Phase 5: User Story 3 - Sequence-integrity error on impossible expectation (Priority: P2)

**Goal**: An inbound 789 that exceeds our next-outbound (or is present-but-unparseable) is a sequence-integrity violation ⇒ `Logout`(text)+disconnect, never a silent accept. (FR-005)

**Independent Test**: feed an inbound Logon with `789 > N` (and separately a malformed 789) ⇒ session sends `Logout` with explanatory text then disconnects; does not enter established.

### Tests for User Story 3 (write RED first) ⚠️

- [X] T020 [US3] Integrity witnesses in `tests/session/test_next_expected_msgseqnum.cpp`: `Honor_XgtN_LogoutTextThenDisconnect` (both roles) and `Honor_Invalid789_LogoutThenDisconnect` (`789=` empty / `789=abc` / overflow ⇒ Logout+disconnect, NO `[1,N-1]` replay) — I-NEX-4/9, contract C6, D-6/D-10.

### Implementation for User Story 3

- [X] T021 [US3] Integrity-error disposition in BOTH handlers (`src/session/session.cpp`): invalid-X (parse→0) evaluated FIRST (before the `X<N` compare, closes the `[1,N-1]` amplification) and X>N ⇒ `build_logout("NextExpectedMsgSeqNum too high, expecting N but received X"` / `"… invalid")` then disconnect; replace the T014/T015 interim-safe routing — contract C6, I-NEX-4/9, D-6/D-10.

**Checkpoint**: US3 witnesses green; all three stories independently functional.

---

## Phase 6: Polish & Cross-Cutting Concerns

- [ ] T022 [US1] Live interop cell `tests/interop/happy/hp_fix44_next_expected_test.cpp` (both roles): fixpp + a `EnableNextExpectedMsgSeqNum` counterparty (QFcpp/QFJ in the parent `phase-9-harness/`) recover an at-logon gap with zero `ResendRequest` on the wire + a bidirectional-gap cell; skip-without-counterparty — contract C10, SC-005, `[const §VII.6]`.
  - Sub-deliverable (analyze C2): configure the QFcpp/QFJ counterparty in the parent `phase-9-harness/` with `EnableNextExpectedMsgSeqNum=Y` — a harness-side config delta (cross-repo), NOT a fixpp source change.
- [ ] T023 [P] `spec/feature-catalogue.md`: mark **S-031** (`:344`) `backlog → implementation-parity-4.4` (NOT `done`; FIXT.1.1/5.0SP2 outstanding to G4), cite `027-next-expected-msgseqnum` with the "FIX 4.4 parity; FIXT/5.0 deferred to G4" gap-note, fill evidence_pr `(pending merge)` + Tests; append B-027-1 — plan §VI delta.
- [ ] T024 [P] `spec/coverage-index.md`: add the `§4.4.1` coverage entry for S-031 with the "4.4-parity, FIXT/5.0 outstanding" qualifier on the §4.4 / §4.4.1 / §4.7.1 rows (exact-set diff) — plan §VI delta, [[feedback_completeness_gate_exact_set_not_subset]].
- [ ] T025 [P] `spec/behaviors-and-limitations.md`: B-027-1 (789 advertise/honor/proactive-resend; X>N or invalid ⇒ Logout+disconnect; default off byte-identical; FIX 4.4 only), L-027-1 (both-peers-required, NO automatic ResendRequest fallback), L-027-2 (lost proactive resend self-heals via the Active arm) — plan §VI delta.
- [ ] T026 Extend the fuzz harness for the new `case 789:` parser arm (analyze E1; `[const §VII] item 7` — new parser-touching code without a fuzz harness is a Gate B blocker). The new arm is in `scan_frame_header`, already driven by `tests/fuzz/fuzz_session_recovery_admin_parse.cpp` (`Session::on_inbound_frame()` → Framer → header scan), so this is a **seed/corpus extension, NOT a new harness**: add a `789`-bearing Logon to that harness's preamble/corpus (incl. malformed `789=`/`789=abc`/overflow to exercise the `parse_seqnum→0` invalid path) and confirm the 789 arm is reached. Depends on T011. Verify via `/speckit-verify` fuzz smoke (≥10 min on the full gate).
- [ ] T027 Run `quickstart.md` validation (the scenario→witness table maps 1:1 to the landed tests).
- [ ] T028 Feature-completeness audit (Gate B precondition, [[feedback_feature_completeness_gate]]): every tasks.md row `[X]` or waived; every FR-001..010 and SC-001..005 maps to a landed test AND landed impl; S-031 catalogue row + coverage-index entry consistent (exact-set). **Also (analyze E2):** assess whether an asymmetric-peer witness is wanted — knob-on, behind side, peer sends NO 789 ⇒ still no at-logon ResendRequest (FR-004 suppression is unconditional), gap detected on the first Active frame. The spec is internally consistent (FR-004/FR-009) without it; add the witness or record it as a deliberate non-gap. Record 100%-or-waived.

---

## Dependencies & Execution Order

- **Setup (T001–T002)**: no dependencies — start immediately.
- **Foundational (T003–T006)**: T003 [P] with T004; T005 depends on T004 (RED); T006 depends on T005. BLOCKS all user stories (the honor path reuses `replay_outbound_range_`).
- **US1 (T007–T017)**: after Foundational. Tests T007–T010 before impl T011–T017. T011 + T013–T017 share `session.cpp` (sequential); T012 [P] (admin_messages). T014/T015 route integrity cases to an interim-safe disconnect until US3 T021.
- **US2 (T018–T019)**: after US1 (audits US1's knob guards); independently testable.
- **US3 (T020–T021)**: after US1 (replaces the interim-safe integrity routing in T014/T015); independently testable.
- **Polish (T022–T028)**: after all stories. T023/T024/T025 [P] (distinct doc files). T026 (fuzz seed) depends on T011. T028 (completeness audit) last — audits everything incl. the §VI delta + fuzz.

## Parallel Opportunities

- T001 ‖ T002 (Setup, different files).
- T003 ‖ T004 (Foundational, config header vs test file).
- T012 ‖ the `session.cpp` impl tasks within US1 (admin_messages is a different file).
- T023 ‖ T024 ‖ T025 (Polish docs, different files).

## Implementation Strategy

**MVP = Setup + Foundational + US1.** That alone delivers the feature's whole reason to exist (zero-round-trip resume) and is independently demoable. US2 (regression guarantee) and US3 (P2 integrity guard) are incremental hardening on the same honor switch.

1. Setup → Foundational (walk extraction behaviour-preserving, 013 green).
2. US1 RED → impl → **STOP & VALIDATE** (zero ResendRequest, both roles).
3. US2 (default-off byte-identity + full regression).
4. US3 (integrity disposition, replaces interim-safe routing).
5. Polish (interop cell + catalogue/coverage/B&L + fuzz seed extension + completeness audit).

## Notes

- The data-model RED-witness table is authoritative; T007–T010, T018, T020 cover all 19 unit witnesses + T022 the live cell.
- Watch the two-counter trap (I-NEX-11): the 789 compare uses `peek_outbound()` (OUTBOUND); never compare X against `next_inbound_unsafe()`.
- The behind-side counter target is `peer_N` reached by in-sequence admission — never `set_next_inbound(X_logon+1)` (I-NEX-5/12).
- After code-changing tasks: `codegraph sync` (per project CLAUDE.md); commit after each task or logical group.
