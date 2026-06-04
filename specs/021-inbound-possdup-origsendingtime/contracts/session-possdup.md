# Contract: Inbound PossDup / OrigSendingTime Disposition

**Feature**: 021-inbound-possdup-origsendingtime | **Date**: 2026-06-04

This feature exposes no new public C ABI and no new pluggable interface. The contracts are: (1) the **wire-observable** inbound disposition, and (2) the **`SessionConfig` knob** surface (C++ struct fields).

## C1 — Wire-observable inbound disposition (FR-001..FR-007)

Given an established FIX 4.4 session whose next expected inbound sequence number is `N`:

| Inbound frame | fixpp MUST emit | Session state |
|---------------|-----------------|---------------|
| `34<N`, `43=Y`, `122` present & valid, **admin** | nothing | stays `Active`; expected stays `N` |
| `34<N`, `43=Y`, `122` present & valid, **app**, `redeliver_poss_dup=false` | nothing (dropped) | stays `Active`; expected stays `N` |
| `34<N`, `43=Y`, `122` present & valid, **app**, `redeliver_poss_dup=true` | delivers to `Application::fromApp` (flagged possible-duplicate) | stays `Active`; expected stays `N` |
| `34<N`, **no** `43=Y` | `Logout` (`"MsgSeqNum too low…"`) | → `Disconnected` |
| `43=Y`, `122` **missing** (non-`35=4`) | `Reject(35=3)`, `373=122`, `380=1` (RequiredTagMissing) | stays `Active` |
| `43=Y`, `122` **>** `52` (strict) | `Reject(35=3)`, `380=10` (SendingTimeAccuracyProblem) + `Logout` | → `Disconnected` |
| `35=4` (SequenceReset) + `43=Y` | existing gap-fill/reset disposition (exempt from `122` check) | per S-023 |

Notes:
- `380` = `SessionRejectReason`, `373` = `RefTagID` (standard FIX 4.4 reject fields, already emitted by `build_reject`).
- Behavior is **role-symmetric** (initiator and acceptor) and confirmed identical across QuickFIX-cpp v1.16.0 + QuickFIX-J 3.0.1 (research.md).

## C2 — SessionConfig knob surface (FR-008, FR-010)

Two additive, default-valued `bool` fields on `fixpp::session::SessionConfig`:

```cpp
struct SessionConfig {
  // … existing fields …
  bool allow_poss_dup      = false;  // FR-008: plain send retains caller 43/122 when true; strips when false. Auto-resend always re-adds.
  bool redeliver_poss_dup  = false;  // FR-010: inbound validated too-low app duplicate redelivered to fromApp when true; dropped when false. Admin always ignored.
};
```

Compatibility: both default to `false` (the recommended/QFJ-parity behavior), so existing `SessionConfig` constructions are source- and behavior-compatible except for the deliberate FR-008 correction (plain send now strips caller-supplied possdup by default — previously the spec mis-stated "retain"; no prior shipped code relied on retain because outbound replay is handled by the resend path, not plain send).

## C3 — Negative / regression contracts (test-binding)
- A too-low message **without** `43=Y` still disconnects (Arm B preserved) — regression pin.
- The automatic resend path still emits `43=Y` + `122` regardless of `allow_poss_dup` — regression pin.
- `122 == 52` is accepted (not Arm D).
- `35=4` PossDup never triggers Arm C/D.
