---
description: "Task list for 010-session-cfg-lifetime (W-5 cfg lifetime + PR #82 Gate B P2/P3 waiver closure)"
---

# Tasks: 010-session-cfg-lifetime

**Input**: Design documents from `/specs/010-session-cfg-lifetime/` ([spec.md](spec.md), [plan.md](plan.md), [research.md](research.md), [data-model.md](data-model.md), [quickstart.md](quickstart.md), [contracts/session_error_state_for_send.hpp](contracts/session_error_state_for_send.hpp))
**Prerequisites**: plan.md (✅), spec.md (✅), research.md (✅), data-model.md (✅), contracts/ (✅)
**Tests**: REQUIRED — every FR requires a runtime-behavior test per `[const §VII.1]` red-green-refactor + the test-bodies discipline from `[[project_005_phase8_completeness_false_pass]]` + `[[feedback_simplify_pass_catches_9th_burn]]`.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: parallelizable (different files, no dependencies on incomplete tasks)
- **[Story]**: which user story (US1/US2/US3/US4) — only on Phase 3–6 tasks
- File paths are absolute-relative to `library/` submodule root

## Path conventions

- C++ source: `include/fixpp/session/`, `src/session/`, `include/fixpp/core/`
- Tests: `tests/session/`
- Decisions: `.specify/decisions/`

---

## Phase 1: Setup (verify clean starting state)

**Purpose**: confirm the branch is positioned correctly and the inherited 005/009 work is green before any 010 edit lands. No source changes.

