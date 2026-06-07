# Research — NextExpectedMsgSeqNum(789) fast session resume

Phase 0 grounding. All NEEDS CLARIFICATION resolved (the spec's two deferred axes were settled at `/speckit-clarify` via the reference-engine sweep; the source sweep below settled the integration seams).

## D-1: Tag 789 is already in fixpp's FIX 4.4 dictionary + Logon (NO codegen)

- **Decision**: do NOT add tag 789 or touch codegen. `dictionaries/FIX44.xml:6128` defines `<field number='789' name='NextExpectedMsgSeqNum' type='SEQNUM'/>`, and the `Logon` message block (`:278`) lists `<field name='NextExpectedMsgSeqNum' required='N'/>` at `:284`.
- **Rationale**: inbound 789 is therefore a *defined, optional* Logon field — the inbound validator will not reject it as unknown; emit is a hand-written conditional append (build_logon is hand-written, D-3). FR-010 ("no codegen / no new error slot / no C-ABI") holds.
- **Alternatives considered**: adding 789 to a custom-fields extension (unnecessary — already present); a codegen pass (unnecessary).

## D-2: Single knob, emit+honor, default off (Clarifications Q2)

- **Decision**: one additive `SessionConfig` bool `enable_next_expected_msg_seq_num = false`, controlling BOTH emit and honor.
- **Rationale**: QFcpp `m_sendNextExpectedMsgSeqNum` (`Session.cpp:71` default false) and QFJ `enableNextExpectedMsgSeqNum` (`Session.java:443` default false, setting `EnableNextExpectedMsgSeqNum`) both use a single knob. A primitive bool needs no new header include ⇒ §XV.9 is N/A (contrast 026, which added a chrono include).
- **Alternatives considered**: split emit/honor knobs (no use case; doubles config + test surface; diverges from both engines).

## D-3: Emit — conditional 789 append in the hand-written build_logon, at both Logon-build sites

- **Decision**: add an optional `next_expected_seq` parameter to `build_logon` (`admin_messages.cpp:75`); when present, append tag 789 (`render_u32` + `w.append_raw(789, …)`) after the existing `ResetSeqNumFlag(141)` block. Pass it from the two Logon-build call sites: the initiator path `emit_initiator_logon_` (`session.cpp:601`) and the acceptor reply (`session.cpp:1745`).
- **Rationale**: `build_logon` already conditionally appends 141 via `bool reset_seqnum` — 789 is the exact parallel. Both build sites already exist; this is additive. QFcpp emits at the same two points (`generateLogon()` for the initiating Logon, `generateLogon(aLogon)` for the reply).
- **Alternatives considered**: a post-build field injection (fragile — re-frames BodyLength/CheckSum); emitting only on the initiator (breaks FR-007 both-roles).

## D-4: Advertised value + the acceptor-reply off-by-one (E-OBO)

- **Decision**: the advertised 789 = next-expected-**inbound**. Initiator's own Logon: `seqnum_mgr_.next_inbound_unsafe()`. Acceptor *reply* Logon: the next-expected-inbound computed **before** the inbound Logon's own seqnum has incremented the target counter — pin the exact fixpp expression in data-model **E-OBO** by reading fixpp's increment order at the reply site (`session.cpp:1745` relative to where the inbound Logon advances `next_inbound_`), and RED-witness it (do not copy QFcpp's literal `+1` blind).
- **Rationale**: QFcpp documents the hazard explicitly — `generateLogon(aLogon)` emits `getExpectedTargetNum() + 1` "because incoming Logon did not increment the target SeqNum yet". fixpp's increment timing may differ; the value MUST be derived from fixpp's actual state machine and proven by a witness, not assumed. This is the single highest off-by-one risk.
- **Alternatives considered**: blindly using `next_inbound_unsafe()` at both sites (wrong if fixpp increments at a different point → off-by-one advertise → peer resends the wrong range or false X>N).

## D-5: Honor — comparison basis + reuse the existing resend walk

