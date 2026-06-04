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
- Q: `AllowPossDup` send-path knob — default retain or strip? → A: **Intended behavior = QFJ-style knob, default = STRIP** caller-supplied `PossDupFlag(43)`/`OrigSendingTime(122)` on a plain `send`; the automatic resend/retransmission path always (re)adds them regardless of the knob. **NOTE (Gate A round 1): this send-path knob is DEFERRED out of this slice** — see Session 2026-06-04 (Gate A round 1) below. The default-strip decision is preserved as the *intended* behavior for the future send-path slice; this slice is INBOUND-ONLY.

### Session 2026-06-04 (Gate A round 1)

- **FR-008 / User Story 3 (`AllowPossDup` send-path knob) DE-SCOPED from this slice.** The plain-`send` path (`Session::send_impl`) is **opaque-payload**: it copies business fields verbatim and does NOT field-parse the app body. "Strip caller-supplied `43`/`122` on a plain send" is therefore NOT a reuse of an existing strip seam — it would require a *new* boundary-anchored payload parser carrying the same delimiter-injection hazard fixpp hit in 020 RC#1 ([[feedback_delimiter_injection_verbatim_field_copy]]). Sending it cleanly is a separable hardening effort, so the send-path knob is split out into its own future slice. **This slice is INBOUND-ONLY.** The user's earlier "default = STRIP" decision is preserved as the *intended* behavior for that deferred slice, pending confirmation. The `redeliver_poss_dup` inbound knob is retained.
- **PossDup validation applies to ALL inbound `43=Y` non-`SequenceReset` messages**, not only the too-low ones. The OrigSendingTime validation (Arms C/D) runs for any `43=Y` inbound message including those at the expected sequence number — matching QuickFIX-J (which validates PossDup at-expected, `Session.java:1843`) and deliberately a *superset* of QuickFIX-cpp (which validates only via the too-low path). The too-low seqnum-tolerance disposition (Arms A/B) runs *after* this validation. See research.md "third divergence".

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

**Independent Test**: Feed a possible-duplicate message with `43=Y` and no `122`; assert fixpp emits a session-level `Reject(35=3)` citing the missing required tag (`371=122`, `373=1`) and the session survives. Separately feed `OrigSendingTime > SendingTime`; assert `Reject(373=10)` + `Logout` + disconnect.

**Acceptance Scenarios**:

1. **Given** an inbound message with `PossDupFlag(43)=Y` and no `OrigSendingTime(122)`, **When** it is processed, **Then** fixpp emits a session-level `Reject(35=3)` with `SessionRejectReason(373) = RequiredTagMissing(1)` and `RefTagID(371) = 122`.
2. **Given** an inbound message with `PossDupFlag(43)=Y` and `OrigSendingTime(122)` strictly later than `SendingTime(52)`, **When** it is processed, **Then** fixpp emits a `Reject(35=3)` with `SessionRejectReason(373) = SendingTimeAccuracyProblem(10)` and `RefTagID(371) = 122`, and then sends a `Logout` and disconnects.
3. **Given** an inbound `SequenceReset(35=4)` carrying `PossDupFlag`, **When** it is processed, **Then** it is **exempt** from the `OrigSendingTime` requirement and the existing gap-fill recovery path handles it unchanged (no new reject).
4. **Given** an inbound `43=Y` message **at the expected sequence number** (`34 == expected`) with **no** `OrigSendingTime(122)`, **When** it is processed, **Then** validation still fires (seqnum-independent) and fixpp emits the same Arm-C `Reject(35=3)` `371=122`, `373=1` — the at-expected case is NOT exempt. (Test: `34==expected, 43=Y, no 122`.)
5. **Given** an inbound `43=Y` message **at the expected sequence number** with `OrigSendingTime(122)` strictly later than `SendingTime(52)`, **When** it is processed, **Then** fixpp emits the Arm-D `Reject(373=10)` + `Logout` + disconnect. (Test: `34==expected, 43=Y, 122>52`.)

---

### Edge Cases

- **Too-low without PossDup (regression guard)**: a message with `MsgSeqNum < expected` and **no** `PossDupFlag=Y` MUST still trigger the existing fatal "MsgSeqNum too low" disposition — a transition to `Disconnected` with **no Logout wire frame** (`session.cpp:1860-1862`, `record_state_transition_` only) — as in FR-003 / contract C3. The new tolerance applies only when `43=Y`.
- **PossDup at the expected sequence number**: `43=Y` with `MsgSeqNum == expected` is processed once normally (it is not below expected; tolerance/ignore does not apply) and not double-applied.
- **OrigSendingTime equal to SendingTime**: equality is allowed (only strictly-greater is the accuracy-problem reject).
- **Possible-duplicate during an open resend window** (`MsgSeqNum > expected` with `43=Y`): handled by the existing too-high/queue path; the duplicate flag does not change too-high disposition.
- **Both roles**: the behavior must hold whether fixpp is the initiator or the acceptor.

