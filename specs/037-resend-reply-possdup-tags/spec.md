# Feature Specification: Resend-reply PossDup wire conformance (GapFill 43/122 + replay dup-tag suppression)

**Feature Branch**: `037-resend-reply-possdup-tags`
**Created**: 2026-06-14
**Status**: Draft
**Input**: Fable review finding **F-e** (assessments `2.4-half-restructure.md` §3 "DRIFT #2" and `1.6-possdup-021-022.md` §5 "P3"). Two related wire-conformance defects in the SAME two resend-reply frame emitters.

## Overview

When a FIX session replies to a peer's `ResendRequest`, it retransmits the requested sequence-number range as a mix of two frame kinds: replayed application messages (re-serialized from the store) and `SequenceReset`-GapFill administrative frames (covering ranges of skipped administrative messages). Both kinds carry sequence numbers at or below what the peer currently expects, so both MUST be marked as possible duplicates (`PossDupFlag(43)=Y`) to suppress the peer's too-low-sequence-number kill.

Today the two emitters disagree:

- The **replayed application frame** correctly stamps `PossDupFlag(43)=Y` + `OrigSendingTime(122)`.
- The **GapFill** stamps **neither** — leaving it exposed to rejection by a strict counterparty.

Separately, the replayed-application-frame emitter stamps `43`/`122` **unconditionally**, so when the operator has opted to retain caller-supplied `43`/`122` on the original send, the replayed frame ends up carrying each of those tags **twice** — a malformed frame a strict counterparty rejects.

Both defects are latent today (the live QuickFIX-cpp / QuickFIX-J interop cells pass because those peers tolerate the current output), but both are genuine FIX-conformance gaps against the protocol and against the reference engines. This feature closes both in one pass, scoped narrowly to the resend-reply path. It is **not** chunked-resend (C-103, which stays deferred).

## User Scenarios & Testing *(mandatory)*

### User Story 1 - GapFill survives a strict counterparty's duplicate check (Priority: P1)

A session is talking to a strict counterparty that validates `PossDupFlag` on every frame whose sequence number is at or below the expected number (this is what the protocol mandates and what QuickFIX itself does). The peer detects a gap and sends a `ResendRequest`. The session's reply includes a `SequenceReset`-GapFill covering a range of skipped administrative messages; that GapFill's sequence number sits below the peer's expected number.

**Why this priority**: This is the headline conformance gap and the only one on the **default** path — every GapFill on every resend reply is affected, regardless of configuration. A strict peer rejects the unmarked GapFill and the session fails to recover from the gap (resend stall / disconnect).

**Independent Test**: Drive a resend reply that produces a GapFill and inspect the emitted frame: it carries `PossDupFlag(43)=Y` and `OrigSendingTime(122)` equal to the GapFill's own `SendingTime(52)`. Re-run the live QuickFIX-cpp / QuickFIX-J received-reset / resend cells (both roles) and confirm the peers accept the now-marked GapFill and the session recovers.

**Acceptance Scenarios**:

1. **Given** a session replying to a `ResendRequest` over a range that requires a GapFill, **When** the GapFill frame is emitted, **Then** the frame contains exactly one `PossDupFlag(43)=Y` and exactly one `OrigSendingTime(122)` whose value equals the frame's `SendingTime(52)`.
2. **Given** the same GapFill, **When** its field set is compared to the prior behavior, **Then** every previously-present field is unchanged (`8/35/34/49/52/56/36/123`) and only `43`/`122` are added.
3. **Given** a strict live counterparty (QuickFIX-cpp and QuickFIX-J, both roles), **When** the session sends the marked GapFill during gap recovery, **Then** the peer accepts it and the session reaches steady state (no rejection, no disconnect).

---

### User Story 2 - Retained-PossDup application frames replay without duplicate tags (Priority: P2)

An operator has opted into retaining caller-supplied `PossDupFlag(43)` / `OrigSendingTime(122)` on outbound application sends (the existing opt-in "allow PossDup" knob — callers that manage their own duplicate flags). One such frame is stored verbatim. Later the peer requests a resend that includes this frame.

**Why this priority**: This is gated behind a **non-default** opt-in plus caller-supplied duplicate tags plus a resend, so it affects far fewer deployments than Story 1 — but when it triggers, the replayed frame carries each of `43`/`122` **twice**, which a strict counterparty rejects ("tag appears more than once"), stalling the resend.

**Independent Test**: With the retain knob enabled, send an application frame that already carries `43=Y`/`122`, store it, then trigger a resend of that frame and inspect the replayed bytes: each of `43` and `122` appears exactly once.

**Acceptance Scenarios**:

1. **Given** the retain knob enabled and a stored application frame already containing `43=Y` and `122=<t>`, **When** that frame is replayed in a resend reply, **Then** the replayed frame contains exactly one `43` and exactly one `122`.
2. **Given** the default configuration (retain knob off), **When** an application frame is replayed, **Then** the replayed-frame bytes are unchanged from prior behavior (the default path strips caller `43`/`122` on send, so stored frames are already clean and this fix is inert for them).

