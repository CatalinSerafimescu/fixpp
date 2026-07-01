---
description: "Task list for 057 — Behavioral Reify / Typed-Read Round-Trip Unblock"
---

# Tasks: Behavioral Reify / Typed-Read Round-Trip Unblock (057)

**Input**: Design documents from `specs/057-behavioral-reify-unblock/`
**Prerequisites**: plan.md, spec.md, research.md (D-1..D-7), data-model.md (E-1..E-6), contracts/reify-dispatch-bridge.md (C-1..C-5), quickstart.md

**Tests**: REQUESTED and first-class. FR-010 mandates activating the `FIXPP_R6_WIRE_BODY_READY`-guarded deferred tests + adding discriminating, mutation-tested per-field witnesses; SC-003 grades the mutation-discrimination. Each user story's "Independent Test" is a required deliverable.

**Organization**: Grouped by user story. This is a **mechanism-unblock** feature: the dispatch bridge + emitter flip + single forced-regen gate are shared across all paths, so they live in Foundational (blocking). Each User-Story phase then delivers the discriminating tests that prove its path (US3 additionally ships the independent `reify_as` inline impl).

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependency on an incomplete task)
- **[Story]**: Which user story the task serves (US1/US2/US3); omitted for Setup/Foundational/Polish

## Path Conventions

Repository root is the library submodule (`research/G19-fix-fpml-iso20022/library/`). Build caps: max `-j2`, sanitizer presets ONE AT A TIME (WSL2 OOM).

---

## Phase 1: Setup (Baseline / RED capture)

**Purpose**: Establish the pre-feature RED baseline so mutation-discrimination has a proven starting state.

- [ ] T001 Configure the `linux-clang-debug` preset and capture the RED baseline in `${build}/_codegen/include/fixpp/_dispatch/`: assert BOTH `reify_dispatch_application.hpp` and `reify_dispatch_fixt.hpp` currently CONTAIN `dict_reify_wire_body_not_ready` and do NOT contain `detail::owning_message_handle_from_frame`; confirm `FIXPP_R6_WIRE_BODY_READY` is defined nowhere in the tree (`git grep -n FIXPP_R6_WIRE_BODY_READY` shows only the `#ifndef` guard sites in `tests/dictionary/reify_dispatch_test.cpp`, so those deferred tests currently `GTEST_SKIP()`). Record this baseline in the phase log.

---

## Phase 2: Foundational (Blocking Prerequisites — the shared reify mechanism)

**Purpose**: Build the dispatch bridge, the completed `owning_message_handle` storage, the emitter flip, and the single binding forced-regen gate. **⚠️ No user-story test can pass until this phase completes.**

