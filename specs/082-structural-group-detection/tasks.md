# Tasks: Structural Repeating-Group Detection for Legacy FIX Dictionaries

**Feature**: `082-structural-group-detection` | **Branch**: `082-structural-group-detection`
**Input**: `spec.md` (28 FRs, 13 SCs, US1–US4), `plan.md` (§ Implementation Sequencing steps 1–9),
`research.md` (D-1..D-13), `data-model.md` (FR → pin map), `contracts/group-detection.md` (C1–C4, K1–K11),
`quickstart.md` (S0–S9)

**Gate A**: converged round 3, user-signed-off 2026-07-30 (`plan.md` § Gate A).
OD-1 resolved → the fail-closed loader rejection (**FR-023**). OD-2 ratified → the annotation-only
Article XVIII §7 amendment (**FR-020**, task T052). Appendix-A user `/plan` sign-off given.

## Format: `[ID] [P?] [Story] Description`

- **[P]** — parallelizable (different files, no dependency on an incomplete task)
- **[US1]–[US4]** — user-story phase tasks only; Setup / Foundational / Polish carry no story label

## Path Conventions

Repository root is the library submodule (`research/G19-fix-fpml-iso20022/library/`). All paths below
are relative to it.

## Tests are REQUIRED (not optional)

`[const §VII]` mandates RED→GREEN TDD: no code without a test, test first. Every pin in
`data-model.md` § "FR → pin map" is a required witness. **RED-first ordering is load-bearing in two
places specifically**, and reversing either destroys the evidence:

1. **Step 1's oracle (T005–T008) must land BEFORE the predicate change (T023).** If the census is
   simply flipped to the new predicate it moves in lockstep with the code under test and witnesses
   nothing (D-6 / FR-018).
2. **FR-022(a)'s pre-change bench figure (T002) must be captured BEFORE T023.** No
   `bench/baselines/dictionary/table_view_footprint_bench.json` exists today, so there is nothing to
   compare against after the fact — the "before" number is only obtainable now.

Because `tools/codegen/**` is touched, **`ctest -L codegen` is mandatory** on every local
verification run — a label-filtered run that omitted it has previously missed a subsystem's COUNT pin.
Never pass repeated `-L` flags: `ctest -L A -L B` is **conjunctive** in CMake and selects the empty
set. Use regex alternation (`-L "codegen|dictionary|wire|session"`).

---

## Phase 1: Setup — capture the pre-change evidence that is unobtainable later

**Purpose**: three of this feature's checks compare against a "before" state that stops existing the
moment T023 lands. Capture all of it first.

