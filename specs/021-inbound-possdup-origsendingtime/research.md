# Phase 0 Research: Inbound PossDup / OrigSendingTime Handling

**Feature**: 021-inbound-possdup-origsendingtime | **Date**: 2026-06-04

## Method

Reference-engine sweep performed via **CodeGraph** against the two live interop targets at their pinned tags:
- **QuickFIX-cpp v1.16.0** — `src/C++/Session.cpp` (`Session::verify` → `doTargetTooLow` → `doPossDup`)
- **QuickFIX-J 3.0.1** — `quickfixj-core/src/main/java/quickfix/Session.java` (`verify` → `doTargetTooLow` → `validatePossDup`)

No `NEEDS CLARIFICATION` markers remained from `/speckit-clarify` (the two divergences below were resolved there). This document records the canonical disposition each decision follows and why.

## Normative References

- `[FIX-SL §2]` Standard message header — `PossDupFlag(43)`, `OrigSendingTime(122)`, `SendingTime(52)` semantics.
- `[FIX-SL §4.3]` Message recovery — possible-duplicate handling during ResendRequest replay.
- `[FIX-SL §4.5/§4.6]` Session-level reject and logout on sequence/accuracy errors.
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

### D3 — Arm B: too-low without PossDup → fatal, preserved unchanged
**Decision**: `MsgSeqNum < expected` with no `PossDupFlag=Y` continues to emit `Logout` + transition to `Disconnected` exactly as today (`session.cpp:1860-1862`, `session_seqnum_too_low=69`). The PossDup branch is inserted **ahead** of this arm and only intercepts `43=Y`.
**Rationale**: Both engines emit `Logout("MsgSeqNum too low, expecting X but received Y")` and disconnect. This is existing, correct behavior; the feature must not regress it. The too-low Heartbeat(0) silent-ignore exception (`session.cpp:1856-1859`) is independent and unchanged.
**Alternatives rejected**: replacing the arm (would lose the fatal-too-low guarantee).

### D4 — Arm C: PossDup + OrigSendingTime missing → Reject(RequiredTagMissing=1, RefTagID=122), survive
**Decision**: A `43=Y` message (non-SequenceReset) lacking `OrigSendingTime(122)` → session-level `Reject(35=3)`, `SessionRejectReason=RequiredTagMissing(1)`, `RefTagID=122`; the session **survives** (no disconnect).
**Rationale**: Both engines agree (QFJ `validatePossDup` → reject `REQUIRED_TAG_MISSING` value 1 with `RefTagID=122`; QuickFIX-cpp `doPossDup` → `SessionRejectReason_REQUIRED_TAG_MISSING` value 1, field 122). Reuses `build_reject` (`admin_messages.cpp:540`) — reason 1 is a standard SessionRejectReason; no new builder.
**Alternatives rejected**: disconnect (over-strict; neither engine disconnects here).

### D5 — Arm D: PossDup + OrigSendingTime > SendingTime (strict) → Reject(SendingTimeAccuracyProblem=10) + Logout + disconnect
**Decision**: A `43=Y` message whose `OrigSendingTime(122)` is **strictly greater** than its `SendingTime(52)` → `Reject(35=3)`, `SessionRejectReason=SendingTimeAccuracyProblem(10)`, then `Logout` + `Disconnected`. Equality (`122 == 52`) is allowed.
**Rationale**: Both engines agree (strict `>` comparison; reason 10; then generateLogout + disconnect). Reason 10 is **already used** in fixpp for the inbound MaxLatency SendingTime check (`session.cpp:1699`) — same `build_reject` path, then the existing Logout+Disconnected emit pattern.
**Alternatives rejected**: reject-without-logout (engines both logout); non-strict `>=` (would falsely reject the common equal-timestamp replay).

### D6 — Arm E: SequenceReset(35=4) with PossDup → exempt from the OrigSendingTime requirement
**Decision**: An inbound `SequenceReset(35=4)` bearing `PossDupFlag` is **exempt** from the `122`-required check (Arm C/D do not apply); the existing inbound sequence-reset / gap-fill path (`session.cpp:1880`, S-023) handles it unchanged.
**Rationale**: Both engines special-case `MsgType==SequenceReset` before the OrigSendingTime checks. fixpp already routes `35=4` through `apply_inbound_sequence_reset`; the new PossDup branch must guard on `hdr.msg_type != "4"` so it does not intercept reset frames.
**Alternatives rejected**: applying Arm C/D to SequenceReset (would reject legitimate gap-fill recovery — a regression against 018/S-023).

### D7 — FR-008 AllowPossDup send-path knob: default STRIP, auto-resend always re-adds
**Decision**: A `SessionConfig` knob `allow_poss_dup` (default `false`) controls a **plain `send`**: by default the engine strips caller-supplied `PossDupFlag(43)`/`OrigSendingTime(122)` before transmission; when `true`, it retains them. The **automatic resend/retransmission** path (`session.cpp:~2115`, which stamps `43=Y` + `122` from the stored `52`) is unaffected and always emits them.
**Rationale**: Corrects the spec's original FR-008 "default retain", which was wrong. QFJ `send(message, allowPosDup)` defaults to stripping `43`/`122`; QuickFIX-cpp has no knob and unconditionally strips on plain `send` (its resend path re-adds). Default-strip matches both engines' effective plain-send behavior; the knob preserves QFJ parity for callers who deliberately pre-stamp possdup.
**Alternatives rejected**: default-retain (matches neither engine; emits possdup on first transmission, which counterparties may flag); no knob at all (drops the agreed AllowPossDup deliverable / QFJ parity).

## Implementation surface (confirmed by source read, not assumed)

- **Header capture**: `scan_frame_header` (`session.cpp`) parses `43`/`52` but **not** `122` today → add `FrameHeader::orig_sending_time` + `case 122:`. Localized, additive.
- **Disposition site**: the too-low arm at `session.cpp:1849-1863` — insert the PossDup branch between the Heartbeat-too-low exception and the fatal `session_seqnum_too_low` transition.
- **Reject reuse**: `build_reject(admin_messages.cpp:540)` already takes `RefSeqNum/RefTagID/RefMsgType/SessionRejectReason`; reasons 1 and 10 already emitted elsewhere.
- **Time comparison**: reuse the existing SendingTime parse machinery (the `52` MaxLatency path already parses `52`); compare parsed `122` vs `52`.
- **Config**: two additive POD fields on `SessionConfig` (pattern: `reconnect_policy`), default-valued → no ABI/breaking change.
- **No new** `error::core` enum slot (rejects use SessionRejectReason values, not engine error slots); **no** persistent dedup store; **no** codegen change.

## Open follow-ups (out of scope, recorded)
- `NextExpectedMsgSeqNum(789)` Logon-based recovery shortcut — next G3 slice.
- Remaining G3 config knobs (ResetOn*/RefreshOnLogon, CheckCompID=N, validateSequenceNumbers=N, MaxLatency) — later G3 slices.
- FIXT.1.1 / FIX 5.0 SP2 routing — G4.
