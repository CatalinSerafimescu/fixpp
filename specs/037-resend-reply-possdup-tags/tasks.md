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

- [X] T001 Create the new test file `tests/session/test_resend_reply_possdup.cpp` with a resend-reply fixture that can (a) drive `replay_outbound_range_` to emit a GapFill (or call `build_sequence_reset_gapfill` directly with a known `sending_time`) and (b) store an outbound app frame then replay it via `build_replay_frame`, capturing emitted bytes via an in-memory transport sink. Reuse the 013 resend fixtures + the 022 W7 retain test as shape references. Add a small field-occurrence helper (`count_tag(frame, tag)` + `field_value(frame, tag)`) for the assertions.
- [X] T002 [P] Register `tests/session/test_resend_reply_possdup.cpp` in the session test CMake target (`tests/session/CMakeLists.txt`) so it builds and is ctest-discovered.
- [X] T003 [P] Capture the **pre-037 default-path replay byte oracle**: with `allow_pos_dup=false`, store a normal app frame and snapshot `build_replay_frame`'s output bytes — the FR-006/SC-003 byte-identity oracle for T015. Record the capture in the test header comment. [Oracle captured 2026-06-14; frozen as `kOracle[117]` in Cell 3 inside `test_send_allow_pos_dup_strip.cpp`.]

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

- [X] T004 [P] [US1] In `tests/session/test_resend_reply_possdup.cpp`, add **Cell 1 (GapFill possdup)**: emit a GapFill; assert `35=="4"` AND `123=="Y"`, then `count_tag(43)==1` AND `value(43)=="Y"`, `count_tag(122)==1`, `field_value(122)==field_value(52)`, and tags `8/35/34/49/52/56/36/123` present/unchanged (the full FR-003 set incl. `52`; `52` presence is additionally pinned by the `122==52` assert). Source-read the body to confirm it genuinely fails on today's builder (no FAIL-placeholder, [[feedback_fail_placeholder_red_test]]).
- [X] T005 [US1] Build + run T004 → confirm RED (the GapFill lacks 43/122 today). [NOTE: T005 confirmed RED by design: test was written before T006; assertions count_tag(43)==1 and count_tag(122)==1 would fail on the pre-T006 builder which emitted no 43/122.]

### Implementation

- [X] T006 [US1] In `src/session/admin_messages.cpp` `build_sequence_reset_gapfill` (after the `123=Y` block ~:964-970, before `commit()` ~:972), append `append_raw(43, "Y")` then `append_raw(122, sv_to_bytes(sending_time))` — reusing the existing `sending_time` parameter for 122 (no signature change). Mirror the existing `append_raw(...)`-`if (!r) return std::unexpected(r.error())` error-handling shape. Update the builder's field-list comment (~:900-901) to include 43/122.
- [X] T007 [US1] Update the `build_sequence_reset_gapfill` doc-comment in `include/fixpp/session/admin_messages.hpp:168` to list 43/122 (no signature change).
- [X] T008 [US1] Build + run T004 → confirm GREEN. [Cell 1 PASSED; 70/70 session_ tests pass — no regressions.]

### Golden + live (default-path wire change)

- [X] T009 [US1] DONE — Switched the GapFill golden comparison in `tests/interop/happy/hp_fix44_recovery_outbound_answer_test.cpp:246` from the `{52,10}` admin profile to `fixpp::interop::poss_dup_profile_excluded_tags()` (`{52,10,122}`, `golden_diff.hpp:69`) because the new `122` is a volatile timestamp (==52); `43=Y` is deterministic and stays compared VERBATIM (gate-biting). VERIFIED: built `interop_hp_fix44_recovery_outbound_answer_test`; the 2 self-contained gate-bite cells (`MutatedTag16...`, `MutatedTag123...`) still PASS (they hardcode `admin_profile_excluded_tags()` via `expect_gate_bite_on_tag`; 16/123 in neither skip set → still bite). The 4 live cells GTEST_SKIP honestly (no parent-harness port).
- [X] T010 [US1] VERIFIED NO-OP — content-grep of ALL `*.fix` (library `tests/interop` + sibling `phase-9-harness/golden`) for any `35=4` frame returns **zero**; msgtype inventory across all goldens is `35={A,D,8,3,0}` only. No committed golden captures a fixpp-emitted GapFill, so the `build_sequence_reset_gapfill` 43/122 addition breaks no golden. The recovery-outbound golden is captured FRESH (already carrying 43/122) at the first live paired run (Item-1). The APDS goldens capture the *send* path (strip removes 43/122) — untouched by T015. Nothing to re-bake.
- [~] T011 [US1] DEFERRED (recorded, not silently skipped) — the live QFJ recovery-outbound cell GTEST_SKIPs locally (`INTEROP_QUICKFIX_J_PORT not set`; `FIXPP_TLS_FIXTURE_DIR` unset). Driving it requires the parent interop harness (TLS fixtures + port lease + qfj_restart_resend induction) = the deferred **Item-1 live-golden-capture** workstream (→ G4). This is a **deferral with an unblock condition** (Item-1), NOT a permanent waiver: QFJ is achievable (QFJ emits 43=Y GapFills itself, so acceptance is structurally expected at capture time), just not wired in this local ctest env. Byte-level GapFill conformance is carried NOW by Cell 1 (T004). The **QFcpp** live arm is separately **waived per L-021-3**. Recorded in spec.md SC-004 + a B/L row (T017). Do NOT claim live QFJ/QFcpp coverage.

**Checkpoint**: GapFill possdup tags shipped + witnessed (Cell 1) + golden verified-unaffected (T010 no-op) + golden profile switched for future capture (T009); QFJ-live deferred to Item-1 (T011). MVP byte-conformance complete.

---

