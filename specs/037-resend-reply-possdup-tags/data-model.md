# Phase 1 Data Model — Resend-reply frame field matrix (037)

This feature has no entities/storage. The "data model" is the **field set of the two resend-reply frame kinds**, before and after, with the governing invariants.

## Frame kind A — SequenceReset-GapFill (35=4), emitted by `build_sequence_reset_gapfill`

| Tag | Field | Before (today) | After (037) | Source |
|-----|-------|----------------|-------------|--------|
| 8 | BeginString | ✓ | ✓ (unchanged) | `begin_string` param |
| 35 | MsgType=4 | ✓ | ✓ | literal |
| 34 | MsgSeqNum | ✓ (= gap start, too-low) | ✓ | `seq` param |
| 49 | SenderCompID | ✓ | ✓ | `sender_comp_id` param |
| 52 | SendingTime | ✓ | ✓ | `sending_time` param |
| 56 | TargetCompID | ✓ | ✓ | `target_comp_id` param |
| 36 | NewSeqNo | ✓ | ✓ | `new_seqno` param |
| 123 | GapFillFlag=Y | ✓ | ✓ | literal |
| **43** | **PossDupFlag=Y** | **✗ (MISSING)** | **✓ ADDED** | literal `Y` |
| **122** | **OrigSendingTime** | **✗ (MISSING)** | **✓ ADDED** | **= the `sending_time` param (== this frame's 52)** |
| 9 / 10 | BodyLength / CheckSum | recomputed on commit | recomputed on commit | `Writer` |

Append order: `43` then `122` at the end (after `123`), before `commit()`. Order-safe (research D-3).

## Frame kind B — Replayed application frame, emitted by `build_replay_frame`

The loop copies every stored field except a skip set, then unconditionally appends `43=Y` + `122=<captured stored 52>`.

| Aspect | Before (today) | After (037) |
|--------|----------------|-------------|
| Skip set in copy loop | `{9, 10}` | `{9, 10, 43, 122}` |
| `52 → orig_sending_time` capture | separate `if (tag==52)` during normal iteration | **unchanged** — `52` is not in the (widened) skip set, so the capture is unaffected; it already runs before the appended `122` |
| Unconditional append of `43=Y` | ✓ | ✓ (unchanged) |
| Unconditional append of `122 = captured 52` | ✓ | ✓ (unchanged) |
| **Default config (`allow_pos_dup=false`)** stored frame | clean (send-path strips caller 43/122) → output has one 43 + one 122 | **byte-identical** (skip set never matches → no behavior change) |
| **Retain config (`allow_pos_dup=true`)** stored frame already carrying 43/122 | **two 43 + two 122** (caller's + engine's) — DEFECT | **one 43 + one 122** (caller's skipped, engine's appended) |

## Invariants

- **INV-1 (GapFill possdup completeness)**: a GapFill emitted on a resend reply carries exactly one `43=Y` AND exactly one `122`. Never `43` without `122` (research D-2).
- **INV-2 (122 == 52, both frame kinds)**: the emitted `122` value byte-equals the frame's `52`. For the GapFill, `52` is the `sending_time` param; for a replayed app frame, `52` is the stored original `52` (NOT any caller-supplied `122`). FR-002 / FR-005.
- **INV-3 (single-valued tags on replay)**: a replayed app frame carries exactly one `43` and exactly one `122` regardless of whether the stored frame contained either (FR-004). Holds for both default (clean stored) and retain (caller-supplied) configs.
- **INV-4 (default-path byte-identity)**: under default config, replayed app frames are byte-identical to pre-037; the only default-path wire change anywhere is the two new GapFill tags (FR-006).
- **INV-5 (no new surface)**: no builder signature, error slot, config field, codegen, or C-ABI change (FR-007).
- **INV-6 (pre-existing GapFill fields preserved)**: tags `8/35/34/49/52/56/36/123` keep their existing values and presence; only `43`/`122` are added (FR-003).

## State / lifecycle

None — both functions are pure, `noexcept`, stack-only serializers over a caller-provided `out` span with a `null_memory_resource`-backed `Writer`. No mutable session state is read or written by the builders themselves; the resend reply path that calls them is unchanged.
