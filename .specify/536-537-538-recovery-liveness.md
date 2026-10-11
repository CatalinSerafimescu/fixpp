# fixpp#536 / #537 / #538 — session recovery and liveness (B28)

> **Status: v0.3 — Gate A round 1 applied (2026-10-11).** Round 1 changed: a resend answer aborts on a store read failure, where v0.2 gap-filled it (R-7); a too-high Heartbeat answers a pending TestRequest while a resend is outstanding, so an alive peer reaches the deadline's Logout (R-6, §4.6); C ABI 1.12 is declared on each affected declaration (R-8, §6.2).
> Every open question of v0.2's §7 is now a sentence in a decision plus a cell, or an owner item. The dispositions are under `## Gate A`.
>
> - **Base:** `origin/main` = `22702dc9`. Code is named by symbol, with a recipe where a population matters; re-derive before relying on one.
> - **Branch:** `536-537-538-recovery-liveness`, in the `fixpp-parallel` worktree.
> - **Mode:** issue mode (no `specs/` bundle). Records go to the parent's `decisions/speckit/`.
> - **Gate A triggers touched** (`[const §XVII.1]`): the session FSM and recovery (#537), the message store contract (#538), cancellation (#536), the C ABI (1.12, BREAKING, R-8), and comment text in public headers. Installed C++ headers also change in `session.hpp` (one new private member of `Session`; a text parameter on a private leaf) and in `reconnect_fsm.hpp` (one new non-virtual member of `ReconnectFsm`).
> - **How to read a claim:** *measured* means a run is named; *read* means read from source at the base; *reasoned* means neither. §3.4 is measured. The timelines of §4.6 are reasoned until the cells named there run.

## 0. Owner rulings (2026-10-11). Gate A reviews whether this note implements them.

- **R-1.** Builds for this batch are approved, and may run at the same time as B41's.
- **R-2 (#537).** First: "Check official FIX documentation and QuickFIX C++/J/Go then come back if not settled." §4.1 is that check. It did not settle what happens to the frame, so the owner chose between ignoring it, ignoring it with a deadline on an unfilled gap, and queueing it. **Ruling: ignore the frame, and end the session with a Logout when NextNumIn has not advanced for about 2 × HeartBtInt while a resend is outstanding.** The deadline is checked when a too-high frame arrives.
  - **Reconfirmed after round 1, under R-5.** The owner was told that all three engines queue the frame, and kept the ruling. Application messages are delivered once and in order either way. The observable differences are in administrative frames and are listed in §4.5. fixpp#577 is where a too-high ResendRequest and Logout get the engines' treatment. Implemented by D-537-1 and D-537-5.
- **R-3.** The resend walk's silent fold of a failed `retrieve` into a GapFill is fixed in this batch (D-538-4). R-7 says how.
- **R-4.** Two further leads are filed and are not in this batch: fixpp#577 and fixpp#578 (§8).
- **R-5 (given on B41 the same day; applies here).** "Overall we try not to change behavior of QuickFIX, the internal implementation can be different." Where the three engines agree, that is fixpp's default, and a difference must be stated with its reason. §4.5 states the differences that remain.

Rulings made after round 1 (2026-10-11):

- **R-6 (triage O-1; the consult).** An unfilled gap is two conditions, with one ending each.
  - (a) The peer is alive but does not fill the gap: the D-537-5 deadline. A Logout naming the unanswered resend, once NextNumIn has not advanced for 2 × HeartBtInt, checked when a too-high frame arrives.
  - (b) The peer answers nothing: today's dead-peer ending (an unanswered TestRequest, then a silent `Disconnected`). It is unchanged, and the same with or without a gap.
  - The fix that stops an alive peer being misjudged as dead goes in the inbound arm: while a resend is outstanding, a too-high Heartbeat received while a TestRequest is pending answers it. Implemented by D-537-6.
  - Not chosen: two endings for (a); the liveness-loop form (the consult's B′); a timer; a Logout on the dead-peer ending.
- **R-7 (triage O-2 and C-12; supersedes the v0.2 form of R-3's implementation).** In the resend walk, a store read failure other than `store_seqnum_gap` and `store_cancelled` aborts the answer, with the disposition of the walk's existing truncated-capture arm. Nothing is gap-filled that the store merely failed to read. The same holds for a failed tail read. No event type is widened. R-3's "not silent" is met by the abort. Implemented by D-538-4.
- **R-8 (triage C-4).** C ABI 1.12 is declared BREAKING: a 1.12 clause on each affected declaration in `include/fix/c_api/session.h`, plus the `include/fix/c_api/version.h` entry, following the 1.10 and 1.11 precedent. Implemented by §6.2.
- **Triage C-3 is a disclosure, not a fix.** The owner was told and did not object. After the deadline's Logout the state is `Disconnected` and the transport stays open until the peer closes it or `close()` / `Engine::stop()` runs (fixpp#534, batch B27). Stated in D-537-5 and §4.6; asserted by the deadline cell (§5).

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
- Either role's arm can pass an inbound target of 2 (`unit_in`, when the reset Logon was consumed). D-538-1 is per direction, so it covers both. Read.
- Both stores clear their entries and set the counter to 2. The next `store(2)` appends at position 0.
- Both stores look sequence `s` up at position `s - 1`, in `MemoryStore::retrieve` (`include/fixpp/session/memory_store.hpp`) and `FileStore::retrieve` (`src/session/file_store.cpp`). Each then checks `entry.seq != s`, so a wrong frame is never returned; the lookup reports a gap.
- The resend walk (`Session::replay_outbound_range_`) reads the result as a boolean, `if (rr && cv.captured)`, and folds the slot into a GapFill.
- The default `MessageStore::reset_to` body produces the same state: `reset()`, then a counter-only advance.
- `FileStore` rebuilds its index from the record headers at open, in file order, so the defect survives a restart. Not measured; it is a required cell (§5).
- Under a Session the inbound direction holds no frame. The session never stores an inbound frame; it only advances the inbound counter (`persist_inbound_advance_`). Read; re-derive with `git grep -n 'direction_t::inbound' -- src/session/session.cpp`.

Re-derive the two lookup sites with `git grep -n -E 's - 1\)' -- include/fixpp/session src/session`.

### 2.2 Decisions

- **D-538-1. Both stores look a sequence up relative to the first retained entry of that direction.** Position = `s - first.seq`. A sequence below `first.seq` is a gap. The `entry.seq != s` check stays.
  - **An empty direction is a gap for every requested number.** The emptiness test precedes any read of the first entry, and `s < first.seq` is tested before the subtraction. Today the bound test (`idx >= entries.size()`) gives that protection, and a literal `front()` would drop it.
  - The empty direction is the first state the store reaches, and a peer can reach it. After `reset_to(·, 2)` the outbound direction is empty with the counter at 2. A ResendRequest 7=1 16=0 that arrives before fixpp sends anything makes the walk ask `retrieve(1, 1, outbound)`: `our_last` is 1, so the walk's empty-store branch is not taken. Read and reasoned; a cell in §5.
  - Reporting is unchanged: `retrieve` visits the frames before the first gap, returns one `store_seqnum_gap`, and stops. The empty-range success (`begin > tail_end`) still comes first.
  - `MemoryStore` gains no member: the base is `entries.front().seq`. The class's layout is unchanged, which matters because the header is installed.
  - `FileStore` needs no format change: the base is `index.front().seq`, which the open scan already rebuilds from the record headers. A log written after a (·, 2) reset by a build without this fix becomes readable.
  - A bounded `MemoryStore` at capacity refuses the next `store()` and appends nothing, so its entries stay one contiguous run from the first. Read from `store()`'s capacity return; a cell in §5.
- **D-538-2. The contract says so.** `message_store.hpp`'s `retrieve` comment gains: a frame stored after `reset()` or `reset_to()` is retrievable under its own sequence number, a sequence below the first retained one is a gap, and so is every number of an empty direction. This binds third-party stores. The default `reset_to` body is unchanged.
  - A third-party store with an absolute lookup that inherits the default `reset_to` body already shows #538 today. D-538-2 makes that store non-conforming; it does not break one that worked. One B&L row tells store implementers (§6).
- **D-538-3. Stale comments on the changed lines are corrected**, not replaced by new counts: `file_store.cpp`'s "starting from 1" index comment, and `tests/session/test_retrieve_with_gaps.cpp`'s "seq=1 maps to index 0".
- **D-538-4 (R-3, R-7). A store read failure aborts the resend answer.**
  - **The slot read.** In `replay_outbound_range_`, when a slot's `retrieve` returns an error other than `store_seqnum_gap` and `store_cancelled`, the walk stops and returns `dispatch_aborted`.
  - **The disposition is the truncated-capture arm's** (`cv.truncated` in the same walk), read from source:
    - the walk returns at once. Frames already replayed stay sent. An open GapFill run is not flushed. Nothing more is written;
    - `on_inbound_active_resend_request_` records `Disconnected`, returns the error, and skips the inbound persist;
    - `honor_peer_next_expected_` (the Logon 789 path) does the same, unless its arm was superseded (`logon_arm_superseded_`); then it records nothing and returns `superseded`;
    - under an Engine, `run_read_pump` (`src/session/engine.cpp`) sees the failed `on_inbound_frame` and runs `close(terminal)`, which closes the transport. No Logout is sent. A Session driven without an Engine returns the error to its driver.
  - **The tail read.** When the walk's `next_seqnum(outbound, false)` fails with an error other than `store_cancelled`, the walk returns `dispatch_aborted` before it writes anything. Today the failure is dropped, `our_last` stays 0, and the whole range becomes one GapFill.
  - **`store_seqnum_gap` keeps today's fold.** It is the normal answer for a number that was never retained (`L-008-2`).
  - **`store_cancelled` keeps today's path at both reads.** It means the session is closing, not that the store failed.
  - **What each shipped `retrieve` can return**, read from the `co_return std::unexpected` sites of the two bodies:

    | Code | When | Walk |
    |---|---|---|
    | `store_seqnum_gap` | the number is not retained | fold, as today |
    | `store_cancelled` | the store's lock was cancelled or drained | as today |
    | `store_io_failure` | `FileStore`: the log is not open, or a payload read failed. Both stores: a reset ran between the index snapshot and the visit | abort |
    | `store_visitor_aborted`, or a visitor's own error | the visitor threw or asked to abort. The walk's `CaptureVisitor` does neither | abort |
    | `store_invalid_range` | `end < begin` | cannot occur: the walk asks `(k, k)` |
    | `store_seqnum_invalid` | `begin == 0` | **owner item, §7.** Until ruled, the fold of today |
    | any other code from a third-party store | | abort |

  - **`store_seqnum_invalid` is the one place the ruling's wording and its reach may differ.** A ResendRequest with BeginSeqNo(7) = 0 or absent starts the walk at slot 0 when the range is not empty, and both stores refuse `retrieve(0, 0)` before reading anything. R-7's words are "a store read failure"; this is an argument refusal. Taken literally, R-7 would make such a request end the session, where today the slot is folded. This note keeps the fold and puts the question to the owner (§7). Whether a dictionary validator rejects 7=0 before the walk was not read.
  - **No event is added.** `session_event_resend_slot_gap_filled` keeps its one producer and its meaning, and `B-424-2` is not edited. fixpp#424's ruling D4a covers a stored frame whose replay cannot be built; it is not extended to a frame the store could not read.
  - **R-3 is met by the abort:** the failure ends the session and is returned from `on_inbound_frame`.
  - **This ending closes the transport; D-537-5's does not.** The abort returns an error, so the read pump closes. The deadline's Logout returns success, so the transport stays open (fixpp#534).

### 2.3 Rejected

- **A stored base** (a `MemoryStore` member, or a field in `FileStore`'s sentinel). It adds state that can disagree with the entries. In `MemoryStore` it changes an installed class's layout. In `FileStore`, bumping `kFormatVersion` makes old and new builds refuse each other's logs, because the open check is an equality.
- **A search by sequence number.** It would also serve a hole in the middle of a log. The session cannot make one: `store()` requires `seq == next`, and the session stores no inbound frame. The counter-only advances are the default `reset_to` body, before the first frame, and `next_seqnum(dir, true)`, which the session uses only on the inbound direction. A hole is still reported as a gap.
- **Keeping (1, 1) and re-storing the Logon.** It would change 093's reset unit and its commit boundary to work around a lookup.
- **Gap-fill the unreadable slot and record an event** (v0.2's D-538-4). Superseded by R-7. A read error can be transient; a GapFill is permanent on the wire. No engine does it by default (§4.5).
- **Abort only on a persistent store.** R-7 applies to every store.

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
  - Why the state test is enough, read: `close` writes `Disconnected` and then emits `root_cancel_` with no suspension between the two. A loop that missed the emit under a terminal filter therefore meets the state test when it resumes.
  - B41's ruling on `Session::close` makes that window reachable from an application: a close started inside the loop's own `toAdmin` begins one post later, while the loop's emit is suspended. B41 has no cell for it. The cell is this note's (§5).
- **D-536-3. The logout wait gets a wake of its own.** `run_logout_phase1` races a per-session wake against `detail::await_deadline(clock, deadline)`, the pattern `close` already uses for the reset unit (`reset_unit_wake_`: a private timer that the wake cancels, awaited through `as_tuple`). The LogoutSent arm triggers that wake. Because the deadline arm is `await_deadline`, a sweep by anyone else no longer reads as a logout timeout.
  - The LogoutSent arm reaches the wake through a new private member of `Session`, beside `close_grace_wake_` and of the same kind. **This changes `Session`'s layout: C++ code built against the header without it must be rebuilt.** No source change is needed.
  - The wake is cleared on every exit from the wait, a throw included, so a late Logout cannot call into a finished wait.
  - **The outcome is read from `logout_confirmed_`, never from which arm won.** A cancelled wake completes through `as_tuple` exactly as a fired one does.
  - **`logout_confirmed_` is tested before the race is armed as well as after it**, so the design does not rest on how the race's arms are launched.
  - **The race sits inside a `try`.** When the outer cancellation landed during the Logout emit, the `co_await` of the race throws `operation_aborted` at its pre-check, and `run_logout_phase1` is `noexcept`.
  - Checked and found to hold. Read and reasoned, not measured; the cells are in §5:
    - the wait is armed with no suspension after the LogoutSent write, and the inbound arm runs on the same serialisation domain, so a Logout cannot arrive between the two;
    - `operator||` resumes only when both arms have finished, so `run_logout_phase1` has cleared its wake before `close` continues past its race with the grace timer;
    - a terminal close that escalates cancels the grace timer (`close_grace_wake_`); the outer group then cancels the phase-1 arm, the terminal bit passes that arm's filter, and the inner race retires;
    - a Session runs phase 1 once, so no older waiter exists to guard against.
- **D-536-4. The liveness loop's answer to an explicit clock-wide `cancel_sleeps()` is unchanged.** It still ends. That is what the `Clock` contract says the call does, and the test teardown helper `cancel_and_drain_or_report` depends on it to release the sleep of a session that was never closed.
  - **So the issue's failure stays reachable through a public call, and it is disclosed.** After D-536-1 no library code sweeps outside the clocks' destructors. `Engine::clock()` hands the shared clock to any caller. One `cancel_sleeps()` on it ends every open session's liveness loop on that Engine: each session stays Active and sends no Heartbeat and no TestRequest, and nothing restarts the loop. Reasoned from the loop's handler.
  - A B&L limitation row says so (§6), and D-536-5's comment says the same.
  - Not taken: the loop telling a foreign sweep from its own cancellation, as `await_deadline` does, and re-arming. §3.3's second bullet rejects it.
- **D-536-5. `clock.hpp`'s `cancel_sleeps` comment is corrected.** It says the call signals each awaiter's cancellation slot. Neither clock does that: both complete the waiter directly, which is why `await_deadline` can tell a sweep from its own cancellation. The comment also states that a session does not call it, and what the call does to open sessions (D-536-4). No signature changes.

### 3.3 Rejected

- **`close` emits terminal cancellation instead** (or `all`). It passes the terminal filter, so it needs no change in the loop. It would also cancel a liveness emit in flight, which the sweep never did. §3.4 runs it for attribution only.
- **Every sleeper re-arms on a sweep that is not its own** (the issue's second option). It would have to hold at all three raw sites. It also breaks `cancel_and_drain_or_report` and every test that ends an unclosed session's liveness loop with a sweep. A third-party clock whose `cancel_sleeps()` does nothing (the conformance test's `tp_clock`) would still never deliver the LogoutSent wake. And each close would still wake every sleeper on the clock.
- **Per-session cancellation on the `Clock` interface.** `Clock` is in the pluggable roster (`.specify/api-contract.md` §9), and adding a pure virtual to a roster interface is a listed breaking effect (§11 there): it invalidates third-party clocks. The per-operation cancellation slot already is per-waiter cancellation. v0.2 also argued from the cap of `[const §XIV.2]`; that was wrong, because one more pure virtual stays within it.
- **Session identity from `system_clock_source`'s slot map.** Sessions under an Engine do not sleep on a `session_executor`, so they take the per-call path, whose registry entry carries no owner. Read from the spawn sites (the role loops on the session strand, the liveness loop on the delivering coroutine's executor), not measured.

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

### 3.5 Where else the lock's filter reset matters

The reset to terminal is the lock sequence's, not the liveness loop's, and it dates from 006 (PR #73), not from B35: `git log -S'enable_terminal_cancellation{}' -- include/fixpp/core/sync/async_mutex.hpp`. Every coroutine thread that takes a lock leaves with a terminal filter. Sites that ask for total cancellation: `git grep -n 'enable_total_cancellation' -- src include`. Every hit that is code falls into one row below.

| Kind | Members | After a lock on the same thread |
|---|---|---|
| An operation that resets the filter to total at its own entry, then awaits | the transports' read, write, connect and handshake; the listener's accept; the certificate source; `mock_transport`; `await_deadline` | unaffected: the reset is repeated at each use |
| A coroutine that sets total once and relies on it at a later await | the liveness loop; the read pump and the two role loops in `engine.cpp`; `ReconnectFsm`'s attempt loop; the two reset-unit sites; `Engine::send`'s control-strand lambda and its session-strand lambda | affected only where the later await is not of the first kind |
| The lock sequence itself | part 2 of `FIXPP_DETAIL_CO_AWAIT_LOCK`: total, for the acquisition only | it is the mechanism: part 4 then sets terminal |

- **The liveness loop** awaits `Clock::sleep_until`, and neither shipped clock resets the filter. This is the member §3.4 measured, and D-536-2 moves it to the first kind.
- **The read pump and the role loops** await transport operations and `await_deadline`, which are of the first kind. Read, not measured.
- **`Engine::send`'s two lambdas** await a strand hop and `Session::send`. Neither sleeps on the clock. Read.
- **`ReconnectFsm`'s attempt loop** (`drive_reconnect_attempt`), traced by reading. Its awaits are the private backoff timer, the certificate source's `load_credentials`, the transport's `async_connect`, and `async_handshake`. None of the shipped classes behind the last three takes an `async_mutex`: `git grep -c -E 'async_lock|FIXPP_DETAIL_CO_AWAIT_LOCK' -- src/tls/file_cert_source.cpp src/transport/` matches nothing, and the same expression matches in `src/session/seqnum_manager.cpp`. So a total filter set in that loop still stands at the next backoff wait. Reasoned from that reading. Condition: a third-party certificate source or transport that takes a lock would change it.
- **The reset-unit sites** reset after the store operation and then run their persists; they do not sleep.
- **A third filter state, read:** `Session::close` resets its caller's thread to `disable_cancellation` and does not restore it, and the read pump awaits `close` inline. Other sites that disable cancellation: `git grep -n 'disable_cancellation{}' -- src include`. None is a clock sleeper.
- **What this change can newly expose** is only a clock sleeper that the sweep used to wake: the raw `sleep_until` callers of §3.1. The liveness loop's two sleeps get the reset, and the logout wait moves onto `await_deadline`.
- One more consequence, read and not measured: the filter at a liveness sleep depends on the last callee. A store leaves it terminal; a real transport's write then sets it to total. The fixture in §3.4 sends through a callback, so it stayed terminal. D-536-2 removes that dependence.

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
- `on_inbound_active_out_of_sequence_` drops a Heartbeat, handles a PossDup frame, handles the knob-off case, and then falls into "Arm B", which records `Disconnected` for every remaining error. Those include `session_already_closed` from a drained sequence-number mutex. The function consults the error only for the overflow case.

### 4.3 Decisions

- **D-537-1 (R-2). A too-high frame that arrives while a resend is outstanding is ignored.** Before the deadline of D-537-5, the session stays in its state, sends nothing, and does not advance. The outstanding request already covers the frame, because it runs to 0 and the peer answers it after sending the frame. This is §4.8.2's first approach and fixpp's existing treatment of the first too-high frame.
  - The argument assumes the peer sent the frame before it answered the request. A frame the peer sends during its replay is ignored and not replayed. The next frame then opens a new gap, and a second ResendRequest fetches it. Recovery holds, one frame later. Reasoned; a cell in §5.
- **D-537-2. The change is in `on_inbound_active_out_of_sequence_`, ahead of "Arm B":** an error equal to `session_seqnum_too_high` returns success. Every other error stays fatal, `session_already_closed` included. A too-high error reaches this function only while a resend is outstanding or with the knob off, and the knob-off arm returns before it. A too-low frame without PossDup stays fatal.
- **D-537-3. The PossDup arms keep their order.** A too-high PossDup frame is still validated (Stage 1) and then dropped or redelivered (Stage 2), as today.
- **D-537-4. The comments that describe the old route are corrected**: the one at the `check_inbound` call, the function's header comment, and "Too-low Heartbeat", which takes any out-of-sequence Heartbeat.
- **D-537-5 (R-2, R-6 a). An unfilled gap has a deadline.**
  - **The clock.** The time point is `ResendState::started_at`, which exists and has no writer today. A new non-virtual member of `ReconnectFsm` writes it; `exit_awaiting_resend` already clears it with the rest of the state. The session writes it when it enters AwaitingResend, and again each time NextNumIn advances while the resend is outstanding. No class layout changes. The field's comment in `resend_state.hpp` changes with its meaning: it promised a recovery-elapsed metric, and a field refreshed on each advance is no longer that.
  - **The check.** It runs when a frame fails the sequence check as too high while a resend is outstanding. It sits at the top of `on_inbound_active_out_of_sequence_`, after the overflow arm and before that function's Heartbeat and PossDup arms. No timer is added.
  - **Which frames reach it.** Only a frame that gets as far as `check_inbound` in `on_inbound_active_`. A too-high frame stopped earlier does not run the check. Read, arm by arm:
    - a Reject, and the session stays up: the validate gate's `reject` outcome (`on_inbound_active_validate_failed_`), and PossDup Stage 1's Arm C with its unparseable-122 branch (`on_inbound_active_poss_dup_`). Their `consume_rejected_seqnum_` does nothing for a too-high number;
    - the session ends there: Stage 1's Arm D (Reject, Logout, `Disconnected`), the validate gate's `parse_failed` outcome (a close), and the BeginString, CompID and SendingTime guards.
  - **Ahead of every callback of its arm** (a condition B41 relies on). The check precedes the PossDup redelivery's `fromApp` and the knob-off dispatch, and no callback runs between the top of the function and the check. B41's premise is that at every inbound arm the outbound number is peeked after any inbound callback of that arm; the check's Logout peeks it with none before.
  - **The bound** is 2 × HeartBtInt since the time point, where HeartBtInt is the value `run_liveness_loop` resolves. One helper computes it, by applying the saturating `detail::deadline_after` twice, never as a bare `2 * hb`. The deadline has expired when `steady_now()` is at or past it.
  - **Off** when HeartBtInt ≤ 0 or the session has no clock. The liveness loop is off only at exactly 0, and `deadline_after` requires a non-negative duration, so the deadline states its own test. A bound of zero would end the session at the second frame, which is the defect.
  - **Not configurable.** The ruling names the value.
  - **On expiry** the session sends a Logout whose Text(58) names the unanswered resend and the expected MsgSeqNum, records `Disconnected`, and returns success.
    - `inbound_logout_leaf_` gains a text parameter, passed to `build_logout`; its other callers pass an empty text. Its order is kept: `toAdmin` before `assign_outbound`. The leaf is declared in the installed `session.hpp`.
    - The text is built in a stack buffer, as `honor_peer_next_expected_`'s too-high arm builds its own.
    - If the Logout cannot be built, no frame goes out and `Disconnected` is still recorded. If `toAdmin` throws or the assign fails, the leaf records `Disconnected` and returns the error, as today.
  - **After expiry** (the C-3 disclosure). The state is `Disconnected` and a Logout was written. The transport stays open until the peer closes it or `close()` / `Engine::stop()` runs. Every later frame is drained by the `Disconnected` row. The liveness loop leaves at its next wake. This is fixpp#534 (B27), and every existing Logout-then-`Disconnected` arm has the same shape. Read.
  - **Refresh sites.** NextNumIn advances during a resend at the in-sequence `check_inbound` in `on_inbound_active_` and at the one in `consume_rejected_seqnum_` (a rejected frame that consumes its number, fixpp#423). Both already call `close_filled_resend_gap_()` straight after, and the refresh is one statement there, behind that function's existing `is_awaiting_resend()` test. `apply_inbound_sequence_reset` also moves NextNumIn, and it ends the resend. Re-derive with `git grep -n -E 'check_inbound\(|set_next_inbound\(' -- src/session/session.cpp`. The other hits are on the Logon paths and in the reset unit, where no resend is outstanding unless fixpp#578 holds (§8).
  - **A GapFill that lands short of the recorded end** ends AwaitingResend (`apply_inbound_sequence_reset` exits it unconditionally). That is progress in R-2's sense: NextNumIn moved. The next too-high frame takes the too-high arm again, sends a fresh ResendRequest, and starts a new time point. Read and reasoned; a cell in §5.
- **D-537-6 (R-6). While a resend is outstanding, a too-high Heartbeat answers a pending TestRequest.**
  - **Why.** While a resend is outstanding, every Heartbeat the peer sends is too high, and the out-of-sequence arm drops it before the only site that clears `pending_test_req_id_` can run (the in-sequence Heartbeat branch of `on_inbound_active_`). So a TestRequest fixpp sends in that state cannot be answered, and the liveness loop ends the session silently at its grace check, ahead of D-537-5's Logout. Read. Re-derive the clear site with `git grep -n -E 'pending_test_req_id_\.(clear|assign)' -- src/session/session.cpp`.
  - **Where.** In `on_inbound_active_out_of_sequence_`, after D-537-5's check, so a Heartbeat that arrives past the bound draws the Logout and not a clear.
  - **Predicate.** The frame is a Heartbeat, the error is `session_seqnum_too_high`, a resend is outstanding, and a TestRequest is pending.
  - **Action.** The in-sequence branch's TestReqID disposition. An empty or matching TestReqID(112) clears `pending_test_req_id_` and `unanswered_tr_`. A mismatched 112 follows the in-sequence rule: a silent `Disconnected`. Then the arm returns: no advance, no persist, no `fromAdmin`.
  - **Unchanged.** A too-low Heartbeat keeps today's drop. So does a too-high Heartbeat with no TestRequest pending.
  - **The gate excludes the knob-off case by construction.** AwaitingResend is entered only behind `validate_sequence_numbers`. With the knob off, a too-high Heartbeat still leaves a pending TestRequest set. Noticed, not changed (§8).
  - **`B-093-9` is untouched.** It is about the liveness refresh, which already happens before the sequence check. This rule makes the same frame also answer the TestRequest. Session Layer §4.10, precedence 14, is the text both rest on.
  - **Contact with B27.** The rule reads and writes `pending_test_req_id_` and `unanswered_tr_`, which `run_liveness_loop` owns. No line of the loop changes.
- **D-537-7 (triage C-5). A ResendRequest that cannot be built ends the session.** In `inbound_resend_request_leaf_`, a failed `build_resend_request` records `Disconnected` and returns `dispatch_aborted`, the disposition `emit_gapfill_async` gives a GapFill that cannot be built.
  - Today the leaf returns success with AwaitingResend set, for a request the peer never got. With D-537-1 that session would ignore every later frame, and with HeartBtInt ≤ 0 it would do so without end.
  - Reach: a ResendRequest that overflows the leaf's buffer while the Logon fitted, a few bytes of margin at extreme CompID lengths. Reasoned.
  - Not taken: leaving AwaitingResend and retrying. The build fails for the same CompIDs at every later too-high frame.
  - A failed assign or emit already records `Disconnected` (read from the leaf).

### 4.4 Rejected

- **Queue the frame** (§4.10; the engines). 013 FR-009 specified a queue, and `ResendState::inbound_held` exists with no writer. It needs a capacity policy, an order for held administrative frames, and a delivery path, and it gains nothing on the wire while the request runs to 0. `L-013-2`, which describes the queue as shipped, is corrected (§6). Reconfirmed by the owner after round 1 (R-2).
- **A second ResendRequest per extra frame.** All three engines suppress it by default.
- **Stop refreshing inbound liveness on a too-high frame**, which is what bounds QuickFIX C++. It reverses owner ruling R-1 on fixpp#516 (`B-093-9`), and it still does not protect a slow replay.
- **No deadline** (QuickFIX/J and QuickFIX/Go). Where the peer never fills the gap (`L-092-1`, `L-092-2`), the session would stay Active and ignore every new message until the connection ends. Not taken (R-2).
- **A timer for the deadline.** It adds a sleeper, and it would still need D-537-6 to stop the TestRequest from ending the session first. v0.2 rejected it for another reason ("the peer's Heartbeat answering the TestRequest is itself a too-high frame, so the check runs"). That was incomplete: the check ran before the bound, and the Heartbeat was then dropped. D-537-6 is the fix.
- **Two endings for an alive peer** (a Logout when it keeps sending, a silent end when it is quiet). Not taken (R-6).
- **The grace exit reading "any sound frame since the TestRequest"** (the consult's B′). It gives the same single ending, also for a peer that sends traffic and never heartbeats, and also with the knob off. It edits `run_liveness_loop`, which is B27's surface. Not taken (R-6); noticed in §8.
- **A Logout on the dead-peer ending.** Not taken (R-6). QuickFIX C++ also ends that case with no Logout.

### 4.5 What still differs from QuickFIX after this change (R-5)

Frames (R-2):
- **The frame is ignored, not held.** An application message is delivered once and in order either way. A QuickFIX application receives the queued original; a fixpp application receives the peer's replay, which carries PossDupFlag(43)=Y and OrigSendingTime(122). That is observable to the application, and it is already true of the first too-high frame.
- **A held TestRequest** is answered by the engines once the gap closes. fixpp never answers it, because the peer gap-fills over it. A too-high TestRequest as the *first* frame already behaves this way.
- **A too-high Heartbeat** is processed by the engines once the gap closes. fixpp ignores it, with no `fromAdmin`, except that it answers a pending TestRequest (D-537-6).
- **A too-high ResendRequest or Logout** is acted on by the engines whatever its number. fixpp does not act on it. That is fixpp#577, and it is not changed here.
- **A frame sent during the replay** is ignored and fetched by a second ResendRequest (D-537-1). The engines hold it.
- **After a GapFill that lands short of the recorded end**, fixpp leaves its resend state and can send a new ResendRequest where QuickFIX C++ would not: that engine keeps its range until the range's end is reached. Behaviour that exists today.

An unfilled gap (R-6; the endings are in §4.6):
- **The peer is alive and does not fill the gap.** fixpp sends a Logout after 2 × HeartBtInt without progress. QuickFIX C++ disconnects at its heartbeat timeout (2.4 × HeartBtInt), with no Logout, because a too-high frame never refreshes its last-received time. QuickFIX/J and QuickFIX/Go wait. The engines differ among themselves, so there is no single behaviour to keep.
- **The peer answers nothing.** fixpp ends as for any dead peer: an unanswered TestRequest, then a silent `Disconnected`. QuickFIX C++ does the same. This is not a difference.
- **HeartBtInt = 0.** The gap waits, as in QuickFIX/J and QuickFIX/Go.

A store read failure in a resend answer (R-7):
- fixpp aborts the answer and ends the session, with no Logout (D-538-4). This follows QuickFIX C++, whose store error leaves `generateRetransmits` and whose handler logs it and disconnects, and QuickFIX/Go, whose `resendMessages` returns the store error and whose session then leaves its connected state.
- QuickFIX/J differs: it rethrows the error unless `ForceResendWhenCorruptedStore` is set, logs it, and sends nothing; the session stays logged on with the peer's gap open. fixpp follows the two engines that end the session, because a peer whose gap cannot be filled gains nothing from a session that stays up.

### 4.6 How a session with an unfilled gap ends (R-6)

One table for every ending. §4.4, §4.5, §5 and §6 are written from it. E is the time the gap opened; HB is HeartBtInt.

| Condition | Trigger | What fixpp does | What the peer sees |
|---|---|---|---|
| (a) The peer is alive and does not fill the gap | a too-high frame reaches the check of D-537-5 at or after 2 × HB since NextNumIn last advanced | a Logout whose Text(58) names the unanswered resend; `Disconnected` | the Logout. The transport stays open until the peer closes it or `close()` / `Engine::stop()` runs (fixpp#534) |
| (b) The peer answers nothing | fixpp's TestRequest is still pending at the liveness loop's grace check | silent `Disconnected`, as today, with or without a gap | no Logout; the transport stays open (fixpp#534) |
| The peer sends frames but never a Heartbeat for fixpp's TestRequest | the same grace check: it reads only the pending id | as (b), with or without a gap | as (b) |
| A too-high Heartbeat carries a TestReqID(112) other than the pending one | D-537-6's mismatch rule | silent `Disconnected`, the in-sequence rule | no Logout; the transport stays open |
| Every too-high frame is stopped before the sequence gate by an arm that Rejects it and leaves the session up (D-537-5, "Which frames reach it") | none: the check never runs, and each frame refreshes liveness | nothing; the gap waits | Rejects |
| HeartBtInt ≤ 0, or no clock | none | nothing; the gap waits | nothing |

- **Condition (a)'s ending is the only one this note adds.** The dead-peer endings are today's, and D-537-6 keeps an alive peer that answers TestRequests out of them.
- **A healthy slow replay is not ended.** Replayed frames are in sequence, NextNumIn advances, and the time point is refreshed. The deadline is tripped only when a too-high frame arrives after 2 × HB with no advance: the peer interleaves new frames or Heartbeats with a replay it has not started, or has stalled.
- **The timelines are reasoned, from the consult.**
  - A peer that only heartbeats, or that keeps sending new messages: each too-high frame refreshes liveness and runs the check; a TestRequest sent in a lull is answered by the peer's too-high Heartbeat; the first frame at or after E + 2 HB draws the Logout. Expected between E + 2 HB and about E + 3 HB.
  - A peer that sends nothing: TestRequest at E + HB, then the silent `Disconnected` at the loop's `grace_deadline`. The check never runs, because no frame arrives.
  - Cells Q, N and R and Q's mutant (§5) are what turn these timelines into observation. Until they run, this section is a prediction.
- **Not an ending of a gap, but an ending this note adds:** the R-7 abort (D-538-4). It closes the transport; the deadline's Logout does not.

## 5. Tests and gates

- Each new or changed cell is shown RED before the fix, unless it is marked *guard*. A guard cell is GREEN at the base; it exists for its mutant.
- Each decision gets an arm that goes RED when the fix is deleted. A mutant that stays GREEN is reported as a finding against the cell, not assumed away.
- A decision that only corrects a comment (D-538-3, D-536-5, D-537-4) has no cell. `check-comment-claims.py` reads the added comments.
- A cell that needs a store double uses a delegating wrapper over a shipped store. A hand-written store would measure its own `retrieve`.

**#538**

| Decision | Cell | Fix-deleted arm |
|---|---|---|
| D-538-1 | The lifted cells of `session_538_*`: the claim cells turn GREEN and the controls stay GREEN | the absolute position restored in one store: that store's claim cell is RED |
| D-538-1 | RED first: `FileStore` reopened after a (·, 2) reset and two stores, then `retrieve` | as above |
| D-538-1 | After a (·, 2) reset and two stores: `retrieve(1, 1)` and `retrieve(1, 0)` report a gap and visit nothing; `retrieve(2, 0)` visits 2 and 3. The last is the RED half. A gap stops a range walk today, and that stays; the resend walk asks one slot at a time | as above |
| D-538-1, empty direction | Guard: after `reset_to(2, 2)`, `retrieve(1, 1)`, `(1, 0)` and `(2, 0)` on both directions, on `MemoryStore` (both policies) and `FileStore`. The first two report a gap; the third is the empty-range success | the emptiness test deleted. It must be shown RED in the debug tree or under ASan |
| D-538-1, empty direction | Guard, session level: a ResendRequest 1..0 straight after the 141=Y exchange, nothing sent yet. One GapFill, no crash | the same mutant |
| D-538-1, capacity | A bounded `MemoryStore` filled to capacity after a (·, 2) reset: the retained numbers are retrievable, the refused ones are gaps | the absolute position restored |
| D-538-2 | RED first: a wrapper that forwards `store`, `retrieve`, `next_seqnum` and `reset` to a shipped store and does not override `reset_to`. Then `reset_to(1, 2)`, `store(2)`, `retrieve(2, 2)` | the absolute position restored in the wrapped store |
| D-538-4 | RED first: a wrapper whose `retrieve` returns `store_io_failure` for one slot, with stored application messages on both sides of it. The frames before the slot are replayed; no GapFill covers the slot; `on_inbound_frame` returns `dispatch_aborted`; the state is `Disconnected`; no Logout; no `session_event_resend_slot_gap_filled`. Today the slot is gap-filled and the session stays Active | the new error test deleted |
| D-538-4 | Under an Engine, the same failure closes the transport | the same mutant |
| D-538-4 | RED first, the tail read: `next_seqnum(outbound, false)` fails with `store_io_failure`. Nothing is written; `Disconnected`; the error is returned. Today one GapFill covers the range | the tail test deleted |
| D-538-4 | The 789 path: the same failing store under `honor_peer_next_expected_`'s resend | the first mutant |
| D-538-4, controls | A plain gap folds silently. `store_cancelled` at either read keeps today's path. An unbuildable replay still folds with its event (fixpp#424 unchanged) | each control is RED if the abort is widened to its code |
| D-538-4, slot 0 | A ResendRequest with BeginSeqNo(7)=0 over a non-empty store. The assertion follows the owner's answer (§7) | — |

- `run_q28` in `test_memory_store_round_trip.cpp` discards a `retrieve` result on exactly this state. It is noted, not changed.

**#536**

| Decision | Cell | Fix-deleted arm |
|---|---|---|
| D-536-1 | The lifted cells of `session_536_*`: both shared-clock cells turn GREEN. The failure string is reworded to what the cell observes | either sweep restored |
| D-536-1 | A counting clock shows that open, traffic, graceful close and terminal close issue no `cancel_sleeps()` call. This is the arm that catches a third sweep site | either sweep restored |
| D-536-2, the reset | `close` completes promptly for a session whose liveness loop has sent a Heartbeat, and for one that has sent a TestRequest, on a mock clock. `AdminDistinctNowTest.TR_DistinctNow_*` and `CrossSessionTestReqID.*` are the existing witnesses (measured RED without the reset, §3.4, run P1+P2) | the filter reset deleted |
| D-536-2, the state test | `close` runs while the loop's TestRequest emit is suspended in the store (a hooked store holds it), and the emit is then released. `close` must complete without the clock advancing. **Fixture: the callback transport (`transport_send_`), so the released emit succeeds.** On a live transport `close` has closed it, the emit fails, and the loop leaves before the grace sleep, so the cell could not fail. The probe did not cover this window | the state test before the grace sleep deleted, shown RED on that fixture |
| D-536-3 | RED first: a clock whose `cancel_sleeps()` does nothing. A graceful close returns when the peer's Logout arrives, not at the logout timeout | the wake's trigger in the LogoutSent arm deleted |
| D-536-3 | Changed: `AdversarialSessionTest.LogoutGracefulCancelMidSleep` pins the old mechanism (a sweep ends the logout wait). It becomes the cell for the new one: a sweep by a third party during the logout wait does not end it; the confirmation, the deadline or the grace timer does | the deadline arm put back on a raw `sleep_until` |
| D-536-3, lifetime | The grace timer wins while the inner wait is pending. A terminal close escalates during the wait. In both, `close` completes and no wake is called after the wait ended | the clear of the wake on exit deleted |
| D-536-3, lifetime | The peer's Logout arrives while fixpp's Logout emit is still suspended in the store. The Logout then meets the Active arm, not the LogoutSent arm; the outcome at the base is read first. The cell asserts that `close` completes no later than the logout timeout and that no wake is called after the wait ended. Not traced | — |
| D-536-4 | Control kept: an explicit `cancel_sleeps()` still ends an unclosed session's liveness loop | — |
| D-536-4, disclosure | An explicit `cancel_sleeps()` on `Engine::clock()` with open sessions: each stays Active and sends no further Heartbeat. This pins the disclosed limitation | — |

**#537**

Before the cells are written, the one-statement edit of D-537-2 is run over §3.4's selection, as the #536 probe was. The tests that pin the old outcome are found by running, not by reading: `Disclosed_L1_*` and `Disclosed_L2_*` are named from reading only.

Every deadline cell states the traffic that keeps the liveness loop from ending the session first: an inbound frame inside every HeartBtInt, or a Heartbeat for each TestRequest. A single clock advance past the bound with no traffic is cell N, not a deadline cell.

| Decision | Cell | Fix-deleted arm |
|---|---|---|
| D-537-1, D-537-2 | The lifted cells of `session_537_*`: `SecondTooHigh` turns GREEN on both roles | D-537-2's statement deleted |
| D-537-1, D-537-2 | Per message type, both roles: a second too-high TestRequest, application message, Reject, GapFill without PossDup, and Logon. Each leaves the session Active, sends nothing, and leaves NextNumIn unchanged; the replay through the highest number then completes | the same mutant |
| D-537-1, D-537-2 | The same in LogonReceived: the arm serves both states | the same mutant |
| D-537-1, D-537-2 | A second too-high ResendRequest and Logout: NextNumIn does not move, and the session does not enter `Disconnected` without sending a Logout. What fixpp should send is fixpp#577's question; these two assertions survive either answer | the same mutant |
| D-537-1 | A frame sent during the replay is ignored; the next frame draws a second ResendRequest, and every message is delivered once and in order | — |
| D-537-2, controls | A too-low frame without PossDup is still fatal, and the overflow arm is unchanged | "any error returns success" in place of the exact match: the too-low control is RED |
| D-537-5, no progress | Mock clock, both roles: a too-high frame before the bound is ignored; one at or after it draws the Logout. Asserted: the Logout and its Text(58), `Disconnected`, **and that the transport is still open**, with a pointer to fixpp#534 so B27 flips that assertion | the check deleted: the session is still Active after the frame |
| D-537-5, progress | A replay that takes longer than the bound in total, with NextNumIn advancing inside each window, completes | the refresh deleted |
| D-537-5, second refresh site | The same, where the only advances inside a window are frames that `consume_rejected_seqnum_` consumes | the refresh moved out of `close_filled_resend_gap_` to the in-sequence caller only |
| D-537-5, frame types | After the bound, a too-high Heartbeat ends the session with the Logout. So does a too-high PossDup frame that passes Stage 1 | the check moved below the Heartbeat arm, or below the PossDup arm |
| D-537-5, off | HeartBtInt = 0, and a Session with no clock: no deadline. A negative HeartBtInt is tested on the helper that computes the bound: no deadline, and no assertion in the debug tree. What `run_liveness_loop` does with a negative interval was not traced, so a session cell may not stay Active long enough to observe it | "off at exactly 0" in place of "≤ 0": the negative cell is RED |
| D-537-5, zero bound | — | a zero bound: `SecondTooHigh` is RED again |
| D-537-5, short GapFill | A GapFill short of the recorded end, then a too-high frame: a new ResendRequest, and a deadline that starts again | — |
| D-537-6, **Q** | Open a gap with a too-high application frame. Advance to E + HB and expect a TestRequest. Deliver a too-high Heartbeat carrying its 112. Advance to just past E + 2 HB, and not to E + 3 HB: the session is still Active (RED today and under v0.2). Deliver a too-high Heartbeat: the Logout, then `Disconnected`. The window matters: at that wake the loop sends a second TestRequest, whose grace ends near E + 3 HB (reasoned from the loop) | D-537-6's predicate deleted: Q is RED at its grace step |
| D-537-6, **N** | The same gap, nothing delivered, the clock advanced past the grace deadline: `Disconnected`, no Logout | guard for condition (b): RED if the dead-peer ending gains a Logout or is lost |
| D-537-6, **R** | Gap, TestRequest at E + HB, a too-high Heartbeat answer, then in-sequence PossDup frames spaced 1.5 HB apart, with every later TestRequest answered by a too-high Heartbeat: no `Disconnected` | the predicate deleted |
| D-537-6, mismatch | A too-high Heartbeat with a 112 other than the pending one: silent `Disconnected` | — |
| D-537-6, bounds of the rule | A too-low Heartbeat does not clear a pending TestRequest. With `validate_sequence_numbers = false`, a too-high Heartbeat does not clear it either | the gate on an outstanding resend deleted: the knob-off control is RED |
| D-537-7 | A cell only if the fixture's CompID lengths can make `build_resend_request` fail while the Logon fits. Otherwise the statement is reported uncovered under `[const §IX.1]` | — |

- `Disclosed_L1_*` and `Disclosed_L2_*` pin the fatal outcome today. Their traces change: the frames are ignored, and the session ends by one of §4.6's rows, according to the traffic each cell sends.
- No cell reconnects with the awaiting flag set (fixpp#578, §8).

**C ABI.** One witness per row of §6.2's table, on the raw-peer fixture named there. Each is shown RED on the base tree first. A witness that cannot be made RED there means the effect has no C-ABI reach, and the table row is corrected, not the test.

**Gates**
- A full `ctest` in `linux-clang-debug`, then ASan, TSan and UBSan trees. TSan matters most for #536.
- An MSVC leg: `session.cpp` changes, and so do the public headers `session.hpp`, `reconnect_fsm.hpp`, `resend_state.hpp`, `message_store.hpp`, `memory_store.hpp`, `clock.hpp`, `fix/c_api/session.h` and `fix/c_api/version.h`. It needs the shared sandbox and its lock.
- Every Tier 1 source-audit job is run locally before the labelled push.
- Coverage is assessed per changed line (`[const §IX.1]`).

## 6. Disclosures and documents

### 6.1 B&L

`spec/behaviors-and-limitations.md`; rows are written at implementation.

- New behaviour rows: the relative lookup (#538); the abort on a store read failure (R-7); a session no longer sweeps the clock, and the logout wait has its own wake (#536); the ignored too-high frame (#537); the deadline; the too-high Heartbeat that answers a TestRequest (R-6); the ResendRequest that cannot be built (D-537-7).
- **The deadline row states:** the two conditions of §4.6 and the ending of each; that after the Logout the transport stays open until the peer closes it or `close()` / `Engine::stop()` runs (fixpp#534); that the deadline is tripped only when a too-high frame reaches the sequence gate after 2 × HeartBtInt with no advance; and that it is off when HeartBtInt ≤ 0.
- **New limitation row (D-536-4):** an explicit `Clock::cancel_sleeps()` on a clock that open sessions share ends their liveness loops, and nothing restarts them.
- **New row for store implementers (D-538-2):** what `retrieve` must return after a reset to a first number other than 1, and for an empty direction.
- **`L-092-1`, `L-092-2`, `L-093-7`.** The last step changes from "the session ends silently at the peer's next new message" to text that is true for both conditions:
  - the peer's new messages are ignored;
  - if the peer keeps sending and answers fixpp's TestRequests, the session ends with a Logout naming the unanswered resend, at the first too-high frame that reaches the sequence gate once NextNumIn has not advanced for 2 × HeartBtInt;
  - if the peer answers nothing, or leaves a TestRequest unanswered, the session ends as it does for any dead peer: a silent `Disconnected` at the TestRequest's grace check;
  - with HeartBtInt ≤ 0 it does not end.
  - `L-092-1` also says a too-high Heartbeat is dropped silently while the gap is open. It gains: unless it answers a pending TestRequest (D-537-6).
- `L-013-2`: corrected. No hold queue is implemented; the replay stands in for it.
- `B-424-2`: not edited (R-7).
- **The C-ABI 1.12 row**, marked BREAKING, with the effects of §6.2 and, as `L-092-12` does for 1.10, the effects that have no C-ABI witness.

### 6.2 C ABI 1.12 (R-8)

- **1.12 is declared BREAKING** under `[const §X.7]`: `FIXPP_C_ABI_VERSION_MINOR` goes to 12, with a `version.h` entry.
- **Each affected declaration in `include/fix/c_api/session.h` carries a 1.12 clause**, as it carries its 1.10 and 1.11 clauses. The observers those entries name are `fixpp_session_is_established`, `fixpp_session_close`, `fixpp_session_send`, `fixpp_session_register_callback` and `fixpp_session_register_send_callback`.
- **The 1.10 sentences this batch falsifies are amended in place, marked 1.12**, as 1.11 amended 1.10's. They are the 1.10 bullets that end "the session ends at the peer's next new message that is not a Heartbeat": the resent faulty frame, and the faulty SequenceReset in a resend recovery. Those bullets describe the #537 trace. Re-derive the declarations that carry them with `git grep -n "next new message" -- include/fix/c_api/session.h`; `fixpp_session_close`'s 1.10 clause refers to `fixpp_session_is_established`'s instead of repeating them.
- **B41 declares no C-ABI change**, so 1.12 is this batch's unless B41's open decision OD-1 flips. If it does, the second PR to merge renumbers.
- No symbol, signature or error code is added.

Effect → declarations → witness. The fixture is `tests/capi/inbound_frame_dispositions_capi_support.hpp`: a C-ABI engine on a real clock, facing a raw TCP peer that writes hand-built frames. The classifications are reasoned; `.specify/api-contract.md` §11 is the bullet list `[const §X.7]` points at.

| Effect | 1.12 clause on | Class, reason, §11 bullet | C-ABI witness |
|---|---|---|---|
| A second too-high frame while a resend is outstanding is ignored; the session stays established where it ended (D-537-1, D-537-2) | the observers named above, with the amended 1.10 bullets | **Breaking.** A documented result changes: the 1.10 bullets say the session ends there. §11: "changing the meaning of a … C-ABI symbol" | the peer opens a gap and sends a second too-high application message: `fixpp_session_is_established` stays true and the peer reads no second ResendRequest; after the replay the receive callback has each message once, in order |
| An unfilled gap ends with a Logout once NextNumIn has not advanced for 2 × HeartBtInt (D-537-5) | the same clause | **Breaking**, same bullet: it is the new end of the trace those 1.10 bullets describe | heartbeat of 1 s; the peer keeps sending too-high frames and answers each TestRequest: it reads a Logout with Text(58); `fixpp_session_is_established` turns false; the connection is still open (pointer to fixpp#534). The cell waits at least two intervals of real time |
| While a resend is outstanding, a too-high Heartbeat answers fixpp's TestRequest; the session stays up where it ended at the grace check (D-537-6) | the same clause | **Breaking** on the reading 1.10 and 1.11 applied to these observers: when an established session ends changes. Same bullet. No header sentence documented the old result; `L-092-1` documents the dropped Heartbeat | the same gap; the peer lets one interval pass, answers the TestRequest with a too-high Heartbeat, and the session is still established after the grace window |
| One session's close, or a peer's Logout on one session, no longer stops the Heartbeats and TestRequests of other sessions of the engine; another session's close no longer ends this session's logout wait early (D-536-1 to D-536-3) | `fixpp_session_close`, `fixpp_session_is_established` | **A correction.** No document specified the old result, so taken alone it is additive under `[const §X.7]`'s last sentence, and no §11 bullet applies. It is declared in the 1.12 clause because R-8 declares the entry as a whole. `fixpp_session_close`'s return code does not change | two C sessions on one engine, each facing a raw peer; one is closed; the other's peer still reads Heartbeats past one interval |
| After a 141=Y exchange that leaves the first outbound number at 2, a message sent is replayed on a ResendRequest where it was gap-filled (D-538-1) | `fixpp_session_send` | **A correction**, as the row above. A resend is not surfaced to the send callback (`L-019-4`), so the effect is on the wire only | a C initiator with `fixpp_session_config_set_reset_on_logon`; the peer acknowledges with 141=Y; one `fixpp_session_send`; the peer sends a ResendRequest and reads the message with PossDupFlag(43)=Y, not a GapFill |
| A store read failure in a resend answer ends the session where the slot was gap-filled (D-538-4) | **depends on §7 item 1.** Under this note's reading: none; the effect is recorded in the `version.h` entry as one with no carrying declaration, as the 1.10 entry records its own. Under R-7 read literally: the observers named above | **Under this note's reading, no reach through the C ABI as shipped.** A C session runs on `MemoryStoreFactory` (`src/capi/config.cpp`). `MemoryStore::retrieve`'s aborting codes are a reset between the snapshot and a visit, and a failing visitor. The walk asks one slot at a time, and no suspension separates the snapshot from that slot's generation check; `CaptureVisitor` neither throws nor aborts. Read. **Under R-7 read literally, it has reach:** a peer's ResendRequest with BeginSeqNo(7)=0 ends a C session, and the row is then breaking by the first row's bullet | under this note's reading none is possible; the C++ cells stand, and the B&L row says so. Under the literal reading: the peer sends that ResendRequest and `fixpp_session_is_established` turns false |

- **A ResendRequest that cannot be built** (D-537-7) is declared with the first row's clause. It has no C-ABI witness unless the C fixture's CompIDs can reach it; the B&L row says which.
- **`[const §X.7]`'s other duties.** BREAKING is marked on each declaration, in the PR description and in the B&L delta. Every in-repository consumer is updated in the same PR; re-derive the sites that encode the minor with `git grep -n -l -i -E 'abi.?minor|VERSION_MINOR' -- include src tests bindings tools` and classify each.
- **The Appendix A controls** (`[const §X.6]`, `[const §X.7]`, `[const §XI.7]`; Appendix A rows: ABI surface, threading / concurrency, session FSM), met as `.specify/495-493-486-dict-reify-copy.md` §0.2 met them in issue mode:
  - Codex Gate A: this loop;
  - `/clarify`: by hand. §0's rulings are its record;
  - `/analyze`: run over this note by the spec-analyzer after Gate A converges and before implementation. Not yet run;
  - the owner's `/plan` sign-off: the owner's approval of the converged note, which serves as the plan. Not yet given.

### 6.3 Hot path (`[const §VIII.5]`)

No violation was found. Read and reasoned:

- The deadline check, the Heartbeat rule and the Logout's text are in the out-of-sequence arm, which is a slow arm of `on_inbound_active_`. The text is built in a stack buffer.
- The refresh is on the in-sequence path, inside `close_filled_resend_gap_`, behind that function's existing `is_awaiting_resend()` test. It adds one `steady_now()` read, and only while a resend is outstanding. **Condition: the refresh stays inside that branch.**
- The logout wake is a `std::function` that captures one reference, as `close_grace_wake_` does, on the close path.
- The recovery window's global-heap half is disclosed, not gated (`tests/perf/test_session_recovery_alloc_guard.cpp`, `L-497-1`).

### 6.4 Design documents

Appended at the end of each file, never inserted; frozen `specs/` are not edited.

- `.specify/2d-threading.md`: an erratum on `cancel_sleeps` (who calls it; what it does to a waiter; what it does to open sessions).
- `.specify/2e-msgstore.md`: an erratum on `retrieve` after a reset, and for an empty direction.
- `brain/components/session.md` and `message-store-quiescence.md`: pointers to this note, and the rejected alternatives.
- Header comments at each superseded decision name this note's issues, as `async_mutex.hpp` names its erratum.

## 7. Still open after round 1

v0.2 listed its open risks here. Each is now a sentence in a decision plus a cell (see the disposition table under `## Gate A`). What remains:

1. **Owner item: slot 0 under R-7.** A ResendRequest with BeginSeqNo(7) = 0 or absent, over a non-empty store, makes the walk ask `retrieve(0, 0)`, which both stores refuse with `store_seqnum_invalid`.
   - R-7 read literally: the answer aborts and the session ends.
   - This note's reading: an argument refusal is not a read failure; the slot keeps today's fold (D-538-4).
   - The cell in §5 is written to the owner's answer.
2. **Reasoned claims a second round should attack first:**
   - §4.6's timelines, until cells Q, N, R and Q's mutant run;
   - D-536-3's lifetime argument, which is read and reasoned, with cells named and none run;
   - §6.2's classifications, and "no reach through the C ABI" for R-7;
   - the interaction with a stale awaiting flag (fixpp#578, §8), which no cell exercises.
3. **Not re-verified in round 1:** the FIX quotations of §4.1 against the local copies, and the probe transcripts beyond their verdict tables.

## 8. What this does not decide

- **fixpp#577:** a too-high ResendRequest or Logout is discarded rather than acted on. Unconfirmed. §5's two cells for those frames assert only what survives either answer.
- **fixpp#578:** the awaiting-resend flag across an in-place reconnect. Unconfirmed. If it holds, it interacts with D-537-5: the flag is cleared in `close_filled_resend_gap_` and `apply_inbound_sequence_reset` and set in `on_inbound_active_too_high_` (`git grep -n -E 'enter_awaiting_resend|exit_awaiting_resend' -- src`), and nothing clears it at a reconnect or in the reset unit. The Logon paths and the reset unit would then move NextNumIn with the flag set, and the first too-high frame after a reconnect could meet a time point from the previous connection and draw the "unanswered resend" Logout at once, for a request never sent on that connection. The deadline's own exit leaves the flag set too, as every other end of a session does today. Reasoned; not fixed here.
- **fixpp#534** (B27): `Disconnected` does not close the transport. The deadline's Logout inherits it (D-537-5), and the deadline cell asserts the open transport so that B27 flips it.
- **fixpp#535** (`test_request_threshold`, B27) lives in the liveness loop. This note does not touch it. While #536 is open, a #534 cell must use one session per clock.
- **B27's surface** is `run_liveness_loop` and the `Disconnected` writers. This note adds a state test and a filter reset before each of the loop's sleeps (D-536-2) and changes no other line of it. D-537-6 shares `pending_test_req_id_` and `unanswered_tr_` with the loop, and no line.
- **The grace exit reads only the pending id**, not "any sound frame since the TestRequest" (the consult's B′). That departs from Session Layer §4.10 precedence 14 for every peer that sends traffic and no Heartbeat, with or without a gap. It is in `run_liveness_loop`. Noticed, not changed (R-6).
- **With `validate_sequence_numbers = false`**, a too-high Heartbeat does not answer a pending TestRequest. Noticed, not changed (D-537-6's gate).
- **B41** (fixpp#563, #564; its note is on its own branch):
  - B41 edits `Session::send`, `Engine::send`, the callback dispatch scope, and the entry of `Session::close`. It does not touch `run_liveness_loop`, `run_logout_phase1`, `close`'s phase 2, `on_inbound_logout_sent_`, `store_then_emit`, `replay_outbound_range_` or the stores.
  - The only shared function is `Session::close`. The second PR to merge rebases it and re-runs its cells.
  - B41 tracks callback depth inside the one dispatch routine every `toAdmin` and `toApp` goes through. This note's new emit, the deadline's Logout, goes through the existing inbound Logout leaf, so it is covered with no action here.
  - B41 relies on one premise that D-537-5 states as a condition: the deadline check stays ahead of every callback of its arm.
  - B41's ruling on `close` makes one interleaving reachable from an application, and D-536-2's state test before the grace sleep covers it (§3.2, §5).
- **The lock sequence's terminal reset as a general hazard** (§3.5). This note fixes the one member that this change exposes. Whether the lock should restore the caller's filter instead is a `2f` design question and is not decided here.
- Noticed and left alone:
  - `tls_transport.hpp` says its handshake timeout uses `Clock::sleep_until`, and the code uses a private timer;
  - `reconnect_fsm.hpp` declares three timers nothing reads (fixpp#559's class);
  - `FileStoreImpl::write_frame` has no caller;
  - a ResendRequest with BeginSeqNo(7)=0 starts the resend walk at slot 0, and the GapFill run then starts there. What that frame carries was not read;
  - `run_liveness_loop` is off only at a HeartBtInt of exactly 0. What it does with a negative interval was not traced.

## Gate A

- Round 1 applied 2026-10-11: Codex P1=4 P2=8 P3=0; Opus post-judging P1=0 P2=8 P3=14; rewrite addresses root causes RC-1, RC-2, RC-3, RC-4. Reviews: research/reviews/codex_536_537_538_1_recovery-liveness_review.md, research/reviews/opus_536_537_538_1_recovery-liveness_triage.md; consult research/reviews/fable_536_537_538_stuck-gap-ending_consult.md.

Paths are relative to the parent repository's `research/G19-fix-fpml-iso20022/`.

### Round 1 — root causes

- **RC-1.** D-537-5 was analysed against the inbound path only; the liveness loop is a second writer of the same outcome. Fix: §4.6, one table of every ending, with §4.4, §4.5, §5 and §6 written from it; D-537-6 (R-6).
- **RC-2.** D-538-4 extended fixpp#424's ruling to a different failure and did not shape the event for it. Fix: R-7. The answer aborts, and no event is widened.
- **RC-3.** Open questions were left in §7 instead of being decided. Fix: each is now a sentence in a decision plus a cell, or an owner item; §7 keeps only what is still open.
- **RC-4.** Populations were listed from memory of the recipe's output, not from the output. Fix: each recipe was re-run and every hit classified (§2.1, §2.3, §3.5, D-537-5's refresh sites, §4.2).

### Round 1 — disposition of every finding

Severity is the triage's, after judging.

| Finding | Severity | Disposition | Answered in |
|---|---|---|---|
| C-1 `front()` on an empty direction | P2 | applied | D-538-1; §2.3; §5 |
| C-2 a short GapFill ends the awaiting state | P3 | applied (the residue); counter-proposal disagreed | D-537-5; §4.5; §5 |
| C-3 expiry leaves the transport open | P2 | applied as a disclosure (owner); counter-proposal disagreed | D-537-5; §4.6; §5; §6.1 |
| C-4 the C-ABI disposition | P2 | applied per ruling R-8 | §6.2; §5 |
| C-5 AwaitingResend with no ResendRequest sent | P3 | applied (the smaller fix); the P2 form disagreed | D-537-7 |
| C-6 where the time point lives | P3 | applied | D-537-5, "The clock" |
| C-7 negative and extreme HeartBtInt | P3 | applied | D-537-5, "The bound" and "Off"; §5 |
| C-8 the rejected per-session clock cancellation | P3 | applied (the cap argument); second half disagreed | §3.3 |
| C-9 the `enable_total_cancellation` population | P3 | applied | §3.5 |
| C-10 the logout wake's lifetime | P3 | applied; generation identity disagreed | D-536-3; §5 |
| C-11 third-party `MessageStore`s | P3 | applied; "implementation break" disagreed | D-538-2; §5; §6.1 |
| C-12 the event's identity and volume | P2 | applied per ruling R-7: no event is emitted | D-538-4 |
| O-1 the Logout is pre-empted by the liveness loop | P2 | applied per ruling R-6 | D-537-6; §4.6; §5 |
| O-2 a store read failure is gap-filled | P2 | applied per ruling R-7 | D-538-4; §4.5; §5 |
| O-3 D-536-4 leaves the failure reachable | P2 | applied | D-536-4; D-536-5; §6.1 |
| O-4 cells that cannot fail | P2 | applied | §5 (the state-test fixture; the #577 cells; the wrapper) |
| O-5 the Logout leaf cannot carry Text(58) | P3 | applied | D-537-5, "On expiry" |
| O-6 "no resend is outstanding" on the Logon paths | P3 | applied | D-537-5, "Refresh sites"; §8 |
| O-7 missing cells | P3 | applied | §5 |
| O-8 §4.5 is incomplete | P3 | applied | §4.5; D-537-1; §4.6 |
| O-9 the `[const §VIII.5]` disposition | P3 | applied | §6.3 |
| O-10 wording | P3 | applied | §2.1; §4.2; D-537-2 |

### Round 1 — disagreements

Codex's fix is not applied for these.

- **C-2's counter-proposal** (keep AwaitingResend until the recorded end is crossed after a SequenceReset). It changes `apply_inbound_sequence_reset`'s existing exit, which neither #537 nor a ruling covers. Its premise is also wrong: a partial GapFill advances NextNumIn, which is the progress R-2 measures.
- **C-3's counter-proposal** (close the transport at expiry and assert EOF). That is fixpp#534, batch B27. Every other Logout-then-`Disconnected` arm would then differ from this one. The note discloses instead, and the owner agreed.
- **C-8's second half** ("sessions sleep on their session executor and the clock keys the slot by `Session*`"). False for sessions under an Engine: their loops are not spawned on a `session_executor`. §3.3's sentence stands.
- **C-10's generation identity.** A Session runs the logout wait once; there is no older waiter to guard against.
- **C-11's "third-party implementation break".** A store that fails D-538-2 already shows #538 today, and the constitution makes no C++ API promise. A B&L row is enough.
- **C-12's "stop probing after the first I/O error, then gap-fill the rest".** R-7 subsumes the first half: the walk stops. It rejects the second: nothing is gap-filled that the store failed to read.
- **C-5's "commit the resend state only after successful dispatch", as a P2.** The premise is right; the reach is a few bytes of buffer margin. Kept at P3, with D-537-7.
