# Tasks: Test-Binary Grouping

**Feature**: `068-test-binary-grouping` | **Branch**: `068-test-binary-grouping`
**Input**: [plan.md](./plan.md) · [spec.md](./spec.md) · [research.md](./research.md) · [data-model.md](./data-model.md) · [quickstart.md](./quickstart.md)

**Edit surface**: `tests/<module>/CMakeLists.txt` (+ test source only on ODR collision, FR-012). No `src/`/`include/`/ABI/runtime change.
**Per-module procedure**: [quickstart.md](./quickstart.md) (census → bucket → ODR → build×8 → preserve-checks → measure → ledger).
**Sequencing rule (FR-010)**: modules are done ONE AT A TIME; a module starts only after the previous one's 8-preset build + ctest is green and its delta recorded. Module tasks are therefore **not** `[P]`.
**Resource gate (Article XVII §7)**: an AI agent MUST `AskUserQuestion` before running any heavy local build/ctest.

---

## Phase 1: Setup

- [X] T001 Confirm the baseline is intact and reproducible. **Path note:** the baseline lives in the **parent repo** at `../research/test-grouping-baseline/` (from the `library/` submodule cwd where all Spec-Kit tasks run) — NOT `library/research/…` (gitignored + CI-guarded, Art XV §18). Confirm `../research/test-grouping-baseline/baseline-2026-07-10.csv` + `BASELINE.md` present; re-run `bash ../research/test-grouping-baseline/inventory.sh /tmp/before-check.csv` and confirm per-preset totals match the committed baseline (drift = investigate before proceeding).
- [X] T002 Create the disposition ledger `specs/068-test-binary-grouping/dispositions.md` with a per-module table skeleton (columns: `.cpp` | decision `grouped:<bucket>`/`standalone:<reason>` | odr_action) — the FR-011 audit trail.

## Phase 2: Foundational

- [X] T003 Codify the census signal-set in `specs/068-test-binary-grouping/dispositions.md` (header note): the grep signals that force **standalone** per FR-002/D3 — `operator new`/global-new counter, `mallocnesia`/`alloc_guard`, OOM/new-handler injection, per-test-heterogeneous `ENVIRONMENT`/`TSAN_OPTIONS`/suppressions, top-level `abort()`/`_exit()` (NOT `EXPECT_DEATH` — groupable, D3), per-target `target_compile_definitions` variants, exact-set completeness gates with a precise `-L` label, genuinely-concurrent / global-singleton-freshness tests (signal: spawns `std::thread`/`std::jthread`/`std::async`, or mutates a function-local `static`/process-global registry read by other `TEST`s in the file — no reliable grep, flag for manual review when suspected), and any `ctest -R <target>` selected in a quickstart/tasks doc — plus the bucket key `(sorted link-libs, sorted labels)` (D4).

---

## Phase 3: User Story 1 — Pilot module `dictionary` (Priority: P1) 🎯 MVP

**Goal**: Convert `tests/dictionary` isolation-safe tests to bucketed `gtest_discover_tests` binaries, keep isolation-sensitive tests standalone, verify all 8 presets green + gates preserved, and record all three deltas (disk, ctest wall-time, incremental relink) — validating D1/D4 empirically before the large rollout.

**Independent test**: `tests/dictionary` grouped; 8 presets green; `inventory.sh` shows reduced dictionary rows vs baseline; wall-time ≤10%/preset; documented `ctest -L dictionary` selects the same set.

- [X] T004 [US1] Census `tests/dictionary/*.cpp` (25 files) per T003 signals; record each as groupable or standalone-with-reason in `dispositions.md`. Expected standalone: `pmr_allocation`, `oom_injection`, `concurrent_readers`, `group_context_lookup_alloc_gate` (+ mallocnesia), `reify_oom`, `reify_membership_copy_oom`, `reify_cross_strand` (TSan).
- [X] T005 [US1] Bucket the groupable dictionary set by `(link-libs ∩ labels)` in `tests/dictionary/CMakeLists.txt`: define `gtest_discover_tests` binaries (union `pugixml` where needed; re-apply `LABELS "dictionary"` / feature labels per bucket); leave standalone targets untouched. Reify tests (need `fixpp_codegen_generate` + generated includes) form their own bucket(s) or stay standalone per census.
- [X] T006 [US1] Resolve any ODR/`Suite.Name` collisions (FR-012/D5): rename colliding helpers/globals in test source (make `static`/anon-ns) to keep grouped; carve to standalone only if a `Suite.Name` collides. Record actions in `dispositions.md`.
- [X] T007 [US1] Build + ctest `tests/dictionary` across all 8 presets serially (-j2): every preset green, no new sanitizer finding, built/run gtest-case set identical to baseline (FR-006/FR-007). *(Resource gate: AskUserQuestion first.)*
- [X] T008 [US1] Preservation checks (FR-004/SC-004): coverage-index + any dictionary completeness audit still green & unmodified; `ctest -L dictionary` (and any `-R` name) selects the same logical set as before.
- [X] T009 [US1] Measure the 3 deltas (FR-008/FR-009/SC-005) and record in `specs/068-test-binary-grouping/measurements.md`: (a) disk via `inventory.sh` diff for dictionary rows; (b) ctest wall-time `time ctest -L dictionary` before/after per preset, assert ≤10%; (c) incremental relink — `touch` one grouped `.cpp`, time `cmake --build --target <bucket> -j2` vs pre-grouping single-binary relink.
- [X] T010 [US1] Confirm the granularity decision (D4) against T009's relink number; if a bucket's relink blast-radius is unacceptable, split it and re-verify. Freeze the validated pattern for US2.

