---

description: "Task list for 085-fold-flat-cap-loop"
---

# Tasks: Fold the Flat Per-Instance Cap Loop into the Nesting-Aware Traversal

**Feature**: `085-fold-flat-cap-loop` · **Branch**: `085-fold-flat-cap-loop` · **Generated**: 2026-08-03
**Closes**: fixpp#214 (L-063-4 leg 2). **Files (already open)**: fixpp#220.
**Gate A**: converged-on-content, user-signed-off 2026-08-03 — `.specify/decisions/085-fold-flat-cap-loop-gatea.md`.

**Inputs**: [spec.md](./spec.md) · [plan.md](./plan.md) · [research.md](./research.md) · [data-model.md](./data-model.md) · [contracts/group_cap_accounting.md](./contracts/group_cap_accounting.md) · [quickstart.md](./quickstart.md)

## Format: `[ID] [P?] [Story] Description`

- **[P]** — parallelizable (different file, no dependency on an incomplete task)
- **[US1] / [US2] / [US3]** — user-story phases only; Setup / Foundational / Polish carry no story label

## Path Conventions

All paths are **library-root-relative** (`research/G19-fix-fpml-iso20022/library/`), matching `quickstart.md`. The only `src/` file this feature touches is `src/wire/offset_table.cpp`.

## ⚠️ Tests ARE requested for this feature

Article VII §3 is mandatory and is discharged **by compliance**, not substitution: **T006 is a red-first structural pin that MUST be observed RED on the unmodified tree before T007 relocates anything.** This is the artifact carrying VII §3 (`plan.md` Constitution Check, `spec.md` FR-001b, SC-005b). If T006 is skipped or written after T007, VII §3's `PASS (planned)` becomes **false** and Gate B blocks.

## Build constraints (read before running anything)

- **`cmake --preset` is broken in this tree** (missing Conan include) — use `-S`/`-B` and `--test-dir`, per `quickstart.md` §0. Never `cmake --preset`.
- **Build with `-j2`.** Wider parallelism has OOM-killed the session in this tree.
- **Never `ctest -R <gtest-case-name>`** — CTest registers *bucket* names (`wire_pure_tests`, `wire_dict_tests`, `delimiter_census`); a case-name selector matches nothing and **exits 0**. Select by `-L`, then `--gtest_filter` on the bucket binary, and assert the count. This section false-greened four times during Gate A; `quickstart.md` §3a records the sequence.

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Establish the pre-change baseline that later verification compares against. Nothing here modifies behaviour.

- [ ] T001 Re-verify **A-001** against the tree being implemented on, per its own standing obligation: confirm all four steps of the redundancy argument (`research.md` R-1) and the `delim`-source equality premise (**C-1a**) still hold at the current `main`. Run `git rev-parse main` and `git show main:src/wire/offset_table.cpp | sed -n '450p;458p;545p;551p;575p'` (`quickstart.md` §1c). If any step fails, **STOP** — FR-002's no-op claim is void and the feature is re-scoped, not patched.
- [ ] T002 [P] Add the FR-001b build wiring to `tests/wire/CMakeLists.txt`: `target_compile_definitions(wire_pure_tests PRIVATE "FIXPP_SRC_DIR=\"${CMAKE_SOURCE_DIR}/src\"")`, mirroring `tests/dictionary/CMakeLists.txt:178-183` verbatim in mechanism. The bucket does not define it today. **This one line is the only build change this feature makes.**
- [ ] T003 [P] Configure and build the debug and release trees used throughout (`quickstart.md` §0): `cmake -S . -B build/linux-clang-debug` and `cmake -S . -B build/linux-clang-release`, each `--build ... -j2`.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Capture the **pre-change** measurements that cannot be reconstructed after the relocation lands. These block US1 because two of them must be taken on the unmodified tree.

