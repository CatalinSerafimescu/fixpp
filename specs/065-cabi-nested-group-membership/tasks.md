# Tasks: Membership-aware C-ABI nested repeating-group read (065)

**Feature**: `065-cabi-nested-group-membership` | **Spec**: [spec.md](./spec.md) | **Plan**: [plan.md](./plan.md)
**Fixes**: issue #179 / L-063-2 (C-ABI nested read absorbs a trailing outer member into the last nested instance → silent wrong value on the GA-frozen `1.5.0` C-ABI).

> **Anchors re-pinned against 066-as-merged (2026-07-10, /speckit-tasks).** The plan/research/data-model line anchors predated 066's `offset_table.cpp`/`message_read.cpp` restructure. Live, verified anchors used below: `struct fixpp_group` = `src/capi/capi_internal.hpp:310` (was stale `:291`); `fixpp_msg_get_group` = `src/capi/message_read.cpp:336`; `fixpp_group_get_nested_group` = `src/capi/message_read.cpp:462`; `stored_group_context()` (private) = `include/fixpp/wire/offset_table.hpp:257`, defined out-of-line at `src/wire/offset_table.cpp:413`; `nested_group_slices` (7-arg) decl = `offset_table.hpp:219`; `gen_` (`#ifndef NDEBUG`) = `offset_table.hpp:274` (was `:262-264`); the skipped witness = `tests/capi/message_read_test.cpp:1101`; the inline-XML `as_table_view()` precedent `TopLevelCollidingGroup296CAbiReadsFullMassQuoteExtent` = `message_read_test.cpp:1735` (was `:1778-1791`); the dict066 loopback red target `capi_dict066_group_membership_red_test` (file `dict066_group_membership_red_test.cpp`) = `tests/capi/CMakeLists.txt:418-432`; loopback scaffold `tests/capi/capi_dict066_loopback_support.hpp`.

## Overview

Net **deletion + primitive reuse**: delete the hand-rolled positional scanner in `fixpp_group_get_nested_group` and delegate nested-instance slicing to the existing membership-aware `OffsetTable::nested_group_slices` (062) + `consume_group_extent` (063). Three **non-C-ABI** surface additions carry the missing input (nested-descent context) to the C-ABI cursor: (1) one `group_context group_ctx` field on the internal `fixpp_group`; (2) a public out-of-line `OffsetTable::group_context_for` seed accessor; (3) a public `OffsetTable::nested_group_slices` 4-arg convenience overload. Exported C-ABI surface is byte-identical (SC-003).

**Two user stories, both P1**: US1 = the correctness fix (the defect); US2 = no-regression + C-ABI freeze held. Both must land together.

---

## Phase 1: Setup — RED discriminator (mutation-proof the witness)

- [ ] T001 Mutation-prove the pre-fix witness is RED for the RIGHT reason. Temporarily delete ONLY the `GTEST_SKIP()` line (`tests/capi/message_read_test.cpp:1102`) — no source fix — build `message_read_test` (`cmake --preset linux-clang-debug && cmake --build build/linux-clang-debug -j2 --target message_read_test`), run `ctest --test-dir build/linux-clang-debug -R MessageReadGroup --output-on-failure`, and CONFIRM `NestedGroupLastInstanceExtentDoesNotAbsorbTrailingOuterMember` (`:1101`) FAILS on the trailing-tag `999`-on-`nested[last]` assertion (returns `FIXPP_ERR_OK`/`"TRAIL"` instead of `FIXPP_ERR_TAG_NOT_FOUND`). Capture the RED output as evidence, then **restore the `GTEST_SKIP()`** (no source fix in this task). (FR-010 discriminator; guards against the FAIL-placeholder / coverage-enshrines-bug trap.)

---

## Phase 2: Foundational — non-ABI seams (blocking prerequisites for US1)

