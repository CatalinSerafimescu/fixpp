# Feature Specification: NextExpectedMsgSeqNum(789) fast session resume

**Feature Branch**: `027-next-expected-msgseqnum`
**Created**: 2026-06-07
**Status**: Draft
**Input**: User description: "NextExpectedMsgSeqNum(789) in Logon for fast session resume without a ResendRequest round-trip, both directions, per-session config knob default off (byte-identical no-op), per [FIX-SL §4.4.1]"

## Clarifications

### Session 2026-06-07

- Q: When the 789 knob is ON but the peer doesn't support/act on 789, how should fixpp behave? → A: **Match QuickFIX — no automatic `ResendRequest` fallback.** 789 is a both-peers-required feature: when enabled the session suppresses its own at-logon `ResendRequest` and relies on the peer's proactive resend; "both peers must enable 789" is a documented limitation (revises FR-009 from a fallback into a limitation).
- Q: Config knob shape? → A: **A single additive `SessionConfig` bool** (default false) controlling BOTH emit and honour (matches QFcpp `m_sendNextExpectedMsgSeqNum` / QFJ `EnableNextExpectedMsgSeqNum`).
- Q: Disposition when an inbound 789 exceeds our next-outbound? → A: **Send a `Logout` with explanatory text, then disconnect** (QFcpp `generateLogout(stream)+disconnect()`; QFJ `generateLogout("Tag 789 … higher than expected")`).
- Reference sweep (QuickFIX-cpp v1.16.0 + QuickFIX/J 3.0.1; Fix8 n/a) settled the rest:
  - **Version applicability**: 789 is shipped in QFcpp's `fix44/Logon.h` as a permitted FIX 4.4 Logon field, gated only by the config knob ⇒ implement now on fixpp's FIX 4.4 sessions; FIXT.1.1/5.0SP2 per-version gating defers to G4.
  - **Comparison basis**: an inbound `789 = X` is compared to our next-outbound N (QFcpp `getExpectedSenderNum()`): `X < N` ⇒ proactively resend `[X, N-1]`; `X == N` ⇒ in sync; `X > N` ⇒ the Logout+disconnect error above.
  - **Advertised value / off-by-one**: a side's own *initiating* Logon advertises plain `next-expected-inbound`; a Logon sent in *response* to a received Logon ALSO advertises plain `next-expected-inbound` (NO `+1`). NOTE (corrected at Gate A round 1 — supersedes the original reference-sweep note): the QFcpp `+1` in `generateLogon(aLogon)` ("the inbound Logon has not yet incremented the target seqnum at emit time") does **NOT** apply to fixpp — fixpp's acceptor `check_inbound` (`session.cpp:1571`) advances `next_inbound_` BEFORE the reply Logon is built (`:1745`), so `next_inbound_unsafe()` is already post-increment at the build site and plain (no `+1`) is correct. See Clarifications Session 2026-06-07 (Gate A round 1), FR-002, data-model E-OBO, research D-4. Plan/data-model detail.
  - **Resend semantics**: because fixpp persists messages (008 store), the proactive resend is a real retransmit (PossDup app messages + `SeqReset`-`GapFill` for admin), matching QFcpp `generateRetransmits` (the pure `generateSequenceReset` gap-fill is used only by non-persisting engines).

### Session 2026-06-07 (Gate A round 1)

