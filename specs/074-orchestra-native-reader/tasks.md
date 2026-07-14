# Tasks: Native Orchestra Reader (FIX Latest)

**Feature**: `074-orchestra-native-reader` | **Branch**: `074-orchestra-native-reader`
**Inputs**: `spec.md`, `plan.md`, `research.md`, `data-model.md`, `contracts/orchestra_loader.md`, `quickstart.md`
**Design authority**: spike-doc Deliverable #6 (no `inherits_design`). Gate A converged round 2 (`c2611b2f`).

**TDD is mandatory** (`[const §VII.3]`): every implementation task is preceded by a failing test. Tests are authored into the grouped bucket `dictionary_orchestra_tests` (`[const §VII.8]`; label `orchestra`; **no `gtest_discover_tests`**), selected by `ctest -L orchestra`. The new parser gets a libFuzzer harness (`[const §VII.7]`).

**Conventions**: `[P]` = parallelizable (distinct files, no incomplete dep). `[US#]` = user-story phase task. Paths are repo-relative to the library submodule root.

---

## Phase 1 — Setup (shared infrastructure; blocks all stories)

- [X] T001 Fetch + vendor the pinned Orchestra source into `dictionaries/orchestra/`: `OrchestraFIXLatest.xml` from `FIXTradingCommunity/orchestrations @ 236d4a4054f0818f1931601713f7a6a68b275df7`, compute its sha1 and **assert it equals `26f60db1c1f52d169d3b6825ac68800abf487fde`** — the spike's grade-1 recorded sha1 of the OFFICIAL file (spike-and-plan doc line 36; supply-chain integrity pin). On mismatch, STOP and investigate (wrong/tampered file) — do not proceed. Record the value into `UPSTREAM.txt` (T002). (Requires network.)
- [X] T002 [P] Add provenance + license files under `dictionaries/orchestra/`: `UPSTREAM.txt` (mirror `dictionaries/UPSTREAM.txt` convention: `repo @ SHA tag= date=`, record EP303), `LICENSE` (Apache-2.0 text), `NOTICE` (Apache-2.0 §4 attribution for the Orchestra source).
- [X] T003 Apply the constitution amendment to `.specify/memory/constitution.md` (Article I §1 / XVIII.1): widen the v1.0 supported version set to include **FIX Latest at the runtime/dictionary tier** (the first version-set widening; draft text in `research.md` D-7), with a Sync Impact Report entry. Article XX; folds on this feature branch.
- [X] T004 Wire CMake for the new reader + its tests: add `orchestra_loader.cpp` to the `fixpp_dictionary` source list in `src/dictionary/CMakeLists.txt` (keeps pugixml PRIVATE); add a new grouped test executable `dictionary_orchestra_tests` in `tests/dictionary/CMakeLists.txt` with `add_test(NAME dictionary_orchestra_tests COMMAND dictionary_orchestra_tests)` + `set_tests_properties(... PROPERTIES LABELS "dictionary;orchestra")` + a `FIXPP_ORCHESTRA_DATA_DIR="${CMAKE_SOURCE_DIR}/dictionaries/orchestra"` compile definition (no `gtest_discover_tests`).

---

## Phase 2 — Foundational (reader scaffold + version identity; blocks US1–US3)

