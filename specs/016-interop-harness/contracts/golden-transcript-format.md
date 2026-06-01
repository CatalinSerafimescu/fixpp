# Contract: Golden Transcript Format + Normalization

**Feature**: `016-interop-harness` | **Date**: 2026-06-01

Defines the checked-in golden file shape and the diff/normalization contract (FR-006). Mirrors the `tests/abi/golden/` discipline: one file per scenario, diffed at test time, deliberate updates only.

## File

- **Path**: `tests/interop/happy/golden/HP-<id>.fix`
- **Content**: byte-exact captured wire dialogue, **no canonicalization** — SOH (`\x01`) preserved, both directions interleaved with a direction marker per frame.
- **Per-frame line**: `<dir> <raw-frame-bytes>` where `dir ∈ {>, <}` (`>` = fixpp→counterparty, `<` = counterparty→fixpp). The raw bytes are stored verbatim (SOH rendered as `\x01` in the checked-in text for diff-ability; the comparator operates on bytes).

## Normalization rule set (applied before diff, never to the stored golden)

Excluded from equality (non-deterministic across runs):

| Tag | Field | Why excluded |
|-----|-------|--------------|
| `34` | MsgSeqNum | run-dependent (asserted separately as a delta, E1/FR-007) |
| `52` | SendingTime | wall-clock |
| `60` | TransactTime | wall-clock |
| `122` | OrigSendingTime | wall-clock (PossDup paths) |
| `10` | CheckSum | derived from the above; recomputed, not compared byte-wise |
| `9` | BodyLength | derived; recomputed |
| `112` | TestReqID (when timestamp-derived) | run-dependent if the driver uses a clock value |

Everything else — MsgType, admin field values, repeating-group order/content, EncryptMethod, HeartBtInt, reject reasons/RefTagID — **is** compared. The normalization rule set is itself part of the scenario descriptor so a cell can tighten/loosen it with rationale.

## Diff result

- `match` — all non-normalized fields equal, frame order equal, frame count equal.
- `mismatch:<dir>:<frame-index>:<tag-or-structure>` — first divergence, with enough context to triage.

## Update discipline

A golden change is a **deliberate** commit: the diff is reviewed, the wire-format reason stated in the PR (e.g. a fixpp emit change, or a counterparty version bump). Silent golden churn is a Gate-B finding. Captures + goldens are archived as release artifacts and linked from the badge (FR-024).
