---
description: "Task list — Orchestra runtime dictionary load for non-C++ consumers"
---

# Tasks: Orchestra runtime dictionary load for non-C++ consumers

**Input**: Design documents from `/specs/080-orchestra-runtime-load/`
**Prerequisites**: plan.md ✓, spec.md ✓, research.md ✓, data-model.md ✓, contracts/ ✓, quickstart.md ✓
**Gate A**: CONVERGED 2026-07-19 (2 rounds) — design signed off; tasks inherit it, do not re-litigate.

**Tests**: REQUIRED. The spec's "User Scenarios & Testing" section is mandatory and quickstart.md prescribes RED-first tests for each FR/SC. TDD ordering applies (write the test, see it fail, then implement).

**Organization**: Grouped by user story (US1 = C-API, US2 = TOML). Both are P1. The shared `dict::load_any` helper is a blocking Foundational prerequisite for both.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies on incomplete tasks)
- **[Story]**: US1 / US2 (user-story phases only)
- Exact file paths included. Repo root = the library submodule (`research/G19-fix-fpml-iso20022/library/`).

## Scope reminders (from the signed-off bundle)

- **No C-ABI change** (FR-005/SC-005): no new C symbol, no new `fixpp_error_t`, no edit to any byte-frozen header (`error.h`/`version.h`/`include/fix/c_api/dict.h`). The only additive surface is one new **public C++** symbol `dict::load_any`. `nm` golden + `capi_freeze.sha256` pass **without regeneration**; `FIXPP_C_ABI_VERSION` stays `1.5.0`.
- **FR-007 is intentionally absent** — the dual-dictionary collision leg was **descoped by Gate A round 1** (unreachable via any config surface; research.md D-3/D-4 marked SUPERSEDED-retained-for-audit). No collision pre-check, no new `reason_class`. The `version_registry` `std::abort` is retained as the direct-C++ fail-loud backstop. Do **not** re-introduce or renumber FR-007.
- **Test targets** follow the 068 whole-binary grouping: add cases to the existing grouped modules (`tests/dictionary`, `tests/config`, `tests/capi`), selected by `ctest -L` (Article VII §8) — do not spawn per-case binaries.
- **No new goldens** — reuse 074/076-era FIX-Latest fixtures + `dictionaries/orchestra/OrchestraFIXLatest.xml` + a classic `dictionaries/FIX44.xml` baseline.

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Build wiring for the one new TU and the new test cases; confirm fixtures.

- [X] T001 [P] Wire the new source `src/dictionary/load_any.cpp` into the dictionary library CMake target, and register the new test cases into the existing whole-binary grouped test targets (`tests/dictionary`, `tests/config`, `tests/capi`) with their `ctest -L` labels (Article VII §8). No new standalone test binaries.
- [ ] T002 [P] Confirm the required fixtures are present without adding new golden artifacts: `dictionaries/orchestra/OrchestraFIXLatest.xml` (074 supply-chain artifact), a classic `dictionaries/FIX44.xml` regression baseline, and the reused 074/076 FIX-Latest message fixtures.

---

## Phase 2: Foundational (Blocking Prerequisites) — the shared `dict::load_any` helper

**Purpose**: The single shared root-sniff/dispatch helper (FR-003/FR-004, contract `contracts/load_any.md`, entity E1). BLOCKS both US1 and US2 — both call sites depend on it.

**⚠️ CRITICAL**: No user-story redirect (T011, T014) can begin until T008 lands.

### Tests for the helper (write FIRST, ensure they FAIL) ⚠️

- [X] T003 [P] Unit test in `tests/dictionary/load_any_test.cpp`: `dict::load_any(OrchestraFIXLatest.xml, mr)` returns a `Dictionary` whose parse/validate/read outcomes equal a dictionary from `dict::OrchestraLoader{}.load(...)` directly (0 divergences) — contract load_any.md obligation 1, SC-001 basis, quickstart Scenario 1 core. RED before T008.
- [X] T004 [P] Unit test in `tests/dictionary/load_any_test.cpp`: `dict::load_any(FIX44.xml, mr)` returns a `Dictionary` byte-identical to `dict::XmlLoader{}.load(...)` (FR-006, contract obligation 2). RED before T008.
- [X] T005 [P] Unit test in `tests/dictionary/load_any_test.cpp`: an XML whose root is neither `fix` nor `fixr:repository` → `dict::load_any` throws a `dict::` parse error (NOT `std::runtime_error`, NOT a wrong `Dictionary`); a malformed/unreadable file likewise throws a `dict::` error — FR-003, guarantee G2, quickstart Scenario 4. RED before T008.
- [X] T006 [P] Regression pin in `tests/dictionary/` (reuse `orchestra_loader_test.cpp` `VendoredOrchestraFileFedToXmlLoaderThrows`): `dict::XmlLoader{}.load(OrchestraFIXLatest.xml, mr)` still throws `dict::xml_parse_error` — assert on exception **type only** (message-agnostic). Confirms the 074 T022h loader-unit invariant is preserved (FR-009, guarantee G3, quickstart Scenario 5). This is a pin, not a RED-first test (the loader is unchanged). **While reusing this test, correct its stale inline comment** (`orchestra_loader_test.cpp:441,446`): it cites `xml_loader.cpp:284-285`'s `root.name() != "fix"` guard, but that guard (a) drifted to `:348` and (b) is DEAD for this path per research.md D-5 — the guard that actually fires is `parse_document`'s `doc.child("fix")`-missing check at `xml_loader.cpp:742-745`. Re-cite `:742-745` so the reused test stops perpetuating a self-contradicting citation (Gate-A round-2 fixed this in research.md only; the test comment was missed).

