---
description: "Task list for 039-ff-tail-hardening (LOW: US2–US5)"
---

# Tasks: F-f tail hardening bundle (LOW)

**Input**: Design documents from `/specs/039-ff-tail-hardening/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, quickstart.md

**Tests**: This feature *is* test/comment/build-gate/doc work — there is no separate "production code"
to TDD against. US2 and US3 deliver test witnesses; the assertions ARE the deliverable. Where a witness
could pass without discriminating (US2), a mutation-discrimination task is included explicitly.

**Organization**: Tasks grouped by user story (US2–US5). Each user story is independently implementable,
independently testable, and — per the `phase-implementer-sonnet` runaway-scope guard and the spec —
gets **one implementer invocation per story**.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: US2 / US3 / US4 / US5 (US1 split out to merged 040)

## Path Conventions

Library layout (existing): `src/`, `include/`, `tests/`, `spec/` at the submodule root
(`research/G19-fix-fpml-iso20022/library/`). No new modules.

---

## Phase 1: Setup (Shared Infrastructure)

**No tasks.** Existing library layout; no new dependencies, modules, or build infrastructure. All test
seams (`mutex_test_access`, `OsFile` move-ctors) and the corpus gate already exist (verified pre-tasks).

## Phase 2: Foundational (Blocking Prerequisites)

**No tasks.** The four user stories are fully independent — none shares a prerequisite. There is **no
production behavior change** in this feature (FR-014), so there is no foundational production surface.

**Checkpoint**: Foundation is trivially ready — user stories can begin in any order / in parallel.

---

## Phase 3: User Story 2 - C-ABI decimal sentinel behavior pinned (Priority: P1) 🎯 MVP

**Goal**: Pin the ratified frozen-ABI `_checked` `INT64_MIN`-sentinel behavior (`FIXPP_ERR_OK` + ordering/
equal 0) with a regression test + cross-reference comment. **NO production behavior change** — the diff
to `src/capi/decimal.cpp` MUST be comment-only.

**Independent Test**: `ctest -R 'decimal_capi_error' -V` — sentinel (valid exponent) as left/right/both
operands of `_checked` compare/equal returns `FIXPP_ERR_OK` + 0; out-of-domain *exponent* still returns
`FIXPP_ERR_DECIMAL_INVALID`; valid operands unchanged.

### Implementation for User Story 2

- [ ] T001 [US2] Add a regression test in `tests/core/decimal_capi_error_test.cpp` asserting
  `fixpp_decimal_compare_checked` and `fixpp_decimal_equal_checked` of the `INT64_MIN` sentinel POD with
  a valid exponent (`∈ [-38,0]`) return `FIXPP_ERR_OK` with `out_ordering == 0` / `out_equal == 0`, with
  the sentinel as **left, right, and both** operands (AC-1/AC-2; FR-006; SC-003).
- [ ] T002 [US2] In the same test, add the negative + no-regression assertions: an out-of-domain
  *exponent* (outside `[-38,0]`) still returns `FIXPP_ERR_DECIMAL_INVALID` (AC-3; FR-007), and two
  ordinary valid in-domain decimals report the true ordering/equality unchanged (AC-4; FR-007).
- [ ] T003 [US2] Add a cross-reference comment at the new test citing **001 AC-C6 / research.md D-12 /
  `[const §X.1]`** ("ratified frozen-ABI contract — `_checked` validates the exponent domain only; do NOT
  re-fix to reject the sentinel"); if the existing `src/capi/decimal.cpp:43-48` comment does not already
  carry the AC-C6/D-12 citation, add it there too — **comment-only, zero logic change** (FR-006a).
- [ ] T004 [US2] **Mutation-discrimination check** (non-discriminating-witness guard): temporarily mutate
  the `_checked` path in `src/capi/decimal.cpp` to reject the sentinel (return
  `FIXPP_ERR_DECIMAL_INVALID`), rebuild, and confirm the T001/T002 pin test goes **RED**; then revert the
  mutation. Record the RED observation. (Confirms the witness actually pins the behavior, not a tautology.)
- [ ] T005 [US2] Verify the final `src/capi/decimal.cpp` diff vs `main` is **comment-only**
  (`git diff main -- src/capi/decimal.cpp` shows no executable-line change) — the literal gate that this
  story changed no production logic.

**Checkpoint**: US2 fully functional and independently testable; ABI behavior unchanged and now pinned.

---

## Phase 4: User Story 3 - Coverage-waiver remediation (Priority: P2)

**Goal**: Cover three reachable lines previously dispositioned "untestable", or give each a specific
re-measured waiver. Test-only; no production change.

**Independent Test**: `ctest -R 'seqnum.*set_next_outbound|seqnum.*lock_fail|os_file.*move' -V` plus the
033 re-measured coverage report — each previously-waived line now executed or specifically re-waived.

### Implementation for User Story 3

- [ ] T006 [US3] Witness in `tests/session/` that forces `async_lock` failure via the existing
  `SeqnumManager::mutex_test_access()` seam (`include/fixpp/session/seqnum_manager.hpp:162`, returns
  `fixpp::sync::async_mutex&`) and asserts `set_next_outbound` (`src/session/seqnum_manager.cpp:188`)
  returns `error::session_already_closed` (AC-1; FR-008). **Seam mechanic** (non-trivial): drive the
  returned `async_mutex` into its drain/cancelled state so the next `co_await async_lock(...)` inside
  `set_next_outbound` returns the lock-fail error — mirror the existing seqnum lock-fail witnesses if any.
  Must be deterministic (no flake).
- [ ] T007 [P] [US3] Witness in `tests/session/` exercising the `OsFile` move-constructor
  (`src/session/file_store.cpp:401` POSIX / `:503` Windows) and asserting the moved-from
  fd/handle is invalidated (`-1` / `INVALID_HANDLE_VALUE`) and moved-to is valid (AC-2; FR-009).
- [ ] T008 [US3] Re-measure (lcov DA/BRDA, `[const §IX.1]`) the 033 lines previously deferred without
  per-line measurement; for each, either add a covering witness or record a **specific re-measured**
  waiver citing the exact `file:line` and reason (AC-3; FR-010; SC-004). Zero unjustified "untestable"
  dispositions may remain among the three. **First identify the exact lines** — research.md D-3(c) does
  not enumerate them; recover the deferred set from the 033 verify/Gate-B coverage records
  (`.specify/decisions/033-*-verify.md` / `033-*-gateb.md` and `research/reviews/*033*`).

**Checkpoint**: US3 coverage record is honest; US2 + US3 both pass independently.

---

## Phase 5: User Story 4 - §XV.9 no-std-mutex corpus-gate extension (Priority: P2)

**Goal**: Extend the §XV.9 corpus gate's explicit header list to the 7 uncovered session-side awaitable
headers; confirm the gate still passes (they are clean today). Build/test-infra only.

**Independent Test**: `ctest -R 'check_no_std_mutex_corpus' -V` — the gate lists the 7 headers and PASSES.

### Implementation for User Story 4

- [ ] T009 [US4] Re-confirm the uncovered set via the gate's own preprocess criterion
  (`tools/check_no_std_mutex_in_awaitable_headers.sh`: post-`-E`, a header is in scope only if it BOTH
  pulls `asio::awaitable<...>` AND could name a banned `std::*mutex`): the **7** session-side headers
  `detail/has_flush_for_session_close`, `engine`, `file_store`, `memory_store`, `message_store`,
  `reconnect_fsm`, `retrieve_visitor`; confirm `business_messages.hpp` is correctly **excluded** (no
  awaitable include, self-declared `:30`). Record the criterion output (edge case in spec.md).
- [ ] T010 [US4] Add the 7 confirmed headers to the explicit `check_no_std_mutex_corpus` list in
  `tests/sync/CMakeLists.txt:140`, matching the existing entries' **list style** (alignment/quoting) —
  but **do NOT carry the stale `[const §VI.4]` citation** that the existing comment at `:138` uses;
  Article VI §4 is bidirectional coverage-index traceability, not a glob rule (research.md D-4 / Gate A
  round 1 P2 correction). Cite the local CMake-gate convention if a comment is warranted. Run the gate
  and confirm it **PASSES** on the current tree (FR-011; SC-005). **Default-real**: if any added header
  *fails* post-preprocess, that is a genuine §XV.9 violation — surface and fix it (route to
  `fixpp::sync::async_mutex`), do NOT silently drop it from the list. **Premise guard (analyze F2)**: a
  gate failure on any of the 7 would re-trigger a concurrency/Appendix-A surface and invalidate the
  FR-014 "no production change → Gate A not required" premise — STOP and reassess Gate A, do not push
  through.

**Checkpoint**: US4 gate covers the 7 headers and passes; US2–US4 all pass independently.

---

## Phase 6: User Story 5 - L-033-3 design-follow-up doc resolution (Priority: P3)

**Goal**: Resolve the open L-033-3 wording and document the absent-`1137`-ack case. Doc-only.

**Independent Test**: `grep -n 'L-033-3' spec/behaviors-and-limitations.md` — resolved wording (no open
placeholder) and the absent-`1137`-ack case described, consistent with shipped FIXT `DefaultApplVerID`.

### Implementation for User Story 5

- [ ] T011 [US5] Resolve the L-033-3 entry wording in `spec/behaviors-and-limitations.md` (remove the
  open placeholder) and document the absent-`1137`-ack case in the FIXT `DefaultApplVerID(1137)` notes,
  internally consistent with the shipped behavior (FR-012; SC-006). No code/wire/config change.

**Checkpoint**: All four user stories independently functional.

---

## Phase 7: Polish & Cross-Cutting Concerns

- [ ] T012 [P] Run `quickstart.md` validation for US2–US5 (each story's listed `ctest`/`grep` command
  passes as described).
- [ ] T013 Confirm 039 owns **no** OFFICIAL `feature-catalogue.md` row (source-grounded hardening / test-
  completeness / build-gate / doc — adds no normative FIX coverage per spec.md Normative References); if
  a row is nonetheless warranted, add it with a matching `coverage-index.md` entry (T057-class check).
- [ ] T014 Feature-completeness audit (T058 / `[const §XVII.8]`): every tasks.md row `[X]` or waived;
  every FR-/SC- maps to a landed test/doc; catalogue/coverage consistent. Record verdict for gate-b
  pre-flight (`.specify/decisions/039-ff-tail-hardening-completeness.md`).
- [ ] T015 Author the phase-4 lifecycle doc
  `research/G19-fix-fpml-iso20022/phases/phase-4/session/039-ff-tail-hardening.md` explicitly recording
  **Gate A = NOT required** with the FR-014 justification (no wire/C-ABI-behavior/codegen/config surface)
  + the verify verdict — so `/gate-b` pre-flight step 4c does not STOP demanding Gate A evidence.

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)** + **Foundational (Phase 2)**: no tasks — nothing blocks the user stories.
- **User Stories (Phase 3–6)**: fully independent; may be done in any order or in parallel. Priority order
  is US2 (P1, MVP) → US3 (P2) → US4 (P2) → US5 (P3).
- **Polish (Phase 7)**: after all four stories land.

### Within Each User Story

- **US2**: T001/T002 (write pin test) → T003 (comment) → T004 (mutation-discrimination, must go RED) →
  T005 (comment-only diff gate). T004 depends on the test existing.
- **US3**: T006, T007 are parallel (different files); T008 (re-measure) after the new witnesses land so
  the coverage report reflects them.
- **US4**: T009 (confirm set) → T010 (extend list + run gate).
- **US5**: T011 single task.

### Parallel Opportunities

- The four user stories are independent and could be staffed in parallel — but per the runaway-scope guard
  they are implemented **one implementer invocation per story**, sequentially in priority order.
- Within US3, T006 and T007 touch different files and are `[P]`.

---

## Implementation Strategy

### MVP First (User Story 2 only)

1. Phases 1–2: no-ops.
2. Phase 3 (US2): pin test + comment + mutation-discrimination + comment-only-diff gate → **STOP and
   VALIDATE** (the only story with a real correctness risk — the discrimination trap).

### Incremental Delivery

US2 → US3 → US4 → US5, each independently testable, then Phase 7 polish + close-out artifacts.

---

## Notes

- One implementer invocation per user story (runaway-scope guard).
- US2 is the only correctness-sensitive story: comment-only production diff + a mutation-discriminating
  pin test (the 040 non-discriminating-witness class).
- US4 is default-real: a gate failure on any added header is a finding to fix, not to hide.
- Gate A is **not required** (FR-014) — record that explicitly in the phase-4 doc (T015) for gate-b.
- Commit after each user story (logical group).
