# Quickstart / Validation Guide: Mid-Session Sequence-Number Reset (071)

Validation scenarios that prove the feature end-to-end. Implementation lives in `tasks.md`; this is the run/verify guide.

## Prerequisites

- `linux-clang-debug` preset built (Article XVII §7 local gate).
- Test module: `tests/session/` (new witnesses grouped per Article VII §8 into the session bucket, selected by `ctest -L session`; the alloc/no-heap and any own-`main` cases stay standalone).

## Scenario 1 — Happy path (US1, SC-001, INV-071-1)

Drive an initiator to Active against an in-process acceptor/fixture, then call `reset_sequence_numbers()`.

Expect:
1. Exactly one `Logon` with `ResetSeqNumFlag=Y` emitted on the existing transport at MsgSeqNum 1.
2. No transport close across the reset (SC-001).
3. On the fixture's confirming `Logon(141=Y)`: state returns to Active; `next_outbound()==2 && next_inbound()==2`.
4. A subsequent `send()` carries MsgSeqNum 2 (no duplicate 1).

## Scenario 2 — Inert by default (FR-006, SC-003, INV-071-4)

Run the three shipped reset suites unchanged and confirm green:

```bash
ctest --test-dir build/linux-clang-debug -L session -R "reset_on_lifecycle|persistent_seqnum_hydrate|reset_seqnum_policy_matrix" --output-on-failure
```

Plus a byte-level baseline compare of connect/send/receive on a session where the trigger is never called → zero diff vs pre-feature.

## Scenario 3 — Invalid-state refusal (FR-007, SC-004)

Invoke `reset_sequence_numbers()` from each non-eligible condition and assert `std::unexpected(session_invalid_state_for_reset)` with no state/counter change:
- NotConnected, LogonSent, LogonReceived, LogoutSent, Disconnected.
- Active **but** `is_awaiting_resend()` (drive an inbound gap first).

## Scenario 4 — Callback symmetry (INV-071-2)

Register onLogon/onLogout observers; run Scenario 1. Assert **neither** fires across the reset. Then trigger a real `close()` and assert onLogout **does** fire (proves the latch was not swallowed).

## Scenario 5 — Single liveness loop (INV-071-3, R6)

After Scenario 1 completes, assert exactly one `run_liveness_loop` is active (e.g. heartbeat cadence unchanged — no doubled heartbeats over an interval). Run under TSan.

## Scenario 6 — Emit-site safety mutation proofs (SC-005)

Each guard has a discriminating witness proven RED against a mutant:
- FR-011: remove the "skip hydrate" → witness detects double-hydrate / wrong seq.
- FR-012: remove the `effective_clock_` guard on a null-clock seam → witness detects the crash/guard.
- FR-013: sample seq before the reset → witness detects wrong durable seq.

## Sanitizer / coverage gate (`/speckit-verify`)

```bash
# per Article XVII §8 — serial preset matrix, produces .specify/decisions/071-*-verify.md
```
- ASan/UBSan/TSan green on the new witnesses.
- ≥95% line / ≥85% branch on touched `session/` lines (Article IX §1), or a recorded Opus assessment per uncovered error/edge path.
- No-heap invariant (INV-071-5) holds on the emit path.
