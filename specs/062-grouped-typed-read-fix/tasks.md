---
description: "Task list — Grouped Typed-Read Path Fix (062)"
---

# Tasks: Grouped Typed-Read Path Fix (062)

**Input**: Design documents from `specs/062-grouped-typed-read-fix/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/group-entry-read.md, quickstart.md

**Tests**: REQUESTED. FR-006 mandates discriminating witnesses over **generated** flyweights + a regression guard; SC-002 mandates a sanitizer-matrix + allocation-gate run. Test tasks are therefore in scope and, per the pipeline's TDD ordering, are authored before (or alongside) the implementation they pin. Note: several witnesses **cannot compile** until the fix lands — that non-compiling state *is* their initial RED (it is the exact symptom 062 removes), so "write test → observe it fail" holds by construction.

**Organization**: Grouped by user story. All three stories are **P1** (the feature is indivisible: the compile blocker is only truly discharged when scalar reads, nested reads, and the lifetime/alloc contract all hold). US1 is the MVP slice; US2/US3 complete the contract.

**Repo root for all paths**: the library submodule `research/G19-fix-fpml-iso20022/library/` (all paths below are relative to it).

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependency on an incomplete task).
- **[Story]**: US1 / US2 / US3 (Setup, Foundational, Polish carry no story label).

---

## Phase 1: Setup (baseline + regen discipline)

**Purpose**: Establish a trustworthy green baseline and the forced-regen procedure so later codegen diffs are attributable.

- [X] T001 Build the debug preset and run the existing `tests/wire/` + `tests/codegen/` suites GREEN on branch `062-grouped-typed-read-fix` (clean `build/<preset>` — no stale objects, per the stale-object false-green lesson). Capture the **pre-fix witness state**: confirm a `msg.<group>()[0].<field>()` read on a *generated* flyweight does NOT compile today (the blocker 062 removes). Record the baseline command + result inline in the PR notes / phase-4 sub-file.

**Checkpoint**: Known-good baseline; the regression the fix targets is reproduced.

---

## Phase 2: Foundational (Blocking Prerequisites — the enabling wire seams)

**Purpose**: The shared wire primitives every user story depends on. Per plan §Implementation Sequencing steps 1–2. **No US1/US2/US3 work can begin until this phase is complete.**

**⚠️ CRITICAL**: These seams are the load-bearing change; the codegen edit in US1/US2 is a thin consumer of them.

- [X] T002 Add the `entry_context` struct (NEW) in `include/fixpp/wire/group_view.hpp` (or `include/fixpp/wire/view.hpp` if placement is cleaner): `{ std::span<const std::byte> span; std::pmr::memory_resource* mr; void const* opaque_dict; OffsetTable::group_member_fn_t group_member_fn; detail::generation_token gen; OffsetTable* parent_cache_owner; <outer_occurrence_id> }` — trivially copyable, no allocation (data-model §entry read context, RC2/N1/N2). `outer_occurrence_id` = the entry slice's globally-unique `data` pointer identity.
- [X] T003 [P] Add the **`frame_view`-over-slice friend-seam** in `include/fixpp/wire/framer.hpp` (+ `view.hpp` if needed): mint a `frame_view` from `{slice.data, len(+1)}` via the `frame_view_access` friend precedent (`src/capi/message_write.cpp:63-74`) WITHOUT widening the public `frame_view` ctor (Complexity-Tracking row 2).
- [X] T004 [P] Add the **span-scan → token-bearing `field_view` helper** `get(std::span<const std::byte>, std::uint16_t tag, detail::generation_token) -> core::expected_t<field_view>` in `include/fixpp/wire/parser.hpp` (N1): reuse `field_iterator` to locate the tag within the slice and mint a `field_view` carrying the generation token via `field_view_access::make` (mirror `MessageView::get`, minus the OffsetTable). No sub-index, zero heap allocation. Tolerates a missing final SOH (`field_iterator` `end==size` fall-through).
- [X] T005 Add the **dict-aware sub-view-over-slice builder** in `include/fixpp/wire/offset_table.hpp` + `src/wire/offset_table.cpp`: build a sub-`OffsetTable` over the **slice-scoped `{data, len+1}`** via the dict-aware ctor (threads `opaque_dict` + `group_member_fn`). The whole-frame `OffsetTable::build()` guard (`offset_table.cpp:264-268`) AND the shared `group_slice.len` are **UNCHANGED** (RC1). The dict-aware path is MANDATORY (never the dict-free fallback — INV-G7).
- [X] T006 Add the **single flat nested sub-view cache** on the **ROOT** `OffsetTable` in `include/fixpp/wire/offset_table.hpp` + `src/wire/offset_table.cpp`: a per-message-arena map keyed `(unique_slice_identity = outer slice `data` ptr, nested_no_tag)` with build-once / fetch-cached semantics; reached via `entry_context.parent_cache_owner` (points at the root, threaded **unchanged** at every depth — nested sub-tables own no cache; RC2/RC5, INV-G3). Depends on T005 (builder) + T002 (context).
- [X] T007 **[RELOCATED → lands atomically with T012 in US1]** — `operator[]` cannot build `GroupT{span,ctx}` until the generated `G_n` has the `entry_context` ctor (T012); splitting them breaks the build mid-phase (symmetric-interface-halves rule). Thread the `entry_context` through the read chain (depends on T002–T006):
  - `include/fixpp/wire/group_view.hpp`: `group_view<G>` carries the `entry_context`; `operator[]`/`iterator::operator*` construct `GroupT` from `{slice_span, context}` and derive `outer_occurrence_id` from `slice.data` — **same `i` from both paths → seam-#8 preserved** (INV-G4, FR-003).
  - `include/fixpp/wire/parser.hpp`: `MessageView::group<>()` populates the `entry_context` (span, `mr`, `opaque_dict`, `group_member_fn`, generation token from the `View` base, `parent_cache_owner` = this root `OffsetTable`) into the `group_view`.
- [X] T008 [P] Create `tests/wire/group_slice_trailing_soh_test.cpp` with `WholeFrameParseUnchanged` (top-level parse of a checksum-terminated frame is byte-for-byte unaffected) + `NestedSliceBuildCountedLastField` (a slice-scoped `len+1` build over an entry whose last field is a counted Length+Data field succeeds — the terminal SOH is present in the parent buffer at `data+len`) + `OversizedCountPerInstanceCapPreserved` (FR-006 group-cap / oversized-count no-regression edge, spec.md:73 — an entry count exceeding the existing `OffsetTable::Config` per-instance cap yields the same fail-closed behaviour as before, no regression from the entry-read change); register the target in `tests/wire/CMakeLists.txt`. This is the seam's own RED→GREEN witness (RC1 / FR-006 / FR-007). Depends on T003, T005.

**Checkpoint**: Wire seams compile; `WholeFrameParseUnchanged` + `NestedSliceBuildCountedLastField` pass; the codegen consumer can now be written.

---

## Phase 3: User Story 1 - Read a repeating-group entry's typed fields (Priority: P1) 🎯 MVP

**Goal**: Typed **scalar** reads (string/char/int/decimal + `field_value`) on a *generated* group-entry flyweight compile and return exact per-instance wire values, with fail-closed errors for absent fields.

**Independent Test**: Parse a real frame with a repeating group; `msg.<group>()[i].<field>()` returns the exact wire value for each `i`; an absent field returns the typed not-found error — over a **generated** flyweight.

### Tests for User Story 1 (author first — initially fail / do not compile)

- [ ] T009 [P] [US1] Create `tests/codegen/group_entry_read_test.cpp` with `OneLevelScalarAndDecimalReadExactValues` (US1 AC1 / FR-001 / FR-005 / SC-001) — pin this witness to a **named** one-level grouped message, **NewOrderList `orders`** (`G_73`), which is distinct from the nested MassQuote case in T015 so SC-001's "≥2 distinct grouped messages" numeral is met unambiguously; `AbsentEntryFieldReturnsTypedError` (US1 AC2 / FR-001 — reads the *absent* arm, not a present field), `AbsentVsPresentButEmptyField` (edge), `EmptyGroupSizeZeroNoDeref` (edge — `size()==0`, `begin()==end()`), `LastEntryDelimiterExtentExact` (edge — single/last-entry delimiter extent, spec.md:71); register the target in `tests/codegen/CMakeLists.txt` (append the stem to `FIXPP_CODEGEN_TESTS`).
- [ ] T010 [P] [US1] Extend `tests/wire/repeating_group_equivalence_test.cpp` with `GeneratedFlyweightOperatorEqualsIter` (US1 AC3 / FR-003 — `operator[]` == `iter()` enumerated over a **generated** flyweight, not the hand-written `TestLeg`).
- [ ] T011 [P] [US1] Extend `tests/codegen/typed_accessor_test.cpp` with `GeneratedEntryOperatorSubscriptInstantiates` — the FR-006 / SC-003 regression guard: instantiate `operator[]`/`iter()` on a generated entry and read a field so a revert re-breaks the build/test (compile-level assertion, not a silently-skipped path).

### Implementation for User Story 1

- [X] T012 [US1] `tools/codegen/fixpp-codegen/emit_messages.cpp` — `emit_group_class`: change the entry class `G_<no_tag>` to store `entry_context ctx_` (was `MessageView<Index> const* view_`); rewrite the **scalar** accessors (`emit_scalar`) and `field_value` (`emit_field_value`) ptr-branch to read via the T004 span-scan helper over `ctx_.span` then `decode_field` / `decimal_t::parse`. The **message-level** flyweight (`MessageView const&` by reference) is UNTOUCHED (research §Entry storage & ripple). Nested accessor is US2 (T016) — leave a stub/TODO that still compiles.
- [X] T013 [US1] Forced regen: rebuild the `fixpp-codegen` tool, clear the `build/<preset>/_codegen` markers, reconfigure to regenerate `Messages.hpp`, and update the golden `specs/003-dictionary-codegen/contracts/golden/v44_Messages.golden.hpp`; confirm the golden diff is intentional and confined to the entry-class body (FR-005 determinism; `Codegen.cmake` is blind to emitter edits). Depends on T012.
- [ ] T014 [US1] Build + run the US1 witnesses (T009–T011) GREEN over the regenerated flyweights.

**Checkpoint**: One-level scalar entry reads compile and return exact values over generated flyweights; the regression guard is armed. MVP is demonstrable.

---

## Phase 4: User Story 2 - Read a nested repeating group inside an entry (Priority: P1)

**Goal**: An entry exposes a repeating group nested inside it with the same typed accessors, recursively — QuickFIX C++/J parity (MassQuote quote-entry prices).

**Independent Test**: Parse a message whose entry contains a nested group; `entry.<nestedGroup>()[j].<field>()` returns correct per-instance values, including from a **non-first** outer occurrence.

### Tests for User Story 2 (author first — initially fail)

- [ ] T015 [P] [US2] Create `tests/codegen/nested_group_read_test.cpp` with `NestedQuoteEntriesPerInstancePrices` (US2 AC1 / FR-002 / SC-001 — MassQuote `NoQuoteSets → NoQuoteEntries`, per-instance BidPx/OfferPx), `Depth3NonFirstOuterOccurrenceNoCollision` (FR-002 / INV-G3 — reads `NoQuoteSets[1] → NoQuoteEntries[0] → NoLegs[k]` and asserts its leg value differs from `NoQuoteSets[0]`'s, proving the slice-identity cache key does NOT collide across repeated outer occurrences — targets `[1]` because an ordinal-`i` key would pass for `[0]`), and **`NonLastNestedGroupTrailingFieldNotSwallowed`** (INV-G7 discriminator — reads an outer-entry scalar that sits **after** the nested group in wire order, then reads the nested group, asserting BOTH are exact; the dict-free fallback sets `group_end = entries_.size()` (`offset_table.cpp:436-439`) and would swallow the trailing outer field, so this witness FAILS unless the mandatory dict-aware slicer is used — the MassQuote nested-group-is-last layout cannot distinguish the two paths, so use a frame layout / synthetic-but-generated-flyweight entry where a field follows the nested group); register the target in `tests/codegen/CMakeLists.txt`.

### Implementation for User Story 2

- [X] T016 [US2] `tools/codegen/fixpp-codegen/emit_messages.cpp` — `emit_group_class` nested accessor `group<c, G_c>()`: build a **dict-aware sub-view over `ctx_.span` lazily**, keyed + cached by `(ctx_.outer_occurrence_id, nested_no_tag)` in the root flat cache (T006); return its `group<c>()` with a **recursively-threaded** `entry_context` whose own `outer_occurrence_id` is the child slice's `data` identity and whose `parent_cache_owner` stays the root (N2, INV-G3). Depends on T012 (entry now stores `ctx_`), T005, T006.
- [X] T017 [US2] Forced regen + golden update (as T013) for the nested accessor change; confirm the diff is confined and intentional. Depends on T016.
- [ ] T018 [US2] Build + run the US2 witnesses (T015) GREEN, including the depth-3 non-first-occurrence discriminator.

**Checkpoint**: Nested typed reads work recursively to depth-3/4 with a collision-free cache key.

---

## Phase 5: User Story 3 - Entry lifetime is safe and non-degrading (Priority: P1)

**Goal**: Entries borrow the parent message (no dangling while parent alive); one-level scalar reads allocate zero; nested descent builds at most one bounded arena sub-view per stable outer occurrence (zero on repeat).

**Independent Test**: Under ASan/UBSan/TSan + an allocation-tracking gate, iterate a group, hold and read entries — no UAF, and the allocation profile matches FR-004/FR-004a/FR-004b.

### Tests for User Story 3

- [ ] T019 [P] [US3] Create `tests/codegen/group_entry_alloc_gate_test.cpp` with `OneLevelScalarZeroAlloc` (FR-004/FR-004a — zero heap alloc + no sub-index on one-level scalar reads) and `NestedFirstDescentBoundedRepeatZero` (FR-004b/SC-002 — first nested descent = one bounded arena build per stable occurrence, repeat descent = zero). Gate BOTH with a counting resource AND global-malloc interception (mallocnesia `tools/mallocnesia/libmallocnesia.so`) so a non-PMR escape via global `new` cannot false-pass; register in `tests/codegen/CMakeLists.txt`.
- [ ] T019b [P] [US3] Create/extend a **generation-token trap** witness `GenerationTokenTrapOnStaleEntryRead` (INV-G6) proving no entry read runs under a default `{}` token: in a debug build, mint an entry (and a nested sub-view) from a parsed message, bump the parent pool generation (invalidate the arena/frame), and assert the subsequent entry scalar read + nested descent **trap** (the `[2b §6.4]` armed generation trap fires). This CANNOT be caught by the T021 sanitizer matrix (a default token is `pool_id==0` = "untracked, never traps"), so it needs this dedicated discriminator — reuse the existing flyweight generation-trap test pattern (`view.hpp` `generation_token`). If the debug-trap machinery cannot express this over a group entry, record the residual explicitly in the T026 completeness audit rather than silently dropping it. Place alongside the alloc gate in `tests/codegen/group_entry_alloc_gate_test.cpp` (or a sibling `group_entry_generation_trap_test.cpp` registered in `tests/codegen/CMakeLists.txt`).
- [ ] T020 [US3] Confirm the INV-G1 lifetime **doc-witness** obligation is satisfied: `data-model.md` §Invariants INV-G1 + `contracts/group-entry-read.md` §Lifetime & stability document that an entry (and any nested group/entry) is valid only while the parent message is alive (US3 AC2 — documentation obligation, not a runtime UAF test). (No code; verification-only.)

### Verification for User Story 3

- [ ] T021 [US3] Run the full 062 witness set (T008–T019b) under the ASan / UBSan / TSan matrix and the allocation gate; assert SC-002: one-level scalar zero-alloc, nested cached-once/repeat-zero, sanitizers clean while the parent message is alive.

**Checkpoint**: Lifetime + allocation contract proven under the sanitizer + alloc-gate matrix.

---

## Phase 6: Polish & Cross-Cutting Concerns

**Purpose**: FR-007 no-regression guards, operator-facing limitation record, and the mandatory close-out.

- [ ] T022 [P] FR-007 / SC-004 no-regression guards: create `tests/capi/abi_symbol_golden_test.cpp` (`CabiSymbolSetUnchanged` via `nm`/abidiff against the frozen C-ABI symbol set + `ErrorEnumUnchanged`) and `tests/wire/toplevel_read_regression_test.cpp` (`TopLevelNonGroupReadUnchanged` — top-level/non-group message field reads are byte-identical post-regen); register both targets in their `CMakeLists.txt`.
- [ ] T023 Run the full **Tier-1** suite GREEN after codegen regeneration (SC-004 — no downstream regression from the regenerated headers).
- [ ] T024 [P] Add the **N3 limitation** row to `spec/behaviors-and-limitations.md`: the entry `field_value(tag)` escape hatch span-scans the whole entry slice (including a nested group's bytes) and returns the FIRST occurrence — a tag living ONLY inside a nested group is returned as if an outer-entry field (mirrors whole-message `field_value` first-occurrence semantics; typed accessors are codegen-scoped and unaffected). Record as an `L-062-*` row.

- [ ] T024b [P] Fuzz the **new input shape** (E1 / Article VII §7 posture): 062 feeds a **non-enveloped, mid-frame slice-scoped `{data, len+1}`** byte range into `OffsetTable::build()` (the T005 dict-aware sub-view builder) — a shape the existing `tests/fuzz/fuzz_wire_parser.cpp` (full-frame only, minted via `frame_view_access`) never generates. Add a fuzz seed/target (extend `fuzz_wire_parser.cpp` or add a sibling) that drives arbitrary bytes through the slice-scoped nested-build entry point, so a malformed mid-frame slice cannot OOB/UB the new path. Run before Gate B.

### Mandatory close-out tasks (ALWAYS emit — Gate-B preconditions, Article XVII §8)

- [ ] T025 [P] **Catalogue close-out**: 062 is an **enabling/mechanism** feature and owns **NO** OFFICIAL `spec/feature-catalogue.md` rows (per spec §Normative References — the A-001..013 / M-001..012 / P-001..003 rows it *unblocks* are closed by feature **061**, not here). Record this exemption explicitly (do NOT silently skip), and add a `spec/coverage-index.md` **mechanism** entry for 062 pointing at the enabling seams (span-scan helper, dict-aware slice builder, nested cache) + the FR-006 witnesses, so the enabling capability is traceable. (057-precedent analog.)
- [ ] T026 **Feature-completeness audit (FINAL task)**: assert against the merged tree that (i) every `tasks.md` row is `[X]` or carries an explicit waiver rationale; (ii) every spec **FR-001..FR-007** and **SC-001..SC-004** maps to a landed test AND a landed implementation (use the plan §Acceptance → Witness Map); (iii) the catalogue-row exemption from T025 is recorded with rationale. Write the verdict (100% or fully-waived) to `.specify/decisions/062-grouped-typed-read-fix-completeness.md` (or the `## Completeness` section of the `-verify.md` decision doc). Hard `/gate-b` pre-flight 4d precondition.

