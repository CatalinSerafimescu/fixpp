# Implementation Plan: async_mutex strand-local drain-reap simplification

**Branch**: `048-async-mutex-strand-reap` | **Date**: 2026-06-22 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `specs/048-async-mutex-strand-reap/spec.md`
**Supersedes**: 047 (`047-async-mutex-drain-reap`, PR #143). Branched off `main` (the shipped 006/v1.x `async_mutex`); 047's converging-loop/Dekker/feeder approach is abandoned, not extended.

## Summary

`fixpp::sync::async_mutex::cancel_and_drain()` carries cross-thread "convergence" machinery (a lazily-published `drain_latch_state` over an `asio::experimental::concurrent_channel`, the `draining_`↔counter handshake, and an async quiescence park in reaper step (h)) whose **sole purpose** is to let the reaper *wait for acquirers/holders/resumptions running on OTHER threads to quiesce*. The two DRAIN consumers (Session write-gate `session.cpp:1524`, SeqnumManager `seqnum_manager.hpp:146`) access their mutex only on the per-session strand, and cancellation emission is itself marshaled onto that same strand (`engine.cpp:1255-1281`), so **no acquirer/holder/resumption is ever concurrently in-flight on another thread** when `cancel_and_drain()` runs. (The two LOCK-ONLY consumers, MemoryStore + FileStore, never drain.) The machinery is therefore dead production capability that nonetheless leaves a residual multi-threaded lost-wake (047 W-B1).

This feature **narrows the `cancel_and_drain()` overlap contract to the strand-serialized topology** (ordinary cross-thread `async_lock`/`unlock` stays supported) and replaces the cross-thread machinery with a **single strand-local quiescence loop** (reap both lists → break iff no holders ∧ `in_flight_resumers_==0` ∧ both lists empty → else `co_await asio::post` and repeat), terminating under a drain precondition (not an O(holder-count) bound). It also makes the `inherited_slot.assign` site that currently escapes a `noexcept` boundary **fail closed** (reusing the existing `error::sync_lock_alloc_failed`); the resume/yield post OOM-terminate is a pre-existing residual deferred as L-048. No C-ABI surface change (the error variant already exists), but `sizeof(async_mutex)` changes (C++ layout / header-recompile). `active_acquirers_count_` is REMOVED (vestigial); `in_flight_resumers_`/`draining_complete_` are added (strand-local). Removing `drain_latch_ptr_` reduces 046's libc++ `atomic<shared_ptr>` fallback consumer set 4→3.

## Technical Context

**Language/Version**: C++20 (coroutines, concepts), clang/llvm22 + gcc; libstdc++ (Tier-1) and libc++ (Tier-3) lanes.
**Primary Dependencies**: Boost.Asio (standalone asio) coroutine/cancellation primitives; the project's `expected_t`, `error` enum, `slot_allocator`.
**Storage**: N/A (in-memory synchronization primitive).
**Testing**: GoogleTest seam suite under `tests/sync/` (+ `tests/session/` lifecycle); 6-preset sanitizer matrix (debug/release/ASan/UBSan/TSan-libstdc++/gcc-release); `bench/` micro-benchmarks; mutation-revert discrimination.
**Target Platform**: Linux (Tier-1/3); the redesign is platform-agnostic and reduces the libc++ fallback footprint.
**Project Type**: Header-mostly C++ library (the primitive lives in `include/fixpp/core/sync/async_mutex.hpp`).
**Performance Goals**: No regression vs main (the shipped baseline); 048 branches off main so the 047 +16%/+32% feeder regression is not on main (recovery mentioned only vs the abandoned 047 branch). Drain cost is O(N) waiters; prompt completion rests on the drain precondition, not an O(holder-count) bound.
**Constraints**: Build caps -j2; sanitizer presets ONE AT A TIME (WSL2 ~11 GB OOM risk). No new public/ABI/wire/codegen surface. E-3 (always-post resumption) preserved.
**Scale/Scope**: One header (+ its design doc `.specify/2f-async-mutex.md` amendment), the `error` enum doc (no new code), ~10 drain-machinery test files retired/rewritten + new strand-local witnesses, the 4 consumers unchanged.

## Constitution Check

*GATE: must pass before Phase 0; re-checked after Phase 1.*

- **Article XI (concurrency / no std::mutex in awaitable headers)** — PASS/strengthened. The redesign removes cross-thread atomics machinery; `async_mutex` remains the only legal coroutine-context mutex shape; the §XV.9 no-std-mutex corpus gate is unaffected (no std::mutex introduced). The narrowed contract is *more* aligned with §XI's single-serialization-domain discipline (design-doc §1.1 already states v1.0 hot paths run inside one session serialisation domain with structurally-zero contention).
- **Threading trigger ([const §XVI.3])** — TRIGGERED → `/speckit-clarify` ran (2 clarifications recorded). This is a concurrency-contract change ⇒ Gate A mandatory ([const §XVII.1]) and the 4 threading controls apply.
- **ABI ([const §X.1] frozen ABI)** — PASS (C ABI). No new public symbol, no enum renumber: `error::sync_lock_alloc_failed = 44` already exists with its C-ABI mapping; the OOM fix reuses it (abidiff-clean, verified in verify). **However `sizeof(async_mutex)` CHANGES** (removing `drain_latch_ptr_`/`drain_latch_state`, adding `in_flight_resumers_`/`draining_complete_`) — a C++ object-layout change. `async_mutex` is embedded by value in seqnum_manager/memory_store/file_store, so this is a **header-recompile-required** change (header-mostly library; NOT a runtime `.so` break). FR-008 claims "C ABI frozen; C++ layout changes → header-recompile; layout golden re-baselined" — NOT "no ABI change." A compile-time `sizeof`/`alignof` layout golden is added.
- **§XII.5 no-implicit-default / fail-closed** — PASS/strengthened. The `inherited_slot.assign` OOM site moves from `std::terminate()` (process death) to fail-closed `sync_lock_alloc_failed` (the resume/yield-post OOM-terminate is a pre-existing residual, deferred L-048). The drain-overlap misuse path is documented unsupported (no production assertion seam — P2-4; test-only instrumented executor).
- **Article XVII (gates)** — full pipeline applies (contract change; the tests-only Gate-A waiver does NOT apply). Gate A before /tasks; Gate B before merge.
- **No security/wire/codegen trigger** — the primitive is transport/parse-agnostic; no `reason_class`/`fixpp_error_t`/codegen/C-ABI enumerator change.

**Result: PASS — no unjustified violations.** The single deviation from a pure "no-behavior-change" posture (narrowing the advertised drain-overlap contract) is the whole point of the feature and is design-doc-anchored (new Erratum **E-5**, §4.7 amendment matrix).

## Project Structure

### Documentation (this feature)

```text
specs/048-async-mutex-strand-reap/
├── plan.md              # this file
├── research.md          # Phase 0 — decisions + reachability proof + OOM approach
├── data-model.md        # Phase 1 — mutex member set BEFORE/AFTER, waiter_record, terminal condition, layout golden
├── quickstart.md        # Phase 1 — build/test/witness recipe
├── contracts/
│   └── async_mutex-contract.md   # narrowed cancel_and_drain contract + async_lock/unlock invariants
└── checklists/
    └── requirements.md  # spec quality (closed at specify/clarify)
```

### Source Code (repository root)

```text
include/fixpp/core/sync/async_mutex.hpp   # THE change: remove cross-thread machinery; synchronous strand-local reap (terminal: holders+in_flight_resumers_+lists); fail-closed inherited_slot.assign; layout golden
include/fixpp/core/error.hpp              # NO CHANGE (sync_lock_alloc_failed already present) — verify only
.specify/2f-async-mutex.md                # design-doc amendment: new Erratum E-5 (strand-local reap) + §-ID amendment matrix (§1.1/§3.1/§4.1/§4.5/§4.7.3/§4.7.4); no E-5/I-33 to retire (E-1..E-4 only in merged doc)
spec/behaviors-and-limitations.md          # B-048 (reliable strand-local drain) + L-048 (cross-thread drain unsupported); supersede B-047-1
spec/feature-catalogue.md / coverage-index.md   # NFR-016 row amendment (S-048), coverage index
tests/sync/                                # RETIRE/REWRITE the cross-thread drain witnesses; ADD strand-local reap + OOM-fail-closed witnesses
src/session/, include/fixpp/session/       # consumers UNCHANGED (FR-007) — only re-verified
```

## Design approach (Phase 1 summary — full detail in research.md / data-model.md / contracts/)

1. **Single strand-local quiescence loop.** `cancel_and_drain()` (currently `async_mutex.hpp:1061-1233`) becomes: set `draining_`; then `for(;;){ reap_both_lists() /*exchange state_+next_drain_head_; each waiter: CAS queued→cancelled, result sync_lock_aborted (shipped, NOT sync_lock_drained), schedule_record_resume (posted; ++in_flight_resumers_ inside the helper — sole owner)*/; if (active_holders_count_==0 && in_flight_resumers_==0 && both_lists_empty) break; co_await asio::post(executor, use_awaitable); }` — the yield lets pre-drain holders' `unlock()` AND posted resumers run; terminates under the drain precondition. Finalize: CAS `state_ → not_locked`, then `draining_complete_=true` (ordered after). A reentrant drain AWAITS `draining_complete_`, does not return ok eagerly. No latch, no channel, no `async_wait`, no reaper cancel-slot.
2. **Remove** (data-model.md): `drain_latch_ptr_`, `drain_latch_state` + its `concurrent_channel`/`signal_release`/`signal_abort`/`notify`/`async_wait`, the lazy-latch publish/subscribe steps (a/b/c), the reaper quiescence park (h) + reaper `reaper_slot.assign` + abort path, **and `active_acquirers_count_` (now vestigial — no reader under the corrected terminal condition).** **ADD** `in_flight_resumers_` + `draining_complete_` (strand-local). `draining_`, `drain_in_progress_`, `active_holders_count_` are retained, demoted to plain/relaxed ordering.
3. **Retain** (contracts/): `waiter_record` + refcount (UNCHANGED — no `post_storage_`), the always-post resumption (`resume_fn_`/E-3), `on_cancel` + the `phase_` `queued→{cancelled,granted}` CAS arbitration, `state_`/`next_drain_head_` encoding, `unlock()`'s walk.
4. **OOM fail-closed (US2/FR-003), narrowed scope:**
   - `reaper_slot.assign` (1177): **ELIMINATED** by removing the reaper park.
   - `inherited_slot.assign` (862): **fail-closed** → `sync_lock_alloc_failed` via the awaiter/stored-handler path with the exact ref-balance (NOT a copy of the `:849-856` store_executor exit, which precedes `store_handler`).
   - resume `asio::post` (614) + the holder-yield post: **pre-existing OOM-terminate class, deferred as L-048** (same as 047's L-047-2). The non-allocating-post redesign is dropped; 048 makes no non-allocating claim.
5. **Misuse contract (US3/FR-006):** document strand-overlap-unsupported; ordinary cross-thread `async_lock`/`unlock` stays supported. No production assertion seam (`async_mutex` stores no executor, P2-4) — the unsupported overlap is exercised only by a test-only instrumented executor.

## Complexity Tracking

| Item | Why it is acceptable |
|---|---|
| Narrowing the advertised cross-thread DRAIN-OVERLAP contract | The cross-thread drain-overlap capability is dead production code (source-verified strand-confined drain) AND broken (047 W-B1 residual orphan). Narrowing removes a maintenance liability and a real lost-wake; design-doc-anchored (E-5). Ordinary cross-thread `async_lock`/`unlock` is preserved. |
| Adding `in_flight_resumers_` + `draining_complete_` (strand-local) | The terminal-condition barrier (keeps the mutex alive until posted resumers run) + the reentrant-drain completion signal — replacing the removed cross-thread latch with two plain strand-local members. |
| Retiring ~10 drain-machinery test files | They test removed machinery; replaced by strand-local witnesses + the OOM-fail-closed witness + the 3 new RED witnesses (immediate-destroy-after-reap, reentrant-during-active-drain, on-strand-cancel-during-reap). The cross-thread WB witness is repurposed (test-only instrumented executor), not deleted-to-green. |

## Phase gates

- Phase 0 (research.md) — decisions + the per-instance strand-serialization reachability proof + the OOM scope (inherited_slot fail-closed; resume/yield deferred L-048) + alternatives. **Done in this run.**

## Gate A

- Round 1 applied 2026-06-22: Codex P1=5 P2=8 P3=4; Opus post-judging P1=6 P2=11 P3=6; rewrite addresses root causes #1 terminal-condition, #2 holder-liveness, #3 contract-scope, #4 ABI-accounting, #5 OOM-scope, #6 amendment-matrix. Reviews: research/reviews/codex_048-async-mutex-strand-reap_gate_a_review.md, research/reviews/opus_048-async-mutex-strand-reap_gate_a_adversarial_review.md
- Round 2 applied 2026-06-22: Codex P1=1 P2=2 P3=3; Opus post-judging P1=1 P2=2 P3=4 (RC#2/#3/#4/#6 CLOSED). Rewrite #2 (localized): RC#1 — replace the holder-only yield with a SINGLE unified quiescence loop keyed on the whole terminal condition (holders ∧ in_flight_resumers_ ∧ empty lists) so a no-holder N-waiter reap waits for posted resumers (fixes the round-2 P1-1 control-flow gap); RC#5 — normative posted `inherited_slot.assign` catch with the exact ref-balance; P2-3 resumer-counter ownership precise; P3 — remove vestigial `active_acquirers_count_` (synchronous-initiation interleaving argument); P3-4/5/6 wording. Reviews: research/reviews/codex_048-async-mutex-strand-reap_gate_a_2_review.md, research/reviews/opus_048-async-mutex-strand-reap_gate_a_2_adversarial_review.md
- Phase 1 (data-model.md, contracts/, quickstart.md) — member-set delta, narrowed contract, build/test recipe. **Done in this run.**
- Next: `/gate-a 048-async-mutex-strand-reap` (before `/tasks`).
