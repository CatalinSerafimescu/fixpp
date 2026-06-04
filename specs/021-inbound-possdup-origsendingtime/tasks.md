---
description: "Task list for 021-inbound-possdup-origsendingtime"
---

# Tasks: Inbound PossDup / OrigSendingTime Handling (S-010 PossDup half / S-033)

**Input**: Design documents from `specs/021-inbound-possdup-origsendingtime/`
**Prerequisites**: plan.md, spec.md (US1/US2 in scope; US3/FR-008 DEFERRED), research.md (D1..D7), data-model.md (two-stage decision flow + INV-1..5), contracts/session-possdup.md, quickstart.md

**Tests**: REQUIRED — TDD is `[const §VII]` NON-NEGOTIABLE. Every arm lands RED-first.

**Scope**: INBOUND-ONLY. The fatal inbound too-low arm (`src/session/session.cpp:1849-1863`) becomes PossDup-aware. FR-008 / User Story 3 (AllowPossDup send knob) is DE-SCOPED (Gate A round 1) — no tasks here. No new module, no new public C surface, no new `error::` slot, no persistent dedup store.

## Format: `[ID] [P?] [Story] Description`
- **[P]**: parallelizable (different file, no incomplete-task dependency)
- **[Story]**: US1 / US2 (Setup/Foundational/Polish carry no story label)

---

## Phase 1: Setup (baseline re-verification)

**Purpose**: confirm the research.md/plan.md code anchors on the branch base before coding; record any drift as a bundle defect.

- [X] T001 Re-verify the bundle's code-anchor claims against the current branch base; record any drift: (a) the inbound too-low arm `src/session/session.cpp:1849-1863` emits **no Logout wire frame** — only `record_state_transition_(fsm_state::Disconnected)` (FR-003 / Arm B); (b) `scan_frame_header`/`FrameHeader` (~`session.cpp:1029-1115`) capture tags `43` + `52` but **NOT** `122`; (c) `build_reject` (`src/session/admin_messages.cpp:540`, body ~`:605-636`) emits `45=RefSeqNum`, `371=RefTagID`, `372=RefMsgType`, `373=SessionRejectReason` (NOT `380`); (d) the existing SendingTime-accuracy reject (~`session.cpp:1685-1737`) emits `Reject(373=10)` → `Logout` → `Disconnected` and references `RefTagID=52` — the pattern Arm D reuses; (e) `SessionConfig` lives in `include/fixpp/session/session_config.hpp` with the `reconnect_policy` additive-knob pattern; (f) the inbound `SequenceReset(35=4)` dispatch order relative to the seqnum gate (Reset-mode before the gate; GapFill after, ~`session.cpp:1880`) — Arm E exemption must sit consistently with it; (g) the 019/020 inbound `fromApp` delivery site reused by the Arm-A redeliver path; (h) **no new `error::` slot is required** (Arms C/D use `SessionRejectReason` values via `build_reject`, not engine error slots).

---

## Phase 2: Foundational (blocking prerequisites)

**Purpose**: the two additive surfaces both user stories depend on. Makes US1/US2 tests compile-and-fail RED.

- [X] T002 [P] Add `orig_sending_time` (tag 122) capture to the inbound header: add `std::string_view orig_sending_time;` to `FrameHeader` and `case 122: h.orig_sending_time = val; break;` to `scan_frame_header` in `src/session/session.cpp` (additive; mirrors the existing `case 43/52`). (data-model §3.)
- [X] T003 [P] Add `bool redeliver_poss_dup = false;` to `fixpp::session::SessionConfig` in `include/fixpp/session/session_config.hpp` (additive, default-valued; mirrors the `reconnect_policy` knob; public header — additive only, no ABI break). (FR-010; data-model §2.)

---

## Phase 3: User Story 1 — Tolerate replayed possible-duplicate messages (Priority: P1) 🎯 MVP

**Goal**: a too-low inbound message with `PossDupFlag(43)=Y` and a valid `OrigSendingTime(122)` is tolerated (no disconnect, no seqnum advance); admin dups ignored, app dups dropped (default) or redelivered (knob). Arm B (no-PossDup too-low) preserved byte-identical.

**Independent test**: feed a too-low `43=Y` frame with valid `122`; assert `Active`, expected seqnum unchanged, no Logout, no re-apply; feed a too-low frame without `43=Y`; assert `→Disconnected` with no Logout frame.

