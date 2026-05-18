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

- [ ] T001 Create directory tree: `include/fixpp/core/sync/`, `tests/sync/`, `tests/sync/fixtures/`, `bench/sync/`, `bench/baselines/sync/` (`include/fixpp/session/` already exists).
- [ ] T002 [P] Author `tests/sync/CMakeLists.txt`: register all 29 `sync_*` GoogleTest targets (per `plan.md` Test-seam map), the 3 `fixtures/` headers, link `fixpp_sync` + GTest/GMock; wire `add_subdirectory(tests/sync)` into the parent test CMake so `ctest -R '^sync_'` resolves.
- [ ] T003 [P] Author `bench/sync/` CMake targets `bench_async_mutex_uncontended` + `bench_async_mutex_contended` (Google Benchmark) and create `bench/baselines/sync/async_mutex_baselines.json` placeholder (`[const §VIII.2]`).
- [ ] T004 [P] STL-availability probe (`quickstart.md §0`, research.md D-4): compile+run the `std::atomic<std::shared_ptr<int>>` probe on libc++ and libstdc++ (and record MSVC status); a non-compiling/failing STL is a **hard pre-`/implement` blocker** — record the verdict in `research.md` D-4.
- [ ] T005 Add CMake library target `fixpp_sync` (header-inline; optional out-of-line `src/core/sync/async_mutex.cpp` decision deferred to implementation per `plan.md` Structure decision); ensure it builds empty before headers land.

**Checkpoint**: build graph resolves; T004 probe GREEN on every supported STL (blocks all implementation).

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: error variants + every type skeleton so the 29 test files compile and FAIL (red) before per-story implementation turns them green.

**⚠️ CRITICAL**: No user-story work begins until this phase is complete. T004 (STL probe) must be GREEN.

