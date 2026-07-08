---
description: "Task list for 061-slim — typed application messages write shape-oracle"
---

# Tasks: Typed Application Messages — write shape-oracle + witness harness (061-slim)

**Input**: Design documents from `specs/061-typed-app-messages/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/builder-shape-oracle.md, quickstart.md
**Prereqs merged**: 062 (PR #168, grouped typed-read) + 063 (PR #176, nested group-parse) — grouped/nested dict-backed reads work; L-062 retired.

**Tests**: REQUIRED. This feature's deliverable *is* the witness harness (the write shape-oracle); every FR names a discriminating witness. Test tasks are therefore first-class, not optional.

**Organization**: Grouped by user story (US1–US4). Within the exemplar work the spec pins an **E-first vertical slice** (Assumption "Implementation sequencing"): build the pivotal grouped/nested exemplar E end-to-end (builder → golden → round-trip) before the other four, to de-risk the unproven body-only grouped/nested builder. Task order below front-loads E in each story phase.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies on incomplete tasks)
- **[Story]**: US1–US4 (US-phase tasks only)

## Path Conventions

Single C++ library; repository root = library submodule `research/G19-fix-fpml-iso20022/library/`. All paths below are relative to that root.

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: New test directories + build registration; no production code yet.

- [X] T001 [P] Create `tests/session/golden/` with a `PROVENANCE.md` recording the QuickFIX-cpp version + seed reference each body-only golden was authored from (per data-model §4).
- [X] T002 [P] Create `tests/consumer/` directory scaffold (external-consumer compile witness home, wired in US4) with a placeholder `CMakeLists.txt`.
- [~] T003 (DEVIATION — each test target is registered alongside its source file, since an `add_executable` with no source fails to configure; see per-test CMake edits) Register the new test targets (`test_exemplar_roundtrip`, `test_exemplar_read`, `test_exemplar_build_failclosed`) as stubs in the `tests/session/` CMake test list so they build/skip cleanly before their bodies land.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: The shared `wire::body_builder` primitive + read/diff scaffolding that ALL exemplar and witness work depends on.

**⚠️ CRITICAL**: No US1–US3 task can begin until `body_builder` (T005/T006) and the read-scaffold (T008) exist.

- [X] T004 Re-verify the real generated v44 flyweight group structure against `_codegen/include/fixpp/v44/Messages.hpp` at implementation start (planning existence claims are unreliable — Assumption "Verified group structure"): confirm E `NoOrders(73)→NoPartyIDs(453)→NoPartySubIDs(802)`, AS `Parties NoPartyIDs(453)→NoPartySubIDs(802)`, and 9 genuinely group-free. Record findings inline in the PR notes; adjust seed tables if any tag/chain differs.
- [X] T005 Create `include/fixpp/wire/body_builder.hpp` — public wire-layer header declaring `wire::body_builder` per data-model §1: ctor `body_builder(std::string_view msg_type)`, flat `field(tag, {string_view,char,int64,decimal_t})`, `group_begin(no_tag, delimiter_tag) → group_handle`, `group_handle::add_entry() → entry_handle`, `entry_handle::set_{string,char,int,decimal}`, `entry_handle::group_begin`, `group_end`, `commit(span<byte> out) → expected_t<span<byte>>`. No `memory_resource` on ctor/commit.
- [X] T006 Implement `src/wire/body_builder.cpp` — lift the 020 `wfield`/`wchar`/`wdecimal` helpers out of `business_messages.cpp`'s anon namespace; LIFO open-group stack + recursive accumulate→serialize (count-precedence: `No<Group>=N` before N instances); **fixed internal scratch capped at 3800 B, over-cap → fail closed** (define `body_builder`'s own `constexpr std::size_t` value-equal to the C-ABI `kFrameCap`, which is file-`static`/TU-local at `src/capi/message_write.cpp:106` and NOT cross-TU referenceable; track hoisting a single shared constant as v1.x debt); enforce at `commit()`: INV-2 (no framing tags `8/9/34/49/52/56/10`), INV-3 (decimals canonical via `decimal_t::format`), INV-4 (all-or-nothing to `out`, group-still-open → typed error), INV-5 (each instance non-empty + first field == `delimiter_tag`, mirroring the C-ABI `validate_group_grammar` at `src/capi/message_write.cpp:682-701`). MUST NOT reuse `wire::Writer`.
- [X] T007 Register `src/wire/body_builder.cpp` in the wire-library CMake target and run `tools/check_layers.py` — assert only a `wire→core` edge is introduced (no `wire→dictionary`; delimiter is author-supplied).
- [X] T008 Create `tests/support/app_message_read_scaffold.hpp` — `make_frame(begin_string, body) → bytes` and `parse_dict(bytes, dict, mr) → MessageView<Index>` via the **5-arg dict-backed** `Parser<Index>::parse` path, BeginString-parameterized (data-model §5). Shared by round-trip + read witnesses.
- [X] T009 Add `shape_oracle_profile()` to `tests/interop/support/golden_diff.hpp` — a NEW exclusion set alongside the existing profiles that excludes **only framing tags `{8,9,10,34,52}`** (NOT the interop `default_normalization_tags()` `{9,10,34,52,60,112,122}`, which drops business tag `60`). Decimal comparison stays by-value. **Golden-frame convention**: `diff_transcripts`/`parse_golden` (`golden_diff.cpp`) require each transcript line to carry a `> `/`< ` direction prefix (an unprefixed line yields `dir='?'` and is rejected at the direction check, `golden_diff.cpp:180,220-224`, BEFORE any field compare). Therefore the body-only goldens (T017/T018) MUST be stored as `> <body-bytes>` (single outbound-direction line), and T020 MUST wrap the raw `body_builder` output as a single `GoldenFrame{'>', bytes}` before calling `diff_transcripts`. **`diff_transcripts` is ORDER-SENSITIVE** (verified `golden_diff.cpp:245-262`: `normalized_fields` preserves wire order, the loop compares tag+value positionally) → the exemplar builders MUST emit business fields in **FIX44 dictionary order** (the order QuickFIX emits), and the golden — authored FIRST — is the field-order reference the builder matches. Implement each exemplar slice **seed → golden → builder → round-trip** (golden-first), not builder-first.

**Checkpoint**: `body_builder` compiles + layer-clean; read-scaffold + diff profile ready → exemplar work can begin (E first).

---

## Phase 3: User Story 1 - Build an outbound typed application message body (Priority: P1) 🎯 MVP

**Goal**: Five hand-written `body_builder`-based exemplar builders emitting the body-only (`35=<MsgType>\x01` + business fields) form, with fail-closed atomic validation. E is the pivotal grouped/nested one.

**Independent Test**: Call each builder into a caller span; assert the emitted bytes are the expected canonical body (no `8/9/34/49/52/56/10`); assert invalid inputs / undersized buffer return a typed error and leave the buffer untouched.

### Implementation for User Story 1 (E first)

- [X] T010 [US1] Implement `build_new_order_list` (**E**, 35=E) in `src/session/business_messages.cpp` on `body_builder` — root `66,394,68`, `NoOrders(73)`→per-order `11,67,55,54,38`, nested `NoPartyIDs(453)`→`NoPartySubIDs(802)` (3-level), ≥2 orders with ≥1 carrying the party chain, count-0 case on the optional `453` group. Delimiters `73→11`, `453→448`, `802→523` (data-model §3). **Pivotal — build and prove before the other exemplars.**
- [X] T011 [P] [US1] Implement `build_order_cancel_reject` (**9**, 35=9, group-free) in `src/session/business_messages.cpp` — msg-required `37,11,41,39,434` + optional `102` (int). DONE: observed root-level order is ascending-tag among `11,37,39,41,102,434` (mirrors E's root ascending-tag ordering). See PROVENANCE.md.
- [X] T012 [P] [US1] Implement `build_allocation_report` (**AS**, 35=AS, multi-char + 2-level nested) in `src/session/business_messages.cpp` — msg-required `755,71,794,87,857,54,55,53,6,75`, one `Parties NoPartyIDs(453)→NoPartySubIDs(802)` chain; seed `AllocNoOrdersType(857)=0` so QuickFIX does not treat `NoOrders(73)` as conditionally required (data-model §3.1 AS note). DONE: observed root-level order is ascending-tag among `6,53,54,55,71,75,87,453(group),755,794,857` (`755/794/857` land after the `453` group content, mirroring E's `394`-after-`73` pattern); confirmed `NoOrders(73)` is NOT emitted given `857=0`. See PROVENANCE.md.
> **CORRECTION (implement-time, 2026-07-08) — supersedes the CHK033 "byte-identical to 020" clause below.** Verified conflict: the 020 builders emit business fields in NON-ascending order (D = `11,55,54,38,40,44,60`; 8 = `37,17,150,39,55,54,151,14,6`), but the QuickFIX goldens (SC-001's anchor) are ASCENDING-tag and `diff_transcripts` is order-sensitive. **SC-001 (exemplar body MUST match its QuickFIX golden) is authoritative and overrides byte-identity to the legacy 020 order** — the CHK033 tightening rested on a false premise (that the refactor is byte-preserving; it is not). Evidence it is safe to change order: the existing 020 build/read tests are order-INSENSITIVE (`extract_field_value` finds a tag anywhere, `test_business_messages_build.cpp:138`) and there are NO production callers of these builders (grep of `src/`+`include/`). So the invariant is: refactor onto `body_builder`, emit in the **golden (ascending) order**, MATCH the golden, and preserve field VALUES (020 build+read tests stay green). Byte-identity to the legacy 020 order is NOT required.
- [X] T013 [US1] Refactor `build_new_order_single` (**D**, 35=D) onto `body_builder` in `src/session/business_messages.cpp` — required `11,55,54,60,38,40` + optional `44` (decimal). Emit business fields in the **QuickFIX-golden (ascending-tag) order** `11,38,40,44,54,55,60`; author the D golden first (T018) and match it. Existing 020 build+read tests (order-insensitive) MUST stay green (value preservation). (Supersedes the byte-identity-to-020 clause per the CORRECTION note above.) DONE: refactored onto `wire::body_builder`; ascending order matches golden (`ExemplarRoundtripD` golden-diff green); orphaned `wfield`/`wchar`/`wdecimal`/`kScratchSize`/`kSOH` helpers removed (zero remaining call-sites); `Builder_ScratchOverflow_PerFieldGuards` updated to pin the new single-shot `kBodyCap=3800` cap (the old per-field 1024-scratch narrative no longer applies).
- [X] T014 [US1] Refactor `build_execution_report` (**8**, 35=8) onto `body_builder` in `src/session/business_messages.cpp` — required-complete set `37,17,150,39,55,54,151,14,6`. Emit in the **QuickFIX-golden (ascending-tag) order**; author the 8 golden first (T018) and match it. Existing 020 build+read tests (order-insensitive) MUST stay green. (Supersedes the byte-identity-to-020 clause per the CORRECTION note above.) DONE: refactored onto `wire::body_builder`; ascending order matches golden (`ExemplarRoundtrip8` golden-diff green).
- [X] T015 [US1] Declare the 3 new builders (`build_order_cancel_reject`, `build_new_order_list`, `build_allocation_report`) in `include/fixpp/session/business_messages.hpp`, signature per data-model §2 (`expected_t<span<byte>> build_<msg>(span<byte> out, <typed fields…>) noexcept`). DONE (all 3 declared; `build_order_cancel_reject`/`build_allocation_report` landed this round).
- [X] T016 (+ FR-003 fix: added is_valid_side to build_new_order_list/build_allocation_report — E/AS were missing per-exemplar Side-domain validation; extended FailClosed_OutOfRangeSide to cover E/AS) [US1] Create `tests/session/test_exemplar_build_failclosed.cpp` — discriminating negative witnesses per contracts C4 (INV-4/INV-5/AC-2): empty required string, control-byte/SOH in value, out-of-range char/side, unformattable decimal, malformed UTCTimestamp, undersized buffer → **buffer untouched**, `commit()` with a group still open, empty group instance, wrong delimiter-first field.

**Checkpoint**: All 5 builders emit body-only bytes; fail-closed atomicity + group grammar pinned directly. E proven grouped/nested.

---

## Phase 4: User Story 2 - Round-trip fidelity anchored to an external golden (Priority: P1)

**Goal**: Each exemplar body parses back to its exact seeded values AND matches independently-authored QuickFIX body-only golden bytes — the non-tautological anchor.

**Independent Test**: table-driven harness per exemplar: seed → builder → framer → dict-backed `Parser<Index>::parse` → flyweight → assert every seeded field (incl. nested); AND assert the body matches the checked-in golden (decimal-by-value, framing tags excluded).

### Implementation for User Story 2 (E golden first)

> **QuickFIX golden-authoring rules (validated 2026-07-08 via a linked `libquickfix.so` smoke):** QuickFIX `Message::toString()` emits the body in **ascending tag order** (e.g. D → `35=D 11 38 40 44 54 55 60`); groups are placed with their entries in dictionary order. Strip header (`8,9,34,49,52,56`) + trailer (`10`) to body-only, keeping the leading `35=<MsgType>` + business fields. NOTE `49/56` are NOT in `shape_oracle_profile()`'s exclusion set → they MUST be physically stripped. The exemplar builders therefore emit business fields in ascending-tag order to match. QuickFIX build: `-I<reference-engines/quickfix-cpp/include> -lquickfix -Wl,-rpath,…/lib` (headers `quickfix/fix44/<Msg>.h`).
- [X] T017 [US2] Author `tests/session/golden/new_order_list.fix` (**E**) offline via QuickFIX-cpp for the E seed, strip to body-only (no `8/9/34/49/52/56/10`), store the body as a single `> `-prefixed line (T009 golden-frame convention), record provenance in `PROVENANCE.md`. **First golden — proves the offline authoring path.** DONE: observed root-level order is ascending-tag among `66,68,73,394` (i.e. `66,68,73(group),394` — `394` lands AFTER the `NoOrders` group content, not right after `66`; the pre-verification `66,394,68` shorthand above is superseded by the empirically-observed golden, which is the authoring spec per data-model §4). See PROVENANCE.md for the full observed frame + field-order breakdown.
- [X] T018 [P] [US2] Author the remaining 4 body-only goldens via QuickFIX-cpp — `new_order_single.fix` (D), `execution_report.fix` (8), `order_cancel_reject.fix` (9), `allocation_report.fix` (AS) — including **re-deriving D & 8 as body-only in-submodule files** (do NOT consume the parent `phase-9-harness/BM-*` full-frame transcripts). Record provenance. DONE: all 4 goldens + PROVENANCE.md entries landed (`order_cancel_reject.fix`/`allocation_report.fix` earlier; `new_order_single.fix`/`execution_report.fix` this round, with the field-order CORRECTION note recorded inline).
- [X] T019 [US2] Define the `ExemplarSeed` record + the 5-row constexpr seed table (data-model §3.2) in a shared test header (e.g. `tests/session/exemplar_seeds.hpp`) — scalars + nested group-shape (counts + per-entry field seeds) + `golden_path`, driving both builder call and read-back assertions. DONE: all 5 rows landed (`kNewOrderSingleSeed`/`kExecutionReportSeed` added this round alongside the pre-existing E/9/AS rows).
- [X] T020 [US2] Create `tests/session/test_exemplar_roundtrip.cpp` — table-driven over the 5 seeds: builder → `make_frame` → `parse_dict` (5-arg) → flyweight → assert every seeded field reads back exact, incl. E's 3-level `orders()[i].party_i_ds()[j].party_sub_i_ds()[k].party_sub_id()` and AS's 2-level `party_i_ds()[j].party_sub_i_ds()[k]` (SC-003); THEN wrap the builder output as `GoldenFrame{'>', body}` and assert `diff_transcripts(load_golden(path), {frame}, shape_oracle_profile())` == match (per the T009 golden-frame convention). Exercise the count-of-zero (`NoPartyIDs=0`) case (SC-002). DONE: all 5 exemplars landed (`ExemplarRoundtripD`/`ExemplarRoundtrip8` added this round alongside the pre-existing E/9/AS suites).
- [X] T021 [US2] Add the **byte-exact canonical-decimal assertion** to `test_exemplar_roundtrip.cpp` (FR-004 / contracts C2): for ≥1 decimal field per exemplar, assert the emitted `<tag>=<ascii>\x01` raw bytes equal the canonical expected bytes (e.g. D `44=190.50\x01`), independent of the by-value golden diff. DONE: all 5 exemplars landed — E pins `38=150.75\x01`; 9 pins `102=0\x01` (int-field substitute, no decimal field); AS pins `6=25.5\x01`; D pins `44=190.5\x01` (this round); 8 pins `6=190.5\x01` (this round).

**Checkpoint**: Every exemplar round-trips AND matches its external golden; canonical decimal *format* pinned independently.

---

## Phase 5: User Story 3 - Parse an inbound typed application message (Priority: P2)

**Goal**: Independent inbound read witnesses from hand-authored wire (NOT builder output), cross-checking read vs build.

**Independent Test**: per exemplar, hand-author a wire body → frame → dict-backed parse → flyweight → assert each accessor's exact value; missing required field → typed error.

### Implementation for User Story 3

- [X] T022 [US3] Create `tests/session/test_exemplar_read.cpp` — per exemplar a **hand-authored** wire body (not builder output) → `make_frame` → `parse_dict` → flyweight → assert each business-field accessor returns the exact wire value (discriminating), incl. ≥1 group entry and a nested entry for E (SC-002); plus a missing-required-field → typed-error assertion (FR-007).

**Checkpoint**: Read path independently proven against non-builder wire.

---

## Phase 6: User Story 4 - Typed message headers consumable by external clients (Priority: P2)

**Goal**: Install the generated typed flyweight headers on the public include path and prove external consumability.

**Independent Test**: build a consumer TU outside the build tree against the installed package; include a typed-message header + construct a flyweight → compiles + links.

### Implementation for User Story 4

- [X] T023 [US4] Extend `CMakeLists.txt` — `install(DIRECTORY _codegen/include/…)` for `{v42,v44,v50sp2}` onto the public installed include path, **excluding `_dispatch` and `vt11`** (FR-008).
- [X] T024 [US4] Implement the `tests/consumer/` external-TU witness (CMake target built against the *installed* tree): include a `fixpp::v44::<Msg>` typed header + construct a flyweight over a parsed view; assert it resolves from the installed include path, not a build-tree-private path (SC-004).

**Checkpoint**: Installed package is externally consumable.

---

## Phase 7: Polish & Cross-Cutting Concerns

**Purpose**: Docs, B&L rows, regression gate, and the mandatory close-out tasks.

- [X] T025 [P] Record new `spec/behaviors-and-limitations.md` B-/L- rows: the `body_builder` body-only + fail-closed grammar behavior, and the **v42-no-typed-groups** limitation (why all 5 exemplars are v44) (FR-010).
- [X] T026 (validated; fixed a quickstart drift — build_order_cancel_reject was missing the cxl_rej_reason arg) [P] Run `quickstart.md` end-to-end as written (E build → read-back → golden diff → byte-exact decimal) to validate the documented flow.
- [X] T027 (full debug ctest GREEN except codegen-build-graph-check git-cleanliness gate; diff-verified NO new error-enum/C-ABI/codegen surface; sanitizer legs → /speckit-verify) Confirm SC-005: full Tier-1 sanitizer/analysis matrix green and NO new public wire-format / error-enum / C-ABI surface beyond `body_builder` + the exemplar declarations + the header-install rule (grep the diff; deferred to `/speckit-verify` for the sanitizer legs).

### Mandatory close-out tasks (ALWAYS emit — Gate-B preconditions, Article XVII §8)

- [X] T028 (5 rows in feature-catalogue.md + coverage-index.md carry 061 shape-oracle evidence; status stays backlog per FR-010) [P] **Catalogue close-out**: record the 5 exemplar rows' evidence (exemplar builder + read/round-trip/golden witnesses) in `spec/feature-catalogue.md` and add/update matching `spec/coverage-index.md` entries. **Rows remain `backlog`** for *full* (all-field / all-version) coverage until FR-015a closes them (FR-010) — flip evidence, not status. (T057 analog.)
- [X] T029 (verdict 100%; .specify/decisions/061-typed-app-messages-completeness.md) **Feature-completeness audit (FINAL task)**: assert against the merged tree that (i) every `tasks.md` row is `[X]` or carries an explicit waiver; (ii) every FR-001..FR-010 and SC-001..SC-005 maps to a landed test AND a landed implementation; (iii) the 5 exemplar catalogue rows carry evidence + a matching `coverage-index.md` entry (status intentionally still `backlog` per FR-010, recorded as a waiver rationale, not a gap). Record the verdict in `.specify/decisions/061-typed-app-messages-verify.md` (`## Completeness`) or a sibling `061-typed-app-messages-completeness.md`. Hard `/gate-b` precondition (pre-flight 4d). (T058 analog.)

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: no dependencies.
- **Foundational (Phase 2)**: after Setup. `body_builder` (T005/T006) + read-scaffold (T008) + diff profile (T009) BLOCK all US1–US3 work. T004 (structure re-verify) gates the grouped exemplars (E/AS) specifically.
- **US1 (Phase 3)**: after Foundational. MVP — needs `body_builder`.
- **US2 (Phase 4)**: after US1 (needs the builders) + Foundational (read-scaffold, diff profile). Goldens (T017/T018) can be authored in parallel with US1.
- **US3 (Phase 5)**: after Foundational (read-scaffold). Independent of US1/US2 builders (uses hand-authored wire).
- **US4 (Phase 6)**: independent of the exemplar count — only needs the existing `_codegen` tree; can proceed in parallel once Setup is done.
- **Polish (Phase 7)**: after all desired stories; T028/T029 are LAST.

