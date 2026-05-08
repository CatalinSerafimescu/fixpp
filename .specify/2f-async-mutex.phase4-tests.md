# 2f `async_mutex` — Phase 4 Test Requirements (hand-off)

> **Status:** Phase 4 input artifact (not a Spec Kit `/tasks` output).
> **Author:** Opus (orchestrator note at 2f sign-off, 2026-05-08).
> **Consumed by:** the future Phase 4 `/specify` + `/clarify` + `/plan` + `/tasks` agents working on `core/sync/async_mutex`. This file is binding context — Phase 4 MUST satisfy every requirement below before `core/sync/async_mutex` exits Phase 4.
> **Why this file exists:** the design doc `[2f-async-mutex.md] §9` lists 30 test seams (attachment points). This file expands each seam into pass/fail criteria, names the missing tooling (stateless model checker, fuzz harness, HALO verification protocol), specifies CI-matrix amendments, and lists consumer-integration tests that span 2e + 2g + Phase-4-session-module-spec. The seams alone are necessary but **not sufficient** for production confidence in a lock-free primitive; this file closes the gap.
> **Inherits:** `[2f-async-mutex.md]` v1.5 (entire doc — design contract); `[opus_plan.md] Phase 4` (per-feature TDD pipeline); `[opus_plan.md] Quality gate` (Tier 1 + Tier 2 CI tiers); `[const §VII]` (testing requirements); `[const §IX]` (coverage, sanitizers, static analysis); `[const §VIII]` (performance budgets); `[const §XI]` (concurrency + coroutines).

---

## 1. Categories at a glance

| Category | Why critical for `async_mutex` | Phase 4 deliverable |
|---|---|---|
| 1. Lock-free correctness | Lock-free LIFO + atomic exchange + per-waiter phase CAS + lazy shared_ptr — TSan finds races that occur, not all races. | Stateless model checker; fuzz; property-based tests; ARM64 weak-memory test. |
| 2. Cancellation correctness | 4 named race windows (cancel-after-detach-pre-drain, multi-cancel-same-list, cancel-during-await_resume, cancel-during-grant) + reaper cancellation + abort-signal subscriber wakeup. | Race-window enumeration tests; chaos test; cancellation fuzz. |
| 3. Lifetime / leak verification | shared_ptr drain_latch_state with multi-waiter captures; awaiter embedded in coroutine frame; cancellation slot allocator. | ASan + LSan + valgrind soak; long-running stress under TSan. |
| 4. Performance verification | §6.3 Tier 1 ceilings (≤ 20–25 ns uncontended; ≤ 80 ns contended-enqueue; ≤ 15 ns uncontended unlock; ≤ 30 ns + ≤ 50 ns/waiter contended unlock); HALO-eligibility across compilers. | Per-compiler/per-config latency benchmark with ±5% regression gate; HALO-firing post-build inspection. |
| 5. Cross-platform / cross-compiler | Linux Clang (primary), Linux GCC, Windows MSVC, ARM64 Linux. | CI matrix amendments + per-compiler test coverage. |
| 6. Death-test reliability | `std::terminate()` precondition on destructor with waiters; release + debug; cross-platform. | Platform-specific death-test machinery; reliability harness. |
| 7. Consumer-integration tests | MessageStore writer mutex (`[2e §6.4]`); pinset rotation (`[2g]`); seqnum counter (Phase-4 session-module spec). | Integration tests that exercise the mutex inside each consumer's loop. |
| 8. CI matrix amendments | ARM64 + per-compiler + sanitizer combinations + soak job + model-checker job. | Tier 1 / Tier 2 / nightly definitions per `[opus_plan.md] Quality gate`. |
| 9. Coverage requirements | `[const §IX.1]` ≥ 90% line / ≥ 80% branch on touched modules. | Coverage report per the constitution. |
| 10. Spec-driven seam tests | §9's 30 seams need concrete pass/fail criteria + named test files. | Test file per seam, mapped via Spec Kit `/tasks`. |

---

## 2. Category 1 — Lock-free correctness

### 2.1 Stateless model checker (NEW seam — was missing from §9)

