# Data Model — NextExpectedMsgSeqNum(789) fast session resume

No new persistent entities. The feature adds one config field, one inbound-header field capture, and a set of invariants over the existing sequence-number state. Sequence numbers use the existing `seqnum_t`. **Gate A round 1 re-derived these invariants against the real two-handler FSM (research D-0); the timing-dependent ones (E-OBO, I-NEX-8, I-NEX-9) are resolved from source, not deferred.**

## Entities

### E1 — `SessionConfig::enable_next_expected_msg_seq_num` (config)
- Type: `bool`, default **`false`** (explicit, [const §XII.5]).
- Additive primitive field in `include/fixpp/session/session_config.hpp`. No new include (§XV.9 N/A).
- Semantics: a single knob controlling BOTH emitting our 789 in Logon AND honoring a peer's inbound 789 (D-2).

### E2 — `NextExpectedMsgSeqNum(789)` Logon field (wire)
- Standard FIX tag 789, type SEQNUM. Already defined in `dictionaries/FIX44.xml:6128` and permitted (optional) in the `Logon` message (`:284`) — the **dictionary/codegen need not change**; the hand `scan_frame_header` switch must add a `case 789:` and `interpret_logon` continues to tolerate the optional field (D-1).
- Emitted in `build_logon` (conditional append after the `141` block, `admin_messages.cpp:150-155`); read on the inbound-Logon header scan.

### E3 — inbound `FrameHeader.next_expected_msg_seq_num` (parse capture)
- A `std::string_view` added to the real `FrameHeader` struct (`session.cpp:1152`, NOT a "LogonHeader" — no such struct exists), captured by a `case 789:` in the `scan_frame_header` switch (`:1213`). Empty view ⇒ field absent.
- Parsed to `seqnum_t X` via `parse_seqnum` (`:1266`) only when present AND the knob is on. `parse_seqnum` returns **0** on empty/non-digit/overflow ⇒ present-but-invalid (I-NEX-9).

## Key quantities (read-only from the existing `SeqnumManager`)

- **N = next-outbound** = `seqnum_mgr_.peek_outbound()` (= QFcpp `getExpectedSenderNum()`). Messages with seqnum `[1, N-1]` have been sent; `N` is the next to assign. This is an **outbound** counter.
- **next-expected-inbound** = `seqnum_mgr_.next_inbound_unsafe()` (= the value we advertise as our 789). This is a separate **inbound** counter. The two are distinct — see I-NEX-11.

## The two inbound-Logon handlers (research D-0)

| Role | Handler | check_inbound (fatal site) | reply/own Logon built | honor + suppression added |
|------|---------|----------------------------|------------------------|---------------------------|
| Acceptor | `NotConnected` `:1508` | `:1571` (advances `next_inbound_`) | reply @ `:1745`, emit @ `:1766` | read 789 from inbound; behind-side tolerance gates the `:1571` fatal when knob on; X<N resend AFTER `:1766` |
| Initiator | `LogonSent` `:2755` | `:2842` | own Logon emitted earlier @ `:601` | read peer's 789 from Logon-ack; behind-side tolerance gates the `:2842` fatal when knob on; X<N resend after processing the ack |

The Active too-high arm (`:1968-2009`) is OFF the reconnect-Logon path; it is the steady-state gap path / recovery-of-last-resort (I-NEX-10).

## Invariants