- [ ] T004 Capture the leg-2 benchmark A/B **`main` side** on the unmodified tree, per `quickstart.md` §3a with `TAG=main` → `/tmp/085-bench-main.json`. Fixed at **three runs**, all three reported; the leg-1 observation is the **median of the three per-case medians** (SC-006 — the run count is fixed in advance precisely so the result cannot be obtained by run selection).
- [ ] T005 Record the **pre-change** dictionary-path cap behaviour as the control for T012's ordering precondition: with `consume_group_extent`'s comparison (`:521-524`) deleted **on baseline**, `WireOffsetTable.DoSCapPerInstanceRejectsOversizedSingleInstance` stays **GREEN** (the flat loop still catches the same breach). Restore the source and confirm `git status` clean. Recording this is what makes T012's RED meaningful rather than a false alarm — and it is an independent witness that the redundancy R-1 argues for is real.

**Checkpoint**: baseline captured; the tree is unmodified except T002's build line.

---

## Phase 3: User Story 1 — One traversal owns instance-boundary and cap accounting (Priority: P1) 🎯 MVP

**Goal**: `OffsetTable::group()` derives extent and cap from the single nesting-aware traversal on the dictionary path; the second flat re-walk is gone.

**Independent test**: every returned slice, extent, `group_index`, reserve bound and error disposition is identical to the pre-change engine across all ten shipped dictionaries, and the new structural pin is GREEN.

### Tests for User Story 1 ⚠️ RED-FIRST — T006 BEFORE T007

- [ ] T006 [US1] Author the **red-first structural pin** `WireOffsetTable.FR001_SingleTraversalSourceInspection` in `tests/wire/offset_table_test.cpp` (existing `wire_pure_tests` bucket, **no new executable**), per FR-001b. Slurp `src/wire/offset_table.cpp` via `FIXPP_SRC_DIR` and assert the TU contains **no** `"\n    std::size_t inst_start = first;\n"` (4-space, function-body) and **exactly one** `"\n        std::size_t inst_start = first;\n"` (8-space, `else`-body). Mirror `tests/dictionary/load_any_test.cpp:143-171` in construction — plain `std::string::find`, **no AST**. Its comment block MUST state that it is a source-inspection assertion, not a behaviour test, and **why** behaviour cannot distinguish the two states (a dictionary-path flat re-walk is unreachable-as-an-error either way, so every behavioural test stays green across the change). **Run it and capture the RED output** — measured at `c1564dd2`: 4-space = 1, 8-space = 0. Paste the RED transcript into `.specify/decisions/085-fold-flat-cap-loop-verify.md`. (FR-001b, SC-005b; `[const §VII.3]`.)

### Implementation for User Story 1