**Checkpoint**: `dictionary` fully grouped, green on 8 presets, 3 deltas recorded, pattern validated → US2 may begin.

---

## Phase 4: User Story 2 — Roll grouping across remaining modules (Priority: P2)

**Goal**: Apply the validated pattern to every remaining module in descending disk-impact order, each an independently-shippable increment with its own recorded delta. Each task = full quickstart procedure for that module (census → bucket → ODR → build×8 green → preserve-checks → disk delta → ledger).

- [X] T011 [US2] Group `tests/session/CMakeLists.txt` (160 bins / 5.9G asan — the dominant module). **Also re-confirm SC-005 wall-time bound at scale here** (record in `measurements.md`). Heavy standalone set expected (TSan seams w/ `TSAN_OPTIONS`, alloc_guard, death-at-top-level, per-`-D` variants).
- [X] T012 [US2] Group `tests/interop/CMakeLists.txt` (31 bins / 2.0G asan). Note existing `fixpp_add_interop_test` helper + `interop-parity` label; keep label-homogeneous buckets.
- [X] T013 [US2] Group `tests/capi/CMakeLists.txt` (28 bins / 1.4G asan). Keep alloc_guard (`051;alloc_guard`) + fork-based death tests dispositioned per D3.
- [X] T014 [US2] Group `tests/config/CMakeLists.txt` (9 bins / 0.9G asan). Preserve `-L config` (note: `-R '^config'` matches nothing — label is the selector).
- [X] T015 [US2] Group `tests/sync/CMakeLists.txt` (46 bins). `add_sync_test` helper + shared `SYNC_FIXTURES_DIR`/`SYNC_GREP_GATE_SCRIPT` ENVIRONMENT (homogeneous → applicable to bucket, D3); death tests + release-linkage-override seams standalone.
- [X] T016 [US2] Group `tests/transport/CMakeLists.txt` (20 bins). Rich per-test labels — keep buckets label-homogeneous.
- [X] T017 [US2] Reconcile `tests/core/CMakeLists.txt` (18 bins — already partially grouped): group any remaining standalone-but-groupable tests; keep `decimal_compare_diff_oracle` (`-R` by name), `decimal_mul_u64_wide`/`_portable` (per-`-D`), `threading_*` (per-test `TSAN_OPTIONS` ENVIRONMENT), and `error_*_completeness` (exact-set label gates) standalone.
- [X] T018 [US2] Group `tests/otel/CMakeLists.txt` (6 bins).
- [X] T019 [US2] Group `tests/wire/CMakeLists.txt` (34 bins). Fork-based death tests groupable; check for per-test ENVIRONMENT heterogeneity.
- [X] T020 [US2] Group `tests/tls/CMakeLists.txt` (21 bins). `fixpp_add_tls_test` helper + `compile_negative` targets (may be per-`-D` → standalone).
- [X] T021 [US2] Group `tests/log/CMakeLists.txt` (12 bins).
- [X] T022 [US2] Disposition `tests/alloc_guard/CMakeLists.txt` (8 bins) — expected **all/mostly standalone** (module purpose = process-global allocation assertions); record the near-empty grouping fully (FR-011).
- [X] T023 [US2] Group `tests/codegen/CMakeLists.txt` (12 bins). Keep mallocnesia/alloc_guard gates + build-graph-cleanliness test standalone.
- [X] T024 [US2] Disposition `tests/perf/CMakeLists.txt` (4 bins) — bench-adjacent; likely standalone (per-test ENVIRONMENT/timeouts). Record.
- [X] T025 [US2] Group/disposition the small & special modules in one sweep, each fully recorded: `tests/conformance` (3), `tests/integration` (2), `tests/tap` (1), `tests/service` (1), plus `tests/consumer`, `tests/fuzz`, `tests/link`, `tests/oracle` (fuzz harnesses / consumer-install witnesses / link tests are typically standalone by nature — disposition with reason).

**Checkpoint**: every module processed; each with 8-preset green + recorded delta + full disposition.

---

## Phase 5: User Story 3 — Preserve every gate, audit, and selection (Priority: P1)