- [ ] T002 Complete `owning_message_handle` storage + pin the construction seam in `include/fixpp/dict/reify.hpp` (E-1, C-2): add to the pimpl `std::pmr::vector<std::byte> bytes_` and `mutable std::optional<wire::MessageView<access_mode::Index>> view_cache_`; declare `namespace fixpp::dict::detail { [[nodiscard]] core::expected_t<owning_message_handle> owning_message_handle_from_frame(resolved_message_version, wire::MessageView<Index> const&, std::pmr::memory_resource*) noexcept; }`; `friend` that ONE name into `owning_message_handle` (passkey/attorney); keep `version/msg_type/view/field_value` signatures unchanged and `as<Msg>()` stubbed (T059); refresh the stale `:70` ("Type-erased owning message") and `:97` ("small-variant OR heap polymorphic owner") comments to the byte-storage reality + add the note "`as<Msg>()` remains AC-R6/T059-deferred; byte storage does not foreclose a future lazily-populated owner-cache" (E-1 comment-refresh owed).
- [ ] T004 [P] Create the private same-module declaring header `src/dictionary/reify_dispatch_bridge.hpp` (C-1): declare `fixpp::dict::reify_dispatch_fixt(view, char, version_profile, mr)` and `fixpp::dict::reify_dispatch_application(view, string_view, application_version, version_profile, mr)`, each `-> core::expected_t<owning_message_handle>` `noexcept`. It MUST `#include` ONLY `<fixpp/dict/reify.hpp>` + `<string_view>` + `<memory_resource>` (every named type arrives transitively via `reify.hpp`); it MUST NOT directly include any build-tree `_dispatch/`/`vXX` header or any `<fixpp/wire/...>` header (would be a `dictionary → wire` `check_layers.py` violation). Depends on T002.
- [ ] T003 [P] Wire shipped `src/dictionary/reify.cpp` to the bridge (C-2, C-5, E-1, D-7): define `detail::owning_message_handle_from_frame` out-of-line (deep-copy `view.bytes()` into `mr` inside `try`, `std::bad_alloc → dict_reify_oom`, set `version = rmv`, leave `view_cache_` empty); `#include "reify_dispatch_bridge.hpp"` (private header) and replace the two placeholder returns — FIXT-admin site (`:211`) → `return reify_dispatch_fixt(view, mt_sv.front(), profile, mr);`, application site (`:236`) → `return reify_dispatch_application(view, mt_sv, *app_ver, profile, mr);`; remap the `get<35>`-absent branch (`:181-192`) from `dict_reify_wire_body_not_ready` → `dict_reify_unknown_msg_type` (FR-009 deliberate remap); correct the stale frozen-stub comment (`:174`). Add NO build-tree include. Depends on T002, T004.
- [ ] T005 [P] Create the checked-in CMake template `cmake/templates/reify_dispatch_bridge.cpp.in` (D-1, C-1): a FIXED/version-independent body defining the two bridge functions by delegating to the inline `dispatch::dispatch_fixt` / `dispatch::dispatch_application`; it is the ONLY translation unit that `#include`s `<fixpp/_dispatch/reify_dispatch_fixt.hpp>` and `<fixpp/_dispatch/reify_dispatch_application.hpp>`. Depends on T004 (signatures must match C-1).
- [ ] T006 Wire the bridge target in `src/dictionary/CMakeLists.txt` + `cmake/Codegen.cmake` (D-1, E-3): `configure_file` the template → `${CMAKE_BINARY_DIR}/_codegen/reify_dispatch_bridge.cpp`; add a private `fixpp_dict_dispatch_bridge` STATIC target compiling it, `target_link_libraries(... PRIVATE fixpp::dict::dispatch fixpp_wire)` (for the `_codegen` include dir — keep it on the bridge target ONLY, never broaden `fixpp_dictionary`), `add_dependencies(fixpp_dict_dispatch_bridge fixpp_codegen_generate)`, link it into `fixpp_dictionary`; declare the `fixpp_dict_dispatch_bridge ↔ fixpp_dictionary` back-edge BOTH ways (mirror the two-way `fixpp_wire ↔ fixpp_dictionary` cycle at `src/dictionary/CMakeLists.txt:32-42`) — do NOT rely on TU co-location; keep the intentional forward-reference to `fixpp::dict::dispatch` (generate-phase-resolved — do not "fix"). Depends on T005.
- [ ] T007 [P] Flip `emit_dispatch_application` in `tools/codegen/fixpp-codegen/emit_dispatch.cpp` (D-3, D-4, C-4): replace the placeholder arm body with `return ::fixpp::dict::detail::owning_message_handle_from_frame(rmv_app, view, mr);` (drop the `(void)own`/`(void)rmv_app`/`(void)profile` suppressions + stale "2b-unblock" comment + placeholder return); REMOVE the `if (m.msg_type.size() != 1) continue;` skip (`:294-296`) and REPLACE the `if (msg_type.size() > 1) return dict_reify_unknown_msg_type` guard (`:279-283`) with length-first two-level dispatch — `switch(msg_type[0])` for `len==1`; `switch(static_cast<uint16_t>(msg_type[0])<<8 | static_cast<uint8_t>(msg_type[1]))` for `len==2` (case labels `('A'<<8)|'S'` …); `empty()`/unmatched → `dict_reify_unknown_msg_type`; emit arms in the existing bytewise-sorted order (determinism). Expected multi-char arms: v44=34, v50sp2=105, v42=0.
- [ ] T008 Flip `emit_dispatch_fixt` in `tools/codegen/fixpp-codegen/emit_dispatch.cpp` (D-4, C-4): replace the placeholder arm (`:159-161`) with `return ::fixpp::dict::detail::owning_message_handle_from_frame(rmv, view, mr);`; drop `(void)rmv` (`:140`) + stale comment. FIXT-admin is single-char only (no multi-char change). Same file as T007 → after T007.
- [ ] T009 **FORCED-REGEN GATE (binding named task — do NOT skip; the `project_codegen_emitter_staleness` false-green trap)** (plan §239-250, D-7): (1) `cmake --build ${build} --target fixpp-codegen`; (2) `rm -rf ${build}/_codegen`; (3) reconfigure (`cmake --preset linux-clang-debug`); (4) assert on BOTH regenerated headers `${build}/_codegen/include/fixpp/_dispatch/reify_dispatch_application.hpp` AND `.../reify_dispatch_fixt.hpp`: `grep -L dict_reify_wire_body_not_ready <hdr>` (placeholder GONE) AND `grep -c 'detail::owning_message_handle_from_frame' <hdr>` > 0 (≥1 live factory call). MUST pass BEFORE any test compiles. Depends on T006, T007, T008.
- [ ] T010 Foundational build + link checkpoint: build `fixpp_dictionary` + `fixpp_dict_dispatch_bridge`; confirm the link resolves the `bridge ↔ dictionary` back-edge with no unresolved `owning_message_handle_from_frame` / `reify_dispatch_*` symbols. Depends on T009.