- [ ] T007 [US1] Relocate the flat cap block from `group()`'s function body (`:584-595` as-of `c1564dd2`) into the dict-free `else` branch (`:579-582`) in `src/wire/offset_table.cpp`, and delete it from the dictionary path. Walk **C-3's nine-item semantic-preservation checklist** over the diff (`contracts/group_cap_accounting.md`): separate `inst_start` declaration retained, loop bounds, boundary predicate, strict `>`, re-anchoring order, `group_end` assigned before the loop, return value unchanged. Mechanical re-indentation is expected and permitted; a *rewrite* is not. (FR-001, FR-001a, FR-003, C-2, C-3.)
- [ ] T008 [US1] Add the **FR-002 justification comment** on `group()`'s dictionary branch. It MUST state both parts: (i) why the second walk went — the redundancy argument's conclusion, that `consume_group_extent:521-524` already caps over the nesting-aware partition it returns and runs first; and (ii) **C-1**, the standing property — the cap on this path is now enforced *solely* by that check, so any change to that walk must re-verify the cap measures the partition `:527` describes, or re-introduce an independent cap here. It MUST NOT state the C-1a delimiter-source equality as a standing invariant (that premise is discharged, not standing). Anchor by function and role first with line numbers stamped as-of the merge commit (FR-007b). (FR-002, SC-005a, C-1.)
- [ ] T009 [US1] Re-run T006's pin — now **GREEN** — and capture the transcript beside the RED one. (SC-005b.)
- [ ] T010 [US1] SC-001 regression: `ctest --test-dir build/linux-clang-debug -L dictionary` and the seven `TypedReadSplitAgreement.*` cases via `--gtest_filter` on `wire_dict_tests`, plus `DelimiterCensus.RedCountsReconcileWithSpecBaseline`. Assert the counts `quickstart.md` §1a states. **A fixture or baseline edit made to turn these green is a failure of SC-001, not a fix.**
- [ ] T011 [US1] SC-002 corpora: `ctest -L wire` (4/4), `-L capi` (22/22), `-L dictionary` (17/17), each with its count asserted per `quickstart.md` §1b. Identical pass set to `main` required — in particular the ~16 dict-free `OffsetTable{frame, mr}` tests, including the dict-free `group(453)` at `tests/wire/offset_table_test.cpp:150-157`.
- [ ] T012 [US1] **FR-005b dictionary mutation transcript — post-relocation only.** Delete `consume_group_extent`'s per-instance comparison (`:521-524`), run `WireOffsetTable.DoSCapPerInstanceRejectsOversizedSingleInstance`, capture **RED**, restore, re-run green. Record in `.specify/decisions/085-fold-flat-cap-loop-verify.md`. Scope note that MUST accompany the transcript: it guards **C-1's cap-existence half** — enforcement exists and the check is load-bearing on an *unnested* instance — and does **not** test the nesting-aware partition coupling, which stays discharged by source inspection and T008's comment. **If this is run before T007 it stays GREEN** (see T005) — that is a false alarm, not a finding. (SC-003.)

**Checkpoint**: US1 independently complete — dictionary path has one traversal, nothing else moved.

---

## Phase 4: User Story 2 — Non-dictionary callers keep their cap (Priority: P2)

**Goal**: the DoS cap is not silently lost on the dict-free fallback as a side effect of tidying the dictionary path.

**Independent test**: a dict-free `OffsetTable` with a tightened `Config` fails an over-cap frame, succeeds when the cap is raised above it, and the pin is proven RED by mutation.

### Tests for User Story 2

- [ ] T013 [P] [US2] Add `WireOffsetTable.DictFreeDoSCapPerInstanceRejectsOversizedInstance` to `tests/wire/offset_table_test.cpp`: dict-free construction (`OffsetTable{frame, mr, cfg}`) with `tight_cfg{.max_offset_entries = 4096, .max_group_entries_per_instance = 3}`, a frame whose flat segmentation exceeds the cap, asserting `error::wire_group_too_large`. **Both** dict-free construction **and** a tightened `Config` are required — under default `Config` the branch is arithmetically unreachable (`research.md` R-2: both bounds are 4096 and `build()` clamps at `:326`), which is why no such test existed before. (FR-004, SC-004.)
- [ ] T014 [P] [US2] Add the bracketing companion `WireOffsetTable.DictFreeDoSCapPerInstanceAllowsWhenCapRaised` — the **same frame** with `max_group_entries_per_instance` raised above it, asserting success. Brackets the threshold from both sides. (FR-005a(ii), SC-004a.)
- [ ] T015 [US2] **FR-005a(i) dict-free mutation transcript**: delete the relocated cap check inside the `else` branch, run T013's pin, capture **RED**, restore, re-run green. Record in `.specify/decisions/085-fold-flat-cap-loop-verify.md` and cite it from the test's comment block. **If the pin stays GREEN with the check deleted, STOP** — the frame is not reaching the dict-free path or `Config` was not tightened. (FR-005, FR-005a, SC-004a.)

**Checkpoint**: US2 independently complete — the dict-free cap is enforced and proven load-bearing.

---

## Phase 5: User Story 3 — The remaining flatness is recorded honestly (Priority: P3)