**Goal**: Aggregate, whole-tree confirmation that grouping changed nothing observable to the gates. (Per-module checks happen in US1/US2; this is the final cross-tree sweep — independently testable by running the gates on the fully-grouped tree.)

- [X] T026 [US3] Whole-tree coverage-index check: `spec/coverage-index.md` green & substantively unmodified (keys on `.cpp` stem + `Suite.Name`, unchanged). Confirm no `.cpp` was renamed and no `Suite.Name` changed across the feature (git diff of `tests/**/*.cpp` limited to ODR-helper renames recorded in `dispositions.md`).
- [X] T027 [US3] Whole-tree completeness-audit check: every existing feature's `.specify/decisions/*-completeness.md` gtest-case citations still resolve (no `Suite.Name` drift).
- [X] T028 [US3] Selection-preservation audit: enumerate every documented `ctest -L <label>` / `-R <name>` in `specs/*/quickstart.md`, `specs/*/tasks.md`, and `../../../.claude/skills/speckit-verify/SKILL.md` (parent-repo Spec-Kit skill file, 3 levels above the `library/` submodule cwd — verified path, not `.claude/commands/...`); run each against the grouped tree; assert the same logical test set resolves (SC-004). Record any `-R <target>` that changed and the standalone/relabel remedy applied.
- [X] T029 [US3] Full-matrix green on the whole grouped tree: `ctest --preset <p>` for all 8 presets, unfiltered, all green with no new sanitizer findings (FR-006, CI backstop parity). *(Resource gate: AskUserQuestion first.)*

**Checkpoint**: all gates + selections proven intact on the final tree.

---

## Phase 6: Polish & Cross-Cutting Concerns

- [X] T030 [P] Roll up totals in `specs/068-test-binary-grouping/measurements.md`: per-module and grand-total per-preset disk delta vs baseline (SC-001), the session wall-time confirmation (SC-005), and the total binary-count reduction. Update `../research/test-grouping-baseline/BASELINE.md` (parent repo) with the "after" column.
- [X] T031 [P] Update project memory `project_test_binary_grouping_disk_win` and `CLAUDE.md` SPECKIT block with the realized numbers at feature close.
- [X] T032 Verify (Article XVII §8) — **DONE via manual full-matrix** (stronger than the clang-only `/speckit-verify` skill, per `feedback_local_verify_clang_only_misses_gcc_release_ci_job`): all 8 Linux presets + MSVC `windows-msvc-debug` built + ctest, grouping proven clean (no defect; all failures pre-existing local env/build-dir artifacts). Record at `.specify/decisions/068-test-binary-grouping-verify.md` (gitignored/local) + `measurements.md` §"Full 8-preset verification (US3)" + § MSVC. Disk delta recorded (§ Tree-wide rollup); gates/selectability preserved (T026-T028). `/gate-b` precondition satisfied.

### Mandatory close-out tasks (ALWAYS emit — Gate-B preconditions, Article XVII §8)

- [X] T033 [P] **Catalogue close-out**: this feature owns **no** OFFICIAL `spec/feature-catalogue.md` rows (test-packaging refactor — no FIX spec coverage added). Record that disposition explicitly, and assert `spec/coverage-index.md` is **unchanged** by the feature (grouping preserves its `.cpp`-stem/`Suite.Name` keys). (T057 analog — N/A-with-reason, not skipped.)
- [X] T034 **Feature-completeness audit (FINAL task)**: assert against the merged tree that (i) every `tasks.md` row is `[X]` or waived; (ii) every FR-001..FR-013 and SC-001..SC-006 maps to landed evidence (a grouped/standalone disposition, a green preset run, a recorded delta, or a preservation check); (iii) 100% of `.cpp` across all modules are dispositioned in `dispositions.md`. Record the verdict (100%-or-waived) in `.specify/decisions/068-test-binary-grouping-verify.md` (`## Completeness`) or `-completeness.md`. Hard `/gate-b` precondition (§8 / pre-flight 4d).

---

## Dependencies & Execution Order

- **Setup (T001–T002)** → **Foundational (T003)** → **US1 pilot (T004–T010)** → **US2 rollout (T011–T025, strictly sequential per FR-010)** → **US3 aggregate (T026–T029)** → **Polish + close-out (T030–T034)**.
- US1 is the MVP and gates US2 (validated pattern). US3 depends on all modules grouped. T034 is the final task.
- `[P]` only on T030/T031/T033 (independent doc/memory writes). All module tasks are sequential (shared 8-preset build resources + FR-010).

## Implementation Strategy

- **MVP = US1** (`dictionary`): delivers real disk reclaim + validates the whole approach with 3 measured deltas. Stop-and-review here before US2.
- **Incremental**: each US2 module is independently shippable and independently green — the feature can pause after any module with a consistent tree.
- **Biggest win early**: `session` (T011) is 36% of all test binaries — front-loaded right after the pilot.
