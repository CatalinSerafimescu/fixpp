---
description: "Task list for 016-interop-harness (Session-Layer Interop Gate)"
---

# Tasks: Interop Harness (Session-Layer Interop Gate)

**Input**: Design documents from `specs/016-interop-harness/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/ (all present)

**Tests**: This feature *is* a test-and-corpus feature — the committed deliverable under `tests/interop/` is itself the test suite (drivers, goldens, corpus scenarios, parity witnesses). There is therefore no separate "tests optional" layer; the US1–US3 implementation tasks ARE the test-authoring tasks, written red-first per `[const §VII.1]`.

**Organization**: Tasks grouped by user story (US1–US5 from spec.md). All paths are relative to the library submodule root `research/G19-fix-fpml-iso20022/library/`.

**Scope decision recorded at /speckit-tasks (R-prod / FR-004 / FR-028)**: **Option 1a — scope-in the bounded production change.** Foundational Phase 2 adds a `SessionConfig` reconnect-policy field + a bounded/cancellable engine connect (discharging the 015 down-peer carry-forward, CLAUDE.md L2). Its touched lines fall under Article IX §1 95/85 with a `verify.md` assessment + a coverage-index entry (T031). The "near-zero production surface" claim is therefore **bounded, not zero**.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies on incomplete tasks)
- **[Story]**: US1–US5; Setup/Foundational/Polish carry no story label

## Path Conventions

- Library deliverable: `tests/interop/**` (FR-026); per-PR CI: `.github/workflows/interop-smoke.yml`.
- The one production touch (Option 1a): `include/fixpp/session/session_config.hpp`, `src/session/session.cpp`, `src/session/engine.cpp`.
- **Out of repo (gitignored parent `../phase-9-harness/`, NOT created by these tasks)**: fork-exec orchestration, counterparty clones/builds, the capture proxy, the sweep analysis, the `interop-gate-evaluator` / `interop-badge-emit` named checks (contract: `contracts/parent-harness-gate-contract.md`).

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Create the `tests/interop/` tree and wire it into the build with ctest labels.

- [X] T001 Create the `tests/interop/` directory structure (`support/`, `happy/golden/`, `thorny/`, `parity/`) per plan.md Project Structure
- [X] T002 Register `add_subdirectory(tests/interop)` in `CMakeLists.txt` (after the `tests/transport` line ~122, behind the existing tests-enabled guard) and create `tests/interop/CMakeLists.txt` declaring targets `interop_happy`, `interop_thorny`, `interop_parity` with ctest labels `interop-happy` / `interop-thorny` / `interop-parity` (mirror `tests/transport/CMakeLists.txt` OpenSSL/GTest linkage for the TLS cells)
- [X] T003 [P] Implement `tests/interop/support/counterparty_probe.hpp` — probe a counterparty binary/port at test start; absent ⇒ produce a `skip_reason` (GoogleTest `GTEST_SKIP() << reason`), never silent-pass (FR-023, data-model E6)

**Checkpoint**: `tests/interop` configures and builds an empty suite under ctest.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Shared fixture + golden machinery + scenario descriptor types, plus the one bounded production prerequisite (Option 1a). MUST complete before US1/US2/US3 cells can be authored.

**⚠️ CRITICAL**: No user-story cell can be driven until the fixture (T004), golden diff (T005), descriptor types (T006), and the reconnect prod change (T008) land.

- [X] T004 [P] Implement `tests/interop/support/interop_fixture.hpp` + `interop_fixture.cpp` — a GoogleTest fixture bringing up a fixpp `Engine` (015, `include/fixpp/session/engine.hpp`) in the requested role bound to a TCP port, parameterized by `(role, fix_version, port, security)`; exposes fixpp's observable FSM end-state + seqnum deltas for assertion (research R1, data-model E1)
- [X] T005 [P] Implement `tests/interop/support/golden_diff.hpp` + `golden_diff.cpp` — byte-stream normalization excluding tags `34/52/60/122/10/9/112` then frame-ordered diff returning `match` | `mismatch:<dir>:<frame-index>:<tag-or-structure>` (FR-006, contract `golden-transcript-format.md`)
- [X] T006 [P] Define the scenario-descriptor types in `tests/interop/support/scenario_descriptor.hpp` — the `Scenario` base (E1: `id/kind/preconditions/driven_sequence/pass_criteria/deadline/reconnect_policy/skip_reason`) + `MatrixCell`/`CorpusScenario`/`ParityRow` fields per data-model E2–E4 and contract `scenario-descriptor.md` (REQUIRED `spec_ref` invariant)
- [X] T007 Write the RED witness for the reconnect prerequisite in `tests/session/reconnect_policy_witness_test.cpp` — assert (a) a finite `SessionConfig` reconnect policy is honored (no empty-`ReconnectPolicy` 0-backoff busy-spin) and (b) `Engine::stop()` returns within a stated bound when an initiator is aimed at a never-listening peer; confirm it FAILS against current HEAD (015 down-peer carry-forward, CLAUDE.md L2)
- [X] T008 Production change (Option 1a) to make T007 pass: add a reconnect-policy field to `include/fixpp/session/session_config.hpp`, wire it in `Engine::open()` / the per-session `reconnect_fsm_` default (`src/session/session.cpp:96–98`, replacing the empty `fixpp::transport::ReconnectPolicy{}` whose `// Phase 4 wires cfg_.reconnect_policy` TODO marks this exact spot), and bound/cancel the in-flight `transport::Transport::async_connect` so `cancellation_type::total` promptly tears down a mid-connect initiator (`src/session/engine.cpp`) — FR-004/FR-028. Keep the diff minimal; touched lines are under Article IX §1 95/85 (assessed in T033)

**Checkpoint**: Fixture brings up a fixpp Engine, goldens diff, descriptor types compile, and `Engine::stop()` is provably bounded on a down peer.

---

## Phase 3: User Story 1 - Happy-path session interop against live reference engines (Priority: P1) 🎯 MVP

**Goal**: fixpp drives the full FIX 4.4 session-admin chain against QuickFIX-cpp and QuickFIX-J in both roles; each cell's wire is golden-diffed and the FSM end-state + seqnum deltas + wire-observed counterparty terminal behavior are asserted.

**Independent Test**: Stand up one reference engine, drive a fixpp session opposite it, capture both streams, confirm the expected FSM end-state + seqnum deltas + golden match — no business messages.

> Drivers are value-parameterized over `(counterparty ∈ {quickfix-cpp, quickfix-j}) × (role ∈ {fixpp-initiator, fixpp-acceptor})` so one file per event-chain covers the 4 live cells; the smoke cell is `HP-QFcpp-init-fix44-logon-hb-logout`. Live cells are counterparty-required → `GTEST_SKIP` with reason when absent (T003).

- [ ] T009 [P] [US1] Author the golden transcripts `tests/interop/happy/golden/HP-*.fix` for the 5 FIX 4.4 plain-TCP event chains (capture from a paired run, check in byte-exact per `golden-transcript-format.md`); SOH rendered `\x01`, both directions interleaved with `>`/`<` markers
- [ ] T010 [P] [US1] Driver `tests/interop/happy/hp_fix44_logon_hb_logout_test.cpp` — Logon → idle Heartbeat → TestRequest/Heartbeat → Logout; assert FSM disconnected both sides, seqnum deltas, counterparty terminal (received `Logout(35=5)` / orderly close, FR-007), golden match. `spec_ref [FIX-SL §4.3/§4.5.1/§4.5.5/§4.6]` (this is the smoke cell, FR-022)
- [ ] T011 [P] [US1] Driver `tests/interop/happy/hp_fix44_testrequest_echo_test.cpp` — active+idle Heartbeat + TestRequest echo. `spec_ref [FIX-SL §4.5.1/§4.5.5]`
- [ ] T012 [P] [US1] Driver `tests/interop/happy/hp_fix44_reject_invalid_admin_test.cpp` — invalid admin message → wire-conformant `Reject(35=3)` with RefTagID/RefMsgType. `spec_ref [FIX-SL §4.5.4]`
- [ ] T013 [P] [US1] Driver `tests/interop/happy/hp_fix44_seqnum_recovery_test.cpp` — counterparty higher-numbered message → fixpp drives ResendRequest / SequenceReset-GapFill, session resynchronizes without fatal disconnect (live via 013). `spec_ref [FIX-SL §4.5.3/§4.8.1/§4.8.2/§4.8.5]`
- [ ] T014 [US1] Driver `tests/interop/happy/hp_fix44_disconnect_reconnect_noreset_test.cpp` — abrupt disconnect → reconnect with `ResetSeqNumFlag=N`, sequence continuity preserved, no spurious reset; uses the finite reconnect policy from T008 + a `stop()` watchdog bound (FR-004). `spec_ref [FIX-SL §4.4.2/§4.4.3/§4.8.6]` (depends on T008)
- [ ] T015 [P] [US1] TLS-logon variant drivers `tests/interop/happy/hp_fix44_tls_logon_test.cpp` — the 4 TLS-logon cells (QFC/QFJ × init/acceptor) with a `SecurityProfile` the counterparty config satisfies (server-auth/pinned); `mtls-mutual` reserved v1.1 with a deferred note (FR-025). `spec_ref [FIXS §3.2/§3.4]`
- [ ] T016 [US1] Down-peer regression cell `tests/interop/happy/hp_down_peer_stop_watchdog_test.cpp` — a *separate* (non-matrix) FR-028 cell proving `Engine::stop()` returns within a stated bound when the initiator targets a never-accepting peer; the watchdog IS the assertion, MUST NOT silently pass on a hang (builds on T008)
- [ ] T017 [US1] Register the deferred placeholder rows in `tests/interop/happy/MATRIX.md` (matrix manifest): business cells `deferred:app-messages` (FR-005), FIX 5.0 SP2 / FIXT.1.1 cells `deferred:fixt-routing` (FR-003), Fix8 happy-path cells `deferred:fix8-revisit` (FR-009) — each present with rationale, none executed; every axis covered ≥ once or carries a deferred-to-vN.x note (FR-008)

**Checkpoint**: US1 is independently testable — one role/engine/version cell delivers standalone interop proof; deferred cells are enumerated, not absent.

---

## Phase 4: User Story 2 - Thorny-issues corpus replay (Priority: P1)

**Goal**: Convert a bounded, capped upstream-bug worklist into executable fixpp scenarios, bucketed by priority, append-only, each linked to provenance.

**Independent Test**: Take one categorized upstream bug, encode its triggering sequence + expected fixpp behavior, run it, confirm fixpp handles it per spec or records a known-limitation with a tracking issue.

> Scope is BOUNDED (FR-010 / R-scope): the pre-seeded `../phase-9-harness/manifest/scenarios.yaml` worklist + a capped last-2-years closed-with-fix tail (cap stated in the in-repo manifest). The open-issue `watch:` bucket is a phased follow-on sweep, NOT folded into this feature. The raw sweep analysis stays in the parent (R2).

- [ ] T018 [US2] Create `tests/interop/thorny/CORPUS-INDEX.md` — the in-repo provenance index (issue ref, URL, state, category, priority bucket → test file); MUST NOT match `no-research.yml` patterns (not `*-decisions.md`); record the per-engine closed-tail cap (FR-010/FR-011, R2)
- [ ] T019 [US2] Derive the bounded executable worklist from `../phase-9-harness/manifest/scenarios.yaml` (closed-with-fix tail, capped) into descriptor stubs under `tests/interop/thorny/<category>/` bucketed `P1/P2/P3` (FR-012); the open-issue `watch:` bucket is explicitly out of this feature (follow-on note in CORPUS-INDEX.md)
- [ ] T020 [P] [US2] Encode the P1 (release-blocking) corpus scenarios `tests/interop/thorny/<category>/<engine>-issue-NNN_test.cpp` for the recovery/sequencing categories (SequenceReset/GapFill, ResendRequest edges, Logon/Logout race, persistence-recovery); each cites `spec_ref` (FR-018) and a `differentiator` flag where fixpp is spec-correct vs a buggy upstream (FR-015)
- [ ] T021 [P] [US2] Encode the P1 corpus scenarios for the framing/validation categories (BodyLength/checksum, field-validation, encoding, reject, repeating-group, Heartbeat); same provenance + spec_ref + differentiator discipline
- [ ] T022 [US2] Wire the corpus disposition rule into the scenarios: a failing `P1` MUST be `pass` OR `known-limitation:<open-tracking-issue>` (FR-014); confirm append-only construction is the initial population (FR-013 governs later releases, data-model E3)

**Checkpoint**: US2 is independently testable — one categorized bug replays against fixpp and is dispositioned; the corpus is a bounded, provenance-linked, append-only suite.

---

## Phase 5: User Story 3 - Reference unit-test parity GAP closure (Priority: P2)

**Goal**: Close the still-open Track-1 parity GAP rows (R-parity worklist) with witnesses + citations; leave N/A and deferred-by-design rows with auditable rationale.

**Independent Test**: Pick a GAP row, confirm fixpp lacks the assertion, add the witness, confirm it passes and the matrix row flips GAP → COVERED with a citation.

> Most of the 27-GAP audit is already closed in code (#90/#91); this is witness-writing + a model assertion, not new production behavior (R-parity). Contract: `parity-disposition.md`.

- [ ] T023 [P] [US3] Witness `tests/interop/parity/resend_abort_on_failing_write_test.cpp` — QFJ-646: resend aborts when transport `send()` returns false mid-resend (Bucket-2, still open upstream); plumbing exists → COVERED
- [ ] T024 [P] [US3] Witness `tests/interop/parity/replay_subsumes_reorder_queue_test.cpp` — Bucket-4 model confirmation: assert fixpp's store-replay recovery subsumes QuickFIX's reorder-queue protocol outcome (simultaneous bidirectional ResendRequests, remove-queued-on-SequenceReset, large-queue); default no reorder-queue impl. If the witness FAILS, stop and surface a scoped session-layer finding (do NOT silently absorb — R-prod/2, leaves 016 a separate feature)
- [ ] T025 [P] [US3] Witness `tests/interop/parity/inbound_sequencereset_arms_test.cpp` — confirm the S-023/#90 inbound-SequenceReset `NewSeqNo >/=/<` arms cover the audit rows; cite #90 tests → COVERED (extend only if a row is uncovered)
- [ ] T026 [US3] Disposition update — flip the closed GAP rows to COVERED with test citations in `phases/phase-9/unit-test-parity-matrix.md` (parent doc): T023/T024/T025 + the already-closed EncryptMethod=0 / integer-overflow rows (cite #91 witnesses); leave Bucket-3 (PossDup/FIXT routing/5.0SP2/app-message) as `deferred:by-design` with rationale consistent with `deferred:fixt-routing` (FR-003); acceptor HeartBtInt-echo (Bucket-1) included only if cheap (FR-016/FR-017, SC-003)

**Checkpoint**: US3 is independently testable — every GAP row is COVERED-with-citation or explicitly deferred-with-tracking; no GAP undispositioned.

---

## Phase 6: User Story 4 - Release-gate enforcement, CI tiering, sanitizer discipline (Priority: P2)

**Goal**: Wire the disciplines as an actual gate — per-PR smoke (in-repo), full matrix + sanitizers at release-prep (parent named checks), per-cell completeness so no cell is silently absent.

**Independent Test**: Run the per-PR smoke cell on the normal build and confirm it completes in the PR-latency budget; confirm a deliberately-introduced TSan race in a matrix cell is caught and blocks the gate (parent tier).

> Only fixpp is sanitizer-instrumented (FR-021); the counterparty runs as its unmodified production binary. The `interop-full-matrix` / `interop-gate-evaluator` / `interop-release-prep` named checks live in the parent (contract `parent-harness-gate-contract.md`) — these tasks deliver the in-repo half + the obligations the parent checks depend on.

- [ ] T027 [US4] Create `.github/workflows/interop-smoke.yml` — run the single smoke cell `HP-QFcpp-init-fix44-logon-hb-logout` (T010) on the normal build only, `ubuntu-latest`, gating PRs touching the transport/session/interop surface; counterparty absent ⇒ skip-with-reason (FR-022/FR-023). Enforce the SC-005 budget: the smoke cell MUST complete in **≤ 120 s wall-clock** (job step timeout) — an over-budget run is a gate failure, not a flake (maps SC-005)
- [ ] T028 [US4] Emit a per-cell `cell_result` manifest from the suites (a `tests/interop/cell_results.schema-check` test or a generated artifact) honoring the contract schema — every matrix/corpus cell present incl. each `deferred:*` row as `status: n/a` (per-cell completeness rule); the strict `skip:<reason>` (FR-023 counterparty-unavailable) vs `matrix_disposition: deferred:<tag>` disambiguation is preserved (contract `parent-harness-gate-contract.md`)
- [ ] T029 [US4] Confirm every `interop-happy` + `interop-thorny` ctest target builds + runs clean under the ASan/UBSan and TSan presets locally (`-j2`, one preset at a time, `AskUserQuestion`-gated per `[const §XVII.7]`); a sanitizer-only failure is a real failure (FR-019/FR-020); record results for the parent `interop-full-matrix` named check (the release-prep full matrix itself runs in the parent). **FR-021 guard**: assert the `interop_*` ctest targets do NOT co-compile or link any counterparty engine source (only fixpp's process carries sanitizer flags); the counterparty-runs-as-unmodified-binary half is a parent-harness obligation — document it as inherited in `KNOWN-LIMITATIONS.md` (T030) if no in-repo assertion is feasible

**Checkpoint**: US4 is independently testable — the smoke gate runs per-PR; the suites are sanitizer-clean and emit the gate-evaluator's per-cell input.

---

## Phase 7: User Story 5 - Interop badge and transcript artifacts (Priority: P3)

**Goal**: Provide the in-repo inputs the parent `interop-badge-emit` check consumes — archivable goldens/captures + the documented-limitations source naming exact counterparty versions.

**Independent Test**: After a green full-matrix run, confirm the release inputs include per-scenario goldens + a documented-limitations list naming exact engine versions/commits.

> The badge EMISSION is parent-side (out of repo, FR-026); these tasks deliver the in-repo source artifacts it links.

- [ ] T030 [P] [US5] Assemble the documented-known-limitations source `tests/interop/KNOWN-LIMITATIONS.md` — each corpus `known-limitation` (FR-014) + the §VII.6 residual pointer + any TLS/down-peer caveats, each with its tracking issue, for the badge's limitations list (FR-024, SC-007)
- [ ] T031 [US5] Confirm the per-scenario goldens (`tests/interop/happy/golden/`) + the CORPUS-INDEX are structured as archivable release artifacts the parent `interop-badge-emit` links; the badge text (`Interop verified against QuickFIX-cpp v1.16.0 / QuickFIX-J 3.0.1`) names exact `{name, version, commit}` (FR-024, contract `parent-harness-gate-contract.md`)

**Checkpoint**: US5 inputs ready — goldens archivable, limitations enumerated, badge names exact versions.

---

## Phase 8: Polish & Cross-Cutting Concerns

**Purpose**: §VII.6 residual bookkeeping, completeness/catalogue gates, coverage disposition, repo-hygiene guard.

- [ ] T032 [Polish] Record the `[const §VII.6]` business-message residual (FR-027/SC-008) in `spec/behaviors-and-limitations.md` (parent `book/`-adjacent spec dir) + the feature catalogue — the session-only badge does NOT discharge `Logon → NewOrderSingle → ExecutionReport → Logout`; forward-pointer to A-001/A-006; zero 016 artifact claims the business flow ran
- [ ] T033 [Polish] Coverage disposition for the Option-1a production touch — add a coverage-index entry for the `SessionConfig` reconnect-policy field + bounded-connect lines and an Article IX §1 95/85 assessment note seeding `/speckit-verify`'s `verify.md` (T008 touched modules)
- [ ] T034 [P] [Polish] Run the `no-research.yml` guard grep over `tests/interop/**` (reject `^research/`, `^decisions/`, `*-decisions.md`, `opus_plan.md`, `SYNTHESIS.md`) — confirm CORPUS-INDEX + witnesses pass; the sweep analysis stays in the parent (R2)
- [ ] T035 [Polish] Feature-completeness audit — map every task ↔ FR-001..028 / SC-001..008 ↔ catalogue rows; 100% or explicitly waived (the `/gate-b` precondition per `[[feedback_feature_completeness_gate]]`)
- [ ] T036 [Polish] Update `feature-catalogue.md` + `coverage-index.md` for 016 (S-row interop dispositions, parity-row flips, the §VII.6 residual) per `[[feedback_feature_completeness_gate]]`
- [ ] T037 [Polish] Run quickstart.md validation — the parity standalone run + the smoke cell + the verify step all execute as documented

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: no deps — start immediately.
- **Foundational (Phase 2)**: depends on Setup; BLOCKS all user stories. T008 (prod change) blocks the reconnect/down-peer cells (T014, T016) specifically.
- **US1 (P1, Phase 3)**: depends on Foundational (T004/T005/T006; T008 for T014/T016). MVP.
- **US2 (P1, Phase 4)**: depends on Foundational (T004/T006). Independent of US1.
- **US3 (P2, Phase 5)**: depends on Foundational (T006). Independent of US1/US2 (standalone, no counterparty).
- **US4 (P2, Phase 6)**: depends on US1 (smoke cell T010) + US2 (corpus cells) for the gate inputs; T028 needs the cells enumerated.
- **US5 (P3, Phase 7)**: depends on US1/US2 producing goldens + limitations.
- **Polish (Phase 8)**: depends on the desired stories being complete; T033 depends on T008.

### User Story Dependencies

- **US1 / US2 / US3** are mutually independent after Foundational (US3 is fully standalone — no counterparty).
- **US4** consumes US1+US2 artifacts (gate inputs); **US5** consumes US1+US2 results (badge inputs).

### Within Each User Story

- TDD ordering: the reconnect witness T007 precedes the prod change T008; goldens (T009) precede/accompany their drivers; each cell cites a `spec_ref` before it is considered done (FR-018).

### Parallel Opportunities

- Setup: T003 ‖ (after T001/T002).
- Foundational: T004 ‖ T005 ‖ T006 (different files); T007→T008 sequential.
- US1 drivers T010/T011/T012/T013/T015 are [P] (different files); T014/T016 depend on T008; T009 goldens [P].
- US2: T020 ‖ T021 (different category dirs) after T018/T019.
- US3: T023 ‖ T024 ‖ T025 (different witness files) before the T026 disposition flip.

---

## Parallel Example: User Story 1

```bash
# After Foundational (T004–T008), launch the independent FIX 4.4 chain drivers together:
Task: "Driver hp_fix44_logon_hb_logout_test.cpp (smoke cell)"
Task: "Driver hp_fix44_testrequest_echo_test.cpp"
Task: "Driver hp_fix44_reject_invalid_admin_test.cpp"
Task: "Driver hp_fix44_seqnum_recovery_test.cpp"
Task: "TLS-logon variant drivers hp_fix44_tls_logon_test.cpp"
# T014 (reconnect) + T016 (down-peer) run after T008.
```

---

## Implementation Strategy

### MVP First (User Story 1 only)

1. Phase 1 Setup → 2. Phase 2 Foundational (incl. the bounded reconnect prod change) → 3. Phase 3 US1.
4. **STOP and VALIDATE**: the smoke cell `HP-QFcpp-init-fix44-logon-hb-logout` (T010) green against a live QuickFIX-cpp = standalone interop proof.

### Incremental Delivery

1. Setup + Foundational → foundation ready (fixture, goldens, descriptor, bounded `stop()`).
2. US1 → smoke + full FIX 4.4 matrix green → MVP interop evidence.
3. US2 → bounded thorny corpus replays.
4. US3 → parity GAP rows closed (standalone, no counterparty).
5. US4 → per-PR smoke gate + sanitizer-clean suites + gate-evaluator inputs.
6. US5 → badge inputs + limitations list.
7. Polish → §VII.6 residual, completeness/catalogue, coverage disposition, hygiene guard.

---

## Notes

- [P] = different files, no incomplete-task dependency.
- Every executed cell cites a FIX spec section (FR-018 / SC-006) — a cell justified "because QFC/QFJ does X" is invalid.
- Live cells are counterparty-required: absent binary ⇒ `GTEST_SKIP` with reason (FR-023), never silent-pass; `skip:<reason>` is strictly counterparty-unavailable and MUST NOT be conflated with `matrix_disposition: deferred:<tag>` (by-design scope deferral).
- The corpus is bounded for v1.0 (FR-010); append-only governs later releases (FR-013). The open-issue `watch:` bucket is a phased follow-on sweep, not in this feature.
- §VII.6's business-message clause stays an open v1.0-GA residual (FR-027/SC-008) — not discharged by this badge.
- Builds are resource-gated: `-j2`, one preset at a time, `AskUserQuestion`-approved (`[const §XVII.7]`, `[[feedback_build_resource_cap_oom]]`).