### Deferred Follow-up — AllowPossDup send-path knob (FR-008, out of scope)

> **DEFERRED to a future send-path slice (Gate A round 1) — NOT part of this slice.** An operator would configure whether the engine retains or strips caller-supplied `PossDupFlag`/`OrigSendingTime` on a plain `send`. The **intended** behavior (preserved from the 2026-06-04 clarification) is: default strips them; the automatic resend path always re-adds them.
>
> **Why deferred**: the plain-`send` path (`Session::send_impl`) is **opaque-payload** — it copies the business body verbatim and does not field-parse it, so there is no existing strip seam to toggle. Implementing the strip requires a *new* boundary-anchored `43`/`122` excision parser over the opaque payload, carrying the same delimiter-injection hazard as 020 RC#1 ([[feedback_delimiter_injection_verbatim_field_copy]]) — a separable hardening effort that does not gate interop. This slice is INBOUND-ONLY; the send-path knob is split out as its own future slice (pending user confirmation of the default-strip intent).

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: An inbound message with `MsgSeqNum` below the next expected inbound sequence number AND `PossDupFlag(43)=Y` MUST be classified as a possible duplicate and MUST NOT cause a session disconnect or Logout.
- **FR-002**: A tolerated too-low possible-duplicate administrative message MUST NOT advance the expected inbound sequence number and MUST NOT have its session-level side effects re-applied (idempotent ignore).
- **FR-003**: An inbound message with `MsgSeqNum` below expected and WITHOUT `PossDupFlag=Y` MUST continue to trigger the existing fatal "MsgSeqNum too low" disposition: a transition to `Disconnected` with **no Logout wire frame emitted**, preserved byte-identical to the current behavior (`session.cpp:1860-1862` emits no Logout — only `record_state_transition_(fsm_state::Disconnected)`). (no behavioral regression.)
- **FR-004**: When **any** inbound `43=Y` non-`SequenceReset(35=4)` message — regardless of its `MsgSeqNum` relative to expected — does not carry `OrigSendingTime(122)`, the engine MUST emit a session-level `Reject(35=3)` with `SessionRejectReason = RequiredTagMissing(373=1)` and `RefTagID(371) = 122`. (Validation is seqnum-independent — it runs for at-expected `43=Y` too, matching QuickFIX-J.)
- **FR-005**: When **any** inbound `43=Y` non-`SequenceReset(35=4)` message carries an `OrigSendingTime(122)` strictly later than its `SendingTime(52)`, the engine MUST emit a `Reject(35=3)` with `SessionRejectReason = SendingTimeAccuracyProblem(373=10)` and `RefTagID(371) = 122` (the offending OrigSendingTime field), then send a `Logout(35=5)` and transition to `Disconnected` — reusing the existing §1699 accuracy-reject Reject→Logout→Disconnect emit pattern. (Validation is seqnum-independent.)
- **FR-006**: An inbound `SequenceReset(35=4)` carrying `PossDupFlag` MUST remain exempt from the `OrigSendingTime` requirement; the existing gap-fill / sequence-reset recovery path MUST handle it unchanged.
- **FR-007**: The inbound possible-duplicate tolerance MUST hold against live QuickFIX-cpp and QuickFIX-J counterparties that replay messages during recovery, in both initiator and acceptor roles.
- **FR-008**: *(DEFERRED — out of scope this slice.)* A send-path `AllowPossDup` knob (strip/retain caller-supplied `PossDupFlag(43)`/`OrigSendingTime(122)` on a plain `send`, default strip; auto-resend always re-adds) is split out into a future send-path slice — the plain-`send` path is opaque-payload and stripping requires a new boundary-anchored parser ([[feedback_delimiter_injection_verbatim_field_copy]]). See Clarifications Session 2026-06-04 (Gate A round 1). This slice is INBOUND-ONLY.
- **FR-009**: The **reject/terminate** possible-duplicate dispositions (required-tag reject — Arm C; accuracy-problem reject+logout — Arm D) MUST surface through the engine's existing structured reject / session-event reporting surface (the `Reject(35=3)` wire frame and the existing FSM `→Disconnected` event), consistent with how other reject and disconnect outcomes are reported. The **tolerated** arms (admin-ignore, app-drop) are intentionally **silent** at the session-event surface — they have no wire/event emission by design (a duplicate already processed once); an optional debug-trace is permitted but NOT required.
- **FR-010**: For an inbound too-low possible-duplicate **application** message with a valid `OrigSendingTime`, a session configuration knob MUST control its disposition: the default MUST **drop** it (no Application callback, no sequence-number advance); when enabled, the engine MUST deliver it to the Application callback flagged as a possible duplicate. Neither setting advances the expected sequence number nor disconnects. Administrative possible-duplicates are always ignored irrespective of this knob.