- Q: fixpp has TWO inbound-Logon handlers (acceptor `NotConnected` @ `session.cpp:1508`, initiator `LogonSent` @ `:2755`), both currently fatal-on-too-high via `check_inbound` with no AwaitingResend on the Logon path; the cited `:1968-2009` arm is steady-state Active, OFF the reconnect-Logon path. Where does 789 honor + suppression actually live? → A: **In BOTH inbound-Logon handlers, at the correct sites.** Honor (read X, compare X vs N, resend `[X,N-1]` / noop / Logout) is added to each handler. Behind-side recovery (knob on AND the peer's Logon `MsgSeqNum` itself too-high) must NOT fall through to the fatal `check_inbound` (acceptor `:1571`, initiator `:2842`): when the knob is on, the too-high peer Logon is tolerated and the peer's proactive resend is admitted, with no at-logon `ResendRequest` emitted. The Active `:1968-2009` arm is the *steady-state* gap path and keeps emitting `ResendRequest` when the knob is off; it is a secondary suppression site, NOT the primary 789 honor site.
- Q: Acceptor advertised 789 off-by-one — `next_inbound_unsafe()` or `+1`? → A: **Plain `next_inbound_unsafe()`.** fixpp's acceptor calls `check_inbound(seq)` (`:1571`) which *advances* `next_inbound_` BEFORE the reply Logon is built (`:1745`). So at reply-build time `next_inbound_unsafe()` is already post-increment = the next inbound seqnum we expect. fixpp does NOT need QFcpp's `+1` (QFcpp increments later in its analogous `generateLogon(aLogon)` path). The initiator advertises plain `next_inbound_unsafe()` from `emit_initiator_logon_` (`:601`) — its own Logon is built before any peer Logon is received, so the counter is not yet advanced for this exchange and the plain read is correct.
- Q: Reset (141) + 789 advertised value — always 1? → A: **No, cause-dependent (mirrors the 024 split).** The advertised 789 reads `next_inbound_unsafe()` at the build site, so: initiator reset Logon (reset BEFORE build) ⇒ advertises **1**; acceptor reply under `reset_on_logon` (reset BEFORE `check_inbound`, then `check_inbound(1)` advances to 2) ⇒ advertises **2**; acceptor reply under 013-only received-141 (reset AFTER `check_inbound`, before reply build) ⇒ advertises **1**. The blanket "advertise 1" is affirmatively wrong for the `reset_on_logon` acceptor case.
- Q: Inbound 789 present-but-invalid (`789=` empty, `789=abc`, overflow)? → A: **Logout+disconnect (parity with X>N).** `parse_seqnum` (`:1266`) returns 0 on empty/non-digit/overflow; 0 would satisfy `X<N` and, with the walk's `begin>0?begin:1` clamp (`:2560`), replay `[1,N-1]` (the entire session history — a remote-triggerable amplification). An unparseable next-expected is as unrecoverable as an impossible one ⇒ a present-but-invalid 789 is treated as a sequence-integrity error: `build_logout` + disconnect, evaluated BEFORE the `X<N` compare.
- Q: Acceptor resend ordering for X<N? → A: **AFTER the reply Logon's `store_then_emit` succeeds (`:1766`)**, not at the 789 parse point — the proactive resend must follow the side's own Logon on the wire ("immediately after its own Logon").
- Q: Bidirectional simultaneous resend at logon (both sides have a gap)? → A: **No deadlock / no double recovery.** Each side reads the peer's 789 and resends transmit-only (the walk does not advance the live outbound counter); the resent frames are PossDup/GapFill and are admitted by the peer's behind-side tolerance, NOT re-classified as a fresh too-high gap. See data-model I-NEX-10.
- Q: Catalogue S-031 — flip to `done`? → A: **No, partial / 4.4-implementation-parity.** S-031 is versioned "5.0–5.0SP2, FIXT.1.1"; this slice delivers only FIX 4.4. Mark it implementation-parity for 4.4 with FIXT/5.0 versions explicitly outstanding (to G4).

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

- **Asymmetric support (both-sides-required)**: 789 assumes symmetric configuration. If our knob is enabled but the peer neither advertises nor acts on 789, the peer will not proactively backfill what we are missing, and (per FR-004) we have suppressed our own at-logon `ResendRequest` — so recovery of our missing messages does not complete. This is a documented limitation (matches QFcpp/QFJ); operators MUST enable 789 on both ends (FR-009). [Candidate B&L L-027 entry.]
- **Reset interaction (cause-dependent)**: when a Logon also carries `ResetSeqNumFlag(141)=Y` (the 024 reset knobs), the advertised next-expected reflects the post-reset state *as read at the build site*, which is cause-dependent (mirrors the 024 split): an initiator reset Logon advertises **1**; an acceptor reply under `reset_on_logon` advertises **2** (the reset runs before `check_inbound`, which then advances to 2); an acceptor reply under 013-only received-141 advertises **1** (the reset runs after `check_inbound`). The 789 and 141 mechanisms MUST be mutually consistent on the same Logon, but the value is NOT a blanket "1".
- **Invalid inbound 789 (present-but-unparseable)**: an inbound `789` that is empty, non-numeric, or overflows (the parser yields 0) MUST be treated as a sequence-integrity error — `Logout`+disconnect (parity with X>N) — evaluated BEFORE the `X<N` comparison, so a malformed 789 can never drive a `[1, N-1]` full-history replay.
- **No gap**: advertised next-expected equal to our next-outbound ⇒ no resend (steady-state reconnect).
- **Bidirectional simultaneous resend**: at reconnect both sides may have a gap and both advertise 789; each side resends what the other is missing. The two resend streams do not deadlock or double-count — the resend is transmit-only (does not advance the live outbound counter) and the peer admits the resent PossDup/GapFill frames via its behind-side tolerance rather than re-triggering a fresh recovery.
- **Lost proactive resend (no re-request at logon)**: 789 suppresses the *at-logon* `ResendRequest` only; if the peer's proactive resend frames are themselves lost, the residual gap is detected by the next inbound frame on the Active path, which still issues a `ResendRequest` (knob does not suppress the steady-state Active too-high arm). This is the recovery-of-last-resort; documented as L-027-2 should that path ever be unavailable.
- **Off-by-one discipline**: 789 is the next inbound sequence number the sender *expects* (last-received + 1), not the count received; the comparison and resend range are defined against this exact meaning.
- **No double recovery**: when the 789 proactive resend covers a range, the session MUST NOT also emit a `ResendRequest` for that same range.
- **Version applicability**: 789 is standardised in FIX 5.0+/FIXT.1.1 but is widely used as a permitted Logon field on FIX 4.x sessions (e.g., QuickFIX `EnableNextExpectedMsgSeqNum`). This slice targets fixpp's current FIX 4.4 sessions; FIXT/5.0SP2 per-version gating defers to G4. Catalogue row **S-031** (versioned "5.0–5.0SP2, FIXT.1.1") is therefore marked **implementation-parity for FIX 4.4**, NOT blanket `done` — the listed official versions stay outstanding until G4.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The system MUST expose a **per-session configuration knob** enabling `NextExpectedMsgSeqNum(789)` behaviour, **defaulting to disabled**.
- **FR-002**: When the knob is enabled, every outbound Logon MUST include `NextExpectedMsgSeqNum(789)` set to the next inbound `MsgSeqNum` the session expects to receive from the peer (last-received + 1; under a `ResetSeqNumFlag(141)` reset the exact advertised value is the post-reset counter read at the build site, which is cause-dependent — see Edge Cases).
- **FR-003**: When the knob is enabled and an inbound Logon carries `789 = X`, the session MUST compare X to its own next-outbound sequence number N and act: **X < N** ⇒ proactively resend the range `[X, N-1]` immediately after sending its own Logon, with no `ResendRequest`; **X == N** ⇒ resend nothing; **X > N** ⇒ raise the sequence-integrity error (FR-005).
- **FR-004**: When the knob is enabled, the session MUST rely on the 789 mechanism for at-logon recovery instead of `ResendRequest`: it MUST NOT issue a `ResendRequest` for a range it proactively resends (no double recovery), and when it detects it is itself behind at logon (the peer's Logon `MsgSeqNum` is higher than expected) it MUST suppress the `ResendRequest` it would otherwise send and rely on the peer's 789-driven proactive resend (QFcpp/QFJ behaviour).
- **FR-005**: When an inbound `789` exceeds the session's next-outbound sequence number (the peer claims receipt of messages never sent), the session MUST send a `Logout` carrying explanatory text (e.g. "NextExpectedMsgSeqNum too high, expecting N but received X") and then disconnect; it MUST NOT silently treat the session as in sync.
- **FR-006**: When the knob is **disabled** (default), the outbound Logon MUST be byte-for-byte identical to current behaviour (no `789` field), and an inbound `789` MUST be ignored with recovery proceeding via the existing `ResendRequest` mechanism — zero regression for existing sessions.
- **FR-007**: The behaviour MUST be symmetric across **both roles** — an initiator and an acceptor each emit and honour `789` identically when the knob is enabled.
- **FR-008**: The proactive resend MUST reuse the existing recovery/replay semantics: original application messages re-sent with `PossDupFlag(43)=Y` and the original `OrigSendingTime(122)`; contiguous administrative ranges collapsed into `SeqReset`-`GapFill`.
- **FR-009**: 789 is a **both-peers-required** feature: there is NO automatic `ResendRequest` fallback when the knob is enabled. If a peer does not advertise/act on `789`, recovery of the session's own missing messages will not complete via the 789 mechanism (the at-logon `ResendRequest` was suppressed per FR-004) — operators MUST enable the knob on both ends. This both-sides-required constraint is a documented limitation (matches QFcpp/QFJ, which assume symmetric configuration).
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
- **SC-004**: An inbound `789` above our next-outbound produces a `Logout` (with explanatory text) followed by disconnect (no silent accept), verified by a negative test.
- **SC-005**: Both roles (initiator and acceptor) emit and honour `789`, verified by both-role unit tests and a live interop cell against a counterparty configured with `NextExpectedMsgSeqNum` enabled.

## Assumptions

- **Default disabled** ⇒ the feature is a byte-identical no-op when unset; 789 is opt-in (matches QuickFIX `EnableNextExpectedMsgSeqNum`, which defaults off).
- **Scope is the Logon-time fast-resume mechanism only** — advertising 789, honouring a peer's 789 with a proactive resend, the X>N error, and the asymmetric-peer fallback. No change to steady-state sequencing, the store, or non-Logon recovery.
- **Reuses existing machinery**: the seqnum manager + message store already track next-inbound/next-outbound; the 013/021 recovery path already resends with PossDup and gap-fills admin ranges. This slice wires the Logon path to those, it does not build new recovery.
- **Version applicability (reference-settled)**: implemented now on fixpp's current FIX 4.4 sessions — 789 ships in QFcpp's `fix44/Logon.h` as a permitted Logon field, gated only by the knob; FIXT.1.1/5.0SP2 per-version gating deferred to G4.
- **Both-sides-required (clarified)**: no automatic `ResendRequest` fallback for a non-supporting peer; 789 must be enabled on both ends (FR-009 / Asymmetric-support edge case).
- **Dictionary support for tag 789 in Logon** (emit + parse) is assumed available or an additive, no-new-error-slot extension; the precise mechanism is a plan/HOW concern, not a scope change.
- **Interaction with the 024 `ResetSeqNumFlag(141)` knobs** is well-defined and consistent: the advertised next-expected reflects the post-reset counter read at the build site, which is **cause-dependent** (initiator reset Logon ⇒ 1; acceptor reply under `reset_on_logon` ⇒ 2; acceptor reply under 013-only received-141 ⇒ 1 — see Edge Cases / Clarifications Session 2026-06-07). This slice does not change the 024 reset behaviour.
- **No new wire field beyond the standard 789 tag, no new error slot, no codegen-emitter change, no C-ABI change**; one additive `SessionConfig` field.

## Normative References

- **`[FIX-SL §4.4.1] Using NextExpectedMsgSeqNum(789)`** — the catalogue authority (S-031). Defines the bidirectional advertise of the next-expected inbound sequence number in Logon, the receiver's `X < N` / `X == N` / `X > N` comparison against its next-outbound, the proactive resend that eliminates the `ResendRequest` round-trip (FR-002/FR-003/FR-004), and the impossible-expectation error (FR-005).
- **`[FIX-SL §4.4] Message recovery (ResendRequest / SeqReset-GapFill)`** — the standard recovery the 789 path optimises away when enabled, and which remains the recovery path when the knob is off (FR-006); the resend semantics it reuses (PossDup, gap-fill) are unchanged (FR-008).
- **`[FIX-SL §4.5.2] Sequence reset (ResetSeqNumFlag)`** — the cause-dependent post-reset advertised next-expected (1 or 2, per role × reset cause) interaction with the 024 reset knobs (Edge Cases / FR-002).
