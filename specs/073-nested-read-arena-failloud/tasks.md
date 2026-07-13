# Tasks: Fail-loud on nested-read sub-table allocation failure (073, L-065-2, #184)

**Input**: Design documents from `specs/073-nested-read-arena-failloud/`
**Prerequisites**: plan.md, spec.md, research.md (D1–D8), data-model.md, contracts/nested_slices_result.md, quickstart.md

**Tests**: REQUIRED and RED-first — Article VII (TDD) + the feature's own acceptance is witness-based. Every witness is authored failing-first and mutation-proven per research.md D6.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: parallelizable (different file, no dependency on an incomplete task)
- **[Story]**: US1 (C-ABI) / US2 (typed). Phase 2 Foundational is the shared primitive widening that BLOCKS both stories; Phase 1 Setup is empty (no new scaffolding).

## Path Conventions

Single-project C++ library; `include/`, `src/`, `tools/`, `tests/`, `specs/` at the submodule root.

---

## Phase 1: Setup

**Purpose**: none — this feature adds no new module, directory, build target, or dependency. No setup tasks.

---

## Phase 2: Foundational (Blocking Prerequisites for US1 AND US2)

**Purpose**: widen the shared primitive `OffsetTable::nested_group_slices` to a status-bearing result whose `alloc_failed` ORs ALL THREE arena-exhaustion origins (mode (c) found at implement — research §D2) at BOTH empty-returning exits (research.md D1/D2/D3, data-model.md, contracts). Both user stories consume this; neither can be implemented until it lands.

### Tests for Foundational (write FIRST, prove RED before implementation)

