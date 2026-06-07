# Feature Specification: NextExpectedMsgSeqNum(789) fast session resume

**Feature Branch**: `027-next-expected-msgseqnum`
**Created**: 2026-06-07
**Status**: Draft
**Input**: User description: "NextExpectedMsgSeqNum(789) in Logon for fast session resume without a ResendRequest round-trip, both directions, per-session config knob default off (byte-identical no-op), per [FIX-SL §4.4.1]"

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Fast bidirectional resume after a gap (Priority: P1)

Two FIX peers that both support `NextExpectedMsgSeqNum(789)` lose their connection while one or both have sent messages the other has not yet acknowledged. On reconnect, each side's Logon advertises the next inbound sequence number it expects from the other. Each side reads the peer's advertised value and **proactively resends** exactly the messages the peer is missing, immediately after its own Logon — so the whole gap is recovered within the Logon exchange, with **no `ResendRequest` round-trip**. An operator running latency-sensitive sessions (or connecting to a venue that mandates 789) enables a per-session knob to get this behaviour; with the knob off, the session behaves exactly as today.

**Why this priority**: This is the feature's reason to exist — eliminating the `ResendRequest`→resend round-trip at session resume. Enabling the knob and observing recovery complete in one Logon exchange delivers the whole primary value.

**Independent Test**: Configure two sessions with the knob enabled, force a sequence gap (send messages, drop the link before acknowledgement), reconnect, and assert (a) all missed messages are delivered in order, (b) no `ResendRequest` appears on the wire, and (c) the session reaches the established state.

**Acceptance Scenarios**:

1. **Given** both sides have the 789 knob enabled and one side is behind by K messages, **When** they reconnect, **Then** the behind side receives all K missed messages (PossDup) within the Logon exchange and **no `ResendRequest` is sent** by either party.
2. **Given** the 789 knob enabled and the peer's advertised next-expected equals our next-outbound, **When** we process the peer's Logon, **Then** we resend nothing (already in sync).
3. **Given** the 789 knob enabled and a gap that spans both application and administrative messages, **When** we proactively resend, **Then** application messages are re-sent with `PossDupFlag(43)=Y` + `OrigSendingTime(122)` and administrative ranges are collapsed into `SeqReset`-`GapFill`.

---

### User Story 2 - Default off, byte-identical no-op (Priority: P1)

An existing operator who does not opt in must see **no change whatsoever**. With the knob at its default (disabled), fixpp's outbound Logon is byte-for-byte identical to today (no 789 field), and a peer that sends 789 is handled by the standard recovery path (`ResendRequest`) exactly as before.

**Why this priority**: Zero regression for every existing session is co-equal with the feature itself — 789 is opt-in, and the established `ResendRequest`-driven recovery (013/021) must be untouched when the knob is off.

**Independent Test**: With the default configuration, capture an outbound Logon and assert it is byte-identical to the pre-feature baseline (no 789 tag); feed an inbound Logon carrying 789 and assert the session ignores the field and recovers via the existing `ResendRequest` path.

**Acceptance Scenarios**:

1. **Given** the default (disabled) configuration, **When** the session emits a Logon, **Then** the Logon contains no `NextExpectedMsgSeqNum(789)` field and is byte-identical to current behaviour.
2. **Given** the default configuration and an inbound Logon that carries 789, **When** the session processes it, **Then** the 789 value is ignored and any required recovery proceeds via the existing `ResendRequest` mechanism.
3. **Given** the default configuration, **When** the full existing session/recovery/logon regression suite runs, **Then** every witness remains green.

---

### User Story 3 - Sequence-integrity error on impossible expectation (Priority: P2)

If a peer's advertised next-expected sequence number is **higher** than the number of messages we have actually sent, the peer is claiming to have received messages we never sent — an unrecoverable sequence-integrity violation. fixpp must treat this as a session-level error rather than silently accepting it.

