---
description: "Task list for 037-resend-reply-possdup-tags"
---

# Tasks: Resend-reply PossDup wire conformance (GapFill 43/122 + replay dup-tag suppression)

**Input**: Design documents from `specs/037-resend-reply-possdup-tags/`
**Prerequisites**: plan.md ✓, spec.md ✓ (Gate A converged round 2), research.md ✓, data-model.md ✓, contracts/resend-reply-wire.md ✓, quickstart.md ✓

**Tests**: REQUIRED. Witness-driven: the durable regressions are (US1) a GapFill carrying exactly one `43=Y` + one `122==52`, and (US2) a replayed retained-PossDup frame carrying exactly one `43` + one `122`. Both cells are written to **FAIL first** (today the GapFill carries neither tag; today the retain-case replay duplicates them), then made to pass by the two builder edits. Honesty asserts per contract C-4 are mandatory (the retain cell must first prove the STORED frame had 43/122).

**Organization**: by user story. The two production edits live in **different files** (`src/session/admin_messages.cpp` for the GapFill, `src/session/session.cpp` for the replay), so US1 and US2 production edits are independent; within a story the same-file edits are sequential.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: parallelizable (different file / distinct test cell, no dependency on an incomplete task)
- **[Story]**: US1 (GapFill 43/122, P1) · US2 (replay dup-tag suppression, P2)

## Path Conventions

Single library `fixpp`: production in `src/session/`, tests in `tests/session/` + `tests/interop/happy/`, golden tooling in `tests/interop/support/`, docs in `spec/` + `specs/013-*`/`specs/022-*`. All paths repository-relative to the library submodule (`research/G19-fix-fpml-iso20022/library/`).

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: the witness test file + the default-path byte oracle every story leans on.

- [ ] T001 Create the new test file `tests/session/test_resend_reply_possdup.cpp` with a resend-reply fixture that can (a) drive `replay_outbound_range_` to emit a GapFill (or call `build_sequence_reset_gapfill` directly with a known `sending_time`) and (b) store an outbound app frame then replay it via `build_replay_frame`, capturing emitted bytes via an in-memory transport sink. Reuse the 013 resend fixtures + the 022 W7 retain test as shape references. Add a small field-occurrence helper (`count_tag(frame, tag)` + `field_value(frame, tag)`) for the assertions.
- [ ] T002 [P] Register `tests/session/test_resend_reply_possdup.cpp` in the session test CMake target (`tests/session/CMakeLists.txt`) so it builds and is ctest-discovered.
- [ ] T003 [P] Capture the **pre-037 default-path replay byte oracle**: with `allow_pos_dup=false`, store a normal app frame and snapshot `build_replay_frame`'s output bytes — the FR-006/SC-003 byte-identity oracle for T015. Record the capture in the test header comment.

**Checkpoint**: test file compiles + links; the byte oracle is captured.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: none beyond Phase 1 — both builders (`build_sequence_reset_gapfill` `admin_messages.cpp:905`, `build_replay_frame` `session.cpp:1625`) and the `append_raw`/`Writer` path already exist; no new entity/config/error. Proceed to user stories.

**Checkpoint**: Foundation ready — builder edits can begin.

---

## Phase 3: User Story 1 — GapFill carries PossDupFlag(43)=Y + OrigSendingTime(122) (Priority: P1) 🎯 MVP

**Goal**: every `SequenceReset`-GapFill emitted on a resend reply carries exactly one `43=Y` and one `122` equal to its own `52` (FR-001/FR-002/FR-003; INV-1/INV-2/INV-6).

**Independent Test**: a unit cell parses the emitted GapFill and asserts `35=4` + `123=Y` (honesty: this IS the GapFill) then exactly one `43=Y`, exactly one `122`, `value(122)==value(52)`, and the pre-existing field set unchanged. Fails today (no 43/122); passes after T014.

### Tests (write first — must FAIL)

- [ ] T004 [P] [US1] In `tests/session/test_resend_reply_possdup.cpp`, add **Cell 1 (GapFill possdup)**: emit a GapFill; assert `35=="4"` AND `123=="Y"`, then `count_tag(43)==1` AND `value(43)=="Y"`, `count_tag(122)==1`, `field_value(122)==field_value(52)`, and tags `8/35/34/49/52/56/36/123` present/unchanged (the full FR-003 set incl. `52`; `52` presence is additionally pinned by the `122==52` assert). Source-read the body to confirm it genuinely fails on today's builder (no FAIL-placeholder, [[feedback_fail_placeholder_red_test]]).
- [ ] T005 [US1] Build + run T004 → confirm RED (the GapFill lacks 43/122 today).

### Implementation

