---
description: "Task list — 072-nested-group-hardening"
---

# Tasks: Harden doubly-nested repeating-group correctness (census disjointness + load guard + typed depth-≥2 context)

**Input**: Design documents from `specs/072-nested-group-hardening/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/ (load-guard.md, typed-context.md), quickstart.md — all read.

**Tests**: REQUIRED (TDD red-green mandatory, Art. VII §3). Every fix lands behind a witness proven RED first. Census/load-reject/depth-3/validator witnesses are all mutation- or trip-proven per the docs.

**Organization**: By user story (both P1, fully independent — no shared file or fixture). FR-010's validator rewrite is carried in its **own severable phase (Phase 5)** so the `/implement`-time SPLIT-TRIGGER is a clean "drop the phase," leaving US1 + the accessor half of US2 independently shippable.

**cwd for all commands**: `research/G19-fix-fpml-iso20022/library`.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependency on an incomplete task)
- **[Story]**: US1 / US2 (Phase 5 FR-010 is cross-cutting to US2's validation surface, not in US2's independent test — no story label)
- Exact file paths included.

---

## Phase 1: Setup

**Purpose**: Baseline the tree so the no-golden codegen model and TDD reds are trustworthy.

- [X] T001 Confirm a configured build tree in the submodule and a green baseline `ctest -L dictionary` + `ctest -L wire` before any change, so later REDs are attributable to the new witnesses (not a pre-existing break). Note the **no-checked-in-golden** codegen model (`cmake/Codegen.cmake` — flyweights generated into `${CMAKE_BINARY_DIR}/_codegen/` at configure time, gated by the emitter-source SHA256 fingerprint): the Part B emitter change is verified by clean reconfigure, NOT a golden diff (research D-B5, SC-005).

---

## Phase 2: Foundational (Blocking Prerequisites)

**No shared foundational work.** US1 (census + load guard, `dict/` + census test) and US2 (typed emitter accessor, codegen + `nested_group_read_test.cpp`) are both P1 and **fully independent** — no shared source file, no shared fixture (US1's colliding dialect goes through `XmlLoader::load_*`; US2's hand-built `table_view` bypasses it — spec Edge Cases / research D-B6). Either can start immediately after Setup. This phase is deliberately empty.

---

## Phase 3: User Story 1 — dialect rejected at load; shipped dicts pinned clean (Priority: P1) 🎯 MVP

**Goal**: Convert two unenforced structural conventions into (a) a load-time **reject** for the nested==parent delimiter collision (dialect-XML path) and (b) two permanent CI-tripping census assertions pinning today's clean shipped state.

**Independent Test**: A hand-authored dialect XML whose nested group's `first_field_tag` equals its parent's is rejected at `XmlLoader::load_*` with a catchable typed error and produces no usable view; separately, the census assertions over all 9 vendored dicts pass (0 delimiter collisions, 0 shared parent/child scalar members), non-vacuously.

### Tests for User Story 1 (write FIRST, prove RED before implementation)

- [X] T002 [P] [US1] In `tests/dictionary/reused_tag_census_test.cpp`, add the **FR-001 delimiter-disjointness** census assertion over `kRuntimeDicts` (all 9). Derive it from the **raw per-`<group>` walk** (`walk_groups`/`delimiter_scan_for`, already in the file) **extended three ways** per research D-A3 / contracts/load-guard.md: (i) thread the immediate-enclosing-group's delimiter down the recursive walk; (ii) expand `<component>` refs so cross-component nesting (e.g. FIX44 `NoQuoteEntries(295)→InstrmtLegGrp→NoLegs(555)`) is parent-linked; (iii) resolve **each group's own delimiter post-component-expansion** (first field of the fully-expanded member list, mirroring `expand_field_list`), NOT the first literal `<field>` child — do NOT source the delimiter from the first-seen-deduped `groups_`. Assert **non-vacuous coverage** (`> 0` group-declaration sites observed per dict — mirrors the existing `EXPECT_TRUE(saw_fix44_295_collision)` hard invariant at `:281`). **Prove RED first**: on a throwaway inline XML injecting a nested==parent delimiter, the assertion logic must flag it; then it is GREEN over the 9 shipped dicts.
- [X] T003 [P] [US1] In the same `tests/dictionary/reused_tag_census_test.cpp`, add the **FR-002 scalar-member-disjointness** census assertion: no scalar member tag shared between a parent group and its nested child, per-group member sets, for every dict it claims to cover; assert `> 0` member-sets examined (non-vacuous). Where per-group member sets are NOT structurally recoverable for the `INT`-typed-count dicts **FIX40/41/42**, record them as an **explicit unpinned residual** (do NOT let them pass vacuously as covered) — FR-013d. **Prove RED first** on an injected shared parent/child scalar member; GREEN over shipped dicts.
- [X] T004 [P] [US1] Add the **load-rejection witness** in `tests/dictionary/` (new file `test_072_delimiter_guard_test.cpp`): (a) a small inline XML declaring a nested group whose `first_field_tag` equals its parent's, loaded via `XmlLoader{}.load_from_string(xml, &arena)`, MUST throw `dict::group_delimiter_collision_error` (catchable as `dict::xml_parse_error`), produce **no** view, and not crash/UB/mis-split — RED until the guard (T006) exists; (b) a conforming variant loads fine; **(c) FR-004 asymmetry negative witness** — a dialect that shares a **scalar member** tag between a parent group and its nested child but keeps **disjoint delimiters** MUST **load successfully** (no throw), pinning that the load guard enforces the delimiter convention ONLY and never the scalar-member case (which stays census-only, FR-002). (SC-002 / FR-004.)

### Implementation for User Story 1

- [X] T005 [P] [US1] Add the new error type `dict::group_delimiter_collision_error` in `include/fixpp/dict/error.hpp`, **deriving from `dict::xml_parse_error`**, carrying a message naming the offending `no_tag`, its delimiter, and the parent's delimiter. **Negative constraints (do NOT violate — research D-A1):** it **reuses the inherited `code()`** (`dict_xml_parse_failed`); it does **NOT** append a `fixpp::core::error` variant; this task touches **neither** `include/fixpp/core/error.hpp` **nor** `tests/core/test_020_error_completeness.cpp` (an append would break the no-`default` `error_message()` `-Wswitch`/`-Werror` build + trip `test_020` slot-132). Discrimination is by catch type only (`code()` is non-virtual). Also amend the `error.hpp` top-of-file doc comment: note that a type **deriving from an existing exception and reusing its inherited `code()`** (discriminated by catch type) is a valid alternative to the "append a matching `fixpp::core::error` variant" pattern the comment currently states as the general invariant — so the new type does not read as a silent violation of that comment.
- [X] T006 [US1] In `src/dictionary/xml_loader.cpp` `LoaderState::finalize()` (after `groups_` is populated, `~:633`), add the parent-chain walk: for each `GroupDef` with `parent_group_no_tag != 0`, look up the parent via `group_index_by_no_tag_` and, if `first_field_tag == parent.first_field_tag`, **throw** `dict::group_delimiter_collision_error`. Reject the whole load (not silent-skip); happens before any `table_view` is built; `as_table_view()` stays allocation-only/non-throwing. Makes T004 GREEN. (FR-003/FR-005.)
- [X] T007 [US1] Wire the FR-001/FR-002 census helpers to run over all 9 `kRuntimeDicts` and confirm GREEN (0 collisions, non-vacuous), and confirm every shipped dict still loads unchanged through the guarded path (FR-006). Run `ctest -L dictionary` (grouped): `reused_tag_census` + the new guard witness green.

**Checkpoint**: US1 fully functional and independently testable — MVP (closes the public-API → silent-mis-split path).

---

## Phase 4: User Story 2 — grandchild (depth-3) member reads under its full parent path (Priority: P1)

**Goal**: Fix the **typed** accessor to push the nested group's own `no_tag` onto the returned child view's **stored** context at the emitter mint, so a depth-3 grandchild read resolves under the full path — reconciling the typed path *up* to 065's already-correct C-ABI cursor (never un-pushing it).

**Independent Test**: A depth-3 typed read of a `v44::MassQuote` `legs()` (555) member over a hand-built `table_view` (divergent context-vs-bare registration at 555) resolves the context-scoped member post-fix; mutation-proven RED on the pre-fix emitter output.

### Tests for User Story 2 (write FIRST, mutation-proven RED before implementation)

- [ ] T008 [P] [US2] Add the **depth-3 discrimination witness** in `tests/codegen/nested_group_read_test.cpp` (extends the existing depth-2 pattern `RealDictionaryMassQuote296RootContextSeededAtCtorNoCachePoison` one level deeper): real generated `v44::MassQuote` (`296→295→555`) driven by a **hand-built `table_view`** whose grandchild group `555` context store is registered `add_group_member_ctx("i", [296,295], 555, …)` — under the **wire `MsgType` value `"i"`** (NOT "MassQuote", else the read misses the store and false-greens — research D-B6 / data-model NEW-3) — with a member set that DIFFERS from the legacy bare `add_group_member(555, …)` store. Assert the depth-3 `legs()` read resolves the context-scoped (correct) member under lookup key `("i",[296,295],555)`; the returned view then stores `[296,295,555]` for further descent. **Mutation-prove RED on the pre-fix (unpushed) emitter output** (pre-fix reads the wrong bare-fallback member). SHOULD also assert typed≡C-ABI depth-3 agreement if a C-ABI depth-3 read is constructible on the same hand-built dict (SC-004). Bypasses `XmlLoader::load_*` (independent of US1).

### Implementation for User Story 2

- [ ] T009 [US2] Apply the **one push site** at `tools/codegen/fixpp-codegen/emit_messages.cpp:270-271`: the emitted `group_view<G_c>` base context becomes `ctx_` with `group_ctx = ctx_.group_ctx.pushed(<c>)` (c = nested `no_tag`, already in scope from the `:265` slices arg). **Keep unchanged**: (i) the `nested_group_slices(...)` **call arg** stays `ctx_.group_ctx`; (ii) `group_view::operator[]` (`include/fixpp/wire/group_view.hpp`) stays a verbatim `base_ctx_` copy — pushing there double-pushes; (iii) `parser.hpp:309` depth-1 seed unchanged; (iv) C-ABI cursor (`message_read.cpp:506`) unchanged — do NOT un-push. (FR-007/FR-008.)
- [ ] T010 [US2] **Clean codegen reconfigure** (`rm -rf <build>/_codegen`) so the changed emitter regenerates all 4 codegen-input dicts, then recompile + run the depth-3 witness (T008 now GREEN) and the typed-read suites. Verify shipped-dict runtime read results are **byte-identical** to pre-fix (defect inert on every shipped dict) across at least **debug + sanitizer + coverage** configs — guarding the stale-`_codegen`-header false-green/red hazard (SC-005, `project_codegen_emitter_staleness`). Re-index CodeGraph after regen: `codegraph index --force`.

**Checkpoint**: US2 accessor half (FR-007/FR-008/FR-011) fully functional and independently testable — shippable even if Phase 5 splits out.

---

## Phase 5: Validator nesting-aware rewrite (FR-010 / L-063-3) — ⚠️ SEVERABLE (SPLIT-TRIGGER)

**Scope**: The strict `dictionary_driven_validator` (`include/fixpp/wire/validator.hpp`) Step-3 group walk hardcodes `root_path = {}` (`:204`) → every nested lookup queries root context and misses (one level worse than the pre-fix accessor). This is a **from-scratch recursive rewrite of unknown size**, NOT a push tweak.

**⚠️ SPLIT-TRIGGER (recorded Gate A round 3, user-signed-off 2026-07-12 — plan.md `## Gate A`):** FR-010 **rides 072 by default**; the split is the **`/implement`-time escape**. If, during T011/T012, the rewrite proves **unbounded or materially larger than the T009 accessor fix**, DROP this entire phase from 072 → 072 ships Part A (Phase 3) + the accessor half of Part B (Phase 4: FR-007/008/011); FR-010 moves to its own follow-up feature; and the L-063-3 "fixed" promotion is **WITHHELD** (Phase 6 T014 leaves L-063-3 "open/tracked," carried to the follow-up). Because this is its own phase, the drop is "skip Phase 5," leaving Phases 3–4 intact. Record the fire/no-fire decision in `.specify/decisions/072-nested-group-hardening-verify.md`.

