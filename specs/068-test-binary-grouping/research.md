# Phase 0 Research: Test-Binary Grouping

All Technical-Context unknowns are resolved. This feature's "research" is the mechanics of grouping under GoogleTest/CTest + the census of isolation-sensitive tests. Grounded in the dictionary spike (2026-07-10), the `tests/core` precedent, and a repo census.

## D1 — Grouping mechanism: `gtest_discover_tests`, defaults (POST_BUILD)

- **Decision**: Grouped buckets use `add_executable(<bucket> <cpp...>)` + `gtest_discover_tests(<bucket> PROPERTIES LABELS "<shared-labels>")`, matching `tests/core/CMakeLists.txt` (`fixpp_core_tests`, `fixpp_capi_tests`). No project-wide `DISCOVERY_MODE` is set, so the default **POST_BUILD** discovery applies (binary is run once at build time with `--gtest_list_tests` to enumerate cases).
- **Rationale**: POST_BUILD is already proven across the full sanitizer matrix in-repo (core). Each gtest case becomes its own CTest entry → preserves `ctest -R <case>` and `ctest -L <label>` at case granularity (spec FR-001/FR-003).
- **Alternatives considered**: `DISCOVERY_MODE PRE_TEST` (defers enumeration to test time — avoids running the instrumented binary at build). Rejected as default because core proves POST_BUILD works; **revisit only if** the pilot shows build-time discovery cost is material under sanitizers. A single coarse `add_test(NAME <bucket>)` was rejected — it collapses to one CTest entry and loses `-L`/`-R` granularity (spec Assumptions).

## D2 — Label preservation at case granularity

- **Decision**: Apply LABELS on the grouped binary via `gtest_discover_tests(<bucket> PROPERTIES LABELS "<labels>")`; every discovered case inherits that set. Buckets are **label-homogeneous** (FR-003) so `ctest -L <label>` selects exactly the intended cases — no over/under-selection.
- **Rationale**: The coverage-index and completeness audits key on `.cpp` source stems + gtest `Suite.Name` (Explore census 2026-07-10), which grouping does **not** change → those gates are unaffected (FR-004). The only identity that changes is the CTest *entry name* for the collapsed `add_test`-per-binary tests; re-applying labels restores `-L` selectability, the sole thing `/speckit-verify`/quickstarts rely on.
- **Consequence**: A test carrying a **unique** feature label that must be `-L`-selected alone either goes in its own label-homogeneous bucket or stays standalone.

## D3 — Refined must-stay-INDIVIDUAL taxonomy (census-corrected)

Two spec categories were **narrowed** by the census — more tests are groupable than the initial spike taxonomy assumed:

- **Death tests → GROUPABLE when fork-based.** `EXPECT_DEATH`/`ASSERT_DEATH` fork a child process, so they run safely inside a shared binary (used in capi, codegen, session, sync, wire). **Standalone only** when a test calls `abort()`/`_exit()`/installs a process-terminating handler at **top level** (outside a death-macro child). Refines FR-002.
- **ENVIRONMENT → GROUPABLE when homogeneous across the bucket.** `gtest_discover_tests(... PROPERTIES ENVIRONMENT ...)` applies one env to all discovered cases. A bucket whose members share the same env (e.g. a common `*_FIXTURES_DIR`) can carry it on the grouped binary. **Standalone only** when the env is **per-test-heterogeneous** (e.g. a TSan suppression file scoped to one seam). Modules with ENVIRONMENT today: tls, core, oracle, perf, session, transport, wire, sync.

**Standalone (unchanged from spec FR-002):**
- allocation-counting / `alloc_guard` (in-TU global-`operator new` counters; mallocnesia `LD_PRELOAD` gates run the *binary* under interception — pooling siblings pollutes the count)
- OOM-injection (global new-handler / injection state)
- TSan-only targets / per-test-heterogeneous ENVIRONMENT
- top-level process-terminating tests (per D3 above)
- genuinely-concurrent / global-singleton-freshness
- per-target compile-definition variants (e.g. `decimal_mul_u64_wide` vs `_portable` — two objects from one `.cpp`, cannot share a TU)
- completeness-gate tests asserting exact-set equality with a precise feature label
- any test a documented procedure selects by `ctest -R <target-name>`

## D4 — Bucketing algorithm (per module)

- **Decision**: Partition a module's groupable `.cpp` by the key **(sorted set of link libraries, sorted set of intended labels)**. Each partition → one grouped binary. Union each member's link deps (e.g. `pugixml`) onto the bucket. Homogeneous ENVIRONMENT (D3) applied to the bucket; else that member is standalone.
- **Rationale**: Bounds incremental-relink blast radius (FR-013) — editing one test relinks only its bucket, not the whole module — while deduping the bulk of the static lib. Mirrors core's link-lib split (`fixpp_core` vs `fixpp_capi`).
- **Alternatives**: one-giant-per-module (rejected: 160-file session relink on every edit) and one-per-feature-label (rejected: link-dep unions bloat, cross-feature pure tests fragment).

## D5 — ODR / symbol-collision policy

- **Decision** (FR-012): On a collision merging two `.cpp` into one TU-set — same-named non-`static` free function/global, or identical `TEST(Suite,Name)` — **first** rename the colliding **helper/global within test source** to keep the test grouped; carve to standalone **only** when the collision is a gtest `Suite.Name` (must not rename — coverage-index keys on it) or is otherwise unresolvable. Record each rename/carve-out.
- **Rationale**: Maximizes grouping yield without touching the identities the audits depend on. Test-local helpers should be `static`/anonymous-namespace anyway; renaming is behavior-neutral.

## D6 — Measurement harness (the 3 deltas)

- **Disk** (every module, FR-008): `../research/test-grouping-baseline/inventory.sh <out.csv>` (parent repo, from `library/` cwd) before/after, diff vs `baseline-2026-07-10.csv` for the module's rows.
- **ctest wall-time** (pilot + session, FR-009/SC-005): `time ctest --preset <p> -L <module>` (or the module's test subset) before vs after; assert ≤10%/preset. Captures the POST_BUILD per-case launch overhead.
- **Incremental relink** (pilot, FR-009): `touch` one grouped test's `.cpp`, time `cmake --build --target <bucket> -j2`; compare to the pre-grouping single-binary relink — validates the bucket blast-radius choice (D4).

## D7 — Rollout order & gating

- **Decision**: One module at a time, descending asan size: `dictionary` (pilot) → `session` → interop → capi → config → sync → transport → core* → otel → wire → tls → log → alloc_guard* → codegen → perf → conformance → integration → tap → service. A module starts only after the previous module's 8-preset build + ctest is green and its delta recorded (FR-010).
  - `core` is already partially grouped (precedent) — pass reconciles remaining standalone-but-groupable tests.
  - `alloc_guard` module is likely **all-standalone** (its whole purpose is process-global allocation assertions) — expect a near-empty grouping, fully dispositioned (FR-011).
- **Rationale**: Pilot on `dictionary` settles D1/D4 empirically before the high-value/high-risk `session` module.

## Open items for `/speckit-plan` re-check → none

All resolved. Constitution re-check after Phase 1 remains PASS (no design artifact introduces a triggered domain).