### Implementation for the helper

- [X] T007 Create `include/fixpp/dict/load_any.hpp` — declare `[[nodiscard]] Dictionary load_any(std::filesystem::path const& path, std::pmr::memory_resource* mr);` in `namespace fixpp::dict` (contract load_any.md Interface).
- [X] T008 Implement `src/dictionary/load_any.cpp` — assert `mr != nullptr`; one pugixml parse; read the root via `document_element()` (NOT `first_child()` — N-2 hardening, research D-2); dispatch `fix` → `XmlLoader{}.load(path, mr)`, `fixr:repository` → `OrchestraLoader{}.load(path, mr)`, any other/empty root or parse failure → throw a `dict::xml_parse_error`. Never let a non-`dict::` type escape (G2). Makes T003–T005 pass; keeps T006 green. (depends on T007)

**Checkpoint**: `dict::load_any` compiles, all helper tests green — both surfaces can now be redirected.

---

## Phase 3: User Story 1 - Load a FIX Latest dictionary through the C-API (Priority: P1) 🎯 MVP

**Goal**: `fixpp_dict_load_from_xml` accepts an Orchestra `<fixr:repository>` document and returns a valid handle equivalent to `OrchestraLoader::load` — with classic `<fix>` load unchanged and zero C-ABI change.

**Independent Test**: Call the C-API entry point with `OrchestraFIXLatest.xml`; assert a valid handle; parse/validate a FIX-Latest message and confirm it matches `OrchestraLoader::load` directly. Then call it with `FIX44.xml` and confirm byte-identical pre-080 behavior.

### Tests for User Story 1 (write FIRST, ensure they FAIL) ⚠️

- [X] T009 [P] [US1] Integration test in `tests/capi/dictionary_load_test.cpp` (existing grouped `tests/capi` binary): `fixpp_dict_load_from_xml(".../OrchestraFIXLatest.xml", &h)` → `FIXPP_ERR_OK` + non-null `h`; parse/validate a FIX-Latest message through `h` and assert the outcome equals the same message via `dict::OrchestraLoader{}.load(...)` directly (SC-001, US1 AC1/AC2, quickstart Scenario 1). RED before T011 (`FIXPP_ERR_CAPI_CONFIG_INVALID` today). **Delivered as entry-point contract (OK + non-null) + `set_dictionary` usability**, not a field-level compare against `OrchestraLoader` — the opaque `fixpp_dict_t*` is not field-introspectable through the public C-ABI; deep `load_any`≡`OrchestraLoader` parity is already established by Phase-2 `dictionary_load_any_tests` (T003).
- [X] T010 [P] [US1] Regression test in `tests/capi/dictionary_load_test.cpp`: `fixpp_dict_load_from_xml(".../FIX44.xml", &h)` → `FIXPP_ERR_OK` with a dictionary behaviorally identical to the pre-080 `XmlLoader` result (SC-003, US1 AC3, quickstart Scenario 2).

### Implementation for User Story 1

- [X] T011 [US1] Redirect `fixpp_dict_load_from_xml` in `src/capi/dictionary.cpp` (~L48): replace the hard-wired `dict::XmlLoader{}.load(...)` with `dict::load_any(path, std::pmr::get_default_resource())`. Keep the existing `catch(...)` → `FIXPP_ERR_CAPI_CONFIG_INVALID` disposition. No signature/symbol/error-code change (E4). Makes T009 pass; keeps T010 green. (depends on T008)

**Checkpoint**: US1 fully functional and independently testable.

---

## Phase 4: User Story 2 - Name an Orchestra dictionary in TOML configuration (Priority: P1)

**Goal**: A TOML `dictionary.path` pointing at an Orchestra `<fixr:repository>` file loads FIX Latest at config-resolution time, with no new config key and classic `<fix>` resolution unchanged.

