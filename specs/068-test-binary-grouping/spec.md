# Feature Specification: Test-Binary Grouping

**Feature Branch**: `068-test-binary-grouping`
**Created**: 2026-07-10
**Status**: Draft
**Input**: User description: "Group isolation-safe test .cpp files within each module into fewer executables to cut per-preset disk and build/link cost, preserving all gates and ctest selectability."

## Overview *(context)*

The test suite currently builds **one executable per test `.cpp`** (462 binaries per preset), each statically linking the fixpp stack. Across 8 build presets this is **66.9 GB** of test binaries (baseline captured 2026-07-10 in `../research/test-grouping-baseline/` — the **parent repo**, one level above the `library/` submodule; `research/` is gitignored + CI-guarded inside `library/` per Article XV §18, so it correctly lives in the parent — re-runnable via `inventory.sh`). The dominant cost is identical instrumented library code duplicated hundreds of times per preset. This is the top disk consumer on the developer machine and inflates clean-build link time and CI storage.

A validated spike (dictionary, 2026-07-10) grouped 14 pure tests into one binary: **debug 85.3→12.0 MB (7.1×), asan 150.0→18.1 MB (8.3×)**, 251 tests green in one process, per-test granularity preserved, no ODR collisions. `tests/core/CMakeLists.txt` already uses this pattern in production (`gtest_discover_tests` for `fixpp_core_tests`/`fixpp_capi_tests`, standalone `add_test` for tests needing name-selection or special properties). This feature generalizes that proven pattern across **all 23 test modules**, one module at a time, with **before/after measurement** and **zero regression** to any existing gate or test guarantee.

## Clarifications

### Session 2026-07-10