- **I-NEX-1 (advertise = next-expected-inbound)**: when the knob is on, every outbound Logon carries `789 = seqnum_mgr_.next_inbound_unsafe()` (plain, NO `+1`), at BOTH build sites (initiator `:601`, acceptor reply `:1745`). See E-OBO for why the acceptor needs no `+1`.
- **I-NEX-2 (comparison basis)**: an inbound `789 = X` (when valid, I-NEX-9 first) is compared to `N = peek_outbound()`. `X < N` ⇒ resend `[X, N-1]`; `X == N` ⇒ no resend; `X > N` ⇒ error (I-NEX-4). The resend range is `[X, N-1]` inclusive — NOT `[X, N]` (N is the next-to-assign, not yet sent).
- **I-NEX-3 (resend reuse = the `replay_outbound_range_` extraction, TWO-value end model)**: the `X<N` proactive resend goes through `replay_outbound_range_(begin=X, requested_end=N-1, end_is_through_current=true)`, factored from the inline `ResendRequest`-reply walk (`session.cpp:2485-2635`, research D-5): stored app messages replayed with `PossDupFlag(43)=Y`+`OrigSendingTime(122)` keeping their original `MsgSeqNum`; admin/absent runs collapsed into one `SeqReset`-`GapFill(123=Y, 36=<next live>)`. Transmit-only — does NOT advance the live outbound counter, not re-stored. **The helper signature MUST preserve the walk's TWO-value end model** — `(begin, requested_end, end_is_through_current)`, NOT a lossy single `(begin, end_inclusive)`: the walk needs both `eff_end` (clamped store-walk bound = `min(requested, our_last)`) AND `requested_end` (the basis for the empty/short-store GapFill `NewSeqNo = end_is_through_current ? peek_outbound() : requested_end+1`, mirroring `:2558-2559`). A single `end_inclusive=eff_end` arg loses `requested_end` and regresses the shipped 013 ResendRequest path (peer requests `[10,20]`, store through 5 ⇒ must GapFill `NewSeqNo=21`, not 6). The helper owns the `EndSeqNo=0 ⇒ through-current` + clamp resolution internally; the ResendRequest caller passes `requested_end=rr_end, end_is_through_current=(rr_end==0)`. The helper returns `expected_t<void>`; the FSM `Disconnected` transition on failure is owned by the CALLER, not the helper (the inline block's embedded `record_state_transition_(Disconnected)` early-returns move out). **Exactly one walk implementation** (invariant-count regression, [[feedback_half_restructure_symmetric_api]]); tests cover explicit-end-beyond-store AND `EndSeqNo=0`-empty-store for BOTH callers.
- **I-NEX-4 (integrity error)**: `X > N` ⇒ `build_logout("NextExpectedMsgSeqNum too high, expecting N but received X")` then disconnect; MUST NOT advance to established as in-sync (FR-005, D-6). Applies in both handlers.
- **I-NEX-5 (behind-side recovery — held-Logon consume model + no double recovery)**: when the knob is on AND the peer's Logon `MsgSeqNum = X_logon` is itself too-high, the handler MUST NOT fall through to the fatal `check_inbound` (acceptor `:1571`, initiator `:2842`). The held-Logon counter bookkeeping is **formulation A (do-NOT-advance):** the handler leaves `next_inbound_` at its current value X (= what we still expect from the peer), does NOT call `set_next_inbound` and does NOT advance for the held too-high Logon's own seqnum, emits NO at-logon `ResendRequest`, and proceeds toward Active. The peer's proactive resend of `[X, peer_N-1]` (driven by OUR advertised 789) is then admitted **in-sequence through the existing Active in-sequence path** (`seqnum_manager.cpp:85-87` advances by 1 per PossDup app frame; admin/Logon ranges jump via `apply_inbound_sequence_reset` @ `:2227`), carrying `next_inbound_` forward frame-by-frame to `peer_N` (the peer's true next-outbound). The held Logon's own seqnum is **superseded by being inside the peer's resend range** — the resend ALWAYS spans `X_logon` because `X_logon ≤ peer_N-1` (the Logon is the most recent thing the peer sent; any post-Logon app messages only raise `peer_N`). It is NOT separately counted. After the fill `next_inbound_ == peer_N`, and the next live frame (seq `peer_N`) is in-sequence. **`set_next_inbound(X_logon+1)` is WRONG here** — it would make the peer's resent frames at `[X, X_logon]` (which span `X_logon`) all arrive too-low (`< X_logon+1`) → fatal too-low (`seqnum_manager.cpp:67-70`) → breaks the feature. The recovery uses the ordinary Active in-sequence path, NOT an explicit `enter_awaiting_resend` entry (the resent frames are in-sequence relative to the un-advanced `next_inbound_=X`, so AwaitingResend — needed only for a too-low PossDup interleave, already handled by 021/022 Arm A — is not engaged for the in-sequence fill). When the knob is OFF, the fatal path + `ResendRequest` recovery (013) is unchanged (D-7).
- **I-NEX-6 (both-peers-required, no fallback)**: there is no automatic `ResendRequest` fallback at logon. If the knob is on but the peer doesn't advertise/act on 789, our own at-logon gap does not fast-recover (the at-logon ResendRequest was suppressed per I-NEX-5). Documented limitation L-027-1 (D-7). (The Active path still self-heals on the next frame — I-NEX-10.)
- **I-NEX-7 (default-off byte-identity)**: with the knob off, the outbound Logon contains no 789 field and is byte-identical to the pre-feature baseline; an inbound 789 is ignored and standard recovery applies (FR-006/SC-002).
- **I-NEX-8 (reset consistency — cause-dependent)**: when a Logon also carries `141=Y` (024 reset), the advertised 789 = `next_inbound_unsafe()` read at the build site, which is cause-dependent (see Reset table). NOT a blanket "1".
- **I-NEX-9 (present-but-invalid 789)**: a present 789 whose `parse_seqnum` yields 0 (empty/non-digit/overflow) ⇒ `build_logout`+disconnect (parity with I-NEX-4), evaluated BEFORE the `X<N` compare so a malformed value can never drive a `[1, N-1]` full-history replay via the clamp at `:2560` (D-10).
- **I-NEX-10 (lost-resend self-heal)**: 789 suppresses only the *at-logon* ResendRequest; the steady-state Active too-high arm (`:1968-2009`) stays active, so a lost proactive resend is re-requested on the next inbound frame (recovery-of-last-resort, D-11). A future suppression of the Active arm would create a never-recover hole ⇒ L-027-2.
- **I-NEX-11 (two distinct counters)**: the 789 comparison uses `N = peek_outbound()` (OUTBOUND), while the existing Active too-high arm computes `next_expected = next_inbound_unsafe()` (INBOUND) at `:1968`. These are different counters; an implementer must NOT compare X against `next_inbound_unsafe()`. The honor decision and the suppression site operate on different quantities (New-P2#2, D-0).
- **I-NEX-12 (behind-side post-recovery counter target)**: after a knob-on behind-side recovery (I-NEX-5), the LOCAL `next_inbound_` MUST equal `peer_N` (the peer's next-outbound, = the first seqnum the peer will send live after its proactive resend), NOT `X_logon + 1`. The counter is carried there by the in-sequence admission of the peer's `[X, peer_N-1]` resend, not by an explicit `set_next_inbound`. RED witness `BehindSide_KnobOn_AdmitsPeerResend_NoFatalDisconnect` MUST assert `final next_inbound_ == peer_N` (the witness sets up a measurable `peer_N` distinct from `X_logon+1` — i.e. ≥1 post-Logon app frame in the resend — so the two values diverge and the assertion is not a coincidental pass).

## Reset × role × cause — advertised 789 (I-NEX-8)

| Role / cause | Reset site vs build | Advertised 789 | Witness |
|---|---|---|---|
| Initiator reset Logon (`reset_on_logon`) | reset `:559` BEFORE build `:601` | **1** | `Reset_InitiatorResetLogon_Advertises1` |
| Acceptor reply, `reset_on_logon` | reset `:1562` BEFORE `check_inbound(1)` (→2) BEFORE build `:1745` | **2** | `Reset_AcceptorReplyResetOnLogon_Advertises2` |
| Acceptor reply, 013-only received-141 | `check_inbound` `:1571` BEFORE reset `:1718` BEFORE build `:1745` | **1** | `Reset_AcceptorReplyReceived141_Advertises1` |

## E-OBO — acceptor-reply off-by-one — RESOLVED: plain `next_inbound_unsafe()` (no `+1`)

The acceptor's *reply* Logon (`session.cpp:1745`) is built AFTER `check_inbound(seq)` (`:1571`), which **advances `next_inbound_` on success**. Therefore at the reply-build site `next_inbound_unsafe()` is already post-increment = "the seqnum we next expect from the peer AFTER having received this Logon". **The acceptor advertises plain `next_inbound_unsafe()` — NO `+1`.**

- QFcpp's analogue (`generateLogon(aLogon)`) uses `getExpectedTargetNum() + 1` "because incoming Logon did not increment the target SeqNum yet" — but **fixpp increments earlier** (`check_inbound` @ `:1571` precedes reply build @ `:1745`), so copying QFcpp's `+1` would advertise one too high → false X>N or wrong resend range at the peer.
- The initiator (`:601`) builds its own Logon before any peer Logon, so `next_inbound_` is not yet advanced for the exchange — plain `next_inbound_unsafe()` is correct there too.
- **RED-witness it**: a both-role round-trip where the acceptor's advertised 789 is checked against the peer's actual next send (`Emit_AcceptorReply_AdvertisesNextInboundNoPlusOne`).

## RED witnesses (TDD targets)

| Witness | Asserts |
|---|---|
| `Emit_Initiator_AdvertisesNextExpectedInbound` | initiator Logon `789 == next_inbound_unsafe()` (I-NEX-1) |
| `Emit_AcceptorReply_AdvertisesNextInboundNoPlusOne` | acceptor reply `789 == next_inbound_unsafe()` post-`check_inbound` (no `+1`); equals the peer's actual next send (E-OBO) |
| `Honor_Acceptor_XltN_ResendsExactRange_AfterReply_NoResendRequest` | acceptor: `X<N` ⇒ resend exactly `[X, N-1]` AFTER the reply Logon emit (`:1766`), PossDup app + GapFill admin, zero ResendRequest (I-NEX-2/3/5, RC#4) |
| `Honor_Initiator_XltN_ResendsExactRange_NoResendRequest` | initiator: `X<N` ⇒ resend exactly `[X, N-1]` after processing the Logon-ack, zero ResendRequest (I-NEX-2/3, both-role) |
| `Honor_XeqN_NoResend` | `X==N` ⇒ no resend (I-NEX-2), both roles |
| `Honor_XgtN_LogoutTextThenDisconnect` | `X>N` ⇒ Logout with text + disconnect, not established (I-NEX-4), both roles |
| `Honor_Invalid789_LogoutThenDisconnect` | `789=` / `789=abc` / overflow ⇒ Logout+disconnect, NO `[1,N-1]` replay (I-NEX-9) |
| `BehindSide_KnobOn_AdmitsPeerResend_NoFatalDisconnect` | knob on + peer Logon MsgSeqNum too-high ⇒ NOT fatal at `check_inbound`; `next_inbound_` left at X (no `set_next_inbound`); admits the peer's `[X, peer_N-1]` resend in-sequence; **final `next_inbound_ == peer_N`** (NOT `X_logon+1` — peer_N set ≥1 frame beyond the Logon so the two diverge, I-NEX-5/I-NEX-12); no at-logon ResendRequest, both roles |
| `Bidirectional_BothGaps_RecoverNoDoubleRecovery` | both sides have a gap ⇒ each leaves its inbound counter at X, admits the peer's `[X, peer_N-1]` resend in-sequence (post-Active) to `peer_N`, zero ResendRequest, no duplicate delivery, no double-count (I-NEX-5/I-NEX-12, D-12) |
| `LostResend_SelfHealsViaActiveArm` | knob on, proactive resend dropped ⇒ next inbound frame triggers Active-path ResendRequest (I-NEX-10) |
| `DefaultOff_ByteIdenticalLogon_InboundIgnored` | knob off ⇒ no 789 emitted (byte-identical) + inbound 789 ignored + ResendRequest still used (I-NEX-7) |
| `Suppression_KnobOn_NoAtLogonResendRequest_KnobOff_Yes` | knob-on at-logon gap emits no ResendRequest; knob-off still does (I-NEX-5, 013 regression guard) |
| `Reset_InitiatorResetLogon_Advertises1` | initiator reset Logon ⇒ 789 == 1 (I-NEX-8) |
| `Reset_AcceptorReplyResetOnLogon_Advertises2` | acceptor reply under `reset_on_logon` ⇒ 789 == 2 (I-NEX-8) |
| `Reset_AcceptorReplyReceived141_Advertises1` | acceptor reply under received-141-only ⇒ 789 == 1 (I-NEX-8) |
| `WalkExtraction_SingleImplementation` | `replay_outbound_range_` is the sole store-walk; ResendRequest handler + 789 path both call it (I-NEX-3) |
| `WalkExtraction_TwoValueEnd_ExplicitEndBeyondStore` | request `[10,20]`, store through 5 ⇒ GapFill `NewSeqNo=21` (NOT 6) — guards the 013 regression; asserted via BOTH the ResendRequest caller and the 789 caller (I-NEX-3) |
| `WalkExtraction_TwoValueEnd_EndSeqNo0_EmptyStore` | `EndSeqNo=0`/through-current, empty store ⇒ GapFill `NewSeqNo=peek_outbound()`, BOTH callers (I-NEX-3) |
| `NoHeap_EmitAndResendPath_789Entry` | emit append + proactive resend via the 789 entry allocate zero heap (mallocnesia) — exercises the 789 entry specifically, not just the ResendRequest entry |
