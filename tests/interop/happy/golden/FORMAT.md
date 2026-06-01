# Golden transcript format (`HP-*.fix`)

Per-scenario golden FIX wire transcripts for the US1 happy-path matrix. This
directory holds the checked-in goldens the **parent harness** diffs the live
passthrough-proxy capture against (FR-006; contract
`specs/016-interop-harness/contracts/golden-transcript-format.md`).

## File shape

- **Path**: `tests/interop/happy/golden/HP-<id>.fix`
- One file per scenario, **byte-exact** captured wire dialogue, no canonicalization.
- Per-frame line: `<dir> <raw-frame-bytes>` where `dir ∈ {>, <}`
  (`>` = fixpp→counterparty, `<` = counterparty→fixpp).
- SOH is rendered `\x01` in the checked-in text (the comparator operates on bytes;
  `parse_golden()` in `support/golden_diff.hpp` decodes the `\x01` escape).

## Normalization (applied before diff, never to the stored file)

Tags excluded from equality (non-deterministic): `34` (MsgSeqNum — asserted
separately as a delta), `52` (SendingTime), `60` (TransactTime), `122`
(OrigSendingTime), `10` (CheckSum), `9` (BodyLength), `112` (TestReqID when
timestamp-derived). Everything else is compared. See
`support/golden_diff.hpp::default_normalization_tags()`.

## Status — PENDING first paired capture

**No goldens are checked in yet, and they MUST NOT be hand-fabricated.** A golden
is by definition a *capture* from a real fixpp↔QuickFIX paired run; fabricating
one would diff against fixpp's own output and prove nothing (a lying artifact).

The capture requires (a) the gitignored parent harness `../phase-9-harness/`, and
(b) a running **SSL-configured** QuickFIX-cpp / QuickFIX-J counterparty (all-TLS
baseline, FR-025). Until that exists, the in-repo deliverable is this `FORMAT.md`
+ the directory; the goldens are populated at the first paired capture (a
parent-harness runtime step — tasks.md T009, deferred).

Golden updates are deliberate, reviewed commits (golden-transcript-format.md
"Update discipline"); silent churn is a Gate-B finding.
