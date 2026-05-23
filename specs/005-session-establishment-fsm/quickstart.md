# Quickstart — 005-session-establishment-fsm

Build / test / bench / sanitizer / coverage / verify / gate flow for the `session/` first feature. All commands run with cwd = library submodule (`research/G19-fix-fpml-iso20022/library`). Resource gate: an AI agent surfaces `AskUserQuestion` before any local Conan/CMake build (`[const §XVII.7]`); the user approves first.

## 1. Scope recap

Owns S-001/2/3/4/7/8/9/15/16/19/20 + folded `core/` time-helper #4. Consumes the `[2e §4.1]` MessageStore seam, the `[2d §4.1]` Clock seam, `[2f §4.1]` async_mutex, and the merged `wire/`/`dictionary/` surfaces. Publishes `seqnum_t` (closes the `[2e §10 Q9]` handoff) and closes the `core/` module-exit row #4. **No** C-ABI surface, **no** fuzz harness (D-12).

## 2. Build

```bash
conan install . -pr conan/profiles/linux-clang-debug --build=missing
cmake --preset linux-clang-debug
cmake --build --preset linux-clang-debug --target fixpp_session fixpp_core
```

`fixpp_session` reuses `fixpp_core` (incl. the new `core/fix_time`), `fixpp_sync` (`async_mutex`), `fixpp_wire`, `fixpp_dict`, and the `[2e §4.1]` seam header — **no new Conan row** (`[const §III.2]`).

## 3. Tier-1 preset matrix (serial — `[const §XVII.8]`)

`linux-clang-debug`, `linux-clang-release`, `linux-clang-asan`, `linux-clang-ubsan`, **`linux-clang-tsan`** (threading-affecting — strand/async-mutex/cancellation), `linux-clang-coverage`, plus `linux-gcc-release` sanity. One configuration at a time (never parallel).

```bash
ctest --preset linux-clang-debug   --output-on-failure   # all tests/session/* + conformance
ctest --preset linux-clang-tsan    --output-on-failure   # strand/mutex/cancellation seams #3/#7/#11
```

## 4. Test seams

`tests/session/` seams #1..#13 + `tests/session/conformance/tc_*.cpp` (Q2 in-scope `[FIX-TC]` subset, authored from the QuickFIX/J `.def` oracle — research D-10) + `tests/support/{transport_double,store_double}.hpp` + `[2d §4.3]` `mock_clock`. TDD red-green-refactor (`[const §VII.1/3]`). Mapping: see plan.md "Test-seam → file mapping" (no globs).

## 5. Benchmarks

```bash
cmake --build --preset linux-clang-release --target session_bench
./build/linux-clang-release/bench/session/fsm_bench   # + seqnum/fix_time/heartbeat
```

±5% vs `bench/baselines/session/*.json` (`[const §VIII.2]`); ceilings per plan.md Technical-Context table. Session-throughput parity-vs-QuickFIX is a v1.0 release gate measured later under `transport/` (D-13) — not a this-PR blocker.

## 6. Coverage

`linux-clang-coverage`, fresh per-binary profraw (never reuse an aborted build's — see the verify-procedure note / memory `feedback_coverage_profraw_staleness`). Gate basis = lcov DA/BRDA, not the `llvm-cov report` aggregate (memory `feedback_coverage_gate_lcov_basis`). Touched modules = `include/fixpp/session/*`, `src/session/*`, `include/fixpp/core/fix_time.hpp`, `src/core/fix_time.cpp`. ≥95% line / ≥85% branch, or every uncovered line/branch carries a recorded Opus risk assessment in the verify doc (`[const §IX.1]` binding rule).

## 7. Allocation discipline

`tools/check_alloc.py` under `mallocnesia` (`tools/mallocnesia/libmallocnesia.so`, memory `reference_mallocnesia_path`): zero global `new`/`delete` on the inbound-dispatch / timer-fire / seqnum paths (seam #12, SC-009). Allocation confined to `Session::open` + caller arenas (`[const §VIII.5]`).

## 8. `/speckit-verify` (mandatory post-`/implement`, `[const §XVII.8]`)

`/speckit-verify 005-session-establishment-fsm` → `.specify/decisions/005-session-establishment-fsm-verify.md`. Verdict `GREEN` (all PASS / SKIPPED-with-reason — fuzz + abidiff are SKIPPED-with-reason per D-12) / `YELLOW` (every FAIL waived w/ rationale) / `RED`. `/gate-b` refuses on absent/`RED`.

## 9. Gate A (before `/tasks`, `[const §XVII.1]`)

`/gate-a 005-session-establishment-fsm` — first FSM design review of record (no Phase-2 FSM doc by design, `[2e §3.1]`). Both Codex passes (rescue + `/codex:adversarial-review`) per memory `feedback_gate_a_codex_dual_pass`; Opus triages, Opus rewrites; reviews → `research/reviews/`. Blockers resolved/waived before `/tasks`. Then `/speckit-analyze` (drift), then `/speckit-tasks`.

## 10. Gate B (pre-merge, `[const §XVII.2]`)

Standard Gate B; `/speckit-verify` `GREEN`/`YELLOW` precondition (`[const §XVII.8]`). Local pre-PR build line in the PR body: `local build: green on linux-clang-debug @ <sha>` (`[const §XVII.7]`).

## 11. Module-exit bookkeeping at merge (pipeline step 16 — memory `feedback_pipeline_mark_done_step`)

On merge, update **all** close-out surfaces: catalogue rows S-001/2/3/4/7/8/9/15/16/19/20 → `done`; `coverage-index.md` (owned rows + the Q2 deferred-with-traceability entries pointing at the later recovery feature); submodule bump; gate labels via `gh api` REST (`--repo CatalinSerafimescu/fixpp`); phase-4 Track Log; **`session/README.md`** features table + module-exit checkboxes; **`core/README.md` row #4 + `core/` exit checkbox** (the time-helper #4 close — most-forgotten surface); the controlling decision-doc log; lifecycle sign-off; project memory.