---

### Edge Cases

- **GapFill with no original frame to source `122` from**: a GapFill represents a *range* of skipped messages, not a single original message, so there is no single stored `SendingTime` to copy. The `OrigSendingTime(122)` value is the GapFill's own `SendingTime(52)` (the reference engines do exactly this). No new timestamp is generated for `122`; it reuses the `52` the GapFill already stamps.
- **Stored application frame that carries `43`/`122` followed by another field**: the duplicate-tag suppression operates on whole, boundary-delimited fields only; the captured original `SendingTime(52)` (used to source the engine's `122`) is still read before suppression, so a retained frame whose own `122` differs from its `52` still replays with `122 = stored 52` (engine semantics), exactly once.
- **A replayed frame with no `SendingTime(52)`**: out of scope — every stored outbound frame carries a `52` (it is a required header field stamped at original send); no behavior change here.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: Every `SequenceReset`-GapFill emitted on a resend reply MUST carry `PossDupFlag(43)=Y`.
- **FR-002**: Every `SequenceReset`-GapFill emitted on a resend reply MUST carry `OrigSendingTime(122)` equal to that frame's own `SendingTime(52)` value.
- **FR-003**: The GapFill MUST continue to carry all fields it carries today (`BeginString(8)`, `MsgType(35)=4`, `MsgSeqNum(34)`, `SenderCompID(49)`, `SendingTime(52)`, `TargetCompID(56)`, `NewSeqNo(36)`, `GapFillFlag(123)=Y`) with their existing values; only `43` and `122` are added.
- **FR-004**: A replayed application frame MUST carry exactly one `PossDupFlag(43)` and exactly one `OrigSendingTime(122)`, even when the stored frame already contained either tag (i.e. the retained-PossDup case MUST NOT produce duplicate tags).
- **FR-005**: When the stored frame already contained `43`/`122`, the replayed frame's `OrigSendingTime(122)` MUST be the stored frame's `SendingTime(52)` (preserving today's replay semantics — the engine sources `122` from the stored `52`, not from any caller-supplied `122`).
- **FR-006**: On the default configuration (retain knob off), the only change to any emitted frame MUST be the two added GapFill tags; replayed application frames MUST be byte-identical to prior behavior.
- **FR-007**: This feature MUST NOT introduce any new public configuration field, error code/slot, generated-code surface, or C-ABI surface. It is a pure wire-output correction in the two existing resend-reply emitters.
- **FR-008**: The new GapFill output MUST be accepted by live QuickFIX-cpp and QuickFIX-J counterparties (both roles) on the resend-recovery path.

### Key Entities

- **SequenceReset-GapFill frame**: the administrative frame (`35=4`, `123=Y`) emitted to cover a contiguous range of skipped administrative messages during a resend reply. Gains `43`/`122`.
- **Replayed application frame**: a stored outbound application message re-serialized for retransmission during a resend reply. Already carries `43`/`122`; gains duplicate-tag suppression for the retained-PossDup case.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A GapFill emitted on a resend reply carries exactly one `43=Y` and exactly one `122` whose value equals the frame's `52` — verified by a unit-level frame inspection and by a banked golden capture.
- **SC-002**: A replayed application frame, under the retain knob with caller-supplied `43`/`122` in the stored frame, carries exactly one `43` and exactly one `122` — verified by a unit-level frame inspection with the knob explicitly enabled.
- **SC-003**: On the default configuration, replayed application frames are byte-identical to prior behavior and the GapFill differs only by the two added tags — verified by golden comparison.
- **SC-004**: The live QuickFIX-cpp and QuickFIX-J resend / received-reset interop cells pass (both roles) with the marked GapFill — verified by a live re-run with goldens re-banked.

## Assumptions

- The `OrigSendingTime(122)` value for an administratively-generated GapFill is the GapFill's own `SendingTime(52)`. **Verified 2026-06-14** against the cloned reference engines: QuickFIX-cpp `Session::generateSequenceReset` (`insertOrigSendingTime(hdr, hdr.getField<SendingTime>())`) and QuickFIX-J `Session.generateSequenceReset` (`header.setUtcTimeStamp(OrigSendingTime, header.getUtcTimeStamp(SendingTime), prec)`) both set `122 = 52`.
- Field placement of the two added tags is at the end of the existing field list, matching the proven append-at-end pattern the replayed-application-frame emitter already ships live (header-fields-after-body is technically non-standard ordering but is empirically tolerated by both reference engines and is what the sibling emitter already does).
- The default-strip behavior of the existing send path guarantees that, under the default configuration, stored frames never contain caller-supplied `43`/`122`, so the duplicate-tag suppression (Story 2) is inert on the default path.
- This feature does not touch the inbound validation of `43`/`122`, the chunked-resend backlog item (C-103), or any non-resend emit site.
