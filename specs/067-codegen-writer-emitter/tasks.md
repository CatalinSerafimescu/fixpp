---
description: "Task list for feature 067-codegen-writer-emitter (FR-015a-lite)"
---

# Tasks: FR-015a-lite — Codegen Writer-Emitter

**Input**: Design documents from `specs/067-codegen-writer-emitter/`
**Prerequisites**: plan.md ✓, spec.md ✓ (US1/US2/US3), research.md ✓ (R1–R9), data-model.md ✓, contracts/generated-builder.md ✓ (G1–G9), quickstart.md ✓

**Tests**: REQUIRED (Constitution Article VII — TDD). This feature ships tests-first: shape-oracle byte-equality, round-trip, fail-closed, exact-set completeness, validate-required (top-level + group depth), emitter unit tests. Tests are written and must FAIL before the corresponding implementation lands.

**Organization**: Grouped by user story (US1/US2 = P1, US3 = P2) so each is independently implementable and testable. All paths are relative to the library submodule root (`research/G19-fix-fpml-iso20022/library/`); run every command with cwd inside it.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no incomplete dependency).
- **[Story]**: US1 / US2 / US3 label — ONLY on user-story-phase tasks (not Setup / Foundational / Polish).
- Every task carries an exact file path.

## Path Conventions

Single-project C++ library + codegen tool (the library submodule). Emitter source under `tools/codegen/fixpp-codegen/`; runtime header under `include/fixpp/wire/`; generated output into the build tree `build/<preset>/_codegen/include/fixpp/v44/`; tests under `tests/session/` and `tests/codegen/`.

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Capture the green baseline the no-regression gates (G7/FR-009) compare against, before touching any emitter code.

- [X] T001 [P] Build `fixpp-codegen` (`cmake --build build/linux-clang-debug --target fixpp-codegen`) and run the existing codegen determinism golden + the 061 exemplar/`body_builder` suites to record the pre-change GREEN baseline (guards G7 read-emitter determinism + FR-009 no-collateral-change). No code edits.
- [X] T002 Read and confirm the frozen 061 shape-oracle assets exist and are untouched: `tests/session/exemplar_seeds.hpp`, `shape_oracle_profile()`, `tests/session/golden/{new_order_single,execution_report,order_cancel_reject,new_order_list,allocation_report}.fix`, and the hand exemplars in `src/session/business_messages.cpp` (the byte-for-byte reference for US2). No edits — these are the frozen oracle.