### Tests for User Story 1 (write FIRST, confirm FAIL) ⚠️

- [X] T004 [P] [US1] `tests/session/test_inbound_poss_dup_tolerance.cpp` (RED): **Arm A admin-ignore** — too-low `43=Y` + valid `122` admin frame → session stays `Active`, expected inbound seqnum unchanged (**INV-1**), no Logout/Reject emitted; **Arm A app-drop (default)** — too-low `43=Y` app frame, `redeliver_poss_dup=false` → no `Application::fromApp` call, no advance; **Arm A app-redeliver (knob)** — same frame, `redeliver_poss_dup=true` → exactly one `fromApp` call flagged possible-duplicate, still no advance, still `Active`; **AS2 idempotent** — an already-applied possdup admin frame replayed → side effects not re-applied; **Arm B regression pin (INV-2)** — too-low frame **without** `43=Y` → `→Disconnected` and assert **NO Logout wire frame** is emitted (byte-identical to `session.cpp:1860-1862`); **no-heap witness (INV-5)** — wrap the disposition in a `counting_resource` and assert **zero** heap allocations on the inbound path. (US1 AS1/AS2/AS3; FR-001/002/003/010; INV-1/2/5.)

### Implementation for User Story 1

- [X] T005 [US1] Add the Stage-2 too-low PossDup branch in `src/session/session.cpp`, inserted **inside** the `if (!chk)` too-low block, **after** the Heartbeat(0) silent-ignore exception (`:1856-1859`) and **before** the fatal `→Disconnected` (`:1860-1862`): for a too-low (`MsgSeqNum < expected`) frame with `43=Y` and a valid `122`, classify as a possible duplicate — admin → ignore; application → drop when `cfg_.redeliver_poss_dup == false`, else redeliver via the 019/020 `fromApp` site flagged possible-duplicate; **never advance** `seqnum_mgr_` and **never disconnect** on the tolerated arm. Preserve Arm B exactly (no `43=Y` → existing `→Disconnected`, no Logout). A too-low `43=Y` **Heartbeat(0)** is caught by the HB silent-ignore exception *first* (it precedes the Arm-A branch) — identical observable outcome, and this ordering keeps the HB exception from becoming dead code; Arm A intercepts only non-Heartbeat too-low `43=Y` frames. Makes T004 green. (FR-001/002/003/010; INV-1/2.)

### Live interop for User Story 1

- [X] T006 [US1] Live **replay-survives** interop cell, both engines × both roles (fixpp-init × QF-acc and fixpp-acc × QF-init): drive a real ResendRequest→replay so the counterparty re-sends an already-seen message with `43=Y`; assert the fixpp session stays `Active` and emits no Logout. Extend the 018 admin-interop fixture (`tests/interop/`) + `phase-9-harness/tools/run_interop_cell.py` + `emit_matrix.py` with the cell + golden; **skip cleanly** when no counterparty. (US1; FR-007; SC-001/SC-004.)

---

## Phase 4: User Story 2 — Validate OrigSendingTime on possible-duplicates (Priority: P2)

**Goal**: any inbound `43=Y` non-`SequenceReset` message (incl. at-expected) is validated — missing `122` → `Reject(371=122,373=1)` survive; `122 > 52` strict → `Reject(371=122,373=10)` + Logout + disconnect; `SequenceReset(35=4)` exempt.

**Independent test**: feed `43=Y` with no `122` → Reject `371=122`/`373=1`, session survives; feed `122 > 52` → Reject `373=10` + Logout + disconnect; both at too-low AND at-expected seqnum.

### Tests for User Story 2 (write FIRST, confirm FAIL) ⚠️

- [X] T007 [P] [US2] `tests/session/test_inbound_poss_dup_validation.cpp` (RED): **Arm C** — `43=Y` non-`35=4`, missing `122` → `Reject(35=3)` `371=122`, `373=1` (RequiredTagMissing), session `Active`; **Arm D** — `43=Y`, `122 > 52` strict → `Reject(35=3)` `371=122`, `373=10` (SendingTimeAccuracyProblem) + `Logout` + `→Disconnected`; **boundary** — `122 == 52` is accepted (NOT Arm D); **Arm E** — `SequenceReset(35=4)` + `43=Y` with no `122` → **not** rejected (routes to the existing reset/gap-fill path); **at-expected AS4** — `34 == expected`, `43=Y`, no `122` → still Arm C (validation seqnum-independent), AND assert the expected inbound seqnum is **NOT advanced** (verify-returns-false, matches QFJ `Session.java:1843`); **at-expected AS5** — `34 == expected`, `43=Y`, `122 > 52` → still Arm D. (US2 AS1–AS5; FR-004/005/006.)