**Checkpoint**: The reify mechanism is live and links. User-story discriminating tests can now be written and must go GREEN (and RED on mutation).

---

## Phase 3: User Story 1 - Reify an application message into a live typed handle (Priority: P1) 🎯 MVP

**Goal**: `dict::reify()` returns a live `owning_message_handle` for parsed application frames (single- and multi-char MsgType) across v42/v44/v50sp2, with byte-faithful typed reads. (FR-001/003/004/005/014; SC-001/002/003/004.)

**Independent Test**: Parse a real application frame, `dict::reify()`, assert `has_value()`, resolved-version metadata = `{application, resolved version}`, and a typed field read returns the exact wire value; reverting the dispatch arm to the placeholder turns the witness RED.

- [ ] T011 [P] [US1] Add application frame-helper siblings to `tests/support/reify_test_frame.hpp` (E-6), each computing BodyLength+CheckSum: `make_nos_frame_v42()` = v42 NewOrderSingle `35=D,11=ORD1`; `make_nos_frame_v50sp2()` = v50sp2 NewOrderSingle `35=D,11=ORD1`; `make_allocation_report_frame()` = `35=AS` AllocationReport (v44) with a real body field (`70=ALLOC1` AllocID) for the multi-char discriminating read; and `make_fixt_app_applverid_frame()` = a FIXT-transport application frame carrying explicit `ApplVerID(1128)="9"` (FIX50SP2) + a known body field, for the US1 Scenario 4 resolution witness (T013). Reuse the existing `make_nos_frame()` (v44 NOS `35=D,11=ORD1,55=AAPL`) and `frame_view_factory.hpp::make_frame_view()`.
- [ ] T012 [US1] Activate the deferred tests in `tests/dictionary/reify_dispatch_test.cpp` (D-6, FR-010): remove the `FIXPP_R6_WIRE_BODY_READY` `#ifndef/#else/#endif` guards + the `_R6Deferred` `GTEST_SKIP` stubs (keep the real-assertion bodies); flip the `MustIncludeSubsetAllHit` oracle asserts (`:196-234`) from `== dict_reify_wire_body_not_ready` → `ASSERT_TRUE(r.has_value())` + `version().application == c.version`; rewrite the obsolete frozen-stub / `dict_xml_parse_failed` narrative comments (`:323-345`).
- [ ] T013 [US1] Add discriminating per-field witnesses in `tests/dictionary/reify_dispatch_test.cpp` (D-6, FR-003/004/005/014, SC-001/002/003): `reify(make_frame_view(make_nos_frame()), profile_v44, mr)` → `has_value()` + `version()` + `field_value(11) == "ORD1"`; add the v42 + v50sp2 siblings (`field_value(11) == "ORD1"` each); add the multi-char `AS` body-field read (`field_value(70) == "ALLOC1"` — discriminating, not header-only); add a **buffer-reuse** assertion (handle still reads correctly after the source frame buffer is overwritten — FR-005); add the **US1 Acceptance Scenario 4 ApplVerID witness** (FR-003, closes analyze-E1): `reify(make_frame_view(make_fixt_app_applverid_frame()), <FIXT profile with default_appl_ver_id == Unknown>, mr)` → `has_value()` + `version().application == application_version::v50sp2` — the resolved application version is derived from the in-frame `ApplVerID(1128)="9"` via `resolve_application_version`, NOT the profile's `Unknown` default (the only end-to-end witness that a frame-borne `1128` drives resolution through live dispatch; distinct from the negative `UnknownDefaultPropagation_ViaReify` path). Mutation-test EACH witness (revert the arm → placeholder → RED). Depends on T011, T012. (Same file as T012 → sequential.)
- [ ] T014 [US1] Add the preserved-error-contract witnesses in `tests/dictionary/reify_dispatch_test.cpp` (FR-009, SC-004): unknown single-char MsgType → `dict_reify_unknown_msg_type`; unknown/no-arm **multi-char** MsgType → `dict_reify_unknown_msg_type`; **absent tag 35** view → `dict_reify_unknown_msg_type` (a discriminating witness distinct from the "empty-view succeeds" non-discriminating-green trap — assert the exact error, not merely `!has_value()`); `dict_reify_oom` via a failing/limited `memory_resource` in the deep-copy; unresolved application version → the existing `dict_unresolved_application_version` / `dict_unknown_appl_ver_id`. Depends on T012. (Same file → sequential after T013.)

