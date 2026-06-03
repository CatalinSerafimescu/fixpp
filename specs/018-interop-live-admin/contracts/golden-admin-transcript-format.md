# Contract: Golden Admin Transcript Format

**Feature**: `018-interop-live-admin` | **Date**: 2026-06-03
Extends the 016 `happy/golden/FORMAT.md`. **Reuses the 016 P4 normalizer unchanged** — this contract only adds the new admin frame types to the captured set.

## Format

- One file per cell: `tests/interop/happy/golden/<cell_id>.fix`.
- Captured from the QuickFIX-J **engine-log seam** (`toAdmin`/`fromAdmin`) — already-decrypted, both legs (clarify decision-b; 016 P4 decision-(b)). **No MITM.**
- Captured at the **first paired run**, never hand-fabricated (016 T009 rule). Until captured, the cell stays `skip`/deferred.
- Each line is a FIX admin frame in tag=value SOH form, ordered as observed on the seam.

## Frame set (new for G1, beyond the handshake)

| Scenario | Golden frames asserted |
|---|---|
| `testrequest_echo` | `TestRequest(35=1, 112=ID)` and the answering `Heartbeat(35=0, 112=ID)` — both directions |
| `idle_cadence` | ≥3 `Heartbeat(35=0)` frames per direction within the ~5s window; **no** `TestRequest(35=1)` present |
| `recovery_inbound` | fixpp's `ResendRequest(35=2, 7=BeginSeqNo, 16=EndSeqNo)`; QFJ's `SequenceReset-GapFill(35=4,123=Y)` and/or replay `(43=Y,122=)` |
| `recovery_outbound` | QFJ's `ResendRequest(35=2)`; fixpp's answering replay `(43=Y,122=)` and/or `SequenceReset-GapFill(35=4,123=Y)` |
| `session_reject` | the `Reject(35=3, 45=RefSeqNum, 373=SessionRejectReason[, 371=RefTagID])`; subsequent Heartbeat proving session survival |

## Normalization (reuse — DO NOT extend)

- Canonicalize **only** `52=` (SendingTime) and `10=` (CheckSum). All other tags — including `112`, `34`, `7`, `16`, `123`, `43`, `45`, `373` — are matched verbatim.
- **Rationale**: seqnum/`112`/reject-ref tags are the very subject of the assertion; canonicalizing them would mask the behaviour. (research R6.)

## Gate-bite requirement

- A deliberate single-tag mutation of a golden (e.g. flip an echoed `112` or a `BeginSeqNo`) MUST make the drift gate **FAIL** — verified by a negative test (SC-004). A golden that cannot be bit is not a valid golden.
