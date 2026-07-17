---

description: "Task list for 078-precompiled-builder-libs"
---

# Tasks: Precompiled per-version builder/validator libraries

**Input**: Design documents from `specs/078-precompiled-builder-libs/`
**Prerequisites**: plan.md, spec.md, research.md (R1–R10), data-model.md (Entities 1,1b,2,3,4,5,6,7,8 + cross-entity invariants), contracts/{include-layout,cmake-targets,completeness-and-golden}.md, quickstart.md

**Tests**: REQUIRED (Constitution Article VII — TDD). This is a codegen-layout
restructure of 077's typed builder/validator tier; every FR/SC carries a
red-green witness. Write tests FIRST and confirm they FAIL before the emitter/CMake
change that makes them pass.

**Organization**: Grouped by user story. All five stories (US1–US5) depend on a
single shared **Foundational** restructure (Phase 2). US1/US2 are P1; US3/US4/US5
are P2.

**Scope guard (FR-011)**: zero `src/`/`capi/`/`bindings/`/`python` change; C-ABI
frozen 1.5.0. All changes are in the codegen host tool
(`tools/codegen/fixpp-codegen/`), CMake wiring (`cmake/`, `CMakeLists.txt`,
`CMakePresets.json`), tests (`tests/{codegen,session}/`), the golden set, the
bench harness, and docs.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: parallel-safe (different files, no incomplete dependency)
- **[Story]**: US1–US5 (user-story phases only; never in Setup/Foundational/Polish)
- Every task names an exact file path (library-submodule-relative)

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Confirm working context; no new project scaffolding (codegen-tool +
CMake + tests only).

- [ ] T001 Confirm branch `078-precompiled-builder-libs` is checked out (`git rev-parse --abbrev-ref HEAD`) and record the scope invariant in `specs/078-precompiled-builder-libs/plan.md` "Structure Decision" — no new scaffolding; the change surface is `tools/codegen/fixpp-codegen/` + `cmake/` + `tests/{codegen,session}/` + goldens + docs, with **zero** `src/`/`capi/`/`bindings/`/`python` edits (FR-011).

**Checkpoint**: Context confirmed — Foundational phase can begin.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: The shared emitter split + precompiled-library wiring + golden-set
regeneration + full migration-census disposition that ALL five user stories build
on.

**⚠️ CRITICAL**: No user-story work (Phase 3+) can begin until this phase is
complete. Within the phase, **T002 (the R2a probe) is a hard Phase-0 prerequisite
that blocks the emitter split (T003)** — it settles the validator-traits placement
(R2) and the disjoint-object property (R1) empirically before the emitter commits
to the layout.