---

## Dependencies & Execution Order

### Phase dependencies

- **Setup (T001)**: no dependencies.
- **Foundational (T002–T008)**: after Setup; **BLOCKS all user stories**. Internal order: T002 (entry_context) → T003/T004 [P] → T005 → T006 → T007 → T008 [P after T003/T005].
- **US1 (T009–T014)**: after Foundational. Tests T009–T011 [P] first; impl T012 → T013 → T014.
- **US2 (T015–T018)**: after US1 (extends the same entry class in `emit_messages.cpp`; T016 builds on T012's `ctx_`). Test T015 first; T016 → T017 → T018.
- **US3 (T019–T021)**: after US2 (alloc gate exercises both one-level + nested paths). T019 + T019b [P] first; T020 [P doc]; T021 runs the full set after T019/T019b.
- **Polish (T022–T026)**: after all stories. T024b (fuzz) after T005/T016 (the slice-build path it fuzzes). T026 is the FINAL task.

### Codegen serialization

- T012 and T016 both edit `tools/codegen/fixpp-codegen/emit_messages.cpp` → **NOT parallel** with each other. Each is followed by its own forced-regen + golden update (T013, T017).

### Parallel opportunities

- Foundational: T003 + T004 in parallel (distinct headers); T008 in parallel once T003/T005 land.
- US1 tests T009 + T010 + T011 in parallel (distinct files).
- US3 tests T019 + T019b in parallel (distinct/sibling files).
- Polish: T022 + T024 + T024b + T025 in parallel (distinct files).

---

## Implementation Strategy

### MVP (User Story 1 only)

1. Setup (T001) → Foundational (T002–T008, the load-bearing seams).
2. US1 (T009–T014): one-level scalar reads over generated flyweights + regression guard.
3. **STOP & VALIDATE**: `group_entry_read_test` + the equivalence + regression-guard suites green. This alone unblocks the non-nested subset of 061's grouped messages.

### Incremental delivery

- + US2 (nested reads) → MassQuote depth-3/4 parity.
- + US3 (lifetime + alloc gate) → the full FR-004 discipline proven.
- + Polish (FR-007 guards, L-062 limitation, close-outs) → Gate-B-ready.

### Notes

- Tests here fail by **not compiling** pre-fix — that is the intended initial RED (the exact symptom the fix removes).
- Forced-regen discipline is mandatory after every `emit_messages.cpp` edit (rebuild tool + clear `_codegen` markers) — the `Codegen.cmake` configure-time graph is blind to emitter source edits.
- Keep each codegen edit confined to `emit_group_class` + the ptr-branch of `emit_scalar`/`emit_field_value`; do not ripple into the message-level flyweight.
- The whole-frame `OffsetTable::build()` guard and `group_slice.len` are UNCHANGED throughout (RC1) — only the nested build reads a slice-scoped `len+1`.
