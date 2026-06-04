# Phase 1 Data Model: Inbound PossDup / OrigSendingTime Handling

**Feature**: 021-inbound-possdup-origsendingtime | **Date**: 2026-06-04

No persistent entities are introduced. The "data model" here is (1) the inbound disposition decision function and (2) one additive `SessionConfig` field.

## 1. Inbound possible-duplicate disposition (decision function)

Pure classification over the already-parsed inbound `FrameHeader` + session state. The disposition is **two-stage**: Stage 1 (PossDup *validation*, Arms C/D) runs for **any** inbound `43=Y` non-`SequenceReset` message — including at-expected (`34 == expected`); Stage 2 (too-low *tolerance*, Arms A/B) runs only on the too-low path (`MsgSeqNum < expected`). Validation precedes the seqnum disposition (D2b — the at-expected divergence).

Inputs:
- `msg_seq_num (34)`, `msg_type (35)`, `poss_dup_flag (43)`, `sending_time (52)`, `orig_sending_time (122)` — from `FrameHeader` (122 newly captured).
- `expected = seqnum_mgr_.next_inbound_unsafe()`.
- `is_app_message` — whether `35` is an application message (vs admin).
- `cfg_.redeliver_poss_dup` — the inbound app-dup knob.

### Stage 1 — PossDup validation (runs for ALL `43=Y`, non-`35=4`, any seqnum)

First match wins:

| # | Guard | Arm | State | Emits |
|---|-------|-----|-------|-------|
| 0 | `msg_type == "4"` (SequenceReset) | **Arm E** exempt — skip Stage 1, defer to existing reset/gap-fill path | unchanged | (reset path) |
| 1 | `poss_dup_flag != "Y"` | not a PossDup — skip Stage 1, fall through to Stage 2 / normal dispatch | — | — |
| 2 | `orig_sending_time` empty/absent | **Arm C** | stay `Active` | `Reject(35=3, 371=122, 373=1)` (RequiredTagMissing) |
| 3 | `parse(122) > parse(52)` (strict) | **Arm D** | → `Disconnected` | `Reject(35=3, 371=122, 373=10)` (SendingTimeAccuracyProblem) + `Logout` |
| 4 | else (`43=Y`, `122` present & valid) | validated — proceed to Stage 2 (if too-low) or normal at-expected dispatch | — | — |

### Stage 2 — too-low tolerance (runs only when `MsgSeqNum < expected`, after Stage 1 validates)

Evaluated after the too-low Heartbeat(0) silent-ignore exception, before the fatal `session_seqnum_too_low`:

| # | Guard | Disposition | State | Emits |
|---|-------|-------------|-------|-------|
| 5 | `poss_dup_flag != "Y"` | **Arm B** fatal too-low (current behavior) | → `Disconnected` | **nothing** (NO Logout wire frame — `record_state_transition_` only) |
| 6 | `43=Y`, validated, `!is_app_message` | **Arm A admin** ignore | stay `Active`, **no advance** | nothing |
| 7 | `43=Y`, validated, `is_app_message && !redeliver_poss_dup` | **Arm A app-drop** (default) | stay `Active`, **no advance** | nothing |
| 8 | `43=Y`, validated, `is_app_message && redeliver_poss_dup` | **Arm A app-redeliver** | stay `Active`, **no advance** | `Application::fromApp` (flagged possdup) |

A validated `43=Y` **at** the expected seqnum (`34 == expected`) is not a Stage-2 case: it is processed once normally and advances the seqnum (the spec's own edge case "PossDup at expected → processed once").

Invariants:
- **INV-1**: the **too-low tolerated** rows (6/7/8) never advance `seqnum_mgr_` (no `check_inbound`-driven increment). This scopes "no advance" to the too-low tolerated arm ONLY — a validated at-expected `43=Y` IS processed-once and DOES advance (per the spec edge case), so the no-advance rule does not apply to the at-expected path.
- **INV-2**: Arm B (row 5) byte-identical to current `session.cpp:1860-1862` behavior — a bare `record_state_transition_(fsm_state::Disconnected)` with **NO Logout wire frame** emitted — regression-pinned.
- **INV-3**: row 0 (Arm E) never reaches Stage-1 rows 2/3 — SequenceReset is exempt from the `122` requirement.
- **INV-4**: `122 == 52` is **not** Arm D (strict `>` only).
- **INV-5**: all emits use stack buffers via existing builders — no heap on the inbound path.

## 2. SessionConfig addition (additive POD field)

In `include/fixpp/session/session_config.hpp` (the **public** header; same shape as the existing `reconnect_policy` knob; default-valued, no breaking change — additive default-valued field). The behavior site is `src/session/session.cpp`.

| Field | Type | Default | Governs |
|-------|------|---------|---------|
| `redeliver_poss_dup` | `bool` | `false` | **Inbound app dup (FR-010/D2)**: when `false`, a validated too-low possible-duplicate application message is dropped (no `fromApp`); when `true`, redelivered to `fromApp` flagged possdup. Admin dups always ignored regardless. |

> `allow_poss_dup` (the FR-008 send-path knob) is **DEFERRED** out of this slice (D7) — not added here. The opaque `send_impl` path requires a new boundary-anchored `43`/`122` excision parser before that knob can ship.

## 3. FrameHeader addition

`FrameHeader` (`session.cpp`, the `scan_frame_header` output struct) gains:

| Field | Tag | Note |
|-------|-----|------|
| `orig_sending_time` | 122 | `std::string_view` raw value; newly captured (`case 122:` in `scan_frame_header`). Parsed (vs `52`) only on the PossDup path. |

No other struct changes. `poss_dup_flag (43)` and `sending_time (52)` are already captured.