- **Decision**: on an inbound Logon carrying `789=X`, with `N = seqnum_mgr_.peek_outbound()` (our next-outbound to assign): `X<N` ⇒ resend `[X, N-1]`; `X==N` ⇒ nothing; `X>N` ⇒ error (D-6). The `X<N` resend **reuses the existing `ResendRequest`-reply replay walk** (`session.cpp:2485+`): replay stored app messages with `PossDupFlag(43)=Y`+`OrigSendingTime(122)` keeping original `MsgSeqNum`, collapse admin/absent runs into one `SeqReset`-`GapFill`. Factor the walk into a helper invoked by both the `ResendRequest` handler and the 789 path if a clean seam exists; otherwise call it with `begin=X, end=N-1`.
- **Rationale**: `peek_outbound()` is exactly QFcpp's `getExpectedSenderNum()`. The replay walk is the FR-008 semantics already implemented and tested for `ResendRequest`; a second implementation would drift ([[feedback_half_restructure_symmetric_api]]). QFcpp's `sendRetransmitsAfterLogon` block does precisely `beginSeqNo=789, endSeqNo=getExpectedSenderNum()-1` → `generateRetransmits` (the persisting path; fixpp persists via the 008 store).
- **Alternatives considered**: a fresh resend loop for 789 (drift risk, duplicate gap-fill logic); resending `[X, N]` inclusive (off-by-one — N is the *next* to assign, not yet sent).

## D-6: Too-high (X>N) disposition — Logout(text)+disconnect (Clarifications Q3)

- **Decision**: when `X>N`, `build_logout` with explanatory text (e.g. "NextExpectedMsgSeqNum too high, expecting N but received X") then disconnect; do not enter the established state as in-sync.
- **Rationale**: both engines do exactly this — QFcpp `generateLogout(stream)+disconnect()` (`Session.cpp:230-237`), QFJ `generateLogout("Tag 789 (NextExpectedMsgSeqNum) is higher than expected …")` (`Session.java:2257-2260`). The peer is claiming receipt of messages we never sent — an unrecoverable integrity violation. `build_logout` already exists (`:1875/:2128/:2257`).
- **Alternatives considered**: silent disconnect (no diagnostic); session Reject + continue (wrong for an unrecoverable violation); neither matches the engines.

## D-7: Both-peers-required, NO automatic ResendRequest fallback (Clarifications Q1)

- **Decision**: when the knob is on, suppress the at-logon `ResendRequest` (the too-high arm `session.cpp:1964-1991`) and rely on the peer's 789-driven proactive resend. There is NO automatic fallback to `ResendRequest` when the peer doesn't support 789 — operators MUST enable 789 on both ends (limitation L-027-1). The suppression is gated strictly on the knob; the default path keeps emitting `ResendRequest` (013 recovery intact).
- **Rationale**: neither QFcpp nor QFJ has an automatic fallback — when `m_sendNextExpectedMsgSeqNum`/`enableNextExpectedMsgSeqNum` is set, the too-high path does `resendRange`+queue instead of `doTargetTooHigh` (the ResendRequest path). 789 is a both-sides-config feature by design. Adding a fallback would diverge from both references and require a detection/timeout mechanism + extra state. This corrected a divergence in the draft spec (old FR-009 promised a fallback).
- **Alternatives considered**: a timer-based fallback to ResendRequest (divergent, complex); keeping ResendRequest alongside 789 (double recovery — violates FR-004).

## D-8: Interaction with the 024 ResetSeqNumFlag(141) knobs

- **Decision**: when a Logon also carries `141=Y` (a 024 reset), the advertised 789 reflects the post-reset state (next-expected-inbound = 1). 789 is appended after the existing 141 block in `build_logon`; the value is read from the (already-reset) `seqnum_mgr_` state.
- **Rationale**: `[FIX-SL §4.5.2]` — after a reset both sides restart at 1; the advertise must be consistent. The 024 reset path already sets the seqnum state before the Logon is built; reading `next_inbound_unsafe()` after that yields 1 naturally. No new coupling code, but RED-witness the 141+789 combination.
- **Alternatives considered**: special-casing 789 under reset (unnecessary — falls out of reading post-reset state).

## D-9: Scope boundary — orthogonal to 025 hydrate-on-open

- **Decision**: 027 is the *Logon-time* advertise/honor mechanism only; it reads the in-memory `seqnum_mgr_` state. It does NOT add store→manager hydration at open() — that is the parked 025 dependency (cross-restart continuity). 789 fast-resume operates within a live reconnect where the seqnum state is already in memory.
- **Rationale**: keeps this a thin G3 slice; the cross-restart persistence is a separate, riskier 008-boundary slice (the G3 capstone, [[project_025_refresh_on_logon_bundling_plan]]).
- **Alternatives considered**: bundling hydration here (out of scope — that's 025).
