# Implementation Plan: Test-Binary Grouping

**Branch**: `068-test-binary-grouping` | **Date**: 2026-07-10 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/068-test-binary-grouping/spec.md`

## Summary

Reduce the 66.9 GB test-binary matrix (462 one-`.cpp`-per-executable binaries × 8 presets, each static-linking the fixpp stack) by grouping isolation-safe test `.cpp` files within each module into fewer `gtest_discover_tests` executables — bucketed by (shared link-deps ∩ label-homogeneous) — while keeping isolation-sensitive tests standalone. Roll out one module at a time in descending disk-impact order (pilot = `dictionary`, then `session` → interop → capi → …), measuring per-module before/after disk and (on the pilot) ctest wall-time + incremental relink. Preserve every gate, audit, and `ctest -L`/`-R` selection. **Test-infrastructure only — no production/library code change.**

## Technical Context

**Language/Version**: CMake (build system) driving Clang 22 / GCC test compiles; no C++ production source changed
**Primary Dependencies**: CMake ≥ (repo min), GoogleTest + `include(GoogleTest)` → `gtest_discover_tests`, CTest
**Storage**: N/A (build artifacts only; baseline CSV in `research/test-grouping-baseline/`)
**Testing**: the existing GoogleTest suite, run unchanged via `ctest --preset <p>` across all 8 presets; behavior-preservation is the acceptance signal
**Target Platform**: Linux/Clang (Tier 1) primary; must not change what any preset builds/runs
**Project Type**: library — internal test infrastructure
**Performance Goals**: several-fold disk reduction on the grouped portion (SC-001); CI unfiltered-ctest wall-time regression ≤ 10% per preset, target net-neutral (SC-005)
**Constraints**: no `src/`, `include/`, C-ABI, Python, or runtime change; preserve coverage-index + completeness audits + `ctest -L`/`-R` selectability; test source edited only to resolve an ODR collision (FR-012); local build parallelism capped `-j2` (WSL2 OOM); measurement env = local WSL2 clang presets
**Scale/Scope**: 24 test modules, ~462 binaries/preset, 8 presets

*No NEEDS CLARIFICATION remain — resolved in spec `## Clarifications` (2026-07-10).*

## Constitution Check

*GATE: Must pass before Phase 0. Re-checked after Phase 1.*

| Article | Applies? | Disposition |
|---|---|---|
| **VII — Testing (GoogleTest, TDD, no code without a test)** | Adapted | This is a **packaging refactor of existing tests**, not new production behavior. "Red-green" maps to: the full existing suite stays green per preset before/after each module (FR-006/FR-007); no test added or removed, only repackaged. No production code added → §3/§4 (test-first for new code) vacuously satisfied. |
| **VIII — Perf budgets** | Adapted | No production hot-path change. The relevant budget is the feature's own SC-005 (ctest wall-time ≤10%/preset), measured on pilot + session. No `bench/` baseline touched. |
| **IX — Sanitizers/Coverage/Static analysis** | Applies (preserve) | Full ASan/UBSan/TSan + coverage matrix MUST stay green after each module (FR-006), no lost isolation. **Coverage thresholds vacuous**: touched files are `tests/**/CMakeLists.txt` (excluded from coverage per §1); no `include/`/`src/` line added. clang-tidy/format/cppcheck/iwyu unaffected (any ODR-rename in test code stays clang-format clean). |
| **X — ABI** | N/A | No C-ABI surface touched. |
| **XI — Concurrency** | N/A | No coroutine/threading code touched; TSan-sensitive tests stay standalone. |
| **XVI §3 — /clarify mandatory for {ABI,threading,error,wire,codegen,session FSM,security}** | Not triggered, done anyway | Feature touches **none** of the trigger domains (only test CMake). `/clarify` was run regardless (4 Qs, 2026-07-10). |
| **XVI §4 — /analyze mandatory (same set)** | Not triggered, will run | Will run `/speckit-analyze` per pipeline for drift check. |
| **XVII §1 — Gate A** | **Not triggered → waivable** | No Gate-A trigger path touched (no public API/ABI, no concurrency, no wire/parser/codegen, no session FSM, no security, no new `.specify/` design doc). Qualifies for `gate-a-waived` (tests-only + behavior-preserving), per the retro-remediation precedent. Final waive/run decision at gate time with rationale. |
| **XVII §2 — Gate B** | **Applies (mandatory)** | Required before merge regardless of size. |
| **XVII §7 — Local pre-PR build gate + resource gate** | Applies | Local `linux-clang-debug` green confirmation required; **AI agent MUST `AskUserQuestion` before running any heavy local build** (§7 resource gate). |
| **XVII §8 — /speckit-verify mandatory after /implement** | Applies | Produces `.specify/decisions/068-test-binary-grouping-verify.md`; `/gate-b` precondition. |

**Verdict: PASS.** No article violated; no Complexity-Tracking entry needed. Notable posture: Gate A is *not triggered* by domain and is a waiver candidate — recorded, not assumed.

## Project Structure

### Documentation (this feature)

```text
specs/068-test-binary-grouping/
├── plan.md              # This file
├── spec.md              # Feature spec (+ Clarifications)
├── research.md          # Phase 0 — grouping mechanics decisions
├── data-model.md        # Phase 1 — entities (module/bucket/disposition/baseline)
├── quickstart.md        # Phase 1 — per-module grouping + verification procedure
├── checklists/
│   └── requirements.md  # Spec quality checklist (from /speckit-specify)
└── tasks.md             # Phase 2 — /speckit-tasks output (NOT created here)
```

*No `contracts/` — this feature exposes no external interface (no public API/CLI/wire surface). The only observable contract is CTest test identity + labels, addressed in data-model.md and preserved by FR-003/FR-004.*

### Source Code (repository root)

```text
tests/
├── <module>/CMakeLists.txt   # THE edit surface (24 modules); grouped + standalone target defs
├── core/CMakeLists.txt       # reference precedent (already grouped)
└── ...                       # test .cpp edited ONLY on ODR collision (FR-012)

research/test-grouping-baseline/
├── BASELINE.md               # per-module/per-preset baseline (2026-07-10)
├── baseline-2026-07-10.csv   # comparison basis
└── inventory.sh              # re-runnable before/after measurement
```

**Structure Decision**: Single edit surface — `tests/**/CMakeLists.txt`. No `src/`, `include/`, `bindings/`, `bench/`, or codegen change. Measurement artifacts live under the parent-repo `research/` tree with the CSV/script committed into the feature evidence at close.

## Complexity Tracking

*No Constitution violations — section intentionally empty.*