**Required tooling:** Loom (Rust) is not directly applicable to C++; the C++ equivalents are:
- **Relacy Race Detector** (https://github.com/dvyukov/relacy) — header-only, MIT-licensed; replays the algorithm under a stateless model that enumerates interleavings of atomic operations under the C++11 memory model.
- **CDSChecker** (Norris/Demsky, https://github.com/computersforpeace/model-checker) — alternative; UCI Plrg.
- **GenMC** (https://github.com/MPI-SWS/genmc) — formal verification with proven soundness on a subset of the C++ memory model; LLVM-based; harder to integrate.

**Phase 4 requirement:**
- Pick **Relacy** as the v1.0 model checker (header-only, lowest integration cost).
- Add a `tests/model/async_mutex_relacy.cpp` harness that exercises the algorithm under Relacy. Coverage:
  - 2 acquirers + 1 unlocker, with cancellation on each acquirer (24 interleavings).
  - 4 acquirers + 2 unlockers + 1 `cancel_and_drain` (≥ 1000 interleavings).
  - The 4 RC-A race windows from `[2f §4.5]` named explicitly:
    - cancel-after-detach-pre-drain (cancellation lands AFTER `unlock()` exchanges LIFO out, BEFORE drain CAS'd this waiter's phase to `granted`).
    - multi-cancel-same-list (two waiters both cancel before the same `unlock()` drain — both must observe `cancelled` phase).
    - cancel-during-await_resume (cancellation arrives after `await_resume` has executed; slot is cleared; cancellation is no-op).
    - cancel-during-grant (cancellation lands while drain is mid-CAS to `granted` — drain wins; cancellation is no-op on this waiter; mutex is owned by this waiter).
  - The 2 RC-B race windows from `[2f §4.7.2]`:
    - cancel_and_drain wakes existing holder via `drain_latch_state::notify()` not `signal_release()` (holder still owns mutex).
    - cancel_and_drain ordering: `drain_latch_ptr_.store(state, release)` BEFORE `draining_.store(true, release)`.
- **Pass criterion:** Relacy reports zero data races, zero ordering violations, zero use-after-free across all enumerated interleavings. CI fails the build if Relacy reports any violation.
- **Failure mode tolerance:** Relacy's enumeration is not exhaustive for state spaces ≥ 7 actors; for >6-actor scenarios, switch to fuzz (§2.3) + soak (§4.3).

### 2.2 Property-based testing

**Phase 4 requirement:**
- Add `tests/property/async_mutex_property.cpp` using **rapidcheck** (header-only; MIT-licensed; widely used).
- Properties to test:
  1. **Mutual exclusion under any interleaving:** for any sequence of N lock/unlock/cancel ops on a single mutex, at most one coroutine ever holds the lock at a time. Encoded as: `assert(active_holders_count_ <= 1)` at every observable timestep.
  2. **FIFO fairness across drain cycles:** for any sequence of N+1 acquires followed by N+1 unlocks, the (N+1)-th acquirer's grant order matches the order the acquires were enqueued.
  3. **Cancellation transitivity:** if a cancellation is observed by an awaiter at any point, the awaiter's `await_resume()` returns `unexpected{sync_lock_aborted}`. Inverse: if `await_resume()` returns success, no cancellation was observed.
  4. **`cancel_and_drain` quiesce contract:** after `cancel_and_drain` completes, `state_ == not_locked`, `next_drain_head_ == nullptr`, `active_holders_count_ == 0`, `active_acquirers_count_ == 0`, `in_flight_resumptions_ == 0`. Asserted at the awaitable's resumption.
  5. **`async_lock` after drain returns `sync_lock_drained`:** any `async_lock` initiated after `cancel_and_drain` set `draining_ = true` returns `unexpected{sync_lock_drained}`.
  6. **Move-assign destructive semantics:** moving an `async_lock_guard` into an engaged guard unlocks the previously-owned mutex; the previously-owned mutex's `state_` becomes acquirable by another caller.
- **Pass criterion:** rapidcheck runs ≥ 10,000 random sequences per property without violation; Tier 1 CI runs 1,000 per property; nightly runs 100,000.
- **Configuration:** rapidcheck shrinking ON; failure outputs a minimal counterexample to `tests/output/async_mutex_property_failures/`.

### 2.3 Cancellation+lock+unlock fuzz harness

**Phase 4 requirement:**
- Add `tests/fuzz/async_mutex_fuzz.cpp` using libFuzzer (`[opus_plan.md] Tooling: Fuzzing`).
- Input: a byte stream interpreted as a sequence of operations: `0x00 = lock`, `0x01 = unlock`, `0x02 = cancel`, `0x03 = cancel_and_drain`, `0x04 = move-assign-guard`, `0x05 = destroy-mutex`.
- Each fuzz iteration spawns N coroutines (N ∈ [2, 16]) on a randomized strand pool (1–4 strands), executes the operation sequence, and asserts the invariants from §2.2.
- **Pass criterion:** ≥ 10 minutes of fuzzing per Tier 1 CI run with zero crashes / zero invariant violations / zero ASan or TSan reports; nightly fuzzing ≥ 4 hours.
- **Corpus:** seed corpus committed under `tests/fuzz/corpus/async_mutex/`; CI uploads new interesting inputs to the corpus.

### 2.4 ARM64 weak-memory test

**Phase 4 requirement:**
- §9 seam #18 (ARM64 weak-memory contention stress) must run on actual ARM64 hardware, not emulation. AArch64's relaxed memory model is observably different from x86's TSO; bugs that hide under TSO surface under AArch64.
- CI must include an ARM64 Linux runner (GitHub Actions `ubuntu-24.04-arm` or self-hosted). Per `[opus_plan.md] Quality gate`, this lands as Tier 2 nightly (label-triggered for `2f`-touching PRs).
- Specific test: 1024 coroutines on 4 strands, each performing 10K lock/unlock cycles with random cancellation; assert mutual exclusion, FIFO fairness, no UAF. Pass = 100 consecutive runs clean under TSan.

---

## 3. Category 2 — Cancellation correctness

### 3.1 Race-window enumeration tests

Each of the 4 RC-A windows + 2 RC-B windows from §2.1 above gets its own dedicated reproducer test in `tests/race/async_mutex_race_<N>.cpp`. Each test:
- Uses `std::atomic` fences, `std::this_thread::yield()`, and explicit phase checkpoints to force the target interleaving deterministically (or with high probability under stress).
- Runs ≥ 10K iterations under TSan + ASan.
- **Pass criterion:** zero violations of the named invariants.
- **Failure handling:** if any race window cannot be deterministically reproduced after a 10K iterations stress, mark the test `[[gtest::DISABLED_]]` with a TODO comment naming the structural cause; escalate to Loom (§2.1) or a Phase-4 design amendment.

### 3.2 Chaos test (long-running cancellation storm)

**Phase 4 requirement:**
- Add `tests/chaos/async_mutex_cancellation_storm.cpp`.
- 64 coroutines × 4 strands × 1M operations. Cancellation rate: 30%. `cancel_and_drain` rate: 1 per 100K operations.
- Runs ≥ 30 minutes per nightly CI; assertions enabled (debug build); TSan + ASan + UBSan.
- **Pass criterion:** zero invariant violations, zero leaks, zero data races, zero deadlocks, zero infinite waits.
- Reports: P50 / P99 / P99.9 latency for `async_lock`, `unlock`, `cancel`, `cancel_and_drain`. Tracked over time as a regression baseline.

### 3.3 Cancellation propagation tests

Per `[2f §4.7.3]` invariant I-5: cancelled reaper calls `signal_abort()`; subscribers wake with `sync_lock_aborted`.

**Phase 4 requirement:**
- `tests/cancellation/reaper_abort_wakes_subscribers.cpp` — spawn 1 reaper + M ∈ {1, 2, 4, 8, 16, 64} subscribers parked on `state->wait()`. Cancel reaper's parent state via `cancellation_type::total`. Assert: every subscriber wakes within 100 ms; every subscriber returns `unexpected{sync_lock_aborted}`; no subscriber suspends forever (test timeout 5 s; failure on timeout).
- `tests/cancellation/await_drain_cancelled_mid_wait.cpp` — call `cancel_and_drain` from a coroutine; cancel that coroutine's parent state mid-wait. Assert: awaitable returns `unexpected{sync_lock_aborted}`; mutex's `draining_` stays `true`; `drain_latch_state` survives via in-flight resumption handlers' shared_ptr captures; subsequent `async_lock` returns `sync_lock_drained`.

---

## 4. Category 3 — Lifetime / leak verification

### 4.1 ASan + LSan + UBSan baseline

**Phase 4 requirement:** every test in `tests/` runs under ASan + LSan + UBSan in Tier 1 CI. Per `[const §IX.4]`. No exceptions for `async_mutex` tests.

### 4.2 TSan baseline

**Phase 4 requirement:** every test that exercises `async_mutex` runs under TSan in Tier 1 CI. Per `[const §IX.4]`.

### 4.3 Long-running soak (≥ 24 hours)

**Phase 4 requirement:**
- Nightly CI (Tier 2 nightly per `[opus_plan.md]`) runs `tests/soak/async_mutex_24h.cpp` for 24 hours: 16 coroutines × 2 strands × infinite loop of lock/unlock/cancel.
- Assertions enabled. ASan + LSan enabled. TSan enabled (separate run; TSan + ASan don't combine cleanly).
- Memory-growth check: RSS growth ≤ 1 MB over the 24-hour run (no leaks of `drain_latch_state` shared_ptrs, no unbounded growth of the LIFO chain).
- **Pass criterion:** zero crashes, zero leaks, zero memory growth, zero invariant violations, zero P99.9 latency outliers > 10× the §6.3 ceiling.

### 4.4 Valgrind backup (Linux only; nightly)

**Phase 4 requirement:**
- Nightly: `valgrind --tool=helgrind` and `valgrind --tool=memcheck` runs of the chaos test (§3.2) for ≥ 1 hour each.
- Catches what TSan misses (different race-detection algorithm; different false-positive profile).

---

## 5. Category 4 — Performance verification

### 5.1 Latency benchmarks per §6.3 ceilings

Per `[2f §6.3]`:

| Operation | Ceiling | Bench file |
|---|---|---|
| `async_lock` uncontended | ≤ 20–25 ns | `bench/async_mutex/lock_uncontended.cpp` |
| `async_lock` contended-enqueue | ≤ 80 ns | `bench/async_mutex/lock_contended.cpp` |
| `unlock` uncontended | ≤ 15 ns | `bench/async_mutex/unlock_uncontended.cpp` |
| `unlock` contended (drain N) | ≤ 30 ns + ≤ 50 ns/waiter | `bench/async_mutex/unlock_contended.cpp` |
| `cancel_and_drain` quiesce | ≤ 1 µs (no in-flight; no holders) | `bench/async_mutex/drain_no_load.cpp` |
| `cancel_and_drain` quiesce | ≤ 100 µs (with N=64 in-flight) | `bench/async_mutex/drain_with_load.cpp` |

**Phase 4 requirement:**
- Each bench uses Google Benchmark. Per-compiler / per-config baseline stored under `bench/baselines/<compiler>-<config>/async_mutex/`. The `[opus_plan.md] Quality gate` ±5% regression budget applies.
- Tier 1 CI runs benches; if regression > 5% AND the PR doesn't update the baseline with rationale, CI fails.
- Nightly: longer benches with statistical analysis (Google Benchmark `--benchmark_repetitions=100`).

### 5.2 HALO-firing verification

**Phase 4 requirement:**
- Add `tests/halo/async_mutex_halo_check.sh`. For each (compiler, optimisation level) ∈ {Clang Release, GCC Release, MSVC Release}:
  1. Compile a minimal coroutine that calls `co_await mutex.async_lock()`.
  2. Run `objdump -d` (Linux) or `dumpbin /DISASM` (Windows) on the resulting binary.
  3. Grep for any `call _Znwm` / `call malloc` / `call operator new` in the coroutine frame allocation path.
  4. **Pass criterion:** no heap allocation calls in the awaiter path. The awaiter must live in the caller's coroutine frame.
- Failure mode: PMR fallback path is the safety net (per `[2f §6.4]`); document in the test comment that HALO failure is non-fatal but degrades performance.
- The HALO check is a Tier 2 nightly job (compiler-version-sensitive; not stable enough for Tier 1 per-PR).

### 5.3 Cache-line layout verification

**Phase 4 requirement:**
- Add `tests/layout/async_mutex_layout.cpp`. Static assertions:
  - `sizeof(async_mutex) <= 64` (one cache line on x86_64).
  - `alignof(async_mutex) >= 8`.
  - `offsetof(async_mutex, state_) == 0` (hot member at line head).
  - `offsetof(async_mutex, drain_latch_ptr_) >= 32` (cold member; below the hot RC-α atomic counters).
- Run on every (compiler, target) combination; if MSVC packing differs from libstdc++/libc++, document the platform-specific size in a follow-up.

### 5.4 False-sharing measurement

**Phase 4 requirement:**
- Bench: 8 mutexes, each touched by a dedicated thread. Compare throughput against 8 mutexes each separated by `alignas(128)` padding.
- If throughput differs by > 10%, false sharing is observable; consider `alignas(64)` on the mutex object (per `[2f §1.2]`'s explicit no-alignas decision; revisit only if measured).

---

## 6. Category 5 — Cross-platform / cross-compiler

### 6.1 Compiler matrix

Per `[const §II]` + `[opus_plan.md] Tooling`:

| Compiler | Platform | Tier | Coverage |
|---|---|---|---|
| Clang | Linux x86_64 | Tier 1 per-PR | All tests + sanitizers + bench + coverage |
| GCC | Linux x86_64 | Tier 1 per-PR | All tests + ASan/TSan |
| MSVC | Windows x86_64 | Tier 2 nightly + label-triggered | All tests + ASan only |
| Clang | Linux ARM64 | Tier 2 nightly + label-triggered for `2f`-touching PRs | All tests + TSan |
| GCC | Linux ARM64 | Tier 2 nightly | Build sanity + tests |

**Phase 4 requirement:** the ARM64 entries are NEW vs. the current `[opus_plan.md] Quality gate`; they must be added to the Phase 3 CI matrix before Phase 4 `core/sync/async_mutex` `/implement` lands.

### 6.2 Coroutine ABI quirks

**Phase 4 requirement:**
- MSVC's coroutine ABI differs from libstdc++/libc++ on awaiter destruction order; the test `tests/coroutine_abi/async_mutex_msvc_destruction.cpp` exercises the awaiter destruction path under MSVC + asserts no UAF.
- libstdc++ vs libc++: same source, different `coroutine_handle` vtable layout. `tests/coroutine_abi/async_mutex_libstdcpp_libcxx.cpp` runs on both stdlibs with identical assertions.

### 6.3 ASIO version compatibility

**Phase 4 requirement:**
- The mutex depends on `asio::cancellation_slot`, `asio::cancellation_type`, `asio::any_io_executor`, and `asio::experimental::concurrent_channel`. Pin minimum ASIO version in the Conan recipe; test against (pinned + 1) and (pinned + 2) to catch upstream API drift.

---

## 7. Category 6 — Death-test reliability

### 7.1 Destructor-with-waiters precondition

Per `[2f §4.7]`: `~async_mutex()` fires `std::terminate()` if waiters present, in BOTH debug and release.

**Phase 4 requirement:**
- `tests/death/async_mutex_destruction_with_waiters.cpp`.
- Use Google Test's `EXPECT_DEATH_IF_SUPPORTED` or `EXPECT_EXIT`. Fork-mode for stability on Linux; `--gtest_death_test_style=threadsafe` for cross-platform.
- Test cases:
  1. Debug build: destructor with 1 waiter fires `std::terminate`.
  2. Debug build: destructor with N=10 waiters fires `std::terminate`.
  3. Release build: same — both must fire (Phase 2 design says debug + release).
  4. After `cancel_and_drain` completes, destructor does NOT fire (clean shutdown path).
- **Pass criterion:** all 4 tests pass on (Clang/Linux, GCC/Linux, MSVC/Windows). Death tests are notoriously flaky on Windows; if MSVC's death-test machinery doesn't reliably catch `std::terminate`, document the platform-specific harness in a test comment.

### 7.2 Sanitizer interaction with death tests

**Phase 4 requirement:**
- Death tests under TSan can spuriously fail (TSan injects threads that interfere with fork). Test runs that exercise death tests run WITHOUT TSan (separate config).
- Death tests under ASan: ASan's `abort_on_error` interacts with `std::terminate`; configure `ASAN_OPTIONS=abort_on_error=1` for death-test runs.

---

## 8. Category 7 — Consumer-integration tests

### 8.1 MessageStore writer-mutex contract (`[2e §6.4]`)

**Phase 4 requirement:**
- Integration test: `tests/integration/message_store_with_async_mutex.cpp`.
- Spawn a `MemoryStore` instance; call all 4 mutating methods (`store`, `retrieve`, `next_seqnum`, `reset`) concurrently from N=64 coroutines on 4 strands.
- Assert: all 4 methods serialise correctly on the per-instance writer mutex; no two concurrent invocations of the same method observe inconsistent state; no UAF; no deadlock.
- Run under TSan + ASan; ≥ 1 minute per Tier 1 CI run.

### 8.2 Pinset rotation (`[2g]`)

**Phase 4 requirement:**
- Integration test: `tests/integration/pinset_rotation_with_async_mutex.cpp`.
- (Spec depends on `[2g]` — populate this section after `[2g]` Gate A converges.)

### 8.3 Seqnum counter (Phase-4 session-module spec)

**Phase 4 requirement:**
- Integration test: `tests/integration/seqnum_counter_with_async_mutex.cpp`.
- (Spec depends on Phase-4 session-module spec — populate this section after the session-module spec converges.)

---

## 9. Category 8 — CI matrix amendments

The current `[opus_plan.md] Quality gate` defines Tier 1 (per-PR) and Tier 2 (nightly + on-demand). The following amendments MUST land in Phase 3 (`Skeleton + Tooling + CI`) BEFORE Phase 4 `core/sync/async_mutex` `/implement` runs:

| Amendment | Tier | Trigger | Job |
|---|---|---|---|
| ARM64 Linux/Clang | Tier 2 nightly + label-triggered | PR label `2f` OR nightly | All `2f` tests + TSan + chaos test |
| Relacy model checker | Tier 1 per-PR | Always | `tests/model/async_mutex_relacy.cpp` (≤ 5 minutes; if Relacy can't enumerate within 5 min, scope down the harness) |
| Property-based tests | Tier 1 per-PR | Always | `tests/property/async_mutex_property.cpp` (1000 iterations per property, ≤ 2 minutes) |
| Property-based tests (extended) | Tier 2 nightly | Nightly | Same harness with 100,000 iterations per property |
| Fuzz harness | Tier 1 per-PR | Always | libFuzzer 10 minutes |
| Fuzz harness (extended) | Tier 2 nightly | Nightly | libFuzzer ≥ 4 hours |
| HALO post-build inspection | Tier 2 nightly | Nightly | `tests/halo/async_mutex_halo_check.sh` |
| Death tests | Tier 1 per-PR | Always | `tests/death/async_mutex_destruction_with_waiters.cpp`; MSVC death tests quarantined in Tier 2 if flaky |
| 24-hour soak | Tier 2 nightly weekly | Saturday night | `tests/soak/async_mutex_24h.cpp` |
| Valgrind backup | Tier 2 nightly | Nightly | helgrind + memcheck on chaos test |

**Phase 4 requirement:** Phase 3 deliverable; track as Phase 3 item (not a Phase 4 task) — the CI infrastructure must be in place before Phase 4 lands the implementation.

---

## 10. Category 9 — Coverage requirements

Per `[const §IX.1]`: ≥ 90% line coverage AND ≥ 80% branch coverage on touched modules. `core/sync/async_mutex` is one module.

**Phase 4 requirement:**
- llvm-cov + llvm-profdata on Linux/Clang; coverage report in `coverage/async_mutex/`.
- 100% coverage of every public method.
- 100% coverage of every CAS-arbitration code path (the LIFO push success/failure, the phase CAS success/failure for unlocker/cancel-handler/reaper, the `cancel_and_drain` `drain_in_progress_` test_and_set success/failure).
- 100% coverage of every error-return path (the 4 error variants must each be hit by at least one test).
- ≥ 95% branch coverage on `async_lock`, `unlock`, `cancel_and_drain` (these are the load-bearing methods; the higher bar reflects the lock-free risk).

---

## 11. Category 10 — Spec-driven seam tests (per `[2f §9]`)

Each of the 30 seams in `[2f §9]` becomes a Phase 4 test file under `tests/seams/`. The Spec Kit `/tasks` agent generates the per-seam test stub from the seam description; `/implement` fills in the test body.

**Pass/fail criteria expansion** (the `/clarify` agent must confirm each before `/tasks`):

| Seam # | Pass/fail criterion |
|---|---|
| 1 | `bench/async_mutex/lock_uncontended.cpp` reports median ≤ 25 ns; ±5% regression budget per `[opus_plan.md]`. |
| 2 | `bench/async_mutex/lock_contended.cpp` reports median ≤ 80 ns under N=4 enqueues. |
| 3 | FIFO fairness: 100 acquires + 100 unlocks; assert grant order matches enqueue order. ±0 tolerance. |
| 4 | Cancellation mid-wait: assert `await_resume()` returns `unexpected{sync_lock_aborted}`; mutex's `state_` does not retain the cancelled waiter. |
| 5 | Death test per §7.1; pass = `std::terminate` fires on debug + release. |
| 6 | Contention stress: 10K coroutines on shared mutex; assert mutual exclusion (no two coroutines hold simultaneously); ≥ 30 seconds runtime. |
| 7 | TSan clean: chaos test (§3.2) under TSan reports zero races. |
| 8 | ASan clean: chaos test under ASan + LSan reports zero leaks, zero UAF. |
| 9 | HALO firing: §5.2 protocol passes for all 3 compilers. |
| 10 | PMR fallback: `tests/pmr/async_mutex_any_completion_handler.cpp` uses `asio::any_completion_handler`; assert zero global-heap allocations (use a custom `std::pmr::memory_resource` that asserts on default-resource use). |
| 11 | Executor-compat: `tests/executor/async_mutex_completion_on_bound_executor.cpp` binds the awaiter to a strand; asserts completion runs on that strand. |
| 12 | `dispatch` vs `post`: two test files exercising each policy; assert inline-vs-post behaviour via thread-id capture. |
| 13 | Cross-strand acquire: 4 strands × 4 coroutines each; assert FIFO drain across strand boundaries. |
| 14 | CI grep gate: a CI step that runs `tools/check_no_std_mutex_in_awaitable_headers.sh`; fails build if a `std::mutex` slips into a header that includes `asio/awaitable.hpp`. |
| 15 | Race window per §3.1 — cancel-after-detach-pre-drain. |
| 16 | Race window per §3.1 — multi-cancel-same-list. |
| 17 | Race window per §3.1 — cancel-during-await_resume. |
| 18 | ARM64 weak memory per §2.4. |
| 19 | `cancel_and_drain` reaper: §3.3 reaper-abort-wakes-subscribers test. |
| 20 | Move-assign destructive: 2 mutexes A and B, both engaged; move-assign B's guard into A's guard; assert B is unlocked first; assert A is unlocked when target guard is destroyed. |
| 21 | Slot-allocator storage: 3 sub-tests covering the 3 cases from `[2f §4.3]`. |
| 22 | Residual-chain cancellation under graceful close: `cancel_and_drain` with N=10 residual waiters; all 10 must observe `cancelled` phase. |
| 23 | Concurrent `cancel_and_drain`: 4 coroutines call simultaneously; only 1 becomes the reaper (`drain_in_progress_.test_and_set` serialiser); the other 3 subscribe to the same epoch. |
| 24 | `cancel_and_drain` blocks pre-drain holder + drain-latch event signalling: assert `cancel_and_drain` does NOT complete until the holder's guard is destroyed; assert the holder's `unlock()` calls `signal_release()` on the latch state. |
| 25 | In-flight acquirer coverage: spawn an acquirer mid-`await_ready`; call `cancel_and_drain`; assert the in-flight acquirer is counted (returns `sync_lock_drained` instead of acquiring). |
| 26 | Awaitable cancellation propagation: §3.3 await-drain-cancelled-mid-wait test. |
| 27 | Unlock-vs-reaper splice closure: concurrent `unlock()` + `cancel_and_drain`; assert no waiter is granted ownership during `draining_ == true`; assert no splice into `next_drain_head_` during `draining_ == true`. |
| 28 | `*result_` CAS-then-publish under TSan: the seam in §3.2 from the design — assert no two writers race the `*result_` slot under N=64 simultaneous unlock-grant + cancellation-handler interleavings on the same awaiter. |
| 29 | Reaper cancellation wakes subscribers: §3.3 test. |
| 30 | `notify()` non-terminal wake: assert `notify()` wakes parked subscribers; subscribers re-evaluate counter conditions; reaper re-checks counters and either parks again or proceeds to terminal publication. |

**Plus the 3 NEW seams added in this hand-off:**

| Seam # | Description | Pass/fail criterion |
|---|---|---|
| 31 | Stateless model-checker harness (Relacy) | §2.1 — zero races / zero ordering violations / zero UAF across all enumerated interleavings |
| 32 | Property-based tests (rapidcheck) | §2.2 — 6 properties; ≥ 10K iterations per property; zero violations |
| 33 | Cancellation+lock+unlock fuzz harness (libFuzzer) | §2.3 — ≥ 10 minutes Tier 1; zero crashes / zero invariant violations |

---

## 12. Out-of-scope for v1.0 (deferred to v1.x)

Per `[2f §2]` non-goals, the following are NOT tested in v1.0:
- `async_recursive_mutex`, `async_shared_mutex`, `async_timed_mutex` — no implementation, no tests.
- Unbounded-waiter-list shape — `[2f §1.1]`'s post-v1.0 risk note: for audit-tee, gRPC fan-out, the unlock-drain O(N) cost is a defence-in-depth concern; v1.0 callsites are bounded by single-session-serialisation discipline.
- Loom (Rust) port — Relacy is the v1.0 model checker; if a Rust port of `fixpp` happens, Loom may apply.

---

## 13. Phase 4 deliverable summary

Before `core/sync/async_mutex` exits Phase 4 (i.e., before its catalogue row NFR-016 flips to `done`):

- ☐ All 33 test seams have implementing test files under `tests/`.
- ☐ Tier 1 CI runs: Relacy + property + fuzz (10 min) + death tests + bench + sanitizers + coverage.
- ☐ Tier 2 nightly CI runs: ARM64 + extended fuzz (4h) + 24h soak (weekly) + valgrind + HALO inspection.
- ☐ `[opus_plan.md] Quality gate` ≥ 90% line / ≥ 80% branch coverage on `core/sync/async_mutex` (≥ 95% branch on `async_lock` / `unlock` / `cancel_and_drain` per §10).
- ☐ Bench baseline established under `bench/baselines/` per (compiler, config); ±5% regression budget enforced per `[opus_plan.md]`.
- ☐ Integration tests (§8) for MessageStore writer mutex; pinset and seqnum populated after `[2g]` and Phase-4 session-module spec converge.
- ☐ All findings from Codex Gate B PR review resolved or waived.

If any of the above is incomplete, NFR-016 stays `implementing`; Phase 4 module exit criteria per `[opus_plan.md] Module exit criteria` are not met.

---

## 14. Hand-off

This file is the binding input for the Phase 4 `/specify` + `/clarify` + `/plan` + `/tasks` agents working on `core/sync/async_mutex`. The `/clarify` agent should:
1. Read this file end-to-end.
2. Surface any ambiguities in the per-seam pass/fail criteria (§11) — especially anything that depends on `[2g]` or the Phase-4 session-module spec (§8.2 / §8.3).
3. Confirm the CI matrix amendments (§9) are tracked as Phase 3 items (not Phase 4).
4. Confirm the model-checker / fuzz / property-based test infrastructure (§2) is in place before `/tasks` runs.

When this file's Phase 4 deliverables (§13) are complete and signed off, this file may be archived.
