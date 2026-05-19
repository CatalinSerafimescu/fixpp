---
description: "Task list — 006-async-mutex (awaitable mutex fixpp::sync::async_mutex)"
---

# Tasks: Awaitable Mutex `fixpp::sync::async_mutex`

**Input**: Design documents from `specs/006-async-mutex/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/, quickstart.md
**Design anchor**: `.specify/2f-async-mutex.md` **v1.5** (Gate-A-converged). On any conflict the design doc wins; an inconsistency is a defect in these tasks.

**Tests**: REQUIRED. This is a threading/concurrency feature under `[const §VII.3]` (TDD red-green-refactor) with 29 named test seams (`plan.md` Test-seam map) and `SC-001..SC-009`. Test tasks are written FIRST and MUST FAIL before the implementation task that makes them pass.

**Coverage objective (user input)**: Beyond the `[const §IX.1]` floor (≥95% line / ≥85% branch, lcov DA/BRDA basis, `SC-009`), drive **100% line coverage and the maximum achievable branch coverage** on `include/fixpp/core/sync/async_mutex.hpp` (+ `src/core/sync/async_mutex.cpp` if out-of-line). Genuinely-unreachable DA/BRDA (HALO-elided paths, `if constexpr` per-instantiation-dead branches, the `std::terminate()` precondition) are justified in writing per `feedback_coverage_gate_lcov_basis`, never padded with impossible-path tests. The dedicated hardening pass is **T072**; every per-story "seams green" task also re-measures incremental coverage.

**Organization**: Tasks grouped by user story (P1→P3). Each story is an independently testable increment.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: parallelizable (different files, no incomplete dependency)
- **[Story]**: US1..US5 (Setup/Foundational/Polish carry no story label)
- The single header `include/fixpp/core/sync/async_mutex.hpp` is touched by most implementation tasks, so those are **sequential (no [P])** within and across stories; test files and fixtures are distinct files and are [P].

## Path Conventions

Library submodule root: `research/G19-fix-fpml-iso20022/library/`. All paths below are relative to that root.

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: directory layout, build wiring, pre-`/implement` toolchain gate.

- [X] T001 Create directory tree: `include/fixpp/core/sync/`, `tests/sync/`, `tests/sync/fixtures/`, `bench/sync/`, `bench/baselines/sync/` (`include/fixpp/session/` already exists).
- [X] T002 [P] Author `tests/sync/CMakeLists.txt`: `add_sync_test` helper + `^sync_` prefix + fixtures/grep-gate env + the explicit glob-free 29-seam→file map (registrations land red-first per the tests/wire incremental TDD precedent); `add_subdirectory(tests/sync)` wired into the parent test CMake.
- [X] T003 [P] Author `bench/sync/` CMake targets `bench_async_mutex_uncontended` + `bench_async_mutex_contended` (compiling Phase-1 placeholders; bodies in T031) and `bench/baselines/sync/async_mutex_baselines.json` seed placeholder (`[const §VIII.2]`).
- [X] T004 [P] STL-availability probe (`quickstart.md §0`, research.md D-4): **PASS** on g++/clang++ libstdc++ (the matrix every Linux Conan profile pins); **FAIL** on libc++ (LLVM 22 lacks usable P0718 `atomic<shared_ptr>`) — libc++ user-approved as out-of-matrix/Tier-2-waived (unprovisioned by any Conan profile; MSVC-STL OK). Verdict recorded in `research.md` D-4.
- [X] T005 Add CMake library target `fixpp_sync` (INTERFACE — header-dominant per `plan.md` Structure decision, fixpp_session precedent; optional out-of-line `src/core/sync/async_mutex.cpp` deferred); configure-clean, precedent-parity verified.

**Checkpoint**: build graph resolves; T004 probe GREEN on every supported STL (blocks all implementation).

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: error variants + every type skeleton so the 29 test files compile and FAIL (red) before per-story implementation turns them green.

**⚠️ CRITICAL**: No user-story work begins until this phase is complete. T004 (STL probe) must be GREEN.

- [X] T006 Additive edit `include/fixpp/core/error.hpp`: append `sync_lock_aborted = 43`, `sync_lock_alloc_failed = 44`, `sync_lock_outside_session = 45`, `sync_lock_drained = 46` (non-renumbering per `[const §X.4]`; data-model "Error Slot Allocation"; FR-012). Do not renumber slots 1–42.
- [X] T007 [P] Define `fixpp::sync::completion_policy` enum (`dispatch=0` default / `post=1`) in `include/fixpp/core/sync/async_mutex.hpp` with SPDX `AGPL-3.0-or-later` header + BSL-1.0 algorithm attribution (avast/asio-mutex / cppcoro / Lewis-Baker) per `[const §V.4]` (E6).
- [X] T008 [P] Define `fixpp::sync::detail::waiter_phase` enum (`queued=0`/`granted=1`/`cancelled=2`) in the same header (E2 phase machine).
- [X] T009 Declare `fixpp::sync::detail::slot_allocator` (E5) skeleton in the header: ctor `(async_mutex_awaiter*, std::pmr::memory_resource*)`, allocator-concept members; three-case body stubbed (filled in US4 T058).
- [X] T010 Declare `fixpp::sync::detail::drain_latch_state` (E4): members `released_`/`aborted_`/`in_flight_resumptions_`/channel; method signatures `wait()/notify()/signal_release()/signal_abort()` stubbed (filled in US3 T048).
- [X] T011 Define `fixpp::sync::detail::async_mutex_awaiter` (E2) struct skeleton: `alignas(8)`, all fields from data-model E2 layout incl. inline `slot_storage_[32]`, declarations of `await_ready/await_suspend/await_resume/on_cancel`; add `static_assert(alignof(async_mutex_awaiter) >= 8)` AFTER the struct and the `sizeof ≤ 96 B` budget assert (FR-013; data-model compile-time invariants).
- [X] T012 Define full `fixpp::sync::async_lock_guard` (E3): public default + move ctor; **private** engaged ctor `[[clang::lifetimebound]]` + `friend class detail::async_mutex_awaiter`; deleted copy; **destructive** `operator=(&&)` (unlock-then-take, self-assign no-op); `~async_lock_guard()`; `[[nodiscard]] release()`; `[[nodiscard]] owns_lock()` (RC#1/N-P1-3; FR-003).
- [X] T013 Define `fixpp::sync::async_mutex` (E1) class skeleton: all atomic members from data-model E1 (`state_`, `next_drain_head_`, `draining_`, `drain_in_progress_`, `active_holders_count_`, `active_acquirers_count_`, `drain_latch_ptr_`, `policy_`); `not_locked`/`locked_no_waiters` constants; the lock-freedom `static_assert`s (FR-013); `constexpr async_mutex()` + `explicit constexpr async_mutex(completion_policy)`; deleted copy/move; `~async_mutex()` firing `std::terminate()`/`abort_invariant` when held or waiters present (FR-008); declarations only for `async_lock(mr=nullptr)`/`unlock()`/`cancel_and_drain()`/`policy()`. No public `try_lock()` (FR-001/FR-015).
- [X] T014 [P] Create declaration-only header `include/fixpp/session/async_lock_via_session_executor.hpp` (E7): SPDX header + the exact `[2f §4.3.2]` signature `[[nodiscard]] asio::awaitable<expected_t<fixpp::sync::async_lock_guard>> async_lock_via_session_executor(fixpp::sync::async_mutex&) noexcept;` — **declaration only; implemented by the session-module spec, not this feature** (RC#2; FR-011). Note (I1): `sync_lock_outside_session` (slot 45) is a declaration-contract error variant only — no runtime path is implemented in this feature, so it is **not coverage-applicable** per SC-009's declaration-only benignity clause (do not flag it uncovered).
- [X] T015 [P] Scaffold `tools/check_no_std_mutex_in_awaitable_headers.sh` (owned/finalized by this feature per FR-014; not present on this branch — created from scratch here): post-preprocessing (`-E`) scope, exit-non-zero when any header that pulls `asio::awaitable<...>` names a member of the **FR-014 six-type banned set** (`std::mutex`, `std::recursive_mutex`, `std::timed_mutex`, `std::recursive_timed_mutex`, `std::shared_mutex`, `std::shared_timed_mutex`). Corpus/diagnostic/CI wiring completed in US5.

**Checkpoint**: every type named in data-model exists; all 29 seam files (added per story) compile and FAIL.

---

## Phase 3: User Story 1 — Acquire/release exclusive ownership in a coroutine (P1) 🎯 MVP

**Goal**: uncontended single-CAS fast path; contended suspend→FIFO-fair resume on the waiter's bound executor; RAII guard with destructive move (FR-002/003/004; SC-001).

**Independent Test**: two coroutines on one strand against a shared `async_mutex` via a test executor — uncontended acquirer gets the guard with no suspension; second acquirer suspends then resumes exactly on first guard destruction; mutual exclusion never overlaps; FIFO across a multi-waiter drain cycle.

### Tests for User Story 1 (write FIRST — must FAIL)

- [X] T016 [P] [US1] Seam #1 `tests/sync/test_uncontended_latency.cpp` — uncontended fast-path, no suspension, valid guard (`[2f §9 #1]`).
- [X] T017 [P] [US1] Seam #2 `tests/sync/test_contended_latency.cpp` — second acquirer suspends without busy-wait (`[2f §9 #2]`).
- [X] T018 [P] [US1] Seam #3 `tests/sync/test_fifo_fairness.cpp` — FIFO fairness across a drain cycle (LIFO reversed on unlock) (`[2f §9 #3]`/`[2f §4.5.2]`).
- [X] T019 [P] [US1] Seam #6 `tests/sync/test_contention_stress.cpp` — ≥10⁴ coroutines, zero overlap, zero starvation, zero lost waiter (`[2f §9 #6]`; SC-001).
- [X] T020 [P] [US1] Seam #11 `tests/sync/test_executor_compat.cpp` — completion on the awaiter's bound executor (`[2d §7.4]`; `[2f §9 #11]`).
- [X] T021 [P] [US1] Seam #12 `tests/sync/test_dispatch_vs_post.cpp` — `dispatch` (inline iff `running_in_this_thread()`) vs `post` per-mutex policy (`[2f §9 #12]`/`[2f §4.6]`).
- [X] T022 [P] [US1] Seam #13 `tests/sync/test_cross_strand_acquire.cpp` — cross-strand resume via `post`, FIFO-fair drain (`[2f §9 #13]`/`[2f §6.1.3]`).
- [X] T023 [P] [US1] Seam #20 `tests/sync/test_guard_destructive_move.cpp` — destructive move-assign releases prior contents; self-assign no-op; uses `async_mutex_awaiter` friend access for the engaged ctor (`[2f §9 #20]`).
- [X] T024 [P] [US1] Seam #27 `tests/sync/test_unlock_reaper_splice.cpp` — unlock-vs-reaper splice race closure, RC-α/Opus C-R3-P1-3 (`[2f §9 #27]`).
- [X] T025 [P] [US1] Seam #28 `tests/sync/test_result_write_race.cpp` — `*result_` CAS-then-publish: only the CAS winner writes (`[2f §9 #28]`/v1.4; I-06/I-09).

### Implementation for User Story 1

- [X] T026 [US1] `async_mutex::async_lock(mr=nullptr)` awaitable factory in `async_mutex.hpp`: uses `asio::async_initiate<use_awaitable_t, void(expected_t<async_lock_guard>)>`; fast-path CAS inline; slow-path stores handler in frame-local awaiter.slot_storage_ (Erratum E-1: no raw `new async_mutex_awaiter`); `active_acquirers_count_.fetch_add` (I-20) at entry; `[[nodiscard]] noexcept`.
- [X] T027 [US1] Fast-path CAS `not_locked→locked_no_waiters` (I-01) inside the initiation lambda; winner-only `active_holders_count_++` (I-17); `active_acquirers_count_--` decrement-points (I-21); draining pre-check (I-15).
- [X] T028 [US1] Slow-path LIFO push CAS retry in the initiation lambda (I-02/I-03); defense-in-depth draining check (I-14); mid-push `not_locked` → direct lock path; `active_acquirers_count_--` at all 3 decrement points (I-21).
- [X] T029 [US1] Waiter resume path: `phase_.load(acquire)` (I-08) determines whether to return an engaged guard or an error result; handler stored in awaiter.slot_storage_ via placement-new (Erratum E-1 — replaces `resume_fn_` / `std::move_only_function`).
- [X] T030 [US1] `async_mutex::unlock()`: walk `next_drain_head_` first (RC-A; exchange I-11, splice I-12), then LIFO from `state_` (exchange I-04, empty close-out CAS I-05); first non-cancelled waiter `phase_` CAS `queued→granted` (I-06) winner-only + `active_holders_count_` inc (I-17); drain short-circuit (I-16); `active_holders_count_` dec at entry (I-18); invokes awaiter via `invoke_handler()` (no `delete` — frame-local node).
- [X] T031 [US1] `async_lock_guard` dtor/`release()`/destructive move-assign all route to `mutex_->unlock()`; bench body placeholders compile.
- [X] T032 [US1] All 10 US1 seams (T016–T025) GREEN under `linux-clang-debug`; mutual exclusion verified (overlap==0 in N=10,000 coroutine stress); FIFO verified (acquire_order==enqueue_order). lcov coverage note: hot-path branches (fast-path CAS, LIFO push, FIFO drain, destructive move-assign, release) all exercised; uncovered paths for T072: draining_ short-circuit (I-16, needs cancel_and_drain — US3), drain_latch notify (US3), all cancelled-waiter skip branches (need cancellation — US2), PMR mr!=nullptr path (US4).
  - **Rework note (2026-05-18, Erratum E-1):** US1 reworked to Erratum-E-1 conformance — no raw `new async_mutex_awaiter` and no `std::move_only_function`; awaiter is frame-local in `async_lock()`; handler stored via placement-new in `slot_storage_` (32 B); all 10 seams remain GREEN.

**Checkpoint**: US1 fully functional and independently testable (MVP).

---

## Phase 4: User Story 2 — Cancel a pending acquire deterministically (P1)

**Goal**: `cancellation_type::total` removes the waiter and completes with `sync_lock_aborted`; cancel-vs-drain CAS-arbitration yields exactly one of {granted, cancelled} per waiter, resumed exactly once, TSan-clean (FR-005; SC-002).

**Independent Test**: suspend a waiter, signal `cancellation_type::total`, assert `sync_lock_aborted` + waiter removed; then fire cancellation concurrently with the holder's release and assert exactly one of {granted, cancelled}, no double-resume, no lost waiter (TSan-clean).

### Tests for User Story 2 (write FIRST — must FAIL)

- [X] T033 [P] [US2] Seam #4 `tests/sync/test_cancellation_mid_wait.cpp` — mid-wait cancel ⇒ `sync_lock_aborted`, removed, no ownership (`[2f §9 #4]`/`[2f §4.5]`). GREEN debug + `linux-clang-tsan`.
- [X] T034 [P] [US2] Seam #15 `tests/sync/test_race_cancel_pre_drain.cpp` — cancel-after-detach-pre-drain race (RC#1; `[2f §9 #15]`). GREEN debug + TSan.
- [X] T035 [P] [US2] Seam #16 `tests/sync/test_race_multi_cancel.cpp` — multi-cancel-same-list race (RC#1; `[2f §9 #16]`). GREEN debug + TSan (A-fix: missing `ioc.restart()` corrected in seam).
- [X] T036 [P] [US2] Seam #17 `tests/sync/test_race_cancel_during_resume.cpp` — cancel-during-`await_resume` race (RC#1; `[2f §9 #17]`). GREEN debug + TSan (B-fix: scoped cancellation-state restore; A-fix: per-round `ioc.restart()`).
- [X] T037 [P] [US2] Seam #22 `tests/sync/test_residual_cancel_graceful.cpp` — residual-chain cancellation under graceful close (RC-A; `[2f §9 #22]`). GREEN debug + TSan (A-fix: missing `ioc.restart()`).

### Implementation for User Story 2

- [X] T038 [US2] `async_mutex_awaiter::on_cancel(cancellation_type)`: CAS `phase_ queued→cancelled` (I-07, acq_rel); winner writes terminal `unexpected{sync_lock_aborted}` + schedules resumption on bound executor; CAS-loss ⇒ no-op (drain won). Implemented per **Erratum E-2** on `waiter_record::phase_`/`result_` (stable node).
- [X] T039 [US2] Integrate the §4.5 cancel-vs-drain CAS-arbitration across `on_cancel` ↔ `unlock`/reaper grant: exactly one winner per waiter, resumed once, no lost/double waiter (RC#1; I-06/I-07 CAS-then-publish; SC-002). Verified: all RC#1 race seams TSan-clean.
- [X] T040 [US2] US2 seams (T033–T037) GREEN; **all 15 sync_ seams GREEN under `linux-clang-debug` AND `linux-clang-tsan` (0 TSan warnings)** — independent parent re-verification. Remediation beyond Codex's E-2 impl: B (cancellation-state leak), A (3× `ioc.restart()` harness defects), **Erratum E-3** (always-post waiter resumption — re-entrancy heap-UAF), asio handler move-before-invoke discipline (frame-local awaiter `slot_storage_` UAF). E-2 static zero-global-new verified by source inspection (contended `mr==nullptr` = pool + placement-new only). Incremental cancel-branch coverage re-measure deferred to T072 (lcov pending, task #9).

**Checkpoint**: US1 + US2 both pass independently; cancellation race TSan-clean.

---

## Phase 5: User Story 3 — Shut down safely: drain waiters, never silently corrupt (P2)

**Goal**: `cancel_and_drain()` completes every in-flight waiter exactly once and returns only after holder/acquirer/resumption counts reach zero; post-drain `async_lock()`⇒`sync_lock_drained` no-enqueue; destroy-with-waiters ⇒ `std::terminate()` (FR-006/007/008; SC-003).

**Independent Test**: queue N waiters, `cancel_and_drain()`, assert all completed once + post-drain `async_lock()`⇒`sync_lock_drained` + return only after counts zero; separate death test: destroy a mutex with a live waiter ⇒ `std::terminate()`.

### Tests for User Story 3 (write FIRST — must FAIL)

- [X] T041 [P] [US3] Seam #5 `tests/sync/test_destructor_release_death.cpp` — destructor-with-waiters fires `std::terminate()` (release-linkage death test; `[2f §9 #5]`/`[2f §4.7]`). GREEN debug + **`linux-clang-release`** (3/3) + TSan.
- [X] T042 [P] [US3] Seam #19 `tests/sync/test_cancel_and_drain.cpp` — reaps every in-flight waiter exactly once (RC#3; `[2f §9 #19]`). GREEN debug + TSan.
- [X] T043 [P] [US3] Seam #23 `tests/sync/test_cancel_and_drain_concurrent.cpp` — concurrent `cancel_and_drain` serialised into one epoch (RC-B; `[2f §9 #23]`). GREEN debug + TSan.
- [X] T044 [P] [US3] Seam #24 `tests/sync/test_drain_latch_holder_lifecycle.cpp` — lazy `drain_latch_state` + pre-drain holder lifecycle (RC-β; `[2f §9 #24]`). GREEN debug + TSan.
- [X] T045 [P] [US3] Seam #25 `tests/sync/test_in_flight_acquirer_coverage.cpp` — in-flight acquirer epoch window closed (RC-α/Opus C-R3-P1-2; `[2f §9 #25]`). GREEN debug + TSan.
- [X] T046 [P] [US3] Seam #26 `tests/sync/test_drain_awaitable_cancellation.cpp` — `cancel_and_drain` awaitable's own cancellation ⇒ `sync_lock_aborted` (RC-β; `[2f §9 #26]`). GREEN debug + TSan.
- [X] T047 [P] [US3] Seam #29 `tests/sync/test_drain_reaper_abort_subscribers.cpp` — reaper cancellation wakes all subscribers with abort outcome (v1.4; `[2f §9 #29]`). GREEN debug + TSan.

> **US3 seam sequencing note (recorded for `/gate-a`).** The 7 seams were authored by a Sonnet subagent under TDD; 6 subtests were initially mis-sequenced (released the holder *before* `cancel_and_drain()` set `draining_`, contradicting the §4.7.4 canonical graceful-close discipline) and were corrected by the parent (Opus) to the canonical "drain concurrently while the holder still holds" pattern — **assertions unchanged, no implementation weakened**; the correctly-sequenced sibling subtests passed throughout, confirming T048/T049 correctness independently.

### Implementation for User Story 3

- [X] T048 [US3] `detail::drain_latch_state` implemented: `asio::experimental::concurrent_channel`-backed multi-waiter latch — `async_wait()` (direct as_tuple receive; cancel delivered as a value), `notify()` (non-terminal re-check, I-8), `signal_release()`/`signal_abort()` (terminal, channel `close()` wakes all, I-7), `released_`/`aborted_` (I-25/I-26/I-27), `in_flight_resumptions_`; executor captured lazily ([2f §4.7.3] I-1/I-3; mutex stays constexpr).
- [X] T049 [US3] `async_mutex::cancel_and_drain()` reaper implemented per [2f §4.7.2] (a)–(j), translated onto Erratum-E-2 `waiter_record` + Erratum-E-3 posted resumption: idempotent fast-path (a); `drain_in_progress_` serialiser + epoch subscribe (b); lazy `make_shared<drain_latch_state>`, `drain_latch_ptr_` published BEFORE `draining_` (c; I-23 before I-13); reverse-LIFO + FIFO reap, winner-only CAS (e/f); stable re-walk loop (g); counter-quiesce wait with explicit cancellation-slot handler + try/catch converting asio's cancel exception to `unexpected{sync_lock_aborted}` (h; [2f §4.7.3] I-5); finalize + `signal_release` (i/j).
- [X] T050 [US3] `~async_mutex()` hard-precondition `std::terminate()` (held or waiters in `state_`/`next_drain_head_`) verified in debug AND `linux-clang-release`; seam #5 release-linkage death test GREEN 3/3 (FR-008; no `expected_t` variant).
- [X] T051 [US3] Drained fast-fail verified: the E-2 `async_lock` initiation checks `draining_` BEFORE the fast-path CAS and BEFORE `waiter_record` allocation ⇒ `unexpected{sync_lock_drained}`, **no enqueue** (FR-007; seam #19/#24 post-drain assertions GREEN).
- [X] T052 [US3] All US3 seams GREEN debug + TSan; **all 22 sync_ seams GREEN under `linux-clang-debug` AND `linux-clang-tsan` (0 TSan warnings)**; seam #5 death test GREEN under `linux-clang-release` — independent parent re-verification. Incremental drain/teardown coverage re-measure deferred to T072 (lcov pending, task #9).

**Checkpoint**: US1+US2+US3 pass independently; teardown safe.

---

## Phase 6: User Story 4 — Zero global allocation on the hot path; PMR fallback (P2)

**Goal**: embedded HALO path performs zero global `new`/`delete`; explicit `async_lock(mr)` routes storage to the caller resource; PMR exhaustion ⇒ `sync_lock_alloc_failed` trapped (no terminate) (FR-009/010; SC-004).

**Independent Test**: contended acquire/release/cancel/drain under an alloc-counting harness ⇒ zero global new/delete on the embedded path; explicit-`mr` overload ⇒ all fallback allocs hit the supplied resource, none global; PMR exhaustion ⇒ `sync_lock_alloc_failed` (trapped).

### Tests for User Story 4 (write FIRST — must FAIL)

- [X] T053 [P] [US4] Seam #7 `tests/sync/test_tsan_clean.cpp` — TSan-clean under stress (`[2f §9 #7]`/`[const §IX.2]`). GREEN under `linux-clang-tsan` (full 27-seam sync set 27/27, 0 TSan warnings).
- [X] T054 [P] [US4] Seam #8 `tests/sync/test_asan_clean.cpp` — ASan/UBSan-clean under stress (`[2f §9 #8]`). GREEN under `linux-clang-asan` AND `linux-clang-ubsan` (5/5 US4 seams each). asan/ubsan build dirs were stale (pre-asio/1.36.0); resolved via `conan install … --output-folder` per the documented preset-collision workaround.
- [X] T055 [P] [US4] Seam #9 `tests/sync/test_halo_firing.cpp` — awaiter coroutine frame HALO-elided across the compiler matrix (`[2f §9 #9]`/`[2f §6.4]`). `static_assert(sizeof(async_mutex_awaiter) <= 96)` holds (actual 72 B); true frame-elision is §6.4 / §4.3.4 case-2 bench-soft (asserted: size budget + functional correctness).
- [X] T056 [P] [US4] Seam #10 `tests/sync/test_pmr_fallback.cpp` — explicit-`mr` path all-from-resource; exhaustion ⇒ `sync_lock_alloc_failed`; mallocnesia zero-global-heap on embedded path (`[2f §9 #10]`). gtest GREEN debug/TSan/ASan/UBSan; mallocnesia embedded-path `tests/alloc_guard/sync_alloc_guard_test.cpp` PASS via `tools/check_alloc.py` (zero global new/delete, steady state post the **E-4** per-thread cancellation-recycler warm-up).
- [X] T057 [P] [US4] Seam #21 `tests/sync/test_slot_allocator_storage.cpp` — three storage cases: inline 32 B / null-resource overflow-trap / PMR (RC-C; `[2f §9 #21]`). Post-**E-4** the seam unit-tests `detail::slot_allocator` in isolation (case 1 inline + overflow-trap, case 3 PMR forward + exhaustion, case 2 documented N/A). GREEN debug/TSan/ASan/UBSan.

### Implementation for User Story 4

- [X] T058 [US4] Implement `detail::slot_allocator` three-case body. **Re-anchored by Erratum E-4** (asio 1.36.0 `cancellation_slot` has no allocator-binding hook — source-verified; slot_allocator is NOT bound to the cancellation slot, it is the typed storage-policy wrapper for the `waiter_record` fallback, unit-verified by seam #21): case 1 `mr==nullptr` per-mutex `waiter_pool_` (E-2) — overflow→`bad_alloc`→caller trap→`unexpected{sync_lock_alloc_failed}`; case 2 N/A (waiter_record never coroutine-frame-resident); case 3 PMR `polymorphic_allocator<void>{mr}` forward (allocate failure→trap). Header + data-model.md E5 + 2f §4.3.4 amended with E-4 (flagged for `/gate-a`).
- [X] T059 [US4] `async_lock(std::pmr::memory_resource* mr)` PMR-fallback overload: `pmr_waiter_block` allocated from `mr` on the contended path, de-allocated back to `mr` via `waiter_record::release_ref` once refcount hits zero; `core` takes `mr` purely as a parameter — never reaches into `session/`/engine (RC#2; FR-010). Verified by seam #10 (all-from-resource + exhaustion ⇒ `sync_lock_alloc_failed`, no terminate).
- [X] T060 [US4] HALO-eligibility: `async_lock`'s `await_ready`/`await_suspend` equivalents are the inline header-only `async_initiate` lambda (no out-of-line escape); `static_assert(sizeof(async_mutex_awaiter) <= 96)` added and holds (72 B); `tools/check_alloc.py` + `mallocnesia` alloc-guard wired for the embedded path (`tests/alloc_guard/sync_alloc_guard_test.cpp`, CTest `alloc_guard_sync_embedded` — deliberately NOT `sync_`-prefixed so it is not swept into the seam gate) (`[const §VIII.5]`).
- [X] T061 [US4] US4 seams (T053–T057) GREEN: **debug 28/28** (full regression, zero breakage of the 22 prior seams), **TSan 27/27** (mandatory non-waivable gate, 0 warnings), **ASan 5/5**, **UBSan 5/5**; zero global new/delete on the embedded path verified under `mallocnesia` via `check_alloc.py` (the authoritative dynamic zero-new check deferred from Phase 5, now satisfied). Incremental coverage re-measure (alloc/PMR branches) deferred to T072 (lcov pending, task #9).

> **US4 RC-C reconciliation note (recorded for `/gate-a`).** During `/implement` T058 a source-verified design-vs-reality contradiction was found: `2f` §4.3.4 / §4.5 step 4 / §6.1 / §6.3 / NFR-016 prescribe binding `asio::bind_allocator(detail::slot_allocator{this, mr})` to the cancellation slot, but asio 1.36.0's `cancellation_slot::emplace`/`assign` has **no allocator-binding hook** (`prepare_memory()` → `thread_info_base::allocate(cancellation_signal_tag)`; per-thread recycling cache). User-authorized resolution: parent (Opus) authored **Erratum E-4** in `2f` §4.3.4 (additive, like E-1/E-2/E-3) — asio's per-thread recycler is the sanctioned cancellation-handler allocator (zero global new/delete in steady state by construction; one-time per-thread first-touch is §6.4 bench-soft); `slot_allocator` is re-anchored to the `waiter_record` fallback (the allocation 2f controls), unit-verified by seam #21; the substantive zero-global-heap guarantee (the `waiter_record` via E-2 `waiter_pool_`) is unaffected and is verified GREEN under `mallocnesia`. E-4 amends Gate-A-converged `2f` — **flag for `/gate-a` alongside E-2 and E-3** (also reflected in `data-model.md` E5).

**Checkpoint**: US1–US4 pass independently; zero-hot-path-alloc verified.

---

## Phase 7: User Story 5 — CI rejects `std::mutex` in coroutine context (P3)

**Goal**: the `[const §XV.9]` grep gate fails the build when an `asio::awaitable<...>`-including header names `std::mutex`/etc., diagnostic naming `fixpp::sync::async_mutex`; zero FN/FP on the labelled corpus (FR-014; SC-006).

**Independent Test**: run the gate against the labelled corpus — legitimate-`async_mutex` fixtures pass; `std::mutex`-in-awaitable-header fixtures fail with a message naming `async_mutex`; zero false negatives and zero false positives.

### Tests for User Story 5 (write FIRST — must FAIL)

- [X] T062 [US5] Seam #14 `tests/sync/test_no_std_mutex_ci_gate.cpp` — drives the gate over the 3 corpus fixtures and asserts zero FN/FP **per each of the FR-014 six banned spellings** + diagnostic content (`[2f §9 #14]`; SC-006). GREEN: `sync_no_std_mutex_ci_gate` 3 tests / 22 assertions (6 spellings × 3 + 2 + 2); genuine `bash`-driven gate invocation with real exit/stderr capture + env-var guards (parent-verified not a false-green); debug full sync set 28/28.
- [X] T063 [P] [US5] Fixture `tests/sync/fixtures/header_with_std_mutex_and_awaitable.hpp` — deliberate violation, all six FR-014 spellings, one per `#if defined(FX_*)` block; gate fires on every one (zero FN, per-spelling).
- [X] T064 [P] [US5] Fixture `tests/sync/fixtures/header_without_violation.hpp` — legitimate `async_mutex` in an awaitable header; gate does NOT fire (zero FP — the asio-internal-`std::mutex` case the naive grep would have failed).
- [X] T065 [P] [US5] Fixture `tests/sync/fixtures/header_transitive_awaitable_include.hpp` — fixture-owned `std::mutex` with `asio::awaitable` pulled transitively via the impl header; post-`-E` file-attributed scope catches it (`[2f §9 #14]` variant 3).

### Implementation for User Story 5

- [X] T066 [US5] Finalized `tools/check_no_std_mutex_in_awaitable_headers.sh` (Opus-authored — logic-critical): post-`-E` scope WITH line markers kept; **file-attributed** banned-token detection — a token counts only in a NON-system region (line-marker flag-3 absent AND owning file not `/asio/`, libstdc++ `/c++/|/bits/`, `/usr/`, `.conan2/`). This closes the otherwise-guaranteed false positive (asio internally uses `std::mutex`, so a naive `-E` grep fails every legit awaitable header — Codex C-P2-10 intent). Regex = exactly the FR-014 six-type set with identifier boundaries; diagnostic names `fixpp::sync::async_mutex`. Wired as a first-class non-waivable Tier-1 ctest `check_no_std_mutex_corpus` over the **real** awaitable-header corpus (`async_mutex.hpp` + `async_lock_via_session_executor.hpp`), rides the existing Tier-1 ctest/conan env (no separate workflow job); Tier-1 runs the full unfiltered `ctest` (tier1.yml:111) so it gates every preset (`[const §IX.4]`). `using`/`typedef` aliases out of scope (FR-014 recorded limitation).
- [X] T067 [US5] Seam #14 GREEN + the script over the real corpus GREEN — parent-independent verification: zero FN (deliberate direct + transitive violations fire, exact spelling named), zero FP (real `async_mutex.hpp` exits 0 despite asio-internal `std::mutex`) (SC-006). `check_no_std_mutex_corpus` ctest PASS.

**Checkpoint**: all five user stories independently functional.

---

## Phase 8: Polish & Cross-Cutting Concerns

- [ ] T068 [P] Seam #18 `tests/sync/test_arm64_weak_memory.cpp` — ARM64 weak-memory contention stress under TSan; verify the memory-ordering specification I-01..I-31 (FR-013; SC-007; `[2f §6.2.2]`). Run on a Linux-ARM64 host where available; otherwise record host-unavailable in the verify doc.
- [ ] T069 Audit every memory-ordering site against the data-model I-01..I-31 table (atomic + success/failure order) and confirm the FR-013 `static_assert`s (awaiter alignment, pointer round-trip, atomic lock-freedom) compile on every Tier-1 toolchain (SC-007).
- [ ] T070 Sanitizer matrix per `quickstart.md §3`, run serially: `linux-clang-tsan` (mandatory — zero reports is non-waivable) + `linux-clang-asan` + `linux-clang-ubsan` + `linux-gcc-release` over `^sync_` (`[const §IX.2]`/`[const §XVII.8]`; SC-002/SC-009).
- [ ] T071 Bench + ±5% regression gate (`quickstart.md §5`): build `linux-clang-release`, run `bench_async_mutex_uncontended`/`_contended`, commit `bench/baselines/sync/async_mutex_baselines.json`, `tools/bench_compare.py` vs `[2f §6.3]` ceilings (seams #1/#2/#12; drain rows bench-harness-soft) (`[const §VIII.1/2]`; SC-005).
- [ ] **T072 Coverage hardening — 100% line / max branch (USER OBJECTIVE)**: build `linux-clang-coverage`, `ctest -R '^sync_'`, capture lcov; enumerate **every** zero-hit DA line and not-taken BRDA branch in `include/fixpp/core/sync/async_mutex.hpp` (+ `.cpp` if out-of-line); for each, either add a targeted seam case (extend the owning seam file, not a new bloat test) to exercise it, or record it in a written coverage-justification note as genuinely unreachable (HALO-elided duplicate path / `if constexpr` per-instantiation-dead branch / `std::terminate()` precondition / compiler-synthesized) per `feedback_coverage_gate_lcov_basis`. Target: **100% line**, branch coverage maximized (hard floor ≥95%/≥85% per `[const §IX.1]`/SC-009; do not pad against impossible paths). Note (I1): `sync_lock_outside_session` has no runtime path in this feature (session-helper impl is downstream — FR-011/E7) — exclude it as declaration-contract-only, **not** as an uncovered line/branch (SC-009 declaration-only benignity clause).
- [ ] T073 `clang-tidy` + `clang-format` + `cppcheck` + IWYU sweep over `core/sync/` + `session/` headers/sources (`[const §IX.4]`).
- [ ] **T078 Consumer compile/link contract check — SC-008 / `[2e §3.1]` hand-off gate**: add a compile-only consumer translation unit `tests/sync/test_consumer_contract_compile.cpp` (registered in `tests/sync/CMakeLists.txt`, T002) that includes **only** the shipped headers `<fixpp/core/sync/async_mutex.hpp>` + `<fixpp/session/async_lock_via_session_executor.hpp>` and asserts, via `static_assert`/SFINAE, that the shipped surface satisfies the `[2e §6.4]` writer-mutex contract shape and the declared session-side-helper surface — i.e. `2e`/`005`/`2g` can build against 2f. **Positive asserts**: `async_mutex` is `constexpr`-default-constructible, non-copyable, non-movable; `async_lock()/unlock()/cancel_and_drain()/policy()` are well-formed with the contract return types; `async_lock_via_session_executor(async_mutex&)` is declared with the `[2f §4.3.2]` signature; `async_lock_guard` is movable, non-copyable, `sizeof == sizeof(void*)`. **Negative asserts (U1 — FR-015 out-of-scope surface absent)**: no public `try_lock()` member; no `async_shared_mutex`/`async_recursive_mutex`/`async_timed_mutex` typedef/alias; `async_mutex` exposes no public engaged-guard ctor and no CRTP/`concept` extension hook (detect via `requires`/`std::is_*` traits). The TU links against `fixpp_sync` to confirm link-completeness of the shipped non-inline surface. Run GREEN under `linux-clang-debug`. Map: SC-008, FR-015, FR-001, FR-011.
- [ ] T074 Completeness audit: tasks ↔ FR-001..FR-015 ↔ SC-001..SC-010 ↔ `spec/feature-catalogue.md` — 100% coverage or explicitly waived; this is a `/gate-b` precondition (`feedback_feature_completeness_gate`; `[const §XVII.8]`).
- [ ] T075 Apply NFR-016 drop-in (per `[2f §11]`): add the NFR-016 row to `spec/feature-catalogue.md` and the linking entry to `spec/coverage-index.md` (`[2f §4.1]`+`[arch §1.1]`→NFR-016) (SC-010). Appendix D §D.1/§D.2/§D.3 cross-doc drop-ins to `[2d]` are recorded as queued at sign-off, NOT applied here (research D-12).
- [ ] T076 Run `/speckit-verify 006-async-mutex` (mandatory post-`/implement`, `[const §XVII.8]`) → `.specify/decisions/006-async-mutex-verify.md`; require GREEN (steps 0–8 incl. coverage gate from T072). GREEN is the `gate-b-done` precondition.
- [ ] T077 Walk `quickstart.md` §1–§9 end-to-end as a final acceptance pass; record `local build: green on linux-clang-debug @ <git-sha>` for the PR body (`[const §XVII.7]`).

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: no dependencies. T004 STL probe GREEN is a hard gate for all implementation.
- **Foundational (Phase 2)**: depends on Setup. BLOCKS all user stories — all 29 seam files must compile (red) against the skeletons.
- **User Stories (Phases 3–7)**: all depend on Foundational. US1 is the MVP. US2/US3/US4 each extend the same `async_mutex.hpp`/awaiter, so their **implementation** tasks are sequential by priority (US1→US2→US3→US4); US5 (gate) is independent of the runtime primitive and may run in parallel with US1–US4 once Foundational completes.
- **Polish (Phase 8)**: depends on all user stories. T072 (coverage) depends on T070 (sanitizer pass) being clean and all seams green. **T078** (consumer-contract compile/link, SC-008 / `[2e §3.1]` hand-off gate) depends on the full shipped surface (US1–US4 implementation + T014 declaration) and **MUST complete before T074 (completeness audit) and T076 (`/speckit-verify`)** so the hand-off contract is verified before sign-off.

### User Story Dependencies

- **US1 (P1)**: after Foundational. No dependency on other stories. MVP.
- **US2 (P1)**: after US1 implementation (shares awaiter `phase_` arbitration / `unlock` grant path).
- **US3 (P2)**: after US2 (drain composes with the cancel arbitration; reaper re-uses the grant CAS).
- **US4 (P2)**: after US3 (alloc/PMR is verified over the full acquire/cancel/drain surface).
- **US5 (P3)**: after Foundational only; independent of US1–US4 runtime; can run in parallel.

### Within Each User Story

- Tests written and FAILING before implementation (`[const §VII.3]` red-green-refactor).
- Types/enums before awaiter; awaiter protocol before `unlock`/`cancel_and_drain`.
- Story's "seams GREEN" task closes the story and records incremental coverage for T072.

### Parallel Opportunities

- Setup: T002, T003, T004 are [P].
- Foundational: T007, T008, T014, T015 are [P] (distinct files/enums); T009–T013 touch the shared header → sequential.
- Every story's test/fixture tasks are [P] (distinct files); implementation tasks on `async_mutex.hpp` are sequential.
- US5 (T062–T067) can proceed in parallel with US1–US4.
- Polish: T068 is [P] (own test file); T078 is its own consumer-contract file (parallelizable with T072/T073) but must precede T074/T076; T069–T077 sequence around the verify gate.

---

## Parallel Example: User Story 1

```bash
# Launch all US1 seam test files together (all FAIL initially):
Task: "Seam #1 tests/sync/test_uncontended_latency.cpp"
Task: "Seam #2 tests/sync/test_contended_latency.cpp"
Task: "Seam #3 tests/sync/test_fifo_fairness.cpp"
Task: "Seam #6 tests/sync/test_contention_stress.cpp"
Task: "Seam #11 tests/sync/test_executor_compat.cpp"
Task: "Seam #12 tests/sync/test_dispatch_vs_post.cpp"
Task: "Seam #13 tests/sync/test_cross_strand_acquire.cpp"
Task: "Seam #20 tests/sync/test_guard_destructive_move.cpp"
Task: "Seam #27 tests/sync/test_unlock_reaper_splice.cpp"
Task: "Seam #28 tests/sync/test_result_write_race.cpp"
```

---

## Implementation Strategy

### MVP First (User Story 1 only)

1. Phase 1 Setup (T004 STL probe GREEN — hard gate).
2. Phase 2 Foundational (all skeletons; 29 seams compile red).
3. Phase 3 US1 → mutual exclusion + FIFO green under `linux-clang-debug`.
4. **STOP & VALIDATE**: US1 independent test (the awaitable mutex is usable).

### Incremental Delivery

US1 (MVP) → US2 (cancellation, TSan-clean) → US3 (drain/teardown) → US4 (zero-alloc/PMR) → US5 (CI gate, parallelizable) → Polish (sanitizer matrix → **T072 100%-line/max-branch coverage** → bench → verify GREEN → catalogue).

### Coverage Discipline (user objective)

Every story's closing "seams GREEN" task re-measures lcov DA/BRDA on `async_mutex.hpp` and lists not-yet-covered lines/branches; **T072** is the dedicated pass that closes them to 100% line and maximum branch, adding targeted cases to the *owning* seam files (no bloat tests) and writing a justification note for any genuinely-unreachable DA/BRDA. The `[const §IX.1]` 95/85 floor is the non-negotiable gate; 100%/max is the target.

---

## Notes

- [P] = different files, no incomplete dependency. The shared header `include/fixpp/core/sync/async_mutex.hpp` serializes most implementation tasks.
- Design doc `.specify/2f-async-mutex.md` v1.5 wins on any conflict — an inconsistency here is a defect in these tasks.
- TSan is mandatory and zero-reports is a non-waivable Gate B bar (concurrency primitive).
- Pipeline order is canonical at `plan.md` Constitution-Check `[const §XVI.4]` row: `/plan` → Gate A → `/tasks` → `/analyze` → `/implement`. `/speckit-analyze` runs next (after these tasks, before `/speckit-implement`).
- No public `try_lock()` (FR-001/FR-015) — `detail::`/friend-only for seam #20 only.
- Commit after each task or logical group; stop at any checkpoint to validate a story independently.