**Why this priority**: A safety/correctness guard on the new inbound path. It is not the happy-path value, but silently accepting an impossible expectation would corrupt the session — so it must be handled, just at a lower priority than the core resume.

**Independent Test**: Feed an inbound Logon whose 789 exceeds our next-outbound and assert the session raises the defined error disposition (disconnect / logout) and does not enter the established state as if in sync.

**Acceptance Scenarios**:

1. **Given** the 789 knob enabled and an inbound Logon whose advertised next-expected exceeds our next-outbound, **When** we process it, **Then** the session is terminated with the defined sequence-integrity error disposition (no silent accept).

---

### Edge Cases

- **Asymmetric support**: if our knob is enabled but the peer neither advertises nor acts on 789, the peer will not proactively backfill what we are missing — the session MUST fall back to the standard `ResendRequest` recovery for our own gap, so enabling 789 never makes recovery *worse* than today.
- **Reset interaction**: when a Logon also carries `ResetSeqNumFlag(141)=Y` (the 024 reset knobs), the advertised next-expected MUST reflect the post-reset state (next-expected = 1); the 789 and 141 mechanisms MUST be mutually consistent on the same Logon.
- **No gap**: advertised next-expected equal to our next-outbound ⇒ no resend (steady-state reconnect).
- **Off-by-one discipline**: 789 is the next inbound sequence number the sender *expects* (last-received + 1), not the count received; the comparison and resend range are defined against this exact meaning.
- **No double recovery**: when the 789 proactive resend covers a range, the session MUST NOT also emit a `ResendRequest` for that same range.
- **Version applicability**: 789 is standardised in FIX 5.0+/FIXT.1.1 but is widely used as a permitted Logon field on FIX 4.x sessions (e.g., QuickFIX `EnableNextExpectedMsgSeqNum`). This slice targets fixpp's current FIX 4.4 sessions; FIXT/5.0SP2 per-version gating defers to G4.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The system MUST expose a **per-session configuration knob** enabling `NextExpectedMsgSeqNum(789)` behaviour, **defaulting to disabled**.
- **FR-002**: When the knob is enabled, every outbound Logon MUST include `NextExpectedMsgSeqNum(789)` set to the next inbound `MsgSeqNum` the session expects to receive from the peer (last-received + 1; = 1 after a sequence reset).
- **FR-003**: When the knob is enabled and an inbound Logon carries `789 = X`, the session MUST compare X to its own next-outbound sequence number N and act: **X < N** ⇒ proactively resend the range `[X, N-1]` immediately after sending its own Logon, with no `ResendRequest`; **X == N** ⇒ resend nothing; **X > N** ⇒ raise the sequence-integrity error (FR-005).
- **FR-004**: When the 789 proactive resend covers a gap, the session MUST NOT additionally issue a `ResendRequest` for that same range (no double recovery).
- **FR-005**: When an inbound `789` exceeds the session's next-outbound sequence number, the session MUST handle it as a defined session-level sequence-integrity error (terminate the session), never silently treat the session as in sync.
- **FR-006**: When the knob is **disabled** (default), the outbound Logon MUST be byte-for-byte identical to current behaviour (no `789` field), and an inbound `789` MUST be ignored with recovery proceeding via the existing `ResendRequest` mechanism — zero regression for existing sessions.
- **FR-007**: The behaviour MUST be symmetric across **both roles** — an initiator and an acceptor each emit and honour `789` identically when the knob is enabled.
- **FR-008**: The proactive resend MUST reuse the existing recovery/replay semantics: original application messages re-sent with `PossDupFlag(43)=Y` and the original `OrigSendingTime(122)`; contiguous administrative ranges collapsed into `SeqReset`-`GapFill`.
- **FR-009**: When the knob is enabled but the peer does not advertise/act on `789`, the session MUST fall back to standard `ResendRequest` recovery for its own missing messages, so resume is never less reliable than the knob-off behaviour.
- **FR-010**: The feature MUST NOT introduce a new error slot or a C-ABI surface change; it adds one additive `SessionConfig` field and uses the standard `789` tag in the Logon message.