**Checkpoint**: Application reify (single + multi-char, all three versions) is proven live and mutation-discriminating; the reify error contract has 0 regressions. **This is the MVP** (Foundational mechanism + US1 proof — the dam blocking the 30 downstream rows).

---

## Phase 4: User Story 2 - Reify a FIXT-admin message into a live typed handle (Priority: P2)

**Goal**: `dict::reify()` returns a live handle resolved as `session_admin` for a parsed FIXT-admin frame. (FR-002/003/004; SC-002.)

**Independent Test**: Parse a FIXT-admin frame, `dict::reify()`, assert `has_value()`, `version().k == session_admin`, and read a header field exactly.

- [ ] T015 [US2] Add `make_fixt_admin_frame()` (single-char admin MsgType) to `tests/support/reify_test_frame.hpp` (E-6). Same file as T011 → sequential after T011.
- [ ] T016 [US2] In `tests/dictionary/reify_dispatch_test.cpp`: flip the `SevenAdminMsgTypesAllHit` oracle asserts (`:112-135`) from `== dict_reify_wire_body_not_ready` → `has_value()` + `version().k == session_admin`, `.session == vt11`, `.application == Unknown`; add a FIXT-admin **discriminating** header-field witness (read e.g. sender/target CompID exact value). Mutation-test (revert arm → placeholder → RED). Depends on T015. (Same file as US1 tests → sequential.)

**Checkpoint**: FIXT-admin reify proven live via the same bridge; US1 + US2 both independently testable.

---

## Phase 5: User Story 3 - Compile-time typed reify via `reify_as<Msg>` (Priority: P3)

**Goal**: `dict::reify_as<Msg>()` returns a populated `owning_<Msg>` for a matching frame (no runtime bridge). (FR-006.)

**Independent Test**: `dict::reify_as<Msg>()` on a matching parsed frame → `has_value()` + exact field read; mismatch → `dict_reify_msg_type_mismatch`.

