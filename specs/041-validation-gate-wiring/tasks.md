# Tasks: Validation Gate Wiring (041)

**Input**: Design documents from `specs/041-validation-gate-wiring/` (spec.md, plan.md, research.md, data-model.md, contracts/validation-gate.md, quickstart.md)
**Branch**: `041-validation-gate-wiring` (library submodule)

**Tests**: REQUIRED — this is a TDD project ([const §VII]); test tasks precede their implementation (RED → GREEN) within each story.

**Organization**: by user story. US1 (P1) strict inbound validation; US2 (P1) lenient-by-default preservation; US3 (P2) Engine clock gate. US3 is fully independent of US1/US2.

**Gate A carry-ins (2 P3s, fold into the tasks noted):** (a) split the "reason=10 / Logout-only" shorthand into `NotConnected`-`Reject(reason=10)` vs `LogonSent`-Logout-only; (b) pin `UtcTimestamp → String` in the `field_data_type`(29)→`field_type`(7) map.

## Format: `[ID] [P?] [Story] Description`
- **[P]**: parallelizable (different files, no incomplete-task dependency)
- **[Story]**: US1 / US2 / US3 (setup/foundational/polish carry no story label)

---

## Phase 1: Setup (Shared Infrastructure)

- [X] T001 Confirm the existing wire/dict/session/engine build targets compile clean on branch `041-validation-gate-wiring` (clang-debug ctest baseline GREEN) before edits — establishes the pre-change reference for the FR-002/SC-001 no-op claim.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: the opt-in flag + fail-closed config gate that both US1 and US2 depend on. MUST complete before US1/US2.

- [X] T002 Add `bool validate_inbound_messages = false;` to `SessionConfig` (`include/fixpp/session/session_config.hpp`, after `validate_sequence_numbers` ~line 431). Default **false** (FR-001); plain bool, no new include ([const §XV.9]-safe). Mirror the comment style of `check_comp_id` / `validate_sequence_numbers`.
- [X] T003 [P] RED test: `register_session` with `validate_inbound_messages==true` AND `dictionary==nullptr` returns `core::error::invalid_session_config` (slot 53) — fail-closed (FR-011 / C-5). New test in `tests/session/`.
- [X] T004 Implement the fail-closed config check in `Engine::register_session` (`src/session/engine.cpp`): reject `validate_inbound_messages && !dictionary` with `invalid_session_config`. Make T003 GREEN.

---

## Phase 3: User Story 1 — Opt-in strict inbound validation (Priority: P1) 🎯 MVP

**Goal**: when enabled, every inbound message is validated against the session dictionary (validate-first per FSM arm) and dictionary violations → `Reject(35=3, 373∈{14,2,1,5,6})` without advancing seqnum.
**Independent test**: enable validation on a session; feed one message per violation class; assert each reject reason + that a conformant message dispatches.

### RC-A — production realizability (the validator's dependencies; both currently test-mock-only)

