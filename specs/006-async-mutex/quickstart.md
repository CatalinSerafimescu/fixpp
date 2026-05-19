# Quickstart — 006-async-mutex

How to build, test, run sanitizers (especially TSan for concurrency correctness), bench, measure coverage, verify, and gate the awaitable mutex. Anchored to `.specify/2f-async-mutex.md` **v1.5 + v1.6 errata E-1..E-4** (E-1/E-2/E-3/E-4 recorded post-sign-off at `/implement`, re-touching 006 Gate A scope) and the constitution Tier-1 matrix (`[const §IX.6]`).

## 0. STL-availability probe — `std::atomic<std::shared_ptr<T>>` (run BEFORE `/implement`)

The mutex member `std::atomic<std::shared_ptr<detail::drain_latch_state>>`
(`[2f §1.2]`/`[2f §4.1]`; data-model.md E1) needs the C++20 `std::atomic<
std::shared_ptr<T>>` partial specialization (P0718). This is a documented
implementation assumption on the supported libc++/libstdc++/MSVC-STL matrix
(research.md D-4). Fail early if a supported STL lacks the surface:

```sh
# Minimal compile probe — must compile + run cleanly on EVERY supported STL.
cat > /tmp/atomic_shared_ptr_probe.cpp <<'EOF'
#include <atomic>
#include <memory>
int main() {
    std::atomic<std::shared_ptr<int>> a{std::make_shared<int>(1)};
    auto p = a.load();
    a.store(std::make_shared<int>(2));
    (void)p;
    return 0;
}
EOF
# Repeat per Tier-1/Tier-2 toolchain (libc++, libstdc++, MSVC-STL):
clang++ -std=c++23 -stdlib=libc++ /tmp/atomic_shared_ptr_probe.cpp -o /tmp/probe && /tmp/probe
g++     -std=c++23              /tmp/atomic_shared_ptr_probe.cpp -o /tmp/probe && /tmp/probe
# (Windows/MSVC: cl /std:c++latest /EHsc atomic_shared_ptr_probe.cpp)
```

A non-compiling or failing probe on any supported STL is a hard
pre-`/implement` blocker (the drain design depends on this type). This is a
toolchain-availability gate, not a design risk — `[2f §4.1]` (lines 637–640)
already bounds the type honestly (cold path, not lock-free in general,
ordering pinned).

## 1. Build (local pre-PR gate, `[const §XVII.7]`)

> Resource gate: local Conan/CMake builds are heavy. The agent surfaces `AskUserQuestion` before running them; the user approves first. The contributor adds `local build: green on linux-clang-debug @ <git-sha>` to the PR body.

```sh
conan install . -pr conan/profiles/linux-clang-debug --build=missing
cmake --preset linux-clang-debug
cmake --build --preset linux-clang-debug --target fixpp_sync
ctest --preset linux-clang-debug -R '^sync_'
```

The CMake target `fixpp_sync` covers the `async_mutex` implementation unit (`.cpp` in `src/core/sync/`). The test binaries for all 29 seams live under `tests/sync/`.

## 2. Test (TDD red-green-refactor, `[const §VII.3]`)

Each test seam maps to a named file (see `plan.md` Test-seam mapping). Run all 29 seams:

```sh
ctest --preset linux-clang-debug -R '^sync_'              # all 29 seams
```

Selected seam groupings:

```sh
# Correctness (FIFO, cancellation, drain)
ctest --preset linux-clang-debug -R 'sync_fifo_fairness|sync_cancel|sync_drain'

# Latency seams #1/#2 (latency assertions; soft in debug)
ctest --preset linux-clang-debug -R 'sync_bench_uncontended|sync_bench_contended'

# Destructor death test — requires release-mode linkage (seam #5)
ctest --preset linux-clang-release -R 'sync_destructor_death'

# Slot-allocator storage cases seam #21
ctest --preset linux-clang-debug -R 'sync_slot_allocator_storage'

# PMR fallback + mallocnesia seam #10
LD_PRELOAD=tools/mallocnesia/libmallocnesia.so \
  ctest --preset linux-clang-debug -R 'sync_pmr_fallback'
```