- [ ] T017 [US3] Define `reify_as<Msg>` inline in `include/fixpp/dict/reify.hpp` (D-5, C-3, E-5): unwrap `view.get<35>()` — if error/absent tag 35 → `dict_reify_msg_type_mismatch` (mirrors `reify.cpp`'s `!mt_fv` guard); if present and `.as_string() != owning_message_t<Msg>::msg_type_v` → `dict_reify_msg_type_mismatch`; else `return owning_message_t<Msg>::from_view(view, mr)` (propagates `dict_reify_oom`). No bridge; header-definable via dependent names. Same file as T002 (foundational, done) → sequential.
- [ ] T018 [US3] Add `reify_as` tests in `tests/dictionary/reify_dispatch_test.cpp` (FR-006): `reify_as<fixpp::v44::NewOrderSingle>(make_frame_view(make_nos_frame()), mr)` → `has_value()` + exact field read (e.g. ClOrdID `== "ORD1"`); a mismatched-MsgType frame → `dict_reify_msg_type_mismatch`; an absent-35 frame → `dict_reify_msg_type_mismatch`. Depends on T017. (Same file → sequential.)

**Checkpoint**: All three reify entry points (runtime application, runtime FIXT-admin, compile-time `reify_as`) proven live.

---

## Phase 6: Polish & Cross-Cutting Concerns

**Purpose**: Layer-hygiene + determinism guards, FR-011 docs, full local gate, and the mandatory close-out.

- [ ] T019 [P] Layer-hygiene discriminating guard check (SC-005, C-5 Layer-hygiene §, D-7a): assert `python tools/check_layers.py` exits 0 on the clean tree, AND exits **non-zero** if a build-tree (`_dispatch/`/`vXX`) or `<fixpp/wire/...>` `#include` is injected into EITHER `src/dictionary/reify.cpp` OR `src/dictionary/reify_dispatch_bridge.hpp` (the guard must BITE, not merely observe — "no exempt" ≠ "no guard"). `tools/check_layers.py` itself stays UNCHANGED (build-tree pivot needs no exempt).
- [ ] T020 [P] Determinism + build-graph verification (SC-005, B-003-3): `ctest -R 'determinism|build_graph'` green — regenerated `_dispatch/*.hpp` byte-identical run-to-run and `git status --porcelain` clean after reconfigure (multi-char arms emitted in bytewise-sorted order).
- [ ] T021 [P] FR-011 docs — `spec/behaviors-and-limitations.md`: flip **L-003-1** to a **PARTIAL** unblock — runtime `dict::reify()` + `dict::reify_as<Msg>()` shipped (live handles / concrete typed owners), but the `owning_message_handle::as<Msg>()` typed-downcast half remains **AC-R6-deferred (003 spec) / T059-stubbed**. Cite AC-R6 as the still-live deferred contract (NOT a bare "shipped").
- [ ] T022 [P] FR-011 docs — `spec/feature-catalogue.md`: update the 003 §11 R6 roadmap reference + the D-008/OSS-010 supplemental notes to reflect the partial unblock (dispatch live; per-message A/M/P/N rows still downstream — FR-013).
- [ ] T023 Full local gate (`/speckit-verify` intent): ASan/UBSan/TSan presets ONE AT A TIME (`-j2`, WSL2); coverage ≥95% line / ≥85% branch on `src/dictionary/` + `include/fixpp/dict/` (the generated `_dispatch/*.hpp` inline arms are coverage-EXCLUDED as generated headers per plan Article IX disposition; only the two bridge wrapper functions carry the ≥95% target); clang-tidy / clang-format / cppcheck / iwyu clean. No new fuzz harness owed (reify re-frames an already-fuzzed validated view — plan Article VII §7). **Record the generated-arm coverage exclusion as a named LOW-risk waiver** in `.specify/decisions/057-behavioral-reify-unblock-verify.md` (`## Coverage-Waivers` / Opus risk assessment) per Article IX §1 (representative-subset grading, `[2c-codegen]` seam #15b) — a recorded rationale, not a silent exclusion.

### Mandatory close-out tasks (ALWAYS emit — Gate-B preconditions, Article XVII §8)

- [ ] T024 [P] **Catalogue close-out**: flip any 057-owned OFFICIAL row in `spec/feature-catalogue.md` → `done` (with PR/evidence ref); 057 is a mechanism unblock of 003's R6, so if it owns no dedicated row, record that the D-008/OSS-010 R6 supplemental notes were updated (T022) and ADD a matching `spec/coverage-index.md` entry for the 057 reify mechanism (reify()/reify_as + bridge, tests, dispositions).
- [ ] T025 **Feature-completeness audit (MUST be the FINAL task)**: assert against the merged tree that (i) every `tasks.md` row is `[X]` or carries an explicit waiver rationale; (ii) every spec FR-001..014 and SC-001..006 maps to a landed test AND a landed implementation (incl. FR-012/SC-006 "zero new wire/error/public-builder/C-ABI/dependency surface" confirmed by diff review — the private declaring header, CMake template, build-tree bridge TU, and `fixpp_dict_dispatch_bridge` target are internal build artifacts; FR-013 no downstream scope creep); (iii) any 057-owned OFFICIAL catalogue row is `done` with a matching `coverage-index.md` entry. Record the verdict (100% or fully-waived) in `.specify/decisions/057-behavioral-reify-unblock-verify.md` (`## Completeness`) OR a sibling `.specify/decisions/057-behavioral-reify-unblock-completeness.md`. Hard `/gate-b` precondition (Article XVII §8 / pre-flight 4d).

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (T001)**: none — start immediately.
- **Foundational (T002–T010)**: after Setup — **BLOCKS all user stories**. Internal order: T002 → T004 → {T003, T005} → T006; T007 → T008; then T009 (needs T006+T007+T008) → T010.
- **US1 (T011–T014)**, **US2 (T015–T016)**, **US3 (T017–T018)**: each after Foundational (T010). Independently testable; may proceed in parallel by different implementers subject to the shared-file note below.
- **Polish (T019–T025)**: after the desired user stories complete. T025 is the FINAL task.

### Shared-file note (no cross-file [P] within these)

- `tests/dictionary/reify_dispatch_test.cpp`: T012 → T013 → T014 (US1), T016 (US2), T018 (US3) — all sequential (same file).
- `tests/support/reify_test_frame.hpp`: T011 (US1) → T015 (US2) — sequential (same file).
- `tools/codegen/fixpp-codegen/emit_dispatch.cpp`: T007 → T008 — sequential (same file).
- `include/fixpp/dict/reify.hpp`: T002 (foundational) → T017 (US3) — sequential (same file).

### Parallel Opportunities

- Foundational: T003 ∥ T005 (after T004); T007 ∥ the T002→T004 bridge-substrate chain (independent file).
- Polish: T019 ∥ T020 ∥ T021 ∥ T022 ∥ T024 (distinct files/gates); T023 (sanitizer presets) runs one-at-a-time internally.

---

## Implementation Strategy

### MVP (minimum shippable proof)

1. Phase 1 Setup (T001 — RED baseline).
2. Phase 2 Foundational (T002–T010 — the reify mechanism; **T009 forced-regen gate is non-skippable**).
3. Phase 3 US1 (T011–T014 — application reify proven live + error contract).
4. **STOP and VALIDATE**: application round-trip byte-faithful across v42/v44/v50sp2 + multi-char, mutation-discriminating.

### Incremental Delivery

- MVP (Foundational + US1) → the dam is lifted for the largest message family.
- + US2 (FIXT-admin) → second bridge consumer proven.
- + US3 (`reify_as`) → compile-time typed entry proven.
- + Polish → hygiene/determinism guards, FR-011 docs, full gate, mandatory close-out.

### Notes

- Tests are RED-first: activate/flip, watch the witness go GREEN on the live arm, then mutation-test (revert arm → placeholder → RED) per SC-003.
- `dict_reify_wire_body_not_ready` has **zero live producers** after this feature (retired from all success + the get<35>-absent paths); it remains defined in the enum (enum-value removal out of scope).
- Do NOT expand into per-message typed builders / full per-message read coverage (A/M/P/N rows) — out of scope (FR-013).