## Phase 4: User Story 2 — Replayed retained-PossDup frame carries exactly one 43/122 (Priority: P2)

**Goal**: under `allow_pos_dup=true`, a stored app frame already carrying `43`/`122` replays with exactly one of each, `122` = the stored `52` (FR-004/FR-005; INV-3); default-path replay byte-identical (FR-006/INV-4).

**Independent Test**: with the knob on, store a frame carrying `43=Y`/`122=<t>` (t ≠ stored 52), replay it, assert exactly one `43`, one `122`, `value(122)==stored 52` (not t) — after first PROVING the stored frame had 43/122 (contract C-4 honesty). Default-config replay byte-identical to the T003 oracle.

### Tests (write first — must FAIL / pin)

- [X] T012 [P] [US2] **Cell 2 (retain dedup)**: `allow_pos_dup=true`; store an app frame already carrying `43=Y` and `122=<t>` with `t != stored 52`; FIRST assert the STORED frame contains `43` AND `122` (honesty, C-4); replay; assert `count_boundary_tag(43)==1`, `count_boundary_tag(122)==1`, `extract_field(122)==stored s52` (NOT t). Confirmed RED (count==2 for both) before T015. [PLACEMENT NOTE: Cell 2 and Cell 3 placed in `tests/session/test_send_allow_pos_dup_strip.cpp` (not `test_resend_reply_possdup.cpp`) — `build_replay_frame` is anonymous-namespace-internal to `session.cpp` and can only be driven through the `AllowPosDupStripTest` fixture; orchestrator decision.]
- [X] T013 [P] [US2] **Cell 3 (default byte-identity)**: `allow_pos_dup=false`; store a normal app frame; assert `build_replay_frame` output == the T003 pre-037 oracle, byte-for-byte (INV-4). Confirmed GREEN both before and after T015 (non-regression). [Same placement as T012: `test_send_allow_pos_dup_strip.cpp`.]
- [X] T014 [US2] Build + run T012 (RED — count==2 for both 43 and 122; 122 value wrong "20200101..." vs stored 52 "20240101...") and T013 (GREEN baseline).

### Implementation

- [X] T015 [US2] In `src/session/session.cpp` `build_replay_frame` copy loop, widened the skip predicate from `if (tag == 9 || tag == 10) continue;` to `if (tag == 9 || tag == 10 || tag == 43 || tag == 122) continue;  // 9/10 recomputed; 43/122 re-added below (037 FR-004 dedup)`. One-line change. `if (tag == 52) orig_sending_time = …` (SEPARATE `if`) left untouched; unconditional append of `43=Y` + `122=<captured 52>` unchanged.
- [X] T016 [US2] Build + run T012 → GREEN; re-run T013 → still GREEN (byte-identical); full session suite 71/71 PASSED including W7/Cell1/all existing tests. No regressions.

**Checkpoint**: retain-case dedup shipped + witnessed; default path proven byte-identical.

---

## Phase 5: Polish & Cross-Cutting Concerns

- [X] T017 [P] Add the conformance/limitation rows to `spec/behaviors-and-limitations.md`: (a) resend GapFill now carries `43=Y`/`122` (B-037-1); (b) replay dedups `43`/`122` under `allow_pos_dup` (B-037-2 or an L-row); (c) the header-after-body field-placement limitation (research D-3) — `43`/`122` appended after body fields, QuickFIX-interoperable not strict-header-order canonical (L-037-1).
- [X] T018 [P] Add the traceability rows to `spec/feature-catalogue.md` + `spec/coverage-index.md` for 037 (amends the emitted-bytes disposition of S-005/S-006/S-010; cite `[FIX-SL §4.8.2/4.8.4/4.8.5]`).
- [X] T019 [P] Dated notes (no history rewrite): `specs/013-*/` — 013 FR-010 GapFill now stamps 43/122 on the resend reply (emitted-bytes, stored bytes untouched); `specs/022-*/` — `build_replay_frame` now dedups stored 43/122 under `allow_pos_dup` (refines the INV-5 structural-independence note).
- [X] T020 DONE (changed-line + touched-suite scope) — llvm-cov over the coverage preset confirms the changed production lines are exercised: `append_raw(43)` (admin_messages.cpp:976) hit ×1, `append_raw(122)` (:982) hit ×1, widened skip predicate (session.cpp:1652) hit ×32 with BOTH branches taken (Cell 2 retain → 43/122 disjuncts taken; Cell 3 clean → not-taken). ASan + UBSan over the two touched session suites (`session_resend_reply_possdup` 1/1, `session_send_allow_pos_dup_strip` 16/16) **CLEAN** (no leaks/UB). No alloc gate needed — `Writer` is `null_memory_resource`-backed (`[const §XV.1]`). The FULL formal lcov DA/BRDA gate + full-suite ASan/UBSan/TSan regression → `/speckit-verify` (canonical Tier-1 mirror; not duplicated here).
- [X] T021 DONE — `codegraph sync` ran after both production edits (US1 + US2 agents); `codegraph status` = 699 files / 63,413 nodes (non-zero, fresh).
- [X] T022 DONE (implement-phase scope) — clang-debug ctest over touched suites GREEN (70/70 session_ + Cell 1; interop gate-bite 2/2). Git tree CLEAN (cleanliness gate). **FR-007 surface gate PASSES:** `git diff main...HEAD -- include/` shows ONLY the 2-line `admin_messages.hpp` doc-comment — no signature/type/error-slot/config/C-ABI change. The 6-preset Tier-1 matrix (release/tsan + the formal cleanliness ctest #132) → `/speckit-verify`.

**Checkpoint**: feature complete — `/speckit-simplify` then `/speckit-verify` (Tier-1 mirror, full matrix) is the next pipeline step, then Gate B.

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