- Q: How should tests be bucketed into grouped binaries within a module? → A: Bucketed by (shared link-deps ∩ label-homogeneous) — multiple grouped binaries per module, not one giant per-module binary — to bound incremental-relink blast radius (matches the `tests/core` precedent).
- Q: What CI unfiltered-ctest wall-time regression per preset is acceptable? → A: ≤ 10% per preset, target net-neutral-or-better; measured on the pilot and confirmed on `session`. *(Operationalized in SC-005 as the affected module's `ctest -L <module>` subset — the discriminating launch-overhead proxy for the preset-wide bound; a whole-preset delta over ~25–160 of ~462 binaries is noise-dominated.)*
- Q: Which module is the US1 pilot (full 3-metric validation before the session rollout)? → A: `dictionary` (already partially spiked); keep the disk + ctest-wall-time + incremental-relink measurement on it.
- Q: When two grouped `.cpp` files have an ODR/symbol collision, what is the resolution policy? → A: Prefer renaming the colliding helper/global **within test source** to keep the test grouped; carve to standalone only when the collision is a gtest `Suite.Name` (coverage-index keys on those — must not rename) or is otherwise unresolvable.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Prove the grouping pattern + measurement on a pilot module (Priority: P1)

As a maintainer, I convert one representative module's isolation-safe tests into grouped executables, keeping isolation-sensitive tests standalone, and record the disk, ctest wall-time, and incremental-relink deltas — establishing a repeatable, evidence-backed pattern before scaling to the large modules.

**Why this priority**: This is the MVP. It reclaims real disk on that module *and* empirically settles the two open design decisions (grouping granularity / bucketing, and whether `gtest_discover_tests` launch-overhead is acceptable) with measured numbers, so the rollout to the remaining 23 modules is de-risked rather than speculative.

**Independent Test**: Pick one module; apply the group/standalone taxonomy; build all 8 presets; run `ctest` per preset (green); re-run `inventory.sh` and diff against the saved baseline (disk reduced); record ctest wall-time and single-test relink time before/after. Delivers a reclaimed module + a validated, documented pattern.

**Acceptance Scenarios**:

1. **Given** a module with a mix of pure and isolation-sensitive tests, **When** grouping is applied, **Then** every pure/stateless test is grouped into a whole-binary `add_test` executable and every isolation-sensitive test remains its own executable, each disposition recorded with a reason.
2. **Given** the grouped module, **When** each of the 8 presets is built and `ctest --preset <p>` is run, **Then** the full test set passes on every preset with no new sanitizer findings and no isolation regressions.
3. **Given** the grouped module, **When** `inventory.sh` is re-run, **Then** the module's per-preset binary size is measurably reduced versus baseline and the delta is recorded.
4. **Given** the grouped module, **When** ctest wall-time and a single-test incremental relink are measured, **Then** both are recorded against the pre-grouping values and the granularity decision is justified from those numbers.
5. **Given** the grouped module, **When** the documented `ctest -L <module/feature>` and `ctest -R <name>` selections for that module are run, **Then** they select the same logical tests as before (selectability preserved).

---

### User Story 2 - Roll grouping across all remaining modules in disk-priority order (Priority: P2)

As a maintainer, I apply the validated pattern module-by-module across the remaining modules in descending disk-impact order (session first — 36% of all test binaries — then interop, capi, config, …), reclaiming the bulk of the 66.9 GB.

**Why this priority**: Depends on US1's validated pattern and metrics. `session` alone dominates; capturing it plus the next few modules delivers the majority of the total disk win.

**Independent Test**: For each module, apply the taxonomy, build the presets, run ctest green, and diff `inventory.sh` against baseline — each module is an independently shippable increment with its own recorded before/after delta.

**Acceptance Scenarios**:

1. **Given** the validated pattern, **When** each remaining module is processed, **Then** every test in that module is either grouped or dispositioned as must-stay-individual with a stated reason, and the module's disk delta is recorded.
2. **Given** all modules processed, **When** the full baseline inventory is re-run, **Then** the total test-binary footprint per preset is reduced by a recorded, several-fold factor on the grouped portion.
3. **Given** any module in the rollout, **When** its 8-preset build + ctest run completes, **Then** it is green with no isolation regressions before the next module is started.

---

### User Story 3 - Preserve every gate, audit, and selection command (Priority: P1)

As a maintainer, I guarantee that grouping changes **nothing** observable to the coverage-index, the feature-completeness audits, CI gates, `/speckit-verify`, and every documented `ctest -L`/`-R` selection — only the executable packaging changes.

**Why this priority**: P1 alongside US1 — it is the hard invariant that makes grouping safe. A grouping that reclaims disk but breaks a gate or a documented selection command is a net negative and must not merge.

**Independent Test**: Run the coverage-index check, the completeness audits, and every documented per-module `ctest -L`/`-R` command before and after grouping a module; assert identical logical results. CI runs full unfiltered ctest, so its greenness is the backstop.

**Acceptance Scenarios**:

1. **Given** a grouped module, **When** the coverage-index and completeness audits run, **Then** they remain green — they key on `.cpp` source stems + gtest `Suite.Name`, which are unchanged by grouping.
2. **Given** a grouped module, **When** `ctest -L <feature/module>` is run, **Then** it selects the intended gtest cases and no others (buckets are label-homogeneous and labels are re-applied at case granularity).
3. **Given** a test previously selected by `ctest -R <target-name>`, **When** that selection is still required by a documented procedure, **Then** either the test remains standalone (name preserved) or an equivalent label/name selection is documented as its replacement.
4. **Given** a test with a per-target `ENVIRONMENT`, `TIMEOUT`, special link dependency, or compile-definition, **When** grouping is considered, **Then** that property is preserved on the grouped binary or the test stays standalone.

---

### Edge Cases

- **Symbol/ODR collision** across two `.cpp` grouped into one TU-set (same-named non-static free function, global, or identical `TEST(Suite,Name)`) → the grouped binary fails to build or link; that test is carved out to standalone (or the collision renamed only within test code) and the collision recorded.
- **A test relies on fresh process-global state** (singleton, global new-handler, allocation counter, static registry) → pooling with siblings corrupts its assertion; it must stay standalone. This is the core risk the taxonomy guards against.
- **A grouped bucket would span two feature labels** → `ctest -L <feature>` would over- or under-select; the bucket is split so each is label-homogeneous, or the odd test stays standalone.
- **A death test that `abort()`s/`_exit()`s at top level** (not via gtest's fork-based `EXPECT_DEATH`) → would kill sibling tests in the same process; stays standalone.
- **A `.cpp` compiled twice with different `-D`** (e.g. `decimal_mul_u64_wide` vs `_portable`) → cannot share one object; stays as separate targets.
- **A preset that does not build a given test** (e.g. TSan-only or release-only targets) → grouping must not change which tests each preset builds/runs.
- **(Superseded — applied to the rejected `gtest_discover_tests` mechanism, moot under the chosen whole-binary `add_test`, which has no build-time discovery step.)** `gtest_discover_tests` post-build discovery runs the binary at build time to enumerate cases → a binary that crashes on `--gtest_list_tests` breaks discovery (the build fails loudly, so this cannot regress silently); the offending `.cpp` would be carved out of that bucket to standalone, using the same carve-out policy as an unresolvable ODR collision (FR-012), and the carve-out recorded in the disposition ledger. Caught by the pilot before rollout (see measurements.md §(b)).

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The build MUST group isolation-safe (pure, stateless, side-effect-free) tests within a module into one or more shared executables, each registered as a single whole-binary `add_test` entry carrying the module/feature LABEL(s). Case-level `ctest -R Suite.Case` selection is explicitly NOT required (no per-case ctest entry existed for these tests before grouping either); `ctest -L <label>` is the selector for the whole grouped suite.
- **FR-002**: The build MUST keep the following categories as standalone executables (one per test), each disposition recorded with its reason: allocation-counting / `alloc_guard` tests (in-TU global-new counters, mallocnesia `LD_PRELOAD` gates); OOM-injection tests; TSan-specific targets and any test carrying a per-test **heterogeneous** `ENVIRONMENT` (a test whose required `ENVIRONMENT` is shared by its whole bucket may be grouped, carrying that `ENVIRONMENT` on the grouped binary — see Research §D3); death tests that terminate the process at top level or override link mode; genuinely-concurrent / global-singleton-freshness tests; per-target compile-definition variants; completeness-gate tests asserting exact-set equality with precise feature labels; and any test a documented procedure selects by `ctest -R <target-name>`.
- **FR-003**: Every grouped bucket MUST be **label-homogeneous** — all cases in one grouped binary share the same intended `ctest` LABEL set — and the build MUST re-apply those LABELS to the grouped binary's discovered cases so `ctest -L <label>` selects exactly the intended cases (no over- or under-selection).
- **FR-004**: The coverage-index (`spec/coverage-index.md`) and the feature-completeness audits (`.specify/decisions/<feature>-completeness.md`) MUST remain green and unmodified in substance; grouping MUST NOT require renaming any `.cpp` source or any gtest `Suite.Name`, since those are the identities those gates key on.
- **FR-005**: Grouping MUST preserve, per test, every per-target link dependency (unioned into the grouped binary), `TIMEOUT`, and `ENVIRONMENT`; where a required property cannot be represented on a shared binary, that test MUST stay standalone.
- **FR-006**: The full build/test matrix — asan, tsan, ubsan, debug, coverage, gcc-release, clang-release, libc++ — MUST stay green per preset after each module is grouped, with no new sanitizer findings and no loss of isolation (each sanitizer still detects what it detected pre-grouping).
- **FR-007**: Grouping MUST NOT change which tests any preset builds or runs — only how `.cpp` files are packaged into executables. The set of executed gtest cases per preset is identical before and after.
- **FR-008**: Each module's before/after per-preset binary footprint MUST be captured via the baseline `inventory.sh` and the delta recorded as committed evidence.
- **FR-009**: On the first rolled-out (pilot) module, the change MUST additionally record the ctest wall-time delta (launch-overhead check) and a single-test incremental relink-time delta (blast-radius check), and the chosen grouping granularity MUST be justified from those measurements.
- **FR-010**: The rollout MUST proceed one module at a time in descending disk-impact order; a module is not started until the previous module's 8-preset build + ctest run is green and its delta recorded.
- **FR-011**: Every test in every module MUST end the feature either grouped or explicitly dispositioned as must-stay-individual with a recorded reason — no test left unaccounted for.
- **FR-012**: On an ODR/symbol collision between two grouped `.cpp` files, the build MUST first attempt to resolve it by renaming the colliding helper/global **within test source** (keeping the test grouped); a test MUST be carved to standalone only when the collision is a gtest `Suite.Name` (which MUST NOT be renamed per FR-004) or is otherwise unresolvable. Each such carve-out or in-test rename MUST be recorded.
- **FR-013**: Grouped binaries within a module MUST be bucketed by (shared link-dependencies ∩ label-homogeneous group), yielding one or more grouped binaries per module rather than a single per-module binary, so that editing one test relinks only its bucket.

### Key Entities *(include if feature involves data)*

- **Test module**: a `tests/<module>/` directory with its own `CMakeLists.txt` (23 total, out of 25 `tests/` subdirectories; `tests/abi/` and `tests/support/` are each fixtures-only — no `.cpp`/`CMakeLists.txt` — and neither is a module); the unit of rollout.
- **Grouped bucket**: a set of isolation-safe, label-homogeneous, link-compatible test `.cpp` files compiled into one whole-binary executable registered as a single `add_test` entry.
- **Standalone test**: a test that must remain its own executable per the FR-002 taxonomy; carries its own ctest name, labels, and properties.
- **Disposition record**: per-test decision (grouped-into-<bucket> | standalone:<reason>), the audit trail proving FR-011.
- **Baseline inventory**: the committed 2026-07-10 per-module/per-preset size table; the comparison basis for all deltas.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: After full rollout, the total test-binary footprint per sanitizer preset is reduced several-fold on the grouped portion versus the 2026-07-10 baseline. This is a **recorded-observation outcome, not a numeric pass/fail bar** — the enforced gates are FR-008 (every module's delta captured) and SC-006 (100% dispositioned); "several-fold" documents the expected order of magnitude (the dictionary spike measured 7.1×–8.3×) for the rollup in `measurements.md`, not a threshold a module must clear to pass.
- **SC-002**: All 8 presets are `ctest`-green after every module, with zero new sanitizer findings introduced by grouping.
- **SC-003**: The coverage-index and every feature-completeness audit remain green and unmodified in substance across the whole feature.
- **SC-004**: Every documented `ctest -L <label>` and `ctest -R <name>` selection resolves to the same logical set of tests after grouping as before.
- **SC-005**: `ctest` wall-time regression per preset does not exceed **10%** (target net-neutral-or-better). Measurement scope is the affected module's own subset (`ctest -L <module>`, before vs after grouping that module), not a full-preset unfiltered timing — grouping only ~25–160 of ~462 binaries per module makes a whole-preset delta too small to discriminate from noise. This module-subset delta is used as the launch-overhead proxy for the preset-wide bound; measured on the pilot (`dictionary`) and confirmed at scale on the largest module (`session`), per Research §D6.
- **SC-006**: 100% of tests across all 23 modules are accounted for as either grouped or standalone-with-reason at feature close.

## Assumptions

- **Pilot module** *(clarified)*: US1 pilot is **`dictionary`** (already partially spiked); it carries the full 3-metric harness (disk + ctest wall-time + incremental relink). The `session` module (largest, 36% of binaries) leads the US2 rollout and re-confirms the wall-time bound at scale.
- **Grouping granularity** *(clarified)*: **bucketed** — one grouped binary per (shared-link-deps ∩ label-homogeneous) group within a module, not one giant per-module binary — to bound incremental-relink blast radius (FR-013). Refine bucket boundaries against the pilot's measured relink numbers.
- **ctest entry model**: grouped binaries use a single whole-binary `add_test` entry per bucket (not `gtest_discover_tests` per-case discovery) — one ctest launch per bucket, selected via `ctest -L <label>`. Case-level `ctest -R Suite.Case` selection is not preserved and was never required (FR-001); this eliminates the per-case launch overhead the original per-case design would have accepted — quantified on the pilot as faster than status quo, not merely within the wall-time bound (see measurements.md).
- **Acceptable CI wall-time regression bound** *(clarified)*: **≤ 10%** ctest wall-time increase, target net-neutral-or-better — measured on the affected module's `ctest -L <module>` subset (the discriminating launch-overhead proxy), per SC-005 / Research §D6.
- **No production/library code changes**: this feature touches only `tests/**/CMakeLists.txt` and test source only where a within-test-code rename is needed to resolve an ODR collision; no `src/`, no public headers, no C-ABI, no runtime behavior.
- **Precedent**: `tests/core/CMakeLists.txt` is the reference implementation for the grouped + standalone coexistence pattern.
- **Measurement environment**: local WSL2 clang presets are the measurement basis (matching the baseline capture); build parallelism is capped at `-j2` per the project's OOM constraint.