- [ ] T001 Force a **clean** codegen rebuild and record the pre-change generated tree per version under a scratch path — the known emitter-staleness trap silently compiles a stale `Reify.hpp` in non-debug dirs (`plan.md` § Implementation Sequencing step 5; project memory `project_codegen_emitter_staleness`). Every later regeneration diff in T026/T027 is void without this.
- [ ] T002 [P] Capture the **pre-change** `BM_TableView_BuildFix44` / `BM_TableView_BuildFix50SP2` / `BM_TableView_Sizeof` figures by running `bench/dictionary/table_view_footprint_bench.cpp` and record them in the feature's working notes — FR-022 (a) / K10. This profile has **no** `bench/baselines/` entry today (075 recorded it in-file only), so §2's ±5% budget has nothing to compare against unless the before-figure is taken now.
- [ ] T003 [P] Capture the **pre-change** `v42` translation-unit figure from the existing `bench/codegen/compile_time_bench/` harness (`ctest -L bench`) against its load-bearing ≤3 s single-version ceiling — FR-022 (c) / K10. No CI job runs this harness (`tier1.yml`'s `bench` job is soft and runs only `placeholder_bench`), so an overage would otherwise be invisible.
- [ ] T004 [P] Record the pre-change registered-group set for all **ten** dictionaries from `Dictionary::as_table_view()` — the baseline side of K1 / K2 / K3 (FIX40 0, FIX41 0, FIX42 0, FIX43 **33**, FIX44 59, FIX50 67, FIX50SP1 97, FIX50SP2 505, FIXT11 1, Orchestra FIX Latest 524). **FIX43's before-value is 33, not 34** — it is the one dictionary where before ≠ after (contract C2: registered-before 33, registered-after **34**, delta `+1 tag (576)`; `data-model.md` Entity 3 agrees), which is precisely why capturing it correctly here matters.

**Checkpoint**: pre-change evidence captured. Nothing in `src/` has changed yet.

---

## Phase 2: Foundational (BLOCKING — must complete before any user story)

**Purpose**: the non-circular oracle every story's pin reads from, plus the loader rejection that makes
the zero-member `<group>` state unreachable for everything downstream.

### Step 1 — the oracle (FR-018 / D-6 / K7)

- [ ] T005 Extend `tests/dictionary/required_scope_oracle.hpp` (079's shared single oracle) with the **group-tag and per-context-member census**. **Do NOT fork a third XML walker** — 079's own banner states the rule ("do NOT duplicate/fork the walker logic, that would break the single-oracle guarantee"); `contracts/predicate_census.py` is the design-time Python oracle and this is the test-time C++ one.
- [ ] T006 In `tests/dictionary/required_scope_oracle.hpp`, make the extension reproduce the **reachability restriction** — component expansion plus the `<header>`/`<trailer>` merge (`src/dictionary/xml_loader.cpp:926-933`) — and assert its ten-dictionary output equals C2's **registered-after** column (59 / 67 / 97 / 505 / 1 / 524), never the *declared* column (69 / 99 / 507 on FIX50 / SP1 / SP2). Without this, SC-002's both-directions equality fails spuriously on three dictionaries.
- [ ] T007 Add the oracle's **zero-member-`<group>` report** across all ten dictionaries in `tests/dictionary/required_scope_oracle.hpp` — expected count 0. This is the standing no-regression evidence for FR-023 / K11 (the rejection affects zero shipped dictionaries) and the measurement behind contract P1-NON.
- [ ] T008 Re-point `tests/dictionary/reused_tag_census.hpp:74,80` to read T005's oracle instead of `fr.type == field_data_type::NumInGroup`, and rewrite the L-063-1 carve-out text in `tests/dictionary/reused_tag_census_test.cpp` — FR-018. Landing this before T023 is what makes SC-001 / SC-002 / SC-003 self-verifying rather than a one-off transcript.

### Step 2b — the loader fail-closed rejection (FR-023 / K11 / OD-1), RED first

- [ ] T009 [P] RED: add a member-less-`<group>` rejection test for the `<fix>` loader in `tests/dictionary/required_scope_census_test.cpp` — a synthetic dictionary whose `<group>` has no resolvable `field`/`group`/`component` child must fail to load with `fixpp::dict::xml_parse_error` (`include/fixpp/dict/error.hpp:44`), the diagnostic naming the group's `name` and its `no_tag`. **The fixture MUST place the member-less `<group>` at a NON-first-seen occurrence of its `no_tag`** — both `GroupDef` records sit inside a first-seen-wins dedup guard (`src/dictionary/xml_loader.cpp:609`), and a check wrongly placed inside that guard would pass a first-seen-only fixture.
- [ ] T010 [P] RED: the Orchestra sibling in `tests/dictionary/required_scope_census_test.cpp` — must throw `fixpp::dict::orchestra_parse_error` (`include/fixpp/dict/error.hpp:98`), same non-first-seen fixture constraint against the dedup guard at `src/dictionary/orchestra_loader.cpp:626`. Asserted **per loader**, not once.
- [ ] T011 [P] RED→GREEN-by-construction: assert all **ten** vendored dictionaries still load clean in `tests/dictionary/required_scope_census_test.cpp` — the FR-023 no-regression leg, cross-checked against T007's oracle report. T009–T011 together are SC-013's witness.
- [ ] T012 Implement the rejection in `src/dictionary/xml_loader.cpp` (member scan `:610-641`, recorded `:644`, pushed `:649`): throw `xml_parse_error` naming the group's `name` and `no_tag`, per `include/fixpp/dict/error.hpp:73`'s "the facts an operator needs to fix the offending dialect" convention. The check MUST sit **outside** the first-seen dedup guard at `:609` so the rule is not order-dependent. No new exception subclass and no `fixpp::core::error` variant (`error.hpp:18-27`), so FR-017 / SC-009 and `tests/core/test_020_error_completeness.cpp`'s slot pin hold.
- [ ] T013 Implement the Orchestra sibling in `src/dictionary/orchestra_loader.cpp` (`first_member_tag(group_node) == 0` at `:629`, helper `:467`, record block `:626-635`): throw `orchestra_parse_error`, same diagnostic shape, **outside** the dedup guard at `:626`.
- [ ] T014 Verify T009–T011 are GREEN and confirm the contract text in `contracts/group-detection.md` C1.1 / C1.3 P1-NON now describes an **unreachable** state rather than a tolerated limitation. Do not overstate: `group_first_field`'s sentinel is still ambiguous *in isolation*; what changed is that no `Dictionary` the loaders admit can carry a member-less group.

**Checkpoint**: the oracle is independent of the predicate under change, and the zero-member state is
unreachable. Steps 3–6 now inherit an exact `group_first_field` sentinel.

---

## Phase 3: User Story 1 — FIX 4.0/4.1/4.2 repeating groups become visible (Priority: P1) 🎯 MVP

**Goal**: three shipping dictionaries go from group-blind to membership-correct, and the `v42` read
tier materializes typed group accessors (0 → 18 `class G_`).

**Independent test**: load `FIX42.xml`, build `as_table_view()`, assert all 18 group tags registered
with declared members (today 0); regenerate the `v42` read tier and assert 18 typed group accessors
(today 0).

### RED pins for User Story 1 ⚠️ write these before T023

- [ ] T015 [P] [US1] RED: exact-set equality both directions for FIX42's **18** bare-store registered tags — {33, 73, 78, 124, 136, 146, 199, 215, 267, 268, 295, 296, 382, 384, 386, 398, 420, 428} — against T005's oracle, in `tests/dictionary/required_scope_census_test.cpp` (FR-005 / K1).
- [ ] T016 [P] [US1] RED: exact-set equality both directions for FIX40 (**4**) and FIX41 (**7**) in `tests/dictionary/required_scope_census_test.cpp` — these two dictionaries have **no golden to regenerate**, so this direct pin is their only witness (FR-005 / K1).
- [ ] T017 [P] [US1] RED: **per-context member-set** equality for the divergent-signature tag `NoRelatedSym(146)` in `tests/dictionary/required_scope_census_test.cpp` — 4 distinct direct-member lists across its 6 FIX42 occurrences (`{News, Email}` → 19 members, `QuoteRequest` → 31, `MarketDataRequest` → 20, `{SecurityDefinitionRequest, SecurityDefinition}` → 22): the 063 context store must hold the **distinct** set per `(msg_type, parent path, no_tag)` matching the oracle per context, and the bare store the loader's first-seen set. **Not a tag-set projection** — the two stores are keyed differently, so "the stores agree" can only mean a projection, which passes while every per-context member set is wrong. **Do not use `LinesOfText(33)`**: its two occurrences carry identical members `{58, 354, 355}`, so a collapse on it is unobservable (FR-004 / I-4a / K4).
- [ ] T018 [P] [US1] RED: exact-set equality both directions for the six unchanged dictionaries against the **registered-after** column — FIX44 59 / FIX50 67 / FIX50SP1 97 / FIX50SP2 505 / FIXT11 1 / Orchestra FIX Latest 524 — in `tests/dictionary/required_scope_census_test.cpp` (FR-014 / SC-002 / K2).
- [ ] T019 [P] [US1] RED: the regenerated `v42/Messages.hpp` carries exactly **18** `class G_` and keeps its **46** message classes, in `tests/codegen/` (K5 / SC-004).
- [ ] T020 [P] [US1] RED: the `MassQuote` `NoQuoteSets(296) → NoQuoteEntries(295)` nesting is expressed as a **nested** typed group, not flattened, in `tests/codegen/` — FIX42 has 5 nested group occurrences and the emitter's existing depth bound plus deterministic group-emission ordering must hold for the newly-visible legacy groups (US1 AC4).
- [ ] T021 [P] [US1] RED: the ungated parse correction in `tests/session/` with `validate_inbound_messages` **OFF** — a FIX 4.2 read of a tag living inside a repeating group resolves group-scoped rather than flat/positionally, for **every** FIX40/41/42 session and not only strict ones (FR-006a / SC-008). Taking this pin with strict OFF is what keeps the parse axis and the validation axis from being conflated. **Also pin SC-008a's first leg in the same test**: with the flag off, **no** FIX40/41/42 inbound message is newly *rejected* — the ungated change alters read shape only, never acceptance. Read-shape movement alone does not establish that.
- [ ] T021b [P] [US1] RED: SC-008a's **second** leg and FR-006/FR-006b's shared witness, in `tests/session/` — with `validate_inbound_messages` **ON**, the newly-reachable group-required rejections are **exactly** the set derivable from the dictionary's `required='Y'` group members, **enumerated and pinned rather than merely observed**. Derive the expected rejection set from T005's oracle (FIX42's 14 `required='Y'` message/group pairs across 12 messages) and assert set equality in both directions. This is the enforcement axis riding the **existing** opt-in with no additional gate (FR-006b); a "some new rejections occur" assertion would pass while the set is wrong. *(Suffixed ID follows `plan.md` § Implementation Sequencing's own `2b`/`5b` convention, preserving execution order without renumbering.)*
- [ ] T022 [P] [US1] RED: the cross-path P4 pin in `tests/capi/` — on a FIX 4.2 dictionary, `fixpp_msg_group_begin(t)` succeeds for **exactly** the bare store's registered tag set, both directions (FR-006 / K6b). This is the leg K6 cannot reach: it catches a divergent *second* structural realization inside the runtime tier, which is precisely the failure mode that made `Dictionary::group().has_value()` the wrong fix at Gate A.

### Implementation for User Story 1

- [ ] T023 [US1] Replace the datatype gate with `group_first_field(fr.tag) != 0` at **all three** `Dictionary::as_table_view()` sites in `src/dictionary/dictionary.cpp` (`:398` bare loop, `:441` `immediate_parent`, `:446` context loop) — **in one change unit**, per FR-004: no configuration or code path may leave one store structural and the other datatype-gated. Fold the now-tautological `if (legacy_first == 0) { continue; }` guard at `:403-405` into the new predicate (`:402` is the `legacy_first = group_first_field(legacy_no_tag)` **lookup**, not the guard — `plan.md`'s Round-2 log records this same off-by-one for a reviewer-supplied cite) (D-7 disposition FOLD/REDUNDANT). Leave `:463`'s `if (members.empty()) continue;` **unchanged** — it is a post-detection registration guard, outside the predicate's scope (C1.3 P1 / P1-NON). Do **not** change `FieldRef::type` (D-4) — that is what keeps FR-016a's byte-identity prediction falsifiable rather than tautological.
- [ ] T024 [US1] Add `VersionIR::group_tags` to `tools/codegen/fixpp-codegen/ir.hpp` and populate it from the already-correct `MessageIR::group_order` in `tools/codegen/fixpp-codegen/ir.cpp` — D-3. `group_order` is *already* correct for FIX42 today (`ir.cpp:80-100`: `walk_level` keys on the element name and pushes the `GroupOrderEntry` unconditionally), which is what makes the codegen half a re-point rather than new plumbing.
- [ ] T025 [US1] Re-point all **8** emitter line-sites onto `VersionIR::group_tags` — `tools/codegen/fixpp-codegen/emit_messages.cpp:166,234,337,347,425`, `emit_reify.cpp:217,227`, `emit_builders.cpp:606` (D-3 / D-7). Leave `emit_manifest.cpp:73` alone: it is a pure datatype-token column with no group branching, dispositioned NO CHANGE.
- [ ] T026 [US1] Force a clean codegen rebuild, regenerate every version, and classify **all five** emitted `v42` artifacts explicitly — `Fields.hpp`, `Messages.hpp`, `Validator.hpp`, `Reify.hpp`, `NormativeReferences.md` — as byte-identical or changed-with-explanation (FR-016). **`Manifest.txt` is NOT a `v42` artifact**: `MessageIR::occurrences` is populated only on the Orchestra path (`ir.cpp:476`), so `emit_manifest` returns empty for every `<fix>`-schema version and no file is written. `Fields.hpp` and `Validator.hpp` must be byte-identical (FR-016a).
- [ ] T027 [US1] Verify the `v44` / `v50sp2` / `vt11` / `vlatest` read goldens diff **byte-identical** (FR-015 / SC-005 / K5) — the discriminating check that the predicate is set-equal wherever C2 says EQUAL.
- [ ] T028 [US1] Regenerate `specs/003-dictionary-codegen/contracts/golden/v42_Messages.golden.hpp` and reconcile the emitted delta **by construction** to FIX42's declared structure — not "golden regenerated" (FR-016 / SC-004).
- [ ] T029 [US1] Implement the `v42` **class-side ⟷ raw-XML** consistency gate in `tests/codegen/` — FR-021 / SC-004. Class side: parsed from the *text* of the regenerated `v42/Messages.hpp` (class bodies, `view_.template get<N>()` accessor calls, `group_view<...G_N>` return types marking a group reference) per the extraction rule at `tests/codegen/vlatest_manifest_class_consistency_test.cpp:33-63`. Structural side: T005's oracle. The 076 V-1/V-1b *manifest*↔class pair **cannot** be instantiated for `v42` (V-1b keys on a `Manifest.txt` `v42` does not emit), so this is the class-side leg only. Prefer a version-parameterised gate — it closes the same hole for `v44` / `v50sp2` / `vt11`.
- [ ] T030 [US1] Refresh the now-false carve-out comments in the four dictionary tests — `tests/dictionary/reused_tag_census_test.cpp`, `required_scope_test.cpp:107`, `required_scope_census_test.cpp:341`, and the census helper — and move the COUNT pins that shift.

**Checkpoint**: US1 independently testable. FIX40/41/42 register 4/7/18 groups; `v42` reads 18 typed
groups; the six EQUAL dictionaries and their goldens are provably unmoved.

---

## Phase 4: User Story 2 — the `fixpp::v42` typed builder tier is re-instated (Priority: P1)

**Goal**: the deliverable #196 actually asks for — the 078 split builder/validator surface for `v42`,
whose `Args` carry its repeating groups so a `required='Y'` group cannot be silently omitted.

**Independent test**: run the codegen driver without the `v42` exclusion, assert the 078 split file set
is emitted for `v42`; build a `NewOrderList` with a populated `NoOrders(73)` and match a
QuickFIX-derived golden.

**Depends on**: T023–T026 (groups must be visible before they can enter the builder tier).

- [ ] T031 [P] [US2] Derive the expected `v42` plan set from `emit_builders`' **own interning rule** *before* the first run, via `specs/082-structural-group-detection/contracts/builder_plan_census.py` — `--families all` ⇒ **28** distinct `(no_tag, signature)` plan headers over **17** tags, 226 files, `builder_registry` 39; `--families official` ⇒ **19** / **11** tags, 147 files, registry 25 (D-9a / K8). Never transcribe these from a first run. Note `384 NoMsgTypes` is one of the 18 read-tier tags but **not** one of the 17 builder-tier tags: its only FIX42 host is the admin message `Logon`, excluded by `emit_builders`' `is_application` gate.
- [ ] T032 [P] [US2] RED: **invert** (do not delete) `V42EmitsNoBuilders` in `tests/codegen/test_077_builder_no_emit.cpp` — FR-016b.
- [ ] T033 [P] [US2] RED: **invert** the expected `v42` set in `tests/codegen/test_077_v42_vt11_completeness_and_c4.cpp` from "∅ by policy" to the real set, **derived** from T031's rule rather than transcribed — FR-016b. This is an exact-**set** completeness gate; a transcribed set enshrines whatever the first run produced.
- [ ] T034 [P] [US2] RED: `validate_<Msg>` rejects an `Args` value omitting a `required='Y'` group at **all 14** FIX42 message/group pairs, in `tests/codegen/` — FR-008 / SC-006 / K9. **13 are top-level omissions; the 14th, `MassQuote`/`NoQuoteEntries(295)` nested inside the required `NoQuoteSets(296)`, must be built as a 296 *entry* carrying an empty 295 span** and is checked per-entry via `gc.validate_entry`, not by a top-level `group_checks` row — a different construction from the other 13.
- [ ] T035 [US2] Delete the `if (ir.ns != "v42")` exclusion at `tools/codegen/fixpp-codegen/main.cpp:132` with **no** replacement version predicate — D-8 / FR-007 / FR-010.
- [ ] T036 [US2] Check in the `--families all` `v42` builder golden set under `specs/078-precompiled-builder-libs/contracts/golden/` — 226 files, 28 `groups/<PlanName>.hpp` plan headers, registry 39 (FR-009 / K8), matching T031's derivation.
- [ ] T037 [US2] Instantiate the `--families official` **structural witness** for `v42` mirroring `OfficialModeBuildersStructuralShape` (`tests/codegen/determinism_test.cpp:920-948`) — 147 files / 19 plan headers / registry 25. **Not a golden**: 078 deliberately retired the `--families official` pinned-golden gate (`determinism_test.cpp:898-909` — "no `v44-official/` golden set is checked in"), so a new golden directory here would reintroduce a retired convention.
- [ ] T038 [US2] Verify `vt11` still self-skips the builder tier via its genuinely **empty application registry** (not a version predicate) and that its read golden has not moved — FR-010.
- [ ] T039 [US2] Verify the `v44` / `v50sp2` / `vlatest` builder golden **sets** diff byte-identical (FR-015 / SC-005 / K5), and that `v42`'s 18 newly-visible groups entered the tier under B-077-1's structural-key guarantee — a new structural variant of an existing group tag surfaces as a new ordinaled `Args` plan in the golden diff, never a silent mis-share.

**Checkpoint**: US2 independently testable. `v42` ships the full typed builder surface; a required
group is representable in `Args`, so its omission is detectable rather than silent (Article VI).

---

## Phase 5: User Story 3 — FIX43's two mis-typed group-count tags are corrected (Priority: P2)

**Goal**: a discovered latent-defect fix, unavoidable once the predicate changes. Its delta and pins
are kept visible in their own right rather than buried under the `v42` story.

**Independent test**: load `FIX43.xml`, assert tag 576 **is** registered with member
`ClearingInstruction` (it is not today); assert tag 82 remains **un**registered as a group and
`ListStatus` still reads it as a plain required field.

**Depends on**: T023 (the same runtime predicate delivers this — US3 contributes pins, not a separate
implementation).

- [ ] T040 [P] [US3] RED: FIX43 tag **576** (`NoClearingInstructions`) is registered as a repeating group with member `ClearingInstruction`, in `tests/dictionary/required_scope_census_test.cpp` — FR-011 / K3. 576 is `INT`-typed, so its registering is only possible if **no** datatype gate survives on the runtime path: this is FR-001's behavioral witness, not a token grep.
- [ ] T041 [P] [US3] RED: FIX43 tag **82** (`NoRpts`) is **not** registered as a repeating group — now because the dictionary declares no `<group>` for it rather than because a downstream guard rejects it — **and** is still accepted and enforced as a **plain required field** in `ListStatus`, in `tests/dictionary/required_scope_census_test.cpp` (FR-012 / K3). The predicate change must not make the field unknown or optional.
- [ ] T042 [P] [US3] RED: FIX43's registered set differs from the T004 baseline by exactly **+1 tag (576)** — a cardinality delta of +1, not "+576" — and its other 33 group tags are unchanged (FR-013 / SC-003 / K3).
- [ ] T043 [US3] Verify T040–T042 GREEN after T023, and assert detection resolves **per dictionary** rather than globally by tag: tags 82 and 576 are a group in one dictionary and a plain field in another across FIX43/FIX44 (FR-003). Together T040–T043 are FR-002's witness: the predicate is a **replacement**, not a union with the datatype gate — 576 (`INT`, a `<group>`) registers and 82 (`NUMINGROUP`, never a `<group>`) does not, from one predicate.

**Checkpoint**: US3 independently testable. Exactly one FIX43 tag moves.

---

## Phase 6: User Story 4 — a grouped/nested FIX 4.2 write exemplar becomes expressible (Priority: P3)

**Goal**: close L-061-1 and demonstrate the builder tier is *usable*, not merely emitted.

**Independent test**: a `v42` exemplar constructing `MassQuote` with `NoQuoteSets(296)` containing
`NoQuoteEntries(295)`, asserted against an independently-derived golden plus a read round-trip.

**Depends on**: US1 and US2 (delivers no value without both).

- [ ] T044 [P] [US4] Derive an **independent** (QuickFIX-derived) golden for a `v42` `MassQuote` with a populated `NoQuoteSets(296) → NoQuoteEntries(295)` nesting — US4 AC1 / SC-007.
- [ ] T045 [US4] Add the `v42` nested-group write exemplar alongside the 061 suite, assert its emitted bytes against T044's golden, and assert a **read round-trip** through the regenerated `v42` read tier in which every field and **both** group levels round-trip field-for-field — US4 AC2 / SC-007.

**Checkpoint**: all four user stories delivered and independently verifiable.

---

## Phase 7: Polish & Cross-Cutting Concerns

### Step 8 — the three Article VIII benchmark obligations (FR-022 / SC-012 / K10)

All three land in the **same PR**. §2 re-baselining for (a) and (b); §3 run-and-record for (c), which
produces no baseline. Article VIII §2/§3 are **not** scoped to hot-path cost —
`.specify/constitution.md:185-186`; §5 is the only hot-path clause. **Asserting any leg unmoved
instead of measuring it is non-conforming.**

- [ ] T046 **(a)** Re-measure `bench/dictionary/table_view_footprint_bench.cpp` — the one existing bench that times the changed function — on FIX44 and FIX50SP2 **plus a new FIX 4.2 row**, re-report `BM_TableView_Sizeof` with the `group_bits_` heap growth stated (empty → `(max no_tag >> 6) + 1` words per `table_view` copy on FIX40/41/42), and check in the **new** `bench/baselines/dictionary/table_view_footprint_bench.json`. Compare against T002's pre-change figures. Set-equality does **not** bound build time here: the per-field test goes from one enum compare to an O(log G) `groups_` binary search (G = 507 on FIX50SP2, ≈9 comparisons), on **every** dictionary — including the C2 EQUAL rows.
- [ ] T047 **(b)** Add a FIX 4.2 **group-bearing parse** benchmark under `bench/wire/` with a fresh baseline under `bench/baselines/wire/` — registration flips FIX40/41/42 from a `table_view::group_bits_` all-clear short-circuit to real group-context resolution on the parse path, per message (D-12). Reuse the existing 061/067 harnesses.
- [ ] T048 **(c)** Run the existing `bench/codegen/compile_time_bench/` harness (`ctest -L bench`) and record the `v42` TU figure against its load-bearing ≤3 s single-version ceiling, comparing to T003's pre-change figure — 082 adds 18 `class G_` to `v42/Messages.hpp` and `Reify.hpp`. Only `v50sp2` is exempt from a FAIL (`compile_time_bench.sh:139-143` really does `exit 1`); the all-versions ceiling is WARN-only. Ceiling check, **no baseline file**.
- [ ] T049 Re-check SC-012's **8-file** pre-existing baseline set within ±5%: `bench/baselines/wire/{framer,offset_table,parser,validator,writer}.json`, `bench/baselines/codegen/typed_accessor_bench.json`, `bench/baselines/dictionary/{reify_bench,xml_loader}.json`. Note FR-023's per-`<group>`-occurrence check lands inside `XmlLoader::load`'s timed region, so `dictionary/xml_loader.json` is the one entry whose no-move ground is narrowed — expected negligible, but measured, not asserted.

### Frozen-surface invariant pin (FR-017 / SC-009)

- [ ] T049b **Assert the C-ABI surface did not move** — FR-017 / SC-009. Not true-by-omission: Article X's obligation is to **verify** the non-violation, and T055's completeness bar requires every FR to map to a landed *test*, not to the absence of a diff. Three legs, on 081's precedent (`specs/081-strict-validation-residuals/tasks.md:113`, T022): (i) assert **zero diff** under `src/capi/`, `include/fixpp/capi/`, `error.h` and `version.h`, and that `FIXPP_C_ABI_VERSION` is still `1.5.0`; (ii) run `capi_pure_tests` `AbiSymbolGolden.CabiSymbolSetUnchanged` and `AbiSymbolGolden.ErrorEnumUnchanged` and record 2/2 PASS — the `nm` symbol golden **is** this repo's current abidiff-equivalent gate (libabigail was retired 2026-06-22), so no symbol-golden or abidiff *regeneration* is expected or permitted; (iii) confirm `.github/workflows/abi-golden.yml` is green on the branch. Note FR-023 deliberately adds **no** surface — it reuses the existing `xml_parse_error` / `orchestra_parse_error` classes and appends no `fixpp::core::error` variant (`include/fixpp/dict/error.hpp:18-27`), so `tests/core/test_020_error_completeness.cpp`'s slot pin must also still pass. Record the result in the `/speckit-verify` decision doc so T055 has a landed witness to point at.

### Step 9 — documentation and governing documents (FR-019 / FR-006c / FR-020 / FR-023)

- [ ] T050 In `spec/behaviors-and-limitations.md`, record the resolution of **L-063-1**, **L-061-1**, **L-066-1** and **L-077-1**, widen L-066-1's "six group-registering dictionaries" scope note, and record the FIX43 tag-82/tag-576 corrections as a named behavior with their evidence. **Also add one line to the L-073-1 row** noting that as of 082 its top-level-group-read silent-truncation-on-arena-exhaustion limitation also applies to FIX 4.0/4.1/4.2, whose group reads were previously unreachable because those dictionaries registered zero groups. L-073-1's text is *not* dictionary-scoped, so it is not false without this — unlike L-066-1, which enumerates "the six group-registering dictionaries" and would go stale — but adding it gives the two rows symmetric treatment and pre-empts a reader asking why one sibling was widened and the other was not (spec § Assumptions records the same reachability nuance). While rewriting the L-066-1 row, **refresh its stale internal citation**: it cites "`dictionary.cpp:335`'s NumInGroup gate"; the gate is now at `:398` (pre-existing drift, but FR-019 rewrites that row anyway). Exit condition is SC-010: no remaining open L-063-1 / L-061-1 / L-066-1 / L-077-1 carve-out.
- [ ] T051 Add the **FR-006c** behavior-change row + release note (the ungated FIX40/41/42 parse correction) **and** the **FR-023** member-less-`<group>` load-rejection row + release note to `spec/behaviors-and-limitations.md`, stating explicitly that **zero vendored dictionaries are affected** by the rejection (T007/T011's evidence).
- [ ] T052 Amend `.specify/constitution.md` — **annotation-only**, in **this** branch, per the unbroken v0.5 (069) / v0.6 (074) / v0.7 (075) / v0.8 (076) / v0.9 (077) / v0.10 (078) precedent, **not** a standalone `Constitution: amend …` PR (FR-020 / SC-011 / D-13): (a) Article XVIII §7's bolded "**`fixpp::v42` builders remain DEFERRED**" sentence (`:386`) becomes a delivered-by-082 record; (b) the Status banner (`:85`) gains a **v0.11** line naming feature 082; (c) Article I §1's codegen scope (`:94`) is confirmed **unchanged** — it already reads "FIX 4.2, FIX 4.4, FIX 5.0 SP2, FIXT.1.1", so this amendment is permissive and widens nothing; (d) the v0.9 amendment-log entry at `:18` is **left intact** as historical record. Article XX §2's user-ratification precondition is **already satisfied — given 2026-07-30** (`spec.md` § Open decisions OD-2); this task is the edit itself.
- [ ] T053 Run the full `quickstart.md` validation — S0, S0b, S1, S1b, S2, S3, S4, S5, S6, S7, S8, S9 — and the "Full local gate". Use regex alternation, never repeated `-L`: `ctest --test-dir <build> -L "codegen|dictionary|wire|session"` (expected 60 tests; codegen 32, dictionary 16, wire 4, session 9, `wire|session` 13 — the 61-vs-60 sum gap is the genuinely double-labelled `validator_legacy_char_type_test`, `LABELS "075;wire;dictionary"`). `ctest -L codegen` is **mandatory** because `tools/codegen/**` is touched.

### Mandatory close-out tasks (ALWAYS emit — Gate-B preconditions, Article XVII §8)

- [ ] T054 [P] **Catalogue close-out**: flip every feature-owned OFFICIAL row in `spec/feature-catalogue.md` to `done` (with the PR / evidence ref) and add/update its matching `spec/coverage-index.md` entry. 082 introduces **no new** OFFICIAL rows — it makes existing FIX 4.0–4.3 coverage correct — so this task's scope is the existing rows the spec's § Normative References names: W-014 (`coverage-index.md:189`), M-002 (`:239`, whose `v44`-only satisfaction US1/US4 extend to `v42`), and A-002 (`:307`, whose "full-field + all-version coverage deferred" note US2 discharges for `v42`).
- [ ] T055 **Feature-completeness audit (FINAL task)**: assert against the merged tree that (i) every `tasks.md` row is `[X]` or carries an explicit waiver rationale; (ii) every one of the **28** FRs (FR-001..FR-023 plus 006a/006b/006c/016a/016b) and all **13** SCs maps to a landed test **and** a landed implementation; (iii) every feature-owned OFFICIAL catalogue row is `done` with a matching `coverage-index.md` entry. Record the verdict (100% or fully-waived) in `.specify/decisions/082-structural-group-detection-verify.md` under `## Completeness`, or a sibling `082-structural-group-detection-completeness.md`. **This is the hard `/gate-b` precondition** (Article XVII §8 / pre-flight 4d) — `/gate-b` HARD-BLOCKS without it.

---

## Dependencies & Execution Order

### Phase Dependencies

- **Phase 1 (Setup)** → no dependencies. **Must run before T023** — T002/T003/T004 capture state that stops existing once the predicate changes.
- **Phase 2 (Foundational)** → depends on Phase 1. **BLOCKS all user stories.** Within it, step 1 (T005–T008) and step 2b (T009–T014) are mutually independent and may proceed in parallel.
- **Phase 3 (US1, P1)** → depends on Phase 2. The MVP.
- **Phase 4 (US2, P1)** → depends on T023–T026 (groups must be visible before entering the builder tier).
- **Phase 5 (US3, P2)** → depends on T023 (shared implementation; US3 contributes pins only).
- **Phase 6 (US4, P3)** → depends on US1 **and** US2.
- **Phase 7 (Polish)** → depends on all stories. T046 depends on T002; T048 depends on T003; T049 depends on T046–T048. T055 must be **last**.

### User Story Dependencies

- **US1** — foundational; every other story is blocked on it (it is the root cause).
- **US2** — depends on US1's group visibility.
- **US3** — depends on US1's predicate change, but is otherwise independent of US2/US4 and independently testable.
- **US4** — depends on US1 + US2.

### Within Each User Story

RED pins first (they must fail against unchanged code), then implementation, then GREEN verification.

### Parallel Opportunities

- Phase 1: T002, T003, T004 all `[P]`.
- Phase 2: the oracle thread (T005→T006→T007→T008) and the loader thread (T009/T010/T011 `[P]` → T012/T013 → T014) run concurrently.
- Phase 3: T015–T022 all `[P]` (distinct assertions; T015/T016/T017/T018 share `required_scope_census_test.cpp`, so serialize the edits or partition by fixture).
- Phase 4: T031–T034 all `[P]`.
- Phase 5: T040, T041, T042 all `[P]`.
- Phase 7: T054 `[P]` with T050–T053.

## Parallel Example: User Story 1 RED pins

```
# After Phase 2 completes, launch the US1 pins together (partition the shared
# test file by fixture, or land them sequentially within that one file):
T015 FIX42 18-tag exact-set          → tests/dictionary/required_scope_census_test.cpp
T016 FIX40 4 / FIX41 7 exact-set     → tests/dictionary/required_scope_census_test.cpp
T017 tag-146 per-context member sets → tests/dictionary/required_scope_census_test.cpp
T018 six-unchanged exact-set         → tests/dictionary/required_scope_census_test.cpp
T019 v42 18 class G_ / 46 classes    → tests/codegen/
T020 296→295 nested typed group      → tests/codegen/
T021 ungated parse, strict OFF       → tests/session/
T022 K6b cross-path capi pin         → tests/capi/
```

## Implementation Strategy

### MVP First (User Story 1 only)

Phase 1 → Phase 2 → Phase 3, then **stop and validate**: FIX40/41/42 register 4/7/18 groups, `v42`
reads 18 typed groups, the six EQUAL dictionaries provably unmoved. That alone converts three
shipping dictionaries from group-blind to membership-correct and closes L-063-1 / L-066-1.

### Incremental Delivery

1. **US1** — the enabling mechanism and the root-cause fix (closes L-063-1, L-066-1).
2. **US2** — the deliverable #196 asks for (closes L-077-1).
3. **US3** — the discovered FIX43 latent-defect fix, unavoidable once the predicate changes.
4. **US4** — the usability demonstration (closes L-061-1).
5. **Polish** — the three Article VIII benchmark legs, B&L + constitution closure, close-out.

### Traps this ordering exists to avoid

- **Circular census** — T005–T008 before T023, or the oracle moves in lockstep with the code it checks and witnesses nothing.
- **Unobtainable "before"** — T002/T003/T004 before T023; `table_view_footprint_bench` has no checked-in baseline today.
- **Order-dependent fail-closed rule** — T012/T013's check outside the first-seen dedup guards, and T009/T010's fixture at a non-first-seen occurrence, or a wrongly-placed check passes its own pin.
- **Half-restructure** — T023 changes all three `as_table_view()` sites in one unit (FR-004).
- **Emitter staleness** — T001 and T026 force a clean rebuild; non-debug dirs otherwise compile a stale `Reify.hpp`.
- **Transcribed instead of derived** — T031 derives the plan counts from the interning rule before the first run; T033's expected set comes from that derivation.
- **Proxy pin** — T017 asserts per-context member sets on a **divergent** tag (146), not a tag-set projection and not tag 33.
- **Vacuous ctest selection** — T053 uses regex alternation; repeated `-L` is conjunctive and selects nothing.
- **Label-filtered miss** — `ctest -L codegen` is mandatory because `tools/codegen/**` is touched.
