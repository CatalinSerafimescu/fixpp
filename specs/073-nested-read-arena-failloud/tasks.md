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

**Purpose**: widen the shared primitive `OffsetTable::nested_group_slices` to a status-bearing result whose `alloc_failed` ORs BOTH arena-exhaustion sub-modes at BOTH empty-returning exits (research.md D1/D2/D3, data-model.md, contracts). Both user stories consume this; neither can be implemented until it lands.

### Tests for Foundational (write FIRST, prove RED before implementation)

- [ ] T001 [P] Wire-level primitive witness in `tests/wire/nested_group_slices_failloud_test.cpp` (NEW): construct an `OffsetTable` whose parse arena is a tiny-cap `std::pmr::monotonic_buffer_resource` over `std::pmr::null_memory_resource()` (per quickstart.md harness). Assert `nested_group_slices(...)` returns `nested_slices_result{ alloc_failed=true, slices.empty() }` for BOTH sub-modes — **(a)** cap too small for `build_nested_subview`'s `OffsetTable` allocation, and **(b) second-loss**: cap large enough to build the sub-table non-null but too small for its `group_slices()` reserve/push (tune per research.md D2/D6). Add a **repeated-read** assertion (read twice → `alloc_failed` both times) and controls (`slice_data==nullptr` absent → `alloc_failed=false`; genuine count-0 non-null table → `alloc_failed=false`). Mutation-proof: a null-only `alloc_failed` predicate must leave sub-mode (b) RED; instrumenting only the final exit must leave read-2 RED.

### Implementation for Foundational

- [ ] T002 [P] In `include/fixpp/wire/offset_table.hpp`: add `struct nested_slices_result { std::span<group_slice const> slices{}; bool alloc_failed = false; };` and the internal `struct group_slices_result { std::span<group_slice const> slices{}; bool alloc_failed = false; };`, each with a `static_assert(std::is_trivially_copyable_v<...>)`. Declare `[[nodiscard]] group_slices_result group_slices_status(std::uint16_t no_tag) const noexcept;`. Change BOTH `nested_group_slices` overload declarations (the 7-arg and the 2-arg convenience) to return `nested_slices_result`.
- [ ] T003 In `src/wire/offset_table.cpp`: implement `group_slices_status(no_tag)` by moving the existing `group_slices` body into it, setting `alloc_failed=true` in the `catch (std::bad_alloc)` (`:674-675`) and `false` on the warm-cache hit (`:614-617`) and the normal (incl. count-0) return; make the **public `group_slices(no_tag)` a one-line wrapper** `return group_slices_status(no_tag).slices;` (unchanged signature → all top-level callers untouched, FR-002/L-073-1).
- [ ] T004 In `src/wire/offset_table.cpp`: change both `nested_group_slices` overloads to build and return `nested_slices_result`. At the `slice_data==nullptr` guard (`:722-724`) return `{ {}, false }`. At the **cache-hit early exit** (`:748-750`) resolve against **`row.table`** (the matching cache row's pointer — the loop-scoped local `table` variable is NOT yet assigned at this point in control flow for a same-no_tag match, and is `nullptr` there regardless of the cached build's outcome; using the local `table` at this exit is a false-positive trap that reports `alloc_failed=true` on every warm cache-hit of a successfully-built group): `alloc_failed = (row.table == nullptr) || row.table->group_slices_status(nested_no_tag).alloc_failed`, `slices = (row.table != nullptr) ? that_status.slices : {}`. At the **final exit** (`:768`), resolve against the post-build local `table`: `alloc_failed = (table == nullptr) || table->group_slices_status(nested_no_tag).alloc_failed`, `slices = (table != nullptr) ? that_status.slices : {}`. Preserve the FR-004b zero-alloc-on-repeat gate and the build-once cache semantics.
- [ ] T005 [P] Adapt every existing `nested_group_slices(...)` call site from the old `span` binding to the new `nested_slices_result` (`auto r = ...; r.slices.empty()/.size()/[i]`), mechanically, in: `tests/wire/nested_group_slices_cache_test.cpp`, `tests/wire/nested_group_extent_test.cpp`, `tests/wire/group_slice_trailing_soh_test.cpp`, `tests/fuzz/fuzz_wire_nested_slice.cpp`, `tests/alloc_guard/test_dict066_grouped_read_alloc_guard.cpp`, `tests/capi/message_read_test.cpp` (the `parent_cache_owner->nested_group_slices` call). Compile-driven — any missed site fails loudly.