### Test for Phase 5 (write FIRST, mutation-proven RED)

- [ ] T011 Add the **named** validator witness `ValidatorNestedMembership_Depth2ContextMissUnderFlatWalk` in a new file `tests/wire/validator_nested_membership_test.cpp`: a depth-≥2 nested-group membership scenario the current flat walk validates **wrong** (root-context lookup misses → accepts/rejects the wrong member set) and the nesting-aware walk validates right. **Mutation-prove RED on the flat walk**, GREEN after T012. (FR-010b.) If T012 is split out, this witness moves with it.

### Implementation for Phase 5

- [ ] T012 Rewrite `validator.hpp` Step-3 as a **recursive descent threading a path stack**, **query-before-push** (FR-010a / research D-B4 / contracts/typed-context.md): for a candidate nested group `G` at the current descent, **first** query `group_first_field`/`group_member_tags(msg_type, current_parent_path, G)` — where `current_parent_path` **excludes** `G`'s own `no_tag` — in place of the hardcoded root `(msg_type, {}, G)`; **then**, only to recurse into `G`'s children, push `G` (`current_parent_path.pushed(G)`) and pop on exit. Mirror `consume_group_extent` (query-under-ctx before forming `child = ctx.pushed(no_tag)`); per-level instance counts come from the slicer-produced extent, NOT the flat `seen_in_instance` heuristic (`:258-267`). Makes T011 GREEN. **Cap scope / evaluate the SPLIT-TRIGGER as you go** — if it balloons past the accessor fix, invoke the split (see phase header) rather than forcing it in.

