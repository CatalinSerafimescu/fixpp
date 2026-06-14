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

**Why this priority**: This is the headline conformance gap and the only one on the **default** path — every GapFill on every resend reply is affected, regardless of configuration. A strict peer rejects the unmarked (missing `43=Y`) GapFill as a too-low sequence number and the session fails to recover from the gap (resend stall / disconnect). (The kill is the too-low-seqnum check, which `43=Y` suppresses — *not* the `122`-required check, from which a `SequenceReset` is exempt; see Assumptions.)

**Independent Test**: Drive a resend reply that produces a GapFill and inspect the emitted frame: it carries `PossDupFlag(43)=Y` and `OrigSendingTime(122)` equal to the GapFill's own `SendingTime(52)`. Re-run the live **QuickFIX-J** in-process recovery-outbound cell (`hp_fix44_recovery_outbound_answer`, both roles) plus any live QFJ resend cell and confirm the peer accepts the now-marked GapFill and the session recovers. The QuickFIX-cpp arm is **waived with rationale** (the only in-process witness `GTEST_SKIP()`s non-QFJ counterparties; QFcpp's resend choreography cannot be induced into the fixpp-emitted-GapFill path on command — same precedent as L-021-3).

**Acceptance Scenarios**:

1. **Given** a session replying to a `ResendRequest` over a range that requires a GapFill, **When** the GapFill frame is emitted, **Then** the frame contains exactly one `PossDupFlag(43)=Y` and exactly one `OrigSendingTime(122)` whose value equals the frame's `SendingTime(52)`.
2. **Given** the same GapFill, **When** its field set is compared to the prior behavior, **Then** every previously-present field is unchanged (`8/35/34/49/52/56/36/123`) and only `43`/`122` are added.
3. **Given** a strict live **QuickFIX-J** counterparty (both roles; the QuickFIX-cpp arm is waived per L-021-3 — its only in-process witness skips non-QFJ peers), **When** the session sends the marked GapFill during gap recovery, **Then** the peer accepts it and the session reaches steady state (no rejection, no disconnect).

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
- **FR-008**: The new GapFill output MUST be accepted by a live **QuickFIX-J** counterparty (both roles) on the resend-recovery path. The QuickFIX-cpp arm is **waived with rationale** (L-021-3 precedent): QFcpp cannot be driven to exercise the fixpp-emitted-GapFill path on command, and the only in-process witness `GTEST_SKIP()`s non-QFJ counterparties.

### Key Entities