- [X] T005 [P] Add `dict::orchestra_parse_error : public xml_parse_error` to `include/fixpp/dict/error.hpp` (`using xml_parse_error::xml_parse_error;`; reuse inherited `code()`; catch-discriminated — the `group_delimiter_collision_error` precedent). No `core::error` append.
- [X] T006 [P] Add `session_version::vlatest` to `include/fixpp/dict/version_profile.hpp` (new enumerator after `vt11`) and the forced `case session_version::vlatest: return application_version::v50sp2;` arm to `session_to_application` in `src/dictionary/version_registry.cpp` (exhaustive `default`-free switch; `-Wswitch -Werror`). Do **NOT** add `application_version::vlatest`; do **NOT** touch `render_appl_ver_id`.
- [X] T007 [P] Add `friend class OrchestraLoader;` to `include/fixpp/dict/dictionary.hpp:188` (symmetric with `friend class XmlLoader;`) so the reader can call the private handle-ctor.
- [X] T008 Create the `OrchestraLoader` facade `include/fixpp/dict/orchestra_loader.hpp` per `contracts/orchestra_loader.md`: `[[nodiscard]] Dictionary load(std::filesystem::path const&, std::pmr::memory_resource*)` + `load_from_string(std::string_view, std::pmr::memory_resource*)`, stateless, `assert(mr)`, body wrapped in `core::detail::trap_throw_or_throw<xml_oom_error>`.
- [X] T009 Create `src/dictionary/orchestra_loader.cpp` scaffold: `OrchestraLoaderState` (TU-local), root check (`fixr:repository` else `orchestra_parse_error`), version resolver (root `version="FIX.Latest_EP303"` → `session_version::vlatest`, else `unknown_version_error`), and `finalize()` emitting a `detail::dict_metadata_handle` → `Dictionary{handle}`.
- [X] T010 [P] Add the datatype-token table `kOrchestraTypeTable[]` (TU-local, constexpr `{std::string_view, field_data_type}` + `resolve_*` linear scan) in `src/dictionary/orchestra_loader.cpp`, mapping Orchestra `<fixr:datatype>` names → the existing `field_data_type` enum incl. spike collapse rows (`LOCALMKTTIME`→LocalMktDate, `XID`/`XIDREF`→String, `TAGNUM`→Int); fail-closed `orchestra_parse_error` on an unknown token used by a field.

---

## Phase 3 — User Story 1: Load FIX Latest natively (P1) 🎯 MVP

**Goal**: `OrchestraLoader{}.load(OrchestraFIXLatest.xml, mr)` → a `Dictionary` with 181 messages; fields (incl. codeset values+descriptions) queryable.
**Independent test**: `ctest -L orchestra -R` the load bucket → 181 messages + field/codeset assertions pass.

- [X] T011 [US1] RED test `tests/dictionary/orchestra_loader_test.cpp` (`dictionary_orchestra_tests` bucket): `OrchestraLoad.LoadsEP303` — load from `FIXPP_ORCHESTRA_DATA_DIR`, assert `messages().size() == 181` (SC-001/FR-003) and `which_session_version() == session_version::vlatest`.
- [X] T012 [US1] RED test `OrchestraLoad.Headlines` + `OrchestraCodesets.PreservesValuesAndDescriptions`: spot-check known FIX Latest msg types present, and a known codeset field's enumerated values **and** description bytes survive in the internal dictionary (FR-002 codeset flattening).
- [X] T013 [US1] Implement the full `fixr:repository` walk in `src/dictionary/orchestra_loader.cpp` (fields/global datatypes → components → groups → messages → header/trailer), emitting the `FieldRef[]`/`ComponentRef[]`/name-pool tables into the handle (Orchestra analogue of `LoaderState::parse_document`). Make T011 GREEN.
- [X] T014 [US1] Implement codeset flattening (`<fixr:codeSet>`/`<fixr:code>` → per-field enum values + descriptions preserved) and `unionDataType` second-arm drop (keep codeset base type). Make T012 GREEN.

---

## Phase 4 — User Story 2: Deep + reused group shapes resolve queryably (P1)

**Goal**: group-context tables build without throw/truncate; depth-7 chain resolves via full parent path; reused tag 555 resolves per-parent.
**Independent test**: `ctest -L orchestra -R OrchestraGroups` → full-parent-path + reused-tag assertions pass.

