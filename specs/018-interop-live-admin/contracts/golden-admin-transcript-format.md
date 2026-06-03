# Contract: Golden Admin Transcript Format

**Feature**: `018-interop-live-admin` | **Date**: 2026-06-03
Extends the 016 `happy/golden/FORMAT.md`. **Reuses the 016 `diff_transcripts(...)` utility with an explicit `{52,10}` admin normalization profile** (NOT the 016 *default* tag set, which would mask G1's assertions) — this contract adds the new admin frame types to the captured set and pins the admin profile.

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

## Normalization (explicit `{52,10}` admin profile — NOT the 016 default)

- The admin profile excludes **only** `{52, 10}` from equality: canonicalize `52=` (SendingTime) and `10=` (CheckSum), passed explicitly into `diff_transcripts(expected, actual, excluded_tags={52,10})` (the parameter already exists at `tests/interop/support/golden_diff.hpp:44-46`). All other tags — including `112`, `34`, `7`, `16`, `122`, `123`, `43`, `45`, `373` — are matched **verbatim**.
- **DO NOT use the 016 default** `default_normalization_tags()` = `{9, 10, 34, 52, 60, 112, 122}` (`golden_diff.hpp:36-40`) for G1: it drops `112` (echo correlation), `34` (MsgSeqNum), and `122` (OrigSendingTime / replay evidence) — exactly the tags G1 asserts. Using it would make FR-001's echo, FR-003's seqnum, and FR-004/004a's replay evidence un-assertable, and would make the SC-004 gate-bite canary not bite.
- **No library change**: `{52,10}` is a caller-supplied tag set; the seam already exists. (research R6.)

## Gate-bite requirement

- A deliberate single-tag mutation of a **compared** tag (`112`, `34`, `7`, `16`, `122`, or `123` — NOT one of the canonicalized `{52,10}`) MUST make the drift gate **FAIL** — verified by a negative test (SC-004). The canary MUST NOT mutate `52`/`10` (the diff ignores them, yielding a false non-biting "pass"). A golden that cannot be bit is not a valid golden.
