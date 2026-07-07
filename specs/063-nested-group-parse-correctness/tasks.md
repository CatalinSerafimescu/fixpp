---
description: "Task list — Nested Group-Parse Correctness (063)"
---

# Tasks: Nested Group-Parse Correctness (063)

**Input**: Design documents from `specs/063-nested-group-parse-correctness/`
**Prerequisites**: plan.md ✓, spec.md ✓, research.md ✓, data-model.md ✓, contracts/group-membership-and-extent.md ✓, quickstart.md ✓

**Tests**: INCLUDED — the constitution mandates TDD / discriminating witnesses (`[const Art IX]`); every defect lands mutation-proven RED then green (SC-003). Test tasks precede their implementation within each story.

**Design**: Gate A Round 1 → **Option A (exact context-scoped membership, key `(msg_type, bounded parent-no_tag-path, no_tag)`)**; Round 2 → **OffsetTable carries the `group_context`** as construction state, codegen emitter+goldens DO change, C-ABI positional/transitive (byte-identical). This is a coupled two-defect fix: **Defect B (US2) depends on Defect A's (US1) correct membership** to identify nested count tags, and both ride a shared context-threading seam (Phase 2 Foundational).

**Round-2 tasks-pins** (from Gate A triage, MUST be emitted — annotated inline as `[pin#N]`): #1 context-keyed store shape → T012; #2 entry_context size/align regression → T005; #3 non-alloc key/hash + census table-size report + no-alloc lookup test → T004/T012/T019/T020; #4 K=16 overflow→err_group_too_large → T024; #5 C-ABI last-nested-instance witness (+ conditional extent fix) → T027.

## Path Conventions

Single-library layout, all paths relative to the library submodule root `research/G19-fix-fpml-iso20022/library/`.

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Confirm the build/test baseline and the pre-fix RED reproduction before touching code.

- [X] T001 Confirm the debug build + relevant suites build/run on the baseline: `cmake --build build/linux-clang-debug -j2` then `ctest --test-dir build/linux-clang-debug -R "dictionary|wire|codegen|nested_group|group_slice|alloc|abi" --output-on-failure` — record the green baseline (existing behavior) before any change.
- [X] T002 Reproduce both defects RED on `main` (quickstart.md §0): confirm `tests/codegen/nested_group_read_test.cpp:353` `NestedQuoteEntriesPerInstancePrices` is `GTEST_SKIP()`'d (Defect B) and that a real-`FIX44.xml` `as_table_view()` MassQuote `quote_sets()[s].quote_entries()` reads `.size()==0` pre-fix (Defect A). Note the observed pre-fix values — they are the RED anchors for T010/T021/T028. *(Done: Defect-B skip anchor confirmed at :353 with root-cause skip reason; baseline 97/97 green. Defect-A real-dict `.size()==0` RED is observed when its net-new witness lands at T010 — the witness does not exist pre-fix.)*

---

## Phase 2: Foundational (Blocking Prerequisites — the shared context-threading seam)

**Purpose**: Add the `group_context` plumbing that BOTH Defect A (US1) and Defect B (US2) ride on. Threaded here with the membership store still bare-`no_tag`-keyed, so context is carried-but-ignored → behavior is byte-identical to `main` and all existing tests stay green. US1 makes the store context-keyed; US2 consumes the correct membership.

**⚠️ CRITICAL**: No user-story work can begin until this phase is complete — every US1/US2 task depends on this seam.