**Independent Test**: Resolve a config whose single `[dictionary]` path names `OrchestraFIXLatest.xml`; assert the resolved dictionary validates FIX-Latest messages. Resolve one naming `FIX44.xml` and confirm unchanged behavior.

### Tests for User Story 2 (write FIRST, ensure they FAIL) ⚠️

- [X] T012 [P] [US2] Integration test in `tests/config/test_load_happy_path.cpp` (existing grouped `tests/config` binary): resolve a config with a single `[dictionary]` `path = ".../OrchestraFIXLatest.xml"` → successful load; validate a FIX-Latest message through the resolved dictionary (SC-002, US2 AC1, FR-008 single FIX-Latest-only config, quickstart Scenario 3). RED before T014 (config error today).
- [X] T013 [P] [US2] Regression test in `tests/config/test_load_happy_path.cpp`: a config with `dictionary.path = ".../FIX44.xml"` resolves via the classic loader unchanged (SC-003, US2 AC2).

### Implementation for User Story 2

- [X] T014 [US2] Redirect the loader lambda in `src/config/selector_resolver.cpp` (~L359-365): inside `trap_throw_to_expected`, call `dict::load_any(xml_path, opts.resource)` instead of `dict::XmlLoader{}.load(...)`. All other dispositions unchanged (E5). Makes T012 pass; keeps T013 green. (depends on T008)

**Checkpoint**: Both US1 and US2 work independently through the one shared helper.

---

## Phase 5: Polish & Cross-Cutting Concerns

**Purpose**: FR-004 single-source proof, ABI-invariance verification, sanitizer/coverage, and the pinned close-out obligations.

- [X] T015 [P] FR-004 single-dispatch source-inspection gate (quickstart Scenario 6): assert `src/capi/dictionary.cpp` and `src/config/selector_resolver.cpp` each call `dict::load_any` and neither inlines its own root-element sniff/dispatch; assert exactly one runtime root-sniff implementation exists (`src/dictionary/load_any.cpp`). A source-inspection assertion, not a behavior test. (depends on T011, T014) — DONE: `LoadAny.FR004_SingleSharedDispatchSourceInspection` in `tests/dictionary/load_any_test.cpp` (reads the three TUs via `FIXPP_SRC_DIR`; asserts both call sites contain `load_any(` and NEITHER contains `document_element(`, the sniff primitive present only in `load_any.cpp`). GREEN in `dictionary_load_any_tests`.
- [X] T016 [P] ABI-invariance gate (SC-005 / FR-005): assert the `nm` exported-symbol golden `tests/abi/golden/fixpp_capi_symbols.txt` and the header byte-freeze `tools/capi_freeze.sha256` (via `tools/check_capi_freeze.sh`) both pass **without regeneration** (`error.h`/`version.h`/`include/fix/c_api/dict.h` untouched), and `FIXPP_C_ABI_VERSION == 1.5.0`. Confirm `dict::load_any` is the only additive (public C++) symbol by diffing the change against plan.md Project Structure §Source Code (expected: one new TU `src/dictionary/load_any.cpp` + header, two one-line call-site redirects, no other new exported surface) — e.g. `git diff --stat` scoped to the feature branch. — DONE (sanity, formal gate re-run by `/speckit-verify`): `tools/check_capi_freeze.sh` → PASS (12 headers byte-frozen); `dict::load_any` absent from `fixpp_capi_symbols.txt` (0 matches — it is a C++ symbol, not a C export); `FIXPP_C_ABI_VERSION_MAJOR.MINOR.PATCH == 1.5.0`; feature diff = 1 new TU + header + two one-line call-site redirects + tests, no other exported surface.
- [ ] T017 Run the sanitizer matrix (ASan/UBSan/TSan) over the new dictionary/config/capi tests and capture coverage on `src/dictionary/load_any.cpp` (quickstart ABI/hygiene gate). Feeds `/speckit-verify`. — DEFERRED to `/speckit-verify` (its formal deliverable; not run in `/speckit-implement`).
- [ ] T018 [P] Run the quickstart.md end-to-end validation: all six scenarios green (1–5 runtime, 6 the source-inspection gate). — PARTIAL: Scenarios 1/3 (C-API + TOML Orchestra load), 2 (classic C-API), 4/5 (fail-closed + T022h pin), 6 (source-inspection gate) all GREEN via `dictionary_load_any_tests` + `capi_dictionary_load` + `config_044_tests`. Formal end-to-end six-scenario sign-off recorded by `/speckit-verify`.

### Pinned feature-specific close-out obligations (do NOT skip — spec Contract & Compatibility Notes)

