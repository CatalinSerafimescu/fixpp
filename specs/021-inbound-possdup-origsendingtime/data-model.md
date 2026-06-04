# Phase 1 Data Model: Inbound PossDup / OrigSendingTime Handling

**Feature**: 021-inbound-possdup-origsendingtime | **Date**: 2026-06-04

No persistent entities are introduced. The "data model" here is (1) the inbound disposition decision function and (2) two additive `SessionConfig` fields.

## 1. Inbound possible-duplicate disposition (decision function)

Pure classification over the already-parsed inbound `FrameHeader` + session state. Evaluated **only** on the inbound too-low path (`MsgSeqNum < expected`) — at/above-expected handling is unchanged.

Inputs:
- `msg_seq_num (34)`, `msg_type (35)`, `poss_dup_flag (43)`, `sending_time (52)`, `orig_sending_time (122)` — from `FrameHeader` (122 newly captured).
- `expected = seqnum_mgr_.next_inbound_unsafe()`.
- `is_app_message` — whether `35` is an application message (vs admin).
- `cfg_.redeliver_poss_dup` — the inbound app-dup knob.

Decision table (first match wins; evaluated after the too-low Heartbeat(0) silent-ignore exception, before the fatal `session_seqnum_too_low`):

| # | Guard | Disposition | State | Emits |
|---|-------|-------------|-------|-------|
| 0 | `msg_type == "4"` (SequenceReset) | defer to existing reset/gap-fill path (Arm E exempt) | unchanged | (reset path) |
| 1 | `poss_dup_flag != "Y"` | **Arm B** fatal too-low | → `Disconnected` | `Logout` (existing) |
| 2 | `orig_sending_time` empty/absent | **Arm C** | stay `Active` | `Reject(35=3, reason=1, RefTagID=122)` |
| 3 | `parse(122) > parse(52)` (strict) | **Arm D** | → `Disconnected` | `Reject(35=3, reason=10)` + `Logout` |
| 4 | else, `!is_app_message` | **Arm A admin** ignore | stay `Active`, **no advance** | nothing |
| 5 | else, `is_app_message && !redeliver_poss_dup` | **Arm A app-drop** (default) | stay `Active`, **no advance** | nothing |
| 6 | else, `is_app_message && redeliver_poss_dup` | **Arm A app-redeliver** | stay `Active`, **no advance** | `Application::fromApp` (flagged possdup) |

Invariants:
- **INV-1**: rows 4/5/6 never advance `seqnum_mgr_` (no `check_inbound`-driven increment on the tolerated path).
- **INV-2**: row 1 (Arm B) byte-identical to current `session.cpp:1860-1862` behavior — regression-pinned.
- **INV-3**: row 0 (Arm E) never reaches rows 2/3 — SequenceReset is exempt from the `122` requirement.
- **INV-4**: `122 == 52` is **not** Arm D (strict `>` only).
- **INV-5**: all emits use stack buffers via existing builders — no heap on the inbound path.

## 2. SessionConfig additions (additive POD fields)

In `src/session/session_config.hpp` (same shape as the existing `reconnect_policy` knob; default-valued, no breaking change):

| Field | Type | Default | Governs |
|-------|------|---------|---------|
| `allow_poss_dup` | `bool` | `false` | **Send path (FR-008/D7)**: when `false`, a plain `send` strips caller-supplied `PossDupFlag(43)`/`OrigSendingTime(122)`; when `true`, retains them. The automatic resend path is unaffected (always re-adds). |
| `redeliver_poss_dup` | `bool` | `false` | **Inbound app dup (FR-010/D2)**: when `false`, a validated too-low possible-duplicate application message is dropped (no `fromApp`); when `true`, redelivered to `fromApp` flagged possdup. Admin dups always ignored regardless. |

## 3. FrameHeader addition

`FrameHeader` (`session.cpp`, the `scan_frame_header` output struct) gains:

| Field | Tag | Note |
|-------|-----|------|
| `orig_sending_time` | 122 | `std::string_view` raw value; newly captured (`case 122:` in `scan_frame_header`). Parsed (vs `52`) only on the PossDup path. |

No other struct changes. `poss_dup_flag (43)` and `sending_time (52)` are already captured.
