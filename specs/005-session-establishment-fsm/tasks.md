---
description: "Task list — 005-session-establishment-fsm"
---

# Tasks: Session Establishment & FSM Core

**Input**: Design documents from `specs/005-session-establishment-fsm/`
**Prerequisites**: plan.md, spec.md, research.md (D-1..D-13), data-model.md (E1..E9 + transition matrix + error slots), quickstart.md, contracts/

**Tests**: INCLUDED — TDD red-green-refactor is constitutionally mandated (`[const §VII.1]`/`[const §VII.3]`) and the `[FIX-TC]` conformance subset is a feature requirement (FR-018 / SC-008). Every test task is written FIRST and must FAIL before the paired implementation task.

**Organization**: By user story (spec.md US1–US5). Setup + Foundational + Polish carry no story label. All paths are relative to the library submodule root (`research/G19-fix-fpml-iso20022/library/`).

**Gate state**: Gate A converged round 3 (2026-05-18, user-signed-off). `/speckit-analyze` runs after this `/speckit-tasks`, before `/speckit-implement`.

## Format: `[ID] [P?] [Story?] Description`

- **[P]**: parallelizable (different files, no dependency on an incomplete task)
- **[Story]**: US1–US5 (user-story phases only)

## Path Conventions

C++23 single-project library, `session/` module (first feature). Headers under `include/fixpp/session/` + the folded `include/fixpp/core/fix_time.hpp`; out-of-line bodies under `src/session/` + `src/core/`; tests under `tests/session/` (+ `tests/session/conformance/`, `tests/support/`); benches under `bench/session/`.

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: `session/` module skeleton + build wiring (reuse the merged 001–004 pattern; no new Conan row, `[const §III.2]`).

- [X] T001 Create the `session/` module tree per plan.md Project Structure: `include/fixpp/session/`, `src/session/`, `tests/session/`, `tests/session/conformance/`, `tests/support/`, `bench/session/`, `bench/baselines/session/`
- [X] T002 [P] Add the `fixpp_session` CMake target in `src/session/CMakeLists.txt` and wire it into the top-level build; link `fixpp_core` (incl. new `core/fix_time`), `fixpp_sync` (`async_mutex`), `fixpp_wire`, `fixpp_dict`, and the `[2e §4.1]` `message_store.hpp` seam header — no new Conan dependency
- [X] T003 [P] Add `tests/session/CMakeLists.txt` (GoogleTest + GoogleMock) including the `conformance/` subdir, and `bench/session` wiring (Google Benchmark) with a `bench/baselines/session/` baseline directory
- [X] T004 [P] Extend the Tier-1 static-analysis config (clang-tidy / clang-format / cppcheck / IWYU + the `[const §XV.9]` `std::mutex`-in-`asio::awaitable`-header grep gate) to cover `include/fixpp/session/*`, `src/session/*`, `include/fixpp/core/fix_time.hpp`, `src/core/fix_time.cpp`

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Shared primitives every user story depends on — the published `seqnum_t` + `[2e §10 Q9]` handoff, the `session_*` error slots, the folded time-helper #4, the 6-state FSM scaffold, and the test doubles.

**⚠️ CRITICAL**: No user-story work begins until this phase is complete.