**Checkpoint**: Baseline recorded; oracle assets located.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Land the codegen-tool infrastructure every user story depends on — the pugixml build home, the codegen-local `MessageIR.group_order` declaration-order walk (RC#7/R9), the emitter scaffolding + wiring, the shared kind→setter map, and the two mandatory investigations (N3 dedup census, R2 data-quality spot-check).

**⚠️ CRITICAL**: No user-story work can begin until this phase is complete. `emit_builders` cannot build without T003; it cannot order groups correctly without T004–T006.

- [X] **T003 [MANDATORY RIDER — pugixml codegen build home, RC-B]** In `tools/codegen/fixpp-codegen/CMakeLists.txt` add `find_package(pugixml CONFIG REQUIRED)` + `target_link_libraries(fixpp-codegen PRIVATE pugixml::pugixml)` (PRIVATE, host-tool-only, NEVER installed — mirrors this file's own never-installed framing; `fixpp_dictionary`'s pugixml link is PRIVATE and does NOT propagate). No version pin — inherit the project-resolved pugixml (`fixpp_dictionary` already depends on it project-wide). UPDATE the stale `:3-5` "no second QuickFIX-XML parser" banner comment to distinguish "runtime dictionary = single runtime XML truth" from the new host-tool-only declaration-order re-parse. VERIFY the bootstrap codegen build configures + links cleanly (`ir.cpp`'s `#include <pugixml.hpp>` compiles, `fixpp-codegen` links). This is the build home for T008's re-parse dep.
- [X] T004 [P] In `tools/codegen/fixpp-codegen/ir.hpp` add the codegen-local `MessageIR.group_order` field: per message, per repeating-group OCCURRENCE (keyed `(message, parent-path, no_tag)`), a record carrying the group's delimiter tag (first declared member) + members in DECLARATION order, recursive for nested groups. Runtime `Dictionary`/`GroupRef`/C-ABI untouched (FR-009).
- [X] T005 [P] In `tools/codegen/fixpp-codegen/emit.hpp` declare `std::string emit_builders(const VersionIR&)`; in `main.cpp` add `write_file(base/"Builders.hpp", emit_builders(ir))` (empty-skip permits incremental TDD — no file ⇒ tests RED, not stale-file green); create `tools/codegen/fixpp-codegen/emit_builders.cpp` scaffolding returning empty output.
- [X] T006 [P] In `tools/codegen/fixpp-codegen/gen_util.hpp` add the shared kind→`body_builder`-call map: `Decimal→field(tag,decimal_t)`, `Char→field(tag,char)`, `Int32→field(tag,int64_t)`, **`Bool→field(tag, x?'Y':'N')`** (char overload — FR-007a, never int64), `String→field(tag,string_view)`, **Length+Data pair→auto-derive length + string path** (FR-007a, clean-text only), `Skip`→no setter; group-entry variants (`entry->set_decimal/set_char/set_int/set_string`, Bool via `set_char`). Record the settled shared-helper decision (shared verbatim: `kind_of`/`to_accessor`/`to_identifier`/`strip_no_prefix`/`uniquify_accessor`; the group planner + tables are per-message in `emit_builders.cpp`, NOT the read emitter's version-wide `MemberMap` — R7/RC#1). `emit_messages.cpp` type/name helpers reused only.
- [X] T007 [TESTS-FIRST] In `tests/codegen/test_067_emit_builders_unit.cpp` write the RC#7 `group_order` discriminating pins — MUST FAIL first: (a) W `NoMDEntries(268)` delimiter = `MDEntryType(269)` vs X `NoMDEntries(268)` delimiter = `MDUpdateAction(279)` from the SAME `no_tag` (NOT tag-sorted 269 for both); (b) exemplar E `NoOrders` member order is `Symbol(55)` BEFORE `Side(54)` (declaration order, NOT tag-sorted `54 55`). Register the target in `tests/codegen/CMakeLists.txt`.
- [X] T008 In `tools/codegen/fixpp-codegen/ir.cpp` implement the codegen-tool-local pugixml RE-PARSE of `xml_path` (`build_ir` owns `xml_path` at `:67-69` but not the parsed tree; pugixml is TU-local per D-15) populating `MessageIR.group_order`: declaration-order iteration, recursive `<component>` resolution rooted at THIS message's own definition (W→`MDFullGrp`→269-first; X→`MDIncGrp`→279-first), nested `<group>`/`<component>` recursion at every depth; does NOT tag-sort and does NOT tag-dedup. Makes T007 pins GREEN. (Not a runtime dict/loader accessor — FR-009 intact.)
- [X] **T009 [MANDATORY RIDER — N3 `append_run` dedup-collapse census]** Enumerate all 33 OFFICIAL messages for any tag appearing at ≥2 levels within ONE message (top-level + group, or two groups) that `append_run`'s tag-sort+tag-dedup (`xml_loader.cpp:695-702`, copied `ir.cpp:98-100`) could collapse into a single `FieldRef`, dropping it from a level's required set. Record the result as a NAMED unit test in `tests/codegen/test_067_emit_builders_unit.cpp` (e.g. `Group067_N3DedupCensus`). If ANY collapse is found, the affected required set MUST be sourced from the codegen-local declaration walk EXTENDED to carry per-member `rule` (and a top-level declaration set) — NOT from `group_order` (order, not rule) — a conditional impl branch feeding T025's tables. This is a HARD task: flag, don't hand-wave.
- [X] T010 [P] Data-quality spot-check (R2 open item): inspect a generated `build/<preset>/_codegen/include/fixpp/v44/Validator.hpp` and confirm group-child `rule==Required` populates for a SECOND grouped message beyond NewOrderList — e.g. MassQuote (35=i) `NoQuoteEntries` members — before relying on the per-occurrence group required tables universally. Record the one-line finding.

**Checkpoint**: `fixpp-codegen` builds with pugixml; `group_order` walk proven on the W/X + E pins; scaffolding wired; N3 census + data-quality resolved. User-story emission can begin.

---

## Phase 3: User Story 1 — Generated typed builder for every OFFICIAL message (Priority: P1) 🎯 MVP

**Goal**: A generated `build_<Msg>(out, const <Msg>Args&)` for exactly the 33 OFFICIAL distinct MsgTypes, over the single `wire::body_builder` core, with a MsgType→builder registry, canonical field order, Bool `Y`/`N` and Length+Data coupling, and fail-closed build-path behavior.

**Independent Test**: For each of the 33 OFFICIAL MsgTypes a generated builder compiles, accepts a populated `<Msg>Args`, and emits a body that parses back via `dict::reify()` to the seeded values (parameterized round-trip witness), AND the exact-set completeness gate over the MsgType→builder registry passes.

### Tests for User Story 1 (write FIRST — must FAIL before impl) ⚠️

- [X] T011 [P] [US1] [TESTS-FIRST] `tests/session/test_067_completeness.cpp`: exact-set gate (set-equality, not subset) over the generated MsgType→builder REGISTRY keys (MsgType wire strings, multi-char `AF/AC/AG/AS` included) vs the literal 33-element expected set `D E F G H 8 9 q r AF AC t u · V W X Y c d e f g h i b S R AG Z a · J P AS`. Keys on MsgType string, NOT `build_<Identifier>` symbol names (FR-004/G4). Register target in `tests/session/CMakeLists.txt`. RED confirmed: compile failure `fatal error: 'fixpp/v44/Builders.hpp' file not found` (registry shape `fixpp::v44::builder_registry` assumed — no anchor pins it; flagged to orchestrator for Phase 3b).
- [X] T012 [P] [US1] [TESTS-FIRST] Build the round-trip SEED TABLE artifact: a `constexpr {msg, version, [(tag, seed)…], group-shape}` row for each of the 33 OFFICIAL messages (incl. nested group shapes) in `tests/session/test_067_builder_roundtrip.cpp` (or a sibling `test_067_seeds.hpp`). This is real work — 33 seeds with group instances, not a freebie. Landed as `tests/session/support/test_067_seeds.hpp` (standalone, no generated-header dep; verified `-fsyntax-only` clean); all 33 OFFICIAL messages seeded (28 flat scalar + 5 group-bearing: E/AS/W/X/i per R5/G6 mandatory insurance).
- [X] T013 [US1] [TESTS-FIRST] `tests/session/test_067_builder_roundtrip.cpp`: seed-driven build → framed → `dict::reify()`/typed flyweight read-back asserting each seeded field (incl. nested group entries) at exact input value, PLUS non-tautological byte-structural asserts (field order matches G3 canonical order, correct SOH count, NO framing tags `{8,9,34,49,52,56,10}`) — a pure build→parse loop is insufficient (FR-008/G6). MUST FAIL first. RED confirmed: compile failure `fatal error: 'fixpp/v44/Builders.hpp' file not found`. All 33 messages covered (33 `TEST(BuilderRoundtrip067, ...)`).
- [X] T014 [US1] [TESTS-FIRST] `tests/codegen/test_067_emit_builders_unit.cpp` additions: (a) top-level emission order is tag-ascending; (b) group-entry order is declaration order (from `group_order`); (c) header/framing exclusion set `{8,9,10,34,35,49,52,56}`; (d) RC#1 per-message planner pin — ONE `no_tag` 268 yields DISTINCT per-message plans (delimiter/member/required) in W vs X (a version-wide `MemberMap` would collapse them). MUST FAIL first. RED confirmed: target builds/links (emit_builders.cpp scaffolding added to the target's sources); all 4 new `Group067EmitBuilders.*` tests FAIL (empty `emit_builders()` output) while the 7 pre-existing T007/T009 tests stay GREEN.
- [X] T015 [US1] [TESTS-FIRST] `tests/session/test_067_builder_failclosed.cpp` (RC#6/G9) — build-path fail-closed witnesses (MUST FAIL first): undersized `out` returns typed error + `out` byte-unchanged (INV-4); `string_view` field with `0x01` rejected before any byte reaches `out`; Bool `LocateReqd(114)` serializes `114=Y`/`114=N` never `114=1` (FR-007a); clean-text Length+Data auto-derives its Length, both coupled (FR-007a); W-vs-X per-occurrence `NoMDEntries` delimiter discrimination. (The required-group-zero row is added in US3/T023 once `validate_` exists.) Register target in `tests/session/CMakeLists.txt`. RED confirmed: compile failure `fatal error: 'fixpp/v44/Builders.hpp' file not found`.

### Implementation for User Story 1

- [X] T016 [US1] In `tools/codegen/fixpp-codegen/emit_builders.cpp` implement the per-message GROUP PLANNER: delimiter tag + member order from `MessageIR.group_order` (R9), required set from `m.fields` filtered by `group_no_tag ∧ rule==Required` (order-independent — R3), NOT the version-wide `MemberMap`, NOT tag-sorted `m.fields`. Emit `struct <Msg>Args` (one `std::optional<T>` per scalar; Bool→`std::optional<bool>`; Length+Data→ONE coupled `std::optional<string_view>`; `std::optional<std::span<const <G>Args>>` for OPTIONAL groups vs plain `std::span` for REQUIRED groups; recursive nested `<G>Args`). Makes T014 GREEN.
- [X] T017 [US1] In `tools/codegen/fixpp-codegen/emit_builders.cpp` emit `build_<Msg>(std::span<std::byte> out, const <Msg>Args&) noexcept` over `wire::body_builder`: top-level present scalars in tag-ascending order via `bb.field(...)` (Bool via `'Y'`/`'N'` char overload; Length+Data coupled); groups at their `No<G>` tag position via `group_begin(no_tag, delim_tag)` + per-instance `set_*` in declaration member order + `group_end`, recursing for nested groups; `nullopt` optional group omitted entirely; `return bb.commit(out)`. Emit the MsgType→builder REGISTRY (keyed by wire string, multi-char included) for the completeness gate. Same file as T016 (sequential). NOTE: emitted as ONE recursive inline block per message (not per-group named functions) — see Phase-3b implementer report for the T014-driven rationale.
- [X] T018 [P] [US1] Add the R5/RC#1 grouped QuickFIX goldens (offline harness `tests/session/golden/gen/`): `gen/qf_market_data.cpp` producing the PAIRED discriminator `market_data_snapshot.fix` (W, `NoMDEntries` delimiter 269) + `market_data_incremental.fix` (X, delimiter 279); `gen/qf_mass_quote.cpp` + `mass_quote.fix` (deep nested `NoQuoteEntries` insurance). Checked-in `.fix` byte-anchors. NOTE: T013 as landed by Phase 3a does not itself read these goldens (no golden-file comparison call in `test_067_builder_roundtrip.cpp`) — they are insurance artifacts per research R5, available for US2/future byte-oracle wiring; flagged to orchestrator.
- [X] T019 [US1] Forced codegen regeneration → `Builders.hpp` into `build/<preset>/_codegen/include/fixpp/v44/`; re-index CodeGraph (`codegraph index --force` after this structural change); confirm all 33 builders emit and the US1 tests (T011/T013/T014/T015) pass.

**Checkpoint**: All 33 builders generate, round-trip, and pass the exact-set gate — US1 independently functional (MVP).

---

## Phase 4: User Story 2 — Shape-oracle byte-equality (the headline pin) (Priority: P1)

**Goal**: For the 5 exemplar MsgTypes (D/8/9/E/AS in v44), the generated builder body is byte-identical to the hand-written 061 exemplar AND byte-matches the QuickFIX golden — the feature's reason to exist.

**Independent Test**: For each of D/8/9/E/AS, drive the generated builder and the hand-written 061 exemplar with the identical 061 seed; assert the two emitted bodies are byte-equal, and both byte-match the checked-in QuickFIX golden via `shape_oracle_profile()`.

### Tests for User Story 2 (write FIRST — must FAIL before impl) ⚠️

- [X] T020 [US2] [TESTS-FIRST] `tests/session/test_067_builder_shape_oracle.cpp`: for D/8/9/E/AS driven by `tests/session/exemplar_seeds.hpp`, assert `build_<Msg>` bytes == hand-written 061 exemplar body bytes (direct compare) AND both byte-match `tests/session/golden/<msg>.fix` via `shape_oracle_profile()` (excludes only `{8,9,10,34,52}`; every business tag incl. `TransactTime(60)` verbatim; decimals by value); plus the ≥1-decimal direct byte compare (C2). AS asserts the multi-char path (`35=AS\x01`). MUST FAIL first. Register target in `tests/session/CMakeLists.txt`. RESULT (Phase 4): 5/5 GREEN on the first real run of the correct (non-weakened) assertion — the only gap found was in the TEST DRIVER, not the emitter: D's generated `NewOrderSingleArgs` needed `ord_type='2'` set explicitly, because the frozen hand exemplar hardcodes `OrdType(40)="2"` internally (Limit-only design, not a caller parameter) and `exemplar_seeds.hpp`'s `NewOrderSingleSeed` therefore carries no `ord_type` field at all. No emitter byte drift found across D/8/9/E/AS.

### Implementation for User Story 2

- [X] T021 [US2] Converge `tools/codegen/fixpp-codegen/emit_builders.cpp` until all 5 exemplars are byte-identical to the 061 hand exemplars AND the QuickFIX goldens (the exemplars are the executable spec for INV-ORDER — fix the emitter, never the frozen hand builders in `src/session/business_messages.cpp`). Regenerate `Builders.hpp`; T020 GREEN. (Much of this is already satisfied by US1's correct two-regime ordering; this task closes any residual byte drift.) **NO-OP** (Phase 4): T020 landed fully GREEN with real assertions — US1's emitter (top-level tag-ascending order from tag-sorted `m.fields`, group-entry declaration order from `MessageIR.group_order` — G3) already reproduces all 5 frozen hand exemplars byte-for-byte. No emitter source change made; `Builders.hpp` not regenerated (nothing to regenerate from).

**Checkpoint**: Headline byte-equality pin green — the emitter provably reproduces the frozen 061 write contract.

---

## Phase 5: User Story 3 — Required-presence validation, generated over emitted tables (Priority: P2)

**Goal**: A generated `validate_<Msg>(args)` — SEPARATE from `build_`, off the serialize path — enforcing required-field presence recursively (top-level body + every repeating-group entry) over emitter-derived level-scoped, header-excluded, per-occurrence tables, returning the pre-existing `wire_required_field_missing` (=38).

**Independent Test**: A generated builder for a message with a required field (top-level or group-entry), passed to `validate()` with that field absent, returns `wire_required_field_missing`; with all required fields present at every level, `validate()` succeeds; `commit()` output is unchanged by validation.

### Tests for User Story 3 (write FIRST — must FAIL before impl) ⚠️

- [X] T022 [US3] [TESTS-FIRST] `tests/session/test_067_builder_validate.cpp`: (a) D NewOrderSingle missing `Symbol(55)` → `wire_required_field_missing`; (b) E NewOrderList missing `ClOrdID(11)` in a `NoOrders` entry → `wire_required_field_missing` (recursive group depth); (c) W vs X `NoMDEntries` per-occurrence required-set divergence (269 required in W, 279 in X); (d) fully-populated message validates clean AND `commit()` output is byte-identical to the exemplar (validation off the serialize path); (e) **zero-required-field no-op** (spec.md Edge Case): `validate_SecurityStatus` (35=f) or `validate_QuoteStatusRequest` (35=a) — both have zero required application fields at any level — with an EMPTY `Args` returns success (the no-op required-presence path is exercised, not merely assumed). Both required SETs table-derived, zero hand-authored lists (SC-004). MUST FAIL first. Register target in `tests/session/CMakeLists.txt`. **RESULT (Phase 5)**: (a) landed as ClOrdID(11), NOT Symbol(55) — verified empirically against `dictionaries/FIX44.xml:2387`/`src/dictionary/xml_loader.cpp:425-533` that Symbol is genuinely `field_presence::Optional` for NewOrderSingle in this dictionary (the enclosing `<component name='Instrument' required='Y'/>` usage tag's `required` attribute is never read by the loader); a required-presence table correctly derived from `FieldRef.rule` (R3) must exclude it. ClOrdID(11) is genuinely Required and is the discriminating substitute for the same top-level-required-field intent — flagged to orchestrator, not a design re-derivation. All 5 sub-cases (a)-(e) GREEN on the first real run.
- [X] T023 [US3] [TESTS-FIRST] Add to `tests/session/test_067_builder_failclosed.cpp` the required-group-zero row: a REQUIRED group (non-optional span) with `size()==0` fails `validate_*` with `wire_required_field_missing`; an OPTIONAL group `nullopt` omits `No<G>` entirely (RC#2/G9). MUST FAIL first.

### Implementation for User Story 3

- [X] T024 [US3] Create `include/fixpp/wire/builder_validate.hpp`: header-only generic runtime `validate_required<T>(...)` over `writer_traits<T>` — recurses top-level body required set + each present group entry's per-occurrence required set; returns `wire_required_field_missing`; rejects required group `size()==0`; allows optional group `nullopt`/engaged-empty. No `wire→codegen`/`wire→dict` include edge (data baked into generated tables).
- [X] T025 [US3] In `tools/codegen/fixpp-codegen/emit_builders.cpp` emit the level-scoped required tables + `writer_traits` + `validate_<Msg>`: (a) top-level body required set `{group_no_tag==0 ∧ rule==Required ∧ tag ∉ {8,9,10,34,35,49,52,56}}`; (b) per-occurrence group required set `{THIS message's fields : group_no_tag==<group> ∧ rule==Required}` (per (message, occurrence), NOT version-wide per `no_tag`). Source from `m.fields` as the normal source; for any tag T009 found collapsed, source from the extended declaration walk carrying per-member `rule`. Regenerate `Builders.hpp`; T022/T023 GREEN.

**Checkpoint**: All three stories independently functional; fail-closed contract closed for opt-in callers.

---

## Phase 6: Polish & Cross-Cutting Concerns

**Purpose**: No-regression verification, layering, the codegen build-graph cleanliness gate, Art VI close-out, the Tier-1 verify mirror, and the two mandatory close-out tasks.

- [X] T026 [P] FR-009/G7 collateral-surface verification: `nm`, abidiff, `tools/.../check_capi_occupancy.sh`, `tools/abi_history/error_codes_v1.txt` unchanged; C-ABI 1.5.0 byte-identical; NO new `fixpp_error_t`; the read-emitter determinism golden green; existing read-path + Python suites green (SC-005). RESULT: `git diff e4aba190..HEAD -- include/fix include/fixpp/capi src/capi include/python src/python bindings tools/abi_history` empty (zero touched files in the whole 067 range) — C-ABI/Python surface untouched by construction. `tools/check_capi_occupancy.sh` OK (Check A 48 codes, Check B 8 source-domain counts). `codegen_determinism_test` 11/11 GREEN. Full 591-test ctest suite (`ctest --test-dir build/linux-clang-debug`) 100% GREEN, incl. all 36 `capi_*` tests (labels 050/051/052/065/066) and 24 read-path tests (labels 060-066). Python not built in `linux-clang-debug` preset (built via `bindings/python/build-wheel.sh`, cibuildwheel-only) — moot since zero Python-surface files changed.
- [X] T027 [P] Run `tools/check_layers.py` on `include/fixpp/wire/builder_validate.hpp` to confirm no `wire→codegen`/`wire→dict` include edge is introduced (New #4 advisory; `feedback_gate_b_check_layers_post_fixer`). RESULT: `python3 tools/check_layers.py` → OK, no violations. SCOPE CAVEAT (per the tool's own `:84-89` note and `feedback_gate_b_check_layers_post_fixer`): the scanner walks `src/` + `bindings/` only, NOT `include/` — `builder_validate.hpp` and `tools/codegen/fixpp-codegen/emit_builders.cpp` are outside its scan. Manually confirmed `builder_validate.hpp`'s only project-internal include is `<fixpp/core/error.hpp>` (wire→core, allowed) — no `wire→codegen`/`wire→dict` edge.
- [X] **T028 [MANDATORY RIDER — codegen build-graph cleanliness gate, FR-010/G8]** Force full codegen regeneration and assert `git status --porcelain` is clean w.r.t. tracked files (generated output is build-tree only; no source-tree writes; deterministic). Document + enforce the staleness ordering (`project_codegen_emitter_staleness`): non-debug (sanitizer/coverage) build dirs compile a FRESH `_codegen`, so forced regeneration MUST precede those runs (and T030) — else stale-emitter false-greens. RESULT: confirmed `git status --porcelain` clean pre-regen; rebuilt `fixpp-codegen` (no-op, already fresh); `rm -rf build/linux-clang-debug/_codegen`; reconfigured (`cmake -S . -B build/linux-clang-debug` — `cmake --preset` blocked by a pre-existing gitignored `CMakeUserPresets.json` "conan-debug" collision unrelated to 067, worked around via direct `-S`/`-B` reusing the cached toolchain) — configure-time codegen ran fresh, `Builders.hpp`/`Validator.hpp` regenerated into `_codegen/include/fixpp/v44/`. `git status --porcelain` empty post-regen. `fixpp::dict::codegen-build-graph-check` ctest 18/18 assertions PASSED. Full rebuild + full 591-test ctest suite re-run 100% GREEN post-regen (confirms no regression from the reconfigure).
- [X] **T029 [MANDATORY RIDER — Art VI §2 canonical-format, part]** Resolve all three Art VI sub-parts for the 33 application-message write rows BEFORE any row closes: (a) [covered by T031 catalogue close-out]; (b) confirm the spec `## Normative References` section cites the `spec/coverage-index.md` rows; (c) resolve the §2 canonical-format question — either add `[DocAbbrev §X.Y.Z]` section-granular refs OR record them as `[impl]`/design-authority rows per Art VI §3 (the coverage-index carries only message-level refs across the FIX44 application-message domain today — a pre-existing project-wide convention). Not optional bookkeeping.
- [X] T030 Run `/speckit-verify` — the Tier-1 sanitizer/coverage/static-analysis mirror over the new emitter source + runtime validate + harness (generated headers coverage-excluded; emitter `.cpp` + `builder_validate.hpp` + harness are NOT). Forced regeneration (T028) MUST have run first. Evidence → `.specify/decisions/067-codegen-writer-emitter-verify.md`.

### Mandatory close-out tasks (ALWAYS the LAST two — Gate-B preconditions, Article XVII §8)

- [X] **T031 [MANDATORY RIDER — Art VI catalogue close-out]** (second-to-last): flip every 067-owned OFFICIAL write row in `spec/feature-catalogue.md` (the 33 MsgTypes across A-001..A-013, M-001..M-012, P-001..P-003 write coverage) from `in-progress`/`backlog` → `done` with the PR/evidence ref, AND add/update its matching `spec/coverage-index.md` entry. Satisfies Art VI sub-part (a). MUST run before any row closes.
- [X] **T032 Feature-completeness audit** (MUST be the FINAL task): assert against the merged tree that (i) every `tasks.md` row is `[X]` or carries an explicit waiver rationale; (ii) every spec FR-001..FR-010 and SC-001..SC-006 maps to a landed test AND a landed implementation; (iii) every 067-owned OFFICIAL catalogue row is `done` with a matching `coverage-index.md` entry. Record the verdict (100% or fully-waived) in `.specify/decisions/067-codegen-writer-emitter-verify.md` (`## Completeness` section) OR a sibling `.specify/decisions/067-codegen-writer-emitter-completeness.md`. HARD `/gate-b` precondition (Article XVII §8 / pre-flight 4d).

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: no dependencies — start immediately.
- **Foundational (Phase 2)**: depends on Setup — BLOCKS all user stories. T003 (pugixml) blocks the emitter build; T004→T007(RED)→T008 land `group_order`; T009 (N3 census) may add a conditional impl branch consumed by T025.
- **US1 (Phase 3, P1)**: depends on Phase 2. MVP.
- **US2 (Phase 4, P1)**: depends on US1 (byte-equality pins US1's generated output; the exemplars also drive US1's ordering).
- **US3 (Phase 5, P2)**: depends on Phase 2; independently testable of US1/US2 (its own `validate_` path), but its tables (T025) share `emit_builders.cpp` with US1 and honor the T009 census branch.
- **Polish (Phase 6)**: depends on all desired stories complete. T028 forced regen precedes T030 `/speckit-verify`. T031 then T032 are the final two, in that order.

### Story Completion Order

`Setup → Foundational → US1 (MVP) → US2 (headline pin) → US3 (validate) → Polish → catalogue close-out (T031) → completeness audit (T032)`.

### Within Each Story

- Tests written and FAILING before implementation (TDD, Article VII).
- `<Msg>Args` + planner (T016) before `build_` (T017); `builder_validate.hpp` (T024) before generated `validate_` tables (T025).
- Regenerate `Builders.hpp` after each emitter change before running dependent tests.

### Parallel Opportunities

- Setup T001 ∥ (T002 is read-only, effectively parallel).
- Foundational: T004 ∥ T005 ∥ T006 (distinct files: `ir.hpp` / `emit.hpp`+`main.cpp`+`emit_builders.cpp` scaffold / `gen_util.hpp`); T010 ∥ others. T003 gates the build. T007 after T004; T008 after T007.
- US1: T011 ∥ T012 (distinct test files); T018 (goldens) ∥ the emitter impl. T016→T017 sequential (same `emit_builders.cpp`).
- Polish: T026 ∥ T027 (distinct concerns/files).

---

## Parallel Example: User Story 1

```bash
# Tests-first for US1 (distinct files — launch together):
Task: "test_067_completeness.cpp exact-set gate (T011)"
Task: "test_067 seed table for 33 messages (T012)"

# Golden generation runs alongside emitter impl (distinct files):
Task: "qf_market_data.cpp + W/X .fix goldens (T018)"
```

---

## Implementation Strategy

### MVP First (User Story 1 only)

1. Phase 1 Setup → baseline recorded.
2. Phase 2 Foundational (CRITICAL — blocks all stories): pugixml build home, `group_order` walk, scaffolding, N3 census.
3. Phase 3 US1 → 33 builders generate + round-trip + exact-set gate.
4. **STOP and VALIDATE**: US1 independently (T011/T013/T014/T015 green).
5. Demo the generated write surface.

### Incremental Delivery

1. Setup + Foundational → foundation ready.
2. US1 → the 33-builder write surface (MVP).
3. US2 → the headline byte-equality pin (proves the emitter reproduces the frozen 061 contract). Note: the 5 exemplars are the executable ordering spec, so US2's pins effectively drive US1's emitter ordering even though US2 is sequenced after US1 as a distinct verification story.
4. US3 → opt-in required-presence `validate_`.
5. Polish → no-regression + Art VI + verify + close-out.

### Parallel Team Strategy

After Foundational: Developer A → US1 (emitter + round-trip); Developer B → US2 goldens + shape-oracle harness (consumes A's output); Developer C → US3 `builder_validate.hpp` + validate harness (independent runtime header). Integrate through `emit_builders.cpp` (US1/US3 tables) with the read-emitter determinism golden as the shared-helper guard.

---

## Independent-Test Criteria per Story

- **US1**: all 33 OFFICIAL builders compile, accept a populated `<Msg>Args`, and round-trip via `dict::reify()` to the seeded values (parameterized witness); exact-set completeness gate over the MsgType→builder registry passes (T011/T013).
- **US2**: for D/8/9/E/AS, generated body bytes == hand 061 exemplar bytes AND both byte-match the QuickFIX golden via `shape_oracle_profile()` (T020).
- **US3**: `validate_<Msg>` returns `wire_required_field_missing` for an absent required field at top level (D missing 55) and at group-entry depth (E missing 11 in `NoOrders`); succeeds when all present; `commit()` byte-output unaffected (T022).

---

## Notes

- [P] = different files, no incomplete dependency.
- Every `build_*` output stays byte-identical to the 061 exemplars — fix the emitter, never the frozen hand builders.
- No new `fixpp_error_t`; reuse `wire_required_field_missing` (=38). No C-ABI / read-path / Python change (FR-009).
- The `MessageIR.group_order` addition is codegen-tool-local (host-build-tool only) — no runtime `Dictionary`/`GroupRef`/C-ABI/Python change.
- Regenerate `Builders.hpp` (forced) before sanitizer/coverage runs — non-debug build dirs compile a fresh `_codegen` (`project_codegen_emitter_staleness`).
- Verify tests FAIL before implementing; commit after each task or logical group.