**Per-thread cancellation-recycler warm-up (mandatory before any zero-global-`new` measurement — `[2f §4.3.4]` Erratum E-4 ¶4(b)).** asio's `cancellation_slot` has **no allocator-binding hook**: the **first** cancellation-slot assignment on a given thread does one global `aligned_new` into asio's per-thread recycling cache; every subsequent assignment on that thread reuses the thread-local block. The zero-global-`new` guarantee therefore holds **only in steady state**. Each alloc-guard / `mallocnesia` harness MUST run a documented, explicitly-commented per-thread warm-up iteration (one acquire/cancel cycle per worker thread that primes the recycler) **before** the measurement window opens; the warm-up's one-time first-touch allocation is amortized and is **outside** the measured region (treated as `[2f §6.4]` / §4.3.4 case-2 bench-soft, not a measurement-window allocation). The substantive `waiter_record` allocation remains genuinely zero-global via the per-mutex `waiter_pool_` (E-2) and is what the gate asserts. The verified seam #10 / #21 harness (`tasks.md` T056) already performs this warm-up; it is recorded here so the operator does not misread a first-touch as a regression.

## 3. Tier-1 sanitizer matrix — TSan is mandatory (`[const §IX.2]` / `[const §XVII.8]`)

`async_mutex` is a concurrency primitive; TSan is the primary correctness gate. Run each preset serially (never parallel — `[const §XVII.8]`):

```sh
# TSan — mandatory; covers seams #7 #15 #16 #17 #18 #19 #20 #22 #23 #27 #28
conan install . -pr conan/profiles/linux-clang-tsan --build=missing
cmake --preset linux-clang-tsan
cmake --build --preset linux-clang-tsan --target fixpp_sync
ctest --preset linux-clang-tsan -R '^sync_'
# Zero TSan reports is the gate; any TSan finding is a Gate B blocker.

# ASan + UBSan — covers seams #8 #15 #16 #19 #20 #21 #22 #23 #24 #25 #26 #27 #28
conan install . -pr conan/profiles/linux-clang-asan --build=missing
cmake --preset linux-clang-asan
cmake --build --preset linux-clang-asan --target fixpp_sync
ctest --preset linux-clang-asan -R '^sync_'

conan install . -pr conan/profiles/linux-clang-ubsan --build=missing
cmake --preset linux-clang-ubsan
cmake --build --preset linux-clang-ubsan --target fixpp_sync
ctest --preset linux-clang-ubsan -R '^sync_'

# GCC release sanity
cmake --preset linux-gcc-release
cmake --build --preset linux-gcc-release --target fixpp_sync
ctest --preset linux-gcc-release -R '^sync_'
```