- [ ] T002 **[BLOCKING — Phase-0 prerequisite]** Write the hand-written ~30-line R2a ODR / builder⟂validator (SC-003) probe (research R2a) proving the split layout's three legs: (i) a force-inlined validator sharing a group-plan with a *linked* validator collapses to one `inline` trait definition (no duplicate-symbol); (ii) a builder-only binary carries zero `validate_`/`writer_traits` symbols (`nm`); (iii) a consumer linking BOTH builder+validator libs raises no duplicate-symbol for `build_`/`validate_` (disjoint objects). The probe hand-writes a minimal mock of the intended split to settle R2 (traits home) + R1 (disjoint objects) BEFORE the emitter work; if any leg fails, re-decide object/trait placement first. Seeds the SC-002/SC-003 `nm` witnesses and the FR-007 mixing tests. File: `tests/codegen/test_078_odr_sc003_probe.cpp` (+ wiring in `tests/codegen/CMakeLists.txt`).
- [ ] T003 Split the emitter output (data-model Entities 1–5): in `tools/codegen/fixpp-codegen/emit_builders.cpp`, replace the single 4-pass `Builders.hpp` string with a per-version file set — DATA-ONLY `groups.hpp` (`G_<no_tag>Args`, no traits — Entity 1/FR-012, `#pragma once`), `validators/traits.hpp` (shared group-plan `inline writer_traits` + helpers, `#include "../groups.hpp"` — Entity 1b/R2), slim `messages/<Msg>.hpp` (Args + `extern` build_/validate_ decls + macro-branch to `.inl` — Entity 2/FR-001), `messages/<Msg>.builder.inl` + `<Msg>.validator.inl` (inline bodies; validator `.inl` carries per-message top-level traits + `#include "../validators/traits.hpp"` — Entity 3/FR-006), disjoint `messages/<Msg>.builder.cpp` + `<Msg>.validator.cpp` (external-linkage defs; per-message top-level traits on the validator side — Entity 4/FR-002/FR-003), and `all.hpp` (aggregator hosting `builder_registry` — Entity 5/FR-008). Preserve the message / interned-plan / ordinal ordering for determinism (`:824-912`).
- [ ] T004 Drop the monolithic `Builders.hpp` write path and emit the multi-file set: in `tools/codegen/fixpp-codegen/main.cpp` replace the per-version `write_file("Builders.hpp")` (`:118`) with `write_file` calls over the emitted set (`groups.hpp`, `validators/traits.hpp`, per-message five-file set, `all.hpp`); vt11/v42 emit none. Extend emit signatures in `tools/codegen/fixpp-codegen/emit.hpp` as needed (IR unchanged — order intact).
- [ ] T005 Enforce the FR-005 / SC-003 emitter invariant in `tools/codegen/fixpp-codegen/emit_builders.cpp`: the `build_` include graph (slim `<Msg>.hpp` builder branch, `.builder.inl`, `.builder.cpp`) and every `.builder.cpp` reference **no** validator symbol (no `writer_traits`, no `validate_`, no `validators/traits.hpp` include) — the builder surface includes only trait-free `groups.hpp` (cross-entity invariant 1). Realize as an emitter-side assertion/structure so a regression is caught at generation.
- [ ] T006 Wire the precompiled libraries in `cmake/Codegen.cmake`: emit the file set into `_codegen/include/fixpp/<ns>/`; add 6 always-built STATIC targets — `fixpp_builders_<ver>` / alias `fixpp::builders::<ver>` (sources = `messages/<Msg>.builder.cpp`) and `fixpp_validators_<ver>` / alias `fixpp::validators::<ver>` (disjoint sources = `messages/<Msg>.validator.cpp` + traits) for v44/v50sp2/vlatest (Entity 8/FR-002/FR-003/FR-004). Build-tree + in-tree only — NO `install(TARGETS)`/`install(EXPORT)` and do NOT install the slim headers for external linking (Gate A round 1 decision, R3).
- [ ] T007 Regenerate the golden SET per version under `specs/078-precompiled-builder-libs/contracts/golden/` — `groups.hpp` + `validators/traits.hpp` + `messages/<Msg>.{hpp,builder.inl,validator.inl,builder.cpp,validator.cpp}` + `all.hpp` per version — replacing the 077 single-file monolith goldens (Entity 7/FR-010). Explicit regeneration task, not a side effect.
- [ ] T008 Rewrite `tests/codegen/determinism_test.cpp` to assert the emitted file **SET** (R6/FR-010/SC-007): (1) run tool twice → whole set byte-identical; (2) stable file NAME-set + COUNT; (3) generated set == golden set. This task also dispositions census row `determinism_test.cpp:363-368,559-568,763-774,784,807-879` (existence / OFF-path presence-absence / per-version golden-diff → the file-SET forms; OFF-path → `<ver>/all.hpp` or `<ver>/` dir presence/absence).

### Migration census — ONE disposition task per `#include`-independent monolith-name gate (plan.md Gate A round-2 census)

> FR-008 deletes `Builders.hpp`; each of these text-parse / marker / existence gates false-reds or inverts post-split. Two re-point targets: `builder_registry` → `all.hpp`; the `G_<no_tag>Args` group structs → `groups.hpp` (data-only). Different files → all [P] within this batch (they run after T003–T007).

