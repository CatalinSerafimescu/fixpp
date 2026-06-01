# Contract: Parity-Matrix Disposition

**Feature**: `016-interop-harness` | **Date**: 2026-06-01

Defines how a reference-unit-test-parity row is dispositioned and how GAP closure is recorded (US3 / FR-016 / FR-017). Source of truth at authoring time: `phases/phase-9/unit-test-parity-matrix.md` (the 9.G audit, done). The in-repo artifact this feature touches is the **parity witness tests** under `tests/interop/parity/` + the citations.

## Disposition values

| Disposition | Meaning | Required evidence |
|-------------|---------|-------------------|
| `COVERED` | fixpp already asserts the behavior | citation: `tests/**/<file>::<case>` |
| `GAP` | a behavior fixpp should assert but doesn't | a closure task → becomes COVERED, OR an explicit deferral with a tracking entry |
| `N/A` | architecture-specific to a reference engine (runtime DataDictionary, JDBC/BDB store, HTTP admin, raw sockets) | one-line rationale; left unchanged |

## Closure rule (FR-016/FR-017)

- Every `GAP` must end as `COVERED` (with a new test citation), implemented-then-covered, or `deferred:<reason>` with a tracking entry. **No `GAP` left undispositioned** (SC-003).
- `N/A` rows are not touched; their rationale must remain auditable.

## 016 worklist (R-parity — the still-open Track-1 set)

| Row | Action | Resulting disposition |
|-----|--------|-----------------------|
| QFJ-646 — resend aborts when transport `send()` returns false mid-resend | write witness (`tests/interop/parity/resend_abort_on_failing_write_test.cpp`) | `COVERED` |
| Bucket-4 — simultaneous bidirectional ResendRequests / remove-queued-on-SequenceReset / large-queue | confirm fixpp's **store-replay** subsumes the protocol outcome (`replay_subsumes_reorder_queue_test.cpp`); default no reorder-queue impl | `COVERED` (model confirmed) **or** scoped finding if witness fails |
| Inbound SequenceReset `NewSeqNo >/=/<` arms | confirm S-023/#90 arms cover the audit rows | `COVERED` (cite #90 tests) |
| EncryptMethod=0 emit; integer-convertor overflow bounds | already closed (#91) | `COVERED` (cite #91 witnesses) |
| Acceptor HeartBtInt-echo (Bucket-1) | include only if cheap | `COVERED` or note-deferred |
| Bucket-3 (PossDup/OrigSendingTime inbound, AllowPossDup, app-message, FIXT routing, ResetOn*/RefreshOnLogon) | `#8`/option-a | `deferred:by-design` (rationale recorded) |

## Where the disposition lives

- The **witness tests** + their COVERED citations: in-repo (`tests/interop/parity/`).
- The **full audit matrix** (the 255-behavior table): stays in `phases/phase-9/unit-test-parity-matrix.md` (parent) — it is research, not shipped (`[const §XV.18]`, R2). Only the closure citations cross into the repo.