- [X] T003 Add the private compile-time depth cap `kMaxGroupDepth = 16` (mirroring codegen `emit_messages.cpp:137`) and widen `group_member_fn_t` (`include/fixpp/wire/offset_table.hpp:29`) to take a `const group_context&` argument (public C++, non-ABI; clarify-sanctioned). `group_slices(no_tag)` and the C-ABI signatures stay UNCHANGED.
- [X] T004 Define the trivially-copyable `group_context` POD as the non-allocating lookup key [pin#3-key] in `include/fixpp/wire/group_view.hpp`: `std::string_view msg_type` (aliases the message WIRE buffer via `MessageView::msg_type()`, `parser.hpp:149`) + `std::array<std::uint16_t, 16> parent_path` + `std::uint8_t depth`; add `static_assert(std::is_trivially_copyable_v<group_context>)`.
- [X] T005 [P] Add the `group_context` field to `entry_context` (`include/fixpp/wire/group_view.hpp:31-47`), preserving the existing `static_assert(is_trivially_copyable_v<entry_context>)` (`:47`); add a size+alignment regression test [pin#2] for `entry_context` and a representative generated `G_<no_tag>` entry in `tests/codegen/` guarding the enlarged by-value object.
- [X] T006 Make `OffsetTable` CARRY a `group_context` as construction state in `include/fixpp/wire/offset_table.hpp` + `src/wire/offset_table.cpp`: the ROOT table constructed with `{msg_type, parent_path=[]}`, a stored member read by `OffsetTable::group()` and passed into the (now context-taking) membership predicate. `group_slices(no_tag)` signature unchanged.
- [X] T007 In `include/fixpp/wire/parser.hpp`: at `MessageView::group<>` (`:271`) compute the ROOT context (`msg_type()` via the `MessageView` member decl `:149`) into `entry_context`, and widen the bound `group_member_fn` lambda (`:484-494`) to pass the stored context.
- [X] T008 Thread + seed the nested context in `src/wire/offset_table.cpp`: `build_nested_subview` (`:552`) / `nested_group_slices` (`:582`) gain the `group_context` field and `build_nested_subview` SEEDS the nested sub-`OffsetTable` with `{msg_type, outer-path + outer-group-no_tag}`. The slicing/caching **algorithm** and the `(slice_data, nested_no_tag)` cache key are UNCHANGED (FR-005) — context is constant within one parse.
- [X] T009 Emitter edit + forced golden regen (FR-005): edit `tools/codegen/fixpp-codegen/emit_messages.cpp:260-270` so the emitted nested-group-accessor call site passes the context argument, then force-regenerate AND **commit** the golden headers so `codegen-build-graph-check` (the `git status --porcelain` cleanliness gate) stays green. Generated `G_<no_tag>` union member sets + accessor semantics UNCHANGED.

**Checkpoint**: Full tree compiles; the T001 baseline suites remain green (context carried-but-ignored); `entry_context` size/trivially-copyable regression passes; C-ABI golden + `capi_freeze.sha256` unchanged. Seam ready — US1 and US2 can begin.

---

## Phase 3: User Story 1 - Context-scoped membership for a reused NumInGroup tag (Defect A) (Priority: P1) 🎯 MVP

**Goal**: Group-membership resolution becomes context-scoped so a reused `NumInGroup` tag (e.g. FIX44 295) resolves to the members of the group **as the message uses it**, not the globally first-declared variant.

**Independent Test**: Load real `FIX44.xml` via the shipped loader, resolve membership for tag 295 in MassQuote context, and assert it includes QuoteEntryID 299 / BidPx 132 / OfferPx 133 (QuotEntryGrp), not the earlier QuotCxlEntriesGrp variant.

### Tests for User Story 1 (write FIRST — mutation-proven RED on pre-fix)

- [ ] T010 [P] [US1] Defect-A discriminating guard in `tests/dictionary/`: load real `FIX44.xml` → `Dictionary::as_table_view()`, assert `group_member_tags(MassQuote-context, 295)` ⊇ {299,132,133} and `group_member_fn(MassQuote-context, 295, 299)=true` (INV-A). Prove RED on the pre-fix bare-`no_tag` key (resolves to QuotCxlEntriesGrp) (SC-003).

### Implementation for User Story 1

- [ ] T011 [US1] Add a **context-keyed membership store** [pin#1] — a per-context side table `(msg_type, bounded parent-no_tag-path, no_tag) → {members, group_first/delimiter}` — in `include/fixpp/dict/table_view.hpp` (re-key `group_members_`/`group_first_` `:215,218` off bare `no_tag`), with a **non-allocating hash strategy** [pin#3-hash] over the `string_view` msg_type + inline path span. `finalize()`'s bare-`no_tag` dedup (`xml_loader.cpp:801`) cannot recover a variant post-finalize, so this store is populated DURING per-message expansion.
- [ ] T012 [US1] Re-key registration in `src/dictionary/xml_loader.cpp:486`: replace first-seen-wins with membership registered under the context key — expand shared `<component>`s PER referencing message (`component_index` `:479` is invisible on the wire) and accumulate the FULL parent-no_tag path (`expand_field_list` threads only the immediate `enclosing_group_no_tag` today, `:529`). Register the **delimiter (`group_first`) under the same context key**, not just the member set.
- [ ] T013 [US1] Thread the context key through `src/dictionary/dictionary.cpp` `as_table_view()` (`:295-357`) + handle group accessors, and widen `table_view` `group_member_tags`/`group_first_field` accessors to take the context args.
- [ ] T014 [US1] Widen the membership-consumer query at `include/fixpp/wire/validator.hpp:219` (`group_member_tags(no_tag)`) to the context key. *(Membership consumer — plan.md:49; do not drop.)*
- [ ] T015 [US1] Wire `group_member_fn` (`parser.hpp:484-494`) and `OffsetTable::group()`'s predicate call to resolve via the stored `group_context` → makes T010 GREEN.
- [ ] T016 [US1] Implement the **loader-faithful (component-expanding) reused-tag census** over ALL NINE runtime XMLs incl. FIXT.1.1 as a committed `tools/` or `tests/` artifact, built to the methodology documented in research.md (loader-faithful component expansion + full parent-path accumulation; worked exemplar tag 295 → parent-0 QuotCxlEntriesGrp vs parent-296 QuotEntryGrp; FIX44=12 collisions, FIX50SP2=21) — the Phase-0 `census2.py` prototype was ephemeral scratch and is NOT in the tree, so build the production census from the research.md spec, not from a checked-in prototype. Emit per row `{dict, no_tag, variant_count, member_sets, contexts}`, plus a **census-derived table-size report** [pin#3-report] across the nine dicts (C-6 completeness aid, NOT a soundness gate — B-004-1).
- [ ] T017 [US1] Add discriminating regression guards for the censused collisions (SC-002), **parameterized per dict-family** (one guard task per family driven by the census output — v42/v44/v50sp2 typed-read witnesses where a flyweight exists; runtime-XML group-access witnesses for FIX40/41/43/50/50SP1/FIXT.1.1). Assert each resolves to its context-correct variant; do not explode into per-tag tasks.
- [ ] T018 [US1] No-alloc test [pin#3-noalloc] for repeated `group_member_fn` lookups (context-keyed store lookup performs zero heap allocation), under the existing alloc/mallocnesia discipline.

**Checkpoint**: Defect A fixed — context-correct membership for every censused collision; T010 + per-collision guards green; C-ABI + baseline unchanged. Independently testable via the real-dict membership assertion (no dependency on US2).

---

## Phase 4: User Story 2 - Nesting-aware outer-slice extent for a multi-instance nested group (Defect B) (Priority: P1)

**Goal**: An outer group instance holding a nested group with N>1 entries computes an extent enclosing all N nested entries — no truncation at the 2nd nested entry's reappearing member tag.

**Independent Test**: With hand-supplied correct membership (isolating Defect B from A), parse an outer instance whose nested group has 2 entries and assert the outer extent spans both (`size()==2`), single-entry / zero-count / flat cases unchanged.

**Depends on**: Phase 2 (seam) + US1 correct membership (the walk uses exact Option-A membership to identify nested count tags). US2's own witnesses use hand-built membership so it is testable in isolation of the real-dict end-to-end (which is US3b).

### Tests for User Story 2 (write FIRST — mutation-proven RED on pre-fix)

- [ ] T019 [P] [US2] Defect-B extent guard in `tests/wire/`: hand-built membership, an outer instance with a nested group of 2 entries → assert extent/`size()==2` with both entries' distinct fields (INV-B). Prove RED on the pre-fix flat `seen_in_instance` walk (truncates to 1) (SC-003).
- [ ] T020 [P] [US2] Regression guards in `tests/wire/`: single-entry nested (no over-consumption past one entry), count-of-zero nested (consumes no extent, B-004-7), flat/non-nested group, and benign same-membership reuse — all unchanged (FR-006 / INV-preserve / C-5).

### Implementation for User Story 2

- [ ] T021 [US2] Replace the flat `seen_in_instance` walk (`src/wire/offset_table.cpp:450-459`) inside `OffsetTable::group()` (`:402-482`) with an **allocation-free**, depth-bounded (`kMaxGroupDepth=16`), stack-only nested-count-aware boundary walk over `entries_`: on a member tag that is a `NumInGroup` count present as a nested group **in this context**, read its declared count, consume exactly `declared` instances or fail closed (malformed/short/overflow), zero-count consumes no extent (B-004-7), apply `cfg_.max_group_entries_per_instance` (`:476`) PER LEVEL. NO heap. → makes T019 GREEN, T020 stays green.
- [ ] T022 [US2] K=16 depth-overflow regression [pin#4]: nesting deeper than `kMaxGroupDepth`, or a parent-path that cannot append, returns the existing `err_group_too_large` (`offset_table.cpp:477`) — observable, fail-closed, and **distinct** from the nested-count-mismatch validation (which stays the validator's job).
- [ ] T023 [US2] Alloc gate: extend `tests/alloc_guard/` + `tests/codegen/group_entry_alloc_gate_test.cpp` to prove the nesting-aware boundary walk allocates zero heap (INV-alloc / SC-005). *(The alloc-gate header already names Defect B as deferred-to-063.)*
- [ ] T024 [US2] Fuzz: extend `tests/fuzz/fuzz_wire_nested_slice.cpp` to cover the nesting-aware walk (parser-touching → `[const Art VII §7]`).
- [ ] T025 [US2] C-ABI last-nested-instance-extent MassQuote witness [pin#5] in `tests/capi/`: a MassQuote-shaped frame with ≥1 outer member AFTER a multi-entry nested group, asserting the last nested instance's extent does NOT absorb trailing outer members (the newly-activated delimiter-bounded multi-instance branch `message_read.cpp:549-591`). **If the witness shows divergence from the C++ membership-bounded path, apply the small C-ABI extent fix** (plan.md:104 — task allows the fix, not just the assertion).

**Checkpoint**: Defect B fixed — multi-entry nested extents correct; regressions + K=16 overflow + alloc gate + fuzz + C-ABI witness green. US1 + US2 both independently functional.

---

## Phase 5: User Story 3 - Deferred 062 witness turns green; L-062 cleared (Priority: P2)

**Goal**: The un-skipped hand-built Defect-B witness (SC-001a) + a net-new real-dictionary A+B witness (SC-001b) both green, and the two `L-062` limitation rows retired — proving both defects end-to-end through the shipped read path and unblocking feature 061.

**Independent Test**: `nested_group_read_test.cpp:353` un-skipped passes; a real-`FIX44.xml` `as_table_view()` MassQuote with 2 QuoteEntries reads `.size()==2` with both entries' exact 299/132/133.

**Depends on**: US1 (Defect A) + US2 (Defect B).

### Tests for User Story 3

- [ ] T026 [P] [US3] SC-001a: remove the `GTEST_SKIP()` from `tests/codegen/nested_group_read_test.cpp:353` `NestedQuoteEntriesPerInstancePrices` **UNCHANGED** (hand-built `make_correct_massquote_dict()`) and confirm it passes green — Defect B proven in isolation.
- [ ] T027 [P] [US3] SC-001b: ADD a net-new real-`Dictionary::as_table_view()` witness in `tests/codegen/nested_group_read_test.cpp` — load real `FIX44.xml`, parse a MassQuote with a QuoteSet holding 2 QuoteEntries, assert `quote_entries().size()==2` and both entries' exact 299/132/133 (Defect A + B end-to-end, C-3).

### Close-out for User Story 3

- [ ] T028 [US3] Verify C-ABI stability (SC-005 / C-4): `ctest -R "abi_symbol_golden|capi_freeze"` passes unchanged — no exported group symbol added/changed, while `fixpp_msg_get_group`'s nested-group output is now correct.
- [ ] T029 [US3] Retire the two `L-062` rows (A: reused-tag wrong membership; B: multi-entry nested truncation) in `spec/behaviors-and-limitations.md`, marked resolved with this feature's evidence reference (FR-007 / SC-004).

**Checkpoint**: Both defects proven end-to-end; L-062 cleared; feature 061 unblocked.

---

## Phase 6: Polish & Cross-Cutting Concerns

**Purpose**: Final validation and the mandatory close-out.

- [ ] T030 Run the quickstart.md §3 validation end-to-end (build + dictionary/wire/codegen/nested_group/group_slice/alloc suites + alloc gate + C-ABI freeze) and confirm all expected GREEN/unchanged.

### Mandatory close-out tasks (ALWAYS emit — Gate-B preconditions, Article XVII §8)

- [ ] T031 [P] **Catalogue close-out**: 063 is a bugfix with no feature-owned OFFICIAL `spec/feature-catalogue.md` row (matches the 062/169 bugfix precedent — neither owns a row). It CORRECTS existing repeating-group rows **W-006/W-007** (nested groups) + **W-014** (validator), already `done` under `004-wire-codec`/`041` — so append the 063 evidence PR reference to those rows' Evidence column (bidirectional traceability, `[const Art VI §4]`) AND add/update the matching `spec/coverage-index.md` entry. If the T032 audit surfaces a genuinely feature-owned row, flip it `→ done` instead. (Do not fabricate a `063` row where none is owned.)
- [ ] T032 **Feature-completeness audit (FINAL task)**: assert against the merged tree that (i) every `tasks.md` row is `[X]` or carries an explicit waiver rationale; (ii) every spec FR-001…FR-007 and SC-001a…SC-005 maps to a landed test AND a landed implementation; (iii) every feature-owned OFFICIAL catalogue row is `done` with a matching `coverage-index.md` entry. Record the verdict (100% or fully-waived) in `.specify/decisions/063-nested-group-parse-correctness-verify.md` (`## Completeness`) OR a sibling `.specify/decisions/063-nested-group-parse-correctness-completeness.md`. HARD `/gate-b` precondition (Article XVII §8 / pre-flight 4d).

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: no dependencies.
- **Foundational (Phase 2)**: depends on Setup — BLOCKS US1 and US2 (the shared context seam).
- **US1 (Phase 3, P1 MVP)**: depends on Foundational.
- **US2 (Phase 4, P1)**: depends on Foundational **and US1** (the walk needs exact Option-A membership to identify nested count tags — spec Clarification "A first, or together"). US2's own witnesses use hand-built membership so it is verifiable in isolation of the real-dict end-to-end.
- **US3 (Phase 5, P2)**: depends on US1 + US2.
- **Polish (Phase 6)**: depends on all stories.

### Within Each User Story

- Test tasks (mutation-proven RED) precede implementation; each defect goes red→green (SC-003).
- US1: store shape → loader re-key → table_view/dictionary/validator threading → predicate wiring (green) → census → per-collision guards → no-alloc test.
- US2: RED extent + regression guards → nesting-aware walk (green) → K=16 overflow → alloc gate → fuzz → C-ABI witness.

### Parallel Opportunities

- T005 (entry_context field, `group_view.hpp`) can proceed alongside T006 groundwork once T004 lands `[P]`.
- Within US1, T010 (test) is `[P]`; within US2, T019/T020 (tests) are `[P]`; within US3, T026/T027 are `[P]` (distinct assertions).
- T031 (catalogue) is `[P]` vs T030; T032 is the FINAL task (no parallelism).

---

## Implementation Strategy

### MVP First

1. Phase 1 Setup → 2. Phase 2 Foundational (the seam — CRITICAL) → 3. US1 (Defect A membership) → **STOP & VALIDATE** the real-dict 295 membership + census guards.

### Incremental Delivery

Foundational → US1 (Defect A) → US2 (Defect B) → US3 (end-to-end witnesses + L-062 retire) → Polish. Each story adds correctness without regressing the prior (regression-guarded per C-5).

---

## Notes

- The two defects are coupled: B's walk consumes A's context-scoped membership; the shared `group_context` seam (Phase 2) is why the plumbing is foundational rather than per-story.
- FR-005 boundary: 062's slicing/caching **algorithm** + cache keying/eviction + generated `G_<no_tag>` union member sets + accessor semantics are UNTOUCHED; only the nested-descent call site gains the context arg (emitter edit + forced golden regen, T009).
- C-ABI stays byte-identical (positional/transitive nested path) — no cursor/ABI redesign (Gate A Round-2 rejected Codex's cursor-field remedy).
- Each `[pin#N]` annotation traces a Round-2 Gate-A tasks-pin so `/gate-b` can confirm discharge.