### E-first vertical slice (spec Assumption "Implementation sequencing")

Prove **E end-to-end before the other exemplars**: T010 (E builder) → T017 (E golden) → the E rows of T020 (E round-trip + nested read-back). This de-risks the unproven grouped/nested body-only builder. Only then fan out D/8/9/AS (T011–T014, T018).

### Parallel Opportunities

- T001/T002 (setup dirs) in parallel.
- After Foundational: T011 (9) and T012 (AS) in parallel with each other once E (T010) is proven; T013/T014 (D/8 refactors) touch the same TU as T010–T012 so serialize those edits.
- T018 (4 goldens) parallel with US1 builder work.
- US4 (T023/T024) parallel with US1–US3.
- Polish T025/T026/T028 in parallel.

---

## Implementation Strategy

### MVP First — the E vertical slice

1. Phase 1 Setup → Phase 2 Foundational (`body_builder` + scaffolds).
2. T010 E builder → T017 E golden → E round-trip/read-back (T020 E rows).
3. **STOP and VALIDATE**: E's 3-level grouped/nested body-only build ⇄ parse ⇄ golden all green. This is the pivotal risk; everything else is a variation on it.

### Incremental Delivery

1. E slice proven (above) → the grouped/nested primitive is de-risked.
2. Fan out flat/group-free exemplars D/8/9 + multi-char AS (T011–T014, T018) → US1/US2 complete for all 5.
3. Add US3 independent read witnesses (T022).
4. Add US4 header install + consumer witness (T023/T024) — orthogonal, can land any time after Setup.
5. Polish + close-out (T025–T029).

