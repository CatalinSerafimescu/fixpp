# Contract: Inbound PossDup / OrigSendingTime Disposition

**Feature**: 021-inbound-possdup-origsendingtime | **Date**: 2026-06-04

This feature exposes no new public C ABI and no new pluggable interface. The contracts are: (1) the **wire-observable** inbound disposition, and (2) the **`SessionConfig` knob** surface (C++ struct fields).

## C1 — Wire-observable inbound disposition (FR-001..FR-007)

Given an established FIX 4.4 session whose next expected inbound sequence number is `N`:

Validation (Arms C/D) is **seqnum-independent** — it fires for any `43=Y` non-`35=4` inbound, including at `34 == N`; the too-low tolerance (Arm A/B) applies only when `34 < N`.

| Inbound frame | fixpp MUST emit | Session state |
|---------------|-----------------|---------------|
| `34<N`, `43=Y`, `122` present & valid, **admin** | nothing | stays `Active`; expected stays `N` |
| `34<N`, `43=Y`, `122` present & valid, **app**, `redeliver_poss_dup=false` | nothing (dropped) | stays `Active`; expected stays `N` |
| `34<N`, `43=Y`, `122` present & valid, **app**, `redeliver_poss_dup=true` | delivers to `Application::fromApp` (flagged possible-duplicate) | stays `Active`; expected stays `N` |
| `34<N`, **no** `43=Y` | **nothing** (NO Logout wire frame — `→Disconnected` only) | → `Disconnected` |
| `43=Y`, `122` **missing** (non-`35=4`, any seqnum incl. `34==N`) | `Reject(35=3)`, `371=122`, `373=1` (RequiredTagMissing) | stays `Active`; expected stays `N` (no advance — verify-returns-false, QFJ `Session.java:1843`) |
| `43=Y`, `122` **>** `52` (strict, non-`35=4`, any seqnum incl. `34==N`) | `Reject(35=3)`, `371=122`, `373=10` (SendingTimeAccuracyProblem) + `Logout` | → `Disconnected` |
| `35=4` (SequenceReset) + `43=Y` | existing gap-fill/reset disposition (exempt from `122` check) | per S-023 |

Notes:
- Reject field model = the real `build_reject` (`admin_messages.cpp:540`, body `:605-636`): `45=RefSeqNum`, `371=RefTagID`, `372=RefMsgType`, `373=SessionRejectReason`. `380` is **BusinessRejectReason** (35=j only, `build_business_message_reject` `:654`) and is **NOT** emitted by a session `Reject(35=3)`.
- Arm D's `371=122` (the offending OrigSendingTime field) matches QFJ (`Session.java:2583`) and is more specific than fixpp's existing §1699 SendingTime-accuracy reject (which references `52`).
- The Arm-B `→Disconnected` emits **no Logout wire frame** — it is the current `session.cpp:1860-1862` behavior (`record_state_transition_` only), preserved byte-identical. (Both reference engines `generateLogout(text)` here; adding a Logout would be a separate deliberate behavior change.)
- Behavior is **role-symmetric** (initiator and acceptor). Arms C/D/E confirmed identical across QuickFIX-cpp v1.16.0 + QuickFIX-J 3.0.1; the at-expected validation (Arm C/D fires at `34==N`) follows QFJ (QFcpp validates only via the too-low path) — research.md "third divergence".

## C2 — SessionConfig knob surface (FR-010)

One additive, default-valued `bool` field on `fixpp::session::SessionConfig` (public header `include/fixpp/session/session_config.hpp`):

```cpp
struct SessionConfig {
  // … existing fields …
  bool redeliver_poss_dup  = false;  // FR-010: inbound validated too-low app duplicate redelivered to fromApp when true; dropped when false. Admin always ignored.
};
```

Compatibility: defaults to `false` (the QFJ-parity behavior), so existing `SessionConfig` constructions are source- and behavior-compatible (additive default-valued public-header field). The send-path `allow_poss_dup` knob (FR-008) is **DEFERRED** out of this slice — not added here.

## C3 — Negative / regression contracts (test-binding)
- A too-low message **without** `43=Y` still transitions to `Disconnected` with **no Logout wire frame** (Arm B preserved) — regression pin.
- `122 == 52` is accepted (not Arm D).
- `35=4` PossDup never triggers Arm C/D.
- A `43=Y` message **at** the expected seqnum (`34==N`) with missing/late `122` still triggers Arm C/D (validation is seqnum-independent) — at-expected pin.
