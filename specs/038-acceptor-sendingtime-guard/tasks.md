---
description: "Task list — 038-acceptor-sendingtime-guard"
---

# Tasks: Acceptor inbound-Logon SendingTime guard + session/reconnect hardening riders

**Input**: Design documents from `specs/038-acceptor-sendingtime-guard/` (spec.md, plan.md, research.md, data-model.md, contracts/, quickstart.md)
**Prerequisites**: Gate A CONVERGED 2026-06-15 (3 rounds, 2 rewrites; gate-a-done).
**Tests**: REQUIRED — TDD is mandatory per `[const §VII.3]` (red-green-refactor). The witness cells are specified in quickstart.md.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: parallelizable (different file, no dependency on an incomplete task).
- **[Story]**: US1 = Group 1 (P1, SendingTime guard); US2 = Group 2 (P2, callback-seam); US3 = Group 3 (P3, 1137 witness).

## Path Conventions

Single library (`fixpp`). Production: `src/session/`. Tests: `tests/session/`. Docs: `spec/`. All paths relative to the submodule root `research/G19-fix-fpml-iso20022/library/`.

---

## Phase 1: Setup

**Purpose**: scaffold the one new test file; the project build/toolchain already exists.

- [X] T001 [P] Create `tests/session/test_acceptor_logon_sending_time.cpp` (the Group-1 witness file) with a mock-clock acceptor `Session` fixture — reuse the established-Q3 fixture shape from `tests/session/test_sending_time_precision.cpp` / `sending_time_test.cpp` (inject `fixpp::core::mock_clock` from `include/fixpp/core/test/mock_clock.hpp` as `effective_clock_`; default `cfg_.sending_time_threshold = 120 s`). Register the ctest target `session_acceptor_logon_sending_time` in `tests/session/CMakeLists.txt`.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: none. The three groups are independent (no shared blocking prerequisite). Group-1's fixture migration is a US1 task (T008), not foundational. Proceed straight to the user-story phases.

---

## Phase 3: User Story 1 — Acceptor first-Logon SendingTime(52) guard (Priority: P1) 🎯 MVP

**Goal**: the acceptor rejects an establishing Logon whose `52` is absent/empty, malformed, or stale (|52−now| > MaxLatency) with `Reject(35=3, 371=52, 373=10)` + `Disconnected` (NO Logout), without advancing inbound sequence state, while the conforming path establishes byte-identically. FR-001..005; INV-1..6; SC-001..003.

**Independent Test**: drive an acceptor over `mock_transport` with a controllable clock; a stale-`52` first Logon → `Disconnected` + on-wire `Reject(373=10, 371=52)` + no Logout + inbound next-expected unchanged; a within-window `52` → establishes as today. No other group required.

### Tests (write first — must FAIL)

- [X] T002 [P] [US1] In `tests/session/test_acceptor_logon_sending_time.cpp`, add the **reject cells (quickstart 1–5)**: stale-past, stale-future, malformed-`52`, absent-`52`, empty-`52=` (distinct parse path). Each asserts: session → `Disconnected`; on-wire `Reject(35=3, 371=52, 373=10)`; **NO Logout frame**; `toAdmin` observed the Reject; next-expected-inbound unchanged (store-type-aware: memory → `seqnum_min`; the persistent-store branch of this invariant — seeded `D` vs `seqnum_min` under the `withhold_inbound` corner — is witnessed by T003/cell-9 + the data-model INV-4 discriminant). Source-read each body to confirm it genuinely fails on today's no-guard code (no FAIL-placeholder, `[[feedback_fail_placeholder_red_test]]`).
- [X] T003 [P] [US1] Add the **defensive/error cells (quickstart 6–9)**: (6) `toAdmin` throws on the new Reject → `app_callback_threw` + `Disconnected` (assert directly, not via a sibling proxy — `[[feedback_witness_asserts_named_postcondition_not_proxy]]`); (7) `assign_outbound` failure on the Reject → fail-closed; (8) `build_reject` build-failure → FAIL CLOSED `Disconnected` (buffer pinned ≥512B; near-cap RED witness if a CompID-length path approaches the cap — `[[feedback_fixed_buffer_build_failure_silent_success]]`); (9) persistent-store → durable inbound counter unchanged across the rejected Logon.
- [X] T004 [P] [US1] Add the **simultaneous-bad + no-regression + persistent-reconnect cells (quickstart 10–14)**: (10) bad-`52` + bad-`1137` → `52` reject wins (no `371=1137` on wire); (11) bad-`52` + bad-credentials → `52` reject wins (public `Reject 371=52`, not the silent credential disconnect); (12) bad-`52` + too-low-seqnum → `52` reject wins (guard precedes `check_inbound`); (13) **conforming** (`52=now`) → establishes, emitted output byte-identical to a pre-feature acceptor establishment; (14) **persistent-store reconnect, durable outbound `N>1`** → the Reject carries `34=N` (hydrated), the store accepts it (no `store_seqnum_out_of_order`), durable inbound unchanged.
- [X] T005 [US1] Build + run T002–T004 (`session_acceptor_logon_sending_time`) → confirm **RED** for the reject/simultaneous/persistent cells (no guard today) and GREEN for the conforming cell 13 (baseline). Record the RED evidence (which assertions fail) per cell.

