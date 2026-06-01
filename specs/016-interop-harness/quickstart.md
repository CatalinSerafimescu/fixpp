# Quickstart: Interop Harness

**Feature**: `016-interop-harness` | **Date**: 2026-06-01

How to run the interop gate. Two execution contexts: a **standalone** ctest (parity tests + any self-contained cells) and a **counterparty-paired** run driven by the parent harness.

> Builds are resource-gated (`[const §XVII.7]`, `[[feedback_build_resource_cap_oom]]`): `-j2`, one preset at a time, `AskUserQuestion` approval first.

## 1. Parity witnesses (standalone, no counterparty)

```bash
cd research/G19-fix-fpml-iso20022/library
cmake --build build/linux-clang-debug --target interop_parity -j2
ctest --test-dir build/linux-clang-debug -L interop-parity --output-on-failure
```

These (US3) run with no live engine — they assert fixpp behaviors against the FIX spec (QFJ-646 resend-abort, the Bucket-4 replay-subsumes-reorder-queue model confirmation, the inbound-SequenceReset arms).

## 2. One happy-path cell, paired with a counterparty (the smoke cell)

The parent harness owns the counterparty lifecycle + the capture proxy. From the parent:

```bash
cd research/G19-fix-fpml-iso20022/phase-9-harness
# brings up QuickFIX-cpp acceptor on a leased port, then runs the fixpp initiator driver,
# captures the wire via the passthrough proxy, diffs against the golden:
./run-cell.sh HP-QFcpp-init-fix44-logon-hb-logout       # (orchestration script; name illustrative)
```

The fixpp side is the GoogleTest target `interop_happy` built from the submodule; the counterparty is the unmodified production binary (only fixpp is sanitizer-instrumented — FR-021). Counterparty unavailable ⇒ the cell **skips with a reason** (FR-023), it does not pass.

This is the **per-PR smoke** cell (R4): normal build only, `ubuntu-latest`, gates PRs touching the transport/session/interop surface.

## 3. Full gate (release-prep)

The full matrix × {normal, ASan/UBSan, TSan} (FR-019) + the thorny corpus, run at release-prep with counterparty engines built-once-and-cached (Pattern A, R4). A **sanitizer-only failure blocks the tag** identically to a normal-build failure (FR-020). On green:

- archive per-scenario captures + goldens;
- emit the **interop badge** (FR-024): `Interop verified against QuickFIX-cpp v1.16.0 / QuickFIX-J 3.0.1` + transcript links + the documented-limitations list.

## 4. Verify before Gate B

```bash
# after /speckit-implement + /simplify:
/speckit-verify 016-interop-harness    # local Tier-1 mirror; writes .specify/decisions/016-interop-harness-verify.md
```

`/speckit-verify` GREEN/YELLOW is the precondition for `/gate-b` and the gate labels (`[const §XVII.8]`).

## Acceptance smoke (maps to SCs)

| Check | SC |
|-------|-----|
| Smoke cell green against QFC + QFJ, both roles, 4.4 + 5.0SP2, incl. TLS-logon | SC-001 |
| All P1 / watch:P1 corpus scenarios pass or are documented limitations | SC-002 |
| All parity GAP rows COVERED-with-citation or deferred-with-tracking | SC-003 |
| Full matrix green under normal + ASan/UBSan + TSan | SC-004 |
| Smoke completes within PR-latency budget; catches an introduced wire regression | SC-005 |
| Every cell cites a FIX spec section; zero "because engine X" justifications | SC-006 |
| Badge published with exact versions + transcript links | SC-007 |

## Known caveat

A fixpp initiator aimed at a not-yet-listening counterparty can busy-spin / block `stop()` (015 down-peer L2 carry-forward). Fixtures **start the counterparty first** and bound the connect (R5). Do not author a live cell that relies on `ioc.run()` to terminate — use the scenario's internal `deadline_ms`.