**ARM64 weak-memory stress (seam #18):** must run on a Linux-ARM64 host (e.g., Graviton) under TSan. The x86 TSO model masks ARM64-specific memory-ordering defects.

```sh
# On a Linux-ARM64 host:
conan install . -pr conan/profiles/linux-clang-tsan --build=missing
cmake --preset linux-clang-tsan
cmake --build --preset linux-clang-tsan --target sync_arm64_weak_memory
ctest --preset linux-clang-tsan -R 'sync_arm64_weak_memory'
```

## 4. Fuzz + abidiff — N/A for 006-async-mutex

- **Fuzz N/A** (`[const §VII.7]`; research.md D-11; plan.md Constitution Check `[const §VII.7]` row): the awaitable mutex surface (`async_lock`, `unlock`, `cancel_and_drain`) has no external untrusted input boundary — it is not a parser. The concurrency correctness boundary is covered by TSan + the stress seams (#6, #7, #8, #15, #16, #18). `/speckit-verify` marks this `SKIPPED-with-reason`.
- **abidiff N/A** (`[const §IX.5]`; research.md D-11; plan.md Constitution Check `[const §IX.5]` row): no C-ABI surface is added (`async_mutex` is C++-only per `[2f §5]`; zero `extern "C"` symbols). `/speckit-verify` marks this `SKIPPED-with-reason`.

## 5. Bench + ±5% regression gate (`[const §VIII.1]` / `[const §VIII.2]`)

Latency seams #1 and #2 are Google Benchmark targets. The §6.3 ceilings are:

| Seam | Workload | Ceiling |
|---|---|---|
| #1 | `async_lock` uncontended (single CAS fast path) | ≤ 20–25 ns warm-cache |
| #2 | `async_lock` contended (waiter suspends) | ≤ 80 ns |
| #2 | `unlock` uncontended | ≤ 15 ns |
| #12 | `completion_policy` waiter-resume cost (intent knob) | **E-3:** waiter resumption is **always one `post` hop** regardless of `completion_policy` — there is **no** per-policy waiter-resume latency split (`dispatch` is *not* a free inline-resume; `completion_policy` is a semantic/intent selector only). Cost is the single bound-executor `post` hop for **both** policies. |

```sh
cmake --preset linux-clang-release
cmake --build --preset linux-clang-release \
    --target bench_async_mutex_uncontended bench_async_mutex_contended

./build/linux-clang-release/bench/sync/bench_async_mutex_uncontended \
    --benchmark_out=bench/results/sync_uncontended.json

./build/linux-clang-release/bench/sync/bench_async_mutex_contended \
    --benchmark_out=bench/results/sync_contended.json

python tools/bench_compare.py bench/baselines/sync/ bench/results/ \
    # fails on >5% regression vs [2f §6.3] ceilings
```

The drain-side and `cancel_and_drain` rows are bench-harness-soft per `[2e §6.6]` precedent — investigated, not auto-fail.

## 6. HALO firing spike (seam #9, `[arch §11]` row 2, co-owned 2d + 2f)

In-PR decision artifact (§10 Q1 in `[2f §10]`). Compile under Linux/Clang, Linux/GCC, Windows/MSVC; exercise an in-session `async_lock`; dump assembly; verify the awaiter's coroutine frame is HALO-elided.

```sh
cmake --preset linux-clang-release
cmake --build --preset linux-clang-release --target sync_halo_firing
./build/linux-clang-release/tests/sync/sync_halo_firing  # seam #9

# Inspect assembly:
llvm-objdump -d build/linux-clang-release/tests/sync/sync_halo_firing \
    | grep -A30 'async_lock'

# Failure is NON-FATAL iff seam #10 (PMR fallback) passes for the same toolchain:
ctest --preset linux-clang-release -R 'sync_pmr_fallback'
```

Record the HALO-firing verdict (fired / not-fired per toolchain) in the Gate B decision doc.

## 7. CI grep gate — `std::mutex`-in-coroutine-context (`[const §XV.9]`, seam #14)

The gate must run in Tier-1 CI per `[const §IX.4]`. Verify locally:

```sh
bash tools/check_no_std_mutex_in_awaitable_headers.sh
# Exit 0 = clean; exit non-zero = violation found (prints the offending file + line).

# Verify it fires on a deliberately-violating fixture:
ctest --preset linux-clang-debug -R 'sync_no_std_mutex_ci_gate'
```

The gate operates post-preprocessing (post-`-E`) to catch transitive includes (Codex C-P2-10 close).

## 8. Coverage (`[const §IX.1]`)

```sh
conan install . -pr conan/profiles/linux-clang-coverage --build=missing
cmake --preset linux-clang-coverage
cmake --build --preset linux-clang-coverage --target fixpp_sync
ctest --preset linux-clang-coverage -R '^sync_'

# Generate lcov report:
lcov --capture --directory build/linux-clang-coverage/CMakeFiles/fixpp_sync.dir \
     --output-file coverage/sync.info
genhtml coverage/sync.info --output-directory coverage/sync/

# Gate: DA (line) coverage ≥ 95%; BRDA (branch) coverage ≥ 85% on the touched
#       core/sync (+ session/ helper) modules (matches spec.md SC-009).
# Basis: lcov DA/BRDA per [const §IX.1] (not llvm-cov aggregate — profraw
# staleness caveat per project memory).
```

**Note on templated headers:** `[const §XI.6]` HALO paths and `if constexpr` branches may show unreachable DA/BRDA under one toolchain. Judge on zero-hit DA / not-taken BRDA; do not bloat tests against impossible paths. Per project feedback: the `[const §IX.1]` coverage gate basis = lcov DA/BRDA, not llvm-cov report aggregate. (`[const §IX.2]` is the *sanitizer* clause — TSan/ASan/UBSan, see §3; it is NOT the coverage clause.)

## 9. Verify (mandatory after `/speckit-implement`, `[const §XVII.8]`)

```
/speckit-verify 006-async-mutex
```

Produces `.specify/decisions/006-async-mutex-verify.md` (GREEN / YELLOW / RED). Each polish task is executed serially:

0. STL-availability probe — `std::atomic<std::shared_ptr<T>>` compiles + runs on every supported STL (§0; research.md D-4). Hard pre-`/implement` blocker.
1. Sanitizer matrix (TSan + ASan + UBSan + GCC release).
2. Coverage gate (≥ 95% line / ≥ 85% branch on the touched `core/sync` (+ `session/` helper) modules, `[const §IX.1]`, lcov DA/BRDA basis).
3. `clang-tidy` / `cppcheck` / IWYU sweep.
4. Alloc guard under `mallocnesia` (seams #10 #21) — measured **in steady state, after the mandatory per-thread cancellation-recycler warm-up** (`[2f §4.3.4]` Erratum E-4 ¶4(b); §2 warm-up note). The substantive zero-global-`new` assertion is the per-mutex `waiter_pool_` `waiter_record` allocation (E-2); the one-time per-thread asio cancellation first-touch is amortized and outside the measurement window (bench-soft).
5. CI grep gate (`[const §XV.9]` / seam #14).
6. Bench regression (seams #1 #2 #12 — ±5% vs `[2f §6.3]` ceilings).
7. ARM64 TSan stress if ARM64 host is available (seam #18).
8. Completeness audit: tasks ↔ FR / SC ↔ feature-catalogue.md (100% or waived).

`GREEN` is the `gate-b-done` precondition.

## 10. Gate A / Gate B

```
/gate-a 006-async-mutex   # BEFORE /tasks ([const §XVII.1]); Codex + Opus adversarial; both Codex passes
/gate-b <PR#>             # BEFORE merge ([const §XVII.2]); /speckit-verify GREEN/YELLOW precondition
```

Canonical pipeline order is stated once in `plan.md` Constitution-Check `[const §XVI.4]` row (`/plan` → Gate A → `/tasks` → `/analyze` → `/implement`) — single source of truth; not restated here to avoid drift.

Gate A triggers: Threading/Concurrency (all public methods, phase machine, memory-ordering table I-01..I-31) + Error semantics (4 new `sync_*` variants at slots 43–46) — both mandatory escalation criteria per `[const §XVII.1]`.

Gate B precondition: `/speckit-verify` GREEN or YELLOW with all P1 findings waived. TSan zero-reports is a non-waivable Gate B bar for a concurrency primitive.

Feature catalogue + coverage-index amendments owed at Gate B merge (per `[2f §11]`):
- Add NFR-016 to `library/spec/feature-catalogue.md`.
- Add entry to `library/spec/coverage-index.md` linking `[2f §4.1]` + `[arch §1.1]` to NFR-016.
- Apply Appendix D §D.1 / §D.2 / §D.3 cross-doc drop-ins to `[2d]` (orchestrator applies at sign-off commit).