- [X] T015 [US2] RED test `OrchestraGroups.DeepAndReused`: build `as_table_view()`; assert the depth-7 `MassQuoteAck` group resolves via the **full parent path** `296→295→555→40241→41686→41680→41683` to its expected member set + first field (end-to-end, not a bare non-empty lookup), and tag 555 resolves non-empty under each distinct parent (SC-003/FR-004).
- [X] T016 [US2] Implement group/component emission in `src/dictionary/orchestra_loader.cpp`: `<fixr:group>`+`<fixr:numInGroup>` → `GroupRef{no_tag,first_field_tag,first_field_index,field_count,parent_group_no_tag}` + `group_fields_`; **include the NumInGroup count field in the parent run with `type==NumInGroup`**; set `FieldRef.group_no_tag` on members; expand `<fixr:component>`/`<fixr:componentRef>` transitively with correct parent-path. Make T015 GREEN.
- [X] T017 [US2] Apply the `072` load-time nested-delimiter check: if a nested group's delimiter tag equals its parent's, throw `group_delimiter_collision_error` (consistency with `XmlLoader`).

---

## Phase 5 — User Story 3: Real distinct version identity + registry guard (P2)

**Goal**: FIX Latest loads under `session_version::vlatest` (wire app-version `v50sp2`); a registry carrying both FIX50SP2 and FIX Latest fails loud (FR-010).
**Independent test**: version-identity + registry-guard tests pass.