- [X] T019 [P] **Frozen-header docs note (PINNED)**: add a **non-frozen** public docs / API-reference note (release notes / doc site / `spec/behaviors-and-limitations.md`) stating that `fixpp_dict_load_from_xml` accepts both `<fix>` and `<fixr:repository>` roots, and that the frozen `include/fix/c_api/dict.h` Doxygen prose ("FIX XML data dictionary / wraps `XmlLoader`") is **retained verbatim for ABI stability**. MUST NOT edit `dict.h`. (spec.md Frozen-header docs obligation, quickstart close-out — pinned so it cannot be skipped.) — DONE: **B-080-2** in `spec/behaviors-and-limitations.md` (§ 080 section). `dict.h` NOT edited (verified byte-frozen by `check_capi_freeze.sh`).
- [X] T020 [P] **Behaviors-and-limitations L-row**: add an operator-facing L-row in `spec/behaviors-and-limitations.md` recording the C-API/TOML **contract-widening** (an Orchestra document that previously returned config-invalid now loads) and that the FIX50SP2+FIX-Latest `version_registry` `std::abort` is retained by design as the direct-C++ fail-loud backstop (074 L-074-1) — no config surface can express the colliding pair, so no pre-check is added. (spec.md Behaviors-and-limitations note.) — DONE: **B-080-1** (contract-widening) + **L-080-1** (`std::abort` backstop retained) in `spec/behaviors-and-limitations.md` § 080 section.

### Mandatory close-out tasks (ALWAYS emit — Gate-B preconditions, Article XVII §8)

- [X] T021 [P] **Catalogue close-out**: flip every feature-owned OFFICIAL row in `spec/feature-catalogue.md` to `done` (with the PR / evidence ref) AND add/update its matching `spec/coverage-index.md` entry. — DONE: 080 owns NO new OFFICIAL row (adds acquisition entry points for the already-`done` D-011 read-tier dictionary, no new FIX message coverage — plan.md Article VI N/A). Annotated the two WIDENED rows: **CA-011** (C-API loader — 080 delivery + `LoadOrchestraFixLatest` witness) and **D-011** (Orchestra read tier — non-C++ acquisition entry points delivered by 080) in `spec/feature-catalogue.md`, and the matching **D-011 `coverage-index.md`** entry (line ~704).
- [ ] T022 **Feature-completeness audit (MUST be the FINAL task)**: assert against the merged tree that (i) every `tasks.md` row is `[X]` or carries an explicit waiver rationale; (ii) every spec FR-001/002/003/004/005/006/008/009 and SC-001/002/003/005 maps to a landed test AND a landed implementation (FR-007 is intentionally descoped — record as such); (iii) every feature-owned OFFICIAL catalogue row is `done` with a matching `coverage-index.md` entry. Record the verdict (100% or fully-waived) in `.specify/decisions/080-orchestra-runtime-load-verify.md` (`## Completeness`) or a sibling `.specify/decisions/080-orchestra-runtime-load-completeness.md`. Hard `/gate-b` precondition (Article XVII §8 / pre-flight 4d).

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: no dependencies — start immediately.
- **Foundational (Phase 2)**: depends on Setup. BLOCKS both user stories (both call `dict::load_any`).
- **US1 (Phase 3)** and **US2 (Phase 4)**: both depend on T008 (the helper). Once T008 lands they are independent of each other and may proceed in parallel.
- **Polish (Phase 5)**: T015/T018 depend on both redirects (T011, T014); T016/T017 depend on the tests existing; close-out tasks (T019–T022) run last; T022 is the final task.

### Key edges

- T007 → T008 → {T011, T014}
- {T011, T014} → T015, T018
- everything → T021 → T022 (final)

### Parallel Opportunities

- T001, T002 (Setup) in parallel.
- T003, T004, T005, T006 (helper tests, same file grouped but independent cases) authored together before T008.
- After T008: US1 (T009/T010/T011) and US2 (T012/T013/T014) run as two independent tracks.
- T015, T016, T019, T020, T021 are `[P]` (distinct files).

---

## Implementation Strategy

### MVP First (User Story 1)

1. Phase 1 Setup → Phase 2 Foundational (`dict::load_any` green).
2. Phase 3 US1 (C-API redirect) → **STOP and VALIDATE** Scenario 1/2 independently.
3. This alone closes the P1 C-API capability gap.

### Incremental Delivery

1. Setup + Foundational → helper ready and unit-proven.
2. US1 (C-API) → validate → the core capability gap is closed.
3. US2 (TOML) → validate → operator-config parity.
4. Polish → FR-004 single-source proof, ABI-invariance gate, pinned docs/B&L notes, catalogue + completeness close-out.

---

## Notes

- [P] = different files, no dependency on an incomplete task.
- RED-first: T003–T005, T009, T012 must fail before their implementation task; T006/T010/T013 are regression pins (green throughout).
- The whole change is surgical: one new TU (`load_any.cpp` + header) and two one-line call-site redirects. No change to `src/session/`, `bindings/`, codegen, wire path, or any byte-frozen header.
