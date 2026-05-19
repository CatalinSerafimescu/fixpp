# Quickstart — 007-threading-clock

How to build, test, run sanitizers (**TSan mandatory** — this is a threading feature), exercise the alloc guards + cancellation fuzz harness, bench, measure coverage, verify, and gate the threading contract & `fixpp::core::Clock`. Anchored to `.specify/2d-threading.md` **v0.4** and the constitution Tier-1 matrix (`[const §IX.6]`).

## 0. Pre-`/implement` probes (run BEFORE `/speckit-implement`)

1. **`std::atomic<fixpp::otel::trace_context>` lock-freedom probe** — `EngineConfig::engine_trace_context` is held as a `std::atomic<trace_context>` snapshot; if `std::atomic<trace_context>::is_always_lock_free` is false on a supported target, the build header uses a `seqlock` fallback (research D-1; `[2d §4.4]`). Probe `is_always_lock_free` on every Tier-1/Tier-2 STL; a non-lock-free result is **not** a blocker (seqlock fallback is specified) but MUST select the fallback path, not silently degrade.
2. **`session_executor` is-an-executor probe** — compile-assert `asio::execution::is_executor_v<fixpp::core::session_executor>` and that `bind_executor(session_executor, awaitable)`'s bound-executor type is recoverable as `session_executor` (round 3 root cause #1; seam 21). A failure here is a hard pre-`/implement` blocker (the trace-context access mechanism depends on it) — it is a toolchain/ASIO-version-availability gate, not a design risk (`[2d §4.8]` bounds the mechanism).
3. **`mallocnesia` availability** — `tools/mallocnesia/libmallocnesia.so` present (memory `reference_mallocnesia_path`); the per-thread warm-up caveat (`2a §9` seam #6 / `2b §9` seam #10) applies to the alloc guards (seams 7, 18).

## 1. Build (local pre-PR gate, `[const §XVII.7]`)

> Resource gate: local Conan/CMake builds are heavy. The agent surfaces `AskUserQuestion` before running them; the user approves first. The contributor adds `local build: green on linux-clang-debug @ <git-sha>` to the PR body. Per memory `feedback_self_run_build_gate`, when authorized the agent's own build/ctest runs ARE the gate.

```sh
cd research/G19-fix-fpml-iso20022/library
conan install . -of build/linux-clang-debug -s build_type=Debug --build=missing
cmake --preset linux-clang-debug && cmake --build build/linux-clang-debug
```

No new Conan row (`asio/1.36.0` already pinned by 006; `[const §III.2]`).

## 2. Test (GoogleTest; the 21 `[2d §9]` seams)

```sh
ctest --preset linux-clang-debug --output-on-failure
```

Seam → file map is the authoritative table in `plan.md` (Test-seam → file mapping). FSM-shaped seams (3, 9, 16) and the clock-injection corpus (seam 11, `tests/session/test_clock_injection_corpus.cpp`) are driven by the **scripted test-double FSM** in `tests/support/` (Clarifications 2026-05-19 / D-5) — they assert 2d-owned properties only, never FIX FSM correctness. `tests/conformance/` (FIX-TC TC-001..017) is **not** populated by this feature (owned by `005`).

## 3. Sanitizers (Tier 1, `[const §IX.2]`)

- **TSan — MANDATORY.** `ctest --preset linux-clang-tsan` — strand-serialisation (seam 2), sleep/cancel race (seam 10), session_local lifetime (seam 17), cancellation (seams 4/5), `session_executor` round-trip (seam 19).
- **ASan + UBSan.** `ctest --preset linux-clang-asan` — race/leak seams (10, 14, 17) additionally ASan-clean.

## 4. Allocation guards (`tools/check_alloc.py` + `mallocnesia`, Linux/Clang only)

```sh
ctest --preset linux-clang-asan -R 'dispatch_alloc_guard|clock_sleep_alloc_guard'
```

- Seam 7 (`tests/alloc_guard/test_dispatch_alloc_guard.cpp`): zero **global-heap** `new`/`delete`/`malloc` between parse and `fromApp` over a 10⁴-message corpus (PMR-arena allocations expected, NOT flagged — N-P2-4).
- Seam 18 (`tests/alloc_guard/test_clock_sleep_alloc_guard.cpp`): zero global-heap after the first heartbeat cycle over 10⁴ cycles, **both** threading modes (per-`Session*` timer slot allocated once; D-8).

## 5. Cancellation fuzz harness (seam 12; voluntary per `[2d §9 seam 12]` Gate-A discretion)

```sh
ctest --preset linux-clang-fuzz -R fuzz_session_cancellation        # ≥10 min CI; longer on main
```

libFuzzer fires `close(graceful)`/`close(terminal)` at every `co_await` checkpoint; assert no deadlock / double-free / PMR leak. 2d is NOT parser-touching — `[const §VII.7]` is strictly N/A; this harness is shipped voluntarily (research D-11).

## 6. Bench (`[const §VIII.1]`/`[const §VIII.2]`, ±5%)

```sh
cmake --build build/linux-clang-release --target bench_threading
./build/linux-clang-release/bench/threading/bench_threading --benchmark_out=...
```

Ceilings = `plan.md` Technical-Context table (`[2d §6.3]`). CI fails on >5% regression vs `bench/baselines/threading/threading_baselines.json`. The cross-thread dispatch row is **bench-soft** (OS-jitter-dependent; `[2d §10]` Q4 is a 2d-implementation bench-spike follow-up — may tighten below 250 ns; not a blocker).

## 7. Coverage (`[const §IX.1]` ≥95% line / ≥85% branch on touched modules)

```sh
ctest --preset linux-clang-coverage && tools/coverage_report.sh
```

Scope: owned `core/` headers (+ `.cpp` if out-of-line) + `session/session_config.hpp` + the `Session` skeleton. Judged on the **lcov DA/BRDA basis** (memory `feedback_coverage_gate_lcov_basis`), not the `llvm-cov report` aggregate — header-inline / `if constexpr` / per-instantiation-unreachable paths assessed on zero-hit DA / not-taken BRDA. Re-run feature binaries fresh before judging (memory `feedback_coverage_profraw_staleness`).

## 8. `/speckit-verify` (mandatory post-`/implement`, `[const §XVII.8]`)

Local Tier-1 mirror of CI; produces `.specify/decisions/007-threading-clock-verify.md` — required evidence for `/gate-b`. Verify each polish task actually fires: TSan preset, alloc guards (global-heap-only semantics), coverage (lcov basis), static analysis, fuzz harness, bench vs baselines. Non-RED is a hard `/gate-b` precondition, alongside the feature-completeness audit (tasks ↔ FR/SC ↔ catalogue; passes **without** a waiver — 2d claims no FIX-TC discharge; D-5/D-11).

## 9. `/gate-a` (before `/speckit-tasks`)

Phase-4 bundle Gate A — both Codex passes (rescue + `/codex:adversarial-review`) per memory `feedback_gate_a_codex_dual_pass`; Opus triages/rewrites; reviews → `research/reviews/`. Reviews this bundle for internal consistency + faithfulness to `.specify/2d-threading.md` v0.4 — it does **not** re-litigate the signed-off round-3 closures. Run `codex-usage.sh` for quota first (memory `feedback_gate_codex_quota`).

## 10. Pipeline order & `/gate-b`

Canonical order (single source of truth = `plan.md` Constitution-Check `[const §XVI.4]` row): **`/plan` → Gate A → `/tasks` → `/analyze` → `/implement` → `/simplify` → `/speckit-verify` → `/gate-b`**. `/speckit-simplify` runs between `/implement` and `/speckit-verify` (memory `feedback_speckit_simplify_before_verify`). `/gate-b` is mandatory pre-merge (`[const §XVII.2]`); author≠reviewer (`[const §XVII.3]`); preconditions = `/speckit-verify` non-RED + feature-completeness audit non-failing. NFR-015 + `coverage-index.md` + `[arch §11]` row-7 + `[arch §5.4]`/2k-`clock_scope` drop-ins are applied by the **orchestrator at sign-off**, not this feature (research D-12).