- **SequenceReset-GapFill frame**: the administrative frame (`35=4`, `123=Y`) emitted to cover a contiguous range of skipped administrative messages during a resend reply. Gains `43`/`122`.
- **Replayed application frame**: a stored outbound application message re-serialized for retransmission during a resend reply. Already carries `43`/`122`; gains duplicate-tag suppression for the retained-PossDup case.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A GapFill emitted on a resend reply carries exactly one `43=Y` and exactly one `122` whose value equals the frame's `52` — verified by a unit-level frame inspection and by a banked golden capture.
- **SC-002**: A replayed application frame, under the retain knob with caller-supplied `43`/`122` in the stored frame, carries exactly one `43` and exactly one `122` — verified by a unit-level frame inspection with the knob explicitly enabled.
- **SC-003**: On the default configuration, replayed application frames are byte-identical to prior behavior and the GapFill differs only by the two added tags — verified by golden comparison.
- **SC-004**: The fixpp-emitted GapFill `43`/`122` bytes are observed and accepted live by **QuickFIX-J** via the in-process recovery-outbound cell `tests/interop/happy/hp_fix44_recovery_outbound_answer_test.cpp` (both roles) — the only cell that drives a fixpp-emitted GapFill and `GTEST_SKIP()`s non-QFJ counterparties — plus any live QFJ resend / received-reset cell, with the GapFill golden re-baked under the `poss_dup_profile_excluded_tags()` (`{52,10,122}`) profile. The **QuickFIX-cpp arm is waived with rationale** (L-021-3 precedent: QFcpp's resend choreography cannot be induced into the fixpp-emitted-GapFill frame on command, and the in-process witness skips it). Unit-level frame inspection + the banked golden cover the byte-level GapFill conformance for both engines.
  - **Disposition (037 implement):** The golden profile switch shipped (T009, verified — gate-bite cells still bite; `43=Y` compared verbatim). The live **QuickFIX-J** paired run is **DEFERRED to the Item-1 live-golden-capture workstream (→ G4)**, not run in the local ctest environment: the cell GTEST_SKIPs without the parent harness (`FIXPP_TLS_FIXTURE_DIR`/`INTEROP_QUICKFIX_J_PORT` unset). This is a **deferral with an unblock condition (Item-1)**, NOT a permanent waiver — QFJ is achievable and emits `43=Y` GapFills itself (acceptance structurally expected at capture time). Byte-level conformance is carried NOW by the Cell 1 unit witness (`tests/session/test_resend_reply_possdup.cpp`); no committed golden currently captures a `35=4` GapFill (T010 verified no-op), so nothing regressed. See `behaviors-and-limitations.md` L-037-2.

## Assumptions

- The `OrigSendingTime(122)` value for an administratively-generated GapFill is the GapFill's own `SendingTime(52)`. **Verified 2026-06-14** against the cloned reference engines: QuickFIX-cpp `Session::generateSequenceReset` (`insertOrigSendingTime(hdr, hdr.getField<SendingTime>())`) and QuickFIX-J `Session.generateSequenceReset` (`header.setUtcTimeStamp(OrigSendingTime, header.getUtcTimeStamp(SendingTime), prec)`) both set `122 = 52`.
- **`43=Y` is emitted together with `122` by emit-parity, NOT because a strict peer rejects a GapFill missing `122`.** A SequenceReset(`35=4`) is *exempt* from the inbound `122`-required / `122 ≤ 52` check: QFJ's `validatePossDup` (`Session.java:~2575`) guards that entire block behind `if (!MsgType.SEQUENCE_RESET.equals(msgType))`, exactly as fixpp's own inbound exempts `35=4` (Edge Cases). So a strict `RequiresOrigSendingTime=Y` peer does **not** reject a `43=Y` GapFill whose `122` is absent. `122 = 52` is warranted on the GapFill for two reasons instead: (a) **emit-parity** — both QFcpp `generateSequenceReset` and QFJ `generateSequenceReset` unconditionally stamp `43=Y` + `122 = SendingTime` on every GapFill they generate; (b) **FIX grammar correctness** — when `43=Y` is present, `122` is its conditionally-required companion, so emitting `43` without `122` is a malformed possdup frame in the general FIX grammar even where the SequenceReset path happens not to police it. This is why FR-002 makes `122` mandatory alongside FR-001's `43`. (The `122 ≤ 52` boundary is the general possdup inbound rule — it does not apply as a GapFill-reject trigger.)
- Field placement of the two added tags is at the end of the existing field list, matching the proven append-at-end pattern the replayed-application-frame emitter already ships live. **Confirmed order-safe** by the clarify sweep: both reference engines parse the header into a field map (`header.isSetField`/`getField`), so field order is irrelevant to inbound validation; header-fields-after-body is technically non-standard but is what the sibling emitter already ships live and is tolerated.
- The default-strip behavior of the existing send path guarantees that, under the default configuration, stored frames never contain caller-supplied `43`/`122`, so the duplicate-tag suppression (Story 2) is inert on the default path.
- This feature does not touch the inbound validation of `43`/`122`, the chunked-resend backlog item (C-103), or any non-resend emit site.

## Normative References

Per `[const §VI.5]` (exact coverage-index refs matching the catalogue rows this feature amends):

- `[FIX-SL §4.8.5] Gap fill process` — the `SequenceReset(35=4)`-GapFill emitted on a resend reply (the S-006 anchor; `feature-catalogue.md` S-006 row — `build_sequence_reset_gapfill` OUTBOUND emit). DEFECT 1 completes this frame's possdup marking (`43`/`122`).
- `[FIX-SL §4.8.4] Possible duplicates` — `PossDupFlag(43)` + `OrigSendingTime(122)` retransmission semantics (the S-010 anchor; `feature-catalogue.md` S-010 row — 022 `allow_pos_dup` send knob / `build_replay_frame`). DEFECT 2 removes the duplicate-tag emission on the retained-PossDup replay path.
- `[FIX-SL §4.8.2] Request retransmission of messages` — the resend-reply store-walk replay that re-serializes stored outbound frames (the S-005 anchor; `feature-catalogue.md` S-005 row — 013 resend reply: store-walk replay with `PossDup(43)=Y` + `OrigSendingTime(122)`, admin-span GapFill collapse). Both DEFECT 1 and DEFECT 2 act on this reply.
- `[FIX-SL §4.8.4] Possible duplicates` — `OrigSendingTime(122)` required on all `PossDupFlag=Y` retransmitted messages (the S-033 anchor; `feature-catalogue.md` S-033 row, which owns the inbound `122`-required enforcement and the L-021-3 QFcpp waiver this feature mirrors). The now-marked GapFill makes fixpp's *own* emit satisfy the `43`+`122` pairing S-033 enforces on inbound.