- [ ] T006 Additive edit `include/fixpp/core/error.hpp`: append `sync_lock_aborted = 43`, `sync_lock_alloc_failed = 44`, `sync_lock_outside_session = 45`, `sync_lock_drained = 46` (non-renumbering per `[const §X.4]`; data-model "Error Slot Allocation"; FR-012). Do not renumber slots 1–42.
- [ ] T007 [P] Define `fixpp::sync::completion_policy` enum (`dispatch=0` default / `post=1`) in `include/fixpp/core/sync/async_mutex.hpp` with SPDX `AGPL-3.0-or-later` header + BSL-1.0 algorithm attribution (avast/asio-mutex / cppcoro / Lewis-Baker) per `[const §V.4]` (E6).
- [ ] T008 [P] Define `fixpp::sync::detail::waiter_phase` enum (`queued=0`/`granted=1`/`cancelled=2`) in the same header (E2 phase machine).
- [ ] T009 Declare `fixpp::sync::detail::slot_allocator` (E5) skeleton in the header: ctor `(async_mutex_awaiter*, std::pmr::memory_resource*)`, allocator-concept members; three-case body stubbed (filled in US4 T058).
- [ ] T010 Declare `fixpp::sync::detail::drain_latch_state` (E4): members `released_`/`aborted_`/`in_flight_resumptions_`/channel; method signatures `wait()/notify()/signal_release()/signal_abort()` stubbed (filled in US3 T048).
- [ ] T011 Define `fixpp::sync::detail::async_mutex_awaiter` (E2) struct skeleton: `alignas(8)`, all fields from data-model E2 layout incl. inline `slot_storage_[32]`, declarations of `await_ready/await_suspend/await_resume/on_cancel`; add `static_assert(alignof(async_mutex_awaiter) >= 8)` AFTER the struct and the `sizeof ≤ 96 B` budget assert (FR-013; data-model compile-time invariants).
- [ ] T012 Define full `fixpp::sync::async_lock_guard` (E3): public default + move ctor; **private** engaged ctor `[[clang::lifetimebound]]` + `friend class detail::async_mutex_awaiter`; deleted copy; **destructive** `operator=(&&)` (unlock-then-take, self-assign no-op); `~async_lock_guard()`; `[[nodiscard]] release()`; `[[nodiscard]] owns_lock()` (RC#1/N-P1-3; FR-003).
- [ ] T013 Define `fixpp::sync::async_mutex` (E1) class skeleton: all atomic members from data-model E1 (`state_`, `next_drain_head_`, `draining_`, `drain_in_progress_`, `active_holders_count_`, `active_acquirers_count_`, `drain_latch_ptr_`, `policy_`); `not_locked`/`locked_no_waiters` constants; the lock-freedom `static_assert`s (FR-013); `constexpr async_mutex()` + `explicit constexpr async_mutex(completion_policy)`; deleted copy/move; `~async_mutex()` firing `std::terminate()`/`abort_invariant` when held or waiters present (FR-008); declarations only for `async_lock(mr=nullptr)`/`unlock()`/`cancel_and_drain()`/`policy()`. No public `try_lock()` (FR-001/FR-015).
- [ ] T014 [P] Create declaration-only header `include/fixpp/session/async_lock_via_session_executor.hpp` (E7): SPDX header + the exact `[2f §4.3.2]` signature `[[nodiscard]] asio::awaitable<expected_t<fixpp::sync::async_lock_guard>> async_lock_via_session_executor(fixpp::sync::async_mutex&) noexcept;` — **declaration only; implemented by the session-module spec, not this feature** (RC#2; FR-011). Note (I1): `sync_lock_outside_session` (slot 45) is a declaration-contract error variant only — no runtime path is implemented in this feature, so it is **not coverage-applicable** per SC-009's declaration-only benignity clause (do not flag it uncovered).
- [ ] T015 [P] Scaffold `tools/check_no_std_mutex_in_awaitable_headers.sh` (owned/finalized by this feature per FR-014; not present on this branch): post-preprocessing (`-E`) scope, exit-non-zero on `std::mutex`/`std::recursive_mutex`/etc. in any header that pulls `asio::awaitable<...>`. Corpus/diagnostic/CI wiring completed in US5.

**Checkpoint**: every type named in data-model exists; all 29 seam files (added per story) compile and FAIL.

---

## Phase 3: User Story 1 — Acquire/release exclusive ownership in a coroutine (P1) 🎯 MVP

**Goal**: uncontended single-CAS fast path; contended suspend→FIFO-fair resume on the waiter's bound executor; RAII guard with destructive move (FR-002/003/004; SC-001).

**Independent Test**: two coroutines on one strand against a shared `async_mutex` via a test executor — uncontended acquirer gets the guard with no suspension; second acquirer suspends then resumes exactly on first guard destruction; mutual exclusion never overlaps; FIFO across a multi-waiter drain cycle.

### Tests for User Story 1 (write FIRST — must FAIL)

- [ ] T016 [P] [US1] Seam #1 `tests/sync/test_uncontended_latency.cpp` — uncontended fast-path, no suspension, valid guard (`[2f §9 #1]`).
- [ ] T017 [P] [US1] Seam #2 `tests/sync/test_contended_latency.cpp` — second acquirer suspends without busy-wait (`[2f §9 #2]`).
- [ ] T018 [P] [US1] Seam #3 `tests/sync/test_fifo_fairness.cpp` — FIFO fairness across a drain cycle (LIFO reversed on unlock) (`[2f §9 #3]`/`[2f §4.5.2]`).
- [ ] T019 [P] [US1] Seam #6 `tests/sync/test_contention_stress.cpp` — ≥10⁴ coroutines, zero overlap, zero starvation, zero lost waiter (`[2f §9 #6]`; SC-001).
- [ ] T020 [P] [US1] Seam #11 `tests/sync/test_executor_compat.cpp` — completion on the awaiter's bound executor (`[2d §7.4]`; `[2f §9 #11]`).
- [ ] T021 [P] [US1] Seam #12 `tests/sync/test_dispatch_vs_post.cpp` — `dispatch` (inline iff `running_in_this_thread()`) vs `post` per-mutex policy (`[2f §9 #12]`/`[2f §4.6]`).
- [ ] T022 [P] [US1] Seam #13 `tests/sync/test_cross_strand_acquire.cpp` — cross-strand resume via `post`, FIFO-fair drain (`[2f §9 #13]`/`[2f §6.1.3]`).
- [ ] T023 [P] [US1] Seam #20 `tests/sync/test_guard_destructive_move.cpp` — destructive move-assign releases prior contents; self-assign no-op; uses `async_mutex_awaiter` friend access for the engaged ctor (`[2f §9 #20]`).
- [ ] T024 [P] [US1] Seam #27 `tests/sync/test_unlock_reaper_splice.cpp` — unlock-vs-reaper splice race closure, RC-α/Opus C-R3-P1-3 (`[2f §9 #27]`).
- [ ] T025 [P] [US1] Seam #28 `tests/sync/test_result_write_race.cpp` — `*result_` CAS-then-publish: only the CAS winner writes (`[2f §9 #28]`/v1.4; I-06/I-09).

### Implementation for User Story 1

- [ ] T026 [US1] `async_mutex::async_lock(mr=nullptr)` awaitable factory in `async_mutex.hpp`: embedded-awaiter path (mr==nullptr) with `active_acquirers_count_.fetch_add` (I-20) BEFORE `await_ready`'s `draining_` load; `[[nodiscard]] noexcept`.
- [ ] T027 [US1] `async_mutex_awaiter::await_ready`: drained pre-check `draining_.load(acquire)` (I-15) → fast-fail path stub; fast-path `state_` CAS `not_locked→locked_no_waiters` (I-01); winner-only `active_holders_count_++` (I-17); `active_acquirers_count_--` decrement-points #1/#2 (I-21).
- [ ] T028 [US1] `async_mutex_awaiter::await_suspend`: defense-in-depth `draining_` reload; store `coro_`, init `phase_=queued`, recover cancellation_state, bind `slot_allocator{this,mr}`, register `on_cancel`; LIFO push CAS retry (I-02/I-03); mid-push `not_locked` transition → direct lock + inline resume; `active_acquirers_count_--` #3a/#3b/#3c (I-21).
- [ ] T029 [US1] `async_mutex_awaiter::await_resume`: `phase_.load(acquire)` (I-08) → `granted`⇒engaged `async_lock_guard{mutex_}` via friend ctor; `cancelled`⇒`*result_`; clear `slot_`.
- [ ] T030 [US1] `async_mutex::unlock()`: walk `next_drain_head_` first (RC-A; exchange I-11, re-publish I-12), then LIFO from `state_` (exchange I-04, empty close-out CAS I-05); first non-cancelled waiter `phase_` CAS `queued→granted` (I-06) winner-only `*result_` write + `active_holders_count_--` (I-18); splice remaining FIFO tail into `next_drain_head_` (I-10) **unless** `draining_` (I-16); resume under `policy_` (`dispatch` inline-iff-`running_in_this_thread()` else `post`).
- [ ] T031 [US1] Wire `async_lock_guard` runtime release (dtor/`release()`/destructive move all route to `mutex_->unlock()`); author `bench/sync/bench_async_mutex_uncontended.cpp` + `bench_async_mutex_contended.cpp` bodies (seam #1/#2 workloads).
- [ ] T032 [US1] Run US1 seams (T016–T025) GREEN under `linux-clang-debug`; verify mutual exclusion + FIFO; capture incremental lcov DA/BRDA on `async_mutex.hpp` and note any not-yet-covered lines for T072.

**Checkpoint**: US1 fully functional and independently testable (MVP).

---

## Phase 4: User Story 2 — Cancel a pending acquire deterministically (P1)

**Goal**: `cancellation_type::total` removes the waiter and completes with `sync_lock_aborted`; cancel-vs-drain CAS-arbitration yields exactly one of {granted, cancelled} per waiter, resumed exactly once, TSan-clean (FR-005; SC-002).

**Independent Test**: suspend a waiter, signal `cancellation_type::total`, assert `sync_lock_aborted` + waiter removed; then fire cancellation concurrently with the holder's release and assert exactly one of {granted, cancelled}, no double-resume, no lost waiter (TSan-clean).

### Tests for User Story 2 (write FIRST — must FAIL)

- [ ] T033 [P] [US2] Seam #4 `tests/sync/test_cancellation_mid_wait.cpp` — mid-wait cancel ⇒ `sync_lock_aborted`, removed, no ownership (`[2f §9 #4]`/`[2f §4.5]`).
- [ ] T034 [P] [US2] Seam #15 `tests/sync/test_race_cancel_pre_drain.cpp` — cancel-after-detach-pre-drain race (RC#1; `[2f §9 #15]`).
- [ ] T035 [P] [US2] Seam #16 `tests/sync/test_race_multi_cancel.cpp` — multi-cancel-same-list race (RC#1; `[2f §9 #16]`).
- [ ] T036 [P] [US2] Seam #17 `tests/sync/test_race_cancel_during_resume.cpp` — cancel-during-`await_resume` race (RC#1; `[2f §9 #17]`).
- [ ] T037 [P] [US2] Seam #22 `tests/sync/test_residual_cancel_graceful.cpp` — residual-chain cancellation under graceful close (RC-A; `[2f §9 #22]`).

### Implementation for User Story 2

- [ ] T038 [US2] `async_mutex_awaiter::on_cancel(cancellation_type)`: CAS `phase_ queued→cancelled` (I-07, acq_rel); winner writes `*result_=unexpected{sync_lock_aborted}` + schedules resumption on bound executor; CAS-loss ⇒ no-op (drain won).
- [ ] T039 [US2] Integrate the §4.5 cancel-vs-drain CAS-arbitration across `on_cancel` ↔ `unlock`/reaper grant: exactly one winner per waiter, resumed once, no lost/double waiter (RC#1; I-06/I-07 CAS-then-publish; SC-002).
- [ ] T040 [US2] Run US2 seams (T033–T037) GREEN; race seams clean under `linux-clang-tsan`; re-measure incremental coverage (cancel branches) for T072.

**Checkpoint**: US1 + US2 both pass independently; cancellation race TSan-clean.

---

## Phase 5: User Story 3 — Shut down safely: drain waiters, never silently corrupt (P2)

**Goal**: `cancel_and_drain()` completes every in-flight waiter exactly once and returns only after holder/acquirer/resumption counts reach zero; post-drain `async_lock()`⇒`sync_lock_drained` no-enqueue; destroy-with-waiters ⇒ `std::terminate()` (FR-006/007/008; SC-003).

**Independent Test**: queue N waiters, `cancel_and_drain()`, assert all completed once + post-drain `async_lock()`⇒`sync_lock_drained` + return only after counts zero; separate death test: destroy a mutex with a live waiter ⇒ `std::terminate()`.

### Tests for User Story 3 (write FIRST — must FAIL)

- [ ] T041 [P] [US3] Seam #5 `tests/sync/test_destructor_release_death.cpp` — destructor-with-waiters fires `std::terminate()` (release-linkage death test; `[2f §9 #5]`/`[2f §4.7]`).
- [ ] T042 [P] [US3] Seam #19 `tests/sync/test_cancel_and_drain.cpp` — reaps every in-flight waiter exactly once (RC#3; `[2f §9 #19]`).
- [ ] T043 [P] [US3] Seam #23 `tests/sync/test_cancel_and_drain_concurrent.cpp` — concurrent `cancel_and_drain` serialised into one epoch (RC-B; `[2f §9 #23]`).
- [ ] T044 [P] [US3] Seam #24 `tests/sync/test_drain_latch_holder_lifecycle.cpp` — lazy `drain_latch_state` + pre-drain holder lifecycle (RC-β; `[2f §9 #24]`).
- [ ] T045 [P] [US3] Seam #25 `tests/sync/test_in_flight_acquirer_coverage.cpp` — in-flight acquirer epoch window closed (RC-α/Opus C-R3-P1-2; `[2f §9 #25]`).
- [ ] T046 [P] [US3] Seam #26 `tests/sync/test_drain_awaitable_cancellation.cpp` — `cancel_and_drain` awaitable's own cancellation ⇒ `sync_lock_aborted` (RC-β; `[2f §9 #26]`).
- [ ] T047 [P] [US3] Seam #29 `tests/sync/test_drain_reaper_abort_subscribers.cpp` — reaper cancellation wakes all subscribers with abort outcome (v1.4; `[2f §9 #29]`).

### Implementation for User Story 3

- [ ] T048 [US3] Implement `detail::drain_latch_state` body: channel-based multi-waiter latch `wait()/notify()/signal_release()/signal_abort()`; `in_flight_resumptions_` inc/dec (I-28/I-29), last-handler publishes terminal edge; `released_`/`aborted_` stores+loads (I-25/I-26/I-27).
- [ ] T049 [US3] Implement `async_mutex::cancel_and_drain()` reaper: `drain_in_progress_.test_and_set` winner; `make_shared<drain_latch_state>`; store `drain_latch_ptr_` (release, I-23) BEFORE `draining_.store(true,release)` (I-13) v1.4 ordering; exchange `state_`+`next_drain_head_`; CAS each `queued`→`cancelled` winner-only; stable re-walk both lists until null; `co_await latch->wait()` until `active_holders_count_==0 && active_acquirers_count_==0 && in_flight_resumptions_==0` (I-19/I-22/I-30); return `{}` or `unexpected{sync_lock_aborted}` if itself cancelled; concurrent callers subscribe to the epoch latch (RC-B).
- [ ] T050 [US3] Finalize `~async_mutex()` hard-precondition `std::terminate()` path (held or waiters in `state_`/`next_drain_head_`; debug AND release) and wire seam #5 release-linkage death test (FR-008; no `expected_t` variant).
- [ ] T051 [US3] Drained fast-fail: `await_ready`/`await_suspend` `draining_` true ⇒ `*result_=unexpected{sync_lock_drained}`, `phase_=cancelled`, decrement acquirer count, **no enqueue** (FR-007; I-15/I-14).
- [ ] T052 [US3] Run US3 seams (T041–T047) GREEN (death test under `linux-clang-release`); re-measure incremental coverage (drain/teardown branches) for T072.

**Checkpoint**: US1+US2+US3 pass independently; teardown safe.

---

## Phase 6: User Story 4 — Zero global allocation on the hot path; PMR fallback (P2)

**Goal**: embedded HALO path performs zero global `new`/`delete`; explicit `async_lock(mr)` routes storage to the caller resource; PMR exhaustion ⇒ `sync_lock_alloc_failed` trapped (no terminate) (FR-009/010; SC-004).

**Independent Test**: contended acquire/release/cancel/drain under an alloc-counting harness ⇒ zero global new/delete on the embedded path; explicit-`mr` overload ⇒ all fallback allocs hit the supplied resource, none global; PMR exhaustion ⇒ `sync_lock_alloc_failed` (trapped).

### Tests for User Story 4 (write FIRST — must FAIL)

- [ ] T053 [P] [US4] Seam #7 `tests/sync/test_tsan_clean.cpp` — TSan-clean under stress (`[2f §9 #7]`/`[const §IX.2]`).
- [ ] T054 [P] [US4] Seam #8 `tests/sync/test_asan_clean.cpp` — ASan/UBSan-clean under stress (`[2f §9 #8]`).
- [ ] T055 [P] [US4] Seam #9 `tests/sync/test_halo_firing.cpp` — awaiter coroutine frame HALO-elided across the compiler matrix (`[2f §9 #9]`/`[2f §6.4]`).
- [ ] T056 [P] [US4] Seam #10 `tests/sync/test_pmr_fallback.cpp` — explicit-`mr` path all-from-resource; exhaustion ⇒ `sync_lock_alloc_failed`; mallocnesia zero-global-heap on embedded path (`[2f §9 #10]`).
- [ ] T057 [P] [US4] Seam #21 `tests/sync/test_slot_allocator_storage.cpp` — three storage cases: inline 32 B / null-resource overflow-trap / PMR (RC-C; `[2f §9 #21]`).

### Implementation for User Story 4

- [ ] T058 [US4] Implement `detail::slot_allocator` three-case body: case 1 embedded+HALO `slot_storage_` (overflow→`null_memory_resource`→`trap_throw`→`unexpected{sync_lock_alloc_failed}`); case 2 embedded+no-HALO; case 3 PMR `polymorphic_allocator<void>{mr}` (allocate failure→`trap_throw`).
- [ ] T059 [US4] `async_lock(std::pmr::memory_resource* mr)` PMR-fallback overload: allocate awaiter from `mr`, de-allocate back to `mr` after `await_resume`; `core` never reaches into `session/`/engine (RC#2; FR-010).
- [ ] T060 [US4] Ensure HALO-eligibility: `await_ready`/`await_suspend` inline-visible in the header; confirm awaiter `sizeof ≤ 96 B` static_assert holds; wire `tools/check_alloc.py` + `mallocnesia` alloc guard for seams #7/#8/#10 (`[const §VIII.5]`).
- [ ] T061 [US4] Run US4 seams (T053–T057) GREEN; zero global new/delete on embedded path verified under `mallocnesia` (`LD_PRELOAD` per quickstart §2); re-measure incremental coverage (alloc/PMR branches) for T072.

**Checkpoint**: US1–US4 pass independently; zero-hot-path-alloc verified.

---

## Phase 7: User Story 5 — CI rejects `std::mutex` in coroutine context (P3)

**Goal**: the `[const §XV.9]` grep gate fails the build when an `asio::awaitable<...>`-including header names `std::mutex`/etc., diagnostic naming `fixpp::sync::async_mutex`; zero FN/FP on the labelled corpus (FR-014; SC-006).

**Independent Test**: run the gate against the labelled corpus — legitimate-`async_mutex` fixtures pass; `std::mutex`-in-awaitable-header fixtures fail with a message naming `async_mutex`; zero false negatives and zero false positives.

### Tests for User Story 5 (write FIRST — must FAIL)

- [ ] T062 [US5] Seam #14 `tests/sync/test_no_std_mutex_ci_gate.cpp` — drives the gate over the 3 fixtures and asserts zero FN/FP + diagnostic content (`[2f §9 #14]`).
- [ ] T063 [P] [US5] Fixture `tests/sync/fixtures/header_with_std_mutex_and_awaitable.hpp` — deliberate violation; gate MUST fire.
- [ ] T064 [P] [US5] Fixture `tests/sync/fixtures/header_without_violation.hpp` — legitimate `async_mutex` in an awaitable header; gate MUST NOT fire.
- [ ] T065 [P] [US5] Fixture `tests/sync/fixtures/header_transitive_awaitable_include.hpp` — `std::mutex` with `asio::awaitable` pulled transitively; post-preprocessing scope MUST catch it (`[2f §9 #14]` variant 3).

### Implementation for User Story 5

- [ ] T066 [US5] Finalize `tools/check_no_std_mutex_in_awaitable_headers.sh`: post-`-E` preprocessing scope (Codex C-P2-10), diagnostic names `fixpp::sync::async_mutex`; wire as a first-class Tier-1 CI step (`[const §IX.4]`).
- [ ] T067 [US5] Run seam #14 + the script over the corpus GREEN: zero false negatives, zero false positives (SC-006).

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