### Implementation for User Story 2

- [X] T008 [US2] Add the Stage-1 PossDup validation in `src/session/session.cpp`, placed **before** the seqnum gate and the Stage-2 tolerance branch (T005), and exempting `35=4`: for **any** inbound `43=Y` non-`SequenceReset` frame (any seqnum, incl. at-expected) — if `122` absent → emit `Reject(35=3)` via `build_reject` with `371=122`, `373=1`, session survives (Arm C); if parsed `122 > 52` strict → emit `Reject(35=3)` `371=122`, `373=10` then `Logout` + `→Disconnected` reusing the existing §1685-1737 accuracy-reject emit pattern (Arm D); `122 == 52` passes. Parse `122`/`52` via the existing SendingTime time machinery. Ensure ordering: Stage-1 validation precedes Stage-2 tolerance and does not intercept `35=4`. Makes T007 green. (FR-004/005/006.)

### Live interop for User Story 2

- [X] T009 [US2] Live **malformed-duplicate** interop cell, both engines × both roles: feed a `43=Y` message missing `122`; assert a session-level `Reject(35=3)` crosses the wire and the session survives, matching QuickFIX-cpp v1.16.0 + QuickFIX-J 3.0.1 (run QFJ with its default `RequiresOrigSendingTime=Y`). Extend the interop fixture + `run_interop_cell.py`/`emit_matrix.py` + golden; skip-without-counterparty. (US2; FR-007; SC-002/SC-004.)

---

## Phase 5: Polish & Cross-Cutting Concerns

**Purpose**: §VI catalogue/coverage closure (applied here, before merge), B-/L- entries, the feature-completeness gate, and unfiltered-suite-green discipline (pre-`/simplify` / `/speckit-verify`).

