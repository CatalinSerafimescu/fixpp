---
id: 002-dictionary-xml-loader
title: Tasks — XML data dictionary loader (`fixpp::dict::XmlLoader` + `Dictionary`)
module: dictionary/
phase: 4
status: drafted
spec_kit_step: /tasks
last_updated: 2026-05-14
inherits_plan: specs/002-dictionary-xml-loader/plan.md (Gate A converged 2026-05-14)
inherits_spec: specs/002-dictionary-xml-loader/spec.md
---

# Tasks: 002-dictionary-xml-loader

**Input**: Design documents from `specs/002-dictionary-xml-loader/`
**Prerequisites**: plan.md, spec.md, research.md (D-1..D-20), data-model.md (7 entities), contracts/ (7 headers), quickstart.md

**Tests**: REQUIRED. TDD red-green-refactor ordering per `[const §VII.3]` — test files lead, source files follow.

**Organization**: Tasks are grouped by user story per spec.md §3. P1 stories are MVP; P2 stories follow.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies on incomplete tasks)
- **[Story]**: Maps to spec.md §3 user stories (US1, US2, US3, US4). US5 is deferred to F1 (spec.md §10).

## Path Conventions

Single C++23 library under the **library/** submodule root (`research/G19-fix-fpml-iso20022/library/`). All paths below are relative to that root. Tier-1 preset matrix per `[const §IX.6]` (research.md D-17).

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Conan dependency, on-disk XML data, reusable test helpers.

- [X] T001 [P] Add `requires("pugixml/1.14")` row to `conanfile.py` (research.md D-1, D-15; `[const §III.2]` / `[const §V.3]`). Regenerate lockfiles under all Phase-3 profiles.
- [X] T002 Vendor QuickFIX XML data files into `dictionaries/FIX42.xml`, `dictionaries/FIX44.xml`, `dictionaries/FIX50SP2.xml`, `dictionaries/FIXT11.xml` (verbatim copy from upstream `quickfix/quickfix` at pinned SHA); write `dictionaries/README.md` (pin rationale + refresh recipe) and `dictionaries/UPSTREAM.txt` (single-line `quickfix/quickfix @ <sha> tag=<tag> date=<YYYY-MM-DD>`) per research.md D-2.
- [X] T003 [P] Create `tests/support/pmr_allocation_tracking_resource.hpp` (seam #2 — PMR resource counting `operator new` invocations) and `tests/support/failing_pmr_resource.hpp` (seam #9 — PMR resource that throws `std::bad_alloc` on Nth allocate) per data-model.md "PMR allocation accounting" + research.md D-9.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Public headers, `core/` additive surface admission, CMake scaffolding. MUST complete before any user story tests compile.

**⚠️ CRITICAL**: No user story work can begin until this phase is complete.

### `core/` additive admissions (research.md D-3)

- [X] T004 [P] Append three new variants to `fixpp::core::error` enum in `include/fixpp/core/error.hpp` at unused slots: `dict_xml_parse_failed = 20`, `dict_unknown_version = 21`, `dict_xml_oom = 22` (research.md D-3, D-10; `[const §X.4]` forwards-compat). Non-renumbering; existing `decimal_*` slots 10..13 preserved.
- [X] T005 [P] Add sibling helper template `detail::trap_throw_or_throw<E, F>` in `include/fixpp/core/decimal_helpers.hpp` next to the existing `detail::trap_throw<F>`. Semantics: catch `std::bad_alloc` → throw `E{}`; rethrow any other exception unchanged (research.md D-3, D-5; `[arch §5.3]` exception-API carve-out).

### Public `dict/` headers (data-model.md Entities 1–7)

- [X] T006 [P] Create `include/fixpp/dict/field_ref.hpp` — `FieldRef` POD + `field_data_type` enum + `field_presence` enum per data-model.md Entity 1 / `[2c §4.1]`. Embed `static_assert(sizeof(FieldRef) == 16 && alignof(FieldRef) == 2 && std::is_standard_layout_v<FieldRef> && std::is_trivially_copyable_v<FieldRef>)` per AC-F1.
- [X] T007 [P] Create `include/fixpp/dict/component_ref.hpp` — `ComponentRef` POD per data-model.md Entity 2 / `[2c §4.2]`. Embed `static_assert(sizeof(ComponentRef) == 12 && std::is_trivially_copyable_v<ComponentRef>)` per AC-F2.
- [X] T008 [P] Create `include/fixpp/dict/group_ref.hpp` — `GroupRef` POD per data-model.md Entity 3 / `[2c §4.2]`. Embed shape `static_assert`s per AC-F3.
- [X] T009 [P] Create `include/fixpp/dict/version_profile.hpp` — `session_version` + `application_version` enums (`std::uint8_t` underlying) per data-model.md Entity 7 (subset of `[2c §4.3]`; full `version_profile` struct deferred).
- [X] T010 [P] Create `include/fixpp/dict/error.hpp` — `dict::xml_parse_error : std::runtime_error`, `dict::unknown_version_error : std::runtime_error`, `dict::xml_oom_error : std::bad_alloc`, each with `[[nodiscard]] fixpp::core::error code() const noexcept` accessor per data-model.md Entity 6 / research.md D-4, D-10.
- [X] T011 Create `include/fixpp/dict/dictionary.hpp` — loader-MVS subset of `[2c §4.3]`: `Dictionary` move-only owner with `field_ref`, `field`, `field_by_name`, `component`, `group`, `messages`, `which_session_version`, `required_fields`, `group_first_field`, `length_pair_data_tag`; private `handle_` shared_ptr-to-const `dict_metadata_handle` per data-model.md Entity 4. Copy ctor + copy-assign deleted (spec.md §A3). Every public method `const` `noexcept` (AC-D8). Depends on T006..T009.
- [X] T012 Create `include/fixpp/dict/xml_loader.hpp` — stateless `XmlLoader` with `Dictionary load(std::filesystem::path const&, std::pmr::memory_resource*)` and `Dictionary load_from_string(std::string_view, std::pmr::memory_resource*)` per data-model.md Entity 5 / `[2c §4.5]`. **No `load_overlay*`** per /clarify Q2 → A (spec.md §10 F2). Pugixml NOT exposed transitively (research.md D-15). Depends on T010, T011.

### CMake + test scaffolding

- [X] T013 Modify `src/dictionary/CMakeLists.txt` — switch `fixpp_dictionary` target from INTERFACE to STATIC; declare sources `xml_loader.cpp` + `dictionary.cpp`; link `fixpp::core` + Conan-provided `pugixml::pugixml`; preserve `add_library(fixpp::dictionary ALIAS fixpp_dictionary)` per quickstart.md §1.
- [X] T014 Modify `tests/dictionary/CMakeLists.txt` — register per-test executables (`dictionary_xml_loader_test`, `dictionary_lookup_test`, `dictionary_ref_shape_test`, `dictionary_negative_paths_test`, `dictionary_round_trip_test`, `dictionary_determinism_test`, `dictionary_parser_error_test`, `dictionary_pmr_allocation_test`, `dictionary_oom_injection_test`, `dictionary_concurrent_readers_test`); label each with `set_tests_properties(... PROPERTIES LABELS dictionary)` per quickstart.md §2.
- [X] T015 Modify `tests/fuzz/CMakeLists.txt` — register `fuzz_dict_xml_loader` libFuzzer target next to existing `fuzz_decimal_parse`; ASan + UBSan instrumentation per `[const §VII.7]`.

**Checkpoint**: All foundational scaffolding green; user story test files now compile against the declared surface.

---

## Phase 3: User Story 1 — Engine integrator loads FIX44 → `Dictionary` (Priority: P1) 🎯 MVP

**Goal**: `XmlLoader().load("dictionaries/FIX44.xml", mr)` returns a populated `Dictionary` by value; downstream `wire::Validator` consumes it without writing a custom parser.

**Independent Test**: Drop upstream QuickFIX `FIX44.xml` on disk, call `XmlLoader().load(...)`, assert AC-D6 headlines (`NewOrderSingle`/`ExecutionReport`/`Logon`/`Heartbeat`/`Reject`) and AC-D7 delimiter tags (`NoPartyIDs=453`, `NoAllocs=78`, `NoLegs=555`).

### Tests for User Story 1 (TDD — write FIRST, ensure FAIL before T022/T023)

- [X] T016 [P] [US1] Write `tests/dictionary/ref_shape_test.cpp` (seam #4) — re-assert AC-F1..F5 static_asserts in the consumer TU (catches future ABI drift); replaces `dictionary_smoke_test.cpp`.
- [X] T017 [P] [US1] Write `tests/dictionary/xml_loader_test.cpp` — AC-L1 positive-path load of `dictionaries/FIX44.xml`; assert returned `Dictionary` is populated (non-zero message count, `which_session_version() == v44`).
- [X] T018 [P] [US1] Write `tests/dictionary/pmr_allocation_test.cpp` (seam #2) — drive `XmlLoader::load` with a `pmr_allocation_tracking_resource` upstreaming a `monotonic_buffer_resource`; assert AC-P1 (every output-metadata byte allocated from `mr`) and NFR-002-2 (zero `operator new` calls during the entire `load*` call).
- [X] T019 [P] [US1] Write `tests/dictionary/round_trip_test.cpp` (seam #8) — load FIX44.xml, iterate every `(MsgType, tag)` pair via `messages()` + per-MsgType field walk; assert `field_ref(msg_type, tag)` ↔ `field(msg_type, tag)` agreement (AC-D1 canonical / AC-D2 `std::optional` alias) and AC-D5 iteration exhaustiveness.
- [X] T020 [P] [US1] Write `tests/dictionary/determinism_test.cpp` (seam #5) — load FIX44.xml twice in one process into two separate `Dictionary` values on two separate PMR arenas; hash `messages()` iteration order and per-MsgType FieldRef triples; assert byte-identical digests (NFR-002-4 within-process determinism — research.md D-6).
- [X] T021 [P] [US1] Write `tests/dictionary/concurrent_readers_test.cpp` (seam #6) — spawn N reader threads against one shared `Dictionary` performing `field_ref`/`field_by_name`/`messages` lookups; runs under the `linux-clang-tsan` preset; assert zero TSan reports (AC-T1, AC-T2, NFR-002-3).

### Implementation for User Story 1

- [X] T022 [US1] Implement `src/dictionary/dictionary.cpp` — `Dictionary` accessor bodies (`field_ref`, `field`, `field_by_name`, `component`, `group`, `messages`, `which_session_version`, `required_fields`, `group_first_field`, `length_pair_data_tag`); `detail::dict_metadata_handle` ctor allocates the heap-pinned block via `std::allocate_shared<dict_metadata_handle>(std::pmr::polymorphic_allocator<dict_metadata_handle>(mr), ...)` so the shared-control-block deallocator returns memory to the originating `mr` per data-model.md Entity 4 / `[2c §4.3]`. Every accessor `const` `noexcept` (AC-D8). Depends on T011, T016..T021 written-and-failing.
- [X] T023 [US1] Implement `src/dictionary/xml_loader.cpp` positive path — pull `pugixml.hpp` only in this TU (research.md D-15); walk the parsed DOM; emit sorted `FieldRef[]` per MsgType (bytewise lex order over `unsigned char`), `ComponentRef[]` by name, `GroupRef[]` by `no_tag`, `MessageEntry[]` by MsgType bytewise (research.md D-6); allocate every output byte via the caller's `mr` (AC-P1, NFR-002-2); parse `<fix major minor [servicepack]>` into `session_version` via the v1.0-supported-nine table (AC-L4 happy path); populate `_reserved = 0` on every POD per AC-F4. Depends on T022.

**Checkpoint**: User Story 1 fully functional. `XmlLoader{}.load("dictionaries/FIX44.xml", &arena)` returns a usable `Dictionary`; AC-L1, AC-D1..D8, AC-F1..F5, AC-T1..T2, AC-P1, NFR-002-2..NFR-002-4 all green on the positive path.

---

## Phase 4: User Story 2 — In-process negative-path loader via `load_from_string` (Priority: P1)

**Goal**: `XmlLoader().load_from_string(crafted_bad_xml, mr)` drives AC-L2..L9 negative-path assertions without on-disk fixtures cluttering the repo.

**Independent Test**: Feed each AC-L*-shaped malformed XML literal to `load_from_string` and assert the expected typed exception fires (`dict::xml_parse_error` / `dict::unknown_version_error` / `dict::xml_oom_error`).

### Tests for User Story 2 (TDD — write FIRST, ensure FAIL before T027)

- [X] T024 [P] [US2] Write `tests/dictionary/negative_paths_test.cpp` (seam #7) — one `TEST(NegativePaths, ...)` per AC-L*: unreadable path (AC-L2), malformed XML (AC-L3), unknown FIX major/minor (AC-L4), missing/non-numeric `<field number>` (AC-L5), duplicate `<field number="N">` (AC-L6), dangling `<component>` reference (AC-L7), `<field type="UNKNOWN_TYPE">` (AC-L8); each assert the matching `dict::xml_*` exception type via `EXPECT_THROW` + `code()` check.
- [X] T025 [P] [US2] Write `tests/dictionary/parser_error_test.cpp` (seam #10) — feed crafted XML triggering pugixml's `xml_parse_result.status != status_ok`; assert translation site `if (!result) throw dict::xml_parse_error(result.description())` fires; verify `.code() == fixpp::core::error::dict_xml_parse_failed` (AC-L3 translation isolation).
- [X] T026 [P] [US2] Write `tests/dictionary/oom_injection_test.cpp` (seam #9) — drive `load_from_string` with a `failing_pmr_resource` that throws `std::bad_alloc` on the Nth allocate; assert `dict::xml_oom_error` thrown (NOT `std::bad_alloc` escape), `.code() == dict_xml_oom`, no leak under ASan, partial state torn down deterministically (AC-L9, AC-P2).

### Implementation for User Story 2

- [X] T027 [US2] Extend `src/dictionary/xml_loader.cpp` negative paths — translate `pugi::xml_parse_result.status != status_ok` → `dict::xml_parse_error`; out-of-vocabulary `<fix major minor>` → `dict::unknown_version_error`; structural defects (missing/duplicate `<field number>`, dangling `<component>`, unknown `<field type>`) → `dict::xml_parse_error`; `std::filesystem` access error → `dict::xml_parse_error` wrapping the underlying message (research.md D-4); wrap the whole `load*` body in `core::detail::trap_throw_or_throw<dict::xml_oom_error>` so PMR `std::bad_alloc` translates to `dict::xml_oom_error` (AC-L9). No other exception escapes `load*` (NFR-002-5). Depends on T023, T024..T026 written-and-failing.

**Checkpoint**: User Story 2 fully functional. All AC-L2..L9 negative paths green under ASan + UBSan; OOM translation green; no leak.

---

## Phase 5: User Story 3 — Codegen consumer (offline-mode iteration) (Priority: P2)

**Goal**: `fixpp-codegen` (D-008) iterates `Dictionary::messages()` and walks each message's `(MsgType, tag)` field set to emit per-version `constexpr FieldRef[]` arrays.

**Independent Test**: Iterate `Dictionary::messages()` and walk per-MsgType `field()` lookups; assert exhaustive coverage of every `<field>` declared in the loaded XML (AC-D5 + round-trip seam #8).

### Tests for User Story 3

- [X] T028 [US3] Extend `tests/dictionary/round_trip_test.cpp` (already written in T019) with an exhaustive-coverage assertion: count `<field>` declarations in `dictionaries/FIX44.xml` via direct pugixml parse in the test scaffolding, then assert the round-trip walk visits exactly that many distinct `(MsgType, tag)` pairs across all messages — guards against silent under-iteration in `messages()` or per-MsgType `FieldRef` arrays.

**Checkpoint**: User Story 3 validation milestone — codegen-consumer iteration surface confirmed exhaustive against the source XML.

---

## Phase 6: User Story 4 — Multi-version session host (FIXT.1.1 + FIX5.0SP2) (Priority: P2)

**Goal**: Load `FIXT11.xml` and `FIX50SP2.xml` into distinct `Dictionary` values keyed by `which_session_version()`; session host wires both per `[FIXT §5.1]`.

**Independent Test**: Load both XMLs; assert FIXT11's admin headlines (`Logon`/`Logout`/`Heartbeat`/`TestRequest`/`ResendRequest`/`Reject`/`SequenceReset`) and FIX50SP2's application headlines (`NewOrderSingle`/`ExecutionReport`/`MarketDataRequest`/`MarketDataSnapshotFullRefresh`) appear in their respective dictionaries and **not** in each other.

### Tests for User Story 4

- [X] T029 [US4] Write `tests/dictionary/dictionary_lookup_test.cpp` — GoogleTest TYPED_TEST_P parameterized over `{FIX42, FIX44, FIX50SP2, FIXT11}`; for each version assert the full AC-D1..D8 surface per spec.md §4.2 + contracts/dictionary.hpp:
    - **AC-D1** (`field_ref(msg_type, tag)`): for one declared tag per version (e.g., `field_ref("D", 11).rule == Required` on FIX44 NewOrderSingle/ClOrdID) and one absent tag (e.g., `field_ref("D", 9999).rule == NotDeclared`).
    - **AC-D2** (`field(msg_type, tag)` `std::optional<FieldRef>` alias): same pairs as AC-D1; assert `has_value()` ↔ `rule != NotDeclared`.
    - **AC-D3** (`field_by_name`): `field_by_name("ClOrdID") == 11` per version that declares it; `field_by_name("definitely_unknown_xyz") == std::nullopt`; case-sensitive (verify `field_by_name("clordid") == std::nullopt` per research.md D-11).
    - **AC-D4** (`component`/`group`): `component("Instrument").has_value()` per version that declares it (all four shipped versions); `component("Parties").has_value()` on FIX44/50SP2/FIXT11 and `std::nullopt` on FIX42 (per spec.md §4.2 AC-D6 FIX42 sub-bullet); `group(453).has_value()` (NoPartyIDs) on versions that declare it; `group(9999) == std::nullopt`.
    - **AC-D5** (`messages` iteration): non-empty span; bytewise-sorted msg_type order (assert `std::ranges::is_sorted` over `unsigned char` per research.md D-6).
    - **AC-D6** (per-version headlines): per spec.md §4.2 AC-D6 sub-bullets — FIX44: D/8/A/0/3; FIX42: same five (subset); FIX50SP2: D/8 + MarketDataRequest + MarketDataSnapshotFullRefresh; FIXT11: A/5/0/1/2/3/4 (admin only, no application headlines).
    - **AC-D7** (`NoXxx` delimiter tags): FIX44 `group_first_field(453) != 0` (NoPartyIDs), `group_first_field(78) != 0` (NoAllocs), `group_first_field(555) != 0` (NoLegs); per-version equivalents where defined.
    - **AC-D8** (`noexcept` discipline): compile-time `static_assert(noexcept(std::declval<Dictionary const&>().field_ref({}, 0)))` and equivalents for every public accessor (`field`, `field_by_name`, `component`, `group`, `messages`, `which_session_version`, `required_fields`, `field_valid_for`, `group_first_field`, `length_pair_data_tag`); covers NFR-002-5 second clause.
    - **Cross-version isolation**: FIXT11 has no `messages()` entry with msg_type=="D"; FIX50SP2 has one with msg_type=="V" (MarketDataRequest).

  Uses the same `XmlLoader::load` surface — no new implementation required.

**Checkpoint**: User Story 4 fully functional. Multi-version coexistence verified across all four shipped versions.

---

## Phase 7: Polish & Cross-Cutting Concerns

**Purpose**: Fuzz harness, bench harness, layer-edge lint, baselines, verification gate.

- [X] T030 [P] Add `tests/fuzz/fuzz_dict_xml_loader.cpp` libFuzzer harness per `[const §VII.7]` / seam #8 of `[2c §9]` (Gate A round 1 root cause #3) — `LLVMFuzzerTestOneInput` feeds arbitrary bytes to `XmlLoader{}.load_from_string({reinterpret_cast<char const*>(data), size}, &mr)` with a monotonic arena; catches `dict::xml_*` exceptions only; ASan + UBSan instrumentation. Smoke run ≥10 s locally; CI run ≥10 min per quickstart.md §7b.
- [X] T031 [P] Add `bench/dictionary/xml_loader_bench.cpp` Google Benchmark harness per NFR-002-1 (research.md D-18) — benches `XmlLoader{}.load(FIX44.xml)`, `load(FIX42.xml)`, `load(FIX50SP2.xml)`; median of 100 iterations on warm filesystem cache; create `bench/dictionary/CMakeLists.txt` wiring `xml_loader_bench` into `linux-clang-release` preset.
- [X] T032 [P] Verify `tools/check_layers.py` reports clean with only the existing `dictionary → core` edge — no source change expected (research.md D-12 — edge already present from prior Phase-3 scaffolding); document the verification in the PR body to close NFR-002-6.
- [ ] T033 Seed `bench/baselines/dictionary/xml_loader.json` from the first green `linux-clang-release` bench run; subsequent PRs diff against it via `tools/bench_compare.py --tolerance 0.05` per `[const §VIII.2]`.
- [ ] T034 Run static-analysis gate per `[const §IX.4]` — `clang-tidy` + `clang-format --dry-run --Werror` + `cppcheck` + `iwyu_tool.py` clean across `src/dictionary/`, `include/fixpp/dict/`, `tests/dictionary/`; additionally assert the `[const §XV]` banned-pattern rule with `! grep -REn 'thread_local' src/dictionary include/fixpp/dict` (must produce no matches); fix any findings.
- [ ] T035 Run coverage gate per `[const §IX.1]` under `linux-clang-coverage` preset — verify ≥90 % line and ≥80 % branch coverage on the new files in `src/dictionary/` and `include/fixpp/dict/`; fix any uncovered branch with a targeted test before proceeding.
- [ ] T036 Run `/speckit-verify 002-dictionary-xml-loader` per `[const §XVII.8]` — produces `.specify/decisions/002-dictionary-xml-loader-verify.md`; verdict must be `GREEN` (every Tier-1 check PASS or SKIPPED-with-reason) to apply `gate-b-done` label; `YELLOW` requires paired waiver in this tasks.md row + PR body.
- [ ] T037 Run `/gate-b 002-dictionary-xml-loader` per `[const §XVII.2]` — Codex hostile review → Opus triage → Sonnet fixer (≤2 attempts) → Codex fixer (≤2 attempts, fresh context); independence rule per `[const §XVII.3]`; mandatory before merge.
- [ ] T038 Add PR-body confirmation line per `[const §XVII.7]`: `local build: green on linux-clang-debug @ <git-sha>` with `<git-sha>` = `git rev-parse HEAD` at PR open time.

---

## Dependencies & Execution Order

### Phase Dependencies

- **Phase 1 (Setup)**: T001 + T002 + T003 — no dependencies; can start immediately.
- **Phase 2 (Foundational)**: T004..T015 — depends on Phase 1; **BLOCKS** all user-story phases. T004/T005/T006/T007/T008/T009/T010 all `[P]` (different files). T011 depends on T006..T009. T012 depends on T010 + T011. T013 depends on T011 + T012 + T001. T014 depends on T013. T015 depends on T013.
- **Phase 3 (US1, P1, MVP)**: T016..T023 — depends on Phase 2 complete. T016..T021 are all `[P]` (different files). T022 depends on T011 + T016..T021. T023 depends on T022.
- **Phase 4 (US2, P1)**: T024..T027 — depends on Phase 3 complete (US2 extends US1's impl). T024..T026 all `[P]`. T027 depends on T023 + T024..T026.
- **Phase 5 (US3, P2)**: T028 — depends on T019 (round-trip test scaffolding) + T023 (positive-path loader). No new src/ change.
- **Phase 6 (US4, P2)**: T029 — depends on T023 + T002 (the four XML files on disk).
- **Phase 7 (Polish)**: T030..T038 — depends on all user-story phases complete. T030..T032 `[P]`. T033 depends on T031 + first green CI bench. T034..T035 depend on all source/test tasks complete. T036 depends on T034 + T035 + every prior task `[X]`. T037 depends on T036 GREEN. T038 depends on T037.

### User Story Dependencies

- **US1 (P1, MVP)**: standalone after Foundational; nothing else depends on it being shippable, but every later US's implementation hinges on T022/T023 landing first (US2..US4 are surface-extensions or version-parameterized tests of the same impl).
- **US2 (P1)**: extends T023's body with negative-path translation; cannot complete before US1's positive path.
- **US3 (P2)**: validation-only milestone; uses T019's test file with an additional assertion (T028).
- **US4 (P2)**: parameterized over the four shipped versions; depends on T023 handling all version strings (AC-L4 structurally) — no new src/ change.
- **US5 (P3, deferred to F1)**: not in this PR per /clarify Q1 → B; tracked in spec.md §10.

### Within Each User Story

- Tests MUST be written and FAILING before the matching impl task lands (TDD per `[const §VII.3]`).
- T011 (`dictionary.hpp`) before T022 (`dictionary.cpp`).
- T012 (`xml_loader.hpp`) before T023 (`xml_loader.cpp`).
- US1 positive path (T023) before US2 negative paths (T027).
- All AC-* and NFR-002-* tests green before T030..T038 polish gates.

### Parallel Opportunities

- T001 + T003 in Phase 1 (T002 is sequential — must source the four XMLs in one motion).
- T004 + T005 + T006 + T007 + T008 + T009 + T010 in Phase 2 (seven `[P]` headers / surface edits in different files).
- T016 + T017 + T018 + T019 + T020 + T021 in Phase 3 (six `[P]` test files).
- T024 + T025 + T026 in Phase 4 (three `[P]` test files).
- T030 + T031 + T032 in Phase 7 (three `[P]` polish tasks in different files).

---

## Parallel Example — Phase 2 Foundational

```bash
# Launch all seven [P] header / core-surface tasks together:
Task: "T004 Append dict_xml_* variants to include/fixpp/core/error.hpp"
Task: "T005 Add trap_throw_or_throw<E,F> in include/fixpp/core/decimal_helpers.hpp"
Task: "T006 Create include/fixpp/dict/field_ref.hpp"
Task: "T007 Create include/fixpp/dict/component_ref.hpp"
Task: "T008 Create include/fixpp/dict/group_ref.hpp"
Task: "T009 Create include/fixpp/dict/version_profile.hpp"
Task: "T010 Create include/fixpp/dict/error.hpp"

# Then sequentially:
Task: "T011 Create include/fixpp/dict/dictionary.hpp (depends on T006..T009)"
Task: "T012 Create include/fixpp/dict/xml_loader.hpp (depends on T010, T011)"
Task: "T013 Modify src/dictionary/CMakeLists.txt (depends on T011, T012, T001)"
```

## Parallel Example — Phase 3 User Story 1 tests

```bash
# Launch all six [P] test files for User Story 1 together (TDD red phase):
Task: "T016 Write tests/dictionary/ref_shape_test.cpp"
Task: "T017 Write tests/dictionary/xml_loader_test.cpp"
Task: "T018 Write tests/dictionary/pmr_allocation_test.cpp"
Task: "T019 Write tests/dictionary/round_trip_test.cpp"
Task: "T020 Write tests/dictionary/determinism_test.cpp"
Task: "T021 Write tests/dictionary/concurrent_readers_test.cpp"

# Verify all six fail (link error or AC mismatch), then:
Task: "T022 Implement src/dictionary/dictionary.cpp"
Task: "T023 Implement src/dictionary/xml_loader.cpp (positive path)"
```

---

## Implementation Strategy

### MVP First (User Story 1 only)

1. Complete Phase 1: Setup (T001..T003).
2. Complete Phase 2: Foundational (T004..T015) — **CRITICAL**; blocks all stories.
3. Complete Phase 3: User Story 1 (T016..T023).
4. **STOP and VALIDATE**: run `ctest --preset linux-clang-debug -L dictionary` — all US1 tests green; `XmlLoader{}.load("dictionaries/FIX44.xml", &arena)` returns a usable `Dictionary`.
5. This is the demo-ready slice. Subsequent phases harden the surface.

### Incremental Delivery

1. Phase 1 + Phase 2 → Foundation ready.
2. + Phase 3 (US1) → MVP positive-path loader working with FIX44.
3. + Phase 4 (US2) → Negative-path coverage complete (every AC-L* test green).
4. + Phase 5 (US3) → Codegen-iteration exhaustiveness validated.
5. + Phase 6 (US4) → Multi-version coexistence validated (FIXT11 + FIX50SP2 + FIX42 + FIX44).
6. + Phase 7 (Polish) → Fuzz harness, bench baseline, coverage gate, `/speckit-verify` GREEN, `/gate-b` converged.

### Parallel Team Strategy

With multiple contributors (rare for a single-feature PR but possible):

1. One contributor takes T004 + T005 (`core/` additions); another takes T006..T010 (`dict/` headers); they merge into T011 + T012.
2. Once Foundational is done:
   - Contributor A: Phase 3 (US1 — MVP path).
   - Contributor B: Phase 4 (US2 — negative paths, builds atop US1).
   - Contributor C: Phase 6 (US4 — version-parameterized tests; no new impl).
3. Phase 7 polish is run by whoever lands last on `main`.

---

## Format-Validation Self-Check

Every task above conforms to `- [ ] T### [P?] [Story?] Description with file path`:

- Checkbox `- [ ]` present on every row.
- Task IDs T001..T038, sequential, no gaps.
- `[P]` markers on parallelizable tasks (different files, no incomplete-task dependency).
- `[US1]`/`[US2]`/`[US3]`/`[US4]` labels on Phase 3..6 tasks; no story label on Phase 1, 2, or 7 (Setup / Foundational / Polish).
- Explicit file paths in every description.
- Citations to plan.md / spec.md / research.md (D-1..D-20) / data-model.md (Entities 1..7) / `[const §...]` / `[2c §...]` on every task that resolves a design decision.

---

## Notes

- `[P]` tasks operate on different files with no incomplete dependency on each other.
- `[Story]` label maps task → spec.md §3 user story for traceability.
- Each user story (US1..US4) is independently testable; US5 deferred (spec.md §10 F1).
- Verify tests FAIL before implementing (TDD per `[const §VII.3]`).
- Commit after each task or logical group; per quickstart.md §13 the pre-PR confirmation line is mandatory.
- Stop at any user-story checkpoint to validate that story independently before proceeding to the next.
- `dict::table_view`, `dict::reify`, `dict::version_registry`, C-ABI `fixpp_dict_t`, SWIG bindings are **out of scope** per spec.md §5.
