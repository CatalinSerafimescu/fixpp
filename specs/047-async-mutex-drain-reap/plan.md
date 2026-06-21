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

**Fix — five coordinated changes, all inside `async_mutex.hpp`** (expanded from the
original two-part sketch after **Gate A round 1** found four blockers; judge record
`../../research/reviews/opus_047_gate_a_r1_judge.md`; user chose full restructure).
Full proof in `research.md`:

1. **Converging reap+quiesce loop (reaper).** Replace linear (g)→(h)→finalize with a
   loop that finalizes only when, in one pass, **all feeder counters** are 0
   (`active_acquirers_count_`, the new `active_unlockers_count_`,
   `in_flight_resumptions_`), **then** the sink `active_holders_count_ == 0`, **then**
   both lists empty on a confirming exchange. Exploits the **push-visibility edge**
   (edge #1): a push is sequenced-before its feeder's `acq_rel` decrement, so a
   post-quiesce confirming scan sees any late push.
2. **seq_cst Dekker handshake `draining_`↔`active_acquirers_count_`** (edge #2):
   acquirer entry `fetch_add` + **both** `draining_` loads (L780 fast-path gate AND
   L868 push gate are each load-bearing) + reaper `draining_.store` + reaper
   acquirers-load → seq_cst. Closes the store-buffer reorder that acquire/release
   permits (real on x86 TSO).
3. **In-flight-unlocker representation** — new `active_unlockers_count_` brackets the
   whole `unlock()` body, incremented **before** it reads `draining_`, with the same
   seq_cst Dekker handshake (edge #2′). Closes **B3**: a stale `unlock()` that read
   `draining_==false` can no longer privatize a waiter chain and re-push a residual
   tail after the reaper finalized. Restores the I-32 single-walker soundness
   precondition the prior design only assumed.
4. **Notify-on-every-feeder-decrement** under a published latch (**B1**): the acquirer
   fast-fail (L781) and alloc-fail (L835/L850) decrements currently do NOT
   `latch->notify()`, so the converging loop's `async_wait` misses the `→0` edge and
   hangs. Route all feeder decrements through one audited helper that notifies.
5. **Feeder-before-sink quiesce read order (B2)** + **terminal-ownership CAS at the
   second draining gate (B4)**: read acquirers/unlockers before holders so an
   acquirer→holder transition can't be seen as `holders==0` while live; make the L868
   draining branch win terminal ownership via `queued→cancelled` CAS (not an
   unconditional `phase_.store`) so a concurrent `on_cancel` can't double-resume.

**Witness:** the moved 046 `test_async_mutex_drain_latch_publish_acquire` (hardened —
self-deadline, ≥4 workers × ≥32 acquirers × ≥100 rounds, fresh mutex/round,
orphan-at-teardown safety) covers the original orphan; **each of B1–B4 gets its own
discriminating mutation-tested RED→GREEN witness** (per
[[feedback_coverage_push_enshrines_bugs]] — witnessing one fix would enshrine the
other three). B4 lands as its own commit.

No public API, ABI, wire, codegen, error-code, or config change (FR-007) — the new
`active_unlockers_count_` is a private member.

## Technical Context

**Language/Version**: C++20 (coroutines, `asio::awaitable`), clang-22 / gcc Tier-1
**Primary Dependencies**: standalone Asio (cancellation slots, `async_initiate`); GoogleTest
**Storage**: N/A
**Testing**: GoogleTest; full sanitizer preset matrix (debug, release, ASan, UBSan, TSan, gcc-release)
**Target Platform**: Linux (Tier 1 libstdc++ blocking; libc++ Tier 3 on-demand), Windows Tier 2
**Project Type**: C++ library (header-only concurrency primitive in `include/fixpp/core/sync/`)
**Performance Goals**: No new allocation, no new suspension on any existing path; uncontended fast path unchanged
**Constraints**: `noexcept` operation preserved; lock-free protocol; preserve invariants **I-1..I-31 unchanged**, **I-32 soundness note amended** (single-walker — see data-model §AMENDED), **NEW I-33** (convergence); the F-2/F-3 Gate-B hardening fixes; FIFO-fair grant order
**Scale/Scope**: Single header `async_mutex.hpp`, three functions — `cancel_and_drain()` (converging loop, feeder-before-sink quiesce, B4 CAS), `async_lock()` (seq_cst acquirer handshake, notify on feeder decrements), `unlock()` (new `active_unlockers_count_` bracket + seq_cst handshake). One new private atomic member. Five+ discriminating witnesses (one per blocker + the moved original).

**Scope clarification (Gate A round 1 — corrects spec prose "drain protocol only /
reaper-only"):** the correctness proof spans `cancel_and_drain()` + `async_lock()` +
`unlock()` and adds one private counter (`active_unlockers_count_`). All confined to
`async_mutex.hpp`. **FR-005 holds at the functional level** — memory-order
strengthening and a delayed-finalization counter never change *serialized*
observable outcomes (single strand → one thread → no reordering; the unlocker
counter only delays a finalization that on one strand already happens after the
acquirer synchronously returned). FR-005 wording tightened from "single-thread
no-op" to "serialized **functional** semantics unchanged" (seq_cst changes emitted
instructions/latency on weak archs — confirm fast-path within Article VIII §2 ±5%
at verify).

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
└── async_mutex.hpp        # cancel_and_drain(): converging reap+quiesce loop, feeder-before-sink
                            #   read order, B4 terminal-ownership CAS, seq_cst draining_.store + feeder loads
                            # async_lock(): seq_cst entry fetch_add + both draining_ loads (L776/780/868);
                            #   notify() on feeder decrements (L781/835/850)
                            # unlock(): NEW active_unlockers_count_ bracket (entry inc before draining_ read,
                            #   dec at every return after push_residual) + seq_cst draining_ load (L953)
                            # NEW private member: std::atomic<uint32_t> active_unlockers_count_{0}

tests/sync/
├── test_async_mutex_drain_latch_publish_acquire.cpp   # moved from 046; hardened (self-deadline, fresh mutex/round,
│                                                       #   orphan-at-teardown safety) — covers the original orphan
└── test_async_mutex_drain_reap_blockers.cpp           # NEW: discriminating witnesses W-B1..W-B4 (mutation-tested)

specs/2f-async-mutex.md     # design-doc: append I-33 (convergence) + the amended I-32 single-walker
                            #   soundness note + the active_unlockers_count_ Dekker handshake
```

**Structure Decision**: Single-header primitive fix. All production code change is
in `include/fixpp/core/sync/async_mutex.hpp` (three functions + one private member).
The moved original witness plus a new per-blocker witness file land in `tests/sync/`
and are registered in the sync test `CMakeLists`. The 006 design doc
(`.specify/2f-async-mutex.md`) gets I-33 (convergence) and the amended I-32
single-walker soundness note appended so the proof is versioned alongside I-1..I-32.

## Complexity Tracking

> No Constitution Check violations — table intentionally empty.
