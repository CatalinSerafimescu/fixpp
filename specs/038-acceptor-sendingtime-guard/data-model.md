# Phase 1 Data Model — 038-acceptor-sendingtime-guard

No new types, fields, or persisted state. This documents the decision matrices + invariants the three groups realize.

## Group 1 — Acceptor first-Logon SendingTime disposition matrix

Inputs at the acceptor `NotConnected` first-Logon arm, AFTER `interpret_logon` (CompID/BeginString/HeartBtInt) and FIXT `1137` validation succeed, with `effective_clock_` set and threshold `T = cfg_.sending_time_threshold ? : 120 s`:

| inbound `52` state | `check_sending_time` | Establish? | Emitted | Reject `373` | Outbound advance | Inbound advance/persist |
|---|---|---|---|---|---|---|
| absent / empty | n/a (treated bad) | **No** | Reject(35=3,371=52), **no Logout** | **10** | +1 (Reject) | **none** |
| present, unparseable | n/a (parse fail) | **No** | Reject(35=3,371=52), **no Logout** | **10** | +1 | **none** |
| present, parses, `\|52−now\| > T` (stale-past OR future) | fails | **No** | Reject(35=3,371=52), **no Logout** | **10** | +1 | **none** |
| present, parses, `\|52−now\| ≤ T` | ok | **Yes** (unchanged) | reply Logon (as today) | — | as today | as today |

Invariants:
- **INV-1 (uniform reason)**: every non-conforming case → `SessionRejectReason(373)=10`, `RefTagID(371)=52` (Clarifications 2026-06-14; mirrors established-Q3 reason code, diverges from QuickFIX `RequiredTagMissing=1` for absent — documented).
- **INV-2 (no-establish, no-Logout)**: a rejected first-Logon emits the Reject then transitions straight to `fsm_state::Disconnected` with **no Logout** (matches the in-arm `1137` reject `:2102-2136`; the established-Q3 Logout is a live-session teardown, not applicable pre-establishment). The session never reaches `Active`/`LogonReceived`.
- **INV-3 (observation)**: the Reject is passed to `fire_to_admin_` before `store_then_emit` (036/FR-008); a throwing `toAdmin` → `app_callback_threw` + `Disconnected` (same as the `1137` reject).
- **INV-4 (no inbound persist)**: the reject path does NOT call `persist_inbound_advance_`; the manager's next-expected-inbound and (for a persistent store) the durable inbound counter are unchanged across the rejected Logon. Only the outbound counter advances (one `assign_outbound` for the Reject); `store_then_emit` persists that OUTBOUND reject frame (orthogonal to the inbound invariant).
- **INV-5 (boundary)**: `|52−now| == T` is conforming (within window) — same boundary semantics as established-Q3's `check_sending_time` (`≤ max_latency`).
- **INV-6 (conforming byte-identity)**: a within-window first-Logon establishes with output identical to pre-feature (the guard is a pure pre-check on the failure side).

Guard position in the first-Logon validation order: `BeginString/CompID (interpret_logon)` → `FIXT 1137` → **`SendingTime(52)` [NEW]** → establish + reply Logon. (D-5.)

## Group 2 — Reconnect `credentials_rotated` callback containment

State around `reconnect_fsm.cpp:194-209`:

```
if (rotated && emit_credentials_rotated_):
    emit_credentials_rotated_(event)        # <-- bare today; wrap in try/catch
last_active_source_ = snap                  # baseline update (MUST run even if callback threw)
last_active_fp_     = new_fp
... Step 3 (ssl_cfg) ... Step 4 (make) ... handshake ...
```

- **INV-7 (containment)**: a throw from `emit_credentials_rotated_(...)` is caught at the call site; control falls through to the baseline update (`last_active_source_`/`last_active_fp_`) and the remaining attempt steps (`make()` → handshake). The attempt reaches its policy outcome instead of unwinding the coroutine.
- **INV-8 (no spurious re-emit)**: because the baseline update still runs after a caught throw, the NEXT attempt does not re-detect the same rotation and re-emit.
- **INV-9 (transparent)**: a non-throwing callback yields behaviour identical to today (the `try/catch` is inert on the happy path).
- SC-free: no measurable user-facing outcome; unit-test-backed (FR-006/FR-007).

## Group 3 — FIXT 1137 reject observable set (witness only)

For the existing reject arms (`session.cpp:2070-2128`), the witness asserts the observable set — no new state:

| inbound `1137` | on-wire frame | `373` | `toAdmin` observed | terminal state |
|---|---|---|---|---|
| absent | `Reject(35=3, 371=1137)` | 1 (RequiredTagMissing) | yes | `Disconnected` |
| non-conformant value | `Reject(35=3, 371=1137)` | 5 (ValueIsIncorrect) | yes | `Disconnected` |

- **INV-10 (no production change)**: Group 3 asserts current behaviour only; `git diff -- src/` for this group is empty (FR-009).
