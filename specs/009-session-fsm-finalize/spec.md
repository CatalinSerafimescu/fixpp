# Feature Specification: Session FSM Finalize — close PR #81 round-1 drift against the Gate-A-converged 005 design

**Feature Branch**: `009-session-fsm-finalize`
**Created**: 2026-05-22
**Status**: Draft
**Input**: User direction 2026-05-22 (post-/gate-b PR #81 pause): "convert the 7 round-1 P1 findings to a Phase 9 implementation slice."

## Overview

This slice **closes implementation drift** between the Gate-A-converged `005-session-establishment-fsm` design (`specs/005-session-establishment-fsm/{spec,plan,research,data-model,tasks,quickstart}.md` + `contracts/*.hpp`) and what actually shipped on branch `005-session-establishment-fsm`. PR #81 round-1 Gate B Codex hostile review returned **7 P1 binding-contract drift findings** (P2=0, P3=0); Opus triage confirmed all seven @ P1 with no waiver. The Phase-8 T067 completeness audit was a false-PASS for FR-001 / FR-002 / FR-003 / FR-011 / FR-013 (see project memory `[[project_005_phase8_completeness_false_pass]]`).

**The 005 design is unchanged.** Every functional requirement below traces to an existing 005 contract anchor — this slice does not amend `spec.md` / `data-model.md` / `contracts/`. Gate A is not re-run.

**Branch base**: this branch (`009-session-fsm-finalize`) is rooted on `005-session-establishment-fsm` HEAD `4e621e1`, inheriting all 25 prior commits (Phase 1–8 + /simplify + /speckit-verify). At Gate B convergence the slice merges back into `005-session-establishment-fsm` (or directly to `main`, retiring 005's branch) and PR #81 is updated or superseded.

## User Scenarios & Testing *(mandatory)*

The "user" here is a **FIX engine integrator** — a developer building a FIX-speaking application using `fixpp::session` as the embedded session-layer library. Each story is independently testable in isolation and individually delivers a viable slice (the library is unusable without US1; acceptor deployments are blocked without US2; FSM-spec compliance fails without US3; multi-session deployments are unsafe without US4).

### User Story 1 — Send an application message that actually reaches the counterparty (Priority: P1)

A FIX integrator builds an outbound application message (e.g. `NewOrderSingle`), hands it to `Session::send`, and expects the library to (1) assign the next outbound `MsgSeqNum(34)`, (2) stamp `SendingTime(52)` from the effective clock, (3) emit the configured `BeginString(8)` (not a hard-coded default), (4) durably persist the message to the configured `MessageStore` before transmission per the `[2e §4.1]` durable-before-transmit invariant, then (5) hand the framed bytes to the configured transport.

**Why this priority**: Without this, `Session::send` is a no-op (`co_return expected_t<void>{}`). The library cannot send any application traffic. Catalogue rows S-001 / S-009 / S-019 are claimed delivered but are not. This is the single largest gap from PR #81.

**Independent Test**: Build a Session against a test-double `MessageStore` + test-double transport + `mock_clock`. Call `Session::send(payload)`. Assert (a) `MsgSeqNum` was incremented by exactly 1, (b) the outbound frame carries `52=<mock_clock_now_formatted>`, (c) the frame carries `8=<configured_begin_string>` (test BOTH `FIX.4.2` and `FIX.4.4` sessions), (d) the store received the frame BEFORE the transport, (e) when the transport double signals cancellation, the store does NOT retain a phantom committed frame that was never sent (or, if it does, the next session-recovery feature can detect it).

**Acceptance Scenarios**:

1. **Given** an Active FIX.4.4 session with seqnum_out at 7, **When** the integrator calls `Session::send(payload)`, **Then** the on-wire frame carries `34=8`, `52=<mock_clock_now>`, `8=FIX.4.4`, and the store records the frame at seq=8 strictly before the transport sees it.
2. **Given** an Active FIX.4.2 session, **When** the integrator calls `Session::send(payload)`, **Then** the on-wire frame carries `8=FIX.4.2` (i.e., the negotiated BeginString, not a default).
3. **Given** an Active session whose transport cancels mid-`async_write`, **When** `Session::send` returns, **Then** the integrator receives a defined error (no silent success), and the session reaches `Disconnected` per the existing close-path contract.
4. **Given** the 5 admin builders (`build_logon`, `build_logout`, `build_heartbeat`, `build_test_request`, `build_reject`), **When** any of them emits a frame, **Then** every emitted frame carries the negotiated `BeginString` AND a `SendingTime` sourced from `effective_clock.now()`.

---

### User Story 2 — Deploy an acceptor session that waits for inbound Logon (Priority: P1)

A FIX integrator configures a `Session` as the **acceptor** side of a FIX link (their service is the server, the counterparty connects to them). They expect `Session::open` to leave the session in `NotConnected` until a valid peer `Logon` arrives, then transition `NotConnected → LogonReceived → Active` per the `[FIX-SL §4.10]` matrix row recorded in `data-model.md:19`. Currently `Session::open` unconditionally enters `LogonSent` regardless of role — every session behaves as an initiator.

**Why this priority**: Acceptor is half of FR-002 ("Logon initiation/acceptance both roles") and half of every catalogue row claiming Logon/Logout/SendingTime support. Without it, the library is initiator-only — an integrator deploying a FIX server cannot use it. The conformance test suite's "acceptor" scenarios pass only because they call `open()` (entering LogonSent) and then exercise the LogonSent path, not the NotConnected→LogonReceived path they claim to verify.

**Independent Test**: Add a role field to `SessionConfig`. Configure a Session with `role = acceptor`. Call `Session::open`. Assert the session is in `NotConnected` (not `LogonSent`). Feed a valid peer `Logon` frame via the inbound transport double. Assert the session passes through `LogonReceived` (intermediate state, observable via the state trace or via a `state()` snapshot under deterministic dispatch) and ends in `Active`. Repeat with a session configured as `initiator` — verify the existing initiator path still works (regression).

**Acceptance Scenarios**:

1. **Given** a Session with `role = acceptor` and a configured peer CompID pair, **When** the integrator calls `Session::open`, **Then** the session is in `NotConnected` and no outbound Logon is emitted.
2. **Given** an acceptor session in `NotConnected`, **When** a valid peer `Logon` arrives, **Then** the session passes through `LogonReceived` and emits its own `Logon` reply, then reaches `Active`.
3. **Given** a Session with `role = initiator` (the existing default behavior), **When** `Session::open` is called, **Then** the session enters `LogonSent` and emits an initial Logon (existing behavior, regressed).
4. **Given** the conformance suite's "acceptor" scenarios (`tc_establishment_test.cpp` Scenario1a/1b), **When** they run, **Then** they exercise the `NotConnected → LogonReceived → Active` matrix row (not the LogonSent path).

---

### User Story 3 — FSM and inbound validation match the Gate-A-converged contract (Priority: P2)

A FIX integrator (and any conformance auditor) expects the implementation to match the matrix in `data-model.md:19` and the inbound-validation contract in `contracts/sending_time.hpp:23-24` literally: (a) a refused first `Logon` on the `NotConnected` row transitions to `Disconnected` (not preserved-in-`NotConnected`); (b) a missing or malformed inbound `SendingTime(52)` triggers a session-level Reject per `[FIX-SL §4.5.4]` (not silent acceptance); (c) on the `LogonSent` path, missing/malformed `SendingTime` follows the design D-3 LogonSent-special rule: `Logout`-with-error and disconnect (no standalone Reject before establishment).

**Why this priority**: P2, not P1, because both gaps are spec-conformance correctness issues that do not block the library from running but DO block conformance certification and produce wrong on-wire behavior. They are detectable today by reading the matrix table vs the code; an integrator running their own conformance harness would catch them.

**Independent Test**: For (a) — feed a Logon with an invalid `BeginString` to a session in `NotConnected`; assert the session reaches `Disconnected` (not `NotConnected`). For (b) — feed an Active-state inbound message with `52` missing AND another with `52` malformed (e.g. `52=abc`); assert each triggers a session-level `Reject(SessionRejectReason=10, RefTagID=52)` followed by `Logout` and `Disconnected`. For (c) — feed a Logon with `52` missing OR malformed; assert the session emits `Logout(58=<error text>)` and disconnects (no standalone Reject).

**Acceptance Scenarios**:

1. **Given** a session in `NotConnected`, **When** an inbound `Logon` with invalid `BeginString` arrives, **Then** the session reaches `Disconnected`.
2. **Given** a session in `NotConnected`, **When** an inbound `Logon` with invalid `SenderCompID/TargetCompID` arrives, **Then** the session reaches `Disconnected`.
3. **Given** a session in `Active` or `LogonReceived`, **When** an inbound frame arrives with tag `52` absent, **Then** the session emits `Reject(SessionRejectReason=10, RefTagID=52)`, then `Logout`, then reaches `Disconnected`.
4. **Given** a session in `Active`, **When** an inbound frame arrives with `52` present but malformed (parse failure), **Then** the session emits `Reject(SessionRejectReason=10, RefTagID=52)`, then `Logout`, then `Disconnected`.
5. **Given** a session in `LogonSent`, **When** an inbound `Logon` arrives with `52` missing OR malformed, **Then** the session emits `Logout(58=<error text>)` and reaches `Disconnected` (no standalone Reject before establishment).

---

### User Story 4 — Two sessions running concurrently are race-free and tear down without `std::terminate` (Priority: P2)

A FIX integrator running multiple `Session` instances on a thread-pool executor (e.g., one initiator + one acceptor; or a server with many counterparties) expects (a) no cross-session unsynchronized shared state — each session's `TestRequest(35=1)` IDs come from session-local state, not a process-global counter; and (b) calling `Session::close` on a session whose `SeqnumManager` has in-flight mutex acquisitions completes without crashing the process. Today (a) a `static std::uint32_t tr_counter` in `run_liveness_loop` is shared across every Session instance — non-atomic increment, race under TSan when two sessions tick TestRequest concurrently; (b) `SeqnumManager` is never drained before destruction, so any session that closes with a mid-flight `check_inbound` on the seqnum async_mutex calls `std::terminate()` per the documented `async_mutex` teardown precondition (`include/fixpp/core/sync/async_mutex.hpp:155-160,683-690`).

**Why this priority**: P2 because no existing test exercises concurrent multi-session traffic (which is why TSan never fired) and clean-shutdown traffic patterns happen to not hit the drain hazard most of the time. Both are real defects — same class as the 008 PR #78 teardown TSan hotfix — that will fire under production load.

**Independent Test**: For (a) — run two sessions' liveness loops concurrently on a multi-threaded `asio::thread_pool` (each with `HeartBtInt=1s` + `mock_clock` advance); assert each session's TestReqIDs come from disjoint per-session sequences AND TSan reports no race on the counter. For (b) — acquire the seqnum mutex via `Session::check_inbound`, then call `Session::close(graceful)`, then destroy the Session; assert no `std::terminate` (e.g. via a gtest death-test or by surviving destructor in normal pipeline).

**Acceptance Scenarios**:

1. **Given** two Sessions running their liveness loops concurrently on distinct threads, **When** both emit `TestRequest`, **Then** TSan reports zero data races AND each session's IDs come from a session-local counter (not a process-global one).
2. **Given** a Session whose `SeqnumManager::async_mutex` has an active lock holder when `Session::close(graceful)` is invoked, **When** `close` completes and the Session is destroyed, **Then** the process does NOT call `std::terminate`.
3. **Given** a Session that goes through the `never_opened → ~Session` path (constructed but never opened), **When** the Session is destroyed, **Then** the destructor completes without invoking the `async_mutex` teardown precondition violation (drain succeeds trivially because no holders / waiters exist).

---

### Edge Cases

- **Cancellation during `Session::send`**: if the transport's `async_write` is cancelled after the store has committed the frame, the durable-store record exists but the wire transmission did not. The slice does not attempt session-level recovery — that is the deferred session-recovery feature's responsibility per `[2e §3.1]` / `[2e §4 last bullet]`. The slice MUST return a defined error from `Session::send` and leave the session in a state consistent with the existing close-path contract; it MUST NOT silently succeed.
- **Acceptor receiving a non-Logon first message** (`NotConnected × non-Logon`): existing matrix says `Disconnected`; the existing implementation handles this row correctly (`fsm_transition_matrix_test.cpp:376` `NotConnected_NonLogonFirstMessage_TransitionsToDisconnected`). This slice MUST NOT regress it.
- **`SendingTime` exactly at the boundary of `MaxLatency`**: existing 005 `sending_time_test.cpp::ExactBoundaryIsOk` confirms boundary acceptance. The new missing/malformed tests MUST be additive — they do not change the stale-but-well-formed boundary semantics.
- **TestRequest counter overflow**: the per-session counter rolls over at `UINT32_MAX`. Wrap-around is acceptable; uniqueness within a single session lifetime is the contract, not cross-restart uniqueness.
- **`SeqnumManager::drain` failure during close**: per Opus triage Fix Queue RC#7, drain failures are logged-then-proceed; `close()` still reports `closed_drained` and the destructor still completes safely (because the mutex is then in the drained-with-failure state, not the holder-present state).
- **Concurrent `Session::send` calls**: the per-session strand serializes calls. The slice does not introduce parallel `send()` semantics; existing strand reentrancy contract (FR-016, `test_direct_executor_reentrancy.cpp`) is preserved.

## Requirements *(mandatory)*

Every FR below traces to an existing 005 contract anchor (the design is unchanged) AND to a specific PR #81 Root Cause from `research/G19-fix-fpml-iso20022/research/reviews/opus_pr81_1_triage.md`.

### Functional Requirements

**Outbound emit (RC#1 + RC#4 cluster):**

- **FR-001**: `Session::send(payload)` MUST implement the end-to-end outbound pipeline per `005/contracts/session.hpp:46-50`: (a) acquire and assign the next outbound `MsgSeqNum(34)` via `SeqnumManager::assign_outbound`; (b) stamp `SendingTime(52)` from `effective_clock.now()` via `core::utc_time_to_fix_string`; (c) build the framed wire bytes via the existing Writer pattern; (d) durably persist the frame via `store_->store(seq, committed, outbound)` BEFORE handing it to the transport per the `[2e §4.1]` durable-before-transmit invariant; (e) hand the frame to the configured transport. Returns `expected_t<void>` with a defined error on any failure. (Closes RC#1; verifies FR-008 / FR-013 outbound halves; closes catalogue rows S-001 / S-009 / S-019 outbound claims.)
- **FR-002**: Every outbound frame emitted by `Session::send` AND by every admin builder (`build_logon`, `build_logout`, `build_heartbeat`, `build_test_request`, `build_reject`) MUST carry `BeginString(8) = <negotiated_begin_string>` (from `SessionConfig.begin_string`). The hard-coded `kBeginStringDefault = "FIX.4.2"` constant in `src/session/admin_messages.cpp` MUST be removed. (Closes RC#4; verifies FR-003 / S-020 BeginString claim for outbound.)
- **FR-003**: Every outbound frame emitted by any admin builder MUST carry `SendingTime(52) = effective_clock.now()` formatted via `core::utc_time_to_fix_string`. The hard-coded `kSendingTimePlaceholder = "00000000-00:00:00.000"` constant MUST be removed. (Closes RC#4; verifies FR-011 / S-019 SendingTime claim for outbound.)

**Acceptor role (RC#2):**

- **FR-004**: `SessionConfig` MUST expose a `role` field with values `initiator` / `acceptor`, defaulting to `initiator` (preserving existing behavior). The `Session::open` implementation MUST branch on this field: `initiator` → set `fsm_state_ = LogonSent` and emit an initial Logon (the existing path); `acceptor` → set `fsm_state_ = NotConnected` and emit no outbound Logon (wait for peer Logon via `on_inbound_frame`). (Closes RC#2; verifies FR-002 acceptor half.)
- **FR-005**: When an acceptor session in `NotConnected` receives a valid peer `Logon`, it MUST traverse `NotConnected → LogonReceived → Active` per `data-model.md:19` and emit its own `Logon` reply on the `LogonReceived → Active` transition. (Closes RC#2 matrix-row coverage; verifies FR-001 acceptor row.)

**FSM matrix + inbound validation (RC#3 + RC#5):**

- **FR-006**: When a session in `NotConnected` receives an inbound `Logon` that fails BeginString OR CompID OR any other establishment-time validation, the session MUST transition to `Disconnected` per `data-model.md:19` row `NotConnected × inbound Logon (refused)`. The current Phase-3 compromise that preserves `NotConnected` for refused-Logon-shaped frames MUST be removed from `src/session/session.cpp`. (Closes RC#3.)
- **FR-007**: When a session in `Active` or `LogonReceived` receives an inbound frame with tag `52` (`SendingTime`) absent (empty), it MUST emit `Reject(SessionRejectReason=10, RefTagID=52)`, then `Logout`, then reach `Disconnected` — the same path as the existing stale-SendingTime branch. (Closes RC#5 missing half; verifies FR-013 / `contracts/sending_time.hpp:23-24`.)
- **FR-008**: When a session in `Active` or `LogonReceived` receives an inbound frame with tag `52` present but unparseable (parse failure on the FIX UTC grammar), it MUST emit `Reject(SessionRejectReason=10, RefTagID=52)`, then `Logout`, then reach `Disconnected`. The current `// parse failure: lenient on malformed timestamp` fall-through MUST be removed. (Closes RC#5 malformed half.)
- **FR-009**: When a session in `LogonSent` receives an inbound `Logon` with tag `52` missing OR unparseable, it MUST emit `Logout(58=<error text>)` and reach `Disconnected` (no standalone Reject before establishment) — the LogonSent-special D-3 path identical to the existing stale-SendingTime-on-Logon branch. (Closes RC#5 LogonSent half.)

**Concurrency safety (RC#6 + RC#7):**

- **FR-010**: The TestRequest ID counter used by `run_liveness_loop` MUST be per-Session state (a non-`static` data member of `Session`), not a process-global `static` local. Cross-Session uniqueness is NOT required; within-Session uniqueness over the Session's lifetime is sufficient (wrap-around at `UINT32_MAX` is acceptable). (Closes RC#6; satisfies `[const §XI.1-4]` per-session-strand-serialisation contract.)
- **FR-011**: `Session::close` MUST call `co_await seqnum_mgr_.drain()` during phase 2 (after `root_cancel_.emit(...)` and `trace_slot_.clear()`, before transitioning `state_ = closed_drained`). Drain failures MUST be logged-then-proceed; the destructor of `Session` MUST be safe to invoke after `close` returns regardless of drain outcome. Sessions destroyed via the `never_opened → ~Session` path MUST also be safe (the mutex is in its default no-holders state, drain succeeds trivially). (Closes RC#7; satisfies the documented `async_mutex` teardown precondition `include/fixpp/core/sync/async_mutex.hpp:155-160`.)

**Test-quality regressions (cross-cutting):**

- **FR-012**: For every FR-001..FR-011 above, the slice MUST land at least one **runtime-behavior test** (not a `static_assert` / compile-time noexcept attestation alone). The completeness audit T067-equivalent at slice close-out MUST verify test BODIES match the contract assertion, not just file-naming or task-marker existence (per project memory `[[project_005_phase8_completeness_false_pass]]`).
- **FR-013**: The conformance suite outbound assertions in `tests/session/conformance/tc_logout_test.cpp`, `tc_reject_test.cpp`, `tc_establishment_test.cpp`, `tc_liveness_test.cpp`, and `tc_sendingtime_test.cpp` MUST be extended to read AND assert tag `8` AND tag `52` on every outbound frame (not just `35` + the test-specific tags). The 005 conformance tests passed as false-green because they never checked these tags.

### Key Entities *(unchanged from 005)*

No new entities. This slice modifies existing entities (`Session`, `SessionConfig`, `SeqnumManager`, the admin-builder helpers); they remain owned by 005 contracts and data-model.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: 100% of the 7 PR #81 round-1 Root Causes (RC#1–#7) have a landed runtime-behavior test asserting the contract; zero remain covered only by `static_assert` / file-existence / task-marker.
- **SC-002**: 100% of `tests/session/conformance/tc_*_test.cpp` outbound-frame assertions read and assert tag `8` AND tag `52` against the negotiated BeginString and the mock-clock-now respectively; zero conformance tests assert only the message-type tags.
- **SC-003**: A two-session concurrent-liveness regression test runs on `asio::thread_pool` with `HeartBtInt=1s` + `mock_clock` advance, and TSan reports zero data races on the TestRequest ID counter over a 10⁴-iteration sample.
- **SC-004**: A teardown-race regression test acquires the `SeqnumManager` async_mutex via `Session::check_inbound`, calls `Session::close(graceful)`, destroys the Session, and completes without `std::terminate` — verified via gtest death-test or surviving normal pipeline.
- **SC-005**: A second `/speckit-verify` run on the merged 005+009 tree reports a non-RED verdict; coverage on `src/session/session.cpp` and `src/session/admin_messages.cpp` rises into the prior W-1..W-4 waiver envelope or better (the new tests close cascading-defensive-branch arms that earlier coverage missed).
- **SC-006**: A re-run of the feature-completeness audit (T067-equivalent) on the merged tree verifies test BODIES match contract assertions for all 18 FRs from 005's spec, and explicitly confirms FR-001 / FR-002 / FR-003 / FR-011 / FR-013 are now genuinely PASS (not the false-PASS pattern from `[[project_005_phase8_completeness_false_pass]]`).
- **SC-007**: A second Gate B run on the merged 005+009 tree converges with `P1 = 0` AND `P2_unwaived = 0` within the standard Sonnet × 2 cap (no Codex-fixer escalation required), demonstrating the slice closed the drift cleanly.

## Assumptions

- **The 005 Gate-A-converged design is authoritative and unchanged.** Every FR above traces to an existing 005 anchor (`spec.md` / `data-model.md` / `contracts/*.hpp`). No `/speckit-clarify` Q4-equivalent is needed because no new design question is opened. Gate A is NOT re-run for 009.
- **Branch base.** `009-session-fsm-finalize` is rooted on `005-session-establishment-fsm` HEAD `4e621e1`; the 25 prior commits (Phase 1–8 + /simplify + /speckit-verify) are inherited as base.
- **Merge target.** At Gate B convergence, 009 merges back into `005-session-establishment-fsm` (refreshing PR #81's branch) OR 009 merges directly to `main` (retiring 005's branch and PR #81). The choice is deferred to /speckit-plan or to the user; this spec does not constrain it.
- **No new contracts.** This slice MUST NOT introduce new headers under `include/fixpp/session/` or `src/session/` beyond what is required to express the per-session TestReqID counter (a member field on `Session`) and the `session_role` enum on `SessionConfig`. New TEST files are expected (per FR-012 / SC-001..SC-004).
- **Test infrastructure reused.** The existing `mock_clock`, in-memory transport double, test-double `MessageStore`, gtest/gmock, and TSan / coverage / ASan presets cover all FR-001..FR-011 verification needs — no new fixture authoring is required.
- **Catalogue rows.** The 005-claimed catalogue rows (S-001/2/3/4/7/8/9/15/16/19/20 + folded `core/` row #4) remain owned by 005; this slice does not flip them. When the merged 005+009 tree closes, the row-flip from `implementing → done` happens per `[[feedback_pipeline_mark_done_step]]` at merge bookkeeping (5's T068 checklist still applies, refreshed against the 009 work).
- **Test-quality binding.** The completeness audit at 009 close-out MUST adopt the audit-test-bodies rule from `[[project_005_phase8_completeness_false_pass]]` — existence-mapping (file named, FR listed, task `[X]`) is INSUFFICIENT for any FR whose primary deliverable is a runtime behavior; the audit MUST grep test bodies for the contract assertion content.
- **No interop-gate dependency.** This slice is independent of the per-release interop work (Phase 9 in the parent's planning files per `[[project_release_interop_quickfix_fix8]]`). The two share the "Phase 9" naming only by coincidence — different scope, different cadence.

## Dependencies

- The 005 Gate-A-converged bundle (`specs/005-session-establishment-fsm/{spec,plan,research,data-model,tasks,quickstart,contracts/*}.md`) is the binding design input. Read it FIRST.
- The PR #81 round-1 artifacts on disk are the binding drift catalogue:
  - `research/G19-fix-fpml-iso20022/research/reviews/codex_pr81_review.md` — 7 P1 ranked findings.
  - `research/G19-fix-fpml-iso20022/research/reviews/opus_pr81_1_triage.md` — 7 RC concrete fix shapes.
  - `research/G19-fix-fpml-iso20022/research/reviews/opus_pr81_paused.md` — pause-state context.
- The existing `005-session-establishment-fsm-verify.md` (YELLOW, 4 waivers W-1..W-4) and the (false-PASS) `005-session-establishment-fsm-completeness.md` are reference inputs only — both will be re-run / re-recorded at 009 close-out.
- Upstream constitutional invariants binding on the implementation: `[const §VIII.5]` (zero-alloc parse-to-fromApp), `[const §X.2]` (no C++ across C ABI), `[const §XI.1-7]` (coroutines + per-session strand + async_mutex + threading-affecting controls), `[const §XV.9]` (no `std::mutex` in awaitable headers), `[arch §5.3]` (no exceptions in noexcept window).
- Upstream feature dependencies (already merged, consumed unchanged): 006-async-mutex (`fixpp::sync::async_mutex`), 007-threading-clock (`fixpp::core::Clock` + `effective_clock`), 008-message-store (`MessageStore` impls).
