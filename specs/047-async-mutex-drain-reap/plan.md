# Implementation Plan: async_mutex cancel_and_drain late-waiter reap (lost-wake fix)

**Branch**: `047-async-mutex-drain-reap` | **Date**: 2026-06-21 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/047-async-mutex-drain-reap/spec.md`

## Summary

`async_mutex::cancel_and_drain()` (feature 006) can orphan a waiter that pushes
its `waiter_record` onto `state_` concurrently with a drain: the reaper does its
`state_` re-walk (step (g)) **before** the `active_acquirers_count_` quiesce-wait
(step (h)) and never re-scans `state_` after quiescing, so a late-but-counted
waiter lands unreaped, the finalize CAS silently fails, `signal_release()` runs
anyway, and the waiter's awaitable never resumes (lost wake).

**Fix (two coupled parts, both inside `async_mutex.hpp`):**

1. **Converging reap+quiesce loop (reaper).** Replace the linear "(g) re-walk →
   (h) quiesce → finalize" with a single loop that terminates only when, in one
   pass, **both lists** (`state_`, `next_drain_head_`) are observed empty **and**
   `active_holders_count_ == active_acquirers_count_ == in_flight_resumptions_ ==
   0`. After observing quiescence the loop performs one confirming list-scan; if
   it finds a late waiter it reaps it and re-iterates. This closes the
   **push-visibility edge**: a waiter's push (release-CAS) is sequenced-before its
   count decrement (`acq_rel`, ≥release), so the reaper's acquire-load of
   count==0 synchronizes-with that decrement and, via the count release-sequence,
   sees the push on the confirming scan.

2. **Seq_cst `draining_`↔`active_acquirers_count_` handshake (acquirer + reaper).**
   Strengthen the Dekker pair so the converging loop's *termination* is provably
   stable against a brand-new acquirer: make the acquirer's entry
   `active_acquirers_count_.fetch_add` and its two `draining_` fast-fail loads
   seq_cst, and the reaper's `draining_.store(true)` and quiesce-condition count
   loads seq_cst. Then either the acquirer's `draining_` check observes `true`
   (fast-fails, never pushes) or the reaper's count-load observes the increment
   (waits, then catches the push via part 1). acquire/release alone leaves a
   store-buffer reorder that lets both "miss" (real even on x86 TSO).

**Witness:** the multi-threaded `test_async_mutex_drain_latch_publish_acquire`
(authored under 046) is moved into this feature as the permanent RED→GREEN gate,
hardened with an internal self-deadline so a lost wake surfaces as a fast,
attributable test FAILURE rather than a lane-wide CI hang.

No public API, ABI, wire, codegen, error-code, or config change (FR-007).

## Technical Context

**Language/Version**: C++20 (coroutines, `asio::awaitable`), clang-22 / gcc Tier-1
**Primary Dependencies**: standalone Asio (cancellation slots, `async_initiate`); GoogleTest
**Storage**: N/A
**Testing**: GoogleTest; full sanitizer preset matrix (debug, release, ASan, UBSan, TSan, gcc-release)
**Target Platform**: Linux (Tier 1 libstdc++ blocking; libc++ Tier 3 on-demand), Windows Tier 2
**Project Type**: C++ library (header-only concurrency primitive in `include/fixpp/core/sync/`)
**Performance Goals**: No new allocation, no new suspension on any existing path; uncontended fast path unchanged
**Constraints**: `noexcept` operation preserved; lock-free protocol; preserve invariants I-1..I-31 and the F-2/F-3 Gate-B hardening fixes; FIFO-fair grant order
**Scale/Scope**: Single header `async_mutex.hpp`; the reaper (`cancel_and_drain`) restructure + the seq_cst handshake ops in `async_lock`; one witness test moved in. No other source touched.

**Scope clarification (corrects spec prose "drain protocol only / reaper-only"):**
The correctness proof requires strengthening four acquirer-side memory orders in
`async_lock()` (the `draining_`/`active_acquirers_count_` handshake), not just the
reaper body. This is still confined to `async_mutex.hpp` and is a single-thread
no-op (memory-order strengthening never changes serialized observable behavior),
so **FR-005 holds**; the "reaper-only" wording is a simplification the plan
deliberately widens to "drain protocol + its acquirer-side quiescence handshake."

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

This is a **Threading / concurrency** change to a core coroutine primitive →
Appendix A triggers **all four mandatory controls**: `/clarify` (done), `/analyze`
(mandatory — step 6), **Codex Gate A** (mandatory — after this plan), user `/plan`
sign-off (pending).

| Article | Requirement | Status |
|---|---|---|
| XI §3 | `async_mutex` is the only coroutine mutex; change stays inside it | ✅ no new primitive |
| XI §7 | Threading feature → 4 mandatory controls | ✅ clarify done; analyze + Gate A + plan sign-off scheduled |
| VII §3 | TDD red-green-refactor; failing test first | ✅ witness is RED on current code (reproduced), GREEN after fix (US3) |
| VII §4 | No code without a test | ✅ witness + existing 006 suite |
| IX §1 | ≥95% line / ≥85% branch on touched module, lcov DA/BRDA basis | ⚠️ measure at verify; converging-loop branches must be witnessed or waived |
| IX §2 | Tier-1 ASan/UBSan/TSan all pass | ✅ SC-004; **validate under libstdc++ TSan** (libc++ TSan throws false future/promise races — finding 1) |
| X (ABI) | C ABI contract | ✅ untouched (FR-007) |
| VIII §5 | Hot-path allocator policy | ✅ no new alloc; reaper adds no suspension to acquire path |
| XV | Banned patterns (no `std::mutex` in awaitable header, etc.) | ✅ none introduced |
| XVII §8 | `/speckit-verify` mandatory after `/speckit-implement` | ✅ scheduled |

**No constitution violations.** Complexity Tracking table omitted (nothing to justify).

## Project Structure

### Documentation (this feature)

```text
specs/047-async-mutex-drain-reap/
├── plan.md              # This file
├── research.md          # Phase 0 — the two-edge correctness proof + design decision
├── data-model.md        # Phase 1 — reaper convergence state machine + invariants delta
├── quickstart.md        # Phase 1 — how to run the witness + sanitizer matrix
├── checklists/
│   └── requirements.md   # spec quality checklist (done)
└── tasks.md             # /speckit-tasks output (NOT created here)
```

No `contracts/` directory: the change exposes **no** external interface
(FR-007 — internal correctness fix only). Per the plan template, contracts are
skipped for purely-internal changes.

### Source Code (repository root)

```text
include/fixpp/core/sync/
└── async_mutex.hpp        # THE change: cancel_and_drain() reaper restructure (≈L1142-1232)
                            #             + seq_cst handshake ops in async_lock() (L776, L780, L868)
                            #             + reaper draining_.store / count-loads → seq_cst (L1110, L1189-1191)

tests/sync/
└── test_async_mutex_drain_latch_publish_acquire.cpp   # moved from 046; hardened with internal self-deadline

specs/2f-async-mutex.md     # design-doc: append the convergence-loop + Dekker-handshake invariant (I-32?) note
```

**Structure Decision**: Single-header primitive fix. All production code change is
in `include/fixpp/core/sync/async_mutex.hpp`. The witness moves into `tests/sync/`
and is registered in the sync test `CMakeLists`. The 006 design doc
(`.specify/2f-async-mutex.md`) gets the new convergence invariant appended so the
proof is versioned alongside I-1..I-31.

## Complexity Tracking

> No Constitution Check violations — table intentionally empty.