- [ ] T001 Verify branch state: confirm `git branch --show-current` returns `010-session-cfg-lifetime`, confirm `git log --oneline ba2222d..HEAD` shows zero 010 commits yet (untouched base), confirm `git status` is clean. If any check fails, STOP and surface to user.
- [ ] T002 [P] Baseline test run: `ctest --test-dir build/linux-clang-debug -R 'session_|tc_'` against the inherited HEAD `ba2222d`. Capture pass/fail tally. Expected: all GREEN (009 closed at GREEN, the only test currently skipped is `session_coverage_adversarial` under ASan per the W-5 carry-forward — that's the test FR-003 re-enables). If any unexpected session test is RED at baseline, STOP and surface — that's a pre-existing 005/009 issue this slice did not introduce.

---

## Phase 2: Foundational (blocking prerequisites for all user stories)

**Purpose**: add the small shared surface area (one error variant + the FSM visit-history primitive) consumed by US1 + US2 + US3 tests. Foundational because three of the four user stories' tests reference these symbols.

**⚠️ CRITICAL**: No user story work can begin until T003 + T004 + T005 are merged.

- [ ] T003 [P] Add `session_invalid_state_for_send = 77` variant to `include/fixpp/core/error.hpp` immediately after `session_invalid_config = 76` (research D-3 + contracts/session_error_state_for_send.hpp). Paste the doc-comment template verbatim from D-3 (FR-005, [FIX-SL §4.5.4], → FIXPP_ERR_SESSION_REJECT). Verify the build still compiles; no .cpp emits the new variant yet (US2 wires the call sites).
- [ ] T004 [P] Add the FSM visit-history infrastructure to `include/fixpp/session/session.hpp` (research D-2 + data-model E1):
  - Add `#include <array>` + `#include <span>` near the existing `#include <cstdint>` (if not already present).
  - Inside the `Session` private section: `std::array<fsm_state, 16> fsm_visit_history_{}; std::uint8_t fsm_visit_count_ = 0;`.
  - Inside the `Session` private section: declaration `void record_state_transition_(fsm_state new_state) noexcept;`.
  - Inside the `Session` public section: `[[nodiscard]] std::span<const fsm_state> fsm_visit_history() const noexcept;`.
  - Do NOT yet flip `const SessionConfig& cfg_;` — that's T009 (US1 phase, requires the RED test first).
- [ ] T005 Add the `record_state_transition_()` definition + route every `fsm_state_ = X;` site through it in `src/session/session.cpp` (research D-2):
  - Body of helper (define near other private helpers; ~6 lines): `void Session::record_state_transition_(fsm_state new_state) noexcept { fsm_visit_history_[fsm_visit_count_ % 16] = new_state; if (fsm_visit_count_ < std::numeric_limits<std::uint8_t>::max()) { ++fsm_visit_count_; } fsm_state_ = new_state; }`.
  - Body of accessor (define near other public accessors; ~3 lines): `std::span<const fsm_state> Session::fsm_visit_history() const noexcept { return std::span<const fsm_state>{fsm_visit_history_.data(), std::min<std::size_t>(fsm_visit_count_, 16)}; }`.
  - Replace every `fsm_state_ = X;` (~10 sites; lines 239, 292, 376, 388, 630, 641, 648, 659, 690, 696, 702, 712 in the post-009 tree — re-grep at task time, count must still be ≥10) with `record_state_transition_(X);`. Search pattern: `\bfsm_state_ =\b`; the only line that should remain matching post-edit is the assignment INSIDE the helper itself.
  - Build under `linux-clang-debug` — must compile and the full `session_|tc_` ctest set must remain GREEN (this is a zero-behavior-change refactor; tests should not regress).

**Checkpoint**: Foundation ready — user story phases (3-6) can now proceed. Sequential is required (Phase 3 and Phase 4 both edit `session.hpp` + `session.cpp`); Phase 5 + 6 add new test files only and can interleave with the closing GREEN-confirmation tasks of earlier phases.

---

## Phase 3: User Story 1 — SessionConfig lifetime safety (Priority: P1) 🎯 MVP

**Goal**: Eliminate the pre-existing 005-baseline stack-use-after-scope on `Session::cfg_` (W-5). The `Session` constructor copies the caller's `SessionConfig` into a by-value member; caller may freely drop or mutate their config after the ctor returns. Re-enable `session_coverage_adversarial` under ASan (no skip).

**Independent Test**: build the 3 acceptance scenarios in spec.md US1; run under `linux-clang-asan` preset. ASan reports zero `stack-use-after-scope`; the re-enabled `session_coverage_adversarial` test passes.

**MVP scope: just this phase.** US1 alone closes the W-5 memory-safety bug and removes the CI ASan skip — the load-bearing waiver of PR #82 Gate B.

- [ ] T006 [P] [US1] Add the copyability hygiene gate to `include/fixpp/session/session_config.hpp` (research D-1 + data-model E2): `static_assert(std::is_copy_constructible_v<SessionConfig>, "SessionConfig must be copy-constructible per 010 W-5 by-value Session::cfg_ membership");` immediately after the class definition. Add `#include <type_traits>` if not already present. Verify the build still compiles — if it fails, a non-copyable member was added between PR #82 and 010; surface and STOP.
- [ ] T007 [US1] Write `tests/session/cfg_lifetime_safety_test.cpp` (TDD red per FR-001 / FR-002 / SC-001):
  - Test `CallerCfgDropsAfterCtor_SessionContinuesWithoutUAF`: in a nested scope, construct `SessionConfig` as a local with non-default values for `executor_override` + `heartbt_int` + `sender_comp_id`; construct a `Session` with that config; let the nested scope close (cfg goes out of scope); from the outer scope, drive the session through several FSM transitions (open → on_inbound_frame(Logon) → state() reads → close); assert no UAF and no abort. Under ASan this test fires `stack-use-after-scope` pre-fix; clean post-fix.
  - Test `CallerMutatesCfgAfterCtor_SessionUnaffected`: same setup but instead of dropping the local, mutate it after the ctor (e.g. `cfg.heartbt_int = 999s;`); exercise the session; assert it reads the original (pre-mutation) heartbt_int via state behavior (timer fire interval observable through `mock_clock`).
  - Test `MultipleSessionsFromSameCfgEvolveIndependently`: construct one `SessionConfig`; build two `Session`s from it; mutate the cfg between the two ctors; assert the two sessions see different snapshots (the first sees the pre-mutation values, the second sees post-mutation).
  - Add to `tests/session/CMakeLists.txt` via `add_threading_test(session_cfg_lifetime_safety cfg_lifetime_safety_test.cpp)` (mirror the existing `session_coverage_adversarial` pattern). Verify the test compiles and at least one assertion FAILS pre-fix (likely an ASan abort under `linux-clang-asan`; on `linux-clang-debug` the first test may succeed accidentally if the stack slot isn't reused — that's why the ASan run is the binding gate).
- [ ] T008 [US1] Flip the cfg_ member in `include/fixpp/session/session.hpp` (data-model E1): change `const SessionConfig& cfg_;` (~line 281 in the post-009 tree — re-grep at task time) to `SessionConfig cfg_;`. Do NOT add `const` — see analyze finding rationale: a non-const member preserves callable assignment in future refactors without changing observable semantics today (no code mutates `cfg_` post-ctor; the discipline is convention, not const-ness).
- [ ] T009 [US1] Update the `Session` ctor in `src/session/session.cpp` (~line 116) to initialize `cfg_` by copy. The initializer-list expression is identical (`cfg_{cfg}`), but the declared member type is now `SessionConfig` (was `const SessionConfig&`), so the brace-init is now a copy-construction, not a reference binding. Verify all read sites `cfg_.X` remain valid (they should — the C++ syntax is unchanged for member access). Build under `linux-clang-debug`; full `session_|tc_` ctest set GREEN.
- [ ] T010 [US1] Remove the W-5 carry-forward ASan-only skip block from `tests/session/CMakeLists.txt` (FR-003). Delete the comment block + the `if(FIXPP_ENABLE_ASAN) set_tests_properties(session_coverage_adversarial PROPERTIES DISABLED TRUE) endif()` block added in PR #82 (~10 lines). Verify under `linux-clang-asan`: `ctest -R '^session_coverage_adversarial$'` now runs (not skipped) and passes clean (no UAF).
- [ ] T011 [US1] Build + run T007 + the re-enabled `session_coverage_adversarial` under ASan: `cmake --build build/linux-clang-asan -j && ctest --test-dir build/linux-clang-asan -V -R '^session_(cfg_lifetime_safety|coverage_adversarial)$'`. Expected: both GREEN, no ASan reports. If either fails, the by-value fix is incomplete or `cfg_lifetime_safety_test` exposes a sibling lifetime bug — surface and STOP.

**Checkpoint**: US1 delivers the W-5 MVP — the ASan skip is removed, the cfg UAF is closed, the library is sanitizer-clean across the full session test matrix.

---

## Phase 4: User Story 2 — FSM observability + error precision (Priority: P2)

**Goal**: (a) F-04 — a production-shaped test directly observes the synchronous-transient `LogonReceived` state using `fsm_visit_history()`. (b) F-07/E1 — `Session::send` returns the dedicated `session_invalid_state_for_send` variant when the FSM is not in `Active`, replacing the 005-era reuse of `session_invalid_logon`.

**Independent Test**: build the 3 acceptance scenarios in spec.md US2; assert against the visit history accessor + the new error variant.

- [ ] T012 [US2] Write `tests/session/logon_received_observability_test.cpp` (TDD red per FR-004 acceptance scenario 1):
  - Test `AcceptorReplyLogonPath_VisitHistoryContainsLogonReceived`: configure an acceptor session (`cfg.role = session_role::acceptor`); call `open()` (stays NotConnected); feed a valid peer Logon via `on_inbound_frame`; AFTER the awaitable completes (`io_context.run()` returns), read `session.fsm_visit_history()`; assert the span contains `fsm_state::LogonReceived` somewhere (between NotConnected and Active).
  - Test `InitiatorOpen_VisitHistoryContainsLogonSent`: symmetric initiator-side check — visit history contains LogonSent after open() returns.
  - Test `VisitHistoryEmptyOnFreshSession`: a Session whose `open()` has not been called returns an empty span.
  - Add to `tests/session/CMakeLists.txt`. Verify the test compiles; will pass once T005 is in (the helper records every transition). If T005 didn't already make this GREEN, surface — the helper has a defect.
- [ ] T013 [P] [US2] Write `tests/session/session_send_invalid_state_test.cpp` (TDD red per FR-005 acceptance scenarios 1+2):
  - Test `SendPreLogon_ReturnsInvalidStateForSend`: construct a Session, do NOT call open(); call `co_await session.send(payload)`; assert the result is `std::unexpected(error::session_invalid_state_for_send)`.
  - Test `SendInLogonSent_ReturnsInvalidStateForSend`: open() an initiator, freeze the clock so the session stays in LogonSent; call send; assert same.
  - Test `SendInDisconnected_ReturnsInvalidStateForSend`: drive the session to Disconnected; call send; assert same.
  - Test `SendInActive_DoesNotReturnInvalidStateForSend`: positive control — assert `session.send(payload)` returns success (does NOT return invalid_state_for_send) when the FSM is Active.
  - Add to `tests/session/CMakeLists.txt`. Verify the test compiles and FAILS pre-fix (the existing impl returns `session_invalid_logon`).
- [ ] T014 [US2] Replace the two `co_return std::unexpected(error::session_invalid_logon);` sites in `Session::send` in `src/session/session.cpp` (~line 1151 + the symmetric site — re-grep `session_invalid_logon` at task time; verify exactly two hits inside `Session::send` per the contracts/session_error_state_for_send.hpp inventory) with `co_return std::unexpected(error::session_invalid_state_for_send);` (FR-005). Verify T013 GREEN.
- [ ] T015 [US2] Grep + update existing tests that assert `error::session_invalid_logon` at the two `Session::send` non-Active sites (FR-005 AC3): `grep -rn 'session_invalid_logon' tests/session/`. Identify the calls that assert against `Session::send` failure (NOT against the FSM Logon-refusal path — those legitimately stay as `session_invalid_logon`). Update only the send-path assertions to `session_invalid_state_for_send`. Estimated ≤5 LoC across 1-2 files. Verify the full `session_|tc_` ctest set GREEN.

**Checkpoint**: US2 delivers F-04 + F-07/E1 — synchronous-transient state observable in tests; outbound send returns a precise error variant.

---

## Phase 5: User Story 3 — FSM matrix coverage + admin distinct-time + mixed-path bookkeeping (Priority: P2)

**Goal**: (a) F-06 — exhaustive per-cell witness for the 6-state × N-event FSM matrix `[FIX-SL §4.10]`. (b) F-05 — admin builders exercise the per-message-distinct `SendingTime` branch (clock advance between two emits). (c) RC#G mixed-path — the four mixed-success-mode permutations at admin emit sites 1+2 (Reject-ok/Logout-ok, Reject-fail/Logout-skip, Reject-ok/Logout-fail, Reject-fail/Logout-fail). All three are test-only — no production code change.

**Independent Test**: build the 3 acceptance scenarios in spec.md US3; assert against the visit history + outbound frame captures.

- [ ] T016 [US3] Enumerate the FSM matrix cells from the 005 data-model + the existing `switch (fsm_state_)` cascade in `src/session/session.cpp` (research D-4). Output: an inline comment table at the top of the new test file `tests/session/fsm_matrix_witness_test.cpp` listing every (state, event) cell with its design-required outcome: `transition_to(X)` / `reject_and_disconnect` / `ignore_no_op`. Estimated ~40-45 reachable cells (6 × 9 minus design-forbidden). Cross-check against `specs/005-session-establishment-fsm/data-model.md` — if any impl-handled cell is missing from the 005 data-model OR vice-versa, surface as an /speckit-analyze finding for follow-up (do NOT silently invent cells; the matrix witness covers what 005 declared).
- [ ] T017 [US3] Write `tests/session/fsm_matrix_witness_test.cpp` (TDD red per FR-006 + SC-002):
  - One `TEST(FsmMatrixWitness, <State>_<Event>_<ExpectedOutcome>)` per enumerated cell from T016.
  - Each test: prepare the session in the cell's source state (using `fsm_visit_history()` to confirm), fire the event (inbound frame, lifecycle call, or timer fire), then assert via `fsm_visit_history()` that the expected end state was visited (positive cells) OR that no transition occurred (forbidden / ignored cells).
  - Add to `tests/session/CMakeLists.txt` via `add_threading_test(session_fsm_matrix_witness fsm_matrix_witness_test.cpp)`.
  - Estimated 400-500 LoC. Verify compile + at least one cell GREEN at write time (positive-control sanity); the rest verified at T020.
- [ ] T018 [P] [US3] Write `tests/session/admin_builder_distinct_now_test.cpp` (TDD red per FR-007 + SC-006):
  - For each admin builder (Heartbeat / TestRequest / Logout / Reject), one test that: drives the session to Active; captures the SendingTime of an emitted admin frame; calls `mock_clock->advance(std::chrono::seconds(1));`; triggers a second emit of the same admin type; captures the second SendingTime; asserts the two values are distinct (per-message-distinct branch exercised).
  - Add to `tests/session/CMakeLists.txt`. Estimated 80-120 LoC. Verify GREEN (the production code already populates distinct SendingTime per PR #82 RC#G; the test exercises the uncovered branch).
- [ ] T019 [P] [US3] Write `tests/session/admin_emit_mixed_path_test.cpp` (TDD red per FR-008 + SC-005):
  - Identify admin emit sites 1+2 from the PR #82 round-2 RC#G fix (re-grep `assign_outbound` in `src/session/session.cpp` — the round-2 fix added gating at 8 sites; sites 1+2 are the two earliest, per the round-2 commit `1ae894d`/`89ee47d` notes in the 009 Gate B decision record). At each site, four tests exercise: (Reject-ok, Logout-ok), (Reject-fail, Logout-skip), (Reject-ok, Logout-fail), (Reject-fail, Logout-fail).
  - For each combo: inject a controlled `assign_outbound` failure (via the SeqnumManager test-double's failure injection) for the Reject and/or Logout step; assert the gated-emit contract: when Reject fails, Logout is skipped; when Logout fails after Reject succeeded, the session still reaches the documented end state per 005 data-model.
  - Add to `tests/session/CMakeLists.txt`. Estimated 150-200 LoC. Verify GREEN (production already implements the contract; the test exercises uncovered permutations).
- [ ] T020 [US3] Build + run `session_fsm_matrix_witness` + `session_admin_builder_distinct_now` + `session_admin_emit_mixed_path` under `linux-clang-debug`: `ctest --test-dir build/linux-clang-debug -V -R '^session_(fsm_matrix_witness|admin_builder_distinct_now|admin_emit_mixed_path)$'`. Expected: ALL GREEN. If any FSM matrix cell fails, the implementation drifted from the 005 data-model — surface as an /speckit-analyze finding.

**Checkpoint**: US3 delivers F-06 + F-05 + RC#G mixed-path — exhaustive matrix coverage + per-message-distinct-time coverage + gated-emit permutation coverage.

---

## Phase 6: User Story 4 — Initiator transport-throw witness (Priority: P3)

**Goal**: F-11 — symmetric witness to the acceptor-side transport-throw test landed in PR #82 round 1; covers the initiator open() path when `transport.send` throws during the outbound Logon emit.

**Independent Test**: build the 1 acceptance scenario in spec.md US4.

- [ ] T021 [US4] Write `tests/session/initiator_transport_throw_test.cpp` (TDD red per FR-009 + SC-007):
  - Test `InitiatorOpen_TransportThrowsOnLogonEmit_ReturnsDocumentedError`: configure an initiator session with a transport double whose `send(...)` throws on the first call; call `co_await session.open()`; assert the result is `std::unexpected(<documented error>)` (per the 005 design — likely a `transport_*` error variant); assert the FSM end-state matches the 005 design (likely Disconnected, but verify against `data-model.md` of 005). Mirror the acceptor-side existing test for the assertion shape.
  - Add to `tests/session/CMakeLists.txt`. Estimated 60-80 LoC. Verify GREEN — the contract is already wired (open()'s coroutine-catches per the 005 design).

**Checkpoint**: US4 delivers F-11 — initiator/acceptor symmetry on transport-throw coverage.

---

## Phase 7: Polish & Cross-cutting Concerns

**Purpose**: validate the slice against constitutional gates; produce evidence for Gate B precondition; bookkeeping.

**These tasks are orchestrator-inline per `[[feedback_phase_implementer_sonnet_runaway_scope]]`** — do NOT spawn Sonnet for polish tasks; the parent session runs them.

- [ ] T022 [P] Re-run Tier-1 sanitizer matrix serially per `[const §IX.6]`: `for preset in linux-clang-debug linux-clang-release linux-clang-asan linux-clang-ubsan linux-clang-tsan linux-clang-coverage linux-gcc-release; do cmake --preset "$preset" && cmake --build "build/$preset" -j && ctest --test-dir "build/$preset" -V -R 'session_|tc_'; done`. Expected: ALL GREEN. Capture the per-preset pass count; record in /speckit-verify decision record at T028.
- [ ] T023 [P] Coverage gate per `[const §IX.1]` (lcov DA / BRDA basis) on `src/session/session.cpp` + `include/fixpp/session/session.hpp`. Expectation per plan.md §IX.1: coverage RISES into the 009 W-1..W-4 envelope or BETTER (the new FR-006 + FR-007 + FR-008 + FR-009 tests close cascading-defensive arms; the new FR-004 visit-history seam closes a previously-unreachable observation primitive). If residual gaps remain, carry them as 005-style Article IX §1 waivers, NOT new waivers. Generate `coverage.lcov` per quickstart.md; record threshold dispositions in T028.
- [ ] T024 [P] Bench gate per `[const §VIII.2]`: build `fsm_bench seqnum_bench fix_time_bench heartbeat_bench` on `linux-clang-release`; run; diff against `bench/baselines/session/*_baseline.json` with ±5% tolerance. The slice's edits should fit within the existing ceilings per plan.md Technical Context (one-time ctor copy + 1ns/transition ring-buffer push) — flag any genuine regression for /speckit-verify dispositioning at T028.
- [ ] T025 [P] Static analysis sweep on touched files: clang-tidy + clang-format + cppcheck + IWYU per `[const §IX.4]`; `[const §XV.9]` mutex-in-awaitable grep gate (`tests/sync/check_no_std_mutex_corpus` GREEN). Touched files per plan.md Project Structure (session.hpp, session.cpp, error.hpp, session_config.hpp + the new test files). Capture findings; ride-along fixes only (no broader cleanup per `[[feedback_phase_implementer_sonnet_runaway_scope]]` surgical-changes discipline).
- [ ] T026 Invariant I-25 grep gate: confirm every `fsm_state_ =` assignment in `src/session/session.cpp` is INSIDE the body of `record_state_transition_()` (the only legitimate site). Search: `grep -nE '\bfsm_state_\s*=\s*' src/session/session.cpp` — expected output = exactly one line (the assignment inside the helper). If any other site appears, surface as a regression and add the missing helper-routing.
- [ ] T027 Spec / catalogue / coverage-index bookkeeping per `[[feedback_pipeline_mark_done_step]]`:
  - Add a line to `spec/coverage-index.md` under the 005 / 009 session entries: `010-session-cfg-lifetime closed the W-5 + F-04 + F-05 + F-06 + F-07/E1 + F-11 + RC#G-mixed-path waivers on PR #82 Gate B.`
  - In `library/.specify/decisions/009-session-fsm-finalize-gateb.md`: annotate the W-5 row (and F-04/F-05/F-06/F-07/E1/F-11/RC#G rows if listed) `CLOSED — see PR #<N>` once the 010 PR opens (do this at the /gate-b step, not here; this task records the future-pointer).
  - Update `spec/feature-catalogue.md` row(s) touched by 010 to `done` (per `[const §I.3]` row 005/2g session-establishment catalogue; if a separate catalogue row exists for the test-coverage seams, mark it too).
- [ ] T028 Feature-completeness audit per `[const §XVII.8]` + `[[project_005_phase8_completeness_false_pass]]` + `[[feedback_simplify_pass_catches_9th_burn]]` — audit **test BODIES** (not file names): for each FR-001 through FR-009, identify the test file + the specific assertions that BIND the runtime contract; verify no SUCCEED-placeholders, no test that passes for the wrong reason, no end-state assertion that matches via the wrong matrix row. Cross-reference SC-001 through SC-009 the same way. Output a `## Completeness` section in the /speckit-verify record OR a sibling `010-session-cfg-lifetime-completeness.md` per the verify-record's choice.
- [ ] T029 Run `/speckit-verify 010-session-cfg-lifetime` per `[const §XVII.8]`. Expected verdict: GREEN (the W-5 + bundled-deferral closure is purely additive coverage + a surgical refactor — no regressions expected). If YELLOW: list waivers (likely auto-clear of W-1..W-4 OR re-waive with rationale per `[[feedback_codecov_patch_vs_lcov_da_brda_gate]]` + PR #73 precedent). RED is unexpected; surface and STOP.

**Checkpoint**: Phase 7 produces the evidence pack required by /gate-b precondition (verify record GREEN or YELLOW; completeness audit 100% or fully waived; sanitizer matrix all GREEN; static analysis clean).

---

## Dependencies

**Story-completion order**:
- Phase 1 (T001-T002) — pre-flight verification, no source change.
- Phase 2 (T003-T005) — foundation; **blocks** Phase 3-6.
- Phase 3 (US1, P1) — MVP; **independent** of Phase 4/5/6 (only US1 closes W-5).
- Phase 4 (US2, P2) — depends on Phase 2 (visit history + new error variant); **independent** of Phase 3 (US2 doesn't need W-5; it works on the existing reference-cfg session).
- Phase 5 (US3, P2) — depends on Phase 2 (visit history); **independent** of Phase 3 + Phase 4.
- Phase 6 (US4, P3) — independent of all earlier user-story phases (it's purely a new test file).
- Phase 7 — depends on Phases 1-6 (validation/bookkeeping).

**Parallel opportunities per story**:
- Phase 2: T003 + T004 are different files and can run [P]. T005 must follow T004.
- Phase 3: T006 + T007 are different files and can run [P]. T008 → T009 → T010 → T011 must be sequential (same files + ASan verification).
- Phase 4: T012 + T013 are different test files and can run [P]. T014 → T015 sequential.
- Phase 5: T016 → T017 sequential (T017 depends on the cell enumeration). T018 + T019 + T020 are different test files and can run [P] after T017 lands. T020 verifies all three new test files.
- Phase 6: T021 is standalone.
- Phase 7: T022 + T023 + T024 + T025 are different commands and can run [P]. T026 + T027 + T028 sequential. T029 last.

## Implementation strategy

**MVP first** (Phase 1 + 2 + 3): ship US1 alone closes W-5 — the load-bearing PR #82 Gate B waiver. The bundled deferrals (US2/3/4) are nice-to-haves; if a time pressure arises and US1 is GREEN, the slice could ship MVP-only and the residuals re-bundle into a 011-session-coverage-followup slice. Per the user instruction at /speckit-clarify-time, the current intent is to land all four user stories in one slice; this fallback is documented only.

**Incremental delivery**: each Phase 3-6 user-story phase produces a passing ctest in its own right. Spawn one `phase-implementer-sonnet` per phase per `[[feedback_speckit_subagent_phasing]]` + `[[feedback_phase_implementer_sonnet_runaway_scope]]` (cap LoC per call, one phase per invocation, anti-hang clause). Parent re-runs `ctest -V -R 'session_|tc_'` between phases per `[[feedback_self_run_build_gate]]`.

**Risk tracking**: the matrix witness file (T017, ~400-500 LoC) is the largest single deliverable; if it exceeds the Sonnet-invocation LoC cap, split T017 into per-state batches (one Sonnet invocation per FSM state's row).