> These three additions carry membership context to the C-ABI cursor. They are enabling infrastructure (no observable behavior on their own — behavior is proven by US1's witnesses). All non-C-ABI.

- [ ] T002 [P] Add the nested-descent context field to the internal cursor: in `src/capi/capi_internal.hpp` `struct fixpp_group` (`:310`) add `fixpp::wire::group_context group_ctx{};` (default `{}` = empty msg_type/depth 0 → safe degradation, FR-008). Ensure `group_context`'s complete type is available in this TU (it is only forward-declared where the cursor is opaque; include the header that defines it — via `parser.hpp`/`group_view.hpp` — so `group_ctx{}` and `.pushed()` compile). Internal struct; public header forward-declares only → zero C-ABI change (SC-003).
- [ ] T003 [P] Add the public out-of-line seed accessor `OffsetTable::group_context_for(std::uint16_t no_tag) const noexcept` returning `stored_group_context().pushed(no_tag)`: DECLARE in `include/fixpp/wire/offset_table.hpp` (next to `stored_group_context()` `:257`), DEFINE OUT-OF-LINE in `src/wire/offset_table.cpp` (next to `OffsetTable::stored_group_context()` `:413`). MUST be out-of-line — `group_context` is only forward-declared in the header (constituent fields stored to avoid pulling in `group_view.hpp`), so an inline `.pushed()` body needs the complete type and won't compile in every including TU (the identical trap that forced the token helper out-of-line last round). Public C++, non-ABI. (research Decision 2; data-model §Invariant.)
- [ ] T004 [P] Add the `nested_group_slices` 4-arg convenience overload `nested_group_slices(const std::byte* slice_data, std::size_t slice_len, std::uint16_t nested_no_tag, const group_context& ctx) const` in `include/fixpp/wire/offset_table.hpp` (near the 7-arg decl `:219`), forwarding to the 7-arg overload with the table's own `opaque_dict_`/`group_member_fn_` and a build-mode-safe token via a new private helper `token_for_nested_cache() const noexcept` (`#ifndef NDEBUG return gen_; #else return {}; #endif`). The token helper is REQUIRED because `gen_` exists only under `#ifndef NDEBUG` (`offset_table.hpp:274`) — forwarding `gen_` directly would not compile in release. Public C++, non-ABI; the 7-arg algorithm + cache keying stay UNTOUCHED (FR-005). (research Decision 4; data-model §Reused.)

**Checkpoint**: library compiles in BOTH debug and release (`linux-clang-debug` + `linux-clang-release`) with the three seams present and unused; ABI golden still byte-identical.

---

## Phase 3: User Story 1 — Correct nested read when a trailing outer member follows the nested group (P1)

**Goal**: bound the last nested instance by dictionary membership so a trailing outer member is `TAG_NOT_FOUND` on the nested cursor.
**Independent test**: the un-skipped witness `NestedGroupLastInstanceExtentDoesNotAbsorbTrailingOuterMember` passes with all positive assertions intact.

### Implementation (delegate to the shared primitive)

- [ ] T005 [US1] Seed the top-level cursor context in `fixpp_msg_get_group` (`src/capi/message_read.cpp:336`): after minting the top-level `fixpp_group`, set `grp->group_ctx = view->offsets().group_context_for(group_tag);` (= `{msg_type, [group_tag]}`). (data-model §Invariant top-level; research Decision 2.)
- [ ] T006 [US1] Rework `fixpp_group_get_nested_group` (`src/capi/message_read.cpp:462-615`): DELETE the Phase-1/Phase-2 hand-rolled positional scanner (incl. the `LCOV_EXCL` dead block). CALL `parent->parent_view->offsets().nested_group_slices(sl->data, sl->len, nested_tag, parent->group_ctx)` for the parent entry `i`'s slice. Map the returned span: **empty span** → presence-probe (nested_tag absent in the entry → `FIXPP_ERR_TAG_NOT_FOUND`; present with count 0 / count-is-last → `FIXPP_ERR_OK`, `*nested_count_out = 0`) preserving C3 exactly (`NestedGroupAbsentTag` / `NestedGroupEmptyGroupCountLastField`). On a non-empty span, mint the nested cursor and set `nested->group_ctx = parent->group_ctx.pushed(nested_tag)`. The dropped coded `WIRE_LIMIT_EXCEEDED` guard is acceptable per FR-009/C6 (over-limit unreachable on the nested path — outer read fails first). (research Decision 1/6; contract C1/C3/C6.)

### Verify US1

- [ ] T007 [US1] Un-skip the witness (FR-010/SC-001): in `tests/capi/message_read_test.cpp:1101` remove the `GTEST_SKIP()` (`:1102`) + its escalation comment; keep ALL positive assertions (nested_count, per-instance member values, trailing member reachable at the OUTER index). Build + run `ctest -R MessageReadGroup` → the witness now PASSES (trailing `999` → `TAG_NOT_FOUND` on `nested[last]`; `999` → OK/"TRAIL" at the outer index; `nested_count == 2`).
- [ ] T008 [P] [US1] Add FR-011 witness (a) — DIRECT `as_table_view()` extent-arithmetic witness in `tests/capi/message_read_test.cpp` (following the inline-XML precedent `TopLevelCollidingGroup296CAbiReadsFullMassQuoteExtent` `:1735`): build the dictionary from inline XML via `XmlLoader{}.load_from_string(<FIX44-shaped XML declaring NoLegs(555) → NoLegSecurityAltID(604) → trailing LegQty(687)>, &arena)` then `dict.as_table_view()` (NOT a hand-built single-`msg_type` `table_view`; NO `FIXPP_DICT_DATA_DIR`, NO CMakeLists change — mallocnesia-safe in `capi_message_read_test`). Parse a FIX44 `ExecutionReport` with `NoLegs(555)` → `NoLegSecurityAltID(604) ×2` → trailing `LegQty(687)`; assert `687` → `TAG_NOT_FOUND` on the LAST nested instance AND `687` → OK at the outer index; assert C-ABI≡C++ typed equivalence via genuine member values + nested count/extent agreement AND the trailing tag's ABSENCE from the typed nested entry's corrected extent through the `field_value()` escape hatch (NOT by asking the typed nested accessor for `687` directly — it has no such accessor). (FR-011(a)/SC-005; contract C7(a); quickstart §6(a).)
- [ ] T009 [US1] Add FR-011 witness (b) — ENGINE-LOOPBACK dispatch-path witness in a NEW dict066-style loopback target: add a `GroupMembershipCapiRed`-style test driving the SAME FIX44 frame through the 066 C-ABI dispatch path via `tests/capi/capi_dict066_loopback_support.hpp` (two-C-ABI-engine plaintext-TCP loopback to a registered receive callback), descend to the last `NoLegSecurityAltID(604)` instance, assert `687` → `TAG_NOT_FOUND`. It CANNOT live in `message_read_test.cpp` (no loopback scaffold / no `FIXPP_DICT_DATA_DIR`); add a new target in `tests/capi/CMakeLists.txt` mirroring `capi_dict066_group_membership_red_test` (`:418-432` — carries `FIXPP_DICT_DATA_DIR` + `FIXPP_CAPI_FEATURE_B_INCLUDES`), OR add a new case on that existing target. May use the shipped `FIX44.xml`. Pins production msg_type/parent-path threading (the 063 Gate-B RC#1 empty-`msg_type` class). (FR-011(b)/SC-005; contract C7(b); quickstart §6(b).)

---

## Phase 4: User Story 2 — No regression on the common case; C-ABI freeze held (P1)

**Goal**: every layout that reads correctly today reads identically; zero exported-symbol/header/enum/version change.
**Independent test**: full existing `tests/capi/` read suite passes unchanged (bar the one un-skipped witness); ABI hygiene gate reports no delta.

- [ ] T010 [P] [US2] SC-002 no-regression: BEFORE relying on the un-skip, grep the existing `NestedGroup*` suite for any case pinning the OLD positional `nc` on a NON-TERMINAL zero/short nested count where the more-correct declared-count/membership path could flip `nc`; confirm NONE regress (research New #3). Then run the full `tests/capi/` read suite (`ctest --test-dir build/linux-clang-debug -R 'capi|message_read' --output-on-failure`) — all green, only the un-skipped witness changed status. Confirm C4 (no-trailing-member / single-entry / per-entry-distinct / multi-instance) byte-identical.
- [ ] T011 [P] [US2] FR-008 dict-free degradation pin (named safety invariant → DIRECT witness, per /analyze E-1): add a discriminating witness that constructs a genuinely **dict-free** view (`Parser<access_mode::Index>{}` / null `group_member_fn_`, no dictionary), parses a nested-group frame with a trailing-outer-member layout, descends via the C-ABI (`fixpp_msg_get_group` → `fixpp_group_get_nested_group`), and asserts (i) no crash / clean under ASan+UBSan and (ii) the result is **byte-identical to today's positional behavior** (the trailing member IS still absorbed into the last nested instance — the null-predicate `build_nested_subview`/`group()` fallback at `src/wire/offset_table.cpp:545-547` reproduces the old scanner exactly, research Decision 3). This turns Decision 3's "correct by construction" claim into a verified regression pin. **Fallback**: if the existing C-ABI harness genuinely cannot mint a dict-free cursor (path unreachable even in-harness post-066), instead record an explicit FR-008 waiver in T016 citing research Decision 3 with a SOURCE-VERIFIED unreachability leg — the witness is strongly preferred. (FR-008.)
- [ ] T012 [P] [US2] SC-003 C-ABI freeze: verify `tests/abi/golden/fixpp_capi_symbols.txt` + `tools/capi_freeze.sha256` are byte-identical to the `1.5.0` baseline (`ctest -R 'abi|freeze'` and/or `sha256sum -c tools/capi_freeze.sha256`). No exported symbol / header / enum / version delta.
- [ ] T013 [P] [US2] SC-004 zero-global-heap + sanitizers: run the allocation-discipline gate over the nested C-ABI read (confirm nested sub-table + slices come from the per-message arena, no global heap) and the ASan/UBSan/TSan matrix over `tests/capi/` + `tests/wire/` (nested sub-table lifetime + slice extents validated, not asserted). (FR-007; treat any sanitizer finding as a real defect.)

---

## Phase 5: Polish & cross-cutting close-out

- [ ] T014 [P] Behaviors & limitations: in `spec/behaviors-and-limitations.md` RETIRE L-063-2 (C-ABI nested positional last-instance — now fixed by 065) and RECORD candidate `L-065-1` (pre-existing typed-path depth-≥2 unpushed-context gap, research Decision 7 — follow-up, not fixed here). Confirm L-062-3 / L-063-4 (membership-collision scope limitation, #180) remain as the C5 tracking rows.
- [ ] T015 Catalogue close-out (Gate-B precondition, Article XVII §8): flip every feature-owned OFFICIAL `spec/feature-catalogue.md` row for 065 to `done` with `evidence_pr` + tests, and add the matching `spec/coverage-index.md` entry (FR-001..011 ↔ SC-001..005 ↔ landed tests).
- [ ] T016 Feature-completeness audit (FINAL — Gate-B HARD precondition, Article XVII §8 / T058-class): against the MERGED tree assert (i) every tasks.md row `[X]` or explicitly waived; (ii) every FR-/SC- maps to a landed test AND landed implementation (incl. FR-008 via T011's witness OR its source-verified waiver); (iii) every feature-owned OFFICIAL catalogue row is `done` with a matching coverage-index entry. Record the verdict (100% or fully-waived) as a `## Completeness` section in `.specify/decisions/065-cabi-nested-group-membership-verify.md` (or a sibling `-completeness.md`). `/gate-b` step 4d HARD-BLOCKS without this record.

---

## Dependencies & execution order

- **T001** (RED proof) first — establishes the discriminator; no source change survives it.
- **Phase 2 (T002–T004)** are mutually independent `[P]` and block Phase 3 (the seams must exist before the rework compiles).
- **Phase 3**: T005 + T006 (implementation) must precede T007 (un-skip GREEN). T008 `[P]` (inline-XML witness) is independent of T009 (loopback witness); both depend on T005/T006.
- **Phase 4 (T010–T013)** all `[P]`, depend on Phase 3 landing (they verify the reworked path). T011 (FR-008 dict-free pin) is independent of the dict-aware witnesses.
- **Phase 5**: T014 `[P]`; T015 then T016 (audit is the FINAL task, reads the merged state).

## Parallel opportunities

- T002 ∥ T003 ∥ T004 (three distinct files/decls, no ordering).
- T008 ∥ T009 (distinct test targets).
- T010 ∥ T011 ∥ T012 ∥ T013 (independent verification gates).

## Requirement coverage (post-/analyze)

All FR-001..011 and SC-001..005 map to at least one task. `/analyze` E-1 (FR-008 had no direct pin) resolved by T011 (dict-free degradation witness, waiver fallback). FR-009's dropped-limit-guard unreachability is exercised transitively via T006 + T013's sanitizer matrix.

## MVP scope

US1 (T001–T009) delivers the fix + witnesses. US2 (T010–T013) is the non-regression / freeze / degradation-safety guard — both stories P1, both required to ship. Phase 5 is the mandatory close-out.
