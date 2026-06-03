# Quickstart: Live Session-Admin Interop Round-Trips (gap-fill G1)

**Feature**: `018-interop-live-admin` | **Date**: 2026-06-03

How to run one G1 admin cell locally and the full G1 matrix. G1 cells are **counterparty-required** (live QuickFIX-J 3.0.1) and run at **release-prep tier** — absent a counterparty they `skip:counterparty-unavailable`.

## Prerequisites

- The 016 parent harness present and built: `phase-9-harness/` (QFJ launcher, TLS fixtures under `tls/`, configs under `configs/`, `tools/` for capture + `cell_results.yaml` emit).
- fixpp built from the submodule (debug for iteration; sanitizer presets for the IX.2 pass — `-j2`, one preset at a time, `[[feedback_build_resource_cap_oom]]`).
- QuickFIX-J 3.0.1 reachable (parent `reference-engines/`); `one_way_ca` server cert issued under fixpp's CA (016 P0 `tls/`).

## Run one admin cell (in-process witness, counterparty optional)

```bash
cd research/G19-fix-fpml-iso20022/library
# Build the interop target (debug)
cmake --build build/debug --target interop_happy -j2
# Run a single scenario group; skips with reason if QFJ is not up
ctest --test-dir build/debug -R 'HappyTestRequestEcho' --output-on-failure
```

The in-process driver asserts fixpp's own state only (reaches `Active`, FSM history, seqnum delta, bounded stop). The bidirectional wire frames are asserted by the parent golden-diff (next section).

## Run the full G1 matrix (paired, parent-driven)

```bash
# From the parent harness (gitignored), counterparty up FIRST:
cd research/G19-fix-fpml-iso20022/phase-9-harness
# Launch QFJ (both roles), drive the admin scenarios, capture goldens, emit cell_results:
python tools/emit_matrix.py --include-g1        # extends the 016 matrix with the G1 admin cells
# Validate emitted rows through the SAME in-repo schema-check:
ctest --test-dir ../library/build/debug -R interop_cell_results_schema_check --output-on-failure
```

## Sanitizer pass (Article IX.2 — discharges the 016 verify-YELLOW waiver)

```bash
cd research/G19-fix-fpml-iso20022/library
# One preset at a time, -j2:
for P in asan ubsan tsan; do
  cmake --build build/$P --target interop_happy -j2
  ctest --test-dir build/$P -R 'Happy.*(TestRequest|Cadence|Recovery|Reject)' --output-on-failure
done
```

## Capture / verify goldens (first paired run)

- Goldens are captured at first paired run from the QFJ engine-log seam; do **not** hand-fabricate.
- Verify the drift gate bites: mutate one tag (e.g. an echoed `112`) in a golden and confirm the cell FAILs, then revert (SC-004).

## Definition of done (per SC)

- All four admin scenario groups GREEN, both fixpp roles, vs live QFJ over `one_way_ca` TLS (SC-001).
- 100% of G1 cells resolve to `pass`/`skip`/`n-a`; schema-check zero errors (SC-002).
- Recovery cells: fixpp back to `Active`, expected inbound seqnum, no prefix loss (SC-003).
- Every cell has a gate-biting golden or a recorded known-limitation (SC-004).
- Matrix run deterministic, no hangs/flakes across the configured repeat count (SC-005).
- Badge/known-limitations doc reflects asserted admin coverage without overstating scope (SC-006).