- [X] T005 [P] Pin and append the `session_*` `fixpp::core::error` variants at slots **43..53** in `include/fixpp/core/error.hpp` exactly per data-model.md "Error mapping" (non-renumbering, `[const §X.4]`), plus the cross-doc-coordinated `[2d §6.5]/[2d §6.7]` minimal set (`session_already_closed`, `dispatch_aborted`, `clock_sleeps_cancelled`) at the next contiguous slots **54..N**; reuse the existing `[2e §6.7] store_seqnum_overflow` (no duplicate). Document `FIXPP_ERR_SESSION_*` as the 2i-owned C-ABI coalescing target
- [X] T006 [P] Add `include/fixpp/session/errors.hpp` — `session_*` helper wrappers over `fixpp::core::error` (no C++ type crosses the C ABI, `[const §X.2]`)
- [X] T007 [P] Write seam #13 `tests/session/seqnum_t_handoff_test.cpp` (build/consumer check: `2e`'s `#include <fixpp/session/seqnum.hpp>` resolves to the 005-owned type — SC-010, I-10) — must FAIL until T008
- [X] T008 Promote `include/fixpp/session/seqnum.hpp` to 005-owned (D-1/E4): delete the `// PLACEHOLDER` block, define `using seqnum_t = std::uint32_t;`, `seqnum_min = 1`, `seqnum_max = numeric_limits<uint32_t>::max()`, byte-identical to the placeholder so every `2e` include resolves unchanged — turns T007 green
- [X] T009 [P] Write seam #9 `tests/session/fix_time_roundtrip_test.cpp` — format↔parse lossless over an epoch / leap-second-adjacent / sub-second corpus at ms and µs precision (FR-012, SC-007/010, I-6) — must FAIL until T010+T011
- [X] T010 [P] Add `include/fixpp/core/fix_time.hpp` (D-3/E8): reuse `[2d §4.1]` `fixpp::core::utc_time_point`, add `fixpp::core::duration`; declare `utc_time_to_fix_string(utc_time_point, precision) → fixed buffer` and `fix_string_to_utc_time(span<const char>) → expected_t<utc_time_point>`
- [X] T011 Implement `src/core/fix_time.cpp` — the FIX UTCTimestamp grammar `YYYYMMDD-HH:MM:SS[.sss[sss]]` (ms default, µs where the version permits, never coarser than seconds) — turns seam #9 green (depends on T009, T010)
- [X] T012 [P] Add `include/fixpp/session/session_fsm.hpp` — `enum class fsm_state : std::uint8_t` with the **6** states `NotConnected/LogonSent/LogonReceived/Active/LogoutSent/Disconnected` (NO `RecoveryPending`, D-2/E2) + the transition-table type scaffold
- [X] T013 [P] Add `tests/support/transport_double.hpp` (seam #0) — in-memory bidirectional frame transport (feeds verified inbound frames, captures outbound)
- [X] T014 [P] Add `tests/support/store_double.hpp` (seam #0) — in-memory test-double satisfying the `[2e §4.1]` 4-pure-virtual `MessageStore` seam (NOT the S-012 production impl); reuse `[2d §4.3]` `mock_clock` from `tests/support/`
- [X] T015 [P] Confirm `include/fixpp/session/message_store.hpp` is consumed-not-redefined (2e-owned seam) and that `store_double.hpp` conforms to the 4-pv contract (`store`/`retrieve`/`next_seqnum`/`reset` + `retrieve_visitor`) — D-4/E6
- [X] T016 Add `include/fixpp/session/session.hpp` scaffold — `Session` lifecycle decls: `open()`, `close(graceful|terminal)`, `fromAdmin`/`fromApp`, the documented send path, with the per-session-strand reentrancy contract documented per entry point (`[const §X.5]`, FR-016)
- [X] T017 [P] Add decl scaffolds `include/fixpp/session/admin_messages.hpp`, `heartbeat.hpp`, `sending_time.hpp` (signatures only; bodies land per story)
- [X] T018 Add `src/session/session.cpp` skeleton + `src/session/{seqnum_manager,admin_messages,heartbeat}.cpp` stubs so the target compiles green before story work

**Checkpoint**: Primitives published, FSM scaffold + test doubles in place — user stories can proceed.

---

## Phase 3: User Story 1 - Establish a FIX session via the Logon handshake (Priority: P1) 🎯 MVP

**Goal**: Initiator and acceptor reach `Active` via a spec-conformant `Logon` handshake with `BeginString`/CompID validation and `HeartBtInt` negotiation.

**Independent Test**: Drive initiator and acceptor FSMs against each other through the transport double + mock clock; assert both reach `Active`, `HeartBtInt` matches the spec rule, `fromAdmin` delivers the peer `Logon`; mismatched `BeginString`/CompID or a non-`Logon` first message never reaches `Active`.

- [X] T019 [P] [US1] Write seam #2 `tests/session/logon_handshake_test.cpp` — initiator↔acceptor reach `Active`; `HeartBtInt` negotiation; `MsgSeqNum=1` + `SendingTime` on the emitted Logon; BeginString/CompID/first-msg-not-Logon refusals (SC-001/002) — must FAIL first
- [X] T020 [P] [US1] Write conformance `tests/session/conformance/tc_establishment_test.cpp` — parameterized over the fix42/fix44 `.def` oracle: `1a_ValidLogonWithCorrectMsgSeqNum`, `1b_DuplicateIdentity`, `1c_InvalidSenderCompID`, `1c_InvalidTargetCompID`, `1d_InvalidLogonWrongBeginString`, `2i_BeginStringValueUnexpected`, `2k_CompIDDoesNotMatchProfile`, `1e_NotLogonMessage` (D-10) — must FAIL first
- [X] T021 [US1] Implement Logon build/interpret in `src/session/admin_messages.cpp` over `wire::Writer` + typed `dictionary/` access — `HeartBtInt(108)`, `SenderCompID(49)`/`TargetCompID(56)` (point-to-point S-016 49/56 only), `BeginString(8)` (FR-002/003/004)
- [X] T022 [US1] Implement `effective_clock = clock_override ?: EngineConfig::clock` resolution once at `Session::open` (`[2d §7.9]`, NFR-015) and outbound `SendingTime(52)` stamping via `core/fix_time` in `src/session/session.cpp` (FR-011)
- [X] T023 [US1] Implement the initiator path `NotConnected → LogonSent` (emit Logon, seq=1, SendingTime) in `src/session/session.cpp` (data-model matrix)
- [X] T024 [US1] Implement the acceptor path `NotConnected → LogonReceived → Active` with `HeartBtInt` negotiation and `fromAdmin` delivery of the peer Logon (FR-002, US1#2)
- [X] T025 [US1] Implement Logon refusal — `BeginString`/CompID gate + first-message-not-`Logon` (`[FIX-SL §4.3]`) → `session_invalid_logon`/`session_compid_mismatch`/`session_begin_string_unsupported`, FSM never enters `Active` (FR-003/004, US1#3/#4)
- [X] T026 [US1] Make seam #2 + `tc_establishment` green; refactor under the noexcept-window discipline (SC-001/002)

**Checkpoint**: A session can be established and demonstrated end-to-end (MVP).

---

## Phase 4: User Story 2 - Track and enforce message sequence numbers (Priority: P1)

**Goal**: Inbound-expected / outbound-next `MsgSeqNum` counters advance by exactly one under durable-before-transmit ordering; too-low and too-high are session-fatal; overflow is fatal with no wrap.

**Independent Test**: Ordered admin stream advances counters by one; too-low (no PossDup) → fatal; too-high → fatal Logout-with-text+disconnect (no ResendRequest); `seqnum_max` → fatal no-wrap.

- [X] T027 [P] [US2] Write seam #3 `tests/session/seqnum_manager_test.cpp` — increment-by-one in/out, too-low fatal, too-high session-fatal, `seqnum_max` overflow no-wrap (SC-003, I-2/I-8) — must FAIL first
- [X] T028 [P] [US2] Write seam #4 `tests/session/seqnum_gap_fatal_test.cpp` — too-high gap surfaces `session_seqnum_gap_unrecoverable`, orderly Logout-with-text + disconnect, asserts **no** `ResendRequest(35=2)` emitted (I-4, Session-2026-05-18) — must FAIL first
- [X] T029 [P] [US2] Write seam #10 `tests/session/durable_before_transmit_test.cpp` — `store(outbound)` completes before `transport::async_write`; a cancelled transmit leaves no persisted-but-unsent inconsistency (I-3) — must FAIL first **(US4 Phase 6 retirement: outbound half NOW GREEN — `OutboundStoreBeforeTransportSend` injects a `RecordingStoreFactory` minting an `OrderingStore` whose `store(outbound)` appends `"store_out"` to a shared vector that the `transport_send` callback later appends `"transport_send"` to; the test asserts the literal alternating `store_out → transport_send` pair ordering. Both halves of I-3 now covered in one file; the Phase-4-era inbound-only deferral note is retired.)**
- [X] T030 [P] [US2] Write conformance `tests/session/conformance/tc_seqnum_test.cpp` — `2a_MsgSeqNumCorrect`, `2c_MsgSeqNumTooLow`, `2q_MsgTypeNotValid`, `2r_UnregisteredMsgType` (NOT `1a`/`2b` too-high — deferred per D-10) — must FAIL first
- [X] T031 [US2] Implement `src/session/seqnum_manager.cpp` — inbound-expected/outbound-next counters serialized by `fixpp::sync::async_mutex` (`[2f §7.3]`, D-7); in-sequence advance; too-low (no PossDup) → `session_seqnum_too_low` session-fatal (`[FIX-SL §4.1]`)
- [X] T032 [US2] Implement the too-high disposition — `session_seqnum_gap_unrecoverable` → orderly Logout-with-text → disconnect, no ResendRequest/SequenceReset (I-4); receipt of out-of-scope `ResendRequest`/`SequenceReset` is a bounded `session_admin_not_supported` reject (FR-017)
- [X] T033 [US2] Implement `seqnum_max` overflow → reuse `[2e §6.7] store_seqnum_overflow`, session-fatal, no wrap, surfaced via the session-level error callback (I-8, FR-008/009)
- [X] T034 [US2] Implement durable-before-transmit ordering in `src/session/session.cpp` — `co_await store->store(seq, committed_span, outbound)` post-`Writer::commit`, pre-`transport::async_write`; inbound parse→store→`fromAdmin`/`fromApp` ordering (`[2e §root cause #1]`/`[2e §7.6]`, I-3)
- [X] T035 [US2] Wire the seqnum columns (too-low / too-high / in-seq) across all FSM states per the data-model transition matrix
- [X] T036 [US2] Make seams #3/#4/#10 + `tc_seqnum` green; refactor (SC-003)

**Checkpoint**: US1 + US2 both independently functional.

---

## Phase 5: User Story 3 - Keep an established session alive (Heartbeat / Test Request) (Priority: P2)

**Goal**: Heartbeat on outbound idle, TestRequest on inbound silence, TestReqID echo, unanswered-TR → unhealthy; all timing via the effective clock; `HeartBtInt=0` disables.

**Independent Test**: Mock clock past `HeartBtInt` → exactly one Heartbeat; past inbound-silence → TestRequest w/ unique TestReqID; inbound TestRequest → Heartbeat echoing TestReqID; grace window unanswered → unhealthy/disconnect; `HeartBtInt=0` → no timers.

- [X] T037 [P] [US3] Write seam #5 `tests/session/heartbeat_testrequest_test.cpp` — mock-clock heartbeat once-per-idle-window (no storms), TestRequest unique TestReqID, echo on receipt, unanswered→unhealthy→disconnect, `HeartBtInt=0` disables (SC-004) — must FAIL first
- [X] T038 [P] [US3] Write conformance `tests/session/conformance/tc_liveness_test.cpp` — `4a_NoDataSentDuringHeartBtInt`, `4b_ReceivedTestRequest` (D-10) — must FAIL first **(US4 Phase 6 retirement: 4b_ReceivedTestRequest now GREEN — inbound TestRequest → Heartbeat echo wired in session.cpp via store_then_emit; registered in CMakeLists.txt; 2 tests pass. 4a_NoDataSentDuringHeartBtInt STILL DEFERRED — requires outbound-idle tracking (last_outbound_steady_) not yet implemented; deferred to follow-up phase with traceability in tc_liveness_test.cpp header comment)**
- [X] T039 [US3] Implement `src/session/heartbeat.cpp` — heartbeat / test-request / graceful-close timers over `Clock::steady_now`/`sleep_until` armed under the cancellation slot; D-8 defaults (`heartbeat_interval`=30 s, `test_request_threshold`=1×HeartBtInt); `HeartBtInt=0` disables all timers
- [X] T040 [US3] Implement Heartbeat/TestRequest build/interpret in `admin_messages.cpp` incl. `TestReqID(112)` echo (FR-006)
- [X] T041 [US3] Wire the Active-state liveness transitions + unanswered-TestRequest → `session_test_request_unanswered` unhealthy → disconnect (data-model matrix)
- [X] T042 [US3] Make seam #5 + `tc_liveness` green; refactor (SC-004) **(US4 Phase 6 retirement: seam #5 GREEN with 7 tests; tc_liveness now registered + 2 tests GREEN (4b); 4a still deferred — see T038 note)**

**Checkpoint**: US1–US3 independently functional.

---

## Phase 6: User Story 4 - Terminate a session cleanly (Logout exchange) (Priority: P2)

**Goal**: Orderly Logout both directions reaches `Disconnected`; never-confirmed Logout force-disconnects within the clock-bound timeout; non-Active Logout follows `[FIX-SL §4.6]`.

**Independent Test**: Active→initiate Logout→peer confirms→`Disconnected`; never-confirmed → clock-bound force-disconnect (no hang); non-Active Logout transitions defined.

- [X] T043 [P] [US4] Write seam #6 `tests/session/logout_exchange_test.cpp` — graceful both directions; never-confirmed → clock-bound force-disconnect; non-Active Logout transitions (SC-005) — must FAIL first
- [X] T044 [P] [US4] Write seam #11 `tests/session/cancellation_two_phase_test.cpp` — `cancellable_dispatch` parse→`fromAdmin`; child-state Logout/timeout; `close()` idempotent; `close(terminal)` skips phase 1 (I-9) — must FAIL first
- [X] T045 [P] [US4] Write conformance `tests/session/conformance/tc_logout_test.cpp` — `12_*`, `13_*`, `13b_UnsolicitedLogoutMessage` (D-10) — must FAIL first
- [X] T046 [US4] Implement Logout build/interpret + FSM `Active → LogoutSent → Disconnected`, Logout-received path, and non-Active Logout transitions per `[FIX-SL §4.6]` (data-model matrix, FR-005)
- [X] T047 [US4] Implement two-phase `Session::close` (`[2d §6.5]`) — child `asio::cancellation_state` for Logout `async_write` + `Clock::sleep_until` graceful timeout (`session_logout_timeout`, D-8); phase-2 root `total` only after phase-1; `close()` idempotent; `close(terminal)` skips phase 1 (I-9)
- [X] T048 [US4] Implement the `cancellable_dispatch(session_executor, slot, handler)` parser-completion → `fromAdmin`/`fromApp` hand-off with the three `[2d §6.5]` deterministic fire/not-fire cases (US1–US3 deliver `fromAdmin`/`fromApp` via the direct per-session-strand dispatch wired in T016/T024; this task hardens that hand-off with the `[2d §6.5]` child-cancellation/two-phase semantics — not a re-implementation)
- [X] T049 [US4] Make seams #6/#11 + `tc_logout` green; refactor (SC-005)

**Checkpoint**: US1–US4 independently functional.

---

## Phase 7: User Story 5 - Reject invalid session messages & enforce SendingTime (Priority: P3)

**Goal**: Session-level `Reject(35=3)` with full refs + `SessionRejectReason`, no reject loop; inbound `SendingTime` MaxLatency enforcement per Clarification Q3.

**Independent Test**: Missing field / out-of-range / type-invalid-for-state → correct `Reject`; stale `SendingTime` → Reject(10)→Logout→disconnect (Logon→logout-with-error); every outbound `SendingTime` grammar-exact and round-trip-lossless.

- [ ] T050 [P] [US5] Write seam #7 `tests/session/session_reject_test.cpp` — `RefSeqNum`/`RefTagID`/`RefMsgType`/`SessionRejectReason`; no reject loop (I-5, SC-006) — must FAIL first
- [ ] T051 [P] [US5] Write seam #8 `tests/session/sending_time_test.cpp` — MaxLatency breach → `Reject`(reason 10)→`Logout`→disconnect; `Logon`→logout-with-error, no standalone reject (Q3, SC-007) — must FAIL first
- [ ] T052 [P] [US5] Write conformance `tests/session/conformance/tc_reject_test.cpp` — scenario 7 Receive Reject + scenario 14 `14a`–`14g` (`14h`/`14i`/`14j` deferred per D-10) — must FAIL first
- [ ] T053 [P] [US5] Write conformance `tests/session/conformance/tc_sendingtime_test.cpp` — `1d_InvalidLogonBadSendingTime`, `2o_SendingTimeValueOutOfRange` + BeginString version gating (D-10) — must FAIL first
- [ ] T054 [US5] Implement `Reject(35=3)` build/interpret with `RefSeqNum(45)`/`RefTagID(371)`/`RefMsgType(372)`/`SessionRejectReason(373)`; the no-reject-loop guard (a malformed `Reject`/`Logout` is not itself rejected, I-5); `session_msg_type_invalid_for_state` (FR-007)
- [ ] T055 [US5] Implement `include/fixpp/session/sending_time.hpp` + logic — inbound `SendingTime(52)` vs `effective_clock.now()` within `MaxLatency` (D-8 default 120 s); Q3 disposition `Reject`(`SessionRejectReason=10`, ref tag 52)→`Logout`→disconnect, except Logon→logout-with-error (D-3, FR-013)
- [ ] T056 [US5] Wire the guard-precedence ordering (parse/type → CompID/BeginString → SendingTime/MaxLatency → seqnum class → message-type-for-state) per the data-model matrix preamble
- [ ] T057 [US5] Make seams #7/#8 + `tc_reject`/`tc_sendingtime` green; refactor (SC-006/007)

**Checkpoint**: All five user stories independently functional.

---

## Phase 8: Polish & Cross-Cutting Concerns

**Purpose**: FR-001 whole-matrix verification, the no-alloc/noexcept discipline, benches, the Tier-1 matrix, and the close-out bookkeeping the project memory requires.

- [ ] T058 Write seam #1 `tests/session/fsm_transition_matrix_test.cpp` — assert every `[FIX-SL §4.10]` state×event cell in the FR-001 event alphabet has a defined transition (no UB, no silent no-op), incl. out-of-scope admin → `session_admin_not_supported` (FR-001, I-1) — green only after US1–US5
- [ ] T059 Write seam #12 `tests/session/alloc_discipline_test.cpp` under `mallocnesia` (`tools/mallocnesia/libmallocnesia.so`) + `tools/check_alloc.py` — zero global `new`/`delete` on inbound-dispatch / timer-fire / seqnum paths (SC-009, I-7)
- [ ] T060 [P] Verify the `noexcept` window + throwing-user-callback trap (`core::detail::trap_throw`) across the inbound-process / timer-fire window, **and assert the no-C++-across-C-ABI layering check** (`nm` confirms zero `extern "C"` session symbols; no session type appears in `<fix/c_api.h>`) (FR-015, SC-009)
- [ ] T061 [P] Add `bench/session/{fsm,seqnum,fix_time,heartbeat}_bench.cpp` + `bench/baselines/session/*.json` enforcing the plan.md Technical-Context ceilings at ±5% (`[const §VIII.1]`/`[const §VIII.2]`)
- [ ] T062 Run the full Tier-1 preset matrix serially per quickstart §3 (debug / release / asan / ubsan / tsan / coverage + gcc-release sanity); resolve any failure (`[const §IX.2]`/`[const §IX.6]`)
- [ ] T063 Confirm ≥95% line / ≥85% branch on `include/fixpp/session/*`, `src/session/*`, `include/fixpp/core/fix_time.*` on the lcov DA/BRDA basis; any uncovered line/branch carries a recorded Opus risk note (`[const §IX.1]`)
- [ ] T064 [P] clang-tidy / clang-format / cppcheck / IWYU + the `[const §XV.9]` mutex-in-coroutine grep gate clean across all session/fix_time headers and sources
- [ ] T065 [P] Finalize the **005 scope-deferral ledger** in `spec/coverage-index.md` — deferred-with-traceability pointers (too-high + recovery-dependent TC cases, FIXT/5.0SP2 + 4.0/4.1/4.3/5.0 version scope, S-016 115/128, scenario-14 `14h`/`14i`/`14j`) + per-row delivery-coverage for the owned FIX.4.2/4.4 rows
- [ ] T066 [P] Update `spec/feature-catalogue.md` rows S-001/2/3/4/7/8/9/15/16/19/20 to the delivered FIX.4.2/4.4 point-to-point establishment slice with the explicit version/third-party-addressing deferral notes
- [ ] T067 Feature-completeness audit (memory `feedback_feature_completeness_gate`): every FR-001..018 + SC-001..010 ↔ task ↔ catalogue row mapped 100% (or explicitly waived) — a `/gate-b` precondition
- [ ] T068 Run quickstart.md end-to-end validation incl. recording the `core/` time-helper #4 module-exit closure (close `core/` README row #4 + `core/` exit checkbox at merge per pipeline step 16, memory `feedback_pipeline_mark_done_step`)

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (P1: T001–T004)**: no dependencies.
- **Foundational (P2: T005–T018)**: after Setup. BLOCKS all user stories. (TDD pairs: T007→T008; T009→T011; T010→T011.)
- **User Stories (P3–P7)**: all after Foundational. US1 is the MVP; US2–US5 build on the established session but are independently testable via the doubles.
- **Polish (P8: T058–T068)**: after the user stories it verifies (T058 needs all FSM events implemented; T062/T063 need all tests landed).

### User Story Dependencies

- **US1 (P1)** — foundation only. The critical path (`transport/` is blocked on this).
- **US2 (P1)** — foundation only; integrates with US1's send path but tests independently against the doubles.
- **US3 (P2)** — needs an Active session (US1) conceptually; independently testable with the mock clock.
- **US4 (P2)** — needs Active (US1); independently testable.
- **US5 (P3)** — independently testable; exercises the folded time-helper #4 (Foundational T010/T011).

### Within Each User Story

Tests (the seam + conformance `[P]` tasks) are written FIRST and must FAIL before the implementation tasks; FSM-state wiring rides on the message build/interpret task; green+refactor closes the story.

### Parallel Opportunities

- Setup: T002/T003/T004 in parallel.
- Foundational: T005/T006, T009/T010, T012/T013/T014/T015/T017 in parallel (distinct files); T007 before T008; T009+T010 before T011.
- Within a story: all `[P]`-marked test tasks (e.g. T019+T020; T027–T030; T037+T038; T043–T045; T050–T053) run in parallel before implementation.
- Polish: T060/T061/T064/T065/T066 in parallel.

---

## Parallel Example: User Story 2 (tests first)

```bash
Task: "seam #3 tests/session/seqnum_manager_test.cpp"
Task: "seam #4 tests/session/seqnum_gap_fatal_test.cpp"
Task: "seam #10 tests/session/durable_before_transmit_test.cpp"
Task: "conformance tests/session/conformance/tc_seqnum_test.cpp"
# all FAIL → then T031..T036 implement and turn them green
```

---

## Implementation Strategy

### MVP First (User Story 1)

1. Setup (T001–T004) → 2. Foundational (T005–T018) → 3. US1 (T019–T026) → **STOP & validate US1 independently** (initiator↔acceptor reach Active) → demo-able session-establishment substrate.

### Incremental Delivery

US1 (MVP) → US2 (seqnum integrity) → US3 (liveness) → US4 (logout) → US5 (reject/SendingTime) → Polish. Each story is a green, independently testable increment via the transport/store doubles + mock clock.

### Notes

- `[const §VII.5]` ships only the in-scope `[FIX-TC]` subset green; the too-high + recovery-dependent + FIXT/5.0SP2 + 4.0/4.1/4.3/5.0 cases are deferred-with-traceability under the recorded Article XVII §1 Gate-A-blocker waiver (plan Constitution Check / Complexity Tracking) — T065 records the ledger; do not attempt to green deferred cases.
- No C-ABI surface, no fuzz harness, abidiff N/A (D-12) — `/speckit-verify` marks fuzz + abidiff SKIPPED-with-reason, not missing.
- Commit after each task or logical group; resource gate — surface `AskUserQuestion` before any local Conan/CMake build (`[const §XVII.7]`).
- Error slots 43..N in T005 are the Gate-A-pinned allocation; once a C-ABI release freezes them they are non-renumbering (`[const §X.4]`).