**Checkpoint**: T001 GREEN; the full wire test suite + all adapted call sites compile and pass. Foundational status seam correct at the primitive level.

---

## Phase 3: User Story 1 — C-ABI nested read fails loud on arena exhaustion (Priority: P1)

**Goal**: the C-ABI nested read returns `FIXPP_ERR_WIRE_LIMIT_EXCEEDED` (not `OK`/nc=0) when the sub-view allocation fails. **Independent test**: drive `fixpp_group_get_nested_group` under a tiny arena; assert the distinct code.

### Tests for User Story 1 (write FIRST, mutation-proven RED)

- [ ] T006 [US1] C-ABI arena-exhaustion witness in `tests/capi/message_read_failloud_test.cpp` (NEW): present nested group + tiny-cap null-upstream arena → the C-ABI nested read returns `FIXPP_ERR_WIRE_LIMIT_EXCEEDED` (SC-001, both sub-modes a and b); **repeated read** → `WIRE_LIMIT_EXCEEDED` both times; controls (SC-003) → absent nested group returns `FIXPP_ERR_TAG_NOT_FOUND`, genuine count-0 returns `FIXPP_ERR_OK`/nc=0. Assert the code DIRECTLY (not an `nc==0` proxy). Mutation-proof: removing the `alloc_failed` arm returns `OK`/nc=0 → RED.

### Implementation for User Story 1

- [ ] T007 [US1] In `src/capi/message_read.cpp` (the nested read, ~:489): bind `auto r = ...nested_group_slices(...)`; add `if (r.alloc_failed) return FIXPP_ERR_WIRE_LIMIT_EXCEEDED;` **before** the `slices.empty()` presence probe (`:492`); drive the existing absent/count-0 logic from `r.slices`.

**Checkpoint**: US1 witness GREEN; C-ABI path fail-loud, controls unaffected. No C-ABI symbol/struct/signature/macro change (Article X).

---

## Phase 4: User Story 2 — typed C++ nested read fails loud on arena exhaustion (Priority: P1)

**Goal**: the typed nested accessor surfaces the failure via `group_view::alloc_failed()`, delivered as a value (no throw across `noexcept`). **Independent test**: drive `msg.<group>()[i].<nestedGroup>()` under a tiny arena; assert `.alloc_failed()==true`.

### Tests for User Story 2 (write FIRST, mutation-proven RED)

- [ ] T008 [US2] Typed arena-exhaustion witness in `tests/wire/group_view_failloud_test.cpp` (NEW): present nested group + tiny arena → the returned `group_view` has `alloc_failed()==true`, `size()==0`, process does NOT terminate (SC-002, both sub-modes); **second-loss read-twice** (mode b) → `alloc_failed()` both reads; controls (SC-003) → absent/count-0 → `alloc_failed()==false`. Assert `alloc_failed()` DIRECTLY. Mutation-proof: not threading `alloc_failed` in the emitter → RED.

### Implementation for User Story 2

