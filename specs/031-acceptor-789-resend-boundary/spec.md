# Feature Specification: Acceptor NextExpectedMsgSeqNum(789) Resend-Range Boundary Fix

**Feature Branch**: `031-acceptor-789-resend-boundary`
**Created**: 2026-06-10
**Status**: Draft
**Input**: User description: "Acceptor NextExpectedMsgSeqNum(789) resend-range boundary fix — fixpp acceptor honors the peer initiator's initial-Logon 789 against the post-reply outbound counter, emitting a spurious SequenceReset-GapFill at an already-consumed seqnum so both real engines reject and the session fails to establish. Evaluate the honor against the pre-reply outbound counter."

## Overview

This is a **conformance bug fix** for a regression-class defect in the merged `027-next-expected-msgseqnum` feature (catalogue row **S-031**, `NextExpectedMsgSeqNum(789)` fast-resume). It was found by the `027` **SC-005 live acceptor interop cell** running against a real QuickFIX-cpp v1.16.0 counterparty, and is invisible to the in-process `027` unit tests — exactly paralleling the `030` received-141 acceptor off-by-one that live interop also uniquely caught.

When fixpp is the **acceptor** with the `789` knob enabled, it must respond to a peer initiator's Logon that advertises `NextExpectedMsgSeqNum`. fixpp **honors** that value after it has already emitted its own reply Logon (a deliberate ordering: reply first, then honor). The honor logic compares the peer's advertised value (`X`) against fixpp's *own next-outbound* (`N`) to decide whether the peer is behind and needs a proactive resend of `[X, N-1]`. Because the comparison reads the next-outbound **after** the reply Logon has already consumed a sequence number, `N` is one higher than the value the peer's advertisement was measured against. In the ordinary in-sync case (the peer is up to date and asks for fixpp's very next message), this off-by-one makes fixpp believe the peer is behind by exactly one and emit a spurious `SequenceReset-GapFill` whose sequence number is the one the reply Logon **already used**. That is a duplicate-sequence-number protocol violation; the peer rejects it with a `Logout` ("MsgSeqNum too low") and the session never reaches a usable established state.

The reference engines avoid this by capturing the resend decision against the next-outbound value **before** the reply Logon is sent. fixpp must do the equivalent: evaluate the acceptor's `789` honor against the **pre-reply** next-outbound counter.

## Clarifications

### Session 2026-06-10

- Q: The acceptor `789` honor evaluates three comparisons — too-high (`X > N` → Logout), behind (`X < N` → resend), in-sync (`X == N` → no resend) — and today all three read the post-reply outbound (`N_post = N_pre + 1`). The confirmed bug is the in-sync case. How wide should the fix be? → A: **Comprehensive** — capture `N_pre` once (before the reply Logon consumes a sequence number) and use it for **all three** comparisons, matching QuickFIX which evaluates the decision pre-reply. This also corrects the too-high boundary (a peer advertising exactly `N_pre + 1` in its initial Logon is genuinely too-high — it claims a message fixpp has not sent — so it must Logout, not be treated as in-sync). The blast radius (a shifted `X > N` boundary that may touch a `027` negative-test pin) is accepted and reconciled at Gate A / during TDD.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Acceptor with the 789 knob establishes a session with an up-to-date peer (Priority: P1)

An operator runs fixpp as a FIX **acceptor** with `NextExpectedMsgSeqNum(789)` enabled (the fast-resume knob), interoperating with a counterparty initiator that also advertises `789`. In the ordinary case the peer is fully in sync — its Logon advertises that the next message it expects from fixpp is fixpp's very next outbound message (no gap). fixpp must complete the Logon exchange and reach a usable established session **without emitting any spurious resend frame**, so the peer accepts the handshake and the session stays up.

**Why this priority**: This is the canonical, most common acceptor case for the `789` knob — a fresh or in-sync session establishing with a real counterparty. Today fixpp emits a spurious `SequenceReset-GapFill` at an already-used sequence number, the peer rejects it, and the session fails to establish at all. The knob is therefore effectively broken for fixpp-as-acceptor against any conformant peer. Fixing this restores the headline `027` capability on the acceptor side and is the precondition for the `027` SC-005 live acceptor interop cell to pass.

**Independent Test**: Drive an acceptor session with the `789` knob enabled under a non-strict reset-seqnum policy. Have the peer initiator send a Logon advertising `789 = fixpp's next-outbound` (the in-sync value) with no actual gap. Assert: (a) fixpp's reply Logon advertises its own `789`; (b) fixpp emits **no** `SequenceReset` and **no** `ResendRequest` after the reply Logon; (c) fixpp reaches the established (Active) state and **stays** there (the peer does not reject and disconnect). Verifiable in-process by inspecting fixpp's emitted frames and final state, and live by the SC-005 acceptor interop cell against QuickFIX-cpp / QuickFIX-J.

**Acceptance Scenarios**:

1. **Given** a fixpp acceptor with the `789` knob enabled and an in-sync peer initiator whose Logon advertises `789` equal to fixpp's next-outbound sequence number, **When** the Logon exchange completes, **Then** fixpp emits its reply Logon, emits **no** subsequent `SequenceReset` or `ResendRequest`, reaches the established state, and the session remains established (the peer does not reject and disconnect).
2. **Given** the same acceptor session, **When** fixpp's reply Logon is inspected, **Then** every sequence number fixpp puts on the wire is strictly increasing (no two emitted frames share a sequence number).

---

### User Story 2 - Acceptor with the 789 knob proactively resends to a genuinely-behind peer (Priority: P1)

An operator runs the same fixpp acceptor, but the peer initiator's Logon advertises a `789` value **lower** than fixpp's next-outbound — i.e. the peer is genuinely missing messages fixpp already sent. fixpp must proactively resend exactly the missing range and reach the established state with **no** `ResendRequest` on the wire (fast-resume). This behavior already exists in `027`; this story exists to guarantee the boundary fix does **not** regress the genuine-gap path.

**Why this priority**: The boundary fix narrows when a resend fires. It must continue to fire — covering exactly the right range — when the peer really is behind. Losing the genuine proactive-resend would re-break `027`'s core value (`SC-001`/`SC-003`).

**Independent Test**: Drive an acceptor session with the `789` knob enabled where fixpp has a known set of already-sent messages and the peer's Logon advertises a `789` strictly below fixpp's next-outbound. Assert fixpp proactively resends exactly the missing range (as gap-filled admin / replayed app messages), emits **no** `ResendRequest`, and reaches the established state. The resend's sequence numbers must be exactly the missing range — not the just-used reply-Logon sequence number.

**Acceptance Scenarios**:

1. **Given** a fixpp acceptor with the `789` knob enabled, fixpp's pre-reply next-outbound at `N_pre` (`N_pre > 2`), and a peer initiator advertising `789 = X` with `X < N_pre`, **When** the Logon exchange completes, **Then** fixpp proactively resends exactly the `027`-shipped genuine-gap range starting at `X` (upper endpoint per research.md — unchanged from `027`), emits no `ResendRequest`, and reaches the established state. The retransmitted frames legitimately carry their historical sequence numbers (PossDup / GapFill), which are **lower** than later in-order frames — this is required FIX resend behavior and is not a monotonicity violation.
2. **Given** the genuine-gap case above, **When** fixpp's emitted frames are inspected, **Then** the resend covers exactly the `027`-shipped range starting at `X` (endpoint per research.md) and fixpp emits **no newly originated** frame (e.g. a fresh `SequenceReset-GapFill`) that re-uses the sequence number consumed by the reply Logon.

---

### Edge Cases

- **Peer advertises 789 above fixpp's pre-reply next-outbound (X > N_pre)**: the peer claims to have received frames fixpp never sent — a sequence-integrity violation. fixpp must emit a `Logout` with explanatory text and disconnect (the existing `027` X>N arm), with the boundary now evaluated against `N_pre`; in particular `X == N_pre + 1` (`= N_post`) is too-high (today it is mis-classified as in-sync).
- **Peer advertises a present-but-invalid 789** (empty / non-numeric / overflow → parses to zero): fixpp must continue to emit a `Logout` ("NextExpectedMsgSeqNum invalid") and disconnect (the existing `027` invalid-789 arm), unchanged — it must never be reinterpreted as a genuine "behind" value.
- **Peer reset Logon (`34=1, 141=Y`) combined with 789**: out of scope for this fix; reset-Logon inbound advancement is owned by `030`. This feature only changes the boundary used to compute the acceptor's proactive-resend range when honoring a peer's `789`.
- **Initiator role**: fixpp acting as initiator already honors the peer acceptor's reply-Logon `789` correctly (the reply advertises a value that matches fixpp's post-own-Logon next-outbound, so no spurious resend fires; the live initiator interop cells pass against both engines). This fix must not change the initiator path's observable behavior.
- **Knob disabled (default)**: with the `789` knob off, no honoring occurs and behavior is byte-identical to the pre-feature baseline — unchanged by this fix.
- **Behind-side tolerance path**: when the peer's inbound Logon sequence number is tolerated without advancing fixpp's inbound counter (the existing `027` "formulation A" path), the honor of the peer's `789` must still compute its resend range against the correct pre-reply next-outbound.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: When fixpp is an acceptor with the `789` knob enabled and honors a peer initiator's advertised `NextExpectedMsgSeqNum` value `X`, the system MUST evaluate **all** of its honor comparisons (too-high, behind, in-sync) against the **pre-reply** next-outbound sequence number `N_pre` (the value captured before the reply Logon consumes a sequence number), not the post-reply value `N_post = N_pre + 1`.
- **FR-002**: When `X == N_pre` (the peer is in sync and expects fixpp's next outbound message), the system MUST treat the session as in-sync and emit **no** `SequenceReset` and **no** `ResendRequest` after the reply Logon.
- **FR-003**: When `X < N_pre` (the peer is genuinely behind), the system MUST proactively resend exactly the messages the peer is missing (admin gaps gap-filled, app messages replayed as appropriate) and emit **no** `ResendRequest`. The resend range begins at `X`; its upper endpoint MUST match the reference-engine semantics for the post-reply outbound counter — the exact endpoint (`N_pre - 1` vs `N_pre`, i.e. whether the just-emitted reply Logon's sequence number is included) is pinned against QuickFIX-cpp / QuickFIX-J source in `plan.md` / `research.md` and is NOT changed from the already-shipped `027` genuine-gap range (only the *decision to resend* and its lower-bound guard are corrected by this fix).
- **FR-004**: The system MUST NOT emit any **newly originated** frame (e.g. a fresh `SequenceReset-GapFill`) whose sequence number equals a sequence number already consumed by the reply Logon. In the in-sync case (`X == N_pre`) this means: no frame at all is emitted after the reply Logon (the confirmed bug is a fresh `SequenceReset-GapFill` re-using the reply Logon's sequence number). Legitimate retransmissions on the genuine-gap path (`X < N_pre`) carry their historical sequence numbers as PossDup / GapFill by FIX resend semantics and are expressly **exempt** from any "strictly increasing" reading; the binding invariant is solely that no newly originated frame re-uses a consumed sequence number.
- **FR-005**: When `X > N_pre`, the system MUST emit a `Logout` (with explanatory text reporting the expected and received values) followed by disconnect — preserving the existing `027` sequence-integrity arm, with the too-high boundary evaluated against `N_pre` (so a peer advertising exactly `N_pre + 1` in its initial Logon — claiming a message fixpp has not yet sent — is Logout-and-disconnect, not silently treated as in-sync).
- **FR-006**: When the peer's `789` is present but invalid (empty / non-numeric / overflow), the system MUST continue to emit a `Logout` ("NextExpectedMsgSeqNum invalid") followed by disconnect — preserving the existing `027` invalid-789 arm; an invalid value MUST NOT be treated as a genuine "behind" value.
- **FR-007**: The system MUST preserve the existing `027` ordering in which the acceptor's reply Logon is emitted before the peer's `789` is honored; the fix changes only the counter against which the resend boundary is evaluated, not the emit ordering.
- **FR-008**: The fix MUST NOT change the observable behavior of fixpp acting as an **initiator** when honoring a peer acceptor's reply-Logon `789`, nor the default knob-off path (byte-identical to the pre-feature baseline).
- **FR-009**: The fix MUST introduce **no** new wire field, error code/slot, generated-code change, or C-ABI surface; it is a pure correction of an existing internal boundary computation.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: With the `789` knob enabled on a fixpp acceptor and an in-sync peer initiator (advertising `789` equal to fixpp's next-outbound, no gap), the session reaches the established state and **zero** `SequenceReset` and **zero** `ResendRequest` frames are emitted by fixpp after the reply Logon — verified by an automated test counting fixpp's emitted frames, and by a live interop cell against a counterparty with `NextExpectedMsgSeqNum` enabled (both QuickFIX-cpp and QuickFIX-J).
- **SC-002**: With the `789` knob enabled on a fixpp acceptor and a genuinely-behind peer (advertising `789 = X < N_pre`), fixpp proactively resends exactly the missing range starting at `X` (upper endpoint per the `027`-shipped genuine-gap semantics pinned in research.md) and emits **zero** `ResendRequest` — verified by an automated test asserting the exact resent range; the range MUST NOT regress from the shipped `027` behavior.
- **SC-003**: In the in-sync acceptor case, fixpp emits **no** frame after the reply Logon, so no **newly originated** frame re-uses the reply-Logon sequence number — verified by an automated test inspecting emitted frames; today the in-sync case violates this by emitting a fresh `SequenceReset-GapFill` at the reply-Logon sequence number. (Genuine-gap retransmissions legitimately carry historical sequence numbers as PossDup / GapFill and are **not** subject to a strict-monotonic reading — see FR-003 / SC-002.)
- **SC-004**: The live `027` SC-005 acceptor interop cell (`NE-*-acc`) against a running QuickFIX-cpp / QuickFIX-J counterparty (with `NextExpectedMsgSeqNum` enabled) establishes and the counterparty does **not** reject with a "MsgSeqNum too low" `Logout` — verified by the live interop harness wire transcript.
- **SC-005**: 100% of existing session / recovery / `027` / `029` / `030` regression witnesses remain green; the default (knob-off) path and the initiator-role `789` honor path are byte-identical to the pre-fix baseline — verified by the existing test suite plus a byte-identity check on the affected admin frames.

## Normative References

- **[FIX-SL §4.4.1]** — *FIX Session-Layer*, `NextExpectedMsgSeqNum(789)` Logon-based fast-resume: the responder evaluates the peer's advertised next-expected value against its own next-outbound to decide proactive retransmission, measured at the point the peer's advertisement was generated (before the responder's own reply is counted).
- **Reference-engine oracle (grounding)** — QuickFIX-cpp v1.16.0 captures the retransmit decision against `getExpectedSenderNum()` **before** sending its reply Logon, and its acceptor reply advertises `getExpectedTargetNum() + 1`; the initial Logon advertises `getExpectedTargetNum()` (no `+1`). QuickFIX-J 3.0.1 is functionally equivalent. (Exact line citations are recorded in `plan.md` / `research.md` during planning.)

## Assumptions

- The `789` knob (`enable_next_expected_msg_seq_num`) and its wire semantics are otherwise as shipped in `027`; this feature changes only the acceptor honor's resend-range boundary computation.
- Reset-Logon inbound-advancement semantics (`141=Y` paths) are owned by `030` and are out of scope here; the in-sync acceptor case targeted by SC-001 uses a non-strict reset-seqnum policy with no `141=Y` from the peer.
- The reference engines (QuickFIX-cpp v1.16.0, QuickFIX-J 3.0.1) are the conformance oracle for the corrected boundary, consistent with the project's per-release interop gate.
- The fix is confined to the acceptor `789` honor path; the initiator peer-ack honor path is already correct and is treated as a non-regression constraint, not a change target.