### Implementation

- [X] T006 [US1] In `src/session/session.cpp`, acceptor `NotConnected` first-Logon arm: insert the SendingTime guard **immediately after the `ensure_hydrated_` block (~:1925) and BEFORE the `reset_on_logon` block (~:1927) / `check_inbound` (~:1936)**. Extract `hdr.sending_time` via `scan_frame_header(frame)`; gate on `effective_clock_`; `ok := !empty AND fix_string_to_utc_time(st) succeeds AND check_sending_time(parsed, effective_clock_->now(), threshold)` where `threshold = cfg_.sending_time_threshold ? : 120 s`. If `!ok`: `build_reject` into a `std::array<std::byte,512>` (build-fail → `record_state_transition_(Disconnected)` + fail-closed return, never silent-success) with `RefSeqNum`=Logon `34`, `RefTagID(371)=52`, `RefMsgType(372)="A"`, `SessionRejectReason(373)=10` → `assign_outbound()` → `fire_to_admin_` (throw → `app_callback_threw` + `Disconnected`) → `store_then_emit` (I-07 logged-then-proceed) → `record_state_transition_(Disconnected)` → `co_return`. **NO Logout**; **NO `persist_inbound_advance_`**. Scaffold the block on the in-arm `1137` reject (`:2102-2136`); leave the `1137` reject and established-Q3 untouched.
- [X] T007 [US1] Build + run T002–T004 → confirm **GREEN** (all reject/simultaneous/persistent cells pass; cell 13 still byte-identical).
- [X] T008 [US1] **Fixture audit + migrate** (the bounded blast radius, research D-9): grep the acceptor-establishment suites for how each inbound first-Logon stamps `52` relative to its session/mock clock; migrate any fixture whose `52` is now > MaxLatency from the clock to a controllable/current `52` (expected churn, NOT a regression). Re-run the full `session_*` suite → GREEN. Record the count of fixtures migrated.

**Checkpoint**: acceptor first-Logon SendingTime guard shipped + witnessed (cells 1–14); no-Logout + no-inbound-advance + hydrated-outbound-N proven; existing acceptor fixtures migrated. MVP complete.

---

## Phase 4: User Story 2 — Reconnect credentials-rotated callback-seam hardening (Priority: P2)

**Goal**: a throwing `credentials_rotated` callback does not abandon the in-flight reconnect attempt; the attempt proceeds and the rotation baseline still updates (no spurious re-emit). FR-006/FR-007; INV-7/8/9. SC-free.

**Independent Test**: via the 014 standalone-FSM injection path, inject a `credentials_rotated` callback that throws during a rotated reconnect attempt → the throw is contained, the attempt reaches its policy outcome, `last_active_fp_` is updated.

### Tests (write first — must FAIL)

- [X] T009 [P] [US2] In `tests/session/test_credentials_rotated_emit.cpp` (EXTEND), add: (a) **throwing-callback cell** — inject a `credentials_rotated` callback that throws; drive a reconnect attempt with `rotated == true` (`snap != last_active_source_`); assert the throw does NOT propagate out of `drive_reconnect_attempt`, the attempt proceeds to its policy outcome (reaches `make()`), and `last_active_fp_` was updated (a second attempt does NOT re-emit the same rotation, INV-8); (b) **transparency cell** — a non-throwing callback → existing 014 behaviour unchanged (INV-9).
- [X] T010 [US2] Build + run T009 → confirm the throwing-callback cell is **RED** today (the bare call lets the throw escape/abandon the attempt); transparency cell GREEN.

### Implementation

- [X] T011 [US2] In `src/session/reconnect_fsm.cpp` (~:200), wrap the single `emit_credentials_rotated_(...)` invocation in `try { … } catch (...) { /* contain — the rotation notification is best-effort */ }` matching the established `authorize_logon` callback-guard shape. The catch must fall through to the baseline update (`last_active_source_` / `last_active_fp_` at ~:207-208) and the remaining attempt steps (`make()` → handshake) — do NOT skip them.
- [X] T012 [US2] Build + run T009 → confirm **GREEN** (throw contained, attempt proceeds, baseline updated, no spurious re-emit; transparency unchanged).

**Checkpoint**: the reconnect callback-seam is exception-contained; honest framing (production callback is `noexcept`-wrapped/unreachable — this hardens the injection seam).

