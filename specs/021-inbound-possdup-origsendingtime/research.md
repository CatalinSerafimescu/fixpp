# Phase 0 Research: Inbound PossDup / OrigSendingTime Handling

**Feature**: 021-inbound-possdup-origsendingtime | **Date**: 2026-06-04

## Method

Reference-engine sweep performed via **CodeGraph** against the two live interop targets at their pinned tags:
- **QuickFIX-cpp v1.16.0** — `src/C++/Session.cpp` (`Session::verify` → `doTargetTooLow` → `doPossDup`)
- **QuickFIX-J 3.0.1** — `quickfixj-core/src/main/java/quickfix/Session.java` (`verify` → `doTargetTooLow` → `validatePossDup`)

No `NEEDS CLARIFICATION` markers remained from `/speckit-clarify`. This document records the canonical disposition each decision follows and why. **Three** engine divergences govern scope (see D2, D2b, D7); the send-path AllowPossDup default (D7) is DEFERRED out of this slice.

## Normative References

Per `[const §VI.5]` — exact coverage-index refs matching the catalogue rows:

- `[FIX-SL §4.8.4] Possible duplicates` — `PossDupFlag(43)` + `OrigSendingTime(122)` retransmission semantics during ResendRequest replay (the S-010/S-033 anchor; `feature-catalogue.md:30,345`).
- `[FIX-SL §4.5.4] SessionRejectReason` — the session-level `Reject(35=3)` field set actually emitted by `build_reject` (`371=RefTagID`, `372=RefMsgType`, `373=SessionRejectReason`) and the `RequiredTagMissing(1)` / `SendingTimeAccuracyProblem(10)` reason values used by Arms C/D.
- Engine grounding: QuickFIX-cpp v1.16.0 `Session::doPossDup`; QuickFIX-J 3.0.1 `Session.validatePossDup` (both cited inline below).

## Decisions

### D1 — Arm A: too-low + PossDup + valid OrigSendingTime → tolerate, no seqnum advance
**Decision**: A message with `MsgSeqNum < expected`, `PossDupFlag(43)=Y`, and a present, valid `OrigSendingTime(122)` is tolerated: the session stays `Active`, the expected inbound sequence number is **not advanced**, and the message is not re-applied.
**Rationale**: Both engines agree. QFJ `doTargetTooLow` returns `validatePossDup(msg)` and, on success, `verify` returns false (skip default handling) with no `incrNextTargetMsgSeqNum`. QuickFIX-cpp `doTargetTooLow` returns `doPossDup(msg)` similarly with no seqnum increment. A too-low message was already processed once, so advancing or re-applying would corrupt the sequence contract.
**Alternatives rejected**: advancing the counter (would create a phantom gap); reprocessing (double-application).

### D2 — Arm A application-message disposition: configurable, default DROP
**Decision**: For a *validated* too-low possible-duplicate **application** message, disposition is governed by a new `SessionConfig` knob `redeliver_poss_dup` (default `false`): default **drops** it (no `Application::fromApp`), opt-in **redelivers** it to `fromApp` flagged as a possible duplicate. **Administrative** duplicates are always ignored regardless. Neither setting advances the seqnum or disconnects.
**Rationale**: This is the **only** inbound divergence between the engines — QFJ silently drops (no callback); QuickFIX-cpp delivers to the application callback. Resolved in Clarifications (Session 2026-06-04) as configurable, defaulting to the safer QFJ behavior (a too-low message was already delivered once; redelivery would double-deliver unless the app dedups). fixpp has the 019/020 `Application` layer, so the opt-in redeliver path reuses the existing inbound `fromApp` invocation site with no new concurrency surface (L-019-3 single-thread confinement).
**Alternatives rejected**: hard-coded drop (loses QuickFIX-cpp parity for integrators who want it); hard-coded redeliver (forces dedup burden on every integrator).

### D2b — PossDup validation scope: ALL `43=Y` non-SequenceReset inbound (third divergence)
**Decision**: OrigSendingTime validation (Arms C/D) runs for **any** inbound `43=Y` non-`SequenceReset(35=4)` message — regardless of its `MsgSeqNum` relative to expected, including the at-expected (`34 == expected`) case — and runs **before** the too-low seqnum-tolerance disposition (Arms A/B).
**Rationale**: This is a **third** engine divergence (the research previously claimed only two). **QuickFIX-J validates PossDup at the expected sequence number** — `Session.java:1843` `if (isPossibleDuplicate(msg) && !validatePossDup(msg)) { return false; }` runs after the too-high/too-low gate, i.e. for the in-sequence case. **QuickFIX-cpp does not** — it reaches `doPossDup` only via `doTargetTooLow`, with no at-expected `validatePossDup` analogue. fixpp chooses the **QFJ superset**: validate whenever `43=Y` (non-reset), which is safer (a malformed at-expected duplicate is rejected rather than silently processed) and is the only reading consistent with FR-004/005 being seqnum-independent. Without this, an in-sequence `43=Y` with a missing/late `122` would bypass Arms C/D entirely.
**Alternatives rejected**: gate validation on `MsgSeqNum < expected` only (QFcpp behavior — silently accepts a malformed at-expected dup; contradicts FR-004/005).