- [ ] T009 [US2] In `include/fixpp/wire/group_view.hpp`: add private `bool alloc_failed_ = false` and public `[[nodiscard]] bool alloc_failed() const noexcept { return alloc_failed_; }`; add a trailing defaulted `bool alloc_failed = false` parameter to both ctors (back-compat for `MessageView::group<>()` + dict-free ctor). Confirm the `is_trivially_copyable_v<group_view>` property and seam #8 (iter()/operator[] agreement) hold.
- [ ] T010 [US2] In `tools/codegen/fixpp-codegen/emit_messages.cpp` (the nested-descent accessor, ~:256-286): bind `auto const r = ...nested_group_slices(...)` and emit `return group_view<G_c>{ r.slices, child_ctx, r.alloc_failed };`. Keep the `.pushed(c)` child-context (072 FR-007) unchanged.
- [ ] T011 [US2] Force-regenerate the checked-in read goldens `specs/003-dictionary-codegen/contracts/golden/v{44,50sp2,42,vt11}_Messages.golden.hpp` (the generated nested accessors changed) and run `codegen_determinism_test` GREEN. Regenerate via a clean `_codegen` reconfigure (project_codegen_emitter_staleness). NOTE: only `v44` (58 `nested_group_slices` sites) and `v50sp2` (690) actually change; `v42`/`vt11` have zero nested accessors → their regen is a byte-identical no-op (a no-diff there is EXPECTED, not a broken emitter).
- [ ] T012 [P] [US2] Update the shape oracle `specs/004-wire-codec/contracts/group_view.hpp` to add the `alloc_failed()` accessor (Article XVI consistency).

**Checkpoint**: US2 witnesses GREEN; `codegen_determinism_test` GREEN; typed path fail-loud in parity with US1 (SC-005).

---

## Phase 5: Polish & Cross-Cutting Concerns

- [ ] T013 [P] No-regression (SC-004): FULL `ctest` (NOT narrow targets — the determinism golden hangs CI if stale) across debug + the sanitizer presets the diff touches (ASan/UBSan for the `bad_alloc`/arena path). All existing wire/capi/codegen tests green.
- [ ] T014 [P] Confirm C-ABI zero symbol/struct/signature/macro delta vs `1.5.0` (Article X) via the existing `tests/capi/abi_symbol_golden_test.cpp` + `.github/workflows/abi-golden.yml` (nm-based symbol-set gate): the only C-ABI change is a new error-return VALUE (`WIRE_LIMIT_EXCEEDED`) in a formerly-`OK`/nc=0 edge — verify no exported-symbol/struct-layout change and no C-ABI golden asserts the old OK/nc=0.
- [ ] T015 Update `spec/behaviors-and-limitations.md` (FR): record **L-065-2** as ADDRESSED on the nested path (both read consumers, fail-loud). **Also STRIKE/correct the stale clause in the existing L-065-2 row** that says the fix requires "…a larger/growable inbound arena" — FR-009 (clarified 2026-07-13) supersedes it: this feature is fail-loud-only, no arena-sizing change, so the shipped fix's scope must not be misstated. Add **L-073-1** — top-level group reads (`group_slices()` called directly by the top-level getter) retain the same silent-truncation-on-exhaustion (same fixed-arena family as 066 `cc169700`), deliberately out of scope (FR-009).
- [ ] T016 Run `quickstart.md` validation end-to-end (all 5 scenarios: C-ABI fail-loud, typed fail-loud, repeated-read, second-loss, non-failure controls).

### Mandatory close-out tasks (ALWAYS emit — Gate-B preconditions, Article XVII §8)

- [ ] T017 [P] **Catalogue close-out**: flip every feature-owned OFFICIAL row in `spec/feature-catalogue.md` to `done` and add the `spec/coverage-index.md` §073 entry (L-065-2 / #184).
- [ ] T018 **Feature-completeness audit (FINAL task)**: assert tasks ↔ FR-001..009 / SC-001..005 ↔ catalogue all map to a landed witness + implementation; record the 100%-or-waived verdict in `.specify/decisions/073-nested-read-arena-failloud-completeness.md` (or the `## Completeness` section of the verify record). `/gate-b` 4d hard-blocks without this.

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
