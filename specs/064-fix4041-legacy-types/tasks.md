---
description: "Task list — FIX 4.0/4.1 dictionary loader legacy-type support (064 / D-004)"
---

# Tasks: FIX 4.0/4.1 dictionary loader legacy-type support (A-5 / D-004)

**Input**: Design documents from `specs/064-fix4041-legacy-types/`
**Prerequisites**: plan.md, spec.md, research.md (R1–R5), data-model.md (E-1..E-4), contracts/loader-vocabulary-contract.md
**Repo root** = the library submodule (`research/G19-fix-fpml-iso20022/library/`); all paths below are relative to it.

**Tests**: REQUIRED (red-first) — plan.md Constitution Check **VII §3/§4** mandates a test before any code; the two vendored dicts fail-load *before* the loader rows (RED) and pass *after* (GREEN), and the AC-L8 negative witness stays green throughout.

**Organization**: Tasks are grouped by the three user stories from spec.md (US1 P1 = load + typing, US2 P1 = pre-FIXT session lookups, US3 P2 = fail-closed guardrail). The single production-code change (two `kFieldTypeTable` collapse rows) lives in **US1** because it is authored test-first against US1's RED loadability test; US2 and US3 depend on it.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependency on an incomplete task)
- **[Story]**: `[US1]`/`[US2]`/`[US3]` for story-phase tasks; Setup/Foundational/Polish carry no story label

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Establish a measurable RED/GREEN baseline before any change.

- [ ] T001 Confirm branch is `064-fix4041-legacy-types`, then configure + build the three dictionary test targets from a clean tree per quickstart.md (`conan install . -of build/clang-debug --build=missing -s build_type=Debug`; `cmake --preset clang-debug && cmake --build build/clang-debug --target dictionary_lookup_test dictionary_negative_paths_test dictionary_xml_loader_test -j2`) so RED/GREEN transitions are observable.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Vendor the two data files every user story's tests load. **BLOCKS all user stories** — no story test can run until the XMLs are present and verified.

**⚠️ CRITICAL**: No user story work can begin until this phase is complete.

