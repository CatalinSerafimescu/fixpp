---
description: "Task list — 066 dictionary-backed inbound receive parse"
---

# Tasks: Dictionary-backed inbound receive parse (066)

**Input**: Design documents from `specs/066-dict-backed-inbound-parse/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/inbound-parse.md, quickstart.md (all present)

**Tests**: REQUIRED and load-bearing. The entire feature exists because `Parser<Index>{dict}` unit tests masked the shipped-path defect for 4 features; every correctness witness MUST drive a frame through **real `Session` dispatch** (and a C-ABI engine loopback) and be **RED-first** on the pre-change dict-free parse (`[const Art VII §3]`; research Decision 6).

**Organization**: by user story (US1/US2/US3 from spec.md). US1 is the MVP. Mechanism (b) — one internal `MessageView` membership-copy accessor returning an OWNED `table_view` — is the shared primitive for both clone and reify (Gate A convergence; no public-API / codegen change).

## Format: `[ID] [P?] [Story] Description`
- **[P]**: parallelizable (different files, no incomplete-task dependency)
- **[Story]**: US1 / US2 / US3
- Paths are repository-root-relative within the library submodule.

## Path Conventions
Single library. Source at `src/` + `include/fixpp/`; tests at `tests/`; docs at `spec/`.

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: shared test scaffolding reused by every witness.

- [X] T001 [P] Add shared test-support in `tests/session/` + `tests/capi/` support headers: a group-bearing FIX44 frame builder (e.g. `ExecutionReport` 35=8 with `NoLegs(555)`×2 carrying leg members, followed by a **trailing outer field** such as 60=…) **and a variant frame carrying an undeclared tag INTERIOR to a group instance** (between two declared members of `NoLegs` entry #1) — for the FR-008/C3 interior-truncation witness; a **real-`Session`-dispatch** harness (mock transport → `parse_and_dispatch_` → application callback capture), and a **C-ABI engine-loopback** harness (registered receive callback reachable via `fixpp_msg_*`/`fixpp_group_*`). Shared by US1/US2/US3.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: the enabling members + the mechanism-(b) primitive. These do NOT flip inbound group-read behavior on their own (that happens at T006), so they land without a regression and unblock every story.

**⚠️ CRITICAL**: US1/US2 cannot be completed until this phase is complete.

- [X] T002 Add a stable-address `std::optional<fixpp::dict::table_view> inbound_tv_` member to `include/fixpp/session/session.hpp` and build it **once** in `open()` (`src/session/session.cpp`, after the non-null-dictionary guard ~:929/1160) via `cfg_.dictionary->as_table_view()`. Not yet consumed by `parse_and_dispatch_` → no behavior change. Mirror the validator's owned `table_view` ownership (`session.cpp:1173`). Guard the `open()`-on-reconnect rebuild to occur between (never during) parses (data-model §Invariant).
- [X] T003 Add the new **internal `MessageView` membership-copy accessor** (returns an OWNED `table_view` copy of the source view's membership by re-concretizing `opaque_dict_`; a dict-free source → empty copy) on `class MessageView` in `include/fixpp/wire/parser.hpp` (~:84 — NOT a new `message_view.hpp`; that file does not exist), respecting `tools/check_layers.py:23` (wire→dictionary allowed). Add a focused unit test proving (a) the copy answers membership identically to the source, and (b) the copy **outlives the source `Dictionary`** (destroy the `Dictionary`, the copy still reads — the lifetime claim mechanism (b) rests on). `tests/wire/`.

**Checkpoint**: enabling members + the shared accessor exist; inbound behavior still unchanged.

---

## Phase 3: User Story 1 - Membership-correct inbound repeating-group reads (Priority: P1) 🎯 MVP

**Goal**: inbound repeating-group reads resolve by dictionary membership (each instance bounded at the first non-member tag) on the shipped path — for the C-ABI, C++ typed, clone, and reify read surfaces.

**Independent Test**: drive the group-bearing frame through real `Session` dispatch; a trailing outer field queried on the LAST group instance is absent / `TAG_NOT_FOUND`, and each instance's own members read correctly with the wire instance count.

### Tests for User Story 1 (RED-first) ⚠️
- [X] T004 [P] [US1] RED witness (C++ real dispatch) in `tests/session/`, TWO assertions through `Session` dispatch, both RED on the current dict-free parse: (a) **trailing** — a trailing outer field on the last group instance is absent, each instance's members correct, count matches (extent runs to end-of-message pre-fix); (b) **interior-truncation** — using the T001 interior-undeclared-tag variant, a declared group member appearing AFTER an undeclared interior tag is absent / `TAG_NOT_FOUND` (the FR-008/C3 permissive→strict headline behavior — the accepted, clarified change gets a DIRECT shipped-path pin, not a `Parser{dict}` unit-tier inference). (SC-001, FR-008/C3)
- [X] T005 [P] [US1] RED witness (C-ABI engine loopback) in `tests/capi/`: same message via `fixpp_group_get_field_*` in a registered receive callback → `FIXPP_ERR_TAG_NOT_FOUND` on the trailing tag at the last instance. Confirm **RED** pre-change. (SC-001/FR-003)

### Implementation for User Story 1
- [X] T006 [US1] Flip `parse_and_dispatch_` (`src/session/session.cpp:316`) from the default `Parser<access_mode::Index>` to `Parser<access_mode::Index>{*inbound_tv_}`. Run T004+T005 → **GREEN**. This is the shared enabler (US2 rides on it). Verify admin dispatch (the same single parse site) still passes. (FR-001/FR-003)
- [X] T007 [US1] Clone propagation (FR-007) in `src/capi/message_write.cpp` (`fixpp_msg_clone` ~:428) + `src/capi/capi_internal.hpp`: add a clone-owned `table_view` (alongside `owned_frame_`/`owned_view_`), populated by copying the source view's membership via the T003 accessor; bind the clone's `MessageView` dict-backed. NO inbound-handle `dict_`-threading (clone stays `dict_`-free; the pre-existing `dict_` is untouched). Add a **clone-identity witness** (`tests/capi/`): the clone reads a group identically (membership-bounded) to its source.
- [X] T008 [US1] Reify propagation (FR-007, mechanism (b)) in `src/dictionary/reify.cpp` (`owning_message_handle` ~:99-121) via the unchanged factory `detail::owning_message_handle_from_frame(rmv, view, mr)` (`include/fixpp/dict/reify.hpp:87`): copy the source view's membership via the T003 accessor into an owned `table_view` on the handle; re-frame with the dict-backed `MessageView` ctor. Confirm **no** public `reify(...)`/factory signature change and **no** codegen dispatch-emitter edit (`tools/codegen/…emit_dispatch.cpp` untouched; call sites byte-identical). Add a **reify-identity witness**.
- [X] T009 [US1] `vg_parser` assessment (FR-006) at `src/session/session.cpp:1869`: dict-back the opt-in validator's own parse from `inbound_tv_` (preferred — uniformity) OR record in research/plan why the validator's own `table_view` walk already suffices (L-063-3). Add a **validator-ON** grouped witness proving the enabled-validator path observes the same membership-bounded extents.

**Checkpoint**: US1 fully functional — inbound group reads membership-correct across C-ABI, typed, clone, reify, and (validator-on) paths.

---

## Phase 4: User Story 2 - Scalar-as-group query returns the documented result (Priority: P1)

**Goal**: querying a scalar tag as a repeating group on an inbound-dispatched message returns `FIXPP_ERR_TYPE_MISMATCH` (the restored E-2/CA-010 contract), not a spurious instance.

**Independent Test**: through real dispatch, `fixpp_msg_get_group(msg, <scalar tag>, …)` → `FIXPP_ERR_TYPE_MISMATCH`.

### Tests for User Story 2 (RED-first) ⚠️
- [X] T010 [P] [US2] RED-first witness (real dispatch + C-ABI loopback) in `tests/session/` + `tests/capi/`: query a scalar tag as a group on a dispatched inbound message. **RED** on the dict-free parse (bogus instance to end-of-message); **GREEN** after T006 restores the delimiter-membership check → `FIXPP_ERR_TYPE_MISMATCH`. (SC-002)

**Checkpoint**: the scalar-as-group contract is restored on the shipped path (no new implementation beyond T006's dict-backing).

---

## Phase 5: User Story 3 - No regression for non-group traffic, admin, or performance (Priority: P1)

**Goal**: existing behavior, admin/session handling, allocation discipline, and arena budget are preserved; every intended delta is explicit.

**Independent Test**: full session + interop + C-ABI suites pass; alloc gate shows no new global-heap on the parse+read path; a representative group-bearing message fits the stack arena.

- [X] T011 [P] [US3] Regression sweep: run the full `session` + `interop` + `capi` suites; for each intended behavior delta, land an explicit, reviewed, discriminating test edit (no silent breakage) and record the delta list. (SC-003)
- [X] T012 [P] [US3] Admin/no-group witness in `tests/session/`: an admin message (Logon/Heartbeat) and a no-repeating-group app message dispatch with unchanged behavior/cost (membership consulted lazily, only on a group read). (SC-003/FR-005)
- [X] T013 [P] [US3] Alloc-gate in `tests/alloc_guard/`: zero new **global-heap** allocation on the inbound parse+read path (membership lazy; sub-views from the per-message stack arena). (SC-004/FR-004)
- [X] T014 [P] [US3] Arena-fit witnesses in `tests/session/`: a representative group-bearing message parses+reads within `kInboundParseArena=16384` AND (admin path) `kAdminParseArena=8192`; a **near-cap/headroom** probe; a **pathological deeply-nested** message **fails closed** within the depth (`kMaxGroupDepth=16`)/entry caps — never over-read/corrupt. (SC-004/FR-009)
- [ ] T015 [US3] Sanitizer lifetime: ASan/UBSan/TSan over the session/clone/reify membership-ownership paths (`inbound_tv_` stable address bound by the Parser, clone-owned `table_view`, reify owned copy) — treat any finding as a real defect.
- [ ] T016 [P] [US3] ABI-freeze witness in `tests/abi/`: exported-symbol golden + `capi_freeze.sha256` byte-identical (no exported C symbol/header/enum/version change — behavioral only). (SC-003/C-ABI GA freeze)
- [ ] T017 [US3] Captured group-bearing interop fixture driven through the interop harness (`tests/interop/`), or record in quickstart/research why no such external fixture is available. (SC-003)

**Checkpoint**: all three P1 stories independently pass; no regressions.

---

## Phase 6: Polish & Cross-Cutting Concerns

- [ ] T018 [P] FR-008 Behaviors & Limitations: add the **B-066/L-066** row to `spec/behaviors-and-limitations.md` — permissive→strict in-group membership (QuickFIX/J-aligned) + the extension story (dictionary currency now; `dialect_overlay`/D-009 is backlog) — AND a **FIX4x negative row (L-066-x)**: FIX40/41/42 register zero groups (L-063-1: legacy `INT` counts; `dictionary.cpp:335` NumInGroup gate), so their inbound group reads become `TYPE_MISMATCH` under dict-backing (structural INT-count registration out of scope for 066). Add a release note. (The strict interior-truncation runtime behavior this row documents is directly proven on the shipped path by T004(b) — documentation of a test-proven behavior, not an unverified claim.)
- [ ] T019 [P] FR-010: amend the **L-063-2** row in `spec/behaviors-and-limitations.md` (the "C++ typed read path is unaffected" claim is false on the shipped path until 066), and confirm the issue **#179** amendment comment (posted 2026-07-09) reflects the final resolution.
- [ ] T020 [P] Run `quickstart.md` end-to-end validation (§1 RED reproduction → §7 065 prerequisite note).
- [ ] T021 [P] **Catalogue close-out**: flip every 066-owned OFFICIAL row in `spec/feature-catalogue.md` → `done` (with PR/evidence ref) AND add/update its matching `spec/coverage-index.md` entry. (Gate-B precondition, Article XVII §8.)
- [ ] T022 **Feature-completeness audit (FINAL task)**: assert against the merged tree that (i) every `tasks.md` row is `[X]` or carries an explicit waiver; (ii) every spec FR-001..010 and SC-001..005 maps to a landed test AND landed implementation (note SC-005 is discharged by 065's real-dispatch witness after 066 lands — cross-reference, not a 066 test); (iii) every 066-owned OFFICIAL catalogue row is `done` with a matching `coverage-index.md` entry. Record the 100%-or-waived verdict in `.specify/decisions/066-dict-backed-inbound-parse-verify.md` (`## Completeness`). HARD `/gate-b` precondition (pre-flight 4d).