### Key Entities

- **Possible-duplicate disposition**: the classification of an inbound message bearing `PossDupFlag=Y` into one of {admin-ignore, app-drop (default), app-redeliver (opt-in), processed-once, required-tag reject, accuracy-problem reject+terminate, sequence-reset exempt}, derived from its `MsgSeqNum` relative to expected, whether it is an admin or application message, the configured app-duplicate knob, the presence of `OrigSendingTime(122)`, and the `OrigSendingTime`/`SendingTime` ordering. No new persistent store is introduced; the existing expected-sequence-number state is the basis for "already applied".

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: In 100% of recovery scenarios where the counterparty replays a previously-processed message with `PossDupFlag=Y`, the fixpp session remains established (no spurious Logout/disconnect).
- **SC-002**: A malformed possible-duplicate (`43=Y`, missing `122`) produces a session-level reject AND the session survives, matching the disposition of both QuickFIX-cpp v1.16.0 and QuickFIX-J 3.0.1.
- **SC-003**: 100% of existing "sequence number too low" (no-PossDup) tests continue to pass unchanged — zero regression in the fatal-too-low path.
- **SC-004**: fixpp interops cleanly with QuickFIX-cpp v1.16.0 and QuickFIX-J 3.0.1 across the PossDup replay scenarios in both initiator and acceptor roles (live interop cells green under normal + sanitizer builds).

## Normative References

Per `[const §VI.5]` (exact coverage-index refs matching the catalogue S-010/S-033 rows):

- `[FIX-SL §4.8.4] Possible duplicates` — `PossDupFlag(43)` + `OrigSendingTime(122)` retransmission semantics (the S-010/S-033 anchor; `feature-catalogue.md:30,345`).
- `[FIX-SL §4.5.4] SessionRejectReason` — session-level `Reject(35=3)` field set (`371=RefTagID`, `372=RefMsgType`, `373=SessionRejectReason`); the `RequiredTagMissing(1)` and `SendingTimeAccuracyProblem(10)` reason values used by Arms C/D.

## Assumptions

- **FIX 4.4 session-only scope** (interop matrix option (a)). FIXT.1.1 / FIX 5.0 SP2 parse routing and `DefaultApplVerID(1137)` remain deferred to G4 and are out of scope here.
- **`PossResend(97)` is OUT of scope** — this slice delivers `PossDupFlag(43)` inbound duplicate semantics only. The application-resend `PossResend(97)` half of catalogue row S-010 is deferred to a later G3 slice; S-010 therefore stays `backlog` with a 021 partial-delivery note (see §VI delta in plan.md). The spec reads/validates/mentions no tag 97.
- **Canonical disposition follows QuickFIX-cpp / QuickFIX-J**, the two live interop targets, confirmed by a CodeGraph sweep of `Session::doPossDup`/`doTargetTooLow` (QuickFIX-cpp v1.16.0) and `Session.validatePossDup`/`doTargetTooLow` (QuickFIX-J 3.0.1). The engines AGREE on arms C/D/E (FR-004/005/006) and the no-seqnum-advance rule; they DIVERGE on **three** axes: (1) inbound application-duplicate redelivery (QFJ drops, QFcpp redelivers) — resolved configurable, default drop; (2) **PossDup validation at the expected sequence number** — QFJ validates at-expected (`Session.java:1843`), QFcpp validates only via the too-low path. fixpp chooses to **validate PossDup for ALL `43=Y` non-`SequenceReset` inbound messages** (the QFJ superset, safer — see Clarifications Session 2026-06-04 Gate A round 1); (3) the send-path `AllowPossDup` default — **DEFERRED** out of this slice (opaque-send hardening required). Arm B (too-low without PossDup) is a fixpp behavior preserved as-is (`→Disconnected`, no Logout wire frame — see FR-003).
- **Outbound replay already emits `PossDupFlag`/`OrigSendingTime`** (covered by prior work); this feature adds the inbound receive side only. The send-path `AllowPossDup` toggle is deferred (FR-008, out of scope this slice).
- **Application-message duplicate redelivery is in scope as a configurable disposition** (resolved in Clarifications): default drop (QFJ), opt-in redeliver-to-callback (QuickFIX-cpp). It reuses the existing 019/020 Application callback layer for the redeliver path; it does not introduce a new app-message-processing pipeline.
- **Existing engine machinery is reused**: the reject, Logout, disconnect, sequence-number, and structured session-event surfaces from the session FSM and runtime engine already exist; this feature wires the PossDup arms into them rather than introducing new infrastructure.
- **No new persistent dedup store**: "already applied" is determined from the existing expected-inbound-sequence-number state, consistent with the store-replay (not reorder-queue) recovery model confirmed for 9.F.