**Goal**: an operator reading `spec/behaviors-and-limitations.md` sees leg 2 delivered, leg 1's descope evidence intact, and both surviving flat rules named with the reason each was kept.

**Independent test**: inspection of the updated L-063-4 row against the delivered source.

- [ ] T016 [US3] Update **L-063-4** in `spec/behaviors-and-limitations.md` (row at `:1701-1709`): record **leg 2 as DELIVERED** by this feature, preserve leg 1's descope-with-evidence disposition from 083 **unweakened**, and do **not** reopen fixpp#180. (FR-006, SC-009.)
- [ ] T017 [US3] In that same row, name the flat instance-boundary rules that **survive on purpose** — the dict-free cap check in `OffsetTable::group()`, and the instance splitter in **`OffsetTable::group_slices_status()`** (`:712-733`; `group_slices()` at `:639-641` is only the delegating wrapper) — each with the reason it was not changed, so "leg 2 delivered" cannot be read as "no flat rules remain". State that the two do **not** merely share a flat *shape*: `group()`/`consume_group_extent` derive `delim` from the **wire** (`:551`, `:458`) while the splitter derives it from the **per-context dictionary store** (`:704-711`, 083's change across 330 contexts). Anchor every reference by **function and role first**, line numbers appended and stamped as-of the merge commit; leave the row's historical brackets **byte-unchanged**. (FR-007, FR-007a, FR-007b, SC-009.)
- [ ] T018 [US3] Add the limitation row for the **dict-free trailing-field false positive** citing **fixpp#220**, and state its reachability precisely: **unreachable under default configuration** (`default_max_offset_entries` == `default_max_group_entries_per_instance` == 4096, `offset_table.hpp:27-28`; table clamped at `offset_table.cpp:326`), reachable only where a caller sets `max_group_entries_per_instance < max_offset_entries - 1`. A row presenting it as a default-path defect **fails** SC-010 for overstating, exactly as one omitting the limitation fails for understating. (FR-003a, SC-010.)

**Checkpoint**: US3 independently complete — the record is honest in both directions.

---

## Phase 6: Polish & Cross-Cutting Concerns

- [ ] T019 [P] Repair the stale coverage waiver at `tests/wire/offset_table_error_path_test.cpp:10-14`. It is wrong **two** ways: its line numbers are from a pre-063 revision of a file now 700+ lines, and its claim that `group()`'s `err_group_too_large` is "provably unreachable" is **false for `:577`** (covered since 063 shipped `consume_group_extent`'s cap) and false for the relocated branch too once T013 lands. Repair the claim, do not merely re-point the numbers; keep the three still-valid entries. (`research.md` R-3.)
- [ ] T020 Benchmark, both legs, per `quickstart.md` §3a. Leg 1: three runs against `bench/baselines/wire/typed_read_group_bench.json`, all three reported, observation = median of the three per-case medians. Leg 2: `TAG=branch` → `/tmp/085-bench-branch.json`, compared by name against T004's `main` file — **distinct paths, never one shared file**. Put both sets of numbers in the PR body. A null delta is a **pass** (A-004); do not update `bench/baselines/` — this is not an intentional perf change. (SC-006, `[const §VIII.2/§VIII.3]`.)
- [ ] T021 [P] Confirm SC-007: zero change to exported symbols, public headers, error enum values and the C-ABI version. `include/fixpp/wire/offset_table.hpp` must be **untouched**; `git diff main -- include/ src/capi/` empty.
- [ ] T022 Run `/speckit-verify` — it owns the sanitizer matrix (ASan/UBSan/TSan), static analysis (`clang-tidy`/`clang-format`/`cppcheck`/`iwyu`/`check_layers.py`) and the Article IX §1 coverage measurement with fresh per-binary profraw, none of which is runnable from the quickstart in this tree. Coverage is **expected to improve**: the previously dead flat return becomes covered for the first time (`research.md` R-3). Record the measured percentages in `.specify/decisions/085-fold-flat-cap-loop-verify.md`. (SC-008, `[const §IX]`.)
- [ ] T023 Walk `quickstart.md` end to end as written, including the failure paths. Every selection asserts a count; every mutation block halts before its guarded command on a wrong count. Confirm no command selects zero tests and reports success.