---

## Dependencies & Execution Order

### Phase Dependencies
- **Setup (T001)**: no dependencies.
- **Foundational (T002, T003)**: after Setup; BLOCKS the stories. T002 and T003 are independent of each other ([P]-eligible across files).
- **US1 (Phase 3)**: after Foundational. T004/T005 (RED) precede T006; T007/T008 depend on T003 (accessor) + T006 (dict-backed source); T009 depends on T002.
- **US2 (Phase 4)**: T010 authored RED any time after T001; goes GREEN only after **T006**.
- **US3 (Phase 5)**: after US1's T006–T008 land (regression/alloc/sanitizer/ABI all measure the changed path). T011–T017 mutually [P] except T015 (whole-matrix) and T017.
- **Polish (Phase 6)**: after all stories; T022 is the LAST task.

### Within Each User Story
- RED tests written and observed failing BEFORE the implementation that flips them GREEN (T004/T005 before T006; T010 before it goes green on T006).
- Foundational members (T002/T003) before their consumers (T006/T007/T008).

### Parallel Opportunities
- T002 ∥ T003 (different files).
- US1 RED tests T004 ∥ T005.
- US3 witnesses T011 ∥ T012 ∥ T013 ∥ T014 ∥ T016 (distinct test targets).
- Polish docs T018 ∥ T019 ∥ T020 ∥ T021.