**Checkpoint**: If shipped — typed read and strict validation agree on depth-≥2 membership. If split — this phase is dropped cleanly; L-063-3 stays tracked.

---

## Phase 6: Polish & Cross-Cutting Concerns

- [ ] T013 [P] Confirm **C-ABI zero-delta vs `1.5.0`** (SC-006): no exported C symbol / public C header / C error enum value / C-ABI version constant changed (Part A/B touch only C++ typed path + validator + loader + census). Confirm `include/fixpp/core/error.hpp` and `tests/core/test_020_error_completeness.cpp` are **untouched** (no enum append — NEW-1). ABI hygiene gate reports no delta.
- [ ] T014 Update `spec/behaviors-and-limitations.md` (FR-013): **L-063-4** and **L-062-3** → "pinned (+ delimiter reject at load)"; **L-065-1** → "fixed" **AND correct its stale depth-2 prose** — replace "depth-2 member read" framing with the source-verified model ("depth-2 slicing is correct; the stored child-view context is too short, so the first observable failure is slicing a depth-3 grandchild group"); **L-063-3** → "fixed" **ONLY if Phase 5 shipped** — if the SPLIT-TRIGGER fired, L-063-3 stays **"open/tracked"** and is carried to the follow-up feature (promotion withheld). Record the residuals explicitly (not implied covered): (a) scalar-member disjointness not load-enforced (FR-004); (b) hand-built `table_view`/non-`load_*` `Dictionary` not guarded (FR-005a); (c) global-first-seen guard misses a non-first-seen-context collision of a reused `no_tag` (FR-005b); (d) scalar-member census unpinnable for FIX40/41/42 (FR-013d); (e) any context the FR-001 walk cannot structurally reach, bounding the all-contexts claim (FR-013e/SC-001).
- [ ] T015 Run the `quickstart.md` validation guide end-to-end (Part A census + load-reject; Part B depth-3 witness + clean reconfigure across debug/san/coverage; **plus the validator witness only if Phase 5 shipped** — if the SPLIT-TRIGGER fired, skip the validator-witness clause, it moved to the follow-up feature) to confirm the feature's capabilities + zero shipped-dict regression before `/speckit-verify`.