### D3 — Arm B: too-low without PossDup → fatal, preserved unchanged (NO Logout wire frame)
**Decision**: `MsgSeqNum < expected` with no `PossDupFlag=Y` continues to transition to `Disconnected` exactly as today (`session.cpp:1860-1862`, `session_seqnum_too_low=69`). The current arm emits **no Logout wire frame** — it calls only `record_state_transition_(fsm_state::Disconnected)` (which fires the `onLogout` *callback* but builds no Logout message). The PossDup branch is inserted **ahead** of this arm and only intercepts `43=Y`.
**Rationale**: fixpp's existing, correct behavior is a bare `→Disconnected` with no Logout frame; the feature must preserve it byte-identical (INV-2 / SC-003 regression pin). NOTE: QuickFIX-cpp/J *do* `generateLogout(text)` here — adding a Logout-with-text to match them would be a *separate, deliberate* production behavior change with its own wire test + complexity-tracking row, NOT part of this slice. The too-low Heartbeat(0) silent-ignore exception (`session.cpp:1856-1859`) is independent and unchanged.
**Alternatives rejected**: replacing the arm (would lose the fatal-too-low guarantee); adding a Logout frame here (an unflagged production behavior change to the proven 005/013 FSM — out of scope).

### D4 — Arm C: PossDup + OrigSendingTime missing → Reject(373=1 RequiredTagMissing, 371=122), survive
**Decision**: A `43=Y` message (non-SequenceReset) lacking `OrigSendingTime(122)` → session-level `Reject(35=3)` with `RefTagID(371)=122` and `SessionRejectReason(373)=RequiredTagMissing(1)`; the session **survives** (no disconnect).
**Rationale**: The real `build_reject` (`admin_messages.cpp:540`, body `:605-636`) emits `371=RefTagID`, `372=RefMsgType`, `373=SessionRejectReason` (and `45=RefSeqNum`); `380` is **BusinessRejectReason** (used only by `build_business_message_reject`, `admin_messages.cpp:654`) and is NOT a session-Reject field. QFJ `validatePossDup` rejects `REQUIRED_TAG_MISSING` value 1 with `RefTagID=122`; QuickFIX-cpp `doPossDup` → `REQUIRED_TAG_MISSING` value 1, field 122. **Whether QFJ rejects is gated by its `RequiresOrigSendingTime` setting** (`Session.java:2590`), whose live `DefaultSessionFactory` default is `true` (`DefaultSessionFactory.java:122`); QuickFIX-cpp rejects unconditionally (no knob). fixpp's choice is **non-configurable: always require `122` when `43=Y`**, matching QFcpp and QFJ's default-factory. The interop fixture must run QFJ with its default (`RequiresOrigSendingTime=Y`) or the malformed-dup cell (SC-002) would diverge. Reuses `build_reject` — reason 1 is a standard SessionRejectReason; no new builder.
**Alternatives rejected**: disconnect (over-strict; neither engine disconnects here); a configurable require-knob (adds surface for no interop value — both engines' effective defaults require it).

### D5 — Arm D: PossDup + OrigSendingTime > SendingTime (strict) → Reject(373=10 SendingTimeAccuracyProblem, 371=122) + Logout + disconnect
**Decision**: A `43=Y` message whose `OrigSendingTime(122)` is **strictly greater** than its `SendingTime(52)` → `Reject(35=3)`, `RefTagID(371)=122` (the offending OrigSendingTime field), `SessionRejectReason(373)=SendingTimeAccuracyProblem(10)`, then `Logout` + `Disconnected`. Equality (`122 == 52`) is allowed.
**Rationale**: Strict `>` comparison; reason 10; then `generateLogout` + disconnect (both engines). `RefTagID=122` matches QFJ (`Session.java:2583` passes `OrigSendingTime.FIELD`) and is more specific than fixpp's existing §1699 SendingTime-accuracy reject (which references `52`, the SendingTime field) — 122 is the offending field for the OrigSendingTime check. Reason 10 + the `build_reject`→Logout→Disconnect emit pattern are **already used** in fixpp for the inbound MaxLatency SendingTime check (`session.cpp:1685-1737`); Arm D reuses that exact pattern, differing only in the RefTagID argument (122 vs 52).
**Alternatives rejected**: reject-without-logout (both engines logout); non-strict `>=` (would falsely reject the common equal-timestamp replay); RefTagID=52 (matches the §1699 precedent but is less specific — 122 is the field actually in question for OrigSendingTime).

### D6 — Arm E: SequenceReset(35=4) with PossDup → exempt from the OrigSendingTime requirement
**Decision**: An inbound `SequenceReset(35=4)` bearing `PossDupFlag` is **exempt** from the `122`-required check (Arm C/D do not apply); the existing inbound sequence-reset / gap-fill path (`session.cpp:1880`, S-023) handles it unchanged.
**Rationale**: Both engines special-case `MsgType==SequenceReset` before the OrigSendingTime checks. fixpp already routes `35=4` through `apply_inbound_sequence_reset`; the new PossDup branch must guard on `hdr.msg_type != "4"` so it does not intercept reset frames.
**Alternatives rejected**: applying Arm C/D to SequenceReset (would reject legitimate gap-fill recovery — a regression against 018/S-023).

### D7 — FR-008 AllowPossDup send-path knob → DEFERRED out of this slice (third divergence)
**Decision**: The send-path `AllowPossDup` knob (strip/retain caller-supplied `PossDupFlag(43)`/`OrigSendingTime(122)` on a plain `send`) is **deferred to its own future slice**. The **intended** behavior is preserved as the prior clarification: default STRIP, auto-resend always re-adds (matching QFJ's `send(message, allowPosDup)` strip-by-default and QFcpp's unconditional plain-send strip). This slice is INBOUND-ONLY.
**Rationale (de-scope)**: The plain-`send` path `Session::send_impl` is **opaque-payload** — it takes an opaque `std::span<const std::byte>` app payload, validates only the boundary tokens (`8/9/34/49/52/56/10=`, slot-131 `app_payload_malformed`), splits the leading `35=…`, and copies the rest **verbatim** into the body buffer. There is **no** field-level parse of the business body, and `43=`/`122=` are NOT boundary-rejected — so a caller-supplied `43`/`122` flows through verbatim today. "Strip `43`/`122` on plain send" is therefore **not** a toggle of an existing strip seam: it requires *introducing* a new boundary-anchored `43`/`122` excision parser over the opaque payload — a brand-new parser on the hot, `noexcept`/alloc-sensitive send path, carrying exactly the [[feedback_delimiter_injection_verbatim_field_copy]] delimiter-injection hazard fixpp hit in 020 RC#1 (a naive substring strip can be defeated or can corrupt the frame). That is a separable hardening effort that does not gate interop, so the knob is split out. The `allow_poss_dup` `SessionConfig` field is NOT added in this slice (only `redeliver_poss_dup` is).
**Alternatives rejected (for the deferred slice, recorded for continuity)**: default-retain (matches neither engine); no knob at all (drops the QFJ-parity deliverable). These are revisited when the send-path slice is taken.

## Implementation surface (confirmed by source read, not assumed)

- **Header capture**: `scan_frame_header` (`session.cpp`) parses `43`/`52` but **not** `122` today → add `FrameHeader::orig_sending_time` + `case 122:`. Localized, additive.
- **Validation site**: PossDup OrigSendingTime validation (Arms C/D) runs for **all** `43=Y` non-`35=4` inbound (D2b), BEFORE the seqnum disposition. The too-low tolerance disposition (Arms A/B) is at the too-low arm `session.cpp:1849-1863` — insert Arm A between the Heartbeat-too-low exception and the fatal `session_seqnum_too_low` transition; Arm B is the existing `→Disconnected` (no Logout frame).
- **Reject reuse**: `build_reject` (`admin_messages.cpp:540`) already takes `(…, ref_seq_num, ref_tag_id, ref_msg_type, session_reject_reason, …)` and emits `45=RefSeqNum`, `371=RefTagID`, `372=RefMsgType`, `373=SessionRejectReason`; reasons 1 and 10 already emitted elsewhere (§1699). `380` (BusinessRejectReason) is NOT used here.
- **Time comparison**: reuse the existing SendingTime parse machinery (the `52` MaxLatency path already parses `52`); compare parsed `122` vs `52`.
- **Config**: **one** additive POD field on `SessionConfig` for this slice — `redeliver_poss_dup` (pattern: `reconnect_policy`), in the public header `include/fixpp/session/session_config.hpp`, default-valued → no ABI/breaking change. (`allow_poss_dup` is DEFERRED with FR-008, D7.)
- **No new** `error::core` enum slot (rejects use SessionRejectReason values, not engine error slots); **no** persistent dedup store; **no** codegen change.

## Open follow-ups (out of scope, recorded)
- **FR-008 `AllowPossDup` send-path knob (D7)** — deferred to its own future slice; the opaque `send_impl` path needs a new boundary-anchored `43`/`122` excision parser with a delimiter-injection hostile witness ([[feedback_delimiter_injection_verbatim_field_copy]]) before it can ship. Intended default = STRIP (pending user confirmation).
- **`PossResend(97)` application-resend semantics** — the other half of catalogue row S-010; deferred to a later G3 slice (keeps S-010 `backlog` with a 021 partial-delivery note).
- `NextExpectedMsgSeqNum(789)` Logon-based recovery shortcut — next G3 slice.
- Remaining G3 config knobs (ResetOn*/RefreshOnLogon, CheckCompID=N, validateSequenceNumbers=N, MaxLatency) — later G3 slices.
- FIXT.1.1 / FIX 5.0 SP2 routing — G4.