- [ ] T009 [P] Re-point the build-graph markers in `cmake/Codegen.cmake` (`:267,273,285` `_v{44,50sp2,vlatest}_builders_marker` and `:623` `FIXPP_CODEGEN_BUILDERS_MARKER_v44` written into `graph_props.cmake`) from `<ver>/Builders.hpp` → `<ver>/all.hpp` (the new per-version emission sentinel).
- [ ] T010 [P] Re-point the build-graph existence assertion in `tests/codegen/codegen_build_graph_test.cmake:119-125` (`if(EXISTS marker)` → `_assert`) to `v44/all.hpp` via the re-pointed marker (else hard-RED post-split).
- [ ] T011 [P] Re-point `codegen-source-staleness-check` in `tests/codegen/codegen_source_staleness_test.cmake:96-108` (greps `FIXPP_BUILDERS_HEADER` for the `builder_registry` array) so it resolves the `builder_registry` array against `v44/all.hpp` (its new home, Entity 5). NOTE: the `FIXPP_BUILDERS_HEADER` def itself at `tests/codegen/CMakeLists.txt:437` is re-pointed by T018 (single owner of that file) — this task only edits `codegen_source_staleness_test.cmake`.
- [ ] T012 [P] Re-point the shared parse helper in `tests/codegen/builder_completeness_common.hpp:168-177`: `parse_builder_registry` → the `-D`-fed `all.hpp` path for the `builder_registry` array text; `parse_build_fn_identifiers` → the per-message `.builder.{inl,cpp}` set (`all.hpp` holds only `#include`s + the registry array, no literal `build_<Msg>(` bodies).
- [ ] T013 [P] Re-point `tests/codegen/builder_completeness_mutation_witness_test.cpp:146-147`: `ASSERT_TRUE(fs::exists(.../Builders.hpp))` → `v44/all.hpp`; registry text-parse → `all.hpp`; build-fn identifiers → the per-message set.
- [ ] T014 [P] Re-point `tests/codegen/test_077_builder_no_emit.cpp:51-72`: present versions (v44/v50sp2/vlatest) → assert `<ver>/all.hpp` exists; absent versions (vt11/v42) → assert no `<ver>/all.hpp`/messages dir emitted (no-emit semantics preserved).
- [ ] T015 [P] Re-point `tests/codegen/vlatest_compile_smoke_test.cpp:82-91`: existence → `vlatest/all.hpp` + the per-message set; retire the obsolete ~monolith `EXPECT_LT(size, 100MB)` size band (no monolith to bound) or retarget it to the split artifact set.
- [ ] T016 [P] Re-point `tests/codegen/test_077_builder_dedup_count.cpp:61,89-110` (text-parse counting `struct G_…Args` ==576 + `namespace fixpp::vlatest::groups` + size band) to **`vlatest/groups.hpp`** (the `G_…Args` structs' new data-only home, NOT `all.hpp`); size band retargets to `groups.hpp`.
- [ ] T017 [P] Re-point `tests/codegen/test_077_v42_vt11_completeness_and_c4.cpp:72,94,102`: v42/vt11 existence `EXPECT_FALSE` → `<ver>/all.hpp` absence; C4 structural-key text-parse of group structs → **`v50sp2/groups.hpp`** (group structs' new home, NOT `all.hpp`).
- [ ] T018 [P] Re-point the `-D..._BUILDERS_HPP` path defs in `tests/codegen/CMakeLists.txt:158,161-164,234,269,297,324-326,400,437`: each header-path def → `all.hpp` (registry/build-fn/existence) or `groups.hpp` (`G_`-struct text-parse), or drop it where the TU now relinks the libs.
- [ ] T019 Relink the heavy + 067/069/077 test TUs against the prebuilt libs: in `tests/codegen/CMakeLists.txt` and `tests/session/CMakeLists.txt`, and the source TUs, drop `#include <fixpp/<ns>/Builders.hpp>` + `-D..._BUILDERS_HPP` and instead `#include <fixpp/<ns>/all.hpp>` + `target_link_libraries(... PRIVATE fixpp::builders::<ver> fixpp::validators::<ver>)`. Full `#include`-er surface (re-measured this session): 067×5 (`tests/session/test_067_*.cpp`), 069×4 (`tests/session/test_069_{all_families_roundtrip,family_exemplar_golden,mode_count,us1_smoke}*.cpp`), 077×6 (`tests/{codegen,session}/test_077_*_builder_{completeness,roundtrip}.cpp`) — ZERO in `examples/`. Completeness TUs additionally `add_dependencies(... fixpp_builders_<ver> fixpp_validators_<ver>)` — completeness now proves LINK, compile-proof at the lib target (R7, Entity 6).

**Checkpoint**: Foundation ready — the six libs build, the split emits
deterministically against a regenerated golden set, and every monolith-name gate
is re-pointed. User stories US1–US5 can now proceed (in parallel if staffed).

---

## Phase 3: User Story 1 - Consumer compiles against a slim header and links a prebuilt builder library (Priority: P1) 🎯 MVP

**Goal**: A consumer includes a slim declaration header and links `fixpp_builders_<ver>`, paying compile time/size only for what it links.

**Independent Test**: A small consumer TU includes only the slim per-version builder header, calls one `build_<Msg>`, links the prebuilt builder lib — compiles at an order-of-magnitude lower peak RSS than the monolith, links, and produces byte-identical wire output.

> Write these tests FIRST and confirm they FAIL before relying on the Foundational split.

- [ ] T020 [P] [US1] Slim-header compile + link witness: a TU that `#include <fixpp/vlatest/messages/NewOrderSingle.hpp>`, calls one `build_NewOrderSingle`, links `fixpp::builders::vlatest` — asserts it compiles + links and the object contains machine code only for the called builder (US1 AC1/AC2). File: `tests/codegen/test_078_slim_compile_us1.cpp` (+ `tests/codegen/CMakeLists.txt`).
- [ ] T021 [P] [US1] Byte-identical wire round-trip via the **linked** lib for representative messages across v44/v50sp2/vlatest — asserts wire bytes equal the 077 baseline/golden (SC-004 builder, link mode; FR-009). File: `tests/session/test_078_builder_roundtrip_linked_us1.cpp` (+ CMake).
- [ ] T022 [P] [US1] Two-version no-symbol-collision: a consumer linking BOTH `fixpp::builders::v44` and `fixpp::builders::vlatest`, calling one `build_<Msg>` from each — resolves each from its own lib with no collision (US1 AC3). File: `tests/codegen/test_078_two_version_link_us1.cpp` (+ CMake).
- [ ] T023 [US1] Repoint `bench/codegen/vlatest_builders_compile_bench/compile_bench.sh` (currently `#include`s the monolith, `:66`) to the slim header, making it the SC-001 slim-vs-monolith compile-RSS harness (R9); run it and record peak RSS/wall in `.specify/decisions/078-precompiled-builder-libs-verify.md` compile-bench decision record (003/T046 convention — NOT Article VIII). Confirms SC-001 (peak RSS ≥ an order of magnitude below the ~3.6 GiB monolith baseline).

**Checkpoint**: US1 (the MVP) is independently testable — cheap slim compile + link + byte-identical wire.

---

## Phase 4: User Story 2 - Send-only consumer skips validator code entirely by not linking it (Priority: P1)

**Goal**: A send-only consumer links only `fixpp_builders_<ver>` and carries zero validator code; a consumer that links `fixpp_validators_<ver>` gets the outbound required-field check.

**Independent Test**: A builder-only binary contains zero `validate_<Msg>` machine code; a both-linked binary resolves and runs a `validate_<Msg>` correctly.

> Write these tests FIRST and confirm they FAIL before relying on the Foundational split.

- [ ] T024 [P] [US2] `nm` builder-only witness: a binary linking ONLY `fixpp::builders::vlatest` (calls one `build_`) → `nm --defined-only` shows **zero** `validate_`/`writer_traits` symbols and no link dependency on the validator lib (SC-003; FR-005; builder⟂validator cross-entity invariant 1). Seeded from T002 leg (ii). File: `tests/codegen/test_078_nm_builder_only_us2.cpp` (+ CMake).
- [ ] T025 [US2] Validator-linked behavior: a binary linking `fixpp::validators::vlatest` calls `validate_<Msg>` on an `Args` missing a required field → reports the missing required field consistent with the 077 typed validator (validator RESULT, not wire bytes; FR-003/FR-004 AC2). File: `tests/session/test_078_validator_linked_us2.cpp` (+ CMake).

**Checkpoint**: US1 AND US2 both independently functional — link-time opt-in verified in both directions.

---

## Phase 5: User Story 3 - Consumer force-inlines a few hot messages while linking the rest (Priority: P2)

**Goal**: A client mixes modes — links the bulk of a version's builders while force-inlining a chosen few — ODR-safe, byte-identical.

**Independent Test**: A consumer force-inlines one message and links the rest; the inlined body is emitted locally (inlinable), every other `build_<Msg>` resolves from the lib, and both modes produce byte-identical output.

> Write these tests FIRST and confirm they FAIL before relying on the Foundational split.

- [ ] T026 [P] [US3] Builder-side mixing witness (FR-007 headline / New-4): force-inline one `build_<Msg>` via `FIXPP_BUILDERS_HEADER_ONLY_<Msg>`, link the rest of `fixpp::builders::<ver>` → no duplicate-symbol; the inlined body is available at the call site, every other `build_<Msg>` resolves from the lib, and the inlined message's wire output is byte-identical to its linked form (SC-004 inline; FR-006). File: `tests/codegen/test_078_builder_mixing_us3.cpp` (+ CMake).
- [ ] T027 [P] [US3] Validator-side mixing witness (R2a leg i): force-inline one `validate_<Msg>` (via `FIXPP_VALIDATORS_HEADER_ONLY_<Msg>`) that **shares a group-plan** with a linked validator, and link `fixpp::validators::<ver>` → no duplicate-symbol (single `inline` group-plan trait in `validators/traits.hpp`); inlined and linked validators are result-identical (same success/error + offending tag) (FR-007). File: `tests/codegen/test_078_validator_mixing_us3.cpp` (+ CMake).
- [ ] T028 [P] [US3] SC-002 `nm` link-only witness: a consumer calling a SUBSET (say 3) of a version's builders, linking `fixpp::builders::<ver>` → `nm --defined-only` shows only the 3 called `build_` symbols (per-message `.o` archive granularity), not the whole ~18–20 MiB set (SC-002). File: `tests/codegen/test_078_nm_link_only_us3.cpp` (+ CMake).

**Checkpoint**: US1+US2+US3 independently functional — ODR-safe mixing + per-message link granularity verified.

---

## Phase 6: User Story 4 - A consumer that wants "everything" uses the aggregator (Priority: P2)

**Goal**: `all.hpp` exposes a whole version's builders, byte-identical to the removed monolith, staying slim by default; the old `Builders.hpp` path is gone.

**Independent Test**: A consumer includes `all.hpp`, exercises every `build_<Msg>` (byte-identical to the 077 monolith), and the old `Builders.hpp` path no longer resolves.

> Write these tests FIRST and confirm they FAIL before relying on the Foundational split.

- [ ] T029 [P] [US4] `all.hpp` full-set equivalence: a consumer `#include <fixpp/<ns>/all.hpp>` (default/link mode) exercises every `build_<Msg>` for the version → output byte-identical to the 077 monolith / golden set, 0 diffs (SC-005; FR-008). Also exercises FR-012: one TU pulling many per-message headers via `all.hpp` compiles with `groups.hpp` included exactly once effectively (`#pragma once`). File: `tests/codegen/test_078_all_hpp_fullset_us4.cpp` (+ CMake).
- [ ] T030 [US4] `all.hpp` default-mode compile-cost guard: confirm `all.hpp` in default (link) mode does NOT resurrect the ~3.6 GiB monolith parse (R5 guard) — routed to the compile-bench decision record `.specify/decisions/078-precompiled-builder-libs-verify.md` (not a hard ctest gate). Repoint/reuse the `bench/codegen/vlatest_builders_compile_bench/` harness for the `all.hpp`-default measurement.
- [ ] T031 [P] [US4] Negative check — the old include path is gone: assert `fixpp/<ns>/Builders.hpp` no longer resolves after the split (US4 AC2 / SC-005 removal leg). File: `tests/codegen/test_078_builders_hpp_removed_us4.cpp` (+ CMake).

**Checkpoint**: US1–US4 independently functional — aggregator delivers the full set, slim by default, old path removed.

---

## Phase 7: User Story 5 - The library's own test suite compiles the heavy builders once, not per test TU (Priority: P2)

**Goal**: The heavy builder tests link the prebuilt lib (giant compile happens once at the lib target), retiring the #197 CI stopgap.

**Independent Test**: The relinked heavy test TUs compile with peak RSS below the thresholds that necessitated #197 gating.

- [ ] T032 [US5] Verify the (formerly heavy) builder completeness + round-trip TUs — now `#include` slim `all.hpp` + LINK the prebuilt libs (relink done in Foundational T019) — compile with peak build-time RSS well under the 16 GB hosted-runner limit; record the measurement in `.specify/decisions/078-precompiled-builder-libs-verify.md` (SC-006). Surface = `tests/codegen/CMakeLists.txt` + `tests/session/CMakeLists.txt` (the relinked TUs).
- [ ] T033 [US5] #197-removal CI evidence run (R8 step 2): trigger a `workflow_dispatch` (or feature-branch `push`) run on the post-split tree — a non-`pull_request` event sets `python_touched=true` unconditionally (`.github/workflows/tier1.yml:141`) so it exercises the sanitizer-instrumented legs INCLUDING the python-bindings `asan`/`ubsan`/`tsan` legs (path-gated + skip on the PR) plus all tier1 C++ sanitizer legs and tier2 MSVC — confirm all green without the pool. Local `/speckit-verify` (clang-only + local) CANNOT supply this evidence — state it. Capture the green run reference in `.specify/decisions/078-precompiled-builder-libs-verify.md`.
- [ ] T034 [US5] ONLY after T033 is green: delete the #197 stopgap — the `FIXPP_BUILD_HEAVY_BUILDER_TESTS` option (`CMakeLists.txt:208-210`), the 3 preset ON overrides (`CMakePresets.json:26,53,198`), and the `heavy_builder_compile` Ninja job pool (`CMakeLists.txt:347-380`). Keep `/bigobj` on the completeness TUs as cheap insurance. **Fallback:** if a leg regresses, re-scope the minimum still-needed guard (never silently drop it); or split the deletion into a follow-up PR that lands only after a `push:main`/dispatch run proved the legs (note it in the decision record). Files: `CMakeLists.txt` + `CMakePresets.json`.

**Checkpoint**: All five user stories independently functional; the #197 stopgap retired (CI-gated) or its removal explicitly deferred to a named follow-up.

---

## Phase 8: Polish & Cross-Cutting Concerns

**Purpose**: Constitution/docs/decision-record close-out. No story label.

- [ ] T035 Record the constitution AMEND to be folded at merge (do NOT edit at Gate A/tasks): minimal inline clause edits to `.specify/constitution.md:88` (I §1 — "single-TU header" → "(Annotation, 078 …: layout restructured to precompiled per-version libs + slim headers)") and `:375` (XVIII §7 — "single … emitter" → "(Annotation, 078 …: packaged as precompiled per-version builder/validator libs)"), plus a version bump **v0.9→v0.10** and a Sync Impact Report. File: `.specify/constitution.md`.
- [ ] T036 [P] Document the split layout + link-time opt-in + per-message inline mode in `docs/src/dictionary/codegen.md`.
- [ ] T037 [P] Record the include-path break (FR-008: `Builders.hpp` → `all.hpp`) and the #197-stopgap retirement as B/L rows in `spec/behaviors-and-limitations.md`.
- [ ] T038 Consolidate the compile-bench decision record (SC-001/SC-006 compile-RSS/wall via the 003/T046 convention, NOT Article VIII) in `.specify/decisions/078-precompiled-builder-libs-verify.md` (`## compile-bench` / verify section).

### Mandatory close-out tasks (ALWAYS emit — Gate-B preconditions, Article XVII §8)

- [ ] T039 [P] **Catalogue close-out**: 078 adds NO new OFFICIAL `spec/feature-catalogue.md` row — it is a layout restructure of 077's typed builder/validator tier (byte-identical wire, no new FIX coverage). Record that explicitly (no owned OFFICIAL row) and update the builder-tier status note / relevant tier annotation to reflect the 078 restructure; verify there is no orphaned owned row and that `spec/coverage-index.md` needs no new coverage entry (record the "no new row" disposition there). Files: `spec/feature-catalogue.md` + `spec/coverage-index.md`.
- [ ] T040 **Feature-completeness audit (MUST be the FINAL task)**: assert against the merged tree that (i) every `tasks.md` row is `[X]` or carries an explicit waiver rationale; (ii) every FR-001..012 and SC-001..007 maps to a landed test AND a landed implementation (incl. FR-011 zero-core-change verified by grep of `src/`/`capi/`/`bindings/`/`python`); (iii) every US1..5 has an independent witness; (iv) the catalogue close-out (T039) is recorded (no owned OFFICIAL row + coverage-index disposition). Record the verdict (100% or fully-waived) in `.specify/decisions/078-precompiled-builder-libs-verify.md` `## Completeness` (or a sibling `.specify/decisions/078-precompiled-builder-libs-completeness.md`). This is the hard `/gate-b` precondition (Article XVII §8 / pre-flight 4d).

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: no dependencies.
- **Foundational (Phase 2)**: depends on Setup; **BLOCKS all user stories**. Internally, **T002 (R2a probe) blocks T003 (emitter split)**; T003→T004→T005 are the emitter chain; T006 (CMake targets) depends on the emitted set; T007 (goldens) + T008 (determinism) depend on the emitter; the census batch T009–T018 depends on T003–T007 (new artifacts must exist); T019 (relink) depends on T006 (targets) and comes after T018 (shares `tests/codegen/CMakeLists.txt`).
- **User Stories (Phase 3–7)**: all depend on Foundational completion; then proceed in parallel (if staffed) or in priority order P1 (US1, US2) → P2 (US3, US4, US5).
- **Polish (Phase 8)**: depends on all desired user stories; T034 (#197 deletion) depends on T033 (CI evidence green); T040 (audit) is FINAL and depends on everything.

### User Story Dependencies

- **US1 (P1)**: after Foundational — no dependency on other stories.
- **US2 (P1)**: after Foundational — independently testable (its `nm` witness reuses T002 leg ii but is self-contained).
- **US3 (P2)**: after Foundational — mixing/`nm` witnesses seeded from the R2a probe (T002), independently testable.
- **US4 (P2)**: after Foundational — `all.hpp` equivalence + old-path-removed, independently testable.
- **US5 (P2)**: after Foundational — heavy-TU relink verified; T034 gated on the T033 CI evidence run.

### Within Each User Story

- Tests are written and confirmed FAILING before relying on the Foundational split.
- Story complete before moving to the next priority (or parallelize across stories if staffed).

### Parallel Opportunities

- Foundational census batch **T009–T018** are all different files → run in parallel (after T003–T007).
- Foundational emitter chain **T003→T004→T005** is sequential (same emitter files); **T002** blocks it.
- The per-message library compile is inherently parallel: `fixpp_builders_<ver>` / `fixpp_validators_<ver>` (T006) compile all `<Msg>.builder.cpp` / `<Msg>.validator.cpp` objects concurrently (one `.o` per message per side) — the "compile once, per message" build-parallelism that is the SC-006 basis.
- Within a story, the `[P]`-marked witness TUs are independent files → parallel.
- Across stories, US1/US2 (P1) and US3/US4/US5 (P2) can be developed in parallel once Foundational lands.
- Polish **T036/T037/T039** are `[P]` (different files); **T035/T038/T040** touch shared decision/constitution files → sequential.

---

## Parallel Example: Foundational census batch

```bash
# After T003–T007, launch the migration-census dispositions together (all different files):
Task: "T009 re-point build-graph markers in cmake/Codegen.cmake"
Task: "T010 re-point existence assertion in tests/codegen/codegen_build_graph_test.cmake"
Task: "T011 re-point FIXPP_BUILDERS_HEADER staleness grep in codegen_source_staleness_test.cmake"
Task: "T012 re-point parse helpers in tests/codegen/builder_completeness_common.hpp"
Task: "T013 re-point tests/codegen/builder_completeness_mutation_witness_test.cpp"
Task: "T014 re-point tests/codegen/test_077_builder_no_emit.cpp"
Task: "T015 re-point tests/codegen/vlatest_compile_smoke_test.cpp"
Task: "T016 re-point test_077_builder_dedup_count.cpp → groups.hpp"
Task: "T017 re-point test_077_v42_vt11_completeness_and_c4.cpp C4 → groups.hpp"
Task: "T018 re-point -D..._BUILDERS_HPP defs in tests/codegen/CMakeLists.txt"
```

## Parallel Example: independent witness TUs (post-Foundational)

```bash
# Different files, no cross-dependency — launch together:
Task: "T020 slim-header compile+link witness (US1)"
Task: "T024 nm builder-only zero-validator witness (US2)"
Task: "T026 builder-side mixing witness (US3)"
Task: "T028 nm link-only subset witness (US3)"
Task: "T029 all.hpp full-set equivalence (US4)"
```

---

## Implementation Strategy

### MVP First (Foundational + US1)

1. Phase 1: Setup (T001).
2. Phase 2: Foundational (T002–T019) — CRITICAL, blocks all stories. Start with the R2a probe (T002).
3. Phase 3: US1 (T020–T023).
4. **STOP and VALIDATE**: cheap slim compile + link + byte-identical wire (SC-001/SC-004 builder). This is the core value.

### Incremental Delivery

1. Foundational → foundation ready (six libs build, deterministic split, gates re-pointed).
2. US1 (P1) → slim compile + link → MVP.
3. US2 (P1) → send-only zero validator code.
4. US3 (P2) → ODR-safe mixing + per-message link granularity.
5. US4 (P2) → aggregator full set, slim by default, old path removed.
6. US5 (P2) → heavy tests cheap; #197 stopgap retired (CI-gated).
7. Polish → constitution AMEND (at merge) + docs + decision record + close-out.

---

## Requirements → Task coverage (completeness)

| Requirement | Task(s) |
|---|---|
| FR-001 slim declaration surface | T003 |
| FR-002 `libfixpp_builders_<ver>` (compile once) | T006 (build), T003 (emit) |
| FR-003 separate `libfixpp_validators_<ver>` | T006, T003 |
| FR-004 both always-built, link-time opt-in | T006, T024, T025 |
| FR-005 builder-only carries no validator code / no link dep | T005, T024 |
| FR-006 per-message header-only inline mode | T003, T026, T027 |
| FR-007 mixing, no dup / no divergent def | T002, T026, T027 |
| FR-008 `all.hpp` replaces `Builders.hpp` | T003, T029, T031 |
| FR-009 byte-identical builder + result-identical validator, link & inline | T021, T026, T027, T029 |
| FR-010 deterministic split set + regenerated goldens | T007, T008 |
| FR-011 no core change (src/capi/bindings/python) | T001, T040 |
| FR-012 shared `groups` once per version, include-guarded | T003, T029 |
| SC-001 one-message-TU compile RSS ≪ monolith | T023 |
| SC-002 link only used messages | T028 |
| SC-003 send-only zero validator code | T024 |
| SC-004 byte-identical builder + result-identical validator, both modes | T021, T026, T027 |
| SC-005 `all.hpp` full set byte-identical + old path removed | T029, T031 |
| SC-006 heavy TUs under runner limit | T032, T033 |
| SC-007 deterministic regeneration | T008 |

---

## Notes

- `[P]` = different files, no incomplete dependency; `[US#]` only in Phases 3–7.
- Compile-time RSS/wall SCs (SC-001/SC-006) are NOT ctest-assertable — they go to the compile-bench decision record (003/T046 convention), not Article VIII (runtime-only).
- The R2a probe (T002) is a Phase-0 blocking prerequisite, not a plain test — it settles R1/R2 empirically before the emitter commits to the layout.
- The two close-out tasks (T039 catalogue, T040 audit) are ALWAYS the last two; **T040 (completeness audit) is FINAL** and is the hard `/gate-b` precondition.
- Constitution edit (T035) is folded at MERGE per precedent — do not touch `.specify/constitution.md` at Gate A / `/tasks`.