### Key Entities *(include if feature involves data)*

- **NextExpectedMsgSeqNum config knob**: additive per-session boolean (default disabled) selecting the 789 advertise+honour behaviour.
- **`NextExpectedMsgSeqNum(789)`**: the standard FIX Logon field carrying the sender's next-expected inbound sequence number.
- **Next-expected inbound / next-outbound sequence numbers**: the session's sequence-number state (managed by the existing seqnum manager + message store) read to compute the advertised 789 and to drive the proactive-resend comparison.
- **Replay/recovery machinery**: the existing resend path (PossDup re-send, `SeqReset`-`GapFill` for admin) reused to satisfy the proactive backfill.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: With both peers' knob enabled and one side behind by K messages, reconnect delivers all K missed messages in order using **only the Logon exchange — zero `ResendRequest` messages on the wire** — verified by an automated test that counts wire messages.
- **SC-002**: With the default (disabled) configuration, 100% of existing session/recovery/logon regression witnesses remain green and the outbound Logon is byte-identical to the pre-feature baseline.
- **SC-003**: An inbound `789` below our next-outbound triggers a proactive resend of exactly the missing range (PossDup app messages, admin gap-filled) and no `ResendRequest`, verified by automated tests for both roles.
- **SC-004**: An inbound `789` above our next-outbound produces the defined session-integrity error (no silent accept), verified by a negative test.
- **SC-005**: Both roles (initiator and acceptor) emit and honour `789`, verified by both-role unit tests and a live interop cell against a counterparty configured with `NextExpectedMsgSeqNum` enabled.

## Assumptions

- **Default disabled** ⇒ the feature is a byte-identical no-op when unset; 789 is opt-in (matches QuickFIX `EnableNextExpectedMsgSeqNum`, which defaults off).
- **Scope is the Logon-time fast-resume mechanism only** — advertising 789, honouring a peer's 789 with a proactive resend, the X>N error, and the asymmetric-peer fallback. No change to steady-state sequencing, the store, or non-Logon recovery.
- **Reuses existing machinery**: the seqnum manager + message store already track next-inbound/next-outbound; the 013/021 recovery path already resends with PossDup and gap-fills admin ranges. This slice wires the Logon path to those, it does not build new recovery.
- **Version applicability (informed default, to confirm at clarify)**: implemented for fixpp's current FIX 4.4 sessions, treating 789 as a permitted Logon field (QuickFIX 4.x parity); FIXT.1.1/5.0SP2 per-version gating deferred to G4.
- **Dictionary support for tag 789 in Logon** (emit + parse) is assumed available or an additive, no-new-error-slot extension; the precise mechanism is a plan/HOW concern, not a scope change.
- **Interaction with the 024 `ResetSeqNumFlag(141)` knobs** is well-defined and consistent (post-reset advertised next-expected = 1); this slice does not change the 024 reset behaviour.
- **No new wire field beyond the standard 789 tag, no new error slot, no codegen-emitter change, no C-ABI change**; one additive `SessionConfig` field.

## Normative References

- **`[FIX-SL §4.4.1] Using NextExpectedMsgSeqNum(789)`** — the catalogue authority (S-031). Defines the bidirectional advertise of the next-expected inbound sequence number in Logon, the receiver's `X < N` / `X == N` / `X > N` comparison against its next-outbound, the proactive resend that eliminates the `ResendRequest` round-trip (FR-002/FR-003/FR-004), and the impossible-expectation error (FR-005).
- **`[FIX-SL §4.4] Message recovery (ResendRequest / SeqReset-GapFill)`** — the standard recovery the 789 path optimises and falls back to (FR-006/FR-008/FR-009); the resend semantics (PossDup, gap-fill) are unchanged.
- **`[FIX-SL §4.5.2] Sequence reset (ResetSeqNumFlag)`** — the post-reset next-expected = 1 interaction with the 024 reset knobs (Edge Cases / FR-002).