- [X] T018 [US3] RED test `OrchestraVersionIdentity`: `which_session_version() == vlatest` (distinct from all nine legacy), `session_to_application(vlatest) == application_version::v50sp2`, and no `FIX.5.0SP2` relabel is used (SC-005/FR-005). (GREEN once T006+T009 land.)
- [X] T019 [US3] RED test for the FR-010 guard: build a `version_registry` (or `EngineConfig`) carrying **both** a real FIX50SP2 dict and a FIX Latest dict; assert it **fails loud**, NOT a silent last-writer-wins overwrite. Also assert single-dict configs (FIX Latest alone; FIX50SP2 alone) succeed unaffected (FR-010). **Wiring:** if T020 lands as the preferred fatal-in-ctor, author this as an `EXPECT_DEATH`/`ASSERT_DEATH` case **inside the grouped `dictionary_orchestra_tests` bucket** (fork-safe; no separate binary — no extra CMake wiring). Only if T020 falls back to the error-returning `build_version_registry` should this become a standalone target with its own `add_executable`/`add_test`/`LABELS "orchestra"` (mirror T023's explicit wiring).
- [X] T020 [US3] Implement the FR-010 fail-loud guard — **preferred: contained fatal-in-ctor** in `version_registry` (`src/dictionary/version_registry.cpp` ctor, `:71-72` region): detect the FR-010 case — **both a FIX50SP2 and a FIX Latest dictionary** resolving to the shared `application_version::v50sp2` slot (idx 8) — and fail loud release-effectively (not an NDEBUG-stripped `assert`); the `noexcept` ctor surfaces via abort/fatal. Scope the guard to FR-010's literal case (do not broaden to reject unrelated same-slot double-registration unless a follow-up widens FR-010). (Fallback error-returning `build_version_registry` only if a non-fatal path is required — extends into the engine-construction layer.) Make T019 GREEN.

---

## Phase 6 — User Story 4: Source vendored, pinned, attributed (P2)

**Goal**: provenance + Apache-2.0 attribution present and correct.
**Independent test**: provenance/attribution assertions pass.

- [X] T021 [US4] Test `OrchestraProvenance` (or a scripted check): assert `dictionaries/orchestra/UPSTREAM.txt` records the repo + commit `236d4a405…` + sha1 + EP303, that `OrchestraFIXLatest.xml` sha1 matches, and that `LICENSE`/`NOTICE` (Apache-2.0 §4) are present (SC-007/FR-007). (Verifies T001/T002.)

---

## Phase 7 — Polish & Cross-Cutting Concerns

- [X] T022 [P] Fail-closed matrix test coverage (FR-006/FR-009, discriminating per `contracts/orchestra_loader.md`): (a) unknown `<fixr:datatype>` **used** by a field → `orchestra_parse_error` (SC-002 proven RED); (b) an **unused** unknown datatype declaration does NOT fail; (c) a `unionDataType` whose **primary/base** arm is unknown → fails closed (drop-second-arm must not mask it); (d) non-`fixr:repository` root, (e) a QuickFIX `FIX44.xml` fed to `OrchestraLoader`, (f) truncated/malformed XML, (g) dangling component ref → all throw, never a silent partial; (h) [P] the reverse asymmetry the spec names — an Orchestra `fixr:repository` file fed to `XmlLoader` throws (regression pin on the pre-existing non-`"fix"`-root check `xml_loader.cpp:284-285`).
- [X] T023 [P] libFuzzer harness `tests/dictionary/fuzz/orchestra_loader_fuzz.cpp` over `OrchestraLoader::load_from_string` (`[const §VII.7]`), plus its CMake fuzz target; seed corpus from a trimmed EP303 sample. No crash/leak over ≥10 min.
- [X] T024 [P] Legacy no-regression pin (SC-006/FR-008): assert all nine QuickFIX dicts still load through `XmlLoader` with unchanged message counts + group queries; confirm `XmlLoader` behavior untouched.
- [X] T025 Full-ctest golden/determinism no-regression check for the `session_version::vlatest` enum add (`[[feedback_codegen_golden_exists_narrow_verify_misses_it]]`): run the FULL `dictionary`+`codegen` ctest labels (NOT a narrow target); confirm no golden/determinism/completeness test regresses (vlatest gets no codegen namespace, so none should).
- [X] T026 Promote L-074-1 (interim `v50sp2` registry-slot coexistence) to a `spec/behaviors-and-limitations.md` L-row.

### Mandatory close-out (Gate-B preconditions, `[const §XVII.8]`)

- [X] T027 Catalogue + coverage-index close-out: register the Orchestra-schema DocAbbrev in `spec/coverage-index.md`, add the bidirectional entries promoting **D-011** (Post-1.0 Gap → in-scope FIX-Latest read-path) and linking the **A-035..A-065** rows to this feature (the Article VI §4 pre-implement obligation the plan marked CONDITIONAL); flip every 074-owned OFFICIAL `spec/feature-catalogue.md` row to `done`.
- [X] T028 **Feature-completeness audit (FINAL task)**: verify tasks ↔ FR-001..010 / SC-001..007 ↔ catalogue rows all map to a landed test + implementation; record the 100%-or-waived verdict in `.specify/decisions/074-orchestra-native-reader-verify.md` (`## Completeness`). Hard `/gate-b` 4d precondition.

---

## Dependencies & execution order

- **Setup (T001–T004)** → blocks everything. T001 before T011/T021 (needs the file). T003 (amendment) before merge; T004 before any test builds.
- **Foundational (T005–T010)** → blocks US1–US3. T005/T006/T007 are `[P]` (distinct files). T008 before T009; T009 before T010/T013.
- **US1 (T011–T014)** = MVP. **US2 (T015–T017)** depends on US1's parse (T013) for message/group structure. **US3 (T018–T020)** depends on T006/T009 (identity) + independent registry work. **US4 (T021)** depends only on Setup (T001/T002) — fully parallel to US1–US3.
- **Polish (T022–T026)** after the stories it covers; T025 after T006. **Close-out (T027–T028)** last; T028 is the final task.

## Parallel opportunities

- Setup: T002 ∥ (T001 fetch); after T001, T003 ∥ T004.
- Foundational: T005 ∥ T006 ∥ T007 (distinct files), then T008→T009→T010.
- Across stories once Foundational lands: **US4 (T021)** runs fully parallel to US1/US2/US3. Polish T022/T023/T024 are mutually `[P]`.

## Implementation strategy

**MVP = US1** (load 181 messages + field/codeset queries) — the load-bearing slice; delivers a usable FIX Latest dictionary. Then US2 (group correctness — the spike's discriminating invariant), US3 (honest identity + the FR-010 safety guard), US4 (provenance). Polish hardens fail-closed + fuzz + no-regression; close-out satisfies the Gate-B preconditions.

**Total: 28 tasks** — Setup 4, Foundational 6, US1 4, US2 3, US3 3, US4 1, Polish 5, Close-out 2.