- [X] T005 [US1] Create production `dict::field_type` enum in `include/fixpp/dict/field_type.hpp` — the 7 values (String, Int, Float, Char, Boolean, Data, Length) the validator's type arm switches on (`validator.hpp:296`), promoted out of `tests/support/mock_dict_table.hpp:30`. Document the `field_data_type`(29)→`field_type`(7) collapse, **pinning `UtcTimestamp → String`** (Gate A P3-b; this is why a malformed `52` value is not validator-catchable).
- [X] T006 [US1] Create production `dict::table_view` value type in `include/fixpp/dict/table_view.hpp` — the 6-method surface the validator binds by value (`field_valid_for`, `required_fields`, `group_first_field`, `group_member_tags`, `field_type_of`, `enum_valid`), owning its tables (data-model E-2). `enum_valid()` returns `true` (Phase-1, FR-005).
- [X] T007 [US1] RED test: `Dictionary::as_table_view()` produces a `table_view` whose 5 dictionary-backed methods agree with the source `Dictionary` for representative msg types / tags / groups, and whose `enum_valid` is always `true` (C-1). New test in `tests/dict/` (or `tests/wire/`).
- [X] T008 [US1] Implement `Dictionary::as_table_view()` (the deferred method, `dictionary.hpp:18`) in `include/fixpp/dict/dictionary.hpp` (+ `src/dict/` if out-of-line): forward `field_valid_for`/`required_fields`/`group_first_field`; precompute `group_member_tags` `uint16_t` arrays from `group_fields` `FieldRef` spans; build the global tag→`field_type` map (R-1a) **once at construction** ([const §XV.1] — config-time, not per-message); `enum_valid`→true. Make T007 GREEN.
- [X] T009 [US1] Add a complete-type include of `dict/table_view.hpp` into `include/fixpp/wire/validator.hpp` (replacing the forward declaration's production gap) + a compile-witness TU under `tests/` that instantiates `dictionary_driven_validator` from a real `Dictionary`-backed `table_view` (RC-A — no mock-include-order dependency).
- [X] T009a [US1] **Float parse-error remap in `dictionary_driven_validator::check_field_type` (audit finding — SPEC-FIXED)**: `decimal_t::parse` on a badly-formatted Float returns `decimal_invalid_input` (10) or `decimal_overflow` (11); the current validator Float arm (`validator.hpp:307-313`) passes the non-`decimal_precision_loss` error directly as `inner_err`, leaking non-`wire_*` slots out of `validate()`. Fix: after the `decimal_precision_loss` check, add an `else` branch that remaps any other error to `wire_field_value_out_of_range` (→ reason 5 — type non-conformant), so every error emitted by `validate()` is a `wire_*` slot. Add a RED test in T012 that feeds a Float field with a garbage value (e.g., `"abc"`) and asserts `SessionRejectReason=5` (not a raw decimal error). See FR-004 and data-model E-4 Float parse-error remapping note.

### Validate-gate insertion + reject mapping

- [X] T010 [US1] Extend/overload `emit_session_reject_` (`src/session/session.cpp:1597`, hardwired to RefTagID=0/reason=3) to thread a `SessionRejectReason` (+ optional `RefTagID(371)`) through to the **already-capable** `build_reject` (`admin_messages.cpp:613-700`, which already stamps 371/373 — reuse UNCHANGED, RC-C).
- [X] T011 [P] [US1] Add the `constexpr` `wire_*`→`SessionRejectReason` map (data-model E-4): `wire_header_out_of_order(39)→14`, `wire_unexpected_tag(42)→2`, `wire_required_field_missing(38)→1`, `wire_field_value_out_of_range(40)→5`, `wire_field_value_truncated(41)→6`. In `include/fixpp/wire/reject_reason_map.hpp`.
- [X] T012 [US1] RED tests (witnesses, one per violation class → reason): header-out-of-order→14; unexpected-tag→2; required-field-missing→1; type-nonconformant (type arm)→5; Float garbage value→5 (T009a remap); conformant message → dispatched; seqnum NOT advanced on reject (C-2/C-3). **NOTE**: `decimal_precision_loss`→reason=6 arm is dead code with `FIXPP_DECIMAL_T=pod_decimal` — `pod_decimal::from_chars` never returns `decimal_precision_loss`; the arm is a forward-looking guard for fixed-precision decimal types; waived per [const §IX.1] (unreachable arm).
- [X] T013 [US1] RED tests (validate-first Logon-arm ordering + overlap precedence, C-2 rows a–f): dict-invalid Logon → `Reject(35=3)` BEFORE `interpret_logon()`'s silent disconnect; **absent required `52` → `Reject(373=1)`**; **present-but-malformed/stale `52` → existing reason=10 (NotConnected) / Logout-only (LogonSent) path, NOT a validate-first Reject**; inbound `35=3`/`35=5` → no reject loop (drain preserved). Tests use `session_role::acceptor` + `open()` first to keep session in NotConnected (validator_ built at open() time).
- [X] T014 [US1] Implement the per-arm validate gate in `Session::on_inbound_frame` (`src/session/session.cpp`): in `NotConnected`/`LogonSent` arms run validation **before `interpret_logon()`**; in `LogonReceived`/`Active` run it after `scan_frame_header`, **before `check_inbound`** (seqnum); `LogoutSent`/`Disconnected` unchanged (drain, no validation). Guarded on `cfg_.validate_inbound_messages` (SC-005). On `validate()` failure, map via T011 and emit via T010, return without advancing seqnum. T012/T013 GREEN (13 tests total, 7+6).

**Checkpoint**: US1 independently testable — strict validation rejects each violation class with the correct reason; conformant traffic dispatches.

---

## Phase 4: User Story 2 — Lenient-by-default preservation (Priority: P1)

**Goal**: default config (flag false) is byte-for-byte unchanged from the prior release; no validator constructed/invoked.
**Independent test**: run the existing inbound/interop corpus at default config; outcomes identical to baseline (T001).

- [X] T015 [P] [US2] RED/characterization test: with `validate_inbound_messages==false`, an out-of-order-header / undefined-tag / required-missing message is **accepted and dispatched** exactly as before (no validation-induced reject) (C-2 default branch, FR-002).
- [X] T016 [US2] Verify (and, if needed, tighten) the T014 guard so that on the default path the validator and the early MessageView parse are **never constructed/invoked** — SC-005. Add an instrumentation/inspection assertion or a structural test that the default path does not enter the validate block. Make T015 GREEN and confirm no diff vs the T001 baseline.

**Checkpoint**: US2 independently testable — default-off no-op confirmed against baseline.

---

## Phase 5: User Story 3 — Engine fails fast on a missing time source (Priority: P2)

**Goal**: `Engine::start()` rejects an unset clock with `clock_not_set`; valid clock starts unchanged.
**Independent test**: start an engine with null clock → `clock_not_set`, not operational; with valid clock → ok.

- [X] T017 [P] [US3] RED test: `Engine::start()` on a config with `clock==nullptr` returns `error::clock_not_set` and the engine does not become operational (no session loops spawned); with a valid clock returns success and operates unchanged (C-4 / SC-004). New `tests/session/`.
- [X] T018 [US3] Change `Engine::start()` `void`→`[[nodiscard]] expected_t<void>` (`include/fixpp/session/engine.hpp:270`, `src/session/engine.cpp:1079`); call `validate_engine_config(cfg)` at the top, return its error before any `co_spawn`. Make T017 GREEN.
- [X] T019 [US3] Migrate the `start()` call sites to check the result: the `fx.start()` fixture wrapper + the dozens of test/interop direct `start()` callers (RC-C — accurate blast radius; zero production callers, no C-ABI wrapper). Build GREEN across touched suites.

**Checkpoint**: US3 independently testable and independent of US1/US2.

---

## Phase 6: Polish & Cross-Cutting Concerns

- [X] T020 [P] B&L rows added — `## 041-validation-gate-wiring` section: **B-041-1** (opt-in validation wired, supersedes B-004-1/B-005-7 under opt-in), **B-041-2** (clock gate wired, supersedes B-007-2), **L-041-1** (enum-value deferred, supersedes part of L-003-3), **L-041-2** (FIXT two-dict deferred), **L-041-3** (reason-6 unreachable with pod_decimal — forward-looking guard).
- [X] T021 [P] Traceability amendments — `feature-catalogue.md` W-014 (validator now wired into session path, opt-in) + NFR-015 (clock gate wired at start()); `coverage-index.md` §4.5.4 (Reject 35=3 dictionary-violation reasons). Cites `[2b §6.5]`, `[FIX50SP2 §2.1]`, `[2d §4.4]`.
- [X] T022 Feature-completeness audit — 100% FR/SC↔task coverage confirmed by the step-6 re-analyze (all 11 FRs + 5 SCs map to ≥1 task incl. T009a). **One waiver-with-proof: SC-003's reason-6 sub-claim** (Float precision-loss → `SessionRejectReason=6`) is structurally unreachable with the default `pod_decimal` (`decimal_precision_loss` is cross-traits-only; the validator's single-traits `parse` cannot produce it — proof in L-041-3 + `decimal.hpp:82-88/109-113`); the reason-6 path is retained as a forward-looking guard, witnessed by W5a (garbage-Float → reason **5** via the T009a remap). Full disposition recorded in the `/speckit-verify` decision doc.
- [X] T023 [P] §XV.9 no-std-mutex corpus gate **GREEN** (`check_no_std_mutex_corpus` #347 PASSED, 4.32s) — confirms `field_type.hpp`/`table_view.hpp`/`validator.hpp` drag no `std::mutex`/`std::shared_mutex` into the `on_inbound_frame` awaitable closure despite validator.hpp now on that path.
- [ ] T024 Coverage (implement-phase scope): confirm every new branch is exercised — each `wire_*`→reason arm, the default-off skip, the enabled-pass path, each Logon-arm overlap row, the clock-gate reject + success, the fail-closed config check. Formal lcov DA/BRDA gate + full-matrix sanitizer regression → `/speckit-verify` (not duplicated here).
- [X] T025 Implement-phase build/ctest gate — clang-debug clean rebuild GREEN (1421/1421); full `ctest -R "^session_"` = **66/66 passed, 0 failed** on a clean build (the authoritative result; ODR layout-skew false-REDs from incremental builds were ruled out via `--clean-first`); `tools/check_layers.py` = **OK, no layer violations** for the new `dict/field_type.hpp` + `dict/table_view.hpp` + `wire/reject_reason_map.hpp` (+ `session.hpp`/`session.cpp` edits). Git tree clean.
- [X] T026 Fuzz disposition (Article VII §7 — parser-touching gate): 041 wires `dictionary_driven_validator` onto the production hostile-input inbound path, ending the feature-004 "US4 PAUSED — NO fuzz_wire_validator.cpp per HARD CONSTRAINT" deferral (`tests/fuzz/fuzz_wire_parser.cpp:21`, `fuzz_wire_framer.cpp:15`, `CMakeLists.txt:43`). Added `tests/fuzz/fuzz_wire_validator.cpp` feeding arbitrary bytes → frame factory → `Parser<Index>::parse` → `dictionary_driven_validator::validate` built from a real `Dictionary`-backed `table_view` (T008/as_table_view(), built once at static scope). Asserts no crash/UB/exception-escape and every rejection is a defined `wire_*` slot (never a raw `decimal_*` code — active `__builtin_trap()` check witnesses the T009a remap). Registered in `tests/fuzz/CMakeLists.txt` with `fixpp_wire fixpp_dictionary fixpp_core` link. In-PR campaign: 60 s, 1,469,698 runs, **0 crashes**. Stale notes updated in `fuzz_wire_parser.cpp:21`, `fuzz_wire_framer.cpp:15`, `CMakeLists.txt:43`. Requires `-DFIXPP_BUILD_FUZZ=ON` + Clang (same gate as other fuzz targets).

**Checkpoint**: feature complete — `/speckit-analyze` → `/speckit-checklist`(+audit) → `/speckit-implement` ordering already done at plan stage; next pipeline step after implement is `/speckit-simplify` → `/speckit-verify` (Tier-1 full matrix) → Gate B.

---

## Dependencies

### Phase order
- Setup (T001) → Foundational (T002–T004) → US1 (T005–T014) ‖ US3 (T017–T019, independent) → US2 (T015–T016, needs the T014 guard) → Polish (T020–T025).

### Within US1
- RC-A chain: T005 (field_type) → T006 (table_view) → T007 RED → T008 (as_table_view) → T009 (include + compile witness) → T009a (Float parse-error remap, feeds T012's garbage-Float RED).
- Reject chain: T010 (emit extension) + T011 (map) → T012/T013 RED → T014 (gate insertion).

### Story independence
- **US3 is fully independent** of US1/US2 (engine lifecycle, not the inbound path) — can be done in parallel.
- **US2 depends on US1's T014 guard** (it asserts the guard's default-off behaviour).

## Parallel opportunities
- T003, T011, T015, T017, T020, T021, T023 are `[P]` (distinct files / no incomplete-task dependency).
- US1 (RC-A chain) and US3 can proceed concurrently.

## Implementation strategy
1. **MVP = US1 + US2** (the opt-in validator + its zero-cost default) — the headline value; US2 guarantees US1 is safe to ship.
2. US3 (clock gate) in parallel — small, independent.
3. Polish → `/speckit-simplify` → `/speckit-verify` → Gate B.

## Notes
- No production behaviour change at default config (FR-002/FR-009). The only public-API change is `Engine::start()`'s return type (T018), with zero production callers.
- Enum-value checks are out of scope (FR-005); the validator's `enum_valid` is stubbed `true` until the 2c enum tables back `field_ref.enum_table_index`.