- [ ] T002 [P] Vendor `dictionaries/FIX40.xml` verbatim from `quickfix/quickfix @ 19ef6a4c` (`spec/FIX40.xml`) per the quickstart.md fetch recipe (SHA read from `dictionaries/UPSTREAM.txt`). (FR-004, E-3)
- [ ] T003 [P] Vendor `dictionaries/FIX41.xml` verbatim from `quickfix/quickfix @ 19ef6a4c` (`spec/FIX41.xml`) per the same recipe. (FR-004, E-3)
- [ ] T004 Verify `dictionaries/FIX40.xml` and `dictionaries/FIX41.xml` are byte-identical to upstream at the pinned SHA, and assert `dictionaries/UPSTREAM.txt` SHA is **unchanged** (`19ef6a4c`, no pin drift). (confirm-at-implement #2; contract checklist #5)
- [ ] T005 Re-enumerate every distinct `<field type=…>` attribute in the **vendored** `dictionaries/FIX40.xml`/`FIX41.xml` and assert the only names outside the current `kFieldTypeTable` vocabulary are `DATE` and `TIME` — guards research R1 against a fetch mismatch. (confirm-at-implement #1; Assumptions)

**Checkpoint**: Both dictionaries are on disk, pin-verified, and confirmed to need exactly the two legacy aliases. Loading them still THROWS (rows not yet added) — the RED precondition for US1.

---

## Phase 3: User Story 1 - Load a FIX 4.0/4.1 dictionary + resolve legacy types (Priority: P1) 🎯 MVP

**Goal**: `FIX40.xml`/`FIX41.xml` load to completion with no `xml_parse_error`; a `DATE` field resolves to `field_data_type::LocalMktDate` and a `TIME` field to `field_data_type::UtcTimestamp`. This is the entire feature — the two `[const §I.1]` versions that could not previously load.

**Independent Test**: Load both vendored dicts through `XmlLoader`; assert no throw, `TradeDate` → `LocalMktDate`, `SendingTime` → `UtcTimestamp`.

### Tests for User Story 1 (write FIRST — MUST FAIL before T007) ⚠️

- [ ] T006 [US1] Extend `tests/dictionary/xml_loader_test.cpp`: add cases that load `dictionaries/FIX40.xml` and `dictionaries/FIX41.xml` through `XmlLoader` asserting **no** `xml_parse_error`, and (via the existing `field_ref`/field-lookup pattern in that file) assert `TradeDate` resolves to `field_data_type::LocalMktDate` and `SendingTime` to `field_data_type::UtcTimestamp`. **Confirm RED** (loader throws on the first `DATE`/`TIME` field before T007). (SC-001, acceptance scenarios US1-1..4)

### Implementation for User Story 1

- [ ] T007 [US1] Add exactly two collapse rows — `{"TIME", field_data_type::UtcTimestamp}` and `{"DATE", field_data_type::LocalMktDate}` — to `kFieldTypeTable`'s post-canonical carve-out block in `src/dictionary/xml_loader.cpp:97-104`, matching the existing `TAGNUM`/`LOCALMKTTIME`/`XID`/`XIDREF` row style, with a comment citing FIX 4.0/4.1 legacy typing and the QuickFIX `DATE → Unknown` divergence. Do **not** modify the `field_data_type` enum. → turns T006 GREEN. (FR-001, FR-002, FR-003, E-1, contract checklist #1)

**Checkpoint**: US1 fully functional — both dicts load and both legacy types resolve. This is the shippable MVP; US2/US3 build on T007.

---

## Phase 4: User Story 2 - Pre-FIXT session-bearing lookups resolve (Priority: P1)

**Goal**: FIX 4.0/4.1 headline lookups assert their **session** messages (`Logon` A, `Heartbeat` 0) are **present** in-dictionary (pre-FIXT), plus at least one application message each — the inverse of the D-006 FIX 5.0 app-only shape.

**Independent Test**: Run `dictionary_lookup_test`; the two new `VersionParam` rows pass alongside the existing seven with no regression.

**Depends on**: T007 (rows must be landed for the dicts to load).

- [ ] T008 [US2] Add two `VersionParam` rows (FIX 4.0, FIX 4.1) to `tests/dictionary/lookup_test.cpp` (the `AllRuntimeVersions` suite): `filename` `FIX40.xml`/`FIX41.xml`, `expected_version` `session_version::v40`/`v41`, `required_msg_types` including session `A` and `0` **present** plus one app msgtype, `forbidden_msg_types` `{}` (NOT the D-006 app-only shape). Read the vendored files to fill the remaining struct fields (`required_group_no_tags`, `has_clordid`, `parties_expected`, `has_instrument`) accurately. Run the suite; confirm the seven pre-existing rows still pass unchanged. (SC-002, FR-005, E-4, acceptance scenarios US2-1..3)

**Checkpoint**: US1 + US2 both green; session-bearing versions correctly modeled.

---

## Phase 5: User Story 3 - Fail-closed contract holds for genuinely unknown types (Priority: P2)

**Goal**: The AC-L8 relaxation is exactly two named aliases, not a hole. A type outside both the `[FIX50SP2 §3.3]` vocabulary and the full collapse set still throws `xml_parse_error` / `dict_xml_parse_failed`.

**Independent Test**: The AC-L8 witness (`UNKNOWN_TYPE`) still throws; a companion confirms `DATE`/`TIME` now do **not** throw.

**Depends on**: T007.

- [ ] T009 [US3] Extend `tests/dictionary/negative_paths_test.cpp`: confirm the existing AC-L8 witness `AC_L8_UnknownFieldTypeThrowsXmlParseError` (`type='UNKNOWN_TYPE'`, `:230`) still throws `dict::xml_parse_error` with `.code() == dict_xml_parse_failed` **unmodified**, and add a companion case asserting a minimal dict with `<field type='DATE'>`/`<field type='TIME'>` now loads (does NOT throw). Note the SC-003 mutation intuition in a comment (deleting either T007 row re-fails the corresponding dict). (SC-003, FR-006, BG-2, acceptance scenarios US3-1..2)

**Checkpoint**: All three stories independently green; the relaxation is proven narrow.

---

## Phase 6: Polish & Cross-Cutting Concerns

**Purpose**: Record the divergence, refresh vendoring docs, validate end-to-end, and close out.

- [ ] T010 [P] Add a `B-`/`L-` row to `spec/behaviors-and-limitations.md`: `DATE` typed `LocalMktDate` where QuickFIX resolves `TYPE::Unknown` (no validation) — a deliberate stronger-typing choice, **metadata-only** (both `LocalMktDate` and `UtcTimestamp` collapse to `field_type::String` per `include/fixpp/dict/field_type.hpp:98-114`, so no message QuickFIX accepts is rejected), citing the QuickFIX `DataDictionary::XMLTypeToType` anchor and the FIX 4.2+ successor-typing rationale. The `TIME → UtcTimestamp` mapping agrees with QuickFIX and needs no divergence row. (FR-009, SC-005, BG-3, contract checklist #3)
- [ ] T011 [P] Update `dictionaries/README.md` to list `FIX40.xml`/`FIX41.xml` and extend the refresh-recipe that D-006 Gate B scoped to exclude them (now supported). (FR-004, confirm-at-implement #3, contract checklist #4)
- [ ] T012 Run the quickstart.md validation end-to-end: rebuild + `ctest --test-dir build/clang-debug -R 'dictionary_(lookup|negative|xml_loader)' --output-on-failure` (all green). Discharge **SC-006** by statically enumerating `<field type=…>` attributes across the seven already-vendored FIX 4.2+ dictionaries (`FIX42.xml`, `FIX43.xml`, `FIX44.xml`, `FIX50.xml`, `FIX50SP1.xml`, `FIX50SP2.xml`, `FIXT11.xml`) and confirming zero occurrences of `type='DATE'`/`type='TIME'` — a name the loader never encounters cannot change what it resolves to, so no vendored file's resolved typing changes. (Note: this is a static grep-enumeration check, not a runtime before/after diff of resolved `field_data_type` values — the `AllRuntimeVersions` `VersionParam` rows staying green confirms message/group/component structure is unaffected, but is not itself the SC-006 mechanism.) Then run the SC-004 scope-guard `git diff --name-only main...HEAD` path-allowlist to confirm the diff touches only `src/dictionary/xml_loader.cpp`, `dictionaries/`, `tests/dictionary/`, `specs/064-*`, and `spec/{behaviors-and-limitations,feature-catalogue}.md` — no public header / C-ABI / wire / error file, and **no `spec/coverage-index.md`** (T013 must leave it untouched — see T013). (SC-004, SC-006, FR-008, BG-4, BG-5)

### Mandatory close-out tasks (ALWAYS emit — Gate-B preconditions, Article XVII §8)

- [ ] T013 [P] **Catalogue close-out**: flip the **D-004** (FIX 4.0 / FIX 4.1) rows in `spec/feature-catalogue.md` to `done` with this PR as evidence, closing the A-5 "dictionary XMLs" work item and the last `[const §I.1]` all-nine-versions runtime-XML gap. **Also rewrite each D-004 row's rationale/evidence text**, not just the status field — drop the retired `DATE → UtcDateOnly` hypothesis currently in the row and replace it with the shipped `TIME → UtcTimestamp` / `DATE → LocalMktDate` mapping (per research R2/R3), so no `done` row cites the rejected mapping. **Do NOT touch `spec/coverage-index.md`**: per spec §Normative References this feature adds **no** new OFFICIAL FIX rows, so no coverage-index entry is owed — record that N/A rationale in the T014 completeness decision doc (`.specify/decisions/064-fix4041-legacy-types-*.md`, gitignored), not by editing `coverage-index.md` (keeps the T012 scope-guard allowlist consistent). (FR-010, SC-007, contract checklist #6)
- [ ] T014 **Feature-completeness audit (FINAL task)**: against the merged tree, assert (i) every `tasks.md` row is `[X]` or carries an explicit waiver rationale; (ii) every FR-001..010 and SC-001..007 maps to a landed test AND a landed implementation/record; (iii) the D-004 catalogue rows read `done` and the coverage-index N/A is recorded. Write the verdict (100% or fully-waived) to `.specify/decisions/064-fix4041-legacy-types-verify.md` (`## Completeness` section) or a sibling `.specify/decisions/064-fix4041-legacy-types-completeness.md`. **Hard `/gate-b` precondition** (Article XVII §8 / pre-flight 4d).

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: no dependencies.
- **Foundational (Phase 2)**: depends on Setup; **BLOCKS all user stories** (data files must exist + be verified).
- **US1 (Phase 3)**: depends on Foundational. Contains the shared production change (T007).
- **US2 (Phase 4)** and **US3 (Phase 5)**: depend on **US1/T007** (the loader rows must be landed for the dicts to load / for `DATE`/`TIME` to be accepted). US2 and US3 are independent of each other and can run in parallel once T007 lands.
- **Polish (Phase 6)**: depends on US1–US3 complete; T014 is the final task.

### Within Each Story

- US1: T006 (test, RED) **before** T007 (rows, GREEN).
- US2/US3: assertions are GREEN-after-T007; RED-equivalent is the SC-003 mutation intuition (deleting a T007 row re-fails the dict).

### Parallel Opportunities

- **T002 ∥ T003** — vendor the two XML files (different files).
- **T008 ∥ T009** — US2 and US3 tests (different test files, both depend only on T007).
- **T010 ∥ T011 ∥ T013** — B&L row, README, catalogue (three different files).
- T004, T005, T006→T007, T012, T014 are sequential.

---

## Parallel Example: Foundational vendoring

```bash
# Vendor both dictionaries together (different files, pinned SHA):
Task: "Vendor dictionaries/FIX40.xml verbatim from quickfix @ 19ef6a4c"
Task: "Vendor dictionaries/FIX41.xml verbatim from quickfix @ 19ef6a4c"
```

## Parallel Example: Polish

```bash
# After US1–US3 green, land the three recording files together:
Task: "Add DATE-divergence B-/L- row in spec/behaviors-and-limitations.md"
Task: "List FIX40/41 + refresh recipe in dictionaries/README.md"
Task: "Flip D-004 rows to done in spec/feature-catalogue.md"
```

---

## Implementation Strategy

### MVP First (User Story 1)

1. Phase 1 Setup → measurable baseline.
2. Phase 2 Foundational → vendor + verify the two dicts (blocks everything).
3. Phase 3 US1 → RED loadability/typing test (T006), then the two collapse rows (T007) → GREEN. **This is the MVP** — both versions now load and type.
4. STOP and VALIDATE US1 independently.

### Incremental Delivery

- US1 (load + typing) → US2 (session lookups) → US3 (fail-closed guardrail) → Polish (record divergence, refresh docs, close out). Each story adds a guardrail around the MVP without breaking prior stories.

---

## Notes

- The ONLY production-code change is **T007** (two `constexpr` table rows). Every other task is vendored data, test, documentation, or catalogue — consistent with FR-008 (zero public/C-ABI/wire/error/layout surface change).
- `field_data_type` enum stays frozen (`[FIX50SP2 §3.3]`) — no variant added (FR-003).
- Commit after each task or logical group; verify tests fail before implementing (US1).
- The catalogue + completeness close-out tasks (T013/T014) are non-optional Gate-B preconditions — do not drop them.