### Mandatory close-out tasks (ALWAYS emit — Gate-B preconditions, Article XVII §8)

- [ ] T016 [P] **Catalogue close-out**: flip every feature-owned OFFICIAL row in `spec/feature-catalogue.md` for `072-nested-group-hardening` to `done` (with PR / squash / evidence refs) AND add/update the matching `spec/coverage-index.md` entry for 072. Cross-reference the `L-062-3`/`L-063-3`/`L-063-4`/`L-065-1` status flips landed in T014. (T057 analog.)
- [ ] T017 **Feature-completeness audit (FINAL task)**: assert against the merged tree that (i) every `tasks.md` row is `[X]` or carries an explicit waiver rationale; (ii) every spec **FR-001..FR-013** and **SC-001..SC-006** maps to a landed test AND a landed implementation; (iii) every feature-owned OFFICIAL catalogue row is `done` with a matching `coverage-index.md` entry. **Split-aware**: if the Phase 5 SPLIT-TRIGGER fired, record FR-010/L-063-3 as an **explicit deferral/carry-forward** (NOT a coverage gap) — FR↔test↔impl still closes 100%-or-fully-waived, with FR-010 waived-by-split. Record the verdict in `.specify/decisions/072-nested-group-hardening-verify.md` (`## Completeness`) or a sibling `…-completeness.md`. Hard `/gate-b` precondition (Article XVII §8 / pre-flight 4d).

