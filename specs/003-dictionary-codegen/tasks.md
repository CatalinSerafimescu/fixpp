---
id: 003-dictionary-codegen
title: Tasks — Dictionary codegen (`fixpp-codegen` + per-version typed messages + `dict::reify` bridge)
phase: 2 (/speckit-tasks)
generated: 2026-05-15
spec_kit_step: /tasks
gate_a: converged round 3 (Codex P1=0 P2=0 P3=0; Opus P1=0 P2=0 P3=0; user-signed-off 2026-05-15, commit 3824bb5) — /tasks unblocked
inherits: plan.md (re-`/plan` 2026-05-15), spec.md (RC#1/#2/#3 ACs), data-model.md (11 entities), research.md (D-1..D-23), contracts/, .specify/2c-codegen.md v1.4, .specify/architecture.md §2.4 v0.3
tdd: red-green-refactor per `[const §VII.3]` — tests/headers/goldens lead; emitters follow; conformance/integration/bench/polish close
---

# Tasks: 003-dictionary-codegen

**Input**: Design documents from `specs/003-dictionary-codegen/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/ (all present; Gate A converged)

**Tests**: TDD is constitutionally mandated (`[const §VII.1]` GoogleTest, `[const §VII.3]` red-green-refactor). Test tasks are included and ordered FIRST within each phase/story — they MUST fail before the implementation task that makes them green.

**Organization**: Tasks are grouped by user story (spec §3). The `fixpp-codegen` tool scaffolding + the RC#1-owned headers + the vendored wire stub + the `[2c §7.6]` CMake graph are the shared codegen substrate (Phase 2 Foundational) every story compiles against; per-story phases add the story-specific emitter / bridge code and its tests.

All paths are relative to the library submodule root (`research/G19-fix-fpml-iso20022/library/`). Generated headers land under `build/<preset>/_codegen/include/fixpp/...` (build tree only — AC-C4/AC-T2).

## Format: `[ID] [P?] [Story?] Description`

- **[P]**: parallelizable (different files, no incomplete-task dependency)
- **[Story]**: US1..US6 maps to spec §3.1..§3.6; Setup/Foundational/Polish carry no story label

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Tool/test/bench skeleton and build wiring. No new Conan row (F1 Candidate A).

- [X] T001 Create the source/test/bench directory skeleton: `tools/codegen/fixpp-codegen/`, `tests/codegen/{conformance,}`, `tests/dictionary/`, `tests/integration/`, `bench/codegen/{compile_time_bench,}`, `bench/dictionary/`, `specs/003-dictionary-codegen/contracts/golden/` per plan.md Project Structure
- [X] T002 [P] Confirm the Conan profile + CMake presets carry NO new dependency row (F1: `fixpp-codegen` links the merged `fixpp::dict`/`pugixml 1.14` transitively) — verify `conan/profiles/linux-clang-debug` and presets per quickstart §1, `[const §III.2]`/`[const §V.3]`
- [X] T003 [P] Create `tools/codegen/fixpp-codegen/CMakeLists.txt` — C++23 build-only host executable that links `fixpp::dict`; never linked into the user-facing library (`[const §III.5]`; F1 Candidate A)

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: The shared codegen substrate — RC#1-owned headers, the vendored wire stub, the bounded error slots, the codegen tool scaffolding (IR + deterministic templating + CLI), and the `[2c §7.6]` CMake target graph. **Every user story compiles against the headers produced here.**

**⚠️ CRITICAL**: No user-story work can begin until this phase is complete.

### Foundational tests (write FIRST — must fail before the header/impl tasks below)

- [X] T004 [P] `tests/dictionary/version_profile_test.cpp` — AC-VP1..AC-VP6: `version_profile`/`resolved_message_version` structs + `static_assert`s, `dict::resolve_application_version` free fn, full `ApplVerID(1128)`→C++ `application_version` wire map (`2c §4.3:486-501`), the AC-VP4 negative (C++ index NOT reused), `_reserved` discipline, the six locked error slots. Oracle: `contracts/version_profile.hpp`
- [X] T005 [P] `tests/dictionary/field_traits_test.cpp` — AC-FT1..AC-FT3: primary `field_traits<T>` + string_view/char/int/bool/timestamp/MultiChar-MultiString specialisations + `decode_field<T>`; AC-FT2 negative (`decimal_t` is NOT a `field_traits` specialisation). Oracle: `contracts/field_traits.hpp`
- [X] T006 [P] `tests/dictionary/version_registry_test.cpp` — AC-X1..AC-X3 shape-only with an in-test hand-built registry. Oracle: `contracts/version_registry.hpp`

### Foundational implementation

- [X] T007 [P] `include/fixpp/core/error.hpp` — additive edit: append six `dict_*` variants LOCKED at slots 23..28 (`dict_reify_msg_type_mismatch=23` … `dict_no_dictionary_for_application_version=28`), non-renumbering, existing slots verbatim; verify on-disk file ended at 22 (research.md D-10/D-21; data-model "Error mapping"; `[const §X.4]`)
- [X] T008 [P] `include/fixpp/dict/version_profile.hpp` — additive edit (RC#1): append `version_profile` + `resolved_message_version` structs (+ `static_assert`s), `dict::resolve_application_version` free-fn decl, and the verbatim `ApplVerID(1128)`→C++ `application_version` map BELOW the unchanged 002 enums, non-renumbering. Makes T004 green. Oracle: `contracts/version_profile.hpp`; data-model Entity 10
- [X] T009 [P] `include/fixpp/dict/field_traits.hpp` — NEW (RC#1): primary `field_traits<T>` + specialisations + `decode_field<T>`, `decimal_t` excluded by design (I-16). Allocation-free on the ≤20 ns hot path. Makes T005 green. Oracle: `contracts/field_traits.hpp`; data-model Entity 11. Bridge header (`arch §2.4` v0.3)
- [X] T010 [P] `include/fixpp/wire/message_view_contract.hpp` — NEW, vendored FROZEN R6 stub: exactly the `[2b §4.3]`/`[2b §4.7]` locked surface (`MessageView<Index>::get<Tag>`, `field_view`, `group_view`, `OffsetTable`). Oracle: `contracts/wire_message_view_contract.hpp`; data-model Entity 9. Bridge surface (`arch §2.4` v0.3); 2b replaces the body against this same contract
- [X] T011 `include/fixpp/dict/reify.hpp` — NEW bridge header: `dict::reify_as` / `dict::reify` / `owning_message_handle` + the `owning_message_traits<Msg>` primary template and the `dict::owning_message_t<Msg> = typename owning_message_traits<Msg>::type` alias (canonical 2c v1.4 §4.8 form). `noexcept` free fn templates; PMR OOM → `dict_reify_oom` via `trap_throw`. Oracle: `contracts/reify.hpp`; data-model Entities 5/6. Bridge header (`arch §2.4` v0.3) (depends on T007–T010)
- [X] T012 [P] `include/fixpp/dict/version_registry.hpp` — NEW: `dict::version_registry` shape only (concrete value type, `[const §XIV.2]` N/A). Makes T006 green. Oracle: `contracts/version_registry.hpp`; data-model Entity 7
- [X] T013 `tools/codegen/fixpp-codegen/template_writer.hpp` — deterministic, locale-independent string-templating layer (sorted, bytewise-stable emission — inherits 002's NFR-003-7 invariant; research.md D-6). Underpins all emitters and the US5 determinism guarantee
- [X] T014 `tools/codegen/fixpp-codegen/ir.hpp` + `ir.cpp` — XML→IR: call `XmlLoader::load(dictionaries/<VER>.xml, mr)`; walk `FieldRef`/`ComponentRef`/`GroupRef` arrays + `Dictionary::which_session_version()` (F1 Candidate A — one XML truth, no second parser; research.md D-1) (depends on T013)
- [X] T015 `tools/codegen/fixpp-codegen/main.cpp` — CLI driver: per-version dispatch into the emitters, build-tree-only output paths (`build/<preset>/_codegen/include/fixpp/...`), never the source tree (AC-C4) (depends on T014)
- [X] T016 `CMakeLists.txt` + `cmake/Codegen.cmake` — the `[2c §7.6]` target graph: **configure-time** bootstrap via `execute_process` (main-build binary first, then `_codegen_bootstrap` sub-build with same toolchain+build-type) + `fixpp_dict_generate_vXX` custom targets (no-op markers; generation ran at configure time), per-version `INTERFACE` targets `fixpp::dict::{v42,v44,v50sp2,vt11,dispatch,runtime}` carrying `INTERFACE_INCLUDE_DIRECTORIES` into the build tree, `fixpp_codegen_generate` no-op marker (single source of truth; removed from tests/codegen). AC-C4 satisfied: headers exist post-configure, source tree clean, build tree only. (depends on T015)

- [X] T055 (RC#5 — `/speckit-implement`-surfaced 2026-05-15) `include/fixpp/dict/dictionary.hpp` + `src/dictionary/dictionary.cpp` + `src/dictionary/dictionary_internal.hpp` — **003-owned additive edit to the 002-merged Dictionary**: `message_fields(std::string_view)→std::span<FieldRef const>` (full per-message run, required+optional) + `field_name(std::uint16_t)→std::string_view` (tag→FIX name). The F1 IR data path the US1 emitters need; the public 002 surface exposed neither (research D-1 "walk the metadata" was unvalidated). Additive/source-compatible (`[arch §9.3]`/`[const §X.4]`-style); build-time codegen-enumeration only. AC-G13; spec §4.1/§7/A7; plan "/speckit-implement surfaced — RC#5"; research D-24. **Gates T023/T024/T025**.

**Checkpoint**: Codegen tool scaffolding builds; RC#1 headers + wire stub + error slots compile and their shape tests (T004–T006) pass; the RC#5 codegen-enumeration accessors (T055) build green. Generated-header emission and per-story work can begin.

---

## Phase 3: User Story 1 — Typed field access (Priority: P1) 🎯 MVP

**Goal**: An application developer parses a frame and reads strongly-typed, zero-allocation fields off `fixpp::<vXX>::<Msg>` flyweights; misuse fails to compile (`[const §XV.6]` discharge).

**Independent Test**: Include a single-version `Messages.hpp`, bind a `MessageView<Index>`, call `cl_ord_id()`/`side()`/`order_qty()`/`price(mr)`/`field_value(uint16_t)`; assert typed values, zero allocation on the string/int/char + default-`pod_decimal` path, and that a wrong-type access is ill-formed (AC-G1..G12, seam #1/#18/#19).

### Tests for User Story 1 (write FIRST — must fail)

- [X] T017 [P] [US1] `tests/codegen/typed_accessor_test.cpp` — AC-G1..G8, AC-G11: per-field typed accessors, the `price(mr)` v1.4 decimal accessor + AC-G4a default/allocating-trait behaviour, `[[nodiscard]]`/`[[clang::lifetimebound]]`, repeating-group `Leg` flyweight, `field_value(uint16_t)` forwarder
- [X] T018 [P] [US1] `tests/codegen/flyweight_shape_test.cpp` — seam #18: AC-G7 `static_assert(sizeof == sizeof(MessageView<Index> const*))`, AC-G7a per-message `owning_message_traits<Msg>` specialisation pin (compile-time shape oracle), R6 drift guard incl. `view.get<1128>()` AND `view.get<35>()` well-formed against the frozen contract (N-P3-1/N-P2-2)
- [X] T019 [P] [US1] `tests/codegen/msgtype_boundary_test.cpp` — AC-G9 (FIX-Latest A-035..A-065 filtered + build warning, not emitted), AC-G10 (A-014..A-034 not emitted as typed classes)
- [X] T020 [P] [US1] `tests/codegen/validator_shape_test.cpp` — AC-V1..AC-V3, AC-V5, AC-V6: per-message rule-table shape + `NormativeReferences.md` per-message citations
- [X] T021 [P] [US1] `tests/codegen/length_data_table_test.cpp` — seam #19, AC-V4: Length+Data pair table exhaustive vs source XML, cross-checked vs `[FIX50SP2 §3.3]`
- [X] T022 [P] [US1] `tests/codegen/conformance/conformance_test.cpp` + `must_include_manifest.txt` — seam #1/#15b, AC-G1..G6/AC-G12: parameterised round-trip over the curated must-include CI subset (P1 headline + every group-bearing msg + 7 FIXT admin + AC-D4 worked-example + msgtype-boundary probes); manifest is checked in and Gate-A-reviewed; nightly exhaustive

### Implementation for User Story 1

- [X] T023 [US1] `tools/codegen/fixpp-codegen/emit_fields.cpp` — emit `<vXX>/Fields.hpp` constexpr `FieldRef`/`ComponentRef`/`GroupRef` arrays (data-model Entity 2; static storage, 0 runtime alloc). DONE: per-message `inline constexpr ::fixpp::dict::FieldRef <Msg>_fields[]` populated verbatim from the RC#5 `message_fields` run (full FieldRef now carried in `FieldIR.ref`); `_reserved`=0 (AC-V6). Shared `gen_util.hpp` (name/type helpers). Build-validated: tool green; generated `Fields.hpp` compiles for all 4 versions under `-std=c++23`.
- [X] T024 [US1] `tools/codegen/fixpp-codegen/emit_messages.cpp` — emit `<vXX>/Messages.hpp` typed-message flyweights: `msg_type_v`/`version_v`, view-binding ctor, per-field `expected_t<T>` accessors via `field_traits`/`decode_field`, the `price(mr)` v1.4 PMR decimal accessor (AC-G4/AC-G4a), `Leg` group flyweight, `field_value(uint16_t)`, `static_assert(sizeof==…)` (data-model Entity 1). Makes T017–T019 green (depends on T023). DONE: full [2c §4.7] shape per `contracts/generated_message.hpp`; repeating-group structure reconstructed from `FieldRef::group_no_tag`+`type==NumInGroup` (handles N-deep nesting, e.g. v50sp2 NewOrderSingle Allocs→NestedPartyIDs→NestedPartySubIDs); default-constructible nested group flyweights (frozen `group_view<T>` stub returns `T{}`); `owning_<Msg>` fwd-declared (AC-G7a). Build-validated: generated `Messages.hpp`+`Fields.hpp` compile clean for v42/v44/v50sp2/vt11 against `include/` + the frozen wire stub.
- [X] T025 [US1] `tools/codegen/fixpp-codegen/emit_validator.cpp` — emit `<vXX>/Validator.hpp` per-message rule tables + the Length+Data pair table (data-model Entity 3). Makes T020–T021 green
- [X] T026 [US1] `tools/codegen/fixpp-codegen/emit_normative_refs.cpp` — emit `<vXX>/NormativeReferences.md` per-message `[FIXxx §X.Y.Z]` citations (`[const §VI.5]`; AC-V5). Makes the AC-V5 arm of T020 green
- [ ] T027 [US1] Wire `emit_fields`/`emit_messages`/`emit_validator`/`emit_normative_refs` into `main.cpp` + the `generate-vXX` graph; conformance corpus (T022) green for all four versions

**Checkpoint**: US1 is independently functional — typed accessors work for all four versions; MVP deliverable.

---

## Phase 4: User Story 2 — Cross-strand handoff via reify (Priority: P1)

**Goal**: A session-FSM author reifies a parsed frame into an arena-owned `owning_<Msg>`, moves it across a strand, and reads stable values after the source arena is reset.

**Independent Test**: `dict::reify_as<NewOrderSingle>(view, mr)` on thread A, `std::move` to thread B, reset A's arena, read accessors on B — values match pre-reset; ≤4 PMR allocations; OOM → `dict_reify_oom` (AC-R1..R8, seam #7/#12/#14/#16).

### Tests for User Story 2 (write FIRST — must fail)

- [X] T028 [P] [US2] `tests/dictionary/reify_test.cpp` — AC-R1..R3, AC-R6, AC-R8: `reify_as<Msg>` happy path, msg-type-mismatch arm (`get<35>()`), `[[clang::lifetimebound]]` chain
- [X] T029 [P] [US2] `tests/dictionary/reify_move_test.cpp` — seam #14, AC-R4: move + lazy view rebuild; `static_assert`s (no reference members, `is_nothrow_move_constructible_v`, move ctor NOT `= default`)
- [X] T030 [P] [US2] `tests/dictionary/reify_cross_strand_test.cpp` — seam #12, AC-R5/AC-T3: reify A → move → consume B; original traps post-reset (TSan target — `[const §IX.2]`)
- [X] T031 [P] [US2] `tests/dictionary/reify_oom_test.cpp` + `tools/check_alloc.py` driver — seam #7/#16, AC-R7: `trap_throw` PMR-OOM injection → `dict_reify_oom`; ≤4 PMR allocs; `mallocnesia` zero-alloc guard for string/int/char + default-trait decimal

### Implementation for User Story 2

- [X] T032 [US2] `tools/codegen/fixpp-codegen/emit_reify.cpp` — emit `<vXX>/Reify.hpp`: `owning_<Msg>` class (arena-owned bytes, custom `noexcept` move resetting both caches, lazy `view()` rebuild) + the per-message `template<> struct owning_message_traits<...>` specialisation + `static_assert(std::is_same_v<dict::owning_message_t<Msg>, fixpp::<vXX>::owning_<Msg>>)` (data-model Entity 4; AC-G7a). Makes T028–T031 green (depends on T024)
- [X] T033 [US2] Wire `emit_reify` into `main.cpp` + `generate-vXX`; finalize any out-of-line bridge bits in `src/dictionary/reify.cpp` (+ `src/dictionary/CMakeLists.txt`) if needed (D-5 final split). `check_layers.py` `BRIDGE_SOURCE_FILES` exemption applies (depends on T011, T032)

**Checkpoint**: US1 + US2 both work independently — typed access + arena-owned cross-strand reify.

---

## Phase 5: User Story 3 — Runtime-dispatch reify + FIXT cross-vocabulary (Priority: P2)

**Goal**: A session-FSM author feeds frames through `dict::reify(view, profile, mr)` and gets the correct per-version typed owner via `ApplVerID(1128)` / FIXT default resolution.

**Independent Test**: The `[2c §6.3]` worked-example byte stream resolves Logon→vt11, NOS ApplVerID=9→v50sp2, NOS ApplVerID=6→v44 override, OCR no ApplVerID→v50sp2 default, Heartbeat→vt11 with correct `resolved_message_version` (AC-D1..D7, seam #15a/#15b/#15c/#10b).

### Tests for User Story 3 (write FIRST — must fail)

- [X] T034 [P] [US3] `tests/dictionary/reify_dispatch_test.cpp` — seam #15a/#15b/#15c/#10c, AC-D1..D3/D6/D7: 7 FIXT admin MsgTypes switch, application MsgTypes (AC-G12 CI subset; nightly exhaustive), `dict_unresolved_application_version` propagation (AC-D6), fail-loud default arm
- [X] T035 [P] [US3] `tests/integration/fixt_cross_vocabulary.cpp` — seam #10b, AC-D4: the `[2c §6.3]` worked-example byte stream end-to-end

### Implementation for User Story 3

- [X] T036 [US3] `tools/codegen/fixpp-codegen/emit_dispatch.cpp` — emit `_dispatch/reify_dispatch_fixt.hpp` (7 FIXT admin MsgTypes) + `_dispatch/reify_dispatch_application.hpp` (~470 (version, MsgType) cases, fail-loud `dict_reify_unknown_msg_type` default) (data-model Entity 8; R3). Makes T034–T035 green (depends on T032)
- [X] T037 [US3] Wire `emit_dispatch` into `main.cpp` + `generate-vXX`; `dict::reify` consumes `version_profile`/`resolve_application_version` (T008) + the generated dispatch headers (depends on T011, T036)

**Checkpoint**: US1–US3 work independently — full runtime-dispatch reify + FIXT cross-vocabulary.

---

## Phase 6: User Story 4 — Multi-version coexistence (Priority: P2)

**Goal**: A translator/gateway author includes multiple version namespaces in one TU; types stay distinct; consumers pay compile cost only for included versions.

**Independent Test**: A TU including v42 + v50sp2 `Messages.hpp` compiles; `static_assert` the two `NewOrderSingle` are not implicitly convertible; depending only on `fixpp::dict::v44` + `runtime` does not pull v50sp2 headers (AC-C1..C4, seam #10a + the C4 CMake-graph seam).

### Tests for User Story 4 (write FIRST — must fail)

- [X] T038 [P] [US4] `tests/integration/multi_session_multi_version.cpp` — seam #10a, AC-C1..C3: multi-version coexistence, no namespace bleed, non-implicitly-convertible cross-version types
- [X] T039 [P] [US4] `tests/codegen/codegen_build_graph_test.cmake` registered as the `fixpp::dict::codegen-build-graph-check` CTest target (a `cmake -P` script test, NOT a GoogleTest TU) — AC-C4: (a) `build/<preset>/_codegen/include/fixpp/...` exists post-configure; (b) `generate-vXX` is a configure-time custom target; (c) per-version INTERFACE targets carry `INTERFACE_INCLUDE_DIRECTORIES` into the build tree; (d) `git status --porcelain` source tree clean post-configure (DoD §12 build-tree clause)

### Implementation for User Story 4

- [X] T040 [US4] Finalize the `[2c §7.6]` per-version `INTERFACE` target isolation in `CMakeLists.txt`/`cmake/` so a consumer paying for `fixpp::dict::v44` does not pull other versions; makes T038–T039 green (extends T016)

**Checkpoint**: US1–US4 work independently — verified multi-version isolation + build-graph hygiene.

---

## Phase 7: User Story 5 — Deterministic, XML-driven emission (Priority: P2)

**Goal**: A codegen-tool maintainer gets byte-identical output across runs and machines; a template change is caught as a reviewed golden diff.

**Independent Test**: Run `fixpp-codegen` twice against the same XML → byte-identical; assert no source-tree write; diff against the four checked-in golden headers (AC-T1..T3, NFR-003-7, seam #1/#2).

### Tests for User Story 5 (write FIRST — must fail)

- [X] T041 [P] [US5] `tests/codegen/determinism_test.cpp` — NFR-003-7/AC-T1/AC-T2: generate twice, hash, assert equal; assert no source-tree write; byte-identical re-emission vs the four `<vXX>_Messages.golden.hpp`

### Implementation for User Story 5

- [X] T042 [US5] Generate `specs/003-dictionary-codegen/contracts/golden/{v42,v44,v50sp2,vt11}_Messages.golden.hpp` via `fixpp-codegen` and check them in (the 4-golden anchor, /clarify Q-golden → A; regenerated as a reviewed step on template change). Makes T041 green (depends on T024, T027)

**Checkpoint**: US1–US5 work independently — determinism guaranteed and golden-anchored.

---

## Phase 8: User Story 6 — Runtime-XML-only version consumer (Priority: P3, negative path)

**Goal**: A frame whose resolved version has no codegen owner fails loud, not silently mis-dispatched.

**Independent Test**: Hand-build a synthetic `MessageView` whose resolved `application_version` is runtime-XML-only (v40/v41/v43/v50/v50sp1); feed through `dict::reify`; assert the application-switch default arm returns `dict_reify_unknown_msg_type` — no `FIX43.xml` dependency (AC-D5, seam #10c).

### Tests for User Story 6 (write FIRST — must fail)

- [X] T043 [P] [US6] Add the AC-D5 negative arm to `tests/dictionary/reify_dispatch_test.cpp` — hand-built synthetic `MessageView` with a runtime-XML-only resolved version → default arm returns `dict_reify_unknown_msg_type` (seam #10c; no deferred-XML dependency)

### Implementation for User Story 6

- [X] T044 [US6] Verify the `emit_dispatch` application-switch fail-loud default arm (T036) satisfies AC-D5 for runtime-XML-only versions; adjust the emitted default case if the synthetic-fixture test exposes a gap

**Checkpoint**: All six user stories independently functional.

---

## Phase 9: Polish & Cross-Cutting Concerns

**Purpose**: Benches, sanitizer/coverage gates, layer-edge lint, docs, and the gate workflow. Each is a `tasks.md` row so `/speckit-verify` has a 1:1 mapping (`[const §XVII.8]`).

- [X] T045 [P] `bench/codegen/typed_accessor_bench.cpp` + `bench/codegen/CMakeLists.txt` — NFR-003-1 (string/int/char ≤20 ns; `price(mr)` decimal ≤75 ns; `field_value` ≤25 ns); seam #3; wire into Tier-1 release preset
- [X] T046 [P] `bench/codegen/compile_time_bench/` — NFR-003-2 (single-version `Messages.hpp`+`Reify.hpp` ≤3 s load-bearing; all-versions ≤15 s soft, `FIXPP_BENCH_ALL_VERSIONS_CEILING`); seam #2 — v50sp2 KNOWN_OVERAGE (~6 s) documented in bench/codegen/compile_time_bench/README.md (R2 risk; v42/v44/vt11 all PASS)
- [X] T047 [P] `bench/dictionary/reify_bench.cpp` — NFR-003-3 (`reify_as` ≤1 µs/20-tag, ≤10 µs/200-tag; `reify` ≤1.2 µs/20-tag) + the codegen-lookup arm (seam #5/#6); seed `bench/baselines/` — R6-deferred; from_view() used as reify_as proxy (template body not yet defined)
- [X] T048 [P] `tests/codegen/CMakeLists.txt` + `tests/dictionary` wiring — per-test executables with `LABELS codegen`/`dictionary`/`integration`; register the TSan target (`reify_cross_strand_test`) and the `fixpp::dict::codegen-build-graph-check` CTest target — confirmed present; TSan label "dictionary;tsan" on dictionary_reify_cross_strand_test
- [X] T049 [P] `tools/check_layers.py` — add the comment-documented `BRIDGE_SOURCE_FILES`/`BRIDGE_EXEMPT_INCLUDES` bridge exemption (RC#3, `arch §2.4` v0.3); confirm clean exit 0 (research.md D-23 non-blocking follow-up noted) — already implemented; exit 0 confirmed
- [ ] T050 Tier-1 preset matrix green per quickstart §3 — Clang 22 Debug + Release + ASan + UBSan + TSan + Coverage (`[const §IX.2]`/`[const §IX.6]`); coverage ≥90 % line / ≥80 % branch on `tools/codegen/*`, the four `dict/` headers, the wire stub, bridge `.cpp` (`[const §IX.1]`)
- [ ] T051 [P] Static-analysis clean: clang-tidy + clang-format + cppcheck + IWYU on the tool, the bridge, and a sample generated header; pre-commit + Tier-1 (`[const §IX.4]`)
- [ ] T052 [P] `docs/src/dictionary/codegen.md` — how codegen runs, CMake targets, the accessor model, the reify bridge (DoD §12)
- [ ] T053 `/speckit-verify 003-dictionary-codegen` → `.specify/decisions/003-dictionary-codegen-verify.md` must be `GREEN` (mandatory after `/speckit-implement`, `[const §XVII.8]`; precondition for Gate B)
- [ ] T054 `/gate-b` — Codex hostile review → triage → fixer loop; mandatory before merge (`[const §XVII.2]`); independence per `[const §XVII.3]`; label via `gh api` REST `--repo CatalinSerafimescu/fixpp`

---

## Dependencies & Execution Order

### Phase dependencies

- **Setup (Phase 1)**: no dependencies
- **Foundational (Phase 2)**: depends on Setup — **BLOCKS all user stories**
- **US1 (Phase 3, P1)**: depends on Foundational — MVP
- **US2 (Phase 4, P1)**: depends on Foundational; `emit_reify` (T032) depends on `emit_messages` (T024)
- **US3 (Phase 5, P2)**: depends on Foundational; `emit_dispatch` (T036) depends on `emit_reify` (T032)
- **US4 (Phase 6, P2)**: depends on Foundational + the T016 CMake graph
- **US5 (Phase 7, P2)**: depends on US1 emitters (golden anchor needs `emit_messages`/T024+T027)
- **US6 (Phase 8, P3)**: depends on US3 `emit_dispatch` (T036)
- **Polish (Phase 9)**: depends on all targeted stories complete

### Critical implementation chain

`template_writer (T013) → ir (T014) → main (T015) → CMake graph (T016) → emit_fields (T023) → emit_messages (T024) → emit_reify (T032) → emit_dispatch (T036)`. The four RC#1/wire/error headers (T007–T012) are parallelizable and gate `reify.hpp` (T011).

### Parallel opportunities

- T002/T003 (Setup) in parallel
- All Foundational tests T004–T006 in parallel; headers T007–T010, T012 in parallel (T011 after)
- Per story, all `[P]` test tasks in parallel before that story's emitter
- Polish T045–T047, T049, T051–T052 in parallel

---

## Implementation Strategy

### MVP first (US1 only)

1. Phase 1 Setup → 2. Phase 2 Foundational (CRITICAL) → 3. Phase 3 US1 → **STOP & VALIDATE** typed field access for all four versions → demo MVP.

### Incremental delivery

Foundational → US1 (MVP: typed access) → US2 (cross-strand reify) → US3 (runtime dispatch) → US4 (multi-version isolation) → US5 (determinism/goldens) → US6 (negative path) → Polish/bench/gates. Each story is an independently testable increment.

### TDD discipline (`[const §VII.3]`)

Within every phase the test tasks are listed first and MUST fail before the emitter/header task that makes them green. Commit after each task or logical red-green-refactor group.

---

## Notes

- `[P]` = different files, no incomplete-task dependency. `[USx]` maps to spec §3.x for traceability.
- Generated headers are build-tree only (`build/<preset>/_codegen/...`) — never the source tree (AC-C4/AC-T2).
- The four golden headers (T042) are generated codegen output checked in at `/implement` — they were NOT present at Gate A (/clarify Q-golden → A).
- No new fuzz harness: 002's `tests/fuzz/fuzz_dict_xml_loader.cpp` already covers the XML input that drives codegen (spec §7; `[const §VII.7]` satisfied — F1 introduces no new parser).
- No C-ABI surface here → `[const §IX.5]` abidiff N/A (research.md D-17).
- Every test seam in spec §9 + every AC (incl. AC-G7a, AC-C4, AC-VP*/AC-FT*) binds to a named on-disk file or the `fixpp::dict::codegen-build-graph-check` CTest target (plan.md seam→file map).

**Total: 54 tasks** — Setup 3, Foundational 13, US1 11, US2 6, US3 4, US4 3, US5 2, US6 2, Polish 10.