---

## Parallel Example: User Story 1

```bash
# RED witnesses first, in parallel (must fail on the dict-free parse):
Task: "T004 [US1] C++ real-dispatch trailing-field-TAG_NOT_FOUND witness in tests/session/"
Task: "T005 [US1] C-ABI engine-loopback trailing-field witness in tests/capi/"
# Then the enabler (T006) flips both GREEN; clone (T007) and reify (T008) follow.
```

---

## Implementation Strategy

### MVP First (User Story 1)
1. T001 setup → T002/T003 foundational.
2. T004/T005 RED → T006 flip → GREEN. **STOP and VALIDATE** the shipped-path fix.
3. T007/T008 clone+reify identity; T009 validator-on.

### Incremental Delivery
- US1 (membership-correct reads, incl. clone/reify) → US2 (scalar-as-group contract, rides on T006) → US3 (regression/alloc/sanitizer/ABI/arena) → Polish (B&L + amendments + close-out).

---

## Notes
- [P] = different files, no incomplete-task dependency.
- Every correctness witness drives **real dispatch**, RED-first — never a `Parser<Index>{dict}` unit parse (which masked this defect).
- Mechanism (b) is ONE accessor shared by clone and reify — no asymmetry, no public-API/codegen change.
- Correctness is scoped to **group-registering** dictionaries (FIX43/44/50/50SP1/50SP2/FIXT); FIX40/41/42 are the documented L-066-x limitation.
- Commit after each task or logical group; treat any sanitizer finding as a real defect until disproven.