- [ ] T006 [US1] In `src/session/admin_messages.cpp` `build_sequence_reset_gapfill` (after the `123=Y` block ~:964-970, before `commit()` ~:972), append `append_raw(43, "Y")` then `append_raw(122, sv_to_bytes(sending_time))` — reusing the existing `sending_time` parameter for 122 (no signature change). Mirror the existing `append_raw(...)`-`if (!r) return std::unexpected(r.error())` error-handling shape. Update the builder's field-list comment (~:900-901) to include 43/122.
- [ ] T007 [US1] Update the `build_sequence_reset_gapfill` doc-comment in `include/fixpp/session/admin_messages.hpp:168` to list 43/122 (no signature change).
- [ ] T008 [US1] Build + run T004 → confirm GREEN.

### Golden + live (default-path wire change)

- [ ] T009 [US1] Switch the GapFill golden comparison in `tests/interop/happy/hp_fix44_recovery_outbound_answer_test.cpp` from the `{52,10}` admin profile (`admin_profile_excluded_tags()`, the `diff_golden_or_skip` default) to the existing `poss_dup_profile_excluded_tags()` (`{52,10,122}`, `tests/interop/support/golden_diff.hpp:69`) for the GapFill answer — because the new `122` is a volatile timestamp (==52). `43=Y` is deterministic and stays compared verbatim. The synthetic `123`-mutation gate-bite cells (`:95-107`) call `expect_gate_bite_on_tag` which hardcodes `admin_profile_excluded_tags()` (`hp_support.hpp:334-354`) — the profile switch does NOT affect them; they remain valid because tags `123`/`43` are in neither profile's skip set. Confirm they still bite.
- [ ] T010 [US1] Re-bake the affected golden(s): `tests/interop/happy/golden/<recovery-outbound cell ids>.fix` (the fixpp-emitted GapFill answer — the real re-bake target). Diff must show ONLY the added `43=Y`/`122` on the `35=4` frame. NOTE (verified during /speckit-analyze): the sibling `../phase-9-harness/golden/HP-*-disconnect-reconnect-noreset.fix` and `RL-*-reset-on-logon.fix` candidates contain **zero `35=4` GapFill frames** → no re-bake needed for those. Still spot-check any other `../phase-9-harness/golden/*.fix` with a fixpp-emitted `35=4` (sender `49=<our CompID>`, e.g. 030 RR-*) and re-bake only the fixpp-emitted ones (received GapFills unchanged).
- [ ] T011 [US1] Live QFJ re-run (SC-004 achievable arm): re-run the QFJ in-process recovery-outbound cell + the live QFJ resend / received-reset cells (both roles) and confirm the peer ACCEPTS the now-`43=Y` GapFill and the session reaches steady state. The QFcpp live arm is **waived per L-021-3** (the in-process witness `GTEST_SKIP()`s non-QFJ; QFcpp byte-level conformance is covered by the T004 unit cell + golden) — record the waiver disposition, do not claim QFcpp live coverage.

**Checkpoint**: GapFill possdup tags shipped + witnessed + golden re-baked + QFJ-live green. MVP complete.

---

## Phase 4: User Story 2 — Replayed retained-PossDup frame carries exactly one 43/122 (Priority: P2)

**Goal**: under `allow_pos_dup=true`, a stored app frame already carrying `43`/`122` replays with exactly one of each, `122` = the stored `52` (FR-004/FR-005; INV-3); default-path replay byte-identical (FR-006/INV-4).

**Independent Test**: with the knob on, store a frame carrying `43=Y`/`122=<t>` (t ≠ stored 52), replay it, assert exactly one `43`, one `122`, `value(122)==stored 52` (not t) — after first PROVING the stored frame had 43/122 (contract C-4 honesty). Default-config replay byte-identical to the T003 oracle.

### Tests (write first — must FAIL / pin)

- [ ] T012 [P] [US2] In `tests/session/test_resend_reply_possdup.cpp`, add **Cell 2 (retain dedup)**: `allow_pos_dup=true`; store an app frame already carrying `43=Y` and `122=<t>` with `t != stored 52`; FIRST assert the STORED frame contains `43` AND `122` (honesty, C-4); replay; assert `count_tag(43)==1`, `count_tag(122)==1`, `field_value(122)==stored field_value(52)` (NOT t). Source-read → confirm RED (today duplicates 43/122).
- [ ] T013 [P] [US2] Add **Cell 3 (default byte-identity)**: `allow_pos_dup=false`; store a normal app frame; assert `build_replay_frame` output == the T003 pre-037 oracle, byte-for-byte (INV-4). This must stay GREEN before AND after T014 (non-regression).
- [ ] T014 [US2] Build + run T012 (RED) and T013 (GREEN baseline).

### Implementation

- [ ] T015 [US2] In `src/session/session.cpp` `build_replay_frame` copy loop, widen the skip predicate at `:1652` from `if (tag == 9 || tag == 10) continue;` to `if (tag == 9 || tag == 10 || tag == 43 || tag == 122) continue;`. The `if (tag == 52) orig_sending_time = …` capture (`:1653-1656`) is a SEPARATE `if` that runs during normal iteration (52 is not in the skip set) → leave it untouched; the unconditional append of `43=Y` (`:1659-1665`) + `122=<captured 52>` (`:1666-1671`) is unchanged.
- [ ] T016 [US2] Build + run T012 → GREEN; re-run T013 → still GREEN (default path byte-identical). Re-run T004/Cell 1 → still GREEN (no cross-impact).