---

## Phase 5: User Story 3 — FIXT DefaultApplVerID(1137) reject witnesses (Priority: P3, test-only)

**Goal**: the existing acceptor `1137` reject path (`[FIX-SL §4.3.7]`/S-025) gains session-level negative witnesses. NO production change (FR-009). INV-10.

**Independent Test**: an acceptor first-Logon with absent / non-conformant `1137` → on-wire `Reject(35=3, 371=1137)` (`373=1` / `373=5`) + `toAdmin` observation + `Disconnected`.

### Tests (characterize existing behaviour — GREEN immediately; the gap was missing witnesses, not missing behaviour)

- [X] T013 [P] [US3] In `tests/session/test_fixt_logon_establishment.cpp` (EXTEND), add negative witnesses: (a) absent `1137` → on-wire `Reject(35=3, 371=1137, 373=1 RequiredTagMissing)` + `toAdmin` observed + `Disconnected` (never `Active`); (b) non-conformant `1137` → `Reject(373=5 ValueIsIncorrect, 371=1137)` + `toAdmin` + `Disconnected`. Genuinely drive the `1137` reject path (not a vacuous/trivial-seed assert — `[[feedback_witness_asserts_named_postcondition_not_proxy]]`). **(c) [US1 Judge-pass carry-over] the genuine 52-vs-1137-GATE ordering cell**: a FIXT session (using `FixtSetup`) fed a bad-`52` + missing/unserviceable-`1137` Logon → `Reject(371=52, 373=10)` wins, NO `371=1137` on wire, `Disconnected`. This is the discriminating ordering witness deferred from US1 Cell 10 (FIX.4.4 can't reach the `1137` gate; the witness belongs here where the gate is reachable + `FixtSetup` exists).
- [X] T014 [US3] Build + run T013 → confirm **GREEN** (witnesses the existing fail-closed behaviour); confirm `git diff -- src/` is empty for this group (FR-009, test-only).

**Checkpoint**: the 1137 reject path carries session-level negative witnesses (closing the 031-analogous gap).

---

## Phase 6: Polish & Cross-Cutting Concerns

- [X] T015 [P] Add rows to `spec/behaviors-and-limitations.md`: **B-038-1** (acceptor first-Logon now enforces SendingTime MaxLatency — parity with the initiator + established paths); **L-038-1** (absent/empty `52` dispositioned as `reason=10`, a documented divergence from QuickFIX `RequiredTagMissing=1`); **L-038-2** (no-Logout pre-establishment reject shape — consistent with the live-proven `1137` reject; a dedicated LIVE bad-`SendingTime` cross-engine interop witness is DEFERRED, L-021-3 / L-037-2 family); **L-038-3** (the Gate-A round-3 note: benign, self-healing `52`-guard-vs-`1137` outbound-seq asymmetry under `reset_on_logon`); a one-line G2 note (ReconnectFsm callback-seam hardening; production path `noexcept`-unreachable).
- [X] T016 [P] Catalogue/coverage-index traceability for 038: amend **S-019** in `spec/feature-catalogue.md` (latency check now covers the acceptor first-Logon path, `[FIX-SL §4.2.3]`) + matching `spec/coverage-index.md` entry. (The `1137`→`[FIX-SL §4.3.7]`/S-025 stale-ledger correction was already applied at Gate A round 2 — note it as done, do not redo.)
- [X] T017 [P] Fold the Gate-A round-3 P3-prose accuracy note into `specs/038-acceptor-sendingtime-guard/data-model.md` INV-4: the "persistent → seeded `D`" case omits the `141=Y` / `reset_on_logon` `withhold_inbound` corner (`session.cpp:1911` → `apply_inbound_seed=false` → `seqnum_min` even on a persistent store); FR-004 "not advanced" holds in either case. **DONE during `/speckit-analyze` remediation (2026-06-15)** — INV-4 now states the discriminant (persistent+no-withhold → `D`; persistent+withhold or memory → `seqnum_min`). Residual for implement: T003/cell-9 must assert that per-config discriminant (cross-referenced from T002).
- [X] T018 [P] Dated notes (no history rewrite): `specs/005-session-establishment-fsm/` — S-019's MaxLatency check now ALSO covers the acceptor first-Logon path (was established + initiator only); `specs/033-fixt-fix50sp2-session/` — the `1137` reject path now carries session-level negative witnesses (no behaviour change).
- [X] T019 Coverage (changed-line + touched-suite scope): `llvm-cov` over the coverage preset confirms every new Group-1 guard branch (absent / empty / malformed / stale-past / stale-future / conforming / build-fail / assign-fail / toAdmin-throw / persistent-reconnect) and the Group-2 `try/catch` are exercised (DA/BRDA, both arms); **ASan + UBSan + TSan** over the touched suites (`session_acceptor_logon_sending_time`, `session_credentials_rotated_emit`, `session_fixt_logon_establishment`) — TSan is required (`[const §IX.2]`) because Group 2 touches `drive_reconnect_attempt` on the reconnect strand. Any unreachable defensive arm carries a verify-record rationale. Full formal lcov DA/BRDA + 6-preset Tier-1 matrix → `/speckit-verify`.
- [X] T020 `codegraph sync` after the US1 + US2 production edits; `codegraph status` non-zero (≥188 files) — per `[[project_codegraph_library_autoresolve]]`.
- [X] T021 **Feature-completeness audit** (T058-equivalent; the gate that prevents the 001-class end-gap, consumed by `/gate-b` pre-flight 4d): assert against the merged tree that (i) every tasks.md row is `[X]` or carries an explicit waiver; (ii) every spec FR-001..011 + SC-001..004 maps to a landed test AND a landed implementation (G3/FR-009 = test-only by design); (iii) every 038-owned catalogue row (S-019 amendment) is `done` with a matching coverage-index entry; **(iv) FR-010/FR-011 no-new-surface witnessed** — `git diff <base>..HEAD -- include/fix/c_api.h include/fixpp/session/session_config.hpp` shows NO additions (no new wire field / error slot / config key / C-ABI symbol), and `python3 tools/check_layers.py` is clean (session/+reconnect surface only); **(v) Article VIII §3 bench self-waiver recorded** — the Group-1 guard is admin-path-only (one parse + one `check_sending_time` per first Logon, not the hot app path) and adds no heap, so there is NO bench-regression surface and no `bench/` change is required (record "VIII §3 self-waived — admin-path-only, no perf surface"). Record the verdict (100% in-scope or fully-waived) — this is the completeness evidence `/gate-b` requires.

**Checkpoint**: feature complete. Next pipeline step: **`/speckit-analyze`** (mandatory drift check, `[const §XVI.4]`) → `/speckit-checklist` → `/speckit-checklist-audit` (step 9 gate) → `/speckit-implement` → `/speckit-simplify` → `/speckit-verify` (Tier-1 full matrix) → Gate B.

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: T001 (new test file) before US1 tests.
- **Foundational (Phase 2)**: none — the three groups are independent.
- **US1 (Phase 3)**: T002–T004 (RED) → T005 (confirm RED) → T006 (impl) → T007 (GREEN) → T008 (fixture migrate). T008 depends on T006 (the guard must exist to surface the broken fixtures).
- **US2 (Phase 4)**: independent of US1 — T009 (RED) → T010 → T011 (impl) → T012 (GREEN). Can run in parallel with US1.
- **US3 (Phase 5)**: independent — T013 → T014. Can run in parallel with US1/US2 (test-only, no prod change).
- **Polish (Phase 6)**: after US1+US2+US3 land (T015–T018 doc [P]; T019/T020 after the production edits; T021 last).

### Parallel Opportunities

- The three user stories (US1, US2, US3) touch disjoint files and are independently testable — US2 and US3 may proceed in parallel with US1.
- Within US1, the three test tasks T002/T003/T004 are `[P]` (same new file, but additive cells — coordinate or serialize the single-file edits; the RED-confirm T005 joins them).
- Polish doc tasks T015–T018 are `[P]` (distinct files/sections).

## Implementation Strategy

### MVP (US1 — the acceptor SendingTime guard)

US1 alone is a viable, shippable increment: it closes the actual parity/anti-replay gap. US2 (callback-seam) and US3 (1137 witnesses) are independent riders that strengthen robustness/coverage but are not required for the MVP.

### Incremental

1. MVP US1 (guard + cells + fixture migration) → US2 (callback-seam, independent file) → US3 (1137 witnesses, test-only) → Polish.
2. `/speckit-analyze` → `/speckit-checklist`(+audit) → `/speckit-implement` → `/speckit-simplify` → `/speckit-verify` → Gate B.

## Notes

- TDD is mandatory (`[const §VII.3]`): every US1/US2 cell is written RED-first and confirmed RED before implementation. US3 characterizes existing correct behaviour (GREEN-confirming) — its value is the missing witness, so each cell must genuinely exercise the `1137` reject path (non-vacuous).
- No new wire field / error slot / config / codegen / C-ABI (FR-010); session/ + reconnect_fsm only (FR-011). The other Fable F-f tail items (wire-parser overflow, C-ABI sentinel, coverage-waiver remediation, §XV.9 corpus gate, 001-014 B&L back-fill) are explicitly OUT of scope.
- The `build_reject` buffer is pinned ≥512B and fails closed on build-failure — do not regress to a silent-success path (`[[feedback_fixed_buffer_build_failure_silent_success]]`).