### Mandatory close-out tasks (ALWAYS emit — Gate-B preconditions, Article XVII §8)

- [ ] T024 [P] **Catalogue close-out**: flip every feature-owned OFFICIAL row in `spec/feature-catalogue.md` to `done` (with the PR / evidence ref) AND add/update its matching `spec/coverage-index.md` entry. Also re-point the submodule `CLAUDE.md` open-issue count and in-flight pointer: **#214 closes with this feature**, **#220 opens**, so the count moves from the stale "6" to the true figure (9 at 2026-08-03, counting #217/#218 filed by 084).
- [ ] T025 **Feature-completeness audit (FINAL TASK)**: assert against the merged tree that (i) every `tasks.md` row is `[X]` or carries an explicit waiver rationale; (ii) every FR-001…FR-009 (including FR-001a/001b, FR-003a, FR-005a/005b, FR-007a/007b) and every SC-001…SC-010 (including SC-004a, SC-005a/005b) maps to a landed test **and** a landed implementation; (iii) every feature-owned OFFICIAL catalogue row is `done` with a matching `coverage-index.md` entry. Record the verdict (100% or fully-waived) in `.specify/decisions/085-fold-flat-cap-loop-verify.md` under `## Completeness`. **`/gate-b` pre-flight 4d HARD-BLOCKS without this record.**

---

## Dependencies & Execution Order

### Phase dependencies

- **Setup (T001–T003)**: no dependencies. **T001 is a stop-gate** — if A-001 fails to re-verify, the feature is re-scoped, not patched.
- **Foundational (T004–T005)**: depends on Setup; **BLOCKS US1** because both measurements must be taken on the *unmodified* tree and cannot be reconstructed afterwards.
- **US1 (T006–T012)**: depends on Foundational.
- **US2 (T013–T015)**: depends on Foundational. Its pin can be *authored* in parallel with US1, but T015's mutation must run against the **relocated** code, so it lands after T007.
- **US3 (T016–T018)**: depends on US1 landing (the row's as-of line numbers are stamped to the delivered tree).
- **Polish (T019–T025)**: depends on all three stories.

### The one ordering constraint that is easy to get wrong

**T012 must run after T007.** Deleting `consume_group_extent:521-524` on baseline leaves the dictionary pin **GREEN**, because the flat loop at `:584-595` still catches the same breach (T005 records this). Only after the relocation is that check the dictionary path's sole cap. A T012 run before T007 produces a green result that looks like a failed mutation and is not.

### Within US1

**T006 (RED) → T007 → T008 → T009 (GREEN).** This ordering *is* Article VII §3 compliance for this feature; inverting it forfeits the gate.

### Parallel opportunities

- T002 ∥ T003 (Setup)
- T013 ∥ T014 (US2 test authoring)
- T019 ∥ T021 ∥ T024 (Polish, distinct files)
- US3's documentation work (T016–T018) can proceed alongside US2 once US1 has landed

## Implementation strategy

**MVP = Phase 1 + Phase 2 + US1.** That delivers the issue's stated acceptance: the dictionary path derives cap accounting from one nesting-aware traversal. US2 is a *preservation* increment (the behaviour already exists; it must not be lost), and US3 is the record. Ship in order — but note US2 is not optional for merge: without it the relocation is unpinned on the one path where the check is now load-bearing.

## Task count

**25 tasks** — Setup 3, Foundational 2, US1 7, US2 3, US3 3, Polish 7 (including the 2 mandatory close-out tasks).

Format validated: every row is `- [ ]` + `TNNN` + optional `[P]` + story label on story phases only + a concrete file path or command.