- [X] T010 Catalogue + coverage + B/L bookkeeping (`[const §VI]`; [[feedback_feature_completeness_gate]]): in `spec/feature-catalogue.md` — **S-033** (`OrigSendingTime(122)` required on PossDup=Y, `:345`) `backlog → done` citing `021` (inbound OrigSendingTime-required enforcement, Arms C/D); **S-010** (`PossDupFlag(43)+PossResend(97)`, `:30`) **stays `backlog`** with a 021 partial-delivery note (*"PossDupFlag(43) inbound session duplicate semantics delivered by 021; PossResend(97) application-resend semantics deferred"*, mirroring 020's A-001/A-006). In `spec/coverage-index.md` mirror the same (S-033 done; S-010 partial; `NextExpectedMsgSeqNum(789)`, remaining G3 knobs, and the deferred AllowPossDup send knob recorded as still-deferred). In `spec/behaviors-and-limitations.md` add **B-021-1** (inbound PossDup tolerance: too-low+`43=Y` survives, no seqnum advance; Arms C/D reject reasons `373=1`/`373=10`, `371=122`), **L-021-1** (app duplicate default-drop vs opt-in redeliver via `redeliver_poss_dup`; admin always ignored — distinguish from `[const §XV.15]` backpressure drop: a protocol duplicate already processed once, NOT a queue drop), **L-021-2** (FR-008 AllowPossDup send knob DEFERRED — opaque-send hardening). Add the AllowPossDup-send-knob + `PossResend(97)` deferrals to the deferred-work registry. Confirm the error taxonomy is **unchanged** (no new slot).
- [X] T011 Feature-completeness audit ([[feedback_feature_completeness_gate]]; `[const §XVII.8]` precondition for `/gate-b`): FR-001..FR-010 (FR-007 live-gated; FR-008 DEFERRED-recorded) ↔ task ↔ test; SC-001..SC-004 ↔ test (SC-004 live-cell-gated); **named-test map** — Arm A admin-ignore/app-drop/app-redeliver + Arm B + INV-1/2/5 ↔ T004; Arms C/D/E + at-expected AS4/AS5 ↔ T007; **SC-003 ↔ T004 (Arm-B regression pin) + the unfiltered ctest (T012)**; live SC-001/002/004 ↔ T006/T009; *no-new-concurrency* is structural (Arm-A redeliver reuses 019/020's single-thread-confined inbound `fromApp` site — L-019-3, no standalone test); **confirm the `error::` enumerator set is unchanged** (exact-set, per [[feedback_completeness_gate_exact_set_not_subset]] — here the expectation is *no delta*). 100% or explicit waiver-with-rationale → record at `.specify/decisions/021-inbound-possdup-origsendingtime-completeness.md` (gitignored). Depends T010.
- [X] T012 Unfiltered Tier-1 green discipline before `/simplify`/`/speckit-verify`: build + run the full ctest **UNFILTERED** (or `-L sync` for the awaitable corpus per [[feedback_awaitable_header_mutex_include_edge]] — the new `SessionConfig` field + `session.cpp` edits must not drag a mutex into `session.hpp`'s awaitable closure) with a clean `git status` (the codegen-build-graph-cleanliness gate, [[feedback_codegen_build_graph_cleanliness_gate]]); `-j2` cap, sanitizer presets one-at-a-time ([[feedback_build_resource_cap_oom]]). Record green.
- [X] T013 Fuzz-corpus freshness for the parser-touching change (`[const §VII.7]` — `scan_frame_header` gains a `case 122`; new inbound disposition arms): confirm `tests/fuzz/fuzz_session_recovery_admin_parse.cpp` (which feeds `on_inbound_frame` → `scan_frame_header`) reaches the new arms, and add seed-corpus entries under `tests/fuzz/corpus/` exercising `43=Y`+missing-`122`, `43=Y`+`122>52`, and `43=Y`+`35=4` (Arm-E exempt) inputs so the ≥10-minute libFuzzer PR run covers Arms C/D/E. No new harness is required (the existing one covers the inbound frame path — not a Gate-B blocker); record harness adequacy in T011's completeness record. (`[const §VII.7]`.)

---

## Dependencies & Execution Order

### Phase dependencies
- **Setup (Phase 1)** → **Foundational (Phase 2)** → **US1 (Phase 3)** → **US2 (Phase 4)** → **Polish (Phase 5)**.
- US1 is the MVP (replay-survives tolerance). US2 adds wire-conformant validation of malformed/late duplicates.

### Within / across stories (the one subtlety)
- Tests (RED) → implementation (green): T004 RED gates T005; T007 RED gates T008.
- **Execution-order vs priority-order:** in the running engine the data-model's **Stage-1 validation (T008/US2) executes BEFORE the Stage-2 tolerance (T005/US1)** and both sit in the same `session.cpp` dispatch region. Tasks are still authored US1→US2 by priority; T008 **inserts the validation ahead of** the T005 branch (and ahead of the seqnum gate, exempting `35=4`). After T008, re-run T004 to confirm the US1 tolerance arms still pass with validation in front (no regression).
- T002 ∥ T003 (different files). T005 and T008 touch the same `session.cpp` region → **sequential** (T005 then T008).
- T006 (US1 interop) needs T005; T009 (US2 interop) needs T008.
- T010 → T011 (completeness depends on catalogue/B-L); T013 (fuzz corpus) after T008 (the arms exist), feeds T011's harness-adequacy record; T012 last (whole-tree green).

### Parallel opportunities
- T002 [P] ∥ T003 [P] (Foundational, different files).
- T004 [P] (US1 tests) and T007 [P] (US2 tests) are different files — both authorable once Foundational lands, but keep RED→green per story.

## Implementation Strategy
- **MVP = Phase 1–3 (US1)**: inbound too-low PossDup tolerance + Arm B preservation → the core interop value (a counterparty replay no longer kills the session). Independently shippable/testable.
- **Increment = Phase 4 (US2)**: OrigSendingTime validation (Arms C/D/E + at-expected) → wire-conformant rejection of malformed duplicates.
- **Close = Phase 5**: §VI catalogue/coverage (S-033 done, S-010 partial), B-/L-, completeness gate (the `/gate-b` precondition), unfiltered green.
- FR-008 / US3 (AllowPossDup send knob) is **out of scope** (deferred to a future opaque-send-hardening slice).
