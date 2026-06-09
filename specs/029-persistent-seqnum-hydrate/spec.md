# Feature Specification: Persistent seqnum hydrate — resume both sequence-number counters from the store across restart/reconnect

**Feature Branch**: `029-persistent-seqnum-hydrate`
**Created**: 2026-06-09
**Status**: Draft
**Input**: User description: "Close the inbound store-persistence gap (T034) and add bidirectional hydrate-on-open so a persistent-store-backed FIX session resumes both sequence-number counters from disk across restart/reconnect. Persistence spine for the parked 025 RefreshOnLogon capstone."

## Scope & relationship to 025 (RefreshOnLogon)

This feature is the **persistence spine**. It makes the persistent message store the *true, durable source of truth for both sequence-number directions* and resumes the in-memory counters from it when a session opens. It does **not** add any user-facing config knob.

The parked **025 RefreshOnLogon** (S-018) is the *separate follow-on slice* that rides on this spine: a per-logon *re-hydrate* knob for the reconnect-within-one-process case. RefreshOnLogon, the `refresh_on_logon` config key, and the reset-knob precedence / `141` interaction are **OUT OF SCOPE here**. This slice unblocks it by closing the dependency the parked 025 bundle named "T034".

## Two coupled mechanisms

1. **Inbound store-write (closes "T034").** Today the running session persists only the **outbound** counter (each send writes the frame, advancing the store's `next_outbound`); the inbound store-write was deferred and never wired, so the store's `next_inbound` never tracks the live inbound stream. This slice persists each accepted inbound message's sequence advance **after** the application callback returns (D-2, deliver-then-persist / at-least-once — matching QuickFIX), so the durable `next_inbound` follows the live inbound stream; a crash mid-delivery re-delivers on restart (deduped by PossDup/ResendRequest) rather than silently skipping a message.
2. **Hydrate-on-open (store → in-memory counters).** When a session opens (and on the engine-managed reconnect path), read both persisted counters back from the store and load them into the in-memory sequence-number state, so the session resumes from disk rather than from 1.

## Clarifications

### Session 2026-06-09

- Q: Should hydrate-on-open be always-on when a persistent store is configured, or gated behind a config flag? → A: **Always-on (QuickFIX-faithful).** When a persistent (non-memory/non-null) store is configured, the session always loads both counters from it at open; the store is the source of truth (QFcpp `FileStore::populateCache()` hydrates the cache from disk at construction and is read through on every access — there is no enable-flag). The memory/null store yields `1` ⇒ byte-identical no-op (FR-005 holds). No new config key.
- Q: On a crash partway through processing an inbound message, which durability semantic should the inbound counter persist follow? → A: **Deliver-then-persist / at-least-once.** Persist the inbound seqnum advance *after* the application callback (`fromApp`/`fromAdmin`) returns — matching QuickFIX-cpp/J (`verify()`→`fromCallback`, then `incrNextTargetMsgSeqNum()`). A crash mid-delivery re-delivers on restart, deduped by standard PossDup/ResendRequest; a message is never silently skipped. This **supersedes** the aspirational `I-3` "store-before-deliver" comment at `session.cpp:1517`, which was never wired; `/speckit-plan` must reconcile/retire that comment.
- Q: When the inbound store-write itself fails (I/O error) mid-session, what disposition? → A: **Fatal-disconnect.** Mirror QFcpp (`setNextTargetMsgSeqNum` throws `IOException` out of the inbound path → drop). On reconnect, hydrate reads the last durable value and the 013 ResendRequest sub-protocol resyncs any gap. Avoids the New-2 swallowed-failure desync/regression class.

## Resolved design decisions

- **D-1 (hydrate trigger)** — **always-on** when a persistent store is configured; default-off byte-identity (FR-005) holds for the memory/null store.
- **D-2 (crash ordering)** — **deliver-then-persist** (at-least-once); supersedes the unwired `I-3` store-before-deliver comment.
- **D-3 (inbound persist failure)** — **fatal-disconnect**; reuses the existing store-failure disposition (no new error slot).

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Restarted session resumes its OUTBOUND numbering from disk (Priority: P1)

An operator runs a FIX session backed by a persistent message store. The process restarts (crash, deploy, planned bounce) while the counterparty's session day has not rolled. On the next open, the session must continue sending from the outbound sequence number it had persisted before the restart — not start over at 1 — so the counterparty (still expecting a higher number) does not see a backwards/duplicate outbound stream and reject or flood with resend requests.

**Why this priority**: This is the most visible half of cross-restart continuity and the data is already on disk (the outbound counter is persisted on every send); only the read-back at open was missing. It is independently testable and a viable MVP on its own.

**Independent Test**: Pre-seed a persistent store to `next_outbound = 42`, construct a fresh session over that store, open it, and assert the first outbound message (e.g., the initiator Logon) carries `34 = 42`, not `34 = 1`.

**Acceptance Scenarios**:

1. **Given** a persistent store holding `next_outbound = 42`, **When** a session opens over it, **Then** the in-memory next-outbound is loaded from the store before any outbound message's sequence number is sampled, and the first outbound message carries `34 = 42`.
2. **Given** a persistent store holding `next_outbound = 42`, **When** the session sends three messages, restarts, and re-opens over the same store, **Then** it resumes sending from where it left off (no backwards jump, no duplicate outbound number).

---

### User Story 2 - Inbound position is durably tracked and resumed from disk (Priority: P1)

The same operator needs the **inbound** expected sequence number to also survive a restart. Today the store's inbound counter is never advanced by a running session, so after a restart the session expects inbound `1` again while the counterparty continues from a high number — forcing an avoidable recovery storm or a fatal too-low/too-high disconnect on the first inbound message. The session must durably record how far the inbound stream has advanced and resume the inbound expectation from disk.

**Why this priority**: This is the other half of the continuity story and the actual gap this slice exists to close ("T034"). Without it, restart continuity is one-directional and the inbound side silently regresses.

**Independent Test**: Drive five inbound messages through an established session backed by a persistent store, restart, re-open over the same store, and assert the session's next expected inbound is `6` (not `1`) — i.e., the inbound advance was persisted and resumed.

**Acceptance Scenarios**:

1. **Given** an established session backed by a persistent store, **When** an inbound message is accepted in sequence, **Then** the inbound sequence advance is durably recorded **after** the application callback returns (deliver-then-persist / at-least-once, D-2), so a crash mid-delivery re-delivers the message on restart rather than silently skipping it.
2. **Given** a persistent store whose inbound counter reflects five accepted inbound messages, **When** the session restarts and re-opens, **Then** the in-memory next-expected-inbound resumes at the persisted value (`6`), not `1`.
3. **Given** a restarted session resumed at inbound `6`, **When** the counterparty's first post-restart message arrives ahead of the local expectation (a gap), **Then** — **provided** `enable_next_expected_msg_seq_num` (789) is enabled **or** the counterparty's Logon announces a reset (`141=Y`) — the existing recovery sub-protocol (ResendRequest / SequenceReset / behind-side tolerance) resynchronises the inbound stream rather than the session fataling. NOTE: the first post-restart frame is the peer **Logon**, validated by the Logon-path seqnum gate, which **fatals on too-high with the knob off** (it has no ResendRequest arm; only the steady-state Active path does). So a knob-off restart-after-GapFill whose peer Logon is higher than the resumed lower bound can fatal on the Logon and recover by reconnect (L-029-1) — lower-bound recovery without a fatal requires the 789-or-reset precondition.

---

### User Story 3 - No persistent store ⇒ byte-identical, no surprises (Priority: P2)

An operator running with an in-memory (non-persistent) store, a null store, or a fresh first run must see exactly today's behavior: counters start at 1, no added store I/O on the open path, byte-identical frames.

**Why this priority**: Guards the large existing surface of memory-store sessions and the default path; secondary to the resume behavior but a hard non-regression floor.

**Independent Test**: Open a session over a memory/null store and assert (a) counters start at 1, (b) no store read occurs on the open path, (c) produced frames are byte-identical to the pre-feature baseline.

**Acceptance Scenarios**:

1. **Given** a memory/null store (nothing persisted), **When** a session opens, **Then** the counters start at `1` and there is no added store read or allocation on the open path.
2. **Given** a persistent store that has never been written (empty), **When** a session opens, **Then** hydrate is a no-op leaving counters at `1` — not an error.

---

### Edge Cases

- **Empty / never-written persistent store**: hydrate reads back the construction-default (1) — no-op, no error.
- **Inbound store-write failure** (mid-session): **fatal → disconnect** (D-3); the durable counter stays at the last successfully-persisted value (a lower bound — INV-H1, never *ahead* of true). Reconnect re-hydrates that value and 013 recovery resyncs; the in-flight message whose persist failed may be replayed (at-least-once, INV-H2), never skipped.
- **Crash mid-delivery** (between app callback and inbound persist): at-least-once (D-2) — on restart the message is re-delivered (counter not yet advanced), deduped by the standard PossDup/ResendRequest path; never silently skipped.
- **Store-read failure on the hydrate path**: fatal → disconnect, reusing the existing store-failure disposition (no new error slot).
- **Counterparty ahead after restart** (inbound gap): the post-restart peer **Logon** is validated by the **Logon-path** seqnum gate (acceptor `NotConnected` / initiator `LogonSent`), which fatals on too-high **unless `enable_next_expected_msg_seq_num` (789) is enabled** — there is no ResendRequest arm on the Logon path (only the steady-state Active path enters AwaitingResend). So lower-bound recovery without a fatal requires **either** the 789 knob **or** a peer reset Logon; otherwise a knob-off restart-after-GapFill can fatal on the Logon and recover by reconnect (documented L-029-1). The hydrate must leave the inbound expectation recoverable on the 789/reset path and must not pre-empt the received-reset arm (next bullet).
- **Counterparty resets (`141=Y`) on the post-restart Logon**: the existing 013/024 received-reset handling still owns this; hydrate must not mask or reorder it. Specifically, the hydrated inbound seed is **withheld** on a reset Logon so `check_inbound` sees the in-sequence construction value and the existing reset arm owns the post-state — the resumed `next_inbound` MUST NOT pre-empt a peer reset Logon into a too-low fatal (FR-010).
- **Outbound store-write failure desync** (the New-2 case): the existing outbound store write is logged-then-proceed (I-07, 008/024), so a swallowed outbound store-failure leaves the persisted outbound counter behind the in-memory counter. Outbound resume is therefore **only as fresh as the last successful outbound store write** (L-029-2); a later hydrate seeds the last durably-written outbound value, not necessarily the true last-sent one. Pulling outbound→fatal into scope (which would close this gap) is **out of scope** here — it re-opens the 008/024 I-07 policy.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The system MUST durably record each accepted inbound message's sequence advance to the persistent message store, so the store's inbound counter tracks the live inbound stream (closing the unwired inbound store-write).
- **FR-002**: The inbound store-write MUST occur **after** the application callback (`fromApp`/`fromAdmin`) for that message returns (deliver-then-persist / at-least-once, **D-2**), so a crash mid-delivery re-delivers the message on restart (recovered by the standard PossDup/ResendRequest path) rather than silently skipping it.
- **FR-003**: When a session opens, the system MUST load both persisted sequence-number counters (inbound and outbound) from the store into the in-memory sequence-number state, so the session resumes from disk rather than from 1. This hydrate is **always-on** whenever a persistent store is configured (**D-1**); no config flag gates it.
- **FR-004**: The hydrate MUST complete before any outbound message's sequence number is sampled and before the inbound validation gate runs, so resumed sessions emit and validate against the persisted positions.
- **FR-005**: With a **non-persistent** store (memory store or null store), the system MUST behave byte-for-byte identically to current behavior on the open path — counters start at `1`, **no added store read, no added allocation**. Because a configured memory store is non-null and its counter read posts/locks/allocates, the no-read guarantee is keyed to a non-persistent discriminator captured at open (not to `store_ == nullptr`), so a memory store skips the hydrate read entirely. (An empty/first-run **persistent** store performs the open-time read and hydrates a no-op `1` — see FR-003; the byte-identity guarantee for that case is at the WIRE level with the one-time open-time read accepted, not zero reads.)
- **FR-006**: A store-read failure during hydrate MUST be treated as a fatal session error → disconnect, reusing the existing store-failure disposition (no new error slot).
- **FR-007**: An inbound store-write failure MUST be **fatal → disconnect** (**D-3**), reusing the existing store-failure disposition (no new error slot). On reconnect, hydrate reads the last successfully-persisted value (a lower bound — INV-H1, never *ahead* of the true position) and the 013 recovery sub-protocol resyncs any gap. The in-flight message whose persist failed MAY be replayed (at-least-once, INV-H2 — deduped by PossDup); the guarantee is that the durable counter never advances **ahead** of true, so no inbound is ever **skipped** (NOT "no replay" — replay is the expected at-least-once behaviour).
- **FR-008**: The system MUST add a production path to load the in-memory counters from given values — the in-memory sequence-number state today exposes an inbound setter and a reset-to-one but **no production outbound setter** (only a test hook). Hydrate uses this new path.
- **FR-009**: When a resumed session's counterparty is ahead, hydrate MUST leave the inbound expectation in a state the recovery path can act on (it MUST NOT pre-empt a peer reset Logon into a too-low fatal — FR-010). Recovery of a too-high peer **Logon** without a fatal is bounded by the Logon-path gate: it succeeds via the existing 013/027 sub-protocol (behind-side tolerance / ResendRequest) **only when `enable_next_expected_msg_seq_num` is enabled or the peer Logon announces a reset**. With the knob off and a too-high peer Logon, the session fatals on the Logon gate and recovers by reconnect+hydrate (L-029-1) — this is a documented, recovery-correct case, not a silent skip.
- **FR-010**: The existing inbound `ResetSeqNumFlag(141)` handling and the 024 acceptor cause-dependent reset split MUST be preserved — hydrate establishes the resume baseline but MUST NOT mask, reorder, or suppress the received-reset path.
- **FR-011**: The feature MUST NOT introduce a new wire field, a new error slot, codegen, or a C-ABI surface. It builds on the existing store interface (which already carries a direction parameter) and the existing 013 recovery path.
- **FR-012**: The `refresh_on_logon` config knob and per-logon re-hydrate semantics (025 / S-018) are explicitly OUT OF SCOPE; this slice provides only open-time hydrate + inbound persistence.

### Key Entities *(include if feature involves data)*

- **Persistent message store**: the durable source of truth for both `next_inbound` and `next_outbound`; advanced on each persisted send (existing) and each persisted inbound accept (new, FR-001).
- **In-memory sequence-number state**: the session's working inbound/outbound counters; loaded from the store at open (FR-003) and advanced during the live session.
- **Hydrate operation (store → in-memory)**: the new boundary operation reading both persisted counters and loading them into the in-memory state.
- **Inbound persistence point**: the new deliver-then-persist write (after the app callback) that advances the durable inbound counter (FR-001/FR-002).

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: After a restart over a persistent store whose **last successfully written** `next_outbound = N`, the session's first outbound message carries `34 = N` — verified by an automated test. (Outbound resume is only as fresh as the last successful outbound store write; a prior swallowed I-07 outbound-write failure means `N` is the last durable value, not necessarily the last sampled one — L-029-2.)
- **SC-002**: After N accepted inbound messages, a restart, and re-open over the same persistent store, the session's next expected inbound resumes at `N + 1` (not `1`) — verified by an automated test that drives inbound traffic, restarts, and reopens.
- **SC-003**: With a memory store OR a null store (both ⇒ the non-persistent discriminator captured at open), 100% of existing session/seqnum/store regression witnesses remain green and produced frames are byte-identical to the pre-feature baseline; the open path performs zero added store reads and zero added allocations (verified under the existing no-heap/allocation discipline — the memory store is discriminated non-persistent and its counter read is skipped, not paid).
- **SC-004**: A resumed session whose counterparty is ahead recovers without a fatal **when `enable_next_expected_msg_seq_num` is enabled or the peer Logon announces a reset** (behind-side tolerance / ResendRequest / SequenceReset resyncs) — verified by an automated test exercising the post-restart gap path with the knob on. The complementary **knob-off** case (peer Logon too-high → fatal on the Logon gate → recover by reconnect, L-029-1) is verified as its own documented disposition, NOT asserted to recover-in-place. (The witness MUST assert the actual precondition; it MUST NOT claim "recovers via ResendRequest" for the knob-off path — [[feedback_witness_asserts_named_postcondition_not_proxy]].)
- **SC-005**: A store-read failure on the hydrate path produces a fatal disconnect and leaves the in-memory state unchanged (no partial seed) — verified by an automated test (split: first-read-fails and second-read-fails-after-first-succeeds, proving no partial seed).
- **SC-006**: An inbound store-write failure produces a fatal disconnect (D-3); the durable counter remains the last successfully-persisted value (a lower bound — never *ahead* of true, INV-H1) and a subsequent reconnect/hydrate resumes from it — verified by an automated test injecting an inbound persist failure that asserts (a) the fatal Disconnected transition and (b) the durable counter is the last-persisted lower bound. The test MUST NOT assert "manager unchanged" — the in-memory `check_inbound` advanced before delivery, so the in-flight message may be replayed on reconnect (at-least-once), never skipped.

## Assumptions

- **Deliver-then-persist ordering (D-2)**: the inbound persist follows the application callback (at-least-once), matching QuickFIX-cpp/J. This supersedes the unwired `I-3` "store-before-deliver" comment at `session.cpp:1517`; `/speckit-plan` reconciles/retires that comment.
- **Hydrate-on-open is always-on (D-1)**: QuickFIX-cpp / QuickFIX-J construct the in-memory counters *from* the store, making the store the source of truth; fixpp adopts the always-on model (load from the store at open whenever a persistent store is configured). The default-off byte-identity floor (FR-005) holds because a memory/null store yields 1.
- **Inbound store-write failure is fatal-disconnect (D-3)**: a dropped session reconnects and hydrates the last durable value, with 013 recovery resyncing any gap, avoiding the New-2 regression class; confirmed against the reference engines (`setNextTargetMsgSeqNum` throws `IOException`).
- **No new error slot / wire field / codegen / C-ABI**; store-failure paths reuse the existing store-failure disposition.
- **Builds on 008** (FileStore — outbound persistence already wired; the store interface already carries a direction parameter and the FileStore already advances the inbound counter on an inbound write) **and 013** (ResendRequest/SequenceReset recovery sub-protocol for the post-restart gap).
- **Catalogue**: this discharges the inbound half of the persistence story; add net-new catalogue row **S-042** and cross-link **S-018**. The deferred limitation marker for RefreshOnLogon is **L-024-1** ("RefreshOnLogon (S-018) is NOT implemented", `spec/behaviors-and-limitations.md:579`) — not L-025-1 (which does not exist). 029 is the persistence *spine* that unblocks 025; it does not itself retire L-024-1 (RefreshOnLogon remains unimplemented until 025 ships) — see the §VI delta in plan.md.
- **027/789 interaction**: a hydrated initiator with `enable_next_expected_msg_seq_num` enabled advertises `789 = <hydrated next_inbound>` (the true resumed inbound position); `seqnums_at_one` is false on a resumed session, so no spurious `141=Y` is emitted. This is the lower-bound recovery enabler (FR-009).
- **025 RefreshOnLogon** (the per-logon re-hydrate knob) is a separate follow-on slice and is not implemented here.

## Normative References

- **[FIX-SL §4.1] Sequence numbers** — monotonic per-direction `MsgSeqNum(34)` semantics; too-low is a fatal protocol error. Grounds INV-H1 (the persisted counter is a monotonic lower bound) and the too-low fatal disposition the Logon gate enforces.
- **[FIX-SL §4.3.12] Synchronization after a successful logon** — the post-Logon resynchronisation handshake; grounds the hydrate-then-Logon ordering and the 789 / received-reset interaction (FR-004/FR-009/FR-010).
- **[FIX-SL §4.8] Message recovery — ResendRequest** and **[FIX-SL §4.8.x] SequenceReset (GapFill / Reset)** — the recovery sub-protocol the post-restart gap leans on; grounds the lower-bound recovery precondition (SC-004) and the GapFill-jump no-persist decision (INV-H1 / D-5).
