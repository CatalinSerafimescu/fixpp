# Feature Specification: Inbound PossDup / OrigSendingTime Handling

**Feature Branch**: `021-inbound-possdup-origsendingtime`
**Created**: 2026-06-04
**Status**: Draft
**Input**: User description: "Inbound PossDup / OrigSendingTime handling (GAP S-010, the first G3 interop-gap slice). FIX 4.4 session-layer inbound handling of possible-duplicate messages so fixpp interops correctly when a live counterparty (QuickFIX-cpp / QuickFIX-J) replays admin or already-seen messages during recovery."

## Context & Background

When a FIX session recovers from a sequence gap, the side that replays missed messages re-sends them carrying `PossDupFlag(43)=Y` and the original `OrigSendingTime(122)`. The receiving engine must recognise these as *possible duplicates* and tolerate them — a message whose `MsgSeqNum` is at or below what the receiver already expects is the normal, correct shape of a replayed message and must **not** be treated as the fatal "sequence number too low" condition that tears the session down.

fixpp today implements the **outbound** half (it emits `PossDupFlag`/`OrigSendingTime` when it replays). The **inbound** receive side is a deferred gap (parity matrix Bucket 3 — "the single biggest gap by count", flagged as the #1 gap by both QuickFIX-cpp and QuickFIX-J parity audits). Without it, any counterparty that performs a standard ResendRequest→replay recovery against fixpp will have its replayed messages misclassified as a too-low fatal error, dropping the session.

This is the first slice of interop gate **G3**. Scope is FIX 4.4 session-layer only (matrix option (a)); FIXT.1.1 / FIX 5.0 SP2 routing remains in G4.

## Clarifications

### Session 2026-06-04

- Q: Inbound validated too-low possible-duplicate APPLICATION message — drop (QFJ) or redeliver to the app callback (QuickFIX-cpp)? → A: Implement **both** behind a session config knob, **default = drop** (no Application callback). QuickFIX-cpp and QuickFIX-J diverge here; fixpp makes it configurable, defaulting to the safer QFJ behavior. Administrative duplicates are always dropped/ignored regardless of the knob.
- Q: `AllowPossDup` send-path knob — default retain or strip? → A: **QFJ-style knob, default = STRIP** caller-supplied `PossDupFlag(43)`/`OrigSendingTime(122)` on a plain `send`; the automatic resend/retransmission path always (re)adds them regardless of the knob. This corrects the original FR-008 "default = retain".

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Tolerate replayed possible-duplicate messages (Priority: P1)

A counterparty (QuickFIX-cpp or QuickFIX-J) recovering from a gap replays one or more previously-sent messages with `PossDupFlag(43)=Y`. Some of those messages carry a `MsgSeqNum` the fixpp side has already processed (at or below its expected inbound sequence number). fixpp must recognise each as a possible duplicate, ignore the already-applied ones without re-processing or advancing its expected sequence number, and keep the session established.

**Why this priority**: This is the core interop value. Without it the most common real-world recovery flow — the counterparty replaying after a ResendRequest — kills the fixpp session. Every other part of the feature is secondary to "the session survives a standard replay."

**Independent Test**: Drive a live (or fixture) session to a state where the peer replays an already-seen admin message with `43=Y`; assert fixpp stays `Active`, does not emit a Logout, does not re-process the duplicate, and its expected inbound sequence number is unchanged.

**Acceptance Scenarios**:

1. **Given** an established session whose next expected inbound `MsgSeqNum` is N, **When** a message arrives with `MsgSeqNum < N` and `PossDupFlag(43)=Y` and a valid `OrigSendingTime(122)`, **Then** fixpp ignores it as a duplicate, leaves the expected inbound sequence number at N, emits no Logout, and the session remains `Active`.
2. **Given** an established session, **When** a possible-duplicate admin message that was already applied arrives again, **Then** its session-level side effects are not applied a second time (idempotent ignore).
3. **Given** an inbound too-low possible-duplicate **application** message with a valid `OrigSendingTime`, **When** the duplicate-redelivery knob is at its default, **Then** it is discarded without an Application callback and the expected sequence number is unchanged; **When** the knob is enabled, **Then** it is instead delivered to the Application callback flagged as a possible duplicate (still no sequence-number advance, still no disconnect).

---

### User Story 2 - Validate OrigSendingTime on possible-duplicates (Priority: P2)

When a message asserts `PossDupFlag(43)=Y`, the FIX session protocol requires it to also carry `OrigSendingTime(122)`. fixpp must enforce this: a possible-duplicate missing `OrigSendingTime` is rejected at the session level, and one whose `OrigSendingTime` is later than its `SendingTime(52)` is rejected and the session is terminated — matching the disposition QuickFIX-cpp and QuickFIX-J apply.

**Why this priority**: Wire-conformant rejection of malformed duplicates is required for clean interop and to avoid silently accepting corrupt recovery traffic, but it is secondary to the session-survival behavior in US1.

**Independent Test**: Feed a possible-duplicate message with `43=Y` and no `122`; assert fixpp emits a session-level `Reject(35=3)` citing the missing required tag and the session survives. Separately feed `OrigSendingTime > SendingTime`; assert `Reject` + `Logout` + disconnect.

**Acceptance Scenarios**:

1. **Given** an inbound message with `PossDupFlag(43)=Y` and no `OrigSendingTime(122)`, **When** it is processed, **Then** fixpp emits a session-level `Reject(35=3)` with `SessionRejectReason = RequiredTagMissing` and `RefTagID = 122`.
2. **Given** an inbound message with `PossDupFlag(43)=Y` and `OrigSendingTime(122)` strictly later than `SendingTime(52)`, **When** it is processed, **Then** fixpp emits a `Reject(35=3)` with `SessionRejectReason = SendingTimeAccuracyProblem` and then sends a `Logout` and disconnects.
3. **Given** an inbound `SequenceReset(35=4)` carrying `PossDupFlag`, **When** it is processed, **Then** it is **exempt** from the `OrigSendingTime` requirement and the existing gap-fill recovery path handles it unchanged (no new reject).

---

### User Story 3 - AllowPossDup send-path knob (Priority: P3)

An operator configures whether the engine retains or strips caller-supplied `PossDupFlag`/`OrigSendingTime` on a plain `send`, for counterparties whose tolerance differs. The default strips them; the automatic resend path always re-adds them.

**Why this priority**: A small, optional configuration affordance paired with the inbound work (parity matrix lists it alongside S-010). It does not gate interop and is the lowest-value slice of the three.

**Independent Test**: Set the knob both ways; on a plain `send` of a message that already carries `PossDupFlag`/`OrigSendingTime`, assert they are stripped at the default and retained when enabled; separately assert the automatic resend path always emits them regardless.

**Acceptance Scenarios**:

1. **Given** the `AllowPossDup` knob at its default, **When** the application performs a plain `send` of a message carrying caller-supplied `PossDupFlag(43)`/`OrigSendingTime(122)`, **Then** those fields are stripped before transmission.
2. **Given** the knob enabled, **When** the application performs the same plain `send`, **Then** `PossDupFlag`/`OrigSendingTime` are retained.
3. **Given** any setting of the knob, **When** the engine performs an automatic resend/retransmission, **Then** the resent message carries `PossDupFlag(43)=Y` and `OrigSendingTime(122)` (wire-conformant replay, unchanged from today).

---

### Edge Cases

- **Too-low without PossDup (regression guard)**: a message with `MsgSeqNum < expected` and **no** `PossDupFlag=Y` MUST still trigger the existing fatal "MsgSeqNum too low" Logout + disconnect. The new tolerance applies only when `43=Y`.
- **PossDup at the expected sequence number**: `43=Y` with `MsgSeqNum == expected` is processed once normally (it is not below expected; tolerance/ignore does not apply) and not double-applied.
- **OrigSendingTime equal to SendingTime**: equality is allowed (only strictly-greater is the accuracy-problem reject).
- **Possible-duplicate during an open resend window** (`MsgSeqNum > expected` with `43=Y`): handled by the existing too-high/queue path; the duplicate flag does not change too-high disposition.
- **Both roles**: the behavior must hold whether fixpp is the initiator or the acceptor.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: An inbound message with `MsgSeqNum` below the next expected inbound sequence number AND `PossDupFlag(43)=Y` MUST be classified as a possible duplicate and MUST NOT cause a session disconnect or Logout.
- **FR-002**: A tolerated too-low possible-duplicate administrative message MUST NOT advance the expected inbound sequence number and MUST NOT have its session-level side effects re-applied (idempotent ignore).
- **FR-003**: An inbound message with `MsgSeqNum` below expected and WITHOUT `PossDupFlag=Y` MUST continue to trigger the existing fatal "MsgSeqNum too low" Logout + disconnect (no behavioral regression).
- **FR-004**: When an inbound message carries `PossDupFlag(43)=Y`, the engine MUST require `OrigSendingTime(122)`; if absent, the engine MUST emit a session-level `Reject(35=3)` with `SessionRejectReason = RequiredTagMissing` and `RefTagID = 122`.
- **FR-005**: When an inbound message carries `PossDupFlag(43)=Y` and its `OrigSendingTime(122)` is strictly later than its `SendingTime(52)`, the engine MUST emit a `Reject(35=3)` with `SessionRejectReason = SendingTimeAccuracyProblem`, then send a `Logout` and disconnect.
- **FR-006**: An inbound `SequenceReset(35=4)` carrying `PossDupFlag` MUST remain exempt from the `OrigSendingTime` requirement; the existing gap-fill / sequence-reset recovery path MUST handle it unchanged.
- **FR-007**: The inbound possible-duplicate tolerance MUST hold against live QuickFIX-cpp and QuickFIX-J counterparties that replay messages during recovery, in both initiator and acceptor roles.
- **FR-008**: A session-level `AllowPossDup` configuration knob MUST control whether caller-supplied `PossDupFlag(43)`/`OrigSendingTime(122)` are retained or stripped on a plain `send`; the default MUST **strip** them. The automatic resend/retransmission path MUST always (re)add `PossDupFlag`/`OrigSendingTime` regardless of this knob (preserving wire-conformant replay).
- **FR-009**: Each possible-duplicate disposition (tolerated-ignore, required-tag reject, accuracy-problem reject+logout) MUST surface through the engine's existing structured session-event / reject reporting surface, consistent with how other recovery and reject outcomes are reported.
- **FR-010**: For an inbound too-low possible-duplicate **application** message with a valid `OrigSendingTime`, a session configuration knob MUST control its disposition: the default MUST **drop** it (no Application callback, no sequence-number advance); when enabled, the engine MUST deliver it to the Application callback flagged as a possible duplicate. Neither setting advances the expected sequence number nor disconnects. Administrative possible-duplicates are always ignored irrespective of this knob.

### Key Entities

- **Possible-duplicate disposition**: the classification of an inbound message bearing `PossDupFlag=Y` into one of {admin-ignore, app-drop (default), app-redeliver (opt-in), processed-once, required-tag reject, accuracy-problem reject+terminate, sequence-reset exempt}, derived from its `MsgSeqNum` relative to expected, whether it is an admin or application message, the configured app-duplicate knob, the presence of `OrigSendingTime(122)`, and the `OrigSendingTime`/`SendingTime` ordering. No new persistent store is introduced; the existing expected-sequence-number state is the basis for "already applied".

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: In 100% of recovery scenarios where the counterparty replays a previously-processed message with `PossDupFlag=Y`, the fixpp session remains established (no spurious Logout/disconnect).
- **SC-002**: A malformed possible-duplicate (`43=Y`, missing `122`) produces a session-level reject AND the session survives, matching the disposition of both QuickFIX-cpp v1.16.0 and QuickFIX-J 3.0.1.
- **SC-003**: 100% of existing "sequence number too low" (no-PossDup) tests continue to pass unchanged — zero regression in the fatal-too-low path.
- **SC-004**: fixpp interops cleanly with QuickFIX-cpp v1.16.0 and QuickFIX-J 3.0.1 across the PossDup replay scenarios in both initiator and acceptor roles (live interop cells green under normal + sanitizer builds).

## Assumptions

- **FIX 4.4 session-only scope** (interop matrix option (a)). FIXT.1.1 / FIX 5.0 SP2 parse routing and `DefaultApplVerID(1137)` remain deferred to G4 and are out of scope here.
- **Canonical disposition follows QuickFIX-cpp / QuickFIX-J**, the two live interop targets, confirmed by a CodeGraph sweep of `Session::doPossDup`/`doTargetTooLow` (QuickFIX-cpp v1.16.0) and `Session.validatePossDup`/`doTargetTooLow` (QuickFIX-J 3.0.1). The engines AGREE on arms B/C/D/E (FR-003/004/005/006) and the no-seqnum-advance rule; they DIVERGE only on (1) inbound application-duplicate redelivery and (2) the `AllowPossDup` send-path default — both resolved in Clarifications above (configurable, default drop; and QFJ-style strip-by-default).
- **Outbound replay already emits `PossDupFlag`/`OrigSendingTime`** (covered by prior work); this feature adds the inbound receive side plus the `AllowPossDup` plain-`send` toggle, not a new outbound replay path.
- **Application-message duplicate redelivery is in scope as a configurable disposition** (resolved in Clarifications): default drop (QFJ), opt-in redeliver-to-callback (QuickFIX-cpp). It reuses the existing 019/020 Application callback layer for the redeliver path; it does not introduce a new app-message-processing pipeline.
- **Existing engine machinery is reused**: the reject, Logout, disconnect, sequence-number, and structured session-event surfaces from the session FSM and runtime engine already exist; this feature wires the PossDup arms into them rather than introducing new infrastructure.
- **No new persistent dedup store**: "already applied" is determined from the existing expected-inbound-sequence-number state, consistent with the store-replay (not reorder-queue) recovery model confirmed for 9.F.