- [X] T001 [P] Wire-level primitive witness in `tests/wire/nested_group_slices_failloud_test.cpp` (NEW): construct an `OffsetTable` whose parse arena is a tiny-cap `std::pmr::monotonic_buffer_resource` over `std::pmr::null_memory_resource()` (per quickstart.md harness). Assert `nested_group_slices(...)` returns `nested_slices_result{ alloc_failed=true, slices.empty() }` for ALL THREE origins (research §D2). **Pin each mode by INTROSPECTION on the sub-table, NOT by cap band** (`sizeof(OffsetTable)` is platform/build-specific — a cap tuned to mode (c) on clang-debug can silently land in mode (a) or succeed on gcc/MSVC/release, making the discriminator vacuous; the introspection assert fails LOUD if a platform's cap lands in a different mode): **(a)** the resolved sub-table is `nullptr`; **(b)** non-null + `build_status()` ok + `group_slices_status().alloc_failed` (its own `group_slices()` re-throws); **(c)** non-null + `build_status().error()==out_of_memory` (ctor `build()` degraded). Add a **repeated-read** assertion (read twice → `alloc_failed` both times, on modes that cache) and controls (`slice_data==nullptr` absent → `false`; genuine count-0 non-null `build_status()`-ok table → `false`). **Three-mutant matrix, each RED SEEN**: M1 null-only predicate (`alloc_failed = table==nullptr`) → modes (b) AND (c) RED; M2 drop the `|| build_status()==out_of_memory` term (the old 2-mode formula) → mode (c) RED (the mode-c discriminator); M3 final-exit-only instrumentation → read-2 (cache-hit exit) RED. Introspection via a new TEST-ONLY friend seam (`nested_cache_access_for_testing`, declared in offset_table.hpp / defined in wire_test_hooks.hpp, mirrors the frame_view_access/frame_view_slice_access split). All three modes reachable on this build (cap 1500/1930/2700 bytes); all three mutants SEEN RED and cleanly reverted (`git diff --stat src/wire/offset_table.cpp` unchanged after revert).

### Implementation for Foundational

- [X] T002 [P] In `include/fixpp/wire/offset_table.hpp`: add `struct nested_slices_result { std::span<group_slice const> slices{}; bool alloc_failed = false; };` and the internal `struct group_slices_result { std::span<group_slice const> slices{}; bool alloc_failed = false; };`, each with a `static_assert(std::is_trivially_copyable_v<...>)`. Declare `[[nodiscard]] group_slices_result group_slices_status(std::uint16_t no_tag) const noexcept;`. Change BOTH `nested_group_slices` overload declarations (the 7-arg and the 2-arg convenience) to return `nested_slices_result`.
- [X] T003 In `src/wire/offset_table.cpp`: implement `group_slices_status(no_tag)` by moving the existing `group_slices` body into it, setting `alloc_failed=true` in the `catch (std::bad_alloc)` (`:674-675`) and `false` on the warm-cache hit (`:614-617`) and the normal (incl. count-0) return; make the **public `group_slices(no_tag)` a one-line wrapper** `return group_slices_status(no_tag).slices;` (unchanged signature → all top-level callers untouched, FR-002/L-073-1).
- [X] T004 In `src/wire/offset_table.cpp`: change both `nested_group_slices` overloads to build and return `nested_slices_result`, OR-ing **three** arena-exhaustion origins at each exit (mode (c) found at implement — see research §D2). At the `slice_data==nullptr` guard (`:722-724`) return `{ {}, false }`. At the **cache-hit early exit** resolve against **`row.table`** (the matching cache row's pointer — the loop-scoped local `table` is NOT assigned for a same-no_tag match and is `nullptr` there; using the local at this exit is a false-positive trap reporting `alloc_failed=true` on every warm cache-hit of a healthy group): `alloc_failed = (row.table == nullptr) || (row.table->build_status().error() == out_of_memory) || row.table->group_slices_status(nested_no_tag).alloc_failed`. At the **final exit**, resolve against the post-build local `table`: `alloc_failed = (table == nullptr) || (table->build_status().error() == out_of_memory) || table->group_slices_status(nested_no_tag).alloc_failed`. The mode-(c) term (`build_status().error() == out_of_memory`) is scoped to `out_of_memory` ONLY — a table degraded for malformed data (`offset_table_full`/`invalid_field_format`) must NOT fail-loud (FR-007 disjointness). Preserve the FR-004b zero-alloc-on-repeat gate and build-once cache semantics.
- [X] T005 [P] Adapt every existing `nested_group_slices(...)` call site from the old `span` binding to the new `nested_slices_result` (`auto r = ...; r.slices.empty()/.size()/[i]`), mechanically, in: `tests/wire/nested_group_slices_cache_test.cpp`, `tests/wire/nested_group_extent_test.cpp`, `tests/wire/group_slice_trailing_soh_test.cpp`, `tests/fuzz/fuzz_wire_nested_slice.cpp`, `tests/alloc_guard/test_dict066_grouped_read_alloc_guard.cpp`, `tests/capi/message_read_test.cpp` (the `parent_cache_owner->nested_group_slices` call). Compile-driven — any missed site fails loudly.

**Checkpoint**: T001 GREEN; the full wire test suite + all adapted call sites compile and pass. Foundational status seam correct at the primitive level.

---

## Phase 3: User Story 1 — C-ABI nested read fails loud on arena exhaustion (Priority: P1)

**Goal**: the C-ABI nested read returns `FIXPP_ERR_WIRE_LIMIT_EXCEEDED` (not `OK`/nc=0) when the sub-view allocation fails. **Independent test**: drive `fixpp_group_get_nested_group` under a tiny arena; assert the distinct code.

### Tests for User Story 1 (write FIRST, mutation-proven RED)

- [X] T006 [US1] C-ABI arena-exhaustion witness in `tests/capi/message_read_failloud_test.cpp` (NEW): present nested group + tiny-cap null-upstream arena → the C-ABI nested read returns `FIXPP_ERR_WIRE_LIMIT_EXCEEDED` (SC-001, both sub-modes a and b); **repeated read** → `WIRE_LIMIT_EXCEEDED` both times; controls (SC-003) → absent nested group returns `FIXPP_ERR_TAG_NOT_FOUND`, genuine count-0 returns `FIXPP_ERR_OK`/nc=0. Assert the code DIRECTLY (not an `nc==0` proxy). Mutation-proof: removing the `alloc_failed` arm returns `OK`/nc=0 → RED.

### Implementation for User Story 1

- [X] T007 [US1] In `src/capi/message_read.cpp` (the nested read, ~:489): bind `auto r = ...nested_group_slices(...)`; add `if (r.alloc_failed) return FIXPP_ERR_WIRE_LIMIT_EXCEEDED;` **before** the `slices.empty()` presence probe (`:492`); drive the existing absent/count-0 logic from `r.slices`.

**Checkpoint**: US1 witness GREEN; C-ABI path fail-loud, controls unaffected. No C-ABI symbol/struct/signature/macro change (Article X).

---

## Phase 4: User Story 2 — typed C++ nested read fails loud on arena exhaustion (Priority: P1)

**Goal**: the typed nested accessor surfaces the failure via `group_view::alloc_failed()`, delivered as a value (no throw across `noexcept`). **Independent test**: drive `msg.<group>()[i].<nestedGroup>()` under a tiny arena; assert `.alloc_failed()==true`.

### Tests for User Story 2 (write FIRST, mutation-proven RED)

- [X] T008 [US2] Typed arena-exhaustion witness in `tests/wire/group_view_failloud_test.cpp` (NEW): present nested group + arena → the returned `group_view` has `alloc_failed()==true`, `size()==0`, process does NOT terminate (SC-002); **repeated read** → `alloc_failed()` both reads; controls (SC-003) → absent/count-0 → `alloc_failed()==false`. Assert `alloc_failed()` DIRECTLY (not a `size()==0` proxy). The typed path CANNOT introspect the sub-table, so do NOT make it mode-specific — engineer the **FIXTURE (a large nested group) so nested allocation fails by a WIDE arena-cap margin on every CI tier** (clang-debug + gcc-release + MSVC) while the top-level parse still clears the floor; a knife-edge cap tuned to clang-debug `sizeof(OffsetTable)` is fragile across tiers ([[feedback_local_verify_clang_only_misses_gcc_release_ci_job]]). Mutation-proof: not threading `alloc_failed` in the emitter → RED (seen).

### Implementation for User Story 2

- [X] T009 [US2] In `include/fixpp/wire/group_view.hpp`: add private `bool alloc_failed_ = false` and public `[[nodiscard]] bool alloc_failed() const noexcept { return alloc_failed_; }`; add a trailing defaulted `bool alloc_failed = false` parameter to both ctors (back-compat for `MessageView::group<>()` + dict-free ctor). Confirm the `is_trivially_copyable_v<group_view>` property and seam #8 (iter()/operator[] agreement) hold.
- [X] T010 [US2] In `tools/codegen/fixpp-codegen/emit_messages.cpp` (the nested-descent accessor, ~:256-286): bind `auto const r = ...nested_group_slices(...)` and emit `return group_view<G_c>{ r.slices, child_ctx, r.alloc_failed };`. Keep the `.pushed(c)` child-context (072 FR-007) unchanged.
- [X] T011 [US2] Force-regenerate the checked-in read goldens `specs/003-dictionary-codegen/contracts/golden/v{44,50sp2,42,vt11}_Messages.golden.hpp` (the generated nested accessors changed) and run `codegen_determinism_test` GREEN. Regenerate via a clean `_codegen` reconfigure (project_codegen_emitter_staleness). NOTE: only `v44` (58 `nested_group_slices` sites) and `v50sp2` (690) actually change; `v42`/`vt11` have zero nested accessors → their regen is a byte-identical no-op (a no-diff there is EXPECTED, not a broken emitter).
- [X] T012 [P] [US2] Update the shape oracle `specs/004-wire-codec/contracts/group_view.hpp` to add the `alloc_failed()` accessor (Article XVI consistency).

**Checkpoint**: US2 witnesses GREEN; `codegen_determinism_test` GREEN; typed path fail-loud in parity with US1 (SC-005).

---

## Phase 5: Polish & Cross-Cutting Concerns

- [X] T013 [P] No-regression (SC-004): FULL debug `ctest` on a CLEAN-recompiled tree → **319/320 pass**; the sole red is `codegen-build-graph-check` (the git-cleanliness gate — trips only on the uncommitted tree, clears at commit). `codegen_determinism_test` GREEN (goldens in sync). NOTE: a first run showed a spurious `session_tc_reject` `bad_array_new_length` — gdb traced it to a stale `SessionConfig` object (ODR/layout artifact from two background builds racing on the shared build dir, `feedback_stale_build_objects_false_green_masks_pins`); a clean single-job recompile cleared it (test unrelated to this diff). ASan/UBSan + 3-tier matrix run at `/speckit-verify`.
- [X] T014 [P] Confirm C-ABI zero symbol/struct/signature/macro delta vs `1.5.0` (Article X): STATIC verify DONE — `git diff` shows zero change under `include/fix/**` (no C-ABI header touched), only `src/capi/message_read.cpp` implementation (+8/-1 = the `alloc_failed` arm returning the pre-existing `WIRE_LIMIT_EXCEEDED`); ABI-golden test + `.github/workflows/abi-golden.yml` untouched. Dynamic `abi_symbol_golden_test` (nm symbol-set gate) runs in the T013 full ctest + 3-tier CI.
- [X] T015 Update `spec/behaviors-and-limitations.md` (FR): record **L-065-2** as ADDRESSED on the nested path (both read consumers, fail-loud). **Also STRIKE/correct the stale clause in the existing L-065-2 row** that says the fix requires "…a larger/growable inbound arena" — FR-009 (clarified 2026-07-13) supersedes it: this feature is fail-loud-only, no arena-sizing change, so the shipped fix's scope must not be misstated. Add **L-073-1** — top-level group reads (`group_slices()` called directly by the top-level getter) retain the same silent-truncation-on-exhaustion (same fixed-arena family as 066 `cc169700`), deliberately out of scope (FR-009).
- [X] T016 Run `quickstart.md` validation end-to-end — the 5 scenarios map 1:1 to landed witnesses (S1 C-ABI→T006; S2 typed→T008; S3 repeated-read→T006/T008; S4 controls→T006/T008; S5 second-loss mode-(b)→T001, plus mode-(c) added), all GREEN in the subagent runs and re-exercised by the T013 full ctest.

### Mandatory close-out tasks (ALWAYS emit — Gate-B preconditions, Article XVII §8)

- [X] T017 [P] **Catalogue close-out**: 073 owns no NEW OFFICIAL row (it is a limitation-closure on the CA-010 nested-read lineage) — updated CA-010's tail to mark L-065-2 ADDRESSED by 073, and added the `spec/coverage-index.md` §073 entry (L-065-2 / #184).
- [X] T018 **Feature-completeness audit (FINAL task)**: DONE — `.specify/decisions/073-nested-read-arena-failloud-completeness.md` records the full FR-001..009 / SC-001..005 ↔ impl ↔ witness ↔ catalogue mapping, verdict **100% / 0 waived**, plus the implement-time mode-(c) discovery + Gate-B handoff (2→3 origin census).

---

## Dependencies & Execution Order

- **Phase 2 (Foundational) BLOCKS Phase 3 and Phase 4** — both stories consume `nested_slices_result`. T001 (RED) → T002 → T003 → T004 → T005 (adapt callers) → checkpoint.
- **US1 (Phase 3)** depends only on Foundational. T006 (RED) → T007.
- **US2 (Phase 4)** depends on Foundational. T008 (RED) → T009 → T010 → T011 (golden regen, after the emitter change) → T012. T011 depends on T010.
- **US1 and US2 are independent of each other** and may proceed in parallel once Foundational lands.
- **Phase 5** after both stories. T018 is the FINAL task.

## Parallel Opportunities

- Within Foundational: T002 (header) and T005 (test-call-site adaptation) can start in parallel with the T003/T004 body work once the header types exist; T001 is written first (RED).
- US1 (T006/T007) and US2 (T008–T012) run in parallel after the Foundational checkpoint.
- Polish: T013/T014/T017 are `[P]`.

## Implementation Strategy (MVP first)

MVP = Foundational + US1 (C-ABI fail-loud) — the shipped, publicly-supported read surface and the core deliverable. US2 (typed parity) is a required same-feature increment (FR-005 symmetry — do NOT ship US1 without US2, per the half-restructure rule), delivered in the same PR. Golden regen (T011) rides US2. Fail-loud-only scope throughout (FR-009).
