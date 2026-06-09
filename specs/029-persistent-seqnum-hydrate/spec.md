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
3. **Given** a restarted session resumed at inbound `6`, **When** the counterparty's first post-restart message arrives ahead of the local expectation (a gap), **Then** the existing recovery sub-protocol (ResendRequest / SequenceReset) resynchronises the inbound stream rather than the session fataling on a too-low/too-high check.

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
- **Inbound store-write failure** (mid-session): **fatal → disconnect** (D-3); reconnect re-hydrates the last durable value and 013 recovery resyncs, so no later hydrate resumes from a value *behind* the true position.
- **Crash mid-delivery** (between app callback and inbound persist): at-least-once (D-2) — on restart the message is re-delivered (counter not yet advanced), deduped by the standard PossDup/ResendRequest path; never silently skipped.
- **Store-read failure on the hydrate path**: fatal → disconnect, reusing the existing store-failure disposition (no new error slot).
- **Counterparty ahead after restart** (inbound gap): recovered by the existing 013 ResendRequest/SequenceReset sub-protocol, not by this feature; the hydrate must leave the inbound expectation in a state the recovery path can act on rather than fataling at the too-low/too-high gate.
- **Counterparty resets (`141=Y`) on the post-restart Logon**: the existing 013/024 received-reset handling still owns this; hydrate must not mask or reorder it (hydrate establishes the resume baseline; a peer reset then overrides per existing policy).
- **Outbound store-write failure desync** (the New-2 case): even outbound continuity assumes the store tracks the in-memory counter; a swallowed outbound store-failure must not cause a later hydrate to regress the outbound counter and re-stamp a used number.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The system MUST durably record each accepted inbound message's sequence advance to the persistent message store, so the store's inbound counter tracks the live inbound stream (closing the unwired inbound store-write).
- **FR-002**: The inbound store-write MUST occur **after** the application callback (`fromApp`/`fromAdmin`) for that message returns (deliver-then-persist / at-least-once, **D-2**), so a crash mid-delivery re-delivers the message on restart (recovered by the standard PossDup/ResendRequest path) rather than silently skipping it.
- **FR-003**: When a session opens, the system MUST load both persisted sequence-number counters (inbound and outbound) from the store into the in-memory sequence-number state, so the session resumes from disk rather than from 1. This hydrate is **always-on** whenever a persistent store is configured (**D-1**); no config flag gates it.
- **FR-004**: The hydrate MUST complete before any outbound message's sequence number is sampled and before the inbound validation gate runs, so resumed sessions emit and validate against the persisted positions.
- **FR-005**: With no persisted state (memory store, null store, empty/first-run persistent store), the system MUST behave byte-for-byte identically to current behavior on the open path — counters start at `1`, no added store read, no added allocation.
- **FR-006**: A store-read failure during hydrate MUST be treated as a fatal session error → disconnect, reusing the existing store-failure disposition (no new error slot).
- **FR-007**: An inbound store-write failure MUST be **fatal → disconnect** (**D-3**), reusing the existing store-failure disposition (no new error slot). On reconnect, hydrate reads the last durable value and the 013 recovery sub-protocol resyncs any gap — so no hydrate ever resumes from a value *behind* the true position (no silent regression / no replay of already-delivered messages).
- **FR-008**: The system MUST add a production path to load the in-memory counters from given values — the in-memory sequence-number state today exposes an inbound setter and a reset-to-one but **no production outbound setter** (only a test hook). Hydrate uses this new path.
- **FR-009**: When a resumed session's first post-restart inbound message arrives out of order relative to the hydrated expectation, the existing ResendRequest/SequenceReset recovery sub-protocol (013) MUST handle resynchronisation; hydrate MUST leave the inbound expectation in a recoverable state rather than fataling at the too-low/too-high gate.
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

- **SC-001**: After a restart over a persistent store pre-seeded to `next_outbound = N`, the session's first outbound message carries `34 = N` — verified by an automated test.
- **SC-002**: After N accepted inbound messages, a restart, and re-open over the same persistent store, the session's next expected inbound resumes at `N + 1` (not `1`) — verified by an automated test that drives inbound traffic, restarts, and reopens.
- **SC-003**: With a memory/null store, 100% of existing session/seqnum/store regression witnesses remain green and produced frames are byte-identical to the pre-feature baseline; the open path performs zero added store reads and zero added allocations (verified under the existing no-heap/allocation discipline).
- **SC-004**: A resumed session whose counterparty is ahead recovers via ResendRequest/SequenceReset (no fatal disconnect on the first inbound) — verified by an automated test exercising the post-restart gap path.
- **SC-005**: A store-read failure on the hydrate path produces a fatal disconnect and leaves the in-memory state unchanged (no partial seed) — verified by an automated test (split: first-read-fails and second-read-fails-after-first-succeeds, proving no partial seed).
- **SC-006**: An inbound store-write failure produces a fatal disconnect (D-3), and a subsequent reconnect/hydrate resumes from the last durable value (never behind the true position) — verified by an automated test injecting an inbound persist failure.

## Assumptions

- **Deliver-then-persist ordering (D-2)**: the inbound persist follows the application callback (at-least-once), matching QuickFIX-cpp/J. This supersedes the unwired `I-3` "store-before-deliver" comment at `session.cpp:1517`; `/speckit-plan` reconciles/retires that comment.
- **Hydrate-on-open is always-on (D-1)**: QuickFIX-cpp / QuickFIX-J construct the in-memory counters *from* the store, making the store the source of truth; fixpp adopts the always-on model (load from the store at open whenever a persistent store is configured). The default-off byte-identity floor (FR-005) holds because a memory/null store yields 1.
- **Inbound store-write failure is fatal-disconnect (D-3)**: a dropped session reconnects and hydrates the last durable value, with 013 recovery resyncing any gap, avoiding the New-2 regression class; confirmed against the reference engines (`setNextTargetMsgSeqNum` throws `IOException`).
- **No new error slot / wire field / codegen / C-ABI**; store-failure paths reuse the existing store-failure disposition.
- **Builds on 008** (FileStore — outbound persistence already wired; the store interface already carries a direction parameter and the FileStore already advances the inbound counter on an inbound write) **and 013** (ResendRequest/SequenceReset recovery sub-protocol for the post-restart gap).
- **Catalogue**: this discharges the inbound half of the persistence story; coordinate a catalogue row with S-018 and update the deferred L-025-1 inbound-refresh marker accordingly.
- **025 RefreshOnLogon** (the per-logon re-hydrate knob) is a separate follow-on slice and is not implemented here.
