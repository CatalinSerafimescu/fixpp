# Implementation Plan: async_mutex strand-local drain-reap simplification

**Branch**: `048-async-mutex-strand-reap` | **Date**: 2026-06-22 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `specs/048-async-mutex-strand-reap/spec.md`
**Supersedes**: 047 (`047-async-mutex-drain-reap`, PR #143). Branched off `main` (the shipped 006/v1.x `async_mutex`); 047's converging-loop/Dekker/feeder approach is abandoned, not extended.

## Summary

`fixpp::sync::async_mutex::cancel_and_drain()` carries cross-thread "convergence" machinery (a lazily-published `drain_latch_state` over an `asio::experimental::concurrent_channel`, an `active_acquirers_count_` in-flight epoch, the `draining_`↔counter handshake, and an async quiescence park in reaper step (h)) whose **sole purpose** is to let the reaper *wait for acquirers/holders/resumptions running on OTHER threads to quiesce*. All four production consumers (Session write-gate, SeqnumManager, MemoryStore, FileStore) access their mutex only on the per-session strand, and cancellation emission is itself marshaled onto that same strand (`engine.cpp:1255-1281`), so **no acquirer/holder/resumption is ever concurrently in-flight on another thread** when `cancel_and_drain()` runs. The machinery is therefore dead production capability that nonetheless leaves a residual multi-threaded lost-wake (047 W-B1).

This feature **narrows the `cancel_and_drain()` contract to the strand-serialized topology** and replaces the cross-thread machinery with a **synchronous strand-local single-pass reap** plus a **bounded strand-local yield loop** that lets any pre-drain holder's `unlock()` run. It also makes the three `async_lock`-setup allocation sites that currently escape a `noexcept` boundary **fail closed** (reusing the existing `error::sync_lock_alloc_failed`) instead of calling `std::terminate()`. No public/ABI surface change (the error variant already exists); the only externally-visible change is the narrowed drain-contract documentation. Removing `drain_latch_ptr_` reduces 046's libc++ `atomic<shared_ptr>` fallback consumer set 4→3.

## Technical Context

**Language/Version**: C++20 (coroutines, concepts), clang/llvm22 + gcc; libstdc++ (Tier-1) and libc++ (Tier-3) lanes.
**Primary Dependencies**: Boost.Asio (standalone asio) coroutine/cancellation primitives; the project's `expected_t`, `error` enum, `slot_allocator`.
**Storage**: N/A (in-memory synchronization primitive).
**Testing**: GoogleTest seam suite under `tests/sync/` (+ `tests/session/` lifecycle); 6-preset sanitizer matrix (debug/release/ASan/UBSan/TSan-libstdc++/gcc-release); `bench/` micro-benchmarks; mutation-revert discrimination.
**Target Platform**: Linux (Tier-1/3); the redesign is platform-agnostic and reduces the libc++ fallback footprint.
**Project Type**: Header-mostly C++ library (the primitive lives in `include/fixpp/core/sync/async_mutex.hpp`).
**Performance Goals**: No regression vs the shipped uncontended/contended baseline; recover the 047 +16%/+32% feeder regression (the offending RMWs are removed). Drain cost remains O(N) waiters, bounded O(1) on v1.0 hot paths.
**Constraints**: Build caps -j2; sanitizer presets ONE AT A TIME (WSL2 ~11 GB OOM risk). No new public/ABI/wire/codegen surface. E-3 (always-post resumption) preserved.
**Scale/Scope**: One header (+ its design doc `.specify/2f-async-mutex.md` amendment), the `error` enum doc (no new code), ~10 drain-machinery test files retired/rewritten + new strand-local witnesses, the 4 consumers unchanged.

## Constitution Check

*GATE: must pass before Phase 0; re-checked after Phase 1.*

- **Article XI (concurrency / no std::mutex in awaitable headers)** — PASS/strengthened. The redesign removes cross-thread atomics machinery; `async_mutex` remains the only legal coroutine-context mutex shape; the §XV.9 no-std-mutex corpus gate is unaffected (no std::mutex introduced). The narrowed contract is *more* aligned with §XI's single-serialization-domain discipline (design-doc §1.1 already states v1.0 hot paths run inside one session serialisation domain with structurally-zero contention).
- **Threading trigger ([const §XVI.3])** — TRIGGERED → `/speckit-clarify` ran (2 clarifications recorded). This is a concurrency-contract change ⇒ Gate A mandatory ([const §XVII.1]) and the 4 threading controls apply.
- **ABI ([const §X.1] frozen ABI)** — PASS. No new public symbol, no enum renumber: `error::sync_lock_alloc_failed = 44` already exists with its C-ABI mapping; the OOM fix reuses it. FR-007/FR-008 assert ABI-no-change (abidiff in verify). The narrowed drain contract is a *documentation* change to an internal-coroutine-surface member whose signature is unchanged.
- **§XII.5 no-implicit-default / fail-closed** — PASS/strengthened. The OOM sites move from `std::terminate()` (process death) to fail-closed `sync_lock_alloc_failed`. The drain misuse path is documented unsupported + debug-asserted.
- **Article XVII (gates)** — full pipeline applies (contract change; the tests-only Gate-A waiver does NOT apply). Gate A before /tasks; Gate B before merge.
- **No security/wire/codegen trigger** — the primitive is transport/parse-agnostic; no `reason_class`/`fixpp_error_t`/codegen/C-ABI enumerator change.

**Result: PASS — no unjustified violations.** The single deviation from a pure "no-behavior-change" posture (narrowing an advertised contract) is the whole point of the feature and is design-doc-anchored (new Erratum E-6, §4.7 amendment).

## Project Structure

### Documentation (this feature)

```text
specs/048-async-mutex-strand-reap/
├── plan.md              # this file
├── research.md          # Phase 0 — decisions + reachability proof + OOM approach
├── data-model.md        # Phase 1 — mutex member set BEFORE/AFTER, waiter_record, pre-reserved post allocator
├── quickstart.md        # Phase 1 — build/test/witness recipe
├── contracts/
│   └── async_mutex-contract.md   # narrowed cancel_and_drain contract + async_lock/unlock invariants
└── checklists/
    └── requirements.md  # spec quality (closed at specify/clarify)
```

### Source Code (repository root)

```text
include/fixpp/core/sync/async_mutex.hpp   # THE change: remove cross-thread machinery; synchronous strand-local reap; fail-closed init; non-allocating resume post
include/fixpp/core/error.hpp              # NO CHANGE (sync_lock_alloc_failed already present) — verify only
.specify/2f-async-mutex.md                # design-doc amendment: new Erratum E-6 (strand-local reap), §4.7 contract narrowing, retire E-5/I-33 cross-thread-convergence claims
spec/behaviors-and-limitations.md          # B-048 (reliable strand-local drain) + L-048 (cross-thread drain unsupported); supersede B-047-1
spec/feature-catalogue.md / coverage-index.md   # NFR-016 row amendment (S-048), coverage index
tests/sync/                                # RETIRE/REWRITE the cross-thread drain witnesses; ADD strand-local reap + OOM-fail-closed witnesses
src/session/, include/fixpp/session/       # consumers UNCHANGED (FR-007) — only re-verified
```

## Design approach (Phase 1 summary — full detail in research.md / data-model.md / contracts/)

1. **Strand-local synchronous reap.** `cancel_and_drain()` (currently `async_mutex.hpp:1061-1233`) becomes: set `draining_`; synchronously exchange `state_` + `next_drain_head_` and reap every queued waiter (CAS `queued→cancelled`, result `sync_lock_drained`, posted resume); then a **bounded strand-local yield loop** `while (active_holders_count_ > 0) co_await asio::post(executor, use_awaitable)` to let any pre-drain holder's `unlock()` run on the same strand; re-reap any waiters the holder spliced; finalize `state_ → not_locked`. No latch, no channel, no `async_wait`, no `active_acquirers_count_`, no reaper cancel-slot.
2. **Remove** (data-model.md §Removed): `drain_latch_ptr_`, `drain_latch_state` + its `concurrent_channel`/`signal_release`/`signal_abort`/`notify`/`async_wait`, `active_acquirers_count_`, the lazy-latch publish/subscribe steps (a/b/c), the reaper quiescence park (h) + reaper `reaper_slot.assign` + abort path. `draining_` and `drain_in_progress_` (idempotency) and `active_holders_count_` (now a plain strand-local count) are retained but demoted to plain/relaxed ordering.
3. **Retain** (contracts/): `waiter_record` + refcount, the always-post resumption (`resume_fn_`/E-3), `on_cancel` + the `phase_` `queued→{cancelled,granted}` CAS arbitration, `state_`/`next_drain_head_` encoding, `unlock()`'s walk.
4. **OOM fail-closed (US2/FR-003)** for the three escaping sites:
   - `reaper_slot.assign` (1177): **ELIMINATED** by removing the reaper park.
   - `inherited_slot.assign` (862): **wrapped fail-closed** → `sync_lock_alloc_failed` (mirrors the existing `store_executor` precedent at 849-857).
   - resume `asio::post` (614): made **non-allocating** via a per-waiter pre-reserved associated allocator (storage in `waiter_record`), so it cannot throw — fixing both the drain-resume and the pre-existing normal-`unlock()`-grant-resume terminate.
5. **Misuse contract (US3/FR-006):** document strand-serialized-only; add a debug-build assertion that `cancel_and_drain()`/`unlock()` run on the bound executor where a cheap on-strand check exists; no release gate.

## Complexity Tracking

| Item | Why it is acceptable |
|---|---|
| Narrowing an advertised cross-thread contract | The cross-thread capability is dead production code (source-verified strand-serialization) AND broken (047 W-B1 residual orphan). Narrowing removes a maintenance liability and a real lost-wake; design-doc-anchored (E-6). |
| New per-waiter pre-reserved post allocator | Smallest correct fix for the E-3 always-post OOM-terminate; reuses the inline-buffer pattern already in `waiter_record` (exec_storage_/slot_storage_). |
| Retiring ~10 drain-machinery test files | They test removed machinery; replaced by strand-local witnesses + the OOM-fail-closed witness. The cross-thread WB witness is repurposed to assert unsupported use is rejected (not deleted-to-green). |

## Phase gates

- Phase 0 (research.md) — decisions + the strand-serialization reachability proof + the OOM/pre-reserved-allocator design + alternatives. **Done in this run.**
- Phase 1 (data-model.md, contracts/, quickstart.md) — member-set delta, narrowed contract, build/test recipe. **Done in this run.**
- Next: `/gate-a 048-async-mutex-strand-reap` (before `/tasks`).