**Checkpoint**: retain-case dedup shipped + witnessed; default path proven byte-identical.

---

## Phase 5: Polish & Cross-Cutting Concerns

- [ ] T017 [P] Add the conformance/limitation rows to `spec/behaviors-and-limitations.md`: (a) resend GapFill now carries `43=Y`/`122` (B-037-1); (b) replay dedups `43`/`122` under `allow_pos_dup` (B-037-2 or an L-row); (c) the header-after-body field-placement limitation (research D-3) — `43`/`122` appended after body fields, QuickFIX-interoperable not strict-header-order canonical (L-037-1).
- [ ] T018 [P] Add the traceability rows to `spec/feature-catalogue.md` + `spec/coverage-index.md` for 037 (amends the emitted-bytes disposition of S-005/S-006/S-010; cite `[FIX-SL §4.8.2/4.8.4/4.8.5]`).
- [ ] T019 [P] Dated notes (no history rewrite): `specs/013-*/` — 013 FR-010 GapFill now stamps 43/122 on the resend reply (emitted-bytes, stored bytes untouched); `specs/022-*/` — `build_replay_frame` now dedups stored 43/122 under `allow_pos_dup` (refines the INV-5 structural-independence note).
- [ ] T020 Run the changed-line coverage check: the two `append_raw` blocks (GapFill) + the widened skip predicate (replay) at 100% DA/BRDA; full ASan + UBSan over the session + admin-message + recovery-outbound suites (regression). No new alloc gate needed — the `Writer` is `null_memory_resource`-backed (`[const §XV.1]` by construction).
- [ ] T021 `codegraph sync` (incremental) from the submodule after the two production edits land; `codegraph status` to confirm a non-zero index.
- [ ] T022 Full local Tier-1 build + ctest over the touched suites (clang debug) + the 6-preset spot per the build-resource cap (one preset at a time, `-j2`); confirm git tree clean (the codegen-build-graph cleanliness gate, [[feedback_codegen_build_graph_cleanliness_gate]]). **FR-007 surface gate:** assert `git diff include/` shows ONLY doc-comment changes (no signature/type/error-slot/config change) — the no-new-public-surface guarantee.

**Checkpoint**: feature complete — `/speckit-verify` is the next pipeline step (Tier-1 mirror), then Gate B.

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: no deps. T001 blocks all cells; T003 oracle blocks T013.
- **Foundational (Phase 2)**: none.
- **US1 (Phase 3)**: depends on Phase 1. Tests (T004-T005) before impl (T006-T008); golden/live (T009-T011) after T008. MVP.
- **US2 (Phase 4)**: depends on Phase 1; **independent of US1** (different production file — `session.cpp` vs `admin_messages.cpp`). Can run in parallel with US1, but T016 re-runs Cell 1 to confirm no cross-impact.
- **Polish (Phase 5)**: after both builder edits + cells GREEN.

### Within Each User Story

- Test cells (write-first, must FAIL) → builder edit → re-run GREEN. Source-read every RED body before trusting the fail.

### Parallel Opportunities

- T002/T003 (`[P]`), T004 vs T012/T013 (distinct cells, `[P]`), the two builder edits T006 vs T015 (different files, `[P]`), and the doc rows T017/T018/T019 (`[P]`, distinct files).

---

## Implementation Strategy

### MVP (US1 — the default-path conformance fix)

1. Phase 1 Setup (T001-T003).
2. US1 RED cell (T004-T005) → GapFill builder edit (T006-T008) → GREEN → golden profile switch + re-bake (T009-T010) → QFJ live (T011).
3. **STOP and VALIDATE**: every emitted GapFill carries `43=Y`/`122==52`; peers accept it — the headline fix.

### Incremental

1. MVP US1 → US2 (retain-case dedup, independent file) → Polish.
2. `/speckit-verify` → Gate B.

---

## Notes

- `[P]` = different file / distinct cell, no dependency.
- `session.cpp` / `admin_messages.cpp` line anchors are branch-HEAD; re-anchor by surrounding context as edits land (research.md is the authoritative inventory).
- The `122` value is the GapFill's OWN `52` (the `sending_time` param) — NOT a stored original (a GapFill spans a range; research D-1). For the replayed app frame, `122` is the stored `52` (research D-4), NOT a caller-supplied `122`.
- SC-004 QFcpp live arm is waived (L-021-3); do not claim QFcpp live GapFill coverage — unit cell + golden carry the QFcpp byte conformance.
- No new public signature / error slot / config / codegen / C-ABI (FR-007) — confirm via `git diff` of headers (doc-comment only).
