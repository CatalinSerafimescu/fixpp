# fixpp#536 / #537 / #538 — session recovery and liveness (B28)

> **Status: v0.1 — draft, pre-Gate-A.** No review round has run.
>
> - **Base:** `origin/main` = `22702dc9`. Code is named by symbol, with a recipe where a population matters; re-derive before relying on one.
> - **Branch:** `536-537-538-recovery-liveness`, in the `fixpp-parallel` worktree.
> - **Mode:** issue mode (no `specs/` bundle). Records go to the parent's `decisions/speckit/`.
> - **Gate A triggers touched** (`[const §XVII.1]`): the session FSM and recovery (#537), the message store contract (#538), cancellation (#536), and comment text in public headers.

## 0. Owner rulings (2026-10-11). Gate A reviews whether this note implements them.

- **R-1.** Builds for this batch are approved, and may run at the same time as B41's.
- **R-2 (#537).** First: "Check official FIX documentation and QuickFIX C++/J/Go then come back if not settled." §4.1 is that check. It did not settle what happens to the frame, so the owner chose between ignoring it, ignoring it with a deadline on an unfilled gap, and queueing it. **Ruling: ignore the frame, and end the session with a Logout when NextNumIn has not advanced for about 2 × HeartBtInt while a resend is outstanding.** The deadline is checked when a too-high frame arrives.
- **R-3.** The resend walk's silent fold of a failed `retrieve` into a GapFill is fixed in this batch (D-538-4).
- **R-4.** Two further leads are filed and are not in this batch: fixpp#577 and fixpp#578 (§8).
- **R-5 (given on B41 the same day; applies here).** "Overall we try not to change behavior of QuickFIX, the internal implementation can be different." Where the three engines agree, that is fixpp's default, and a difference must be stated with its reason. §4.5 states the differences that remain under R-2.

## 1. Evidence at the base

The three reproduction cells were cherry-picked onto `22702dc9` and run in `linux-clang-debug` and `linux-clang-release`. Both trees give the same verdicts as at `d51ce86d`, so B35 (PR #572) did not change any of the three mechanisms.

| Issue | ctest | Controls | Claim cells |
|---|---|---|---|
| #536 | `session_536_shared_clock_liveness` | GREEN | RED: `heartbeats=0 test_requests=0 B.state=3`, both close modes |
| #537 | `session_537_second_too_high` | GREEN | RED on both roles: `state=5` |
| #538 | `session_538_volatile_store_after_reset_logon` | GREEN | RED on `MemoryStore`, on `FileStore`, and on the bare store |

- Commits: `01e23693`, `67795e60`, `8d876039` on the branch. They touch `tests/` only.
- Earlier records: `b28-536-537-repro.md` and `b28-538-541-563-564-repro.md` in the parent's `decisions/speckit/`.

## 2. #538 — `retrieve` after a reset to a first number other than 1

### 2.1 Mechanism

- After a Logon exchange in which fixpp sent 141=Y and the peer acknowledged with 141=Y, the initiator's reset unit calls `store_->reset_to(in, 2)`. This is the only call site that passes an outbound target other than 1 (`src/session/session.cpp`, the ack arm's `unit_out`; `run_reset_unit_`).
- Both stores clear their entries and set the counter to 2. The next `store(2)` appends at position 0.
- Both stores look sequence `s` up at position `s - 1`, in `MemoryStore::retrieve` (`include/fixpp/session/memory_store.hpp`) and `FileStore::retrieve` (`src/session/file_store.cpp`). Each then checks `entry.seq != s`, so a wrong frame is never returned; the lookup reports a gap.
- The resend walk (`Session::replay_outbound_range_`) reads the result as a boolean, `if (rr && cv.captured)`, and folds the slot into a GapFill.
- The default `MessageStore::reset_to` body produces the same state: `reset()`, then a counter-only advance.
- `FileStore` rebuilds its index from the record headers at open, in file order, so the defect survives a restart. Not measured; it is a required cell (§5).

Re-derive the two lookup sites with `git grep -n -E 's - 1\)' -- include/fixpp/session src/session`.

### 2.2 Decisions

- **D-538-1. Both stores look a sequence up relative to the first retained entry of that direction.** Position = `s - first.seq`. A sequence below `first.seq` is a gap. The `entry.seq != s` check stays.
  - `MemoryStore` gains no member: the base is `entries.front().seq`. The class's layout is unchanged, which matters because the header is installed.
  - `FileStore` needs no format change: the base is `index.front().seq`, which the open scan already rebuilds from the record headers. A log written after a (·, 2) reset by a build without this fix becomes readable.
- **D-538-2. The contract says so.** `message_store.hpp`'s `retrieve` comment gains: a frame stored after `reset()` or `reset_to()` is retrievable under its own sequence number, and a sequence below the first retained one is a gap. This binds third-party stores. The default `reset_to` body is unchanged.
- **D-538-3. Stale comments on the changed lines are corrected**, not replaced by new counts: `file_store.cpp`'s "starting from 1" index comment, and `tests/session/test_retrieve_with_gaps.cpp`'s "seq=1 maps to index 0".
- **D-538-4 (R-3). A failed `retrieve` in the resend walk is recorded.** When `retrieve` returns an error other than `store_seqnum_gap`, the walk emits `session_event_resend_slot_gap_filled{seq, code}` with the store's error, then folds the slot as today. The same applies when the walk's tail read (`next_seqnum`) fails.
  - The event exists (fixpp#424) and has one producer. Its comment in `session_event.hpp` and B&L `B-424-2` widen from "cannot be rebuilt as a replay" to "gap-filled for a reason other than absence or an administrative message".
  - The slot is still gap-filled. That is fixpp#424's ruling D4a: failing the resend leaves the peer's gap open.
  - `store_seqnum_gap` stays silent. It is the normal answer for a number that was never retained (`L-008-2`).
  - `store_cancelled` stays silent too. It means the session is closing, not that the store failed.

### 2.3 Rejected

- **A stored base** (a `MemoryStore` member, or a field in `FileStore`'s sentinel). It adds state that can disagree with the entries. In `MemoryStore` it changes an installed class's layout. In `FileStore`, bumping `kFormatVersion` makes old and new builds refuse each other's logs, because the open check is an equality.
- **A search by sequence number.** It would also serve a hole in the middle of a log. The session cannot make one: `store()` requires `seq == next`, and the only counter-only advance is the default `reset_to` body, before the first frame. A hole is still reported as a gap.
- **Keeping (1, 1) and re-storing the Logon.** It would change 093's reset unit and its commit boundary to work around a lookup.

## 3. #536 — one session's close ends other sessions' liveness loops

### 3.1 Mechanism and populations

- `Clock::cancel_sleeps()` completes every waiter on the clock with `operation_aborted`. Sessions of one Engine share the clock unless a session sets `clock_override`.
- **Sweeps issued by a session:** `Session::close` phase 2, and the LogoutSent arm when the peer's Logout arrives (`on_inbound_logout_sent_`). Re-derive with `git grep -n 'cancel_sleeps()' -- src include`; the other hits are the two clocks' destructors.
- **Raw `sleep_until` callers** (`git grep -n 'sleep_until(' -- src include`):
  - the liveness loop's main sleep and its TestRequest grace sleep. Its handler takes any `std::system_error` as "close fired" and returns.
  - `run_logout_phase1`'s wait. It reads a wake before the deadline as a timeout unless `logout_confirmed_` is set, so another session's sweep ends a graceful logout early.
  - `await_deadline` (`read_first_frame_bounded.hpp`), which already re-arms on a sweep that is not its own. The first-frame deadline, the establishment timeout and close's wait for a reset unit go through it.
- Reconnect backoff, the close grace timer and the transport deadlines use private timers. No sweep reaches them.
- **Both shipped clocks honour the awaiter's cancellation slot** (`mock_clock.cpp`, `cs.assign`; `system_clock_source.cpp`, `async_wait(use_awaitable)`), and `clock.hpp` requires it of every clock. The liveness loop is spawned bound to `root_cancel_.slot()`, and `close` emits `root_cancel_` right after its sweep.
- **The sweep is nevertheless what ends the liveness loop today** (§3.4). The loop sets its cancellation filter to total once, at its top. Every Heartbeat and TestRequest it sends takes the sequence-number lock, and the lock sequence ends by resetting the filter to terminal, "whatever filter the caller entered with" (`FIXPP_DETAIL_CO_AWAIT_LOCK`, part 4, `async_mutex.hpp`). asio keeps one cancellation state per coroutine thread, so after its first emit the loop no longer accepts `close`'s total cancellation.
- `close` then yields until the loop has left (`liveness_counter_`). With the sweep gone and nothing else changed, that wait lasts until the sleep ends by itself: one HeartBtInt on a real clock, and without end on a mock clock.

### 3.2 Decisions

- **D-536-1. A session never sweeps the clock.** Both session call sites of `cancel_sleeps()` are deleted.
- **D-536-2. `close` ends its own liveness loop through `root_cancel_`, and the loop makes that work.** Immediately before each of its two sleeps the loop leaves if the session is no longer Active, and then resets its cancellation filter to total. `close` keeps emitting total.
  - The reset is what §3.4 measures. Only the sleeps become cancellable by `close`, which is what the sweep reached, so an emit in flight is treated as today.
  - The state test before the grace sleep is new and is from reading: a close that runs after the TestRequest's write completed and before the loop resumes would otherwise find no sleep to cancel.
- **D-536-3. The logout wait gets a wake of its own.** `run_logout_phase1` races a per-session wake against `detail::await_deadline(clock, deadline)`, the pattern `close` already uses for the reset unit. The LogoutSent arm triggers that wake. Because the deadline arm is `await_deadline`, a sweep by anyone else no longer reads as a logout timeout.
- **D-536-4. The liveness loop's answer to an explicit clock-wide `cancel_sleeps()` is unchanged.** It still ends. That is what the `Clock` contract says the call does, and the test teardown helper `cancel_and_drain_or_report` depends on it to release the sleep of a session that was never closed.
- **D-536-5. `clock.hpp`'s `cancel_sleeps` comment is corrected.** It says the call signals each awaiter's cancellation slot. Neither clock does that: both complete the waiter directly, which is why `await_deadline` can tell a sweep from its own cancellation. The comment also states that a session does not call it. No signature changes.

### 3.3 Rejected

- **`close` emits terminal cancellation instead** (or `all`). It passes the terminal filter, so it needs no change in the loop. It would also cancel a liveness emit in flight, which the sweep never did. §3.4 runs it for attribution only.
- **Every sleeper re-arms on a sweep that is not its own** (the issue's second option). It would have to hold at all three raw sites. It also breaks `cancel_and_drain_or_report` and every test that ends an unclosed session's liveness loop with a sweep. A third-party clock whose `cancel_sleeps()` does nothing (the conformance test's `tp_clock`) would still never deliver the LogoutSent wake. And each close would still wake every sleeper on the clock.
- **Per-session cancellation on the `Clock` interface.** A new virtual exceeds the interface's cap (`clock.hpp`, `[const §XIV.2]`) and changes a public plugin contract. The per-operation cancellation slot already is per-waiter cancellation.
- **Session identity from `system_clock_source`'s slot map.** Sessions under an Engine do not sleep on a `session_executor`, so they take the per-call path, whose registry entry carries no owner. Read from the spawn sites, not measured.

### 3.4 Feasibility probe

A throwaway edit on the debug tree, never committed. The selection is every ctest registered from `tests/{session,core,interop,capi,alloc_guard,conformance,perf}`, run serially; the interop cells that need a live counterparty skip themselves. Outputs and diffs are under `build/b28-probe/` in the worktree.

| Run | Edit | Verdicts that differ from the baseline |
|---|---|---|
| baseline | none | only the three repro tests fail |
| P1 | the phase-2 sweep deleted | `TerminalClose_SharedClock` turns GREEN. Two tests fail because `close` no longer completes: `AdminDistinctNowTest.TR_DistinctNow_*` and `CrossSessionTestReqID.CrossSessionDisjoint`; the next cell of that binary then hangs |
| P1+P2 | plus the LogoutSent sweep replaced by a per-session wake raced against `await_deadline` | all five #536 cells GREEN. The two `close` failures remain. `AdversarialSessionTest.LogoutGracefulCancelMidSleep` fails: it ends a logout wait with its own `cancel_sleeps()`, which D-536-3 removes on purpose |
| round 2, B | P1+P2 plus the filter reset before each of the loop's two sleeps (D-536-2's reset) | all five #536 cells GREEN, at their baseline durations. Both `close` failures and the hang are gone. One test differs from the baseline: `LogoutGracefulCancelMidSleep`, as in P1+P2 |
| round 2, A | P1+P2 with `close` emitting `all`, no reset in the loop | the same verdicts as B on the three affected binaries. Not run on the full selection |

- The first design in this note said `close`'s emit alone would end the loop. P1 refuted it, and D-536-2 was rewritten.
- Every #536 cell took one HeartBtInt longer under P1 and P1+P2, because each cell's teardown closes sessions whose loops had already sent a frame.
- **Attribution, measured** with temporary prints on `AdminDistinctNowTest.TR_DistinctNow_*` (traces in `build/b28-probe/diag/`):
  - At `close`, the loop is parked in its grace sleep with the mock clock's cancellation handler installed. `emit(total)` does not invoke the handler. `close` then spins in its `liveness_counter_` wait until the test's failure path sweeps the clock.
  - Emitting `all` instead invokes the handler with the terminal bit only. The filter is terminal at that sleep.
  - A filter reset placed before the loop's `assign_outbound()` changes nothing. Placed after it, the handler is invoked by `emit(total)` and the cell passes. So the filter is replaced inside `assign_outbound()`.
  - That the replacing statement is the lock sequence's part 4, and that asio applies it to the whole coroutine thread, is read from source.
- The Heartbeat sibling `HB_DistinctNow_*` passed under P1 because its loop had sent nothing: its Heartbeats are replies on the inbound path. The condition is whether the loop has called `assign_outbound()` before `close`, not which message it sent.
- `TerminalClose_SharedClock` turning GREEN under P1 shows B kept beating. It does not show that `close` woke A's loop: the cell took one HeartBtInt longer.

## 4. #537 — a second too-high frame while a resend is outstanding

### 4.1 What the documents and the engines say (R-2)

**FIX documents** (local copies; cited, not committed):
- Session Layer, November 2020, §4.8.2, p. 34. Two approaches are allowed. The first: "if the receiver misses the second of five messages, the application could ignore messages 3 through 5 and generate a resend request for messages 2 through 5, or, preferably 2 through 0". The second: save the newer messages and request only the missed ones. "In both cases, messages 3 through 5 should not be processed before message 2."
- Session Layer §4.10, p. 43, precedence 11 and 12: "queue message, and send ResendRequest(35=2)"; "Queue incoming messages with MsgSeqNum(34) too high."
- FIX 4.4 session protocol, ResendRequest: "the application should ignore 8 and 9 and ask for a resend of 7-9, or, preferably, 7-0 ... This latter approach is strongly recommended".
- Session Test Cases, June 2020, scenario 2b: "MsgSeqNum(34) higher than expected. Respond with ResendRequest(35=2) message."
- No document says to end the session. No document bounds how long a resend may stay outstanding, except "a reasonable period of time" in precedence 15 (a too-high Logout).

**Engines**, at the commits named:

| Engine | The frame | A second ResendRequest | The session | A bound on an unfilled gap |
|---|---|---|---|---|
| QuickFIX C++ `386ce46e`, `Session::doTargetTooHigh` | queued | no ("Already sent ResendRequest ... Not sending another"), unless `SendRedundantResendRequests` | stays logged on | none explicit. A too-high frame does not refresh its last-received time, so its heartbeat timeout ends a session whose gap never closes |
| QuickFIX/J `db43e4d4`, `Session.doTargetTooHigh` | enqueued | no, same rule | stays logged on | none. `verify` refreshes last-received time first |
| QuickFIX/Go `eff945dd`, `inSession.processReject` | stashed | no ("Assumes target too high reject already sent") | stays in its resend state | none |

**Reading.**
- Settled by every source: the session survives, and no second ResendRequest is sent.
- Not uniform: what happens to the frame. The engines keep it. §4.8.2 and the 4.4 text allow ignoring it when the request runs to 0, and fixpp's request always does (`16=0`, `inbound_resend_request_leaf_`).
- fixpp already ignores the **first** too-high frame and relies on the replay. `tests/interop/parity/replay_subsumes_reorder_queue_test.cpp` records that as a decision ("NO reorder-queue implementation by design").
- An unfilled gap: QuickFIX/J and QuickFIX/Go wait without limit. QuickFIX C++ is bounded by its heartbeat timeout, as the table says. fixpp cannot copy that mechanism: a too-high frame refreshes its inbound liveness (`B-093-9`, owner ruling R-1 on fixpp#516).

### 4.2 Mechanism

- The too-high arm in `on_inbound_active_` is guarded by `!reconnect_fsm_.is_awaiting_resend()`, so a second too-high frame skips it.
- `SeqnumManager::check_inbound` returns `session_seqnum_too_high`. The comment above the call says such a frame advances; it does not.
- `on_inbound_active_out_of_sequence_` drops a Heartbeat, handles a PossDup frame, handles the knob-off case, and then falls into "Arm B", which records `Disconnected` for any remaining error. It consults the error only for the overflow case.

### 4.3 Decisions

- **D-537-1 (R-2). A too-high frame that arrives while a resend is outstanding is ignored.** Before the deadline of D-537-5, the session stays in its state, sends nothing, and does not advance. The outstanding request already covers the frame, because it runs to 0 and the peer answers it after sending the frame. This is §4.8.2's first approach and fixpp's existing treatment of the first too-high frame.
- **D-537-2. The change is in `on_inbound_active_out_of_sequence_`, ahead of "Arm B":** `session_seqnum_too_high` returns success. A too-high error reaches this function only while a resend is outstanding or with the knob off, and the knob-off arm returns before it. A too-low frame without PossDup stays fatal.
- **D-537-3. The PossDup arms keep their order.** A too-high PossDup frame is still validated (Stage 1) and then dropped or redelivered (Stage 2), as today.
- **D-537-4. The comments that describe the old route are corrected**: the one at the `check_inbound` call, the function's header comment, and "Too-low Heartbeat", which takes any out-of-sequence Heartbeat.
- **D-537-5 (R-2). An unfilled gap has a deadline.**
  - **The clock.** The session records a steady time point when it enters AwaitingResend, and again each time NextNumIn advances while the resend is outstanding.
  - **The check.** It runs when a frame fails the sequence check as too high while a resend is outstanding, whatever its type. It sits at the top of `on_inbound_active_out_of_sequence_`, after the overflow arm and before that function's Heartbeat and PossDup arms. No timer is added.
  - **The bound** is 2 × HeartBtInt since that time point, computed with the saturating `detail::deadline_after`.
  - **On expiry** the session sends a Logout whose Text(58) names the unanswered resend, and enters `Disconnected`, through the existing inbound Logout leaf.
  - **Off** when HeartBtInt is 0 or the session has no clock, as the liveness loop is. A bound of zero would end the session at the second frame, which is the defect.
  - **Not configurable.** The ruling names the value.

### 4.4 Rejected

- **Queue the frame** (§4.10; the engines). 013 FR-009 specified a queue, and `ResendState::inbound_held` exists with no writer. It needs a capacity policy, an order for held administrative frames, and a delivery path, and it gains nothing on the wire while the request runs to 0. `L-013-2`, which describes the queue as shipped, is corrected (§6).
- **A second ResendRequest per extra frame.** All three engines suppress it by default.
- **Stop refreshing inbound liveness on a too-high frame**, which is what bounds QuickFIX C++. It reverses owner ruling R-1 on fixpp#516 (`B-093-9`).
- **No deadline** (QuickFIX/J and QuickFIX/Go). Where the peer never fills the gap (`L-092-1`, `L-092-2`), the session would stay Active and ignore every new message until the connection ends. Not taken (R-2).
- **A timer for the deadline.** It would also end a gap during which the peer sends nothing. The liveness loop already covers a silent peer, and the peer's Heartbeat answering the TestRequest is itself a too-high frame, so the check runs.

### 4.5 What still differs from QuickFIX after this change (R-5)

- **The frame is ignored, not held** (R-2). For an application message nothing observable differs: the request runs to 0, so the peer replays it, and it is delivered once and in order. The engines receive that replay too and discard it as a duplicate.
- **A held TestRequest** is answered by the engines once the gap closes. fixpp never answers it, because the peer gap-fills over it. A too-high TestRequest as the *first* frame already behaves this way.
- **A too-high ResendRequest or Logout** is acted on by the engines whatever its number. fixpp does not act on it. That is fixpp#577, and it is not changed here.
- **An unfilled gap** ends a fixpp session with a Logout after 2 × HeartBtInt without progress (R-2). QuickFIX C++ disconnects at its heartbeat timeout with no Logout; QuickFIX/J and QuickFIX/Go wait. The engines differ among themselves, so there is no single behaviour to keep.

## 5. Tests and gates

Each new or changed cell is shown RED before the fix. Each decision gets an arm that goes RED when the fix is deleted.

**#538**
- The lifted cells of `session_538_*`: the claim cells turn GREEN and the controls stay GREEN.
- New, RED first: `FileStore` reopened after a (·, 2) reset and two stores, then `retrieve`.
- New, RED first: a store that inherits the default `reset_to` body, then `store(2)` and `retrieve`.
- New: after a (·, 2) reset and two stores, `retrieve(1, 1)` and `retrieve(1, 0)` each report a gap and visit nothing, and `retrieve(2, 0)` visits 2 and 3. A gap stops a range walk today, and that stays. The resend walk asks one slot at a time, so it does not depend on it.
- D-538-4, RED first: a store whose `retrieve` fails with an I/O error yields the event with that code, and the range is still gap-filled. Control: a plain gap yields no event. A second cell for the tail read.
- `run_q28` in `test_memory_store_round_trip.cpp` discards a `retrieve` result on exactly this state. It is noted, not changed.

**#536**
- The lifted cells of `session_536_*`: both shared-clock cells turn GREEN. The failure string is reworded to what the cell observes.
- New, RED first: a clock whose `cancel_sleeps()` does nothing. A graceful close returns when the peer's Logout arrives, not at the logout timeout.
- Changed: `AdversarialSessionTest.LogoutGracefulCancelMidSleep` pins the old mechanism (a sweep ends the logout wait). It becomes the cell for the new one: a sweep by a third party during the logout wait does not end it; the confirmation, the deadline or the grace timer does.
- New, RED when the filter reset is deleted: `close` completes promptly for a session whose liveness loop has sent a Heartbeat, and for one that has sent a TestRequest, on a mock clock. `AdminDistinctNowTest.TR_DistinctNow_*` and `CrossSessionTestReqID.*` are the existing witnesses.
- New: a counting clock shows that open, traffic, graceful close and terminal close issue no `cancel_sleeps()` call. This is the arm that catches a third sweep site.
- Control kept: an explicit `cancel_sleeps()` still ends an unclosed session's liveness loop (D-536-4).

**#537**
- The lifted cells of `session_537_*`: `SecondTooHigh` turns GREEN on both roles.
- New, per message type, both roles: a second too-high TestRequest, application message, Reject, GapFill without PossDup, and Logon. Each leaves the session Active, sends nothing, and leaves NextNumIn unchanged; the replay through the highest number then completes.
- A second too-high ResendRequest and Logout: the cells assert only that the session is not ended and NextNumIn does not move. What fixpp should send is fixpp#577's question, so it is left unasserted with a pointer.
- Discriminating control: a too-low frame without PossDup is still fatal, and the overflow arm is unchanged.
- The deadline, on a mock clock, both roles:
  - no progress: a too-high frame before the bound is ignored, and one after it draws the Logout and `Disconnected`;
  - progress: a replay that takes longer than the bound in total, with NextNumIn advancing inside each window, completes. This is the arm that fails if the time point is not refreshed;
  - a too-high Heartbeat after the bound also ends the session;
  - HeartBtInt = 0: no deadline;
  - the fix-deleted arms: without the check the first cell stalls, and with a zero bound `SecondTooHigh` is RED again.
- `Disclosed_L1_*` and `Disclosed_L2_*` pin the fatal outcome today. Their traces change: the frames are ignored, and the session ends with a Logout at the deadline.

**Gates**
- A full `ctest` in `linux-clang-debug`, then ASan, TSan and UBSan trees. TSan matters most for #536.
- An MSVC leg: `session.cpp` and three public headers change. It needs the shared sandbox and its lock.
- Every Tier 1 source-audit job is run locally before the labelled push.
- Coverage is assessed per changed line (`[const §IX.1]`).

## 6. Disclosures and documents

**B&L** (`spec/behaviors-and-limitations.md`; rows are written at implementation):
- New rows for the three behaviours and for D-538-4.
- `L-092-1`, `L-092-2`, `L-093-7`: the last step changes from "the session ends silently at the peer's next new message" to "the peer's new messages are ignored, and the session ends with a Logout once NextNumIn has not advanced for 2 × HeartBtInt". With HeartBtInt = 0 it does not end.
- `L-013-2`: corrected. No hold queue is implemented; the replay stands in for it.
- `B-424-2`: the event's second producer.

**C ABI.** A C session runs on `MemoryStore` and on this session code, so all three changes are observable through `fixpp_session_*`. By the precedent of 092 and 093 this is a `version.h` entry declared BREAKING under `[const §X.7]`, with the next minor. ⚠️ B41 may claim the same minor; the second PR to merge renumbers. No declaration changes. Each effect needs a C-ABI witness, not only a C++ one.

**Design documents** (appended at the end of each file, never inserted; frozen `specs/` are not edited):
- `.specify/2d-threading.md`: an erratum on `cancel_sleeps` (who calls it; what it does to a waiter).
- `.specify/2e-msgstore.md`: an erratum on `retrieve` after a reset.
- `brain/components/session.md` and `message-store-quiescence.md`: pointers to this note, and the rejected alternatives.
- Header comments at each superseded decision name this note's issues, as `async_mutex.hpp` names its erratum.

## 7. Risks Gate A should press on

1. **D-536-2:** is there another await in the liveness loop where a close can leave it parked? Does resetting the filter before a sleep erase a cancellation that was already recorded, and does the state test in front of it cover that? The lock sequence's terminal reset reaches every caller of the lock, not only this loop: which other coroutine relies on total cancellation after taking it?
2. **D-536-3's race inside `close`'s own race** with the grace timer: the cancellation filter, the executor, and the noexcept window.
3. **D-537-1's coverage argument** assumes the peer answers the request in order on one connection. What if the request was never sent (its build failed)?
4. **D-537-5's deadline:** can a slow but healthy replay trip it? Is every site that advances NextNumIn during a resend a refresh site? What does a peer see that keeps sending after the Logout (`Disconnected` does not close the transport, fixpp#534)?
5. **D-538-1's `front()`** on an empty direction, and on a bounded `MemoryStore` at capacity.
6. **D-538-4's event volume** on a long range over a failed store.
7. **The C-ABI disposition:** is each change BREAKING in `[const §X.7]`'s sense, or a correction?

## 8. What this does not decide

- **fixpp#577:** a too-high ResendRequest or Logout is discarded rather than acted on. Unconfirmed.
- **fixpp#578:** the awaiting-resend flag across an in-place reconnect. Unconfirmed.
- **fixpp#535** (`test_request_threshold`, B27) lives in the liveness loop. This note does not touch it. While #536 is open, a #534 cell must use one session per clock.
- **B41** (fixpp#563, #564) changes the send path in the same file. The second PR to merge rebases and re-runs its cells.
- Noticed and left alone: `tls_transport.hpp` says its handshake timeout uses `Clock::sleep_until`, and the code uses a private timer; `reconnect_fsm.hpp` declares three timers nothing reads (fixpp#559's class); `FileStoreImpl::write_frame` has no caller.

## Gate A

Not run.