---

## Out of Scope (NOT tasks here — recorded for traceability)

- **The §XVIII.7 constitution amendment** (D3/D7: pull FR-015a into v1.0, drop stale C-/R- typed-scope) — a **separate PR with its own Gate A** (plan.md Complexity Tracking; FR-010). It is a precondition for FR-015a, NOT for 061-slim; 061 partially fulfils §XVIII.7 so no Article XX amend-first obligation binds. Do NOT bundle it into this code diff.
- **The remaining ~28 OFFICIAL builders**, **FR-015a itself** (codegen writer-emitter), **v42 grouped/nested writes**, **FR-015b** (all-version), **N-002/003**, **A-014..A-034** — all out of 061-slim per spec Out of Scope.

---

## Notes

- Tests are the deliverable — each builder is only "done" when its discriminating witness (build-failclosed, round-trip+golden, or read) is green, not merely when it compiles.
- **TDD ordering (Article VII §3)**: although `test_exemplar_build_failclosed.cpp` is finalized as a single task (T016), each builder task (T010–T014) writes and red-runs its scoped fail-closed/shape assertion **before** the builder body — the E-first slice (T010→T017→T020 E rows) runs its own red-green cycle first. tasks.md task-ID order is the file-finalization order, not a licence to implement ahead of a failing test.
- D & 8 refactors MUST preserve byte-identical output (existing 020 read tests are the guard).
- Commit after each task or logical group; re-run `codegraph sync` after code-changing tasks (per project CLAUDE.md).