---

## Dependencies & Execution Order

### Phase Dependencies

- **Phase 1 Setup** → no deps.
- **Phase 2 Foundational** → empty (US1/US2 independent).
- **Phase 3 US1** and **Phase 4 US2** → each depend only on Setup; **fully independent of each other** (no shared file/fixture) — parallelizable across developers.
- **Phase 5 FR-010** → depends on Setup; independent of US1; conceptually adjacent to US2's validation surface but **severable** (its drop leaves US1 + US2 accessor half intact). Not in either story's independent test.
- **Phase 6 Polish** → depends on all *retained* phases complete. T017 is the FINAL task.

### Within a phase (TDD)

- US1: T002/T003/T004 (tests, RED-proven) → T005 (error type) → T006 (guard, makes T004 green) → T007 (census green over 9 dicts + shipped-load-unchanged).
- US2: T008 (witness, mutation-proven RED) → T009 (emitter push) → T010 (clean reconfigure, witness green + shipped-dict parity).
- Phase 5: T011 (witness RED) → T012 (recursive rewrite, green) — or split.

### Parallel Opportunities

- **US1 ⟂ US2 ⟂ Phase 5** entirely (distinct files/fixtures) — three developers could take one each after Setup.
- Test-authoring `[P]`: T002/T003/T004 (US1 tests — but T002/T003 share `reused_tag_census_test.cpp`, so serialize edits to that file; T004 is a new file, truly `[P]`), T008, T011 across phases.
- Impl `[P]`: T005 (new error header) is `[P]` vs T006 (loader) only until T006 needs the type; T013 (ABI check) `[P]` in Polish.

---

## Implementation Strategy

### MVP First

Phase 3 (US1 — census + load guard) alone is a shippable MVP: it closes the reachable public-API → silent-mis-split path, the worst failure class for a wire library.

### Incremental Delivery

1. Setup → baseline green.
2. US1 (Phase 3) → the delimiter guard + census pins → MVP.
3. US2 accessor half (Phase 4) → depth-3 typed correctness reconciled to the C-ABI.
4. FR-010 validator rewrite (Phase 5) → **only if bounded**; else split to a follow-up (clean phase drop).
5. Polish (Phase 6) → catalogue/B&L flips, ABI hygiene, completeness audit.

### Notes

- [P] = different files, no dependency on an incomplete task.
- Every witness is RED-proven before its fix (census trip-proof on an injected bad dict; load-reject RED before the guard; depth-3 + validator witnesses mutation-proven). A never-red test proves nothing (project discipline).
- The Part B emitter change has **no checked-in golden** — verify by clean `_codegen/` reconfigure, never a golden diff.
- Commit after each task or logical group. Stop at any checkpoint to validate a phase independently.
