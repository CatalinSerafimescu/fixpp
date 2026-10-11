# fixpp#536 / #537 / #538 — session recovery and liveness (B28)

> **Status: v0.6 — rewritten after the targeted Gate A round (2026-10-11); the next Gate A review round is pending.** The targeted round (review index 4) on v0.5 gave Codex P1=0 P2=0 P3=0 and, after the Opus judge, P1=0 P2=1 P3=5, so it did not converge. The one P2 was that a cancelled store read in the resend walk kept the GapFill fold. The owner asked for a measurement before ruling, and then ruled (R-13): a cancelled read aborts the answer, as any other read failure does (D-538-4). This rewrite applies that ruling and the round's five P3 corrections, and it is the first rewrite of a new loop instance. The dispositions are under `## Gate A`.
>
> **v0.5 — hand-edited after Gate A round 3 (2026-10-11).** Gate A round 3 did not converge: Codex P1=2 P2=1 P3=0; after the Opus judge P1=2 P2=0 P3=7. The loop was exhausted, with both rewrites used. The owner chose a hand-edit of this note followed by one targeted Gate A round. The hand-edit applies the round-3 judge's written remedies and changes no design: a class 3 read failure returns the store's own error (R-7, D-538-4); §6.2 gives each effect its own C-ABI witness, and its #536 row is now three rows (R-8); the deadline that ends with no Logout is kept and disclosed (R-12); the C-ABI minor is 1.12. The dispositions are under `## Gate A`.
>
> **v0.4 — Gate A round 2 applied (2026-10-11).** Round 2 changed: the deadline check is one helper, called at every exit a frame above NextNumIn can take while a resend is outstanding (§4.7), where v0.3 had it in one arm; a too-high Heartbeat answers a pending TestRequest also when it is the frame that opens the gap (R-9, D-537-8); a resend answer names three classes of store result, and slot 0 keeps today's fold (R-10); one pattern with no ending is disclosed, not fixed (R-11, D-537-9).
> §0 now quotes the owner, with this note's reading under each quotation. §4.6 is derived from §4.7's exit table. Every cell of §5 has an assertion, and no cell is conditional. The dispositions are under `## Gate A`.
>
> - **Base:** `origin/main` = `22702dc9`. Code is named by symbol, with a recipe where a population matters; re-derive before relying on one.
> - **Branch:** `536-537-538-recovery-liveness`, in the `fixpp-parallel` worktree.
> - **Mode:** issue mode (no `specs/` bundle). Records go to the parent's `decisions/speckit/`.
> - **Gate A triggers touched** (`[const §XVII.1]`): the session FSM and recovery (#537), the message store contract (#538), cancellation (#536), the C ABI (the 1.12 clause of §0, BREAKING, R-8), and comment text in public headers. Installed C++ headers also change in `session.hpp` and in `reconnect_fsm.hpp`:
>   - `session.hpp`: one new private data member of `Session` (D-536-3; the only layout change); new private member functions, the deadline helper (D-537-5) and the TestReqID routine that D-537-6 and D-537-8 share; a text parameter on a private leaf (D-537-5); the frame header as a parameter of the private sub-arm `on_inbound_active_too_high_` (D-537-8);
>   - `reconnect_fsm.hpp`: one new non-virtual member of `ReconnectFsm` (D-537-5).
> - **How to read a claim:** *measured* means a run is named; *read* means read from source at the base; *reasoned* means neither. §3.4 is measured, and so is the cancelled read of D-538-4 ("Why `store_cancelled` aborts"). §4.7's table is read. The timelines of §4.6 are reasoned until the cells named there run.

## 0. Owner rulings (2026-10-11). Gate A reviews whether this note implements them.

Text in quotation marks is the owner's own words. What follows a quotation under **Reading** is this note's reading of it, and can be wrong where the quotation cannot.

- **R-1.** Builds for this batch are approved, and may run at the same time as B41's.
- **R-2 (#537).** First: "Check official FIX documentation and QuickFIX C++/J/Go then come back if not settled." §4.1 is that check. It did not settle what happens to the frame, so the owner chose between ignoring it, ignoring it with a deadline on an unfilled gap, and queueing it. The option chosen:
  - "Ignore + gap deadline — … if NextNumIn has not advanced for about 2 x HeartBtInt while a resend is outstanding, end the session with a Logout. This gives fixpp the bound QuickFIX C++ gets from its heartbeat timeout, without reversing the #516 liveness ruling. It is new session-ending behaviour that no FIX document specifies, checked when a too-high frame arrives."
  - Reconfirmed after round 1, under R-5, as "Keep ignore + deadline". The owner had been told that all three engines queue the frame.
  - **Reading.** "Checked when a too-high frame arrives" is read at the frame, not at the sequence gate: a frame whose MsgSeqNum(34) is above NextNumIn, that arrives while a resend is outstanding and that its own arm does not end, runs the check wherever it leaves the inbound arm (§4.7). v0.3 read it as "reaches `check_inbound`", which is narrower than the words.
  - With `redeliver_poss_dup` off, which is the default, application messages are delivered once and in order whether the frame is ignored or queued (§4.5 gives the exception). The observable differences are in administrative frames and are listed in §4.5. fixpp#577 is where a too-high ResendRequest and Logout get the engines' treatment.
  - Implemented by D-537-1 and D-537-2 (the ignore) and D-537-5 (the deadline).
- **R-3.** The resend walk's silent fold of a failed `retrieve` into a GapFill is fixed in this batch (D-538-4). R-7, R-10 and R-13 say how.
- **R-4.** Two further leads are filed and are not in this batch: fixpp#577 and fixpp#578 (§8).
- **R-5 (given on B41 the same day; applies here).** "Overall we try not to change behavior of QuickFIX, the internal implementation can be different." **Reading.** Where the three engines agree, that is fixpp's default, and a difference must be stated with its reason. §4.5 states the differences that remain.

Rulings made after round 1 (2026-10-11):

- **R-6 (round 1's triage O-1; the consult).** The consult's option B, ratified:
  - "Ratify; fix in the inbound arm — Option B. A too-high Heartbeat clears the pending TestRequest, using the same TestReqID rule as an in-sequence one. One predicate in the out-of-sequence arm B28 already edits; no timer, no new state, nothing changed inside the liveness loop. B27's surface is untouched."
  - **Reading.** What is ratified is the consult's analysis: an unfilled gap is two conditions, with one ending each.
    - (a) The peer is alive but does not fill the gap: the D-537-5 deadline, a Logout naming the unanswered resend.
    - (b) The peer answers nothing: today's dead-peer ending (an unanswered TestRequest, then a silent `Disconnected`). It is unchanged, and the same with or without a gap.
  - v0.3 limited the Heartbeat rule to "while a resend is outstanding". That limit was this note's scoping, taken from the consult's predicate. It is not in the owner's words, and R-9 removes it for the frame that opens the gap.
  - **Reading.** "No timer, no new state, nothing changed inside the liveness loop" and "B27's surface is untouched" are the consult's description of its candidate B, the Heartbeat rule, and they hold for D-537-6. They do not describe the batch. The #536 fix adds a state test and a filter reset inside the liveness loop (D-536-2), and a private wake with its timer and one new member of `Session` (D-536-3). §8 states that contact with B27.
  - **Reading.** The consult's table has a row for HeartBtInt = 0: the deadline is off and the gap waits. D-537-5 also turns the deadline off for a negative interval and for a session with no clock. Neither the owner's words nor the consult names those two cases.
  - Not chosen: two endings for (a); the liveness-loop form (the consult's B′); a timer; a Logout on the dead-peer ending.
  - Implemented by D-537-6.
- **R-7 (round 1's triage O-2 and C-12; supersedes the v0.2 form of R-3's implementation).** The question put to the owner was "Resend walk, store read failure (not a plain gap)". The option chosen:
  - "Abort the answer — The walk stops and the session ends with the store's error, as the truncation arm does today and as QuickFIX C++ and Go do. The peer asks again after reconnecting. Nothing is gap-filled that the store merely failed to read. No event type is widened."
  - **Reading.** A read failure is a result that says the store could not read. A plain gap and a refused argument are not read failures (R-10 confirms the second). A cancelled read is one: the store did not read, and the walk returns `store_cancelled` unchanged (R-13 rules it). The same rule holds for a failed tail read. R-3's "not silent" is met by the abort. The error the session ends with is the store's own: the walk returns the code the failed read returned, unchanged. "As the truncation arm does today" is read as the shape of the ending and not as its code: the walk stops at once, the session ends, no Logout is sent, and the peer asks again. The truncation arm has no store error to return. The rule holds for every store, shipped or third-party, because the owner's words name no kind of store.
  - Implemented by D-538-4.
- **R-8 (round 1's triage C-4).** The option chosen:
  - "Declare 1.12 BREAKING — A 1.12 clause on each affected session declaration, the version.h entry, an effect-to-declaration-to-witness table in the note, and a C-ABI witness per effect. #537 changes documented outcomes; #536 and #538 are declared too, as the precedent did."
  - **Reading.** The number in these words stands: the minor is 1.12 (the numbering note below). "A C-ABI witness per effect" is read by effect, not by decision and not by table row: §6.2 gives each effect its own witness, or says why it has none.
  - Implemented by D-ABI-1 (§6.2).
- **Round 1's triage C-3 is a disclosure, not a fix.** The owner was told and did not object. After the deadline's Logout the state is `Disconnected` and the transport stays open until the peer closes it or `close()` / `Engine::stop()` runs (fixpp#534, batch B27). Stated in D-537-5 and §4.6; asserted by the deadline cells (§5).

Rulings made after round 2 (2026-10-11):

- **R-9 (round 2's triage O-1).** The Heartbeat rule of R-6 also covers the frame that opens a gap:
  - "Yes, same rule — A too-high Heartbeat answers a pending TestRequest whether it opens the gap or arrives during it: same TestReqID check as in sequence, then the ResendRequest goes out as today. The peer sees a ResendRequest and a normal recovery; the application's session survives it."
  - **Reading.** In the too-high arm, a Heartbeat that arrives with a TestRequest pending gets the in-sequence TestReqID disposition before the ResendRequest goes out. A mismatched TestReqID ends the session as it does in sequence, and then no ResendRequest goes out. The knob-off exclusion stays: the too-high arm is behind `validate_sequence_numbers`.
  - Not chosen: keeping R-6 as worded and disclosing the gap-opening Heartbeat as a limitation (option (b) of round 2's triage O-1).
  - Implemented by D-537-8.
- **R-10 (round 2's Codex C-2; slot 0).** The option chosen:
  - "Keep today's behaviour — An argument refusal is not a read failure: the slot is folded into the GapFill as today and the session stays up. Matches the three QuickFIX engines, which do not end a session on BeginSeqNo = 0. Recorded as your reading of the ruling."
  - **Reading.** D-538-4 has three classes. A plain gap keeps today's path. An argument refusal keeps today's fold. Every other error is a read failure and aborts the answer. "An argument refusal" is read as either code that the store's contract assigns to the request and not to the store: `store_seqnum_invalid`, which is the slot 0 case the owner was asked about, and `store_invalid_range`, which the walk cannot draw because it asks one slot at a time.
  - Not chosen: clamping the walk's start to 1 (a wire change none of the three issues asks for; a follow-up to file, §8); aborting literally.
  - Implemented by D-538-4.
- **R-11 (the residual pattern).** After §4.7's call sites, one pattern has no ending: a peer that never fills the gap and that, in every interval, sends only frames that are not too high and move nothing. Those frames refresh liveness, and the deadline is checked only on a too-high frame. The option chosen:
  - "Disclose as a limitation — Stated in the endings table and a B&L row, with a follow-up issue. The session stays Active and deaf for such a peer, as QuickFIX/J and /Go would. No new mechanism in the last rewrite the loop allows."
  - Not chosen: checking the deadline on every non-advancing frame.
  - Implemented by D-537-9.
- **C-ABI numbering.** This batch's entry is C-ABI minor **1.12**, with no merge-order condition, and this note calls it **the 1.12 clause**. v0.4 left the number open between two values, because B41 had then declared a breaking C-ABI change of its own. B41 has since withdrawn that change, so no other batch competes for the number.

Ruling made after round 3 (2026-10-11):

- **R-12 (round 3's triage O-5; the deadline Logout that cannot be built).** The question put to the owner was "The deadline Logout is built in a 256-byte buffer, so with very long CompIDs it cannot be built and the session ends at the deadline with no Logout. How should the note handle that case?" The option chosen:
  - "Keep it: disclose and test — As the note has it now. The silent ending is a documented limitation with a C++ test, and C-2's remedy adds its C-ABI accounting. No change to a leaf that other arms share."
  - **Reading.** D-537-5's branch stays as it is: when the Logout cannot be built, no frame goes out and `Disconnected` is recorded. "A C++ test" is the cell "D-537-5, a Logout that cannot be built" (§5). "Its C-ABI accounting" is that effect's own row in §6.2. This note reads the accounting as a witness, not as a "no C-ABI witness" entry, because a C session can reach the branch: the CompIDs and the heartbeat interval are both settable through the C ABI.
  - Not chosen: building the Logout in 512 bytes, which would remove the branch (round 3's triage O-5).
  - Implemented by D-537-5, which is unchanged, and by §6.2.

Ruling made after the targeted round, review index 4 (2026-10-11):

- **R-13 (the targeted round's triage O-1; a cancelled store read in the resend walk).** v0.5 kept today's GapFill fold for `store_cancelled`, on a condition it had not traced. The triage traced it by reading and found that the condition fails. Asked to rule, the owner first chose "Measure first, then decide" over ruling at once. The measurement is in D-538-4 ("Why `store_cancelled` aborts"). The question then put to the owner was "The measurement shows the peer does receive a GapFill over stored messages when the engine is stopped mid-answer. What should the resend answer do when its store read is cancelled?" The option chosen:
  - "Abort the answer — A cancelled read is treated as a read failure, as R-7's sentence says: the walk stops, nothing more is written, and the session ends with `store_cancelled`. The peer asks again after reconnecting. The rewriter applies this with the minor mutant corrections, then a full Codex and Opus round runs on the result."
  - **Reading.** "A cancelled read" is read as either store call of the walk that returns `store_cancelled`: the tail read, and a slot's `retrieve`. Only the first was measured. `store_cancelled` moves from class 1 to class 3 of D-538-4, and nothing else in the three classes changes. "The session ends with `store_cancelled`" is read as R-7's ending with that code: the walk's caller records `Disconnected`, the code is returned unchanged, and no Logout is sent. One path does not end with that code. On a Logon arm's NextExpectedMsgSeqNum(789) path under an `Engine`, the engine's stop has superseded the arm before it cancels the read; that path returns `superseded`, as it does today after an engine stop, and the session ends through `Engine::stop` (D-538-4, "What follows a cancelled read"). The owner's words do not name that path.
  - Not chosen: "Keep the GapFill, disclose it" (today's behaviour, stated as a limitation, with a follow-up issue).
  - Implemented by D-538-4.

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
- **D-538-4 (R-3, R-7, R-10, R-13). A store read failure aborts the resend answer. A gap and a refused argument do not.**
  - **Three classes**, by the code a slot's `retrieve` returns in `replay_outbound_range_`:

    | Class | Codes | The walk |
    |---|---|---|
    | 1. Not retained | `store_seqnum_gap` | today's path: the slot is folded into the GapFill run |
    | 2. Argument refusal | `store_seqnum_invalid`; `store_invalid_range` | today's fold (R-10). The session stays up |
    | 3. Read failure | every other code, from a shipped store or a third-party one. `store_cancelled` is one of them (R-13) | the walk stops and returns the store's error, unchanged (R-7) |

  - **Why `store_cancelled` aborts (R-13).** A cancelled read is a read the store did not make. Folded, as today, it can gap-fill stored messages on a transport that is still open, and a GapFill is permanent on the wire.
    - **What the walk does today, read from `replay_outbound_range_`, `FileStore::next_seqnum` and `async_mutex.hpp`.** A cancelled read does not stop the walk. `FileStore`'s calls take the store's lock through `async_lock()`, a coroutine over the lock sequence, and that sequence ends by replacing the coroutine thread's cancellation state (§3.5); so after the failed lock the walk is no longer cancelled. A failed tail read leaves `our_last` at 0, and the walk then writes one GapFill from BeginSeqNo(7) over the whole requested range. Its NewSeqNo(36) is the requested end plus one for an explicit EndSeqNo(16), and the next outbound number for EndSeqNo(16) = 0.
    - **Measured**, at `d16a1dc3` in the debug tree, with a temporary probe that was never committed to fixpp. The setup: a `FileStore` session under an `Engine` on the plain transport, on one I/O thread; one outbound `store` held inside `FileStore`'s offload hook (`g_store_offload_probe`, which runs at the start of a write on the file I/O executor), so that the store's lock stays taken; a ResendRequest for a range that holds stored application messages, whose tail read then waits for that lock; then `Engine::stop`. In every probe run the tail read returned `store_cancelled`, and the peer then read one GapFill over the stored application messages. The walk resumed and started that write before `Engine::stop` closed the transport, and the transport was still open when the write started. Both controls, which do not stop the engine, one with the hold and one without it, replayed the messages and wrote no GapFill.
    - **The limits of that measurement.** The hold manufactures the window, so the measurement shows that the path exists, and not how often it opens. It ran on one I/O thread. Not measured: TLS, a third-party store, a `Session` with no `Engine`, an executor with more than one thread, and a cancelled slot `retrieve`. That last variant is by reading only: `FileStore::retrieve` takes the same lock in the same way.
    - The probe's source, the diff of its temporary instrumentation, and the outputs of the probe runs and of both controls are in the parent repository, under `research/G19-fix-fpml-iso20022/research/probes/b28_538_cancelled_read/`.
    - **The cells (§5).** "D-538-4, a cancelled read" returns the code from a wrapper, at a slot's `retrieve` and at the tail read. "D-538-4, a cancelled read on the measured path" is the measurement's fixture as a test: a real `FileStore` lock, cancelled by `Engine::stop`. The probe found its window through diagnostics added to library source; the cell finds it through a wrapper around the `FileStore`, and that part is read and not run.
  - **Class 2 is closed.** Its members are the codes that `message_store.hpp`'s `retrieve` comment assigns to the request and not to the store: `begin == 0`, and `end != 0 && end < begin`. Both shipped stores return them before taking their lock or reading anything. A third-party store that returns either code is folded too; any code outside classes 1 and 2 aborts.
  - **What each shipped `retrieve` can return**, read from the `co_return std::unexpected` sites of `MemoryStore::retrieve` and `FileStore::retrieve`:

    | Code | When | Class |
    |---|---|---|
    | `store_seqnum_gap` | the number is not retained. The normal answer for a number never retained (`L-008-2`) | 1 |
    | `store_cancelled` | the walk's wait for the store's lock was cancelled. Under an Engine this is `Engine::stop` reaching a walk that waits behind a store call in flight; a `MemoryStore`'s lock is never queued on one serialisation domain | 3 |
    | `store_seqnum_invalid` | `begin == 0` | 2 |
    | `store_invalid_range` | `end != 0 && end < begin`. Cannot occur here: the walk asks `(k, k)` | 2 |
    | `store_io_failure` | `FileStore`: the log is not open (tested before the argument tests), or a payload read failed. Both stores: a reset ran between the index snapshot and the visit | 3 |
    | `store_visitor_aborted`, or a visitor's own error | the visitor threw or asked to abort. The walk's `CaptureVisitor` does neither | 3 |

  - **The tail read.** The walk's `next_seqnum(outbound, false)` has no argument to refuse. It can return `store_cancelled` (class 3) and, from a `FileStore` that is not open, `store_io_failure` (class 3). On a class 3 result the walk returns that error before it writes anything. Today the failure is dropped, `our_last` stays 0, and the whole range becomes one GapFill. Read from the two `next_seqnum` bodies.
  - **Slot 0 (R-10).** A ResendRequest with BeginSeqNo(7) = 0 or absent starts the walk at slot 0 when the store is not empty. Both stores refuse `retrieve(0, 0)` with `store_seqnum_invalid`. That is class 2: the slot is folded and the session stays up.
    - What today's fold puts on the wire, read: the GapFill run opens at slot 0, and `emit_gapfill_async(gap_start, …)` passes that number to `build_sequence_reset_gapfill`, which renders it. So the answer starts with a GapFill whose MsgSeqNum(34) is 0. The walk's empty-store branch clamps its start to 1; the per-slot walk does not.
    - This is kept as it is (R-10). Clamping the per-slot walk is a follow-up to file (§8). The cell in §5 pins today's wire, so that follow-up flips one named assertion.
    - Whether a dictionary validator rejects 7=0 before the walk was not read. The cell runs with the validate gate off, which is the default.
  - **The disposition of class 3 is the truncated-capture arm's** (`cv.truncated` in the same walk), except for the code. The truncation arm has no store error and returns `dispatch_aborted`; class 3 returns the store's. The rest is the same, read from source:
    - the walk returns at once. Frames already replayed stay sent. An open GapFill run is not flushed. Nothing more is written;
    - `on_inbound_active_resend_request_` records `Disconnected`, returns the error, and skips the inbound persist;
    - `honor_peer_next_expected_` (the Logon 789 path) does the same, unless its arm was superseded (`logon_arm_superseded_`); then it records nothing and returns `superseded`;
    - no Logout is sent. A Session driven without an Engine returns the error to its driver.
  - **Where this ending closes the transport, and where it does not.** The callers of `on_inbound_frame` were listed (`git grep -n 'on_inbound_frame(' -- src`; all are in `src/session/engine.cpp`), and each one's handling of a failed result was read:
    - the delivery loops of `run_read_pump` stop the pump, and the pump's stop runs `close(terminal)`, which closes the transport. This covers a ResendRequest in Active on either role, and the initiator's 789 path, whose Logon reply arrives through the pump;
    - the acceptor's direct delivery of its first Logon discards the result. The acceptor's 789 path runs inside that delivery, before the session enters Active. So on that path the abort leaves the transport open: the pump then starts on a session that is `Disconnected` and never reached Active, and closes it at the establishment deadline (`logon_timeout_ms`), or earlier if the peer closes.
    - D-537-5's Logout returns success on every path, so it never closes the transport (fixpp#534).
  - **The abort repeats while the failure lasts.** It skips the inbound persist, and the peer's gap is still open, so the peer asks again after it reconnects. A failure that persists (a payload read that fails for the same record; a log that is not open) aborts the answer again at the same slot, at every reconnect, and on the 789 path at every Logon. Nothing but an operator's action ends that: repairing the log, or a sequence reset. Reasoned from the reading above; the B&L row says so (§6.1). `L-092-1` discloses the same shape for a faulty frame.
  - **What follows a cancelled read differs from the other class 3 results** (R-13). Read from `Engine::stop`, `on_inbound_active_resend_request_` and `honor_peer_next_expected_`:
    - Under an `Engine` the cancellation is the engine's stop, so the session is being stopped whatever the walk returns. In Active the walk's caller records `Disconnected` and returns `store_cancelled`, as for any class 3 result; `Engine::stop` itself closes the transport and, once the role loops have ended, runs `close(terminal)` on the session.
    - On the 789 path under an `Engine`, the stop sets the session's engine-stop flag before it cancels, so the arm is superseded: `honor_peer_next_expected_` records nothing and returns `superseded`, as the disposition above says for a superseded arm.
    - It does not repeat. A cancelled read belongs to the stop and not to the store, so it is not a failure that persists: when the peer asks again on a later connection, the store reads.
    - For a `Session` driven without an `Engine`, which cancellation reaches the walk is the driver's, and the error is returned to the driver.
  - **No event is added.** `session_event_resend_slot_gap_filled` keeps its one producer and its meaning, and `B-424-2` is not edited. fixpp#424's ruling D4a covers a stored frame whose replay cannot be built; it is not extended to a frame the store could not read.
  - **R-3 is met by the abort:** the failure ends the session and is returned from `on_inbound_frame`.

### 2.3 Rejected

- **A stored base** (a `MemoryStore` member, or a field in `FileStore`'s sentinel). It adds state that can disagree with the entries. In `MemoryStore` it changes an installed class's layout. In `FileStore`, bumping `kFormatVersion` makes old and new builds refuse each other's logs, because the open check is an equality.
- **A search by sequence number.** It would also serve a hole in the middle of a log. The session cannot make one: `store()` requires `seq == next`, and the session stores no inbound frame. The counter-only advances are the default `reset_to` body, before the first frame, and `next_seqnum(dir, true)`, which the session uses only on the inbound direction. A hole is still reported as a gap.
- **Keeping (1, 1) and re-storing the Logon.** It would change 093's reset unit and its commit boundary to work around a lookup.
- **Gap-fill the unreadable slot and record an event** (v0.2's D-538-4). Superseded by R-7. A read error can be transient; a GapFill is permanent on the wire. No engine does it by default (§4.5).
- **Abort only on a persistent store.** R-7 applies to every store.
- **Abort on slot 0** (R-7 read as a set of two codes; Codex's round-2 fix). Not chosen (R-10). No frame can exist at slot 0, so folding it gap-fills nothing the store failed to read. It would differ from all three engines, and it would let any peer end a session with one frame.
- **Keep the GapFill fold for a cancelled read, and disclose it** (v0.5's class 1). Not chosen (R-13). The measurement in D-538-4 shows that fold reaching the peer as a GapFill over stored messages.
- **Clamp the per-slot walk's start to 1.** Not chosen here (R-10): it changes the wire for BeginSeqNo(7) = 0, which none of the three issues asks for. A follow-up to file (§8).

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
  - **The race sits inside a `try`.** When the outer cancellation landed during the Logout emit, the `co_await` of the race throws `operation_aborted` at its pre-check, and the wait's exits must still clear the wake and record the outcome. That is what the `try` is for. It is not what keeps the process alive: `noexcept` on a coroutine covers the call that creates it and not its body, so without the `try` the exception would leave phase 1 and reach `close`'s race with the grace timer. `operator||` rethrows only when both of its arms threw, and the grace timer's arm, awaited through `as_tuple`, does not throw; so the exception would be discarded, and `close` records `Disconnected` itself. Read from `Session::close` and asio's `awaitable_operators.hpp`. §5's cell for this branch therefore covers the `catch` and has no mutant.
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
  - **The check is one helper** (R-2), a new private member function of `Session`. It tests, in this order:
    1. the frame's MsgSeqNum(34) parses and is above NextNumIn;
    2. a resend is outstanding;
    3. the bound is on, and `steady_now()` is at or past the time point plus the bound.
    - When all three hold, it runs the expiry below, and its caller returns the helper's result at once: success, or the Logout leaf's error. Otherwise it does nothing and its caller goes on as today.
    - No timer is added.
  - **Tests 1 and 2 are inside the helper, not at its call sites.** Without them the helper would fire on an in-sequence frame that a Reject arm is about to consume. Such a frame advances NextNumIn and refreshes the time point only after the helper ran (`consume_rejected_seqnum_`), so a healthy slow replay would draw the Logout from a stale time point. Read; a guard cell in §5.
  - **Call sites.** Every exit of the Active inbound arm that a frame above NextNumIn can take while a resend is outstanding, that refreshes liveness, and that does not end the session. §4.7 derives them from `on_inbound_active_` and its sub-arms. They are:
    - `on_inbound_active_validate_failed_`, the `reject` outcome, before `consume_rejected_seqnum_`;
    - `on_inbound_active_poss_dup_`, Arm C (OrigSendingTime(122) absent) and the unparseable-122 branch, each before `consume_rejected_seqnum_`;
    - `on_inbound_active_sequence_reset_`, at each of its returns that leave the session up: after the best-effort Reject of a reset that `fromAdmin` rejected, and after `apply_inbound_sequence_reset` returned success;
    - `on_inbound_active_out_of_sequence_`, at its top, after the overflow arm and before its Heartbeat and PossDup arms (v0.3's one site).
  - **What the frame's own arm still does when the helper fires.**
    - At the Reject arms ahead of the gate, nothing: no consume, no Reject. `consume_rejected_seqnum_` does nothing for a number that is not in sequence, so nothing is lost.
    - In the Reset-mode arm the helper runs after the arm's own outcome is known, so the arm's Reject, where it has one, goes out first and the Logout follows.
    - The Reset-mode site needs no test of its own for "moved nothing". A reset that moves NextNumIn has ended the resend inside `apply_inbound_sequence_reset`, so the helper's second test is false. The site does not repeat that function's cases. Read; guard cell S3.
  - **The helper is never called ahead of the BeginString, CompID and SendingTime guards for a frame that reaches them.** Those guards, a MsgSeqNum(34) that does not parse, Stage 1's Arm D and the late parse failures end the session as today, with the disposition each has today (§4.7).
  - **One site is ahead of those guards by the code's own order.** At HEAD the validate gate is tested before the BeginString, CompID and SendingTime guards, so a frame that fails validation never meets them, today or after this change. Consequence, read: with the gate on, a dictionary-invalid frame above NextNumIn that those guards would have refused draws a Reject today, and after the bound draws the deadline's Logout. A frame that passes validation meets the guards first and keeps their disposition. No guard's disposition changes; §5 has a cell for each half.
  - **B41's condition.** B41's premise is that at every inbound arm the outbound number is peeked after any inbound callback of that arm. The helper's Logout goes through `inbound_logout_leaf_`, which peeks inside the leaf. At the call sites outside the Reset-mode arm, no inbound callback has run in the arm. At the Reset-mode arm's sites, `fromAdmin` has returned before the helper is called. So no inbound callback runs between the leaf's peek and its store call at any site. In the out-of-sequence arm the helper also precedes the PossDup redelivery's `fromApp`. v0.3 stated this as "ahead of every callback of its arm"; that is no longer true of the Reset-mode arm, and B41's premise does not need it.
  - **The bound** is 2 × HeartBtInt since the time point, where HeartBtInt is the value `run_liveness_loop` resolves. One helper computes it, by applying the saturating `detail::deadline_after` twice, never as a bare `2 * hb`. The deadline has expired when `steady_now()` is at or past it.
  - **Off** when HeartBtInt ≤ 0 or the session has no clock. The liveness loop is off only at exactly 0, and `deadline_after` requires a non-negative duration, so the deadline states its own test. A bound of zero would end the session at the second frame, which is the defect.
  - **Not configurable.** The ruling names the value.
  - **On expiry** the session sends a Logout whose Text(58) names the unanswered resend and the expected MsgSeqNum, records `Disconnected`, and returns success.
    - `inbound_logout_leaf_` gains a text parameter, passed to `build_logout`; its other callers pass an empty text. Its order is kept: `toAdmin` before `assign_outbound`. The leaf is declared in the installed `session.hpp`.
    - The text is a fixed phrase plus the expected MsgSeqNum. It is built in a stack buffer, as `honor_peer_next_expected_`'s too-high arm builds its own, and it is at most 64 bytes with the number at its longest.
    - **If the Logout cannot be built, no frame goes out and `Disconnected` is still recorded.** That is the shape of `honor_peer_next_expected_`'s Logout with a text. If `toAdmin` throws or the assign fails, the leaf records `Disconnected` and returns the error, as today.
    - **That branch is reachable, and more easily than D-537-7's.** The Logout is built in a buffer of the Logon's size, and its Text(58) field is longer than what the Logon carries beyond the common header (D-537-7 has the arithmetic). So a session whose CompIDs bring its Logon within a text's length of the buffer's end cannot build this Logout, and ends silently at the bound. Read and reasoned. It has a cell (§5), and the B&L row says so (§6.1). The branch is kept by ruling (R-12).
    - Not taken: falling back to a Logout with no text. It would fit wherever the Logon did (reasoned from the same arithmetic: it carries less than the Logon beyond the header, even after its MsgSeqNum gains digits). But it is one more branch, and the existing arm with a text has no such fallback.
  - **After expiry** (the C-3 disclosure). The state is `Disconnected` and a Logout was written. The transport stays open until the peer closes it or `close()` / `Engine::stop()` runs. Every later frame is drained by the `Disconnected` row. The liveness loop leaves at its next wake. This is fixpp#534 (B27), and every existing Logout-then-`Disconnected` arm has the same shape. Read.
  - **Refresh sites.** NextNumIn advances during a resend at the in-sequence `check_inbound` in `on_inbound_active_` and at the one in `consume_rejected_seqnum_` (a rejected frame that consumes its number, fixpp#423). Both already call `close_filled_resend_gap_()` straight after, and the refresh is one statement in that function.
    - That function has one compound condition today, which exits the state when NextNumIn has passed the recorded end. The refresh is a second statement after it. It runs when a resend is still outstanding, that is, when the function did not just leave the state, and when the session has a clock.
    - `apply_inbound_sequence_reset` also moves NextNumIn, when NewSeqNo(36) is above it, and in that case it ends the resend. Its other outcomes (NewSeqNo absent, equal, or below) move nothing and leave the state as it was; they are §4.7's Reset-mode rows.
    - Re-derive with `git grep -n -E 'check_inbound\(|set_next_inbound\(' -- src/session/session.cpp`. The other hits are on the Logon paths and in the reset unit, where no resend is outstanding unless fixpp#578 holds (§8).
  - **A GapFill that lands short of the recorded end** ends AwaitingResend: `apply_inbound_sequence_reset` exits the state whenever it advances NextNumIn, whatever the recorded end. That is progress in R-2's sense: NextNumIn moved. The next too-high frame takes the too-high arm again, sends a fresh ResendRequest, and starts a new time point. Read and reasoned; a cell in §5.
- **D-537-6 (R-6). While a resend is outstanding, a too-high Heartbeat answers a pending TestRequest.**
  - **Why.** While a resend is outstanding, every Heartbeat the peer sends is too high, and the out-of-sequence arm drops it before the only site that clears `pending_test_req_id_` can run (the in-sequence Heartbeat branch of `on_inbound_active_`). So a TestRequest fixpp sends in that state cannot be answered, and the liveness loop ends the session silently at its grace check, ahead of D-537-5's Logout. Read. Re-derive the clear site with `git grep -n -E 'pending_test_req_id_\.(clear|assign)' -- src/session/session.cpp`.
  - **Scope.** This decision covers a Heartbeat that arrives while a resend is already outstanding. The Heartbeat that opens the gap takes another arm and is D-537-8.
  - **Where.** In `on_inbound_active_out_of_sequence_`, after the deadline helper's call (D-537-5), so a Heartbeat that arrives past the bound draws the Logout and not a clear.
  - **Predicate.** The frame is a Heartbeat, the error is `session_seqnum_too_high`, a resend is outstanding, and a TestRequest is pending.
  - **Action.** The in-sequence branch's TestReqID disposition. An empty or matching TestReqID(112) clears `pending_test_req_id_` and `unanswered_tr_`. A mismatched 112 follows the in-sequence rule: a silent `Disconnected`. Then the arm returns: no advance, no persist, no `fromAdmin`.
  - **Unchanged.** A too-low Heartbeat keeps today's drop. So does a too-high Heartbeat with no TestRequest pending.
  - **The gate excludes the knob-off case by construction.** AwaitingResend is entered only behind `validate_sequence_numbers`. With the knob off, a too-high Heartbeat still leaves a pending TestRequest set. Noticed, not changed (§8).
  - **`B-093-9` is untouched.** It is about the liveness refresh, which already happens before the sequence check. This rule makes the same frame also answer the TestRequest. Session Layer §4.10, precedence 14, is the text both rest on.
  - **Contact with B27.** The rule reads and writes `pending_test_req_id_` and `unanswered_tr_`, which `run_liveness_loop` owns. No line of the loop changes.
- **D-537-7 (triage C-5). A ResendRequest that cannot be built ends the session.** In `inbound_resend_request_leaf_`, a failed `build_resend_request` records `Disconnected` and returns `dispatch_aborted`, the disposition `emit_gapfill_async` gives a GapFill that cannot be built.
  - Today the leaf returns success with AwaitingResend set, for a request the peer never got. With D-537-1 that session would ignore every later frame, and with HeartBtInt ≤ 0 it would do so without end.
  - **Reach, read from the builders and reasoned.** The Logon and the ResendRequest are built in buffers of the same size (`logon_buf` and `reply_buf` for the two roles' Logon; `rr_buf` in the leaf) by the same `Writer`, and share their header fields. They differ in:
    - the body: `build_logon` appends EncryptMethod(98) and HeartBtInt(108), then its optional fields (1137, 553, 554, 141, 789, 383, the 384 group, 464); `build_resend_request` appends BeginSeqNo(7) and EndSeqNo(16), which is always 0;
    - the digits of each frame's own MsgSeqNum(34).
    - So the ResendRequest is the longer frame only when the digits of BeginSeqNo, plus the digits its MsgSeqNum has gained since the Logon, exceed the digits of HeartBtInt by more than two, and by more again for each optional field the Logon carried.
    - The branch therefore needs both: CompIDs long enough that the Logon fills its buffer to within that margin, and a NextNumIn with enough digits. v0.3 named only the first.
    - The second is not only a matter of a long-lived session. A peer's Reset-mode SequenceReset raises NextNumIn to any NewSeqNo(36) above it (`apply_inbound_sequence_reset`), and BeginSeqNo is NextNumIn. The first is local configuration. No bound on a CompID's length was found: `fixpp_session_config_set_comp_ids` refuses a null or empty value and a forbidden byte, and `git grep -n -i -E 'comp_id.*(size|length)\(\)' -- src include` hits only tracing code. Read.
  - **The cell is mandatory** (§5). It finds the boundary by building, not from a byte count.
  - **Under an Engine this ending closes the transport**: the leaf returns an error on a frame the read pump delivered (D-538-4 lists the callers).
  - Not taken: leaving AwaitingResend and retrying. The build fails for the same CompIDs at every later too-high frame.
  - A failed assign or emit already records `Disconnected` (read from the leaf).
- **D-537-8 (R-9). A too-high Heartbeat that opens the gap answers a pending TestRequest, and then the ResendRequest goes out.**
  - **Why.** Read:
    - the first frame above NextNumIn takes `on_inbound_active_too_high_` whatever its type, and that function receives two numbers and never reads TestReqID(112);
    - the grace deadline is fixed when the TestRequest is sent, and the grace check reads only the pending id (`run_liveness_loop`);
    - a conforming replay carries no Heartbeat: administrative frames are gap-filled and not resent, as fixpp's own walk does (`replay_outbound_range_`).
  - So today, when the peer's answer to fixpp's TestRequest is the frame that reveals the gap, the id stays set. A peer that then fills the gap is still ended silently at the grace check, and a peer that does not fill it gets the dead-peer ending, not the deadline's Logout. Reasoned from that reading; RED cells in §5.
  - One documented way into that trace: with `enable_next_expected_msg_seq_num` on, a too-high Logon is tolerated with no ResendRequest, and the too-high arm is the recovery of last resort at the next inbound frame (`L-027-2`). On a quiet line that frame is the TestRequest's answer.
  - **Where.** At the top of `on_inbound_active_too_high_`, before `enter_awaiting_resend`, before the time point is written and before the ResendRequest. The sub-arm gains the frame header as a parameter. It is a private member declared in the installed `session.hpp`; no layout changes.
  - **Predicate.** The frame is a Heartbeat and a TestRequest is pending. The arm is already behind `!is_awaiting_resend()` and `validate_sequence_numbers`.
  - **Action.** The in-sequence branch's TestReqID disposition, as in D-537-6:
    - an empty or matching TestReqID(112) clears `pending_test_req_id_` and `unanswered_tr_`. The arm then goes on as today: AwaitingResend, the time point, the ResendRequest;
    - a mismatched 112 is a silent `Disconnected`, the in-sequence rule. The arm returns there: AwaitingResend is not entered and no ResendRequest goes out.
  - **One routine holds the disposition for D-537-6 and D-537-8.** It is a new private member function of `Session`, because it writes `pending_test_req_id_`, `unanswered_tr_` and the state. The in-sequence branch of `on_inbound_active_` is not edited.
  - **Unchanged.** The Heartbeat gets no `fromAdmin`, no advance and no persist, as any first too-high frame today. A too-high Heartbeat with no TestRequest pending draws the ResendRequest as today. With `validate_sequence_numbers = false` the arm is not taken (§8). The rule runs ahead of PossDup Stage 1, because the too-high arm does: a gap-opening Heartbeat with PossDupFlag(43)=Y and no OrigSendingTime(122) clears the pending TestRequest, where the same frame in sequence is Rejected by Stage 1's Arm C and clears nothing. A conforming peer does not send that frame.
  - **What the peer and the application see.** The peer reads a ResendRequest and a normal recovery. The application's session survives it. If the peer never fills the gap, the session ends by §4.6: the deadline's Logout for a peer that keeps sending, the dead-peer ending for one that goes silent.
- **D-537-9 (R-11). One pattern has no ending. It is disclosed, not fixed.**
  - **The pattern.** A peer that never fills the gap and that, inside every HeartBtInt, sends only frames that are not above NextNumIn and that do not move it.
  - **Why it has no ending.** Each such frame refreshes liveness, so the liveness loop sends no TestRequest. None is a too-high frame, so the helper's first test is false wherever it is called. Read.
  - **Members**, derived as the complement of §4.7's table and not from examples: every exit of the Active inbound arm that a frame at or below NextNumIn can take, that refreshes liveness, that leaves the session up, and that does not advance NextNumIn.
    - a too-low PossDup frame: dropped, or redelivered, by Stage 2 of the out-of-sequence arm;
    - a too-low Heartbeat: dropped;
    - a Reset-mode SequenceReset whose own MsgSeqNum(34) is at or below NextNumIn and whose NewSeqNo(36) is absent, equal or below, or which `fromAdmin` rejects;
    - a frame at or below NextNumIn that a Reject arm ahead of the sequence gate answers and does not consume. Those arms are the validate gate's Reject, with that gate on, and Stage 1's Reject for OrigSendingTime(122). `consume_rejected_seqnum_` does nothing for a too-low number, as for a too-high one. It also returns at once for a Logon and for a SequenceReset, so one of those at NextNumIn is Rejected and not consumed (§4.7).
  - **What it looks like.** The session stays Active. Nothing new reaches the application, because the peer sends nothing new. With `redeliver_poss_dup` on, each resent PossDup application frame is handed to `fromApp` again, as it is when no gap is open. It ends when the peer sends any frame above NextNumIn after the bound (the deadline), stops sending (the dead-peer ending), or closes; or when `close()` / `Engine::stop()` runs. QuickFIX/J and QuickFIX/Go would also wait.
  - **No mechanism is added.** Not chosen: running the check on every frame that does not advance (R-11).
  - Stated in §4.6, in a B&L limitation row (§6.1), and pinned by a cell (§5). A follow-up issue is to be filed (§8).

### 4.4 Rejected

- **Queue the frame** (§4.10; the engines). 013 FR-009 specified a queue, and `ResendState::inbound_held` exists with no writer. It needs a capacity policy, an order for held administrative frames, and a delivery path, and it gains nothing on the wire while the request runs to 0. `L-013-2`, which describes the queue as shipped, is corrected (§6). Reconfirmed by the owner after round 1 (R-2).
- **A second ResendRequest per extra frame.** All three engines suppress it by default.
- **Stop refreshing inbound liveness on a too-high frame**, which is what bounds QuickFIX C++. It reverses owner ruling R-1 on fixpp#516 (`B-093-9`), and it still does not protect a slow replay.
- **No deadline** (QuickFIX/J and QuickFIX/Go). Where the peer never fills the gap (`L-092-1`, `L-092-2`), the session would stay Active and ignore every new message until the connection ends. Not taken (R-2).
- **A timer for the deadline.** It adds a sleeper, and it would still need D-537-6 to stop the TestRequest from ending the session first. v0.2 rejected it for another reason ("the peer's Heartbeat answering the TestRequest is itself a too-high frame, so the check runs"). That was incomplete: the check ran before the bound, and the Heartbeat was then dropped. D-537-6 is the fix.
- **Two endings for an alive peer** (a Logout when it keeps sending, a silent end when it is quiet). Not taken (R-6).
- **The grace exit reading "any sound frame since the TestRequest"** (the consult's B′). It gives the same single ending, also for a peer that sends traffic and never heartbeats, and also with the knob off. It edits `run_liveness_loop`, which is B27's surface. Not taken (R-6); noticed in §8.
- **A Logout on the dead-peer ending.** Not taken (R-6). QuickFIX C++ also ends that case with no Logout.
- **The deadline check ahead of the pre-gate arms** (the first form of Codex's round-2 C-1). It would sit above the BeginString, CompID and SendingTime guards, so a frame those guards end silently today would draw a Logout. Not taken; the helper at each surviving exit is (D-537-5).
- **The helper without its own too-high test.** It would end a healthy slow replay at an in-sequence rejected frame (D-537-5). Not taken.
- **A guard predicate at the validate gate's site** (fire only if the frame would also pass the BeginString, CompID and SendingTime guards). It would leave a stream of frames above NextNumIn with no ending, and that stream is outside R-11, which covers only frames that are not too high. Not taken; the consequence of the code's order is stated in D-537-5 and pinned by cell V2.
- **The deadline check on every frame that does not advance NextNumIn.** It would close D-537-9's pattern. Not chosen (R-11).
- **R-6 kept as worded, with the gap-opening Heartbeat disclosed** (option (b) of round 2's O-1). Not chosen (R-9).

### 4.5 What still differs from QuickFIX after this change (R-5)

Frames (R-2):
- **The frame is ignored, not held.** With `redeliver_poss_dup` off, which is the default, an application message is delivered once and in order either way. A QuickFIX application receives the queued original; a fixpp application receives the peer's replay, which carries PossDupFlag(43)=Y and OrigSendingTime(122). That is observable to the application, and it is already true of the first too-high frame.
  - **The exception: `redeliver_poss_dup` on.** Stage 2 of the out-of-sequence arm does not look at the error. A too-high PossDup application frame is handed to `fromApp` while the gap is open, with no advance, and again when it later arrives in sequence. Read. It exists at the base and D-537-3 keeps it; no behaviour changes here.
- **A held TestRequest** is answered by the engines once the gap closes. fixpp never answers it, because the peer gap-fills over it. A too-high TestRequest as the *first* frame already behaves this way.
- **A too-high Heartbeat** is processed by the engines once the gap closes. fixpp ignores it, with no `fromAdmin`, except that it answers a pending TestRequest, whether it arrives during the gap (D-537-6) or opens it (D-537-8). What each engine does with its own pending TestRequest in those two traces was not read.
- **A too-high ResendRequest or Logout** is acted on by the engines whatever its number. fixpp does not act on it. That is fixpp#577, and it is not changed here.
- **A frame sent during the replay** is ignored and fetched by a second ResendRequest (D-537-1). The engines hold it.
- **After a GapFill that lands short of the recorded end**, fixpp leaves its resend state and can send a new ResendRequest where QuickFIX C++ would not: that engine keeps its range until the range's end is reached. Behaviour that exists today.

An unfilled gap (R-6; the endings are in §4.6):
- **The peer is alive and does not fill the gap.** fixpp sends a Logout after 2 × HeartBtInt without progress. QuickFIX C++ disconnects at its heartbeat timeout (2.4 × HeartBtInt), with no Logout, because a too-high frame never refreshes its last-received time. QuickFIX/J and QuickFIX/Go wait. The engines differ among themselves, so there is no single behaviour to keep.
- **The peer answers nothing.** fixpp ends as for any dead peer: an unanswered TestRequest, then a silent `Disconnected`. QuickFIX C++ does the same. This is not a difference.
- **The peer sends only frames that are not too high and move nothing** (D-537-9). fixpp waits, as QuickFIX/J and QuickFIX/Go would (R-11). Whether QuickFIX C++'s heartbeat timeout bounds that peer was not read.
- **HeartBtInt = 0.** The gap waits, as in QuickFIX/J and QuickFIX/Go.

A store read failure in a resend answer (R-7, R-10):
- fixpp aborts the answer and ends the session, with no Logout (D-538-4). This follows QuickFIX C++, whose store error leaves `generateRetransmits` and whose handler logs it and disconnects, and QuickFIX/Go, whose `resendMessages` returns the store error and whose session then leaves its connected state.
- QuickFIX/J differs: it rethrows the error unless `ForceResendWhenCorruptedStore` is set, logs it, and sends nothing; the session stays logged on with the peer's gap open. fixpp follows the two engines that end the session, because a peer whose gap cannot be filled gains nothing from a session that stays up.
- **BeginSeqNo(7) = 0.** fixpp folds slot 0 and stays up (D-538-4, class 2). None of the three engines ends the session for it: each passes the number to its store's range read, and QuickFIX/Go Rejects an absent tag 7 and stays up. That is from round 2's triage, which read the engine sources; it was not re-read for this rewrite. What differs is the frame fixpp's answer starts with (a GapFill with MsgSeqNum(34) = 0); the follow-up of §8 is where that changes.

### 4.6 How a stalled gap ends, and what liveness does to a session with a gap (R-6, R-9, R-11)

Scope: the endings of a gap the peer does not fill, and the liveness endings of a session that has a gap. The table is derived from §4.7; §4.4, §4.5, §5 and §6 are written from it. The endings this batch does not change are grouped in the last row. E is the time the gap opened; HB is HeartBtInt.

| Condition | Trigger | What fixpp does | What the peer sees |
|---|---|---|---|
| (a) The peer is alive and does not fill the gap, and sends a frame above NextNumIn after the bound | that frame takes a helper exit of §4.7 at or after 2 × HB since NextNumIn last advanced | a Logout whose Text(58) names the unanswered resend; `Disconnected`. A Reset-mode SequenceReset draws its own Reject first, where it has one. No Logout where it cannot be built (D-537-5) | the Logout. The transport stays open until the peer closes it or `close()` / `Engine::stop()` runs (fixpp#534) |
| (b) The peer answers nothing | fixpp's TestRequest is still pending at the liveness loop's grace check | silent `Disconnected`, as today, with or without a gap | no Logout; the transport stays open (fixpp#534) |
| The peer sends frames but never a Heartbeat for fixpp's TestRequest | the same grace check: it reads only the pending id | as (b), with or without a gap | as (b) |
| A too-high Heartbeat carries a TestReqID(112) other than the pending one, during the gap (D-537-6) or as the frame that opens it (D-537-8) | the in-sequence mismatch rule | silent `Disconnected`. For the frame that opens the gap, no ResendRequest goes out | no Logout; the transport stays open |
| **The residual pattern (R-11, D-537-9):** the peer never fills the gap and, inside every HB, sends only frames that are not above NextNumIn and move nothing | none: no TestRequest is sent, and the helper's too-high test is false | nothing. The session stays Active and the gap waits. With `redeliver_poss_dup` on, each resent PossDup application frame is handed to `fromApp` again, as it is when no gap is open | what each such frame draws today: nothing, or a Reject |
| HeartBtInt ≤ 0, or no clock | none | nothing; the gap waits | nothing |
| **Unchanged by this batch, with or without a gap** | the protocol-fatal guards (BeginString or CompID mismatch; SendingTime; a MsgSeqNum(34) that does not parse; PossDup Stage 1's Arm D; a too-low frame without PossDup; sequence-number overflow; a drained sequence-number mutex); a late parse failure; the peer's Logout received in sequence (a too-high one is ignored, fixpp#577); a callback that throws; a transport or read failure; a local `close()` or `Engine::stop()` | as today, each with the disposition it has today | as today |

- **What this note adds to that table.**
  - Condition (a)'s Logout. It is the only new ending of a stalled gap.
  - The mismatch rule at frames that did not reach it before: a too-high Heartbeat during the gap was dropped, and one that opened the gap drew only a ResendRequest. The rule itself is the in-sequence one.
  - The dead-peer endings are today's. D-537-6 and D-537-8 keep an alive peer that answers TestRequests out of them, whether its answer arrives during the gap or opens it.
- **Endings this note adds that are not endings of a gap:** the R-7 abort (D-538-4), and the ResendRequest that cannot be built (D-537-7). Both return an error; D-538-4 says where that closes the transport.
- **A healthy slow replay is not ended.** Replayed frames are in sequence, NextNumIn advances, and the time point is refreshed. The deadline is tripped only when a frame above NextNumIn arrives after 2 × HB with no advance: the peer interleaves new frames or Heartbeats with a replay it has not started, or has stalled.
- **The TestRequest answer that opens the gap is not an ending** (D-537-8). The pending id is cleared, the ResendRequest goes out, and a peer that fills the gap keeps its session.
- **The timelines are reasoned.** Round 2's triage re-read the liveness loop and confirmed the cadence that cells Q and N assume; nothing here was run.
  - A peer that only heartbeats, or that keeps sending new messages: each too-high frame refreshes liveness and runs the helper; a TestRequest sent in a lull is answered by the peer's too-high Heartbeat; the first frame at or after E + 2 HB draws the Logout. Expected between E + 2 HB and about E + 3 HB.
  - A peer that sends nothing: TestRequest at E + HB, then the silent `Disconnected` at the loop's `grace_deadline`. The helper never runs, because no frame arrives.
  - A quiet peer whose TestRequest answer opens the gap and that then fills it: the id is cleared at the answer, the replay advances NextNumIn, and the grace check finds no pending id.
  - Cells Q, N, R, T1, T2 and T3 (§5), with their mutants, are what turn these timelines into observation. Until they run, this section is a prediction; §5 says what happens if a cell disagrees.

### 4.7 Every exit of the Active inbound arm for a frame above NextNumIn (R-2, R-9)

§4.6 and the call sites of D-537-5 are read off this table. v0.3 had no such table and attached each rule to one arm.

- **How it was derived.** `on_inbound_active_` in `src/session/session.cpp` was read from its top to its final persist, with every sub-arm that `on_inbound_active_slow_` dispatches to. An exit is a `co_return`, or a `break` to a sub-arm. Re-derive it the same way; do not extend the table from a symptom.
- **The liveness writers.** `git grep -n 'last_inbound_steady_ =' -- src/session/session.cpp`. The hit inside `on_inbound_active_` is the one per-frame writer; it sits after the garbled and faulty-frame exits and before everything else. The other hits are `open()`'s seed and the two roles' seeds on entering Active.
- **The subject.** A frame whose MsgSeqNum(34) is above NextNumIn, arriving while a resend is outstanding. `validate_sequence_numbers` is then on, because AwaitingResend is entered only behind it.
- **The invariants read off the table.**
  - (i) Above NextNumIn, a resend outstanding, liveness refreshed, the session not ended by the frame's own arm ⇒ the deadline helper runs.
  - (ii) A too-high Heartbeat with a TestRequest pending, knob on ⇒ the TestReqID disposition applies: D-537-6 during the gap, D-537-8 at the frame that opens it.

Rows are in the arm's order. "Own disposition" is what the frame's arm does today, and still does whenever the helper does not fire.

| Exit | Can the subject take it? | Refreshes liveness | Own disposition | Helper runs | Cell (§5) |
|---|---|---|---|---|---|
| The third field is not MsgType(35) (`note_garbles_`) | yes | no | disregarded | no. No refresh, so the dead-peer ending bounds a peer that sends only these | — |
| The header scan found a fault (`dispose_unparseable_`) | yes | no | the faulty-frame dispositions of 092 | no, for the same reason | — |
| Validate gate, `parse_failed` (`close_on_late_parse_failure_`) | yes, with the gate on | yes | the session is closed | no: the arm ends the session | — |
| Validate gate, `reject` (`on_inbound_active_validate_failed_`) | yes, with the gate on. Reject and Logout frames are exempt from the gate | yes | a Reject; the session stays up; the number is not consumed | **yes**, before `consume_rejected_seqnum_`. When it fires, no Reject | V |
| BeginString or CompID guard | yes | yes | silent `Disconnected` | no: the arm ends the session | G1 |
| SendingTime guard (`on_inbound_active_sending_time_reject_`) | yes. Reject and Logout frames are exempt | yes | Reject, Logout, `Disconnected` | no: the arm ends the session | G1 |
| Reset-mode SequenceReset (`on_inbound_active_sequence_reset_`): `fromAdmin` throws, or a late parse failure | yes | yes | the session is closed | no: the arm ends the session | — |
| Same arm: `fromAdmin` rejects the reset | yes, with an application registered | yes | a best-effort Reject; the reset is not applied; the session stays up | **yes**, after the Reject, before the return | S1 |
| Same arm, `apply_inbound_sequence_reset`: NewSeqNo(36) above NextNumIn | yes | yes | NextNumIn moves and AwaitingResend ends | no: progress. The helper is called after the function returns and does nothing, because the reset ended the resend | S3 |
| Same arm, `apply_inbound_sequence_reset`: NewSeqNo absent or equal (nothing), or below (a Reject) | yes | yes | nothing, or a Reject; the session stays up | **yes**, after the function returned success | S2 |
| Same arm: `apply_inbound_sequence_reset` fails (the counter write; the Reject's assign or its `toAdmin`) | yes | yes | `Disconnected`, error returned | no: the arm ends the session | — |
| MsgSeqNum(34) does not parse | no: it is not above NextNumIn | — | silent `Disconnected` | — | — |
| The too-high arm (`on_inbound_active_too_high_`) | no: its guard is `!is_awaiting_resend()`. It is the exit that opens a gap | yes | AwaitingResend and a ResendRequest | no: nothing was outstanding; the time point starts here. Invariant (ii): D-537-8 | T1, T2, T3 |
| PossDup Stage 1, Arm C: OrigSendingTime(122) absent (`on_inbound_active_poss_dup_`) | yes, for a PossDup frame that is not a SequenceReset | yes | a Reject; the session stays up | **yes**, before `consume_rejected_seqnum_`. When it fires, no Reject | P1 |
| Stage 1: 122 present and unparseable | as Arm C | yes | as Arm C | **yes**, as Arm C | P2 |
| Stage 1, Arm D: 122 later than SendingTime(52) | as Arm C | yes | Reject, Logout, `Disconnected` | no: the arm ends the session | — |
| `check_inbound` refuses for a reason other than too high (`session_already_closed`) | yes | yes | `Disconnected` ("Arm B"). A Heartbeat and a PossDup frame return before Arm B: the Heartbeat is dropped, and the PossDup frame is dropped or redelivered. D-537-6 does not apply, because its predicate needs the too-high error | **yes**, at the top of `on_inbound_active_out_of_sequence_`: it is the call of the rows below, which runs for any frame above NextNumIn and does not read the error. Where it does not fire, Arm B ends the session for every frame that is neither a Heartbeat nor a PossDup frame | —. The call site's cell is the out-of-sequence site's; no cell of §5 takes this exit |
| `on_inbound_active_out_of_sequence_`, too high: a Heartbeat | yes | yes | dropped. Invariant (ii): D-537-6 | **yes**, at the top of the function | the deadline cells |
| Same function, too high: a PossDup frame that passed Stage 1 | yes | yes | dropped, or redelivered to `fromApp` | **yes**, the same call | the deadline cells |
| Same function, too high: any other frame | yes | yes | ignored (D-537-2) | **yes**, the same call | the deadline cells |

- **Every row that says "yes" under both "Can the subject take it?" and "Refreshes liveness" ends the session, moves NextNumIn, or runs the helper.** That is invariant (i), and it is what closes round 2's C-1.
- **D-537-9's members are the exits above that a frame at or below NextNumIn can take, that leave the session up, and that do not advance it.** The helper is called at each of them, and its first test is false.
- **An in-sequence frame** either advances NextNumIn, which refreshes the time point, or is ended by its arm. The exceptions are frames at NextNumIn that leave the session up and move nothing, and each is a member of D-537-9. Those found by reading are: a Reset-mode SequenceReset whose MsgSeqNum equals NextNumIn and which moves nothing; with the validate gate on, a Logon or a SequenceReset that the gate Rejects; and a Logon with PossDupFlag(43)=Y and no usable OrigSendingTime(122), which Stage 1 Rejects, because Stage 1 exempts only MsgType "4". The last two are Rejected and not consumed: `consume_rejected_seqnum_` returns at once for MsgType "A" and "4". Each refreshes liveness, and the helper's first test is false for it.
- **LogonReceived.** The arm serves that state too. Under an Engine no frame is delivered in it (§5 gives the reason), so the table has no separate column for it.

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
| D-538-4 | RED first: a wrapper whose `retrieve` returns `store_io_failure` for one slot, with stored application messages on both sides of it. The frames before the slot are replayed; no GapFill covers the slot; `on_inbound_frame` returns `store_io_failure`, the code the wrapper returned; the state is `Disconnected`; no Logout; no `session_event_resend_slot_gap_filled`. Today the slot is gap-filled and the session stays Active | the new error test deleted |
| D-538-4 | Under an Engine, a ResendRequest in Active, on each role: the same failure closes the transport at once (the read pump stops on the failed frame) | the same mutant |
| D-538-4 | RED first, the tail read: `next_seqnum(outbound, false)` fails with `store_io_failure`. Nothing is written; `Disconnected`; the error returned is `store_io_failure`. Today one GapFill covers the range | the tail test deleted |
| D-538-4 | RED first, the 789 path, initiator, under an Engine: the same failing store under `honor_peer_next_expected_`'s resend. `Disconnected`, and the transport is closed at once | the first mutant |
| D-538-4 | RED first, the 789 path, acceptor, under an Engine: `Disconnected`; the session never reaches Active; **the transport is still open when the first Logon's delivery returns**, and it is closed at the establishment deadline (`logon_timeout_ms`). The cell asserts each of these | the first mutant: the session reaches Active |
| D-538-4, class 1 controls | A plain gap folds silently. An unbuildable replay still folds with its event (fixpp#424 unchanged) | each control is RED if the abort is widened to its code |
| D-538-4, a cancelled read (R-13) | RED first. A wrapper whose `retrieve` returns `store_cancelled` for one slot, and, as a second input, whose `next_seqnum(outbound, false)` returns it: nothing more is written, `Disconnected`, and `on_inbound_frame` returns `store_cancelled`. Today the slot, or the whole range, is gap-filled | `store_cancelled` folded again: a GapFill is written |
| D-538-4, a cancelled read on the measured path (R-13) | RED first. The fixture of the measurement, without its diagnostics in library source. An acceptor session under an `Engine` on the plain transport, facing a raw peer, with one thread running the I/O (`tests/session/plain_engine_rig.hpp`). Its store is a real `FileStore` on a file I/O executor of its own, behind a delegating wrapper that a test `MessageStoreFactory` makes, as `HookedStoreFactory` does for a `MemoryStore`. Application messages go out through `Engine::send`, and the peer reads them. One more `Engine::send` is then held inside its `store`, in the offload hook (`install_store_offload_probe`, with a one-shot latch), so the store's lock stays taken. The peer sends a ResendRequest over the stored application messages. **The gate, with no sleep:** the wrapper records that `next_seqnum(outbound, false)` was entered after the ResendRequest; the test then runs the handlers that are ready until none is, which takes the call past `FileStore::next_seqnum`'s leading post to the store's lock, where it queues because the hold is in flight. Then `Engine::stop`. The hold is released when the peer's read has ended, so the held message reaches the peer in neither tree. Today the peer reads one GapFill over the stored messages. After the fix the peer reads no frame after its ResendRequest, and its connection ends. **Two conditions keep the cell honest.** (a) It shows that the window was hit: the wrapper recorded the tail read's entry before the stop began and `store_cancelled` as its result, and the hold was in flight at both moments. `FileStore::next_seqnum` returns that code only from a failed lock, so the wrapper's record is enough and no diagnostic is needed in library source. If the record is missing, the cell fails as "the path was not exercised"; it does not pass. (b) A control arm, the same up to the queued tail read, releases the hold and does not stop the engine: the peer reads the stored messages replayed with PossDupFlag(43)=Y, and no GapFill. So "no GapFill" cannot be met by an empty range. The gate and the release point are read from `FileStore::next_seqnum` and `Engine::stop`, and not run; the implementation steps below say what happens if the window cannot be hit | `store_cancelled` folded again: the peer reads the GapFill, and the cell is RED |
| D-538-4, class 2 (R-10), slot 0 | Guard: a ResendRequest with BeginSeqNo(7) = 0, and one with tag 7 absent, over a non-empty store, with the validate gate off. `on_inbound_frame` succeeds; the session stays Active; the stored application messages are replayed with PossDupFlag(43)=Y. The answer's first frame is a GapFill whose MsgSeqNum(34) is 0: today's wire, pinned in one assertion that names the clamp follow-up of §8, which flips it | the abort widened to `store_seqnum_invalid`: the cell is RED |
| D-538-4, class 3 from another code | A wrapper whose `retrieve` returns, for one slot, a code outside classes 1 and 2 that no shipped `retrieve` returns: the answer aborts and `on_inbound_frame` returns that same code | the abort narrowed to `store_io_failure` alone: the cell is RED; or the return replaced by a fixed `dispatch_aborted`: the cell is RED |

- `run_q28` in `test_memory_store_round_trip.cpp` discards a `retrieve` result on exactly this state. It is noted, not changed.

**#536**

| Decision | Cell | Fix-deleted arm |
|---|---|---|
| D-536-1 | The lifted cells of `session_536_*`: both shared-clock cells turn GREEN. The failure string is reworded to what the cell observes | either sweep restored |
| D-536-1 | A counting clock shows that open, traffic, graceful close and terminal close issue no `cancel_sleeps()` call. This is the arm that catches a third sweep site | either sweep restored |
| D-536-2, the reset | `close` completes promptly for a session whose liveness loop has sent a Heartbeat, and for one that has sent a TestRequest, on a mock clock. `AdminDistinctNowTest.TR_DistinctNow_*` and `CrossSessionTestReqID.*` are the existing witnesses (measured RED without the reset, §3.4, run P1+P2) | the filter reset deleted |
| D-536-2, the state test | `close` runs while the loop's TestRequest emit is suspended in the store (a hooked store holds it), and the emit is then released. `close` must complete without the clock advancing. **Fixture: the callback transport (`transport_send_`), so the released emit succeeds.** On a live transport `close` has closed it, the emit fails, and the loop leaves before the grace sleep, so the cell could not fail. The probe did not cover this window | the state test before the grace sleep deleted, shown RED on that fixture |
| D-536-3 | RED first: a clock whose `cancel_sleeps()` does nothing. A graceful close returns when the peer's Logout arrives, not at the logout timeout | the wake's trigger in the LogoutSent arm deleted |
| D-536-3 | Changed: `AdversarialSessionTest.LogoutGracefulCancelMidSleep` pins the old mechanism (a sweep ends the logout wait). It becomes the cell for the new one: a sweep by a third party during the logout wait does not end it; the confirmation, the deadline or the grace timer does | the deadline arm replaced by a `sleep_until` whose `operation_aborted` is caught and read as the end of the wait, as at the base |
| D-536-3, lifetime | The grace timer wins while the inner wait is pending. A terminal close escalates during the wait. In both, `close` completes and no wake is called after the wait ended | the clear of the wake on exit deleted |
| D-536-3, lifetime | Control (GREEN at the base and after): the peer's Logout arrives while fixpp's Logout emit is still suspended in the store. Fixture: the hooked store holds only that first store call. The trace, read from `on_inbound_active_logout_` and `run_logout_phase1`: the Logout meets the Active arm, which writes a confirming Logout and records `Disconnected`; phase 1 then resumes, records `LogoutSent` over it (`record_state_transition_` assigns whatever the current state is), finds `logout_confirmed_` unset, and waits for its deadline. Asserted: both Logouts are written; `close` completes no later than the logout timeout and not before the store is released; the final state is `Disconnected`; the wake is never triggered, during the wait or after it | none: no statement of D-536-3 is on this trace except the deadline arm, which the `LogoutGracefulCancelMidSleep` row mutates. The cell shows that D-536-3 leaves the trace as it is |
| D-536-3, the `try` around the race | Control (GREEN at the base and after); not a guard, because no edit to the `try` makes it fail. It runs the `catch` branch, for `[const §IX.1]`. At the base the wait already sits inside a `try`. The grace timer wins, or a terminal close escalates, while phase 1's Logout emit is held in a hooked store: the race's `co_await` throws at its pre-check, the `catch` runs, and `close` completes | none that this cell can see: with the `try` removed the exception reaches `close`'s race with the grace timer, which discards it. The `try` is there so that phase 1 leaves the wait through its own exits, which clear the wake and record the outcome (D-536-3) |
| D-536-4 | Control kept: an explicit `cancel_sleeps()` still ends an unclosed session's liveness loop | — |
| D-536-4, disclosure | An explicit `cancel_sleeps()` on `Engine::clock()` with open sessions: each stays Active and sends no further Heartbeat. This pins the disclosed limitation | — |

**#537**

The first step of implementation is a probe: the one-statement edit of D-537-2 is run over §3.4's selection, as the #536 probe was, before any #537 cell is written. The tests that pin the old outcome are found by running, not by reading: `Disclosed_L1_*` and `Disclosed_L2_*` are named from reading only. The probe has not run; its return condition is under "Implementation steps" below.

Every deadline cell states the traffic that keeps the liveness loop from ending the session first: an inbound frame inside every HeartBtInt, or a Heartbeat for each TestRequest. A single clock advance past the bound with no traffic is cell N, not a deadline cell.

Every helper call site of §4.7 has its own cell below, and that cell's fix-deleted arm deletes that site's call and no other. These call sites are added after the last review round, so the cells and §4.7's table are what check them.

| Decision | Cell | Fix-deleted arm |
|---|---|---|
| D-537-1, D-537-2 | The lifted cells of `session_537_*`: `SecondTooHigh` turns GREEN on both roles | D-537-2's statement deleted |
| D-537-1, D-537-2 | Per message type, both roles: a second too-high TestRequest, application message, Reject, GapFill without PossDup, and Logon. Each leaves the session Active, sends nothing, and leaves NextNumIn unchanged; the replay through the highest number then completes | the same mutant |
| D-537-1, D-537-2, LogonReceived | **No cell; waived, with the reason.** The state is entered and left inside one handler (the acceptor's Logon handler records `LogonReceived` and then `Active`). The Engine delivers the acceptor's first Logon by itself and starts the read pump after that delivery returns, so no second frame is delivered in that state. A Session driven directly would have to start a second `on_inbound_frame` while the first is suspended. No shipped driver does that: the read pump awaits each delivery before the next. A cell that forced it would measure a path no caller reaches. D-537-2's statement does not read the state. Read | — |
| D-537-1, D-537-2 | A second too-high ResendRequest and Logout: NextNumIn does not move, and the session does not enter `Disconnected` without sending a Logout. What fixpp should send is fixpp#577's question; these two assertions survive either answer | the same mutant |
| D-537-1 | A frame sent during the replay is ignored; the next frame draws a second ResendRequest, and every message is delivered once and in order (`redeliver_poss_dup` off) | — |
| D-537-2, controls | A too-low frame without PossDup is still fatal, and the overflow arm is unchanged | "any error returns success" in place of the exact match: the too-low control is RED |
| D-537-5, the out-of-sequence site | RED first. Mock clock, both roles: a too-high frame before the bound is ignored; one at or after it draws the Logout. Asserted: the Logout and its Text(58), `Disconnected`, **and that the transport is still open**, with a pointer to fixpp#534 so B27 flips that assertion | that site's call deleted: the session is still Active after the frame |
| D-537-5, **V**: the validate gate's site | RED first. Validate gate on. A gap is open; inside every HB the peer sends a dictionary-invalid application frame above NextNumIn. Before the bound each draws a Reject and the session stays Active. The first at or after the bound draws the Logout with its Text(58) and no Reject; `Disconnected`; the transport is still open. Today the Rejects go on without end | that site's call deleted |
| D-537-5, **V2**: the stated consequence | The same stream, with a CompID the CompID guard would refuse. After the bound: the deadline's Logout. This pins the consequence D-537-5 states for a frame that fails validation | as V |
| D-537-5, **G1**: the guards keep their dispositions | Guard. After the bound, a frame above NextNumIn that passes validation: with a wrong CompID, silent `Disconnected` and no Logout; with a SendingTime outside the threshold, a Reject, a Logout with no Text(58), `Disconnected` | the helper called at the top of the arm, ahead of the guards: G1 is RED |
| D-537-5, **P1**: Stage 1's Arm C site | RED first. A gap is open; inside every HB the peer sends a PossDup application frame above NextNumIn with no OrigSendingTime(122). Before the bound: a Reject each, Active. At or after it: the Logout, no Reject, `Disconnected` | that site's call deleted |
| D-537-5, **P2**: Stage 1's unparseable-122 site | RED first. The same, with a 122 that does not parse | that site's call deleted |
| D-537-5, **S1**: the Reset-mode arm, a reset `fromAdmin` rejects | RED first. An application whose `fromAdmin` rejects a SequenceReset. Inside every HB the peer sends a Reset-mode SequenceReset whose MsgSeqNum is above NextNumIn. Before the bound: a Reject each, Active. At or after it: the Reject, then the Logout, `Disconnected` | that site's call deleted |
| D-537-5, **S2**: the Reset-mode arm, a reset that moves nothing | RED first. Inside every HB the peer sends a Reset-mode SequenceReset whose MsgSeqNum is above NextNumIn, with NewSeqNo(36) below NextNumIn, equal to it, and absent, as separate inputs. At or after the bound: the Logout; for the input below NextNumIn the Reject goes out before it | that site's call deleted |
| D-537-5, **S3**: a reset that advances | Guard. After the bound, a Reset-mode SequenceReset whose NewSeqNo is above NextNumIn and whose own MsgSeqNum is above its NewSeqNo: NextNumIn moves, the resend ends, no Logout, Active | the helper's "a resend is outstanding" test deleted: S3 is RED |
| D-537-5, the helper's too-high test | Guard. A resend is outstanding. Inside every HB the peer sends a too-low Heartbeat, which keeps liveness and moves nothing. More than 2 × HB after the last advance, an in-sequence frame arrives that a Reject arm consumes: once at the validate gate (gate on, dictionary-invalid), once at Stage 1's Arm C (PossDup, no 122). It is consumed and Rejected, the session stays Active, and a too-high frame straight after it is ignored, not answered with the Logout. Precondition, as in cell R: the gap is wide enough that the consumed frame does not reach the recorded end, so the resend is still outstanding at the frame that follows | the helper's "above NextNumIn" test deleted: the cell is RED at the first too-low Heartbeat after the bound, which then draws the Logout before the in-sequence frame arrives |
| D-537-5, progress | A replay that takes longer than the bound in total, with NextNumIn advancing inside each window, completes | the refresh deleted |
| D-537-5, second refresh site | The same, where the only advances inside a window are frames that `consume_rejected_seqnum_` consumes | the refresh moved out of `close_filled_resend_gap_` to the in-sequence caller only |
| D-537-5, frame types | After the bound, a too-high Heartbeat ends the session with the Logout. So does a too-high PossDup frame that passes Stage 1 | the out-of-sequence site's helper call moved below the Heartbeat arm: the Heartbeat input is RED. Or that site's call skipped for a frame with PossDupFlag(43)=Y: the PossDup input is RED. The Heartbeat arm comes first in the function, so a call moved below the PossDup arm would remove both exits and separate neither |
| D-537-5, a Logout that cannot be built | RED first, on D-537-7's fixture. CompIDs at the boundary that fixture finds, and NextNumIn left small, so the ResendRequest is built and the Logout with its text is not; the cell first checks both by building. A too-high frame before the bound is ignored (the RED half: today it ends the session). One at or after the bound: nothing is written, `Disconnected`, and `on_inbound_frame` returns success | the `Disconnected` write moved inside the "a Logout was built" branch: the session is still Active after the bound. The design has no separate write for the build failure: `Disconnected` is recorded once after the leaf, whether or not a frame was built |
| D-537-5, the Logout leaf's error | RED first. A gap is open, and the peer sends a too-high frame inside every HB. After the bound, a too-high frame arrives while the application's `toAdmin` throws on the Logout: `Disconnected`, and `on_inbound_frame` returns `app_callback_threw` | the helper returns success whatever the leaf returned |
| D-537-5, off | HeartBtInt = 0, and a Session with no clock: no deadline. A negative HeartBtInt is tested on the helper that computes the bound: no deadline, and no assertion in the debug tree. It is not tested on a session, because of what `run_liveness_loop` does with a negative interval (read, not run): its deadlines are already past, so on its first pass it sends a Heartbeat and a TestRequest and ends the session at the grace check | "off at exactly 0" in place of "≤ 0": the negative cell is RED |
| D-537-5, zero bound | — | a zero bound: `SecondTooHigh` is RED again |
| D-537-5, short GapFill | A GapFill short of the recorded end, then a too-high frame: a new ResendRequest, and a deadline that starts again | — |
| D-537-6, **Q** | Open a gap with a too-high application frame. Advance to E + HB and expect a TestRequest. Deliver a too-high Heartbeat carrying its 112. Advance to just past E + 2 HB, and not to E + 3 HB: the session is still Active (RED today and under v0.2). Deliver a too-high Heartbeat: the Logout, then `Disconnected`. The window matters: at that wake the loop sends a second TestRequest, whose grace ends near E + 3 HB (reasoned from the loop) | D-537-6's predicate deleted: Q is RED at its grace step |
| D-537-6, **N** | The same gap, nothing delivered, the clock advanced past the grace deadline: `Disconnected`, no Logout | guard for condition (b): RED if the dead-peer ending gains a Logout or is lost |
| D-537-6, **R** | Gap, TestRequest at E + HB, a too-high Heartbeat answer, then in-sequence PossDup frames spaced 1.5 HB apart, with every later TestRequest answered by a too-high Heartbeat: no `Disconnected`. **Precondition, stated in the cell:** the frame that opens the gap is far enough above NextNumIn that the in-sequence frames delivered never reach the recorded end. The resend then stays outstanding for the whole cell, and every answering Heartbeat takes the out-of-sequence arm. Without it the resend ends part-way, the next answer takes the too-high arm, and the cell measures D-537-8 | D-537-6's predicate deleted |
| D-537-6, mismatch | RED first. During the gap, a too-high Heartbeat with a 112 other than the pending one: silent `Disconnected` at that frame. Today the frame is dropped and the session ends one grace window later | the mismatch branch replaced by a clear: the session is still Active after the frame |
| D-537-6, bounds of the rule | A too-low Heartbeat does not clear a pending TestRequest. With `validate_sequence_numbers = false`, a too-high Heartbeat does not clear it either; that half names the follow-up of §8 on this knob, which flips it | the gate on an outstanding resend deleted: the knob-off control is RED |
| D-537-8, **T1**: the answer opens the gap, and the peer fills it | RED first. A quiet session. Advance one HB and expect a TestRequest. Deliver a Heartbeat that carries its 112 and a MsgSeqNum above NextNumIn. Expect a ResendRequest. Deliver the replay through that number (a GapFill is enough). Advance past the TestRequest's grace deadline: the session is still Active. Today it is `Disconnected` at that step | D-537-8's disposition deleted: T1 is RED at the grace step |
| D-537-8, **T2**: the answer opens the gap, and the peer does not fill it | RED first. The same opening; the peer then answers each later TestRequest with a too-high Heartbeat and never fills the gap. The session ends with the deadline's Logout at the first frame at or after the bound, not silently at the first grace check | the same mutant |
| D-537-8, **T3**: mismatch | RED first. The gap-opening Heartbeat carries a 112 other than the pending one: silent `Disconnected` at that frame, and nothing is written, no ResendRequest in particular. Today a ResendRequest is written and the session stays Active until the grace check | the mismatch branch replaced by a clear: a ResendRequest is written |
| D-537-8, bounds of the rule | Controls. A gap-opening Heartbeat that carries a 112 while no TestRequest is pending draws the ResendRequest and the session stays Active, as today. One with an empty 112 and a TestRequest pending clears it (the in-sequence rule) and draws the ResendRequest | the "a TestRequest is pending" test deleted: the first control is RED, because a 112 with nothing pending then reads as a mismatch |
| D-537-7 | RED first, and mandatory. Fixture: a directly driven Session on the callback transport, facing hand-built frames, at its real HeartBtInt. Step 1 finds the boundary by building: the longest CompID at which the session's own Logon is still written. Step 2: the peer sends a Reset-mode SequenceReset whose NewSeqNo(36) has enough digits for D-537-7's inequality, then a frame above it. Asserted: `on_inbound_frame` returns `dispatch_aborted`; `Disconnected`; no ResendRequest is written. Today the frame returns success and the session stays Active, awaiting a request that was never sent | the leaf's success return restored: the session is Active with a resend outstanding and nothing written |
| D-537-9, the residual pattern | Pin of the disclosed limitation (R-11). The cell runs with `redeliver_poss_dup` off. A gap is open. Inside every HB the peer sends one frame of D-537-9's members: a too-low Heartbeat; a too-low PossDup frame; a Reset-mode SequenceReset at NextNumIn that moves nothing. The clock is advanced well past the bound and past several grace windows: the session is Active, no Logout and no TestRequest were written. A frame above NextNumIn then draws the deadline's Logout. The cell names the follow-up issue of §8, which flips its first half | none: it pins a limitation, not a fix |

- `Disclosed_L1_*` and `Disclosed_L2_*` pin the fatal outcome today. Their traces change: the frames are ignored, and the session ends by one of §4.6's rows, according to the traffic each cell sends.
- No cell reconnects with the awaiting flag set (fixpp#578, §8).

**C ABI.** One witness per effect, on the raw-peer fixture named in §6.2. §6.2 has one effect per row, or numbers the effects of a row and gives each number its own witness and its own mutant, an edit that turns that witness RED and leaves the row's other witness GREEN. The third #536 witness has a mutant made of two edits, and says so. Each is shown RED on the base tree first. A witness that cannot be made RED there means the effect has no C-ABI reach, and the table row is corrected, not the test.

**Implementation steps that can send the design back.** None of these is an open question of the design; each is a measurement the note cannot make, with what happens if it disagrees.

| Step | When | Sends the design back to the owner if |
|---|---|---|
| The D-537-2 probe (round 1's O-7; still a plan) | before any #537 cell is written; its verdict table goes into the verify record | a test outside `session_537_*`, `Disclosed_L1_*` and `Disclosed_L2_*` changes its verdict in a way §4.6 and §4.7 do not predict |
| D-537-7's boundary | step 1 of its cell | no CompID length exists at which the Logon is written and the ResendRequest cannot be built. D-537-7's reach statement is then wrong; the disposition of the branch is the owner's (`[const §IX.1]`: tested, waived with a rationale, or a filed issue), and "reported uncovered" is not one of them |
| The timelines of §4.6 | cells Q, N, R, T1, T2 | a cell's observed step differs from the timeline. The note is corrected before the fix is kept |
| D-536-3's lifetime argument (read and reasoned) | its lifetime cells, under TSan and ASan | a wake is observed after the wait ended, or a sanitizer reports on the wake |
| §6.2's classifications and reach (reasoned) | each C-ABI witness, RED on the base tree first | a witness cannot be made RED: the row is corrected and the PR description says so |
| The cancelled-read cell on the measured path (D-538-4; its gate and its release point are read, not run) | its RED run on the base tree | the window cannot be hit, or cannot be shown to have been hit, without a diagnostic in library source. The cell is then reported to the owner with what was observed; it is not replaced by a weaker one |

One more condition can send the design back, and it is not a measurement: the stale awaiting flag of fixpp#578. It is stated with that issue, in §8.

**Gates**
- A full `ctest` in `linux-clang-debug`, then ASan, TSan and UBSan trees. TSan matters most for #536.
- An MSVC leg: `session.cpp` changes, and so do the public headers `session.hpp`, `reconnect_fsm.hpp`, `resend_state.hpp`, `message_store.hpp`, `memory_store.hpp`, `clock.hpp`, `fix/c_api/session.h` and `fix/c_api/version.h`. It needs the shared sandbox and its lock.
- Every Tier 1 source-audit job is run locally before the labelled push.
- Coverage is assessed per changed line (`[const §IX.1]`).

## 6. Disclosures and documents

### 6.1 B&L

`spec/behaviors-and-limitations.md`; rows are written at implementation.

- New behaviour rows: the relative lookup (#538); the abort on a store read failure (R-7); a session no longer sweeps the clock, and the logout wait has its own wake (#536); the ignored too-high frame (#537); the deadline; the too-high Heartbeat that answers a TestRequest, during a gap (R-6) and when it opens one (R-9); the ResendRequest that cannot be built (D-537-7).
- **The deadline row states:** conditions (a) and (b) of §4.6 and the ending of each; that after the Logout the transport stays open until the peer closes it or `close()` / `Engine::stop()` runs (fixpp#534); that the deadline is checked only when a frame above the expected number arrives, after 2 × HeartBtInt with no advance; that a Reset-mode SequenceReset that trips it is Rejected first where it would be Rejected today; that no Logout is written when the CompIDs leave no room for its text in the frame buffer, and the session then ends silently; and that it is off when HeartBtInt ≤ 0.
- **The store read failure row (R-7, R-10) states:** the three classes of D-538-4; that a BeginSeqNo(7) of 0 is folded as before and does not end the session; that the abort closes the connection at once except on the acceptor's NextExpectedMsgSeqNum(789) path, where the connection stays open until the establishment deadline; and that a failure that persists aborts the answer again at every reconnect until an operator repairs the log or resets the sequence numbers.
- **New limitation row (R-11, D-537-9).** Wording: while a resend is outstanding, a peer that never fills the gap and that, inside every heartbeat interval, sends only frames whose MsgSeqNum(34) is not above the expected number and that do not advance it keeps the session Active without end. Examples: resent PossDup frames, too-low Heartbeats, a Reset-mode SequenceReset at or below the expected number, a frame at or below the expected number that is Rejected and not consumed. Each such frame counts as inbound traffic, and the unfilled-gap deadline is checked only when a frame above the expected number arrives. Nothing new reaches the application; with `redeliver_poss_dup` on, each resent PossDup application frame is handed to `fromApp` again, as it is when no gap is open. `close()` or `Engine::stop()` ends the session. The row names the follow-up issue (§8).
- **New limitation row (D-536-4):** an explicit `Clock::cancel_sleeps()` on a clock that open sessions share ends their liveness loops, and nothing restarts them.
- **New row for store implementers (D-538-2):** what `retrieve` must return after a reset to a first number other than 1, and for an empty direction.
- **`L-092-1`, `L-092-2`, `L-093-7`.** The last step changes from "the session ends silently at the peer's next new message" to text that is true for every row of §4.6:
  - the peer's new messages are ignored;
  - if the peer keeps sending and answers fixpp's TestRequests, the session ends with a Logout naming the unanswered resend, at the first frame above the expected number that arrives once NextNumIn has not advanced for 2 × HeartBtInt;
  - if the peer answers nothing, or leaves a TestRequest unanswered, the session ends as it does for any dead peer: a silent `Disconnected` at the TestRequest's grace check;
  - if the peer sends only frames that are not above the expected number and move nothing, it does not end (the R-11 row);
  - with HeartBtInt ≤ 0 it does not end.
  - `L-092-1` also says a too-high Heartbeat is dropped silently while the gap is open. It gains: unless it answers a pending TestRequest (D-537-6).
- `L-027-2`: gains one sentence. When the frame that takes the too-high arm is the answer to fixpp's own TestRequest, it also answers that TestRequest (D-537-8).
- `L-013-2`: corrected. No hold queue is implemented; the replay stands in for it.
- `B-424-2`: not edited (R-7).
- **The C-ABI row for the 1.12 clause**, marked BREAKING, with the effects of §6.2 and, as `L-092-12` does for 1.10, the effects that have no C-ABI witness.

### 6.2 The C-ABI declaration: the 1.12 clause (R-8)

- **D-ABI-1 (R-8). C-ABI minor 1.12 is declared BREAKING** under `[const §X.7]`: `FIXPP_C_ABI_VERSION_MINOR` goes to 12, with a `version.h` entry. "The 1.12 clause" is the clause that carries it.
- **Each affected declaration in `include/fix/c_api/session.h` carries the 1.12 clause**, as it carries its 1.10 and 1.11 clauses. The observers those entries name are `fixpp_session_is_established`, `fixpp_session_close`, `fixpp_session_send`, `fixpp_session_register_callback` and `fixpp_session_register_send_callback`.
- **The clause says what the deadline depends on.** Its deadline sentence begins "with a heartbeat interval set", as the 1.10 bullet about faulty frames does, and it says that an interval of 0 turns the deadline off.
- **`fixpp_session_config_set_heartbeat_seconds` gets no clause.** Codex's round-2 C-3 asked for one; it is declined, with the precedent, under `## Gate A`.
- **The 1.10 sentences this batch falsifies are amended in place, marked with the clause's number**, as 1.11 amended 1.10's. They are the 1.10 bullets that end "the session ends at the peer's next new message that is not a Heartbeat": the resent faulty frame, and the faulty SequenceReset in a resend recovery. Those bullets describe the #537 trace. Re-derive the declarations that carry them with `git grep -n "next new message" -- include/fix/c_api/session.h`; `fixpp_session_close`'s 1.10 clause refers to `fixpp_session_is_established`'s instead of repeating them.
- **B41 declares no C-ABI change** (§0): it withdrew the one it had declared, so this batch's number has no merge-order condition. The recipe for the sites that encode the minor is below the table.
- No symbol, signature or error code is added.

Effect → declarations → witness. The fixture is `tests/capi/inbound_frame_dispositions_capi_support.hpp`: a C-ABI engine on a real clock, facing a raw TCP peer that writes hand-built frames. The classifications are reasoned; `.specify/api-contract.md` §11 is the bullet list `[const §X.7]` points at. Each row that has a witness also names its mutant, the edit to the fixed tree that turns that witness RED; the mutants are reasoned from §5's cells. HB is the heartbeat interval; a witness that samples inside a window uses an interval of 2 s, so that half an interval is a margin of 1 s on either side.

| Effect | 1.12 clause on | Class, reason, §11 bullet | C-ABI witness |
|---|---|---|---|
| A second too-high frame while a resend is outstanding is ignored; the session stays established where it ended (D-537-1, D-537-2) | the observers named above, with the amended 1.10 bullets | **Breaking.** A documented result changes: the 1.10 bullets say the session ends there. §11: "changing the meaning of a … C-ABI symbol" | the peer opens a gap and sends a second too-high application message: `fixpp_session_is_established` stays true and the peer reads no second ResendRequest; after the replay the receive callback has each message once, in order. **Mutant:** D-537-2's statement deleted |
| With a heartbeat interval set, an unfilled gap ends with a Logout at the first frame above the expected number that arrives once NextNumIn has not advanced for 2 × HB (D-537-5) | the same clause | **Breaking**, same bullet: it is the new end of the trace those 1.10 bullets describe | one witness per exit of §4.7 that a C session can reach. In each the peer reads a Logout with Text(58), `fixpp_session_is_established` turns false, and the connection is still open (pointer to fixpp#534); each waits at least two intervals of real time. (1) The peer keeps sending too-high application frames and answers each TestRequest. The same where every frame the peer sends after opening the gap is a too-high Heartbeat, and where every such frame is a too-high PossDup application frame with a valid OrigSendingTime(122): those are the Heartbeat exit and the PossDup exit of the same call site. (2) The peer sends, inside every interval, a too-high PossDup application frame with no OrigSendingTime(122): it reads Rejects, then the Logout. The same with a 122 that does not parse. (3) The peer sends, inside every interval, a too-high Reset-mode SequenceReset whose NewSeqNo(36) is below the expected number: it reads Rejects, then a Reject and the Logout. **Mutant, one per witness:** the helper call at that witness's exit deleted (the out-of-sequence site; Stage 1's Arm C site and its unparseable-122 site; the Reset-mode arm's site). For the two other forms of (1): the out-of-sequence call moved below the Heartbeat arm, which turns the Heartbeat form RED and leaves the application-frame form GREEN; and the out-of-sequence site's helper call skipped for a frame with PossDupFlag(43)=Y: the PossDup form is RED, and the Heartbeat form, the application-frame form and witness (2) stay GREEN |
| With CompIDs that leave no room for the Logout's text in the frame buffer, that gap ends at the bound with no Logout: the session ends silently (D-537-5, "If the Logout cannot be built"; R-12) | the same clause | **Breaking**, same bullet: where the 1.10 bullets say the session ends at the peer's next new message, it now ends at the bound | a C session of its own, with CompIDs at the boundary it finds on its own configuration, by the recipe of the D-537-7 row below (the longest CompIDs at which the peer still reads the session's Logon). It cannot be a step of the D-537-7 witness, because each of the two ends its session. The peer opens a gap and reads the ResendRequest, which is built because the expected number is small. It then sends a too-high application frame inside every interval. That traffic keeps fixpp from sending a TestRequest, so the witness does not depend on whether one could be built at these CompIDs. Read from `build_test_request`, `build_logon` and the id `run_liveness_loop` writes: the two frames are built in buffers of the same size, and a TestRequest carries TestReqID(112), which is `TR` plus a counter, where the Logon carries EncryptMethod(98), HeartBtInt(108) and its optional fields; so the TestRequest is the shorter frame until that counter, or the frame's own MsgSeqNum(34), has gained several digits. Asserted: `fixpp_session_is_established` is still true after the second too-high frame, sampled before the bound; this is the half that is RED on the base tree, where that frame ends the session. After at least two intervals with no advance it turns false, the peer has read no Logout, and the connection is still open (pointer to fixpp#534). Control: the same trace at shorter CompIDs is witness (1) of the row above, where the peer reads the Logout. **Mutant:** the `Disconnected` write moved inside the "a Logout was built" branch; `fixpp_session_is_established` then stays true past the bound. Reach is reasoned (D-537-5); this witness is its measurement |
| While a resend is outstanding, a too-high Heartbeat answers fixpp's TestRequest; the session stays up where it ended at the grace check. A mismatched TestReqID(112) on that Heartbeat ends the session at that frame (D-537-6) | the same clause | **Breaking** on the reading 1.10 and 1.11 applied to these observers: when an established session ends changes. Same bullet. No header sentence documented the old result; `L-092-1` documents the dropped Heartbeat | (1) the peer opens a gap at E and goes silent; it reads a TestRequest near E + HB, answers it with a too-high Heartbeat, and sends nothing more. `fixpp_session_is_established` is sampled near E + 2.5 HB: true. **The window is one interval wide:** without the fix the session ends at E + 2 HB, and with it the second TestRequest's grace ends it near E + 3 HB. The peer must stay silent after its answer, because a too-high frame after E + 2 HB draws the deadline's Logout. (2) The same answer with another 112: `fixpp_session_is_established` is false when sampled near E + 1.5 HB; without the fix the session ends at E + 2 HB. **Mutants, one per numbered effect:** for (1), the clear on an empty or matching 112 deleted from the routine D-537-6 and D-537-8 share, the mismatch branch kept: (1) is RED and (2) stays GREEN. For (2), that routine's mismatch branch replaced by a clear: (2) is RED and (1) stays GREEN. Both edits are in the shared routine, so each also turns the same-numbered witness of the row below RED. D-537-6's predicate deleted turns (1) and (2) of this row RED and leaves the row below GREEN |
| A too-high Heartbeat that opens a gap answers fixpp's TestRequest, and the ResendRequest follows; the session survives the recovery where it ended at the grace check. A mismatched 112 on that Heartbeat ends the session at that frame, and no ResendRequest is sent (D-537-8) | the same clause | **Breaking**, on the same reading and bullet as the row above | (1) the peer goes silent, reads a TestRequest at T, answers it with a Heartbeat whose MsgSeqNum is above the expected number, reads a ResendRequest, fills the gap with a GapFill, and sends nothing more. `fixpp_session_is_established` is sampled near T + 1.5 HB: true. The window is one interval wide: without the fix the session ends at T + HB, and with it the next TestRequest's grace ends it near T + 2 HB. (2) The same answer with another 112: the peer reads no ResendRequest, and `fixpp_session_is_established` is false when sampled near T + 0.5 HB. **Mutants, one per numbered effect:** the two edits of the row above, with the same results here: the clear deleted turns (1) RED and leaves (2) GREEN, and the mismatch branch replaced by a clear turns (2) RED and leaves (1) GREEN. D-537-8's call in the too-high arm deleted turns (1) and (2) of this row RED and leaves the row above GREEN |
| A ResendRequest that cannot be built ends the session, where the session stayed established and ignored every later frame (D-537-7) | the same clause | **Breaking**, on the same reading: when an established session ends changes. No header sentence documented the old result | a C session whose CompIDs are at the boundary the witness finds on its own configuration, by D-537-7's recipe (the heartbeat interval and the Logon's optional fields are in the inequality, so the C++ cell's boundary is not reused); the peer sends a Reset-mode SequenceReset that raises the expected number, then a frame above it. The peer reads no ResendRequest; `fixpp_session_is_established` turns false; the connection is closed by fixpp. Reach is reasoned (D-537-7); this witness is its measurement. **Mutant:** the leaf's success return restored |
| A session's close, whether the application called it or the engine ran it when the peer's connection ended, no longer stops the Heartbeats and TestRequests of the other sessions of the engine (D-536-1, the phase-2 sweep) | `fixpp_session_close`, `fixpp_session_is_established` | **A correction.** No document specified the old result, so taken alone it is additive under `[const §X.7]`'s last sentence, and no §11 bullet applies. It is declared in the 1.12 clause because R-8 declares the entry as a whole. `fixpp_session_close`'s return code does not change | two C sessions on one engine, A and B, each facing a raw peer. One loop sends both frame types (`run_liveness_loop`), so a Heartbeat read shows the loop is alive. (1) The application's close: A's peer does not answer A's Logout. `fixpp_session_close(A)` returns; B's peer still reads a Heartbeat more than one interval later. (2) The engine's close: B's peer drops its connection, and the engine then closes B; A's peer still reads a Heartbeat more than one interval later. **Mutants, one per numbered effect:** the phase-2 sweep restored for a graceful close only is RED in (1) and GREEN in (2); restored for a terminal close only, it is RED in (2) and GREEN in (1). The sweep restored as it was, with no test of the mode, turns both RED |
| The peer's answer to a close's Logout on one session no longer stops the Heartbeats and TestRequests of the other sessions (D-536-1, the LogoutSent sweep). At the base only the answer to this engine's own Logout reaches that sweep; the arm for a Logout the peer starts has none | the same two declarations | **A correction**, as the first #536 row | witness (1) of the first #536 row, with A's peer answering the Logout. **Mutant:** the LogoutSent sweep restored; it is RED here and GREEN in both witnesses of the first #536 row, where no answer arrives |
| Another session's close no longer ends this session's logout wait early (D-536-1 and D-536-3) | the same two declarations | **A correction**, as the first #536 row | A's peer does not answer A's Logout. While `fixpp_session_close(A)` is blocked, B's peer drops its connection, and the engine then closes B. A's peer reads end-of-stream, and `fixpp_session_close(A)` returns, no earlier than the logout timeout less a stated margin. RED on the base tree: both happen when B's peer drops. **Mutant, two edits together:** the phase-2 sweep restored, which is the sweep B's close runs, **and** the deadline arm replaced by a `sleep_until` whose `operation_aborted` is caught and read as the end of the wait, as at the base. Either edit alone leaves the witness GREEN, because the wait is defended twice: no sweep reaches it, and `await_deadline` re-arms on a sweep that is not its own. A bare `sleep_until` as the race's arm would be a third defence, because the race's join (asio's `wait_for_one_success`) ignores an arm that throws |
| After a 141=Y exchange that leaves the first outbound number at 2, a message sent is replayed on a ResendRequest where it was gap-filled (D-538-1) | `fixpp_session_send` | **A correction**, as the #536 rows above. A resend is not surfaced to the send callback (`L-019-4`), so the effect is on the wire only | a C initiator with `fixpp_session_config_set_reset_on_logon`; the peer acknowledges with 141=Y; one `fixpp_session_send`; the peer sends a ResendRequest and reads the message with PossDupFlag(43)=Y, not a GapFill. **Mutant:** the absolute position restored in `MemoryStore`, the store a C session runs on |
| A store read failure in a resend answer ends the session where the slot was gap-filled (D-538-4, class 3) | none. The effect is recorded in the `version.h` entry as one with no carrying declaration, as the 1.10 entry records its own | **No reach through the C ABI as shipped.** A C session runs on `MemoryStoreFactory` (`src/capi/config.cpp`). `MemoryStore::retrieve`'s class 3 codes are a reset between the snapshot and a visit, and a failing visitor, and `store_cancelled`, which needs a queued wait for the store's lock; no critical section of `MemoryStore` suspends, so on one serialisation domain its lock is never queued. The walk asks one slot at a time, and no suspension separates the snapshot from that slot's generation check; `CaptureVisitor` neither throws nor aborts. Read. A BeginSeqNo(7) of 0 is class 2 and changes nothing (R-10) | none is possible; the C++ cells stand, and the B&L row says so |

- **Exits of §4.7 with no C-ABI witness, and why.** The validate gate's exit: a C session cannot turn the gate on (`git grep -c validate_inbound_messages -- src/capi` matches nothing; the control `heartbeat_interval` matches). The Reset-mode arm's `fromAdmin`-rejected exit: the C ABI's application, `CapiApplication`, does not override `fromAdmin`. Both have C++ cells (V, S1), and the B&L row lists them as without a C-ABI witness. The `session_already_closed` exit: it needs a frame delivered while the session's sequence-number mutex is drained or cancelled, which happens in the session's own teardown and which a peer cannot arrange. Its helper call is the out-of-sequence site's, which the forms of witness (1) cover.
- **D-537-9 changes nothing a C caller can observe.** The frames of that pattern keep a session established today as well.
- **`[const §X.7]`'s other duties.** BREAKING is marked on each declaration, in the PR description and in the B&L delta. Every in-repository consumer is updated in the same PR; re-derive the sites that encode the minor with `git grep -n -l -i -E 'abi.?minor|VERSION_MINOR' -- include src tests bindings tools` and classify each.
- **The Appendix A controls** (`[const §X.6]`, `[const §X.7]`, `[const §XI.7]`; Appendix A rows: ABI surface, threading / concurrency, session FSM), met as `.specify/495-493-486-dict-reify-copy.md` §0.2 met them in issue mode:
  - Codex Gate A: this loop;
  - `/clarify`: by hand. §0's rulings are its record;
  - `/analyze`: run over this note by the spec-analyzer after Gate A converges and before implementation. Not yet run;
  - the owner's `/plan` sign-off: the owner's approval of the converged note, which serves as the plan. Not yet given.

### 6.3 Hot path (`[const §VIII.5]`)

No violation was found. Read and reasoned:

- The deadline helper's call sites, the Heartbeat rules (D-537-6, D-537-8) and the Logout's text are in slow sub-arms of `on_inbound_active_`: the validate-failed arm, the Reject branches of PossDup Stage 1, the Reset-mode arm, the too-high arm and the out-of-sequence arm. None is on the path of an in-sequence frame that is accepted. The text is built in a stack buffer.
- The refresh is on the in-sequence path, inside `close_filled_resend_gap_`, as a second statement after that function's existing test. It runs only while a resend is outstanding and the session has a clock, and it adds one `steady_now()` read there. **Condition: the refresh stays behind the "a resend is outstanding" test**, so a session with no gap pays the flag read it already pays and nothing more.
- The logout wake is a `std::function` that captures one reference, as `close_grace_wake_` does, on the close path.
- The recovery window's global-heap half is disclosed, not gated (`tests/perf/test_session_recovery_alloc_guard.cpp`, `L-497-1`).

### 6.4 Design documents

Appended at the end of each file, never inserted; frozen `specs/` are not edited.

- `.specify/2d-threading.md`: an erratum on `cancel_sleeps` (who calls it; what it does to a waiter; what it does to open sessions).
- `.specify/2e-msgstore.md`: an erratum on `retrieve` after a reset, and for an empty direction.
- `brain/components/session.md` and `message-store-quiescence.md`: pointers to this note, and the rejected alternatives.
- Header comments at each superseded decision name this note's issues, as `async_mutex.hpp` names its erratum.

## 7. Still open

No decision of the design is waiting for an answer. v0.3's owner item (slot 0) is ruled (R-10), and its reasoned claims are now steps of implementation with a return condition (§5). What remains is outside this note:

1. **The owner's sign-off on the converged note, and `/analyze`** (§6.2, the Appendix A controls). Neither has happened.
2. **Follow-up issues to file at sign-off** (§8): the per-slot walk's start for BeginSeqNo(7) = 0 (R-10); the residual pattern (R-11); and the TestRequest that no Heartbeat can answer with `validate_sequence_numbers` off. The B&L rows and the cells that point at them need the issue numbers. This rewrite edits one file and files nothing.
3. **Not re-verified by either round:** the probe transcripts of §3.4 beyond their verdict tables. The FIX quotations of §4.1 were re-read against the local copies by round 2's triage.

## 8. What this does not decide

- **fixpp#577:** a too-high ResendRequest or Logout is discarded rather than acted on. Unconfirmed. §5's two cells for those frames assert only what survives either answer.
- **fixpp#578:** the awaiting-resend flag across an in-place reconnect. Unconfirmed. If it holds, it interacts with D-537-5: the flag is cleared in `close_filled_resend_gap_` and `apply_inbound_sequence_reset` and set in `on_inbound_active_too_high_` (`git grep -n -E 'enter_awaiting_resend|exit_awaiting_resend' -- src`), and nothing clears it at a reconnect or in the reset unit. The Logon paths and the reset unit would then move NextNumIn with the flag set, and the first too-high frame after a reconnect could meet a time point from the previous connection and draw the "unanswered resend" Logout at once, for a request never sent on that connection. The deadline's own exit leaves the flag set too, as every other end of a session does today. Reasoned; not fixed here.
  - **Who can reach it, read.** Under an `Engine` no `Session` is connected twice: `run_connect_loop` constructs one `Session`, calls `drive_reconnect` once, runs the read pump and returns, and the accept loop constructs a `Session` for each connection it matches (`git grep -n -E 'make_shared<Session>|drive_reconnect\(\)' -- src/session`). So under an `Engine` the flag is never set when a connection starts. Only a `Session` that its own driver reconnects in place, by calling the public `drive_reconnect` again, can reach the interaction.
  - **The condition that can send this design back.** It is not a measurement, so it is here and not in §5's table of implementation steps. No cell reconnects with the flag set. If fixpp#578 is confirmed before this PR merges, D-537-5's interaction with it is re-read.
- **fixpp#534** (B27): `Disconnected` does not close the transport. The deadline's Logout inherits it (D-537-5), and the deadline cells assert the open transport so that B27 flips it. The mismatch endings of D-537-6 and D-537-8 inherit it too: they are silent `Disconnected` writes, like the in-sequence one.
- **fixpp#535** (`test_request_threshold`, B27) lives in the liveness loop. This note does not touch it. While #536 is open, a #534 cell must use one session per clock.
- **B27's surface** is `run_liveness_loop` and the `Disconnected` writers. This note adds a state test and a filter reset before each of the loop's sleeps (D-536-2) and changes no other line of it. D-537-6 and D-537-8 share `pending_test_req_id_` and `unanswered_tr_` with the loop, and no line.
- **The grace exit reads only the pending id**, not "any sound frame since the TestRequest" (the consult's B′). That departs from Session Layer §4.10 precedence 14 for every peer that sends traffic and no Heartbeat, with or without a gap. It is in `run_liveness_loop`. Noticed, not changed (R-6).
- **With `validate_sequence_numbers = false`**, a too-high Heartbeat does not answer a pending TestRequest. Noticed, not changed: D-537-6's gate excludes it, and D-537-8's arm is behind the same knob. A follow-up is to be filed (the "follow-up to file" bullet on this knob, below).
- **A follow-up to file: the per-slot walk's start for BeginSeqNo(7) = 0** (R-10). Today's answer starts with a GapFill whose MsgSeqNum(34) is 0 (D-538-4). Clamping the walk's start to 1, as its empty-store branch does, changes the wire and is not in this batch. Not yet filed.
- **A follow-up to file: the residual pattern** (R-11, D-537-9). A peer that sends only frames that are not too high and move nothing keeps a session with an unfilled gap Active without end. Not yet filed.
- **A follow-up to file: with `validate_sequence_numbers` off, no out-of-sequence Heartbeat can answer a TestRequest.** Read from `on_inbound_active_out_of_sequence_`: a Heartbeat returns at the function's Heartbeat test, before the knob-off arm, so a Heartbeat whose MsgSeqNum(34) is not exactly the expected number never clears `pending_test_req_id_`, whether it is too high or too low. With the knob off such a frame does not move the expected number, so once the peer's numbers are ahead no later Heartbeat can answer, and a quiet line ends the session at the grace check. It is behaviour at the base, outside the three issues, and not a change to this design. Not yet filed.
- **B41** (fixpp#563, #564; its note is on its own branch):
  - B41 edits `Session::send`, `Engine::send`, the callback dispatch scope, and the entry of `Session::close`. It does not touch `run_liveness_loop`, `run_logout_phase1`, `close`'s phase 2, `on_inbound_logout_sent_`, `store_then_emit`, `replay_outbound_range_` or the stores.
  - The only shared function is `Session::close`. The second PR to merge rebases it and re-runs its cells.
  - **B41 has withdrawn the breaking C-ABI change it had declared** (§0). This batch's entry is 1.12, with no merge-order condition.
  - B41 tracks callback depth inside the one dispatch routine every `toAdmin` and `toApp` goes through. This note's new emit, the deadline's Logout, goes through the existing inbound Logout leaf, so it is covered with no action here.
  - B41 relies on one premise that D-537-5 states as a condition: at every inbound arm the outbound number is peeked after any inbound callback of that arm. The helper's Logout peeks inside its leaf, and no inbound callback runs between that peek and the leaf's store call at any call site. In the Reset-mode arm the helper follows `fromAdmin`; v0.3's wording ("ahead of every callback of its arm") no longer describes that arm.
  - B41's ruling on `close` makes one interleaving reachable from an application, and D-536-2's state test before the grace sleep covers it (§3.2, §5).
- **The lock sequence's terminal reset as a general hazard** (§3.5). This note fixes the one member that this change exposes. Whether the lock should restore the caller's filter instead is a `2f` design question and is not decided here.
- Noticed and left alone:
  - `tls_transport.hpp` says its handshake timeout uses `Clock::sleep_until`, and the code uses a private timer;
  - `reconnect_fsm.hpp` declares three timers nothing reads (fixpp#559's class);
  - `FileStoreImpl::write_frame` has no caller;
  - `run_liveness_loop` is off only at a HeartBtInt of exactly 0. With a negative interval its deadlines are already past, so its first pass sends a Heartbeat and a TestRequest and ends the session at the grace check. Read, not run;
  - `pending_test_req_id_` is written by the liveness loop's assign and by the in-sequence Heartbeat's clear, and by nothing else (the recipe is in D-537-6). Nothing clears it when a session ends. What an id left from an earlier connection does after an in-place reconnect was not traced; it is the class of fixpp#578;
  - when the peer's Logout arrives while fixpp's own Logout emit is suspended, `run_logout_phase1` records `LogoutSent` over `Disconnected` and waits for its deadline although the peer has logged out (the trace is in §5's D-536-3 control). Behaviour that exists today.

## Gate A

- Round 1 applied 2026-10-11: Codex P1=4 P2=8 P3=0; Opus post-judging P1=0 P2=8 P3=14; rewrite addresses root causes RC-1, RC-2, RC-3, RC-4. Reviews: research/reviews/codex_536_537_538_1_recovery-liveness_review.md, research/reviews/opus_536_537_538_1_recovery-liveness_triage.md; consult research/reviews/fable_536_537_538_stuck-gap-ending_consult.md.
- Round 2 applied 2026-10-11: Codex P1=3 P2=1 P3=1; Opus post-judging P1=1 P2=3 P3=6; rewrite addresses root causes R2-RC-1, R2-RC-2, R2-RC-3, R2-RC-4. Reviews: research/reviews/codex_536_537_538_2_recovery-liveness_review.md, research/reviews/opus_536_537_538_2_recovery-liveness_triage.md.
- Round 3 applied 2026-10-11: Codex P1=2 P2=1 P3=0; Opus post-judging P1=2 P2=0 P3=7; not converged, and the loop had no rewrite left; an owner-authorised hand-edit, not a rewrite, addresses root causes R3-RC-1, R3-RC-2, R3-RC-3; one targeted Gate A round over the hand-edit is pending. Reviews: research/reviews/codex_536_537_538_3_recovery-liveness_review.md, research/reviews/opus_536_537_538_3_recovery-liveness_triage.md.
- Round 3, second pass of the hand-edit, 2026-10-11 (still v0.5; no review ran between the two passes): §6.2's rows were re-read effect by effect, and every "(R-n)" tag was checked against its ruling. What changed is listed under "Round 3 — second pass of the hand-edit".
- Targeted round (review index 4, over the hand-edited v0.5) applied 2026-10-11: Codex P1=0 P2=0 P3=0; Opus post-judging P1=0 P2=1 P3=5; not converged; the owner asked for a measurement of the one P2 and then ruled (R-13); this rewrite, the first of a new loop instance, addresses root causes TR-RC-1, TR-RC-2; the next review round is pending. Reviews: research/reviews/codex_536_537_538_4_recovery-liveness_review.md, research/reviews/opus_536_537_538_4_recovery-liveness_triage.md; measurement research/probes/b28_538_cancelled_read/.

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

### Round 2 — root causes

Numbered as in round 2's triage, with the prefix R2 to keep them apart from round 1's.

- **R2-RC-1. Each new rule was attached to one arm, and a frame above NextNumIn has several exits.** The deadline and the Heartbeat rule lived only in the out-of-sequence arm. Collapses C-1, O-1, C-5 and O-4. Fix: §4.7, a table of every exit of the Active inbound arm, read from `on_inbound_active_`; the deadline check as one helper with its own "above NextNumIn" and "a resend is outstanding" tests, called at every surviving exit (D-537-5); the Heartbeat rule at the frame that opens the gap (D-537-8, R-9); §4.6 scoped and derived from the table.
- **R2-RC-2. §0 restated rulings in this note's words, and the restatement was narrower or wider than what the owner said.** R-7 had become a set of two codes (C-2); "when a too-high frame arrives" had become "reaches the sequence gate" (C-1); R-6's gate was the consult's predicate (O-1). Fix: §0 quotes the owner, and this note's reading sits under each quotation, marked as a reading.
- **R2-RC-3. Conditionals were left inside §5 and §6.2.** "A cell only if", "depends on §7 item 1", "the assertion follows the owner's answer", "not traced". Collapses C-4, C-2's cell and row, and O-5. Fix: each was resolved by reading. D-537-7 has a mandatory cell built from the builders' arithmetic; slot 0 has an assertion (R-10); the untraced D-536-3 cell has its trace; the LogonReceived cell is waived with its reason; §6.2's store row has one classification. What cannot be settled without running is in §5's table of implementation steps, each with the condition that sends the design back.
- **R2-RC-4. Populations were listed by example** (round 1's RC-4 again). The exits ahead of the gate (C-1), and the paths on which a failed `on_inbound_frame` closes the transport (O-2). Fix: §4.7 was derived by reading the arm from top to bottom; D-537-9's members are its complement; the callers of `on_inbound_frame` were listed by `git grep` and each one's handling of a failed result was read (D-538-4).

### Round 2 — disposition of every finding

Severity is the triage's, after judging.

| Finding | Severity | Disposition | Answered in |
|---|---|---|---|
| C-1 frames that leave before the sequence gate keep an unfilled gap open | P1 | applied (the helper at each surviving exit); the first form of the counter-proposal disagreed | D-537-5; §4.7; §4.6; §5 (V, V2, G1, P1, P2, S1, S2, S3, the too-high-test guard); §6.2 |
| C-2 `store_seqnum_invalid` for slot 0 | P2 | applied per ruling R-10 (three classes; slot 0 keeps the fold); Codex's fix disagreed | §0; D-538-4; §2.3; §4.5; §5; §6.1; §6.2 |
| C-3 a clause on the heartbeat setter | none (Disagree) | disagreed, not applied; the residue is O-6 | `### Round 2 — disagreements`; §6.2 |
| C-4 D-537-7 may land without a test | P2 | applied (a mandatory cell; the reach statement corrected; its own §6.2 row); "remove it if unreachable" disagreed. The same class of branch in D-537-5, a deadline Logout that cannot be built, was found while applying this and has its own cell | D-537-7; D-537-5 ("On expiry"); §5; §6.1; §6.2 |
| C-5 §4.6 is not a table of every ending | P3 | applied | §4.6 (renamed, scoped, one grouped row for the unchanged endings) |
| O-1 the TestRequest answer that opens the gap | P2 | applied per ruling R-9 | §0; D-537-8; §4.5; §4.6; §4.7; §5 (T1, T2, T3, R's precondition); §6.1; §6.2 |
| O-2 two unstated consequences of the R-7 abort | P3 | applied | D-538-4 ("Where this ending closes the transport", "The abort repeats"); §5; §6.1 |
| O-3 "once and in order" with `redeliver_poss_dup` | P3 | applied | §0 (R-2's reading); §4.5; §5 |
| O-4 wording that does not match the code | P3 | applied | D-537-5 ("Refresh sites", the short GapFill); §4.7; §6.3 |
| O-5 cells that cannot be written as specified | P3 | applied | §5 (cell R; the LogonReceived waiver; the D-536-3 control; the slot-0 guard); §6.2 (the D-537-6 witness's window) |
| O-6 §6.2's clause text and reach | P3 | applied | §6.2 (the clause's deadline sentence; the D-537-7 row; the deadline row's witnesses per exit; the exits with no C-ABI witness) |

Round 2's triage also re-checked round 1's dispositions. It found three honoured in form only and one that was still a plan:

- **Round 1's O-1 and RC-1** ("§4.6, one table of every ending"). The table was incomplete. It is now derived from §4.7 and scoped to what it enumerates.
- **Round 1's C-5** (D-537-7). Its cell was conditional. It is mandatory now.
- **Round 1's O-2 and C-12** (R-7). The exception for slot 0 was not in the disposition. R-10 rules it, and D-538-4 names it as class 2.
- **Round 1's O-7** (the probe of the D-537-2 edit). Still a plan: it is the first step of implementation, with its return condition in §5. This rewrite ran nothing.

### Round 2 — disagreements

Codex's fix is not applied for these.

- **C-3 (a clause on `fixpp_session_config_set_heartbeat_seconds`, and on the reset setters).** The precedent R-8 names decides it. 1.10 and 1.11 each changed what the heartbeat interval governs (a faulty frame stopped counting as traffic; every sound frame started to), and each carried the change on the five observers, not on the setter. The setter has no versioned clause, and its documented meaning and its return code do not change, so no bullet of `.specify/api-contract.md` §11 applies to it. Its "0 disables" witness is the C++ cell "D-537-5, off". What is taken from C-3: the clause's deadline sentence says "with a heartbeat interval set" (O-6).
- **C-1's first form** (run the check ahead of the pre-gate arms). It would sit above the BeginString, CompID and SendingTime guards. The helper at each surviving exit is the fix; guard cell G1 goes RED if the check is hoisted.
- **C-2's fix** (abort on `store_seqnum_invalid`). It differs from all three engines and is not what the owner said (R-10).
- **C-4's "remove D-537-7 if unreachable".** The branch is reachable (D-537-7's reach).

### Round 3 — root causes

Numbered as in round 3's triage, with the prefix R3. Round 3 did not converge, and the loop had used both rewrites. The owner chose a hand-edit of this note followed by one targeted Gate A round. The hand-edit applies the remedies the triage wrote out and changes no design.

- **R3-RC-1. One ruling was implemented from its history, not from its words (C-1).** Round 1's triage had offered the owner an option that named `dispatch_aborted`. The option the owner chose says the session ends "with the store's error". D-538-4 implemented the first. Fix: a class 3 result returns the code the store returned, and the Reading under R-7 says so.
  - For the owner's sign-off, as a fact and not a question: round 1's option had named another code; the note now follows the sentence the owner chose.
  - A check that follows from this root cause: each "(R-n)" tag on a decision must be backed by words inside that ruling's quotation, or by a sentence under its Reading. The hand-edit ran it over every tag in the note, in headings, running text and tables; the recipe and what it changed are under "Round 3 — second pass of the hand-edit".
- **R3-RC-2. §6.2 was organised by decision, and R-8 asks for a witness per effect (C-2, and one effect of D-537-5 that the table omitted).** One row held three #536 effects and one witness. Fix: that row is three rows; the deadline that ends with no Logout has its own row and witness; every row of §6.2 that has a witness names the edit that turns it RED; §5's sentence says "per effect".
- **R3-RC-3. A list was closed with "the one exception" or "nothing" after a derivation that stopped early (O-2, C-3, O-3).** This is round 1's RC-4 and round 2's R2-RC-4 again, at low stakes. Fix: the sentences named in those three findings. No table is added.

### Round 3 — disposition of every finding

Severity is the triage's, after judging.

| Finding | Severity | Disposition | Answered in |
|---|---|---|---|
| C-1 R-7 names the store's error, and D-538-4 returned `dispatch_aborted` | P1 | applied as the triage's remedy gives it | §0 (the Reading under R-7); D-538-4 (the class table, "The tail read", "The disposition of class 3"); §5 (the `store_io_failure` slot cell, the tail-read cell, and "class 3 from another code" with its added mutant) |
| C-2 a C-ABI witness per effect | P1 | applied; where the hand-edit differs from the triage's text is listed below | §0 (the Reading under R-8; R-12); §5 ("C ABI"); §6.2 (the three #536 rows; the row for the deadline that ends with no Logout; a mutant on every row that has a witness) |
| C-3 the residual pattern and `redeliver_poss_dup` | P3 (Codex: P2) | applied (the judge's one clause); Codex's control over both settings of the knob is not taken, for the reason below | D-537-9 ("What it looks like"); §4.6 (the residual row); §6.1 (the R-11 row's wording); §5 (the pin cell states that the knob is off) |
| O-1 two new error branches had no named cell | P3 | applied | §5 (#537: "D-537-5, the Logout leaf's error"; #536: "D-536-3, the `try` around the race") |
| O-2 two more in-sequence frames that move nothing | P3 | applied; one member is named more widely than the triage named it, listed below | §4.7 ("An in-sequence frame"); D-537-9 (the last member); §6.1 (the last example of the R-11 row) |
| O-3 `store_cancelled` keeps today's path under an unstated condition | P3 | applied in v0.5; superseded in v0.6 by R-13, which moves the code to class 3 (the targeted round's O-1) | in v0.5: D-538-4 (the class 1 row); §5 ("Implementation steps that can send the design back"). Both are gone in v0.6 |
| O-4 two cells whose mutant or RED step was not what the design produces | P3 | applied | §5 ("D-537-5, a Logout that cannot be built"; "D-537-5, the helper's too-high test") |
| O-5 build the deadline's Logout in a larger buffer | P3 | not applied, by ruling R-12 | §0 (R-12); §6.2 (the row for that effect) |
| O-6 wording | P3 | applied. Its item on "a peer's Logout on one session" is absorbed by the split of the #536 row | the status header; §0 (the numbering note; the Reading under R-8); §6.1; §6.2 (D-ABI-1, the B41 bullet, the table's heading); §8 (the B41 bullet); §4.7 (the `session_already_closed` row); D-537-8 ("Unchanged") |

Round 3's triage also re-checked round 2's dispositions. It found each honoured as written, and one root-cause fix honoured in form only: R2-RC-2 (quote the owner, with the reading underneath). The quotations were in §0, and D-538-4 still implemented the option round 1's triage had worded. That is C-1.

### Round 3 — disagreements

Codex's fix, or the judge's, is not applied as written for these.

- **C-3's severity.** Codex rated it P2. The judge rated it P3, and this note follows the judge. The sentence was incomplete, not false: a resent duplicate is not new; "deaf" is the owner's word inside R-11's quotation; D-537-9's member list already said "dropped, or redelivered"; the behaviour exists at the base and this batch does not change it; the knob is off by default, and a C session cannot set it.
- **C-3, a control over both settings of the knob.** Not taken. The delivery with the knob on exists at the base and this batch leaves it unchanged, so a new cell for it would test no line of this batch. The pin cell states that it runs with the knob off.
- **C-2, a separate TestRequest witness.** Codex asked that the #536 witness also assert TestRequests. Not taken: one loop, `run_liveness_loop`, sends both frame types, so a Heartbeat read after the other session's close shows that the loop is alive.
- **C-2, the third #536 witness.** Codex's witness is kept and its mutant is corrected. Codex asked for a mutant that restores only the sweep or only the raw sleep. Either edit alone leaves the witness GREEN, because the wait is defended twice: D-536-1 removes the sweep, and D-536-3 puts the wait on `await_deadline`, which re-arms on a sweep that is not its own. A bare `sleep_until` as the race's arm would be a third defence, because `operator||` ignores an arm that throws (added in v0.6, from the targeted round's O-2: the "raw sleep" of the mutant is a `sleep_until` whose `operation_aborted` is caught and read as the end of the wait). The mutant is the two edits together, and the row says so.
- **O-5** (build the Logout in 512 bytes, which would remove the branch). Not applied (R-12). The branch stays, with its C++ cell and its own row in §6.2.

Where the hand-edit differs from the triage's text, with the reason:

- **The witness for the deadline that ends with no Logout is a C session of its own**, not a step added to the D-537-7 witness. Each of the two ends its session, so neither can follow the other on one session. The witness asserts that the session is still established after the second too-high frame, because without that half it is GREEN on the base tree, where that frame already ends the session with no Logout. Its traffic is an inbound frame inside every interval and not answers to TestRequests, because at those CompIDs the session's own TestRequest may not be buildable either. It is a witness and not a "no C-ABI witness" entry, because a C session can reach the branch.
- **The third #536 witness's mutant names the phase-2 sweep**, where the triage wrote "a sweep". In that witness B is closed by the engine when its peer drops, and that close runs phase 2. Nothing in the witness answers a Logout, so the LogoutSent sweep is not reached and restoring it would change nothing.
- **§5's "C ABI" sentence allows a row to number its effects.** The triage's sentence was "§6.2 has one effect per row". The D-537-6 and D-537-8 rows each hold two (the answer; the mismatch), numbered, with a witness for each. The triage judged that those rows hold, and they are not split. The second pass gave each number a mutant that the row's other witness does not catch.
- **O-2's member for the validate gate is "a Logon or a SequenceReset"**, where the triage wrote "a GapFill-mode SequenceReset". The validate gate is tested before the Reset-mode arm is selected, and `consume_rejected_seqnum_` tests only the MsgType, so a SequenceReset of either mode that fails validation at NextNumIn is Rejected and not consumed. Read from `on_inbound_active_` and `consume_rejected_seqnum_`.
- **§6.2's other rows gained a mutant each.** The triage's remedy wrote mutants for the three #536 rows only, and its root cause asks that each row name one. The mutants of the other rows are taken from the fix-deleted arms of §5's cells for the same decisions. They are reasoned, like the witnesses.

### Round 3 — second pass of the hand-edit

Two checks, each for the class behind one of round 3's two P1 findings. No design changed.

**§6.2, effect by effect.** For each row: list the effects its first column states; for each effect, name one edit to the fixed tree that removes that effect and no other; then ask whether a witness of the row turns RED under that edit while the row's other witnesses stay GREEN. A row passes when every effect has such an edit and the row names it.

- **The D-537-6 and D-537-8 rows** each state two effects (the answer; the mismatch) and already had a witness for each. The mutants they named did not separate the two: deleting the predicate or the call removes both effects at once. Each row now names one edit per numbered effect, and says which edit turns both witnesses RED. The rows are not split.
- **The first #536 row** stated one effect with two triggers, a close the application calls and a close the engine runs when a peer's connection ends, and witnessed the first only. A sweep restored for a terminal close alone would have left every C-ABI witness GREEN: the third #536 witness is defended by `await_deadline` as well. The row now has a witness per trigger.
- **The D-537-5 row** promised a witness per exit of §4.7 that a C session can reach, and witness (1) covered only the application-frame exit of the exits that share the out-of-sequence call site. It now has a form for the Heartbeat exit and one for the PossDup exit, each with the mutant of §5's "frame types" cell. The `session_already_closed` exit joins the list of exits with no C-ABI witness, with the reason.
- The other rows state one effect each and are unchanged.

**Every "(R-n)" tag against its ruling.** List every mention of a ruling with `git grep -n -E 'R-[0-9]+' -- .specify/536-537-538-recovery-liveness.md`, and read each hit outside §0, whether it stands in parentheses or in a sentence. For each, the statement it is attached to must be backed by words inside that ruling's quotation in §0, by a sentence under its Reading, or, for a statement that an option was not chosen, by its "Not chosen" line. The entries of this `## Gate A` log are history and were read, not corrected.

- **Sentences added to §0 so that a tag is backed:**
  - R-7's Reading: what "as the truncation arm does today" names, now that class 3 returns another code than that arm; and that the rule holds for every store.
  - R-10's Reading: which codes "an argument refusal" covers.
  - R-9: a "Not chosen" line, for §4.4's "Not chosen (R-9)".
  - R-6's Reading: that the footprint words in the quotation describe the Heartbeat rule and not the batch; and where "off at HeartBtInt = 0" comes from.
  - R-5: its second sentence is marked as a Reading. It was this note's, and unmarked.
- **Tags backed by a Reading that the owner's words do not themselves carry.** In v0.5 they were left as they were and named for the owner; v0.6 changes the first:
  - `store_cancelled` and today's fold, which v0.5 held in class 1 on a Reading under R-7. This one is no longer left as it was: the targeted round's O-1 showed that the Reading did not follow from the owner's words and that the condition attached to it fails. R-13 rules it, the Reading under R-7 is corrected, and the code is in class 3;
  - with `validate_sequence_numbers` off, a too-high Heartbeat does not answer a pending TestRequest (R-9's Reading: "the knob-off exclusion stays"). R-6's quotation has no such limit;
  - the deadline is off for a negative HeartBtInt and for a session with no clock. R-6's Reading says that neither the owner's words nor the consult names those two cases; round 1's C-7 asked for the negative one.
- **R-1, R-3 and R-4 are recorded in §0 without a quotation.** A tag to one of them can be checked against this note's wording only.
- **"Owner ruling R-1 on fixpp#516"** in §4.1 and §4.4 is a ruling on another issue. It is not this note's R-1.

### Targeted round (review index 4) — root causes

Numbered as in that round's triage, with the prefix TR for "targeted round". The round ran over the hand-edited v0.5 and did not converge: one P2 stood. The owner asked for a measurement of it and then ruled (R-13). This rewrite applies the ruling and the round's P3 corrections; it is the first rewrite of a new loop instance.

- **TR-RC-1. A design question was left as an implementation step (O-1).** Round 3's O-3 had turned "is the fold of a cancelled read harmless?" into a step of §5 with a return condition. The question could be answered by reading, and its answer decides which class `store_cancelled` belongs to. Fix: the question is answered in the note. The triage traced it by reading; the measurement confirmed the trace, with the two corrections listed under this round's disagreements; R-13 rules the outcome; and D-538-4 states the reading, the measurement and the ruling's effect.
  - A check that follows from this root cause: every row of §5's "Implementation steps that can send the design back" must be something only a run can settle. This rewrite ran it over the rows that remain:
    - the D-537-2 probe: the tests that pin the old outcome are found by running the selection;
    - D-537-7's boundary: that such a CompID length exists is already read (D-537-7's reach), because a longer CompID adds the same bytes to both frames; the run finds the length and checks that reading;
    - the timelines of §4.6: read in this note and re-derived from `run_liveness_loop` by the triage; the cells observe them;
    - D-536-3's lifetime argument: its return condition is a sanitizer report;
    - §6.2's classifications and reach: each needs a witness shown RED on the base tree;
    - the stale awaiting flag: this row was not a measurement. It waits on fixpp#578, which R-4 keeps outside this batch, so it is moved to that issue's bullet in §8, with what a reading settles: under an `Engine` no `Session` is connected twice, and only a `Session` that its own driver reconnects in place can reach the interaction. Whether fixpp#578 holds for such a driver stays that issue's question. No decision of this note changes;
    - the cancelled-read cell on the measured path: a row added by this rewrite. Its gate is read, and only a run shows whether the window is hit.
- **TR-RC-2. Mutants were reasoned from the fix's intent, not from what the edited code does (O-2, O-3, O-4).** Two of the three turn on one fact: asio's `operator||` ignores an arm that ends with an exception, and rethrows only when both arms threw. Fix: the three rewordings. §5's rule that a mutant which stays GREEN is reported against its cell stays as the backstop.
  - For the owner, as a fact about round 3: the guard cell for the `try` in `run_logout_phase1` was the text of round 3's triage (its O-1, second fix), which the hand-edit applied as written. Its mutant was wrong: removing the `try` terminates nothing.

### Targeted round (review index 4) — disposition of every finding

Severity is the triage's, after judging. Codex reported no finding, so every row is the judge's.

| Finding | Severity | Disposition | Answered in |
|---|---|---|---|
| O-1 class 1 keeps the fold for `store_cancelled`, and the condition said to make that harmless does not hold | P2 | applied per ruling R-13, as the triage's remedy gives it; where the text differs is listed below | the status header; §0 (R-13; the Readings under R-7 and R-10; R-3); D-538-4 (the heading, the class table, "Why `store_cancelled` aborts", the table of what each shipped `retrieve` returns, "The tail read", "What follows a cancelled read"); §2.3; §5 (the class 1 controls; the new cells "D-538-4, a cancelled read" and "D-538-4, a cancelled read on the measured path"; the implementation step removed, and one added for the second cell's gate); §6.2 (the last row) |
| O-2 the mutant "put back on a raw `sleep_until`" stays GREEN when the bare sleep is the race's arm | P3 | applied | §5 (the `LogoutGracefulCancelMidSleep` row); §6.2 (the third #536 row); "Round 3 — disagreements" (the third #536 witness) |
| O-3 the cell "D-536-3, the `try` around the race" has a mutant that stays GREEN | P3 | applied; the cell is also relabelled, listed below | D-536-3 ("The race sits inside a `try`"); §5 (the cell) |
| O-4 in the D-537-5 row, the PossDup form's mutant also turns the Heartbeat form RED | P3 | applied; §5's cell for the same two frame types is aligned, listed below | §6.2 (the D-537-5 row); §5 ("D-537-5, frame types") |
| O-5 the knob-off TestRequest gap is noticed with no follow-up | P3 | applied: a follow-up to file at sign-off, no change to the design | §8; §7 item 2 |
| O-6 wording and leftovers | P3 | applied. Its item on the class table's description of `store_cancelled` is absorbed by O-1. Its item on the walk's header comment is for implementation: that comment is code, and it changes with D-538-4 | §7 (the heading); §4.6 and §4.7 (cells T1, T2 and T3); §6.2 (the row for the deadline that ends with no Logout: the "not traced" remark is replaced by what the builders show) |

The triage also re-checked the hand-edit against round 3's two P1 findings and found both closed, and it found the hand-editor's three departures from round 3's text true of the source.

### Targeted round (review index 4) — disagreements

None. Codex reported no finding, and every finding of the judge is applied.

Where this rewrite differs from the triage's text, with the reason:

- **The evidence for O-1 is measured, where the triage wrote "Read, not run".** The owner asked for the measurement before ruling. D-538-4 states what was measured, its limits, and where the probe is.
- **The GapFill's NewSeqNo(36).** The triage's trace says the walk returns a GapFill that ends at `peek_outbound()`. That is so only for EndSeqNo(16) = 0. For an explicit EndSeqNo the value is the requested end plus one. Read from `replay_outbound_range_`, and it is what the probe's peer read.
- **"Before the transport is closed".** The triage's trace places the walk's resumption before the step of `Engine::stop` that closes the transport. In the measurement the walk resumed and started its write earlier than that trace needs: before the step that cancels had finished on the control strand. Both support "before the transport is closed", and that is what D-538-4 says.
- **O-3's cell is no longer called a guard.** The triage changed only its mutant column. §5 defines a guard cell as one that exists for its mutant, and this cell has none, so it is labelled a control that runs the `catch` branch, as the D-536-3 lifetime control is labelled. Its assertion "the process does not terminate" is dropped, because nothing on that trace could terminate it. D-536-3 says what the `try` is for.
- **O-4 also changes §5's "D-537-5, frame types" cell**, which the triage did not name. §6.2 takes its mutants from §5's cells, so the two would otherwise disagree.
- **Tags and one line that the ruling brings.** R-3's sentence and D-538-4's heading name R-13, and §2.3 has a "Not chosen (R-13)" line. The triage's remedy was written before the ruling.
- **A second cell, on the measured path.** The triage's remedy has one cell, on a wrapper that returns `store_cancelled`. The defect was measured on a real `FileStore` lock cancelled by `Engine::stop`, so §5 also has a cell on that fixture, with a control arm and a condition that it shows its window was hit. Its gate could not use the probe's diagnostics, which were in library source; it is a wrapper's record, read and not run, with an implementation step for the case where the window cannot be hit.
- **What follows a cancelled read is stated in D-538-4**, which the triage's remedy did not ask for: under an `Engine` the session is being stopped anyway, the 789 path is superseded by the same stop, and the abort does not repeat.
