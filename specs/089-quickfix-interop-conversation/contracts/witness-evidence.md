# Contract: run evidence and the witness completeness gate

**Producers**: the harness shim (cell rows) and the fixpp-side cells (witness rows).
**Consumers**: the schema checks, and row 4c's breadth follow-on, which cites a witness per catalogue row.

## Why this contract exists

Today `cell_results.yaml` requires `{id, config, kind, status, matrix_disposition, spec_ref}` and nothing
else (`cell_results_schema_check_test.py:23`). **A hand-edited `status: pass` satisfies all nine tests.**
There is no timestamp, no run id, no artifact pointer, no counterparty version. Whatever else this
feature delivers, evidence that cannot distinguish a real run from a typed word is not evidence.

## Level 1 — cell rows

Existing fields unchanged. New evidence fields per [data-model.md](../data-model.md) §5.

| # | Obligation |
|---|---|
| E-1 | `status: pass` **without** complete evidence ⇒ schema check **FAILS** |
| E-2 | `counterparty_version` is sourced from the **hello record**, never from a config file — a config says what was *asked for*, not what *ran* |
| E-3 | `counterparty_digest` is the digest actually used, not a tag |
| E-4 | Each of the 3 configs emits its **own** row; folding one into another is a violation |
| E-5 | An `ENOSPC`-killed run is recorded as **neither** `pass` **nor** `skip` |

**Proof obligation for E-1**: a deliberately falsified row — `pass`, no evidence — is committed to the
test corpus and the check is shown RED against it. An assertion that has never rejected anything is not
known to reject.

## Level 2 — witness rows

| # | Obligation |
|---|---|
| W-1 | One row per (cell × message × direction) |
| W-2 | The expected set is **derived from the conversation script**, never hand-listed |
| W-3 | Exact-set equality: a missing witness **FAILS** the gate |
| W-4 | Rows **accumulate** across configs; a later config MUST NOT overwrite an earlier one's |
| W-5 | The gate runs **after the last configuration**, over the union |
| W-6 | `skip` may not be produced by a missing readback record — that is `fail` |

**⚠️ W-2 is the anti-vacuity clause.** A hand-maintained expected list drifts toward whatever is
currently produced, so the gate ends up agreeing with reality by construction — a check that cannot
fail. Deriving from the script means the gate disagrees when production stops.

**⚠️ W-4 exists because of disk.** The three configs run in sequence with reclaim between them
(plan.md § Matrix sequencing). A record rewritten per config loses two thirds of the evidence, and the
completeness gate would then pass over the remainder — a green produced by amnesia.

**Proof obligation for W-3**: delete one witness and show the gate RED.

## What this contract does NOT do

It does not flip any catalogue row. The narrow message set maps to `A-001`, `A-003`, `A-004`, `A-006`,
`A-007`, all already `done`. This contract makes the **citation mechanism** exist; the breadth follow-on
uses it.
