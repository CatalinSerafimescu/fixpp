---
description: "Task list for 009-session-fsm-finalize (drift closure against 005 design)"
---

# Tasks: 009-session-fsm-finalize

**Input**: Design documents from `/specs/009-session-fsm-finalize/` ([spec.md](spec.md), [plan.md](plan.md), [research.md](research.md), [data-model.md](data-model.md), [quickstart.md](quickstart.md), [contracts/session_role.hpp](contracts/session_role.hpp))
**Prerequisites**: plan.md (✅), spec.md (✅), research.md (✅), data-model.md (✅), contracts/ (✅)
**Tests**: REQUIRED — every FR requires a runtime-behavior test per FR-012 + `[const §VII.1]` red-green-refactor + the test-bodies discipline from `[[project_005_phase8_completeness_false_pass]]`. NOT optional for this slice.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: parallelizable (different files, no dependencies on incomplete tasks)
- **[Story]**: which user story (US1/US2/US3/US4) — only on Phase 3–6 tasks
- File paths are absolute-relative to `library/` submodule root

## Path conventions

- C++ source: `include/fixpp/session/`, `src/session/`
- Tests: `tests/session/`, `tests/session/conformance/`, `tests/session/support/`
- Decisions: `.specify/decisions/`

---

## Phase 1: Setup (verify clean starting state)

**Purpose**: confirm the branch is positioned correctly and the inherited 005 work is green before any 009 edit lands. No source changes.

- [X] T001 Verify branch state: confirm `git branch --show-current` returns `009-session-fsm-finalize`, confirm `git log --oneline 4e621e1..HEAD` shows zero 009 commits yet (untouched base), confirm `git status` is clean. If any check fails, STOP and surface to user.
- [X] T002 [P] Baseline test run: `ctest --test-dir build/linux-clang-debug -R 'session_|tc_'` against the inherited HEAD `4e621e1`. Capture pass/fail tally. Expected: all GREEN per 005 /speckit-verify YELLOW (zero unwaived FAILs). If any session test is RED at baseline, STOP and surface — that's a pre-existing 005 issue this slice did not introduce.

---

## Phase 2: Foundational (blocking prerequisites for all user stories)

**Purpose**: tiny shared API surface additions + the test helper used by FR-013 and concurrency tests. Foundational because all four user stories' tests/impl reference these.

**⚠️ CRITICAL**: No user story work can begin until T003 + T004 + T005 are merged.

- [X] T003 [P] Add `session_role` enum + `SessionConfig::role` field to `include/fixpp/session/session_config.hpp` per `contracts/session_role.hpp` (D-1, FR-004 enabler). Default value `session_role::initiator` so existing 005 tests + integrations compile unchanged. Add `#include <cstdint>` if not already present.
- [X] T004 [P] Add `std::uint32_t next_test_request_id_ = 0;` private member to `include/fixpp/session/session.hpp` (D-3, FR-010 enabler). Place near the existing per-session state members (search for `private:` block). Do NOT touch `run_liveness_loop` yet — that's T020.
- [X] T005 [P] Add test helper `tests/session/support/frame_field_extract.hpp` + `frame_field_extract.cpp` exposing `std::optional<std::string_view> extract_field(std::span<const std::byte> frame, std::uint16_t tag) noexcept;` for extracting any FIX tag's value from a captured outbound frame. Used by FR-013 + US3 Reject assertions + US4 TestReqID-equality assertions. **CMake placement (per analyze finding C2):** add the new files inline to the existing `tests/session/CMakeLists.txt` as an OBJECT library `session_test_support` — do **NOT** create a new `tests/session/support/CMakeLists.txt` + `add_subdirectory()` (one less moving part; matches the existing 005 `tests/support/` pattern of inline-add-to-parent). Then link `$<TARGET_OBJECTS:session_test_support>` from each session test executable.

**Checkpoint**: Foundation ready — user story phases (3-6) can now proceed. Sequential is recommended (all edit `src/session/session.cpp`); parallel possible only if different files.

---

## Phase 3: User Story 1 — Send an application message that actually reaches the counterparty (Priority: P1) 🎯 MVP

**Goal**: `Session::send` is wired end-to-end (assign + stamp + build + store + emit); admin builders thread negotiated `BeginString` + `effective_clock.now()` instead of hardcoded defaults. Closes RC#1 + RC#4. After this phase, the library is actually usable for outbound application traffic on FIX.4.2 + FIX.4.4 sessions.

**Independent Test**: build the 4 acceptance scenarios in spec.md US1; assert against the test-double `MessageStore` + transport double + `mock_clock`. The library is usable for outbound app messages IFF this phase passes.

**MVP scope: just this phase.** US1 alone makes the library shippable for the most-common use case (FIX-initiating client wanting to send NewOrderSingle / etc.).

- [X] T006 [US1] Write `tests/session/send_path_test.cpp` per FR-001 acceptance scenarios 1–4 (TDD red): (a) Active FIX.4.4 session with seqnum_out=7 → `Session::send(payload)` → on-wire frame has `34=8`, `52=<mock_clock_now>`, `8=FIX.4.4`, store gets frame at seq=8 strictly BEFORE the transport double; (b) FIX.4.2 session variant — assert `8=FIX.4.2`; (c) cancelled transport → defined error returned, session reaches Disconnected; (d) 5 admin builders fire — every emitted frame has the negotiated BeginString AND `52=mock_clock_now`. Use the T005 `extract_field` helper. Add to CMake. Verify the test compiles and FAILS as expected (red).
- [X] T007 [US1] Extend admin-builder signatures in `include/fixpp/session/admin_messages.hpp` so every builder accepts `std::string_view sending_time` and every builder that does NOT already accept `std::string_view begin_string` gains one (FR-002 / FR-003). **Note (analyze finding C1):** `build_logon` (line 40) already has `begin_string` — add only `sending_time` to it. The other 4 builders (`build_logout`, `build_heartbeat`, `build_test_request`, `build_reject`) need BOTH params added. In `src/session/admin_messages.cpp`: delete `kBeginStringDefault` + `kSendingTimePlaceholder` constants; thread the new params into each `build_*` body so they appear in tag 8 + tag 52 of every emitted frame. Update all 9 call sites in `src/session/session.cpp` (Opus triage lines: 322, 657, 671, 715, 880, 1054, plus `run_logout_phase1` etc. — search for `build_logon(`, `build_logout(`, `build_heartbeat(`, `build_test_request(`, `build_reject(`) to pass `cfg_.begin_string` + a per-call-site stamped SendingTime (use the existing `stamp_sending_time(*effective_clock_)` helper). Verify the build is GREEN; the existing `tc_*` conformance tests still pass (they don't check tag 8/52 yet — that's T009).
- [X] T008 [US1] Wire `Session::send` body in `src/session/session.cpp:941-949` per FR-001 + Opus triage RC#1: (1) `co_await seqnum_mgr_.assign_outbound()` for the next outbound seqnum; (2) `stamp_sending_time(*effective_clock_)` for tag 52; (3) build the framed wire bytes using the existing Writer pattern (alloc-free per `[const §VIII.5]`); (4) `co_await store_->store(seq, committed_frame, direction::outbound)` BEFORE transport per `[2e §4.1]`; (5) `co_await transport_send_(committed_frame)`. Return `expected_t<void>` with defined error on any step's failure. Verify T006 GREEN.
- [X] T009 [P] [US1] Extend outbound assertions in `tests/session/conformance/tc_logout_test.cpp` + `tc_reject_test.cpp` + `tc_liveness_test.cpp` + `tc_sendingtime_test.cpp` + `tc_establishment_test.cpp` per FR-013: every place that captures an outbound frame and asserts on tag 35 / 372 / etc., ALSO assert tag 8 equals the configured BeginString AND tag 52 equals `mock_clock_now_formatted` (use the T005 `extract_field` helper). Verify all tc_* binaries GREEN.

**Checkpoint**: US1 delivers the outbound emit MVP — library is usable for outbound application traffic.

---

## Phase 4: User Story 2 — Deploy an acceptor session that waits for inbound Logon (Priority: P1)

**Goal**: `SessionConfig::role` drives `Session::open` initial state. Acceptor sessions wait for peer Logon and traverse `NotConnected → LogonReceived → Active`. Closes RC#2.

**Independent Test**: build the 4 acceptance scenarios in spec.md US2; assert against a peer-Logon feed via the transport double. Acceptors deployable IFF this phase passes.

- [ ] T010 [US2] Extend `tests/session/logon_handshake_test.cpp` with `AcceptorOpenStaysInNotConnected` + `AcceptorValidPeerLogonReachesActiveViaLogonReceived` + `InitiatorOpenStillReachesLogonSentAndEmitsLogon` tests (TDD red per FR-004/FR-005 acceptance scenarios 1–3). Update `make_acceptor_cfg` to set `cfg.role = session_role::acceptor`; update `make_initiator_cfg` to set `cfg.role = session_role::initiator` (explicit, even though default). For the `…ViaLogonReceived` test, observe the intermediate state via a state-trace hook OR via a synchronous deterministic `state()` snapshot after `on_inbound_frame` returns and before the strand re-enters. Verify red.
- [ ] T011 [US2] Branch `Session::open` in `src/session/session.cpp:230-238` on `cfg_.role` per Opus triage RC#2 + D-1. **Initiator arm** (per analyze findings B1 + E1, clarified): set `fsm_state_ = LogonSent`, then emit the initial Logon by calling `build_logon(out_buf, next_outbound_seq_++, cfg_.sender_comp_id, cfg_.target_comp_id, cfg_.begin_string, cfg_.heartbt_int, stamp_sending_time(*effective_clock_))` followed by `store_then_emit(logon_frame)` — this is the SAME `build_* + store_then_emit` infrastructure as T007's admin builders, **NOT** the user-facing `Session::send(app_payload)` path wired by T008 (the two paths are distinct: `send` is for opaque application payload bytes, the initial Logon is admin emission via the builder + store path). **Acceptor arm:** set `fsm_state_ = NotConnected`, emit no outbound Logon (wait for peer Logon via `on_inbound_frame`). Verify T010 GREEN.
- [ ] T012 [P] [US2] Rewrite `tests/session/conformance/tc_establishment_test.cpp` Scenario1a_ValidLogon_fix42 + Scenario1b_ValidLogon_fix44 acceptor cases per FR-005: stop calling `open_session(sess)` before feeding the peer Logon — instead configure `role=acceptor` and let `on_inbound_frame(peer_logon)` drive the `NotConnected → LogonReceived → Active` matrix row. Add an assertion that `sess.state()` was observed at `LogonReceived` at some point during the transition (e.g., via a state-trace hook). Verify the conformance suite still passes.

**Checkpoint**: US2 delivers acceptor support — library is deployable as the server side of a FIX link.

---

## Phase 5: User Story 3 — FSM and inbound validation match the Gate-A-converged contract (Priority: P2)

**Goal**: Refused first Logon → Disconnected (not preserved-in-NotConnected). Missing/malformed inbound `SendingTime` triggers Reject per contract; LogonSent special: Logout-with-error per D-3. Closes RC#3 + RC#5.

**Independent Test**: build the 5 acceptance scenarios in spec.md US3; assert each guard fires the documented path. Spec conformance restored IFF this phase passes.

- [ ] T013 [US3] Flip `tests/session/fsm_transition_matrix_test.cpp::NotConnected_RefusedLogonByBeginString_StaysInNotConnected` assertion to expect `fsm_state::Disconnected` per FR-006 (TDD red). Rename the test → `NotConnected_RefusedLogonByBeginString_ReachesDisconnected`. Also flip `NotConnected_RefusedLogonByCompID_*` if present (Opus triage mentions both validations). Verify red.
- [ ] T014 [US3] Drop the Phase-3 `is_logon` compromise in `src/session/session.cpp:528-585` per Opus triage RC#3 + FR-006: every refusal on the `NotConnected` row sets `fsm_state_ = fsm_state::Disconnected` (no MsgType discrimination). Remove the inline comment that documents the Phase-3 compromise. Verify T013 GREEN. Re-check `tests/session/conformance/tc_establishment_test.cpp` refusal scenarios: any `EXPECT_NE(LogonReceived)` should now ALSO assert `EXPECT_EQ(Disconnected)` per the matrix; update if needed.
- [ ] T015 [P] [US3] Write `MissingSendingTimeInActiveRejects` + `MalformedSendingTimeInActiveRejects` + `MissingSendingTimeInLogonReceivedRejects` in `tests/session/sending_time_test.cpp` (TDD red per FR-007/FR-008). Each test: feed an Active (or LogonReceived) inbound frame with tag 52 empty (or `52=abc`); assert outbound `Reject(SessionRejectReason=10, RefTagID=52)` was emitted (use T005 `extract_field` to check the VALUE of tag 371 equals the string `"52"` — i.e., the rejected tag is SendingTime — AND the VALUE of tag 373 equals the string `"10"` — i.e., SessionRejectReason=10), then outbound `Logout`, then session reached `Disconnected`. Verify red. (Analyze finding F1: notation `371=52` in FIX wire grammar means "field 371's value is `52`", not "tag 371 equals tag 52".)
- [ ] T016 [US3] Tighten the Active/LogonReceived `SendingTime` guard in `src/session/session.cpp:638-684` per Opus triage RC#5 + FR-007/FR-008: rewrite the guard so empty `hdr.sending_time` OR parse-failure both fall through to the existing Reject-Logout-Disconnect path. Remove the `// parse failure: lenient on malformed timestamp` comment + fall-through. Verify T015 GREEN.
- [ ] T017 [P] [US3] Write `MissingSendingTimeOnLogonEmitsLogoutOnly` + `MalformedSendingTimeOnLogonEmitsLogoutOnly` in `tests/session/sending_time_test.cpp` (TDD red per FR-009). Each test: send an inbound Logon (LogonSent state) with `52` empty (or malformed); assert outbound `Logout(58=<error text>)` emitted (no standalone Reject — D-3 LogonSent-special), session reached `Disconnected`. Verify red.
- [ ] T018 [US3] Tighten the LogonSent `SendingTime` guard in `src/session/session.cpp:867-892` per Opus triage RC#5 + FR-009: rewrite the guard so empty `hdr.sending_time` OR parse-failure both go to the Logout-with-error path (NO standalone Reject pre-establishment). Remove the `// parse failure: accept the Logon (lenient on malformed timestamp)` comment. Verify T017 GREEN.

**Checkpoint**: US3 delivers spec-conformance correctness on the FSM matrix + inbound `SendingTime` validation.

---

## Phase 6: User Story 4 — Two sessions running concurrently are race-free and tear down without `std::terminate` (Priority: P2)

**Goal**: Per-session TestReqID counter (no cross-session static); `SeqnumManager::drain()` called in `Session::close` phase 2. Closes RC#6 + RC#7. Multi-session deployments are safe IFF this phase passes.

**Independent Test**: build the 3 acceptance scenarios in spec.md US4; run under TSan + via gtest death-test or surviving harness.

- [ ] T019 [US4] Write `tests/session/test_test_request_id_cross_session_race.cpp` (TDD red per FR-010 + SC-003). Setup: `asio::thread_pool(4)`, construct two `Session` instances both configured with `HeartBtInt=1s` + `mock_clock`; co_spawn both sessions' `run_liveness_loop` concurrently; tick `mock_clock` forward 10⁴ times so each session emits ≥ 10⁴ TestRequests; capture every outbound TestRequest's `112=<id>` via the transport double + T005 `extract_field` helper. Assertions: (a) session A's TestReqID sequence is disjoint from session B's; (b) within each session, IDs increment monotonically from `1`; (c) TSan reports zero data races on the counter. Add as a TSan-only ctest entry (gated on the `linux-clang-tsan` preset). Verify red — the existing impl uses `static tr_counter` so TSan will fire AND the sequences will interleave.
- [ ] T020 [US4] Replace `static std::uint32_t tr_counter = 0;` with `++next_test_request_id_` (the member added in T004) in `src/session/session.cpp:1040,1045` per Opus triage RC#6 + FR-010 + D-3. Verify T019 GREEN under TSan.
- [ ] T021 [US4] Write `tests/session/test_seqnum_drain_on_close.cpp` (TDD red per FR-011 + SC-004 acceptance scenarios 1–3). Test 1 — `CloseWithHolderDoesNotTerminate`: acquire the seqnum async_mutex via `Session::check_inbound` (the existing entry that locks the counter), then call `Session::close(graceful)`, then destroy the Session. Use a gtest death-test OR a surviving asio harness; assert no `std::terminate` AND no UAF AND `state() == lifecycle::closed_drained` post-close. Test 2 — `NeverOpenedDestructionSafe`: construct a Session, never call `open()`, destroy it; assert no terminate (drain succeeds trivially on no-holders mutex). Verify red — current impl never calls `drain()` so test 1 fires `std::terminate`.
- [ ] T022 [US4] Add `co_await seqnum_mgr_.drain()` in `Session::close` phase 2 per Opus triage RC#7 + FR-011 + D-2. Placement: in `src/session/session.cpp:249-372` close path, AFTER `root_cancel_.emit(...)` + `trace_slot_.clear()`, BEFORE `state_ = closed_drained`. Drain result is consumed with `(void)drain_r;` — drain failures are logged via the existing session-error callback (per D-2 logged-then-proceed policy) but do NOT abort the close. Verify T021 GREEN; re-run T019 under TSan to confirm no regression.

**Checkpoint**: US4 delivers cross-session concurrency safety + teardown correctness — multi-session deployments are safe.

---

## Phase 7: Polish & Cross-cutting Concerns

**Purpose**: validate the slice against constitutional gates; produce evidence for Gate B precondition; bookkeeping.

**These tasks are orchestrator-inline per `[[feedback_phase_implementer_sonnet_runaway_scope]]`** — do NOT spawn Sonnet for polish tasks; the parent session runs them.

- [ ] T023 [P] Re-run Tier-1 sanitizer matrix serially per `[const §IX.6]`: `for preset in linux-clang-{debug,release,asan,ubsan,tsan,coverage} linux-gcc-release; do cmake --preset "$preset" && cmake --build "build/$preset" && ctest --test-dir "build/$preset" -V -R 'session_|tc_'; done`. Expected: ALL GREEN. Capture the per-preset pass count; record in /speckit-verify decision record at T028.
- [ ] T024 [P] Coverage gate per `[const §IX.1]` (lcov DA / BRDA basis) on `src/session/session.cpp` + `src/session/admin_messages.cpp` + `include/fixpp/session/session.hpp` (the 005 W-1..W-4 files). Expectation per plan.md: coverage RISES into the W-1..W-4 envelope or BETTER (the new FR-007/008/009 tests close cascading-defensive arms; the new FR-001/004/006/010/011 tests close previously-unreachable production paths). If residual gaps remain, carry them as 005-style Article IX §1 waivers, NOT new waivers. Generate `coverage.lcov` per quickstart.md §5; record threshold dispositions in T028.
- [ ] T025 [P] Bench gate per `[const §VIII.2]`: build `fsm_bench seqnum_bench fix_time_bench heartbeat_bench` on `linux-clang-release`; run; diff against `bench/baselines/session/*_baseline.json` with ±5% tolerance. The slice's edits should fit within the existing ceilings per plan.md Technical Context — flag any genuine regression for /speckit-verify dispositioning at T028.
- [ ] T026 [P] Static analysis sweep on touched files: clang-tidy + clang-format + cppcheck + IWYU per `[const §IX.4]`; `[const §XV.9]` mutex-in-awaitable grep gate (`tests/sync/check_no_std_mutex_corpus` GREEN). Touched files per plan.md Project Structure source-code edits — limit the scan to those + the new test files. Capture findings; ride-along fixes only (no broader cleanup per `[[feedback_phase_implementer_sonnet_runaway_scope]]` surgical-changes discipline).
- [ ] T027 **Feature-completeness audit (T067-equivalent, test-bodies discipline)** per `[const §XVII.8]` + `[[project_005_phase8_completeness_false_pass]]`. For each FR-001..FR-013: (a) locate the test file(s) claiming to cover it; (b) grep the test body for the contract-assertion content per the mapping table below — all listed assertions MUST appear in the body; (c) static_assert / noexcept / SUCCEED placeholders are INSUFFICIENT for any FR whose primary deliverable is a runtime behavior — such mappings FAIL the audit; (d) emit verdict to `library/.specify/decisions/009-session-fsm-finalize-completeness.md` in the format of `008-message-store-completeness.md`; verdict = PASS only when all 13 FRs have test BODIES matching the contract.
  **FR-to-required-assertion-content mapping (009 FRs):**
  - FR-001: assert tag 34 incremented by exactly 1 AND tag 52 = mock_clock_now_formatted AND tag 8 = configured_begin_string AND store sees frame BEFORE transport double sees it.
  - FR-002: assert tag 8 = cfg_.begin_string on every admin-builder outbound frame (not "FIX.4.2" hardcoded); assert for at least 2 distinct begin_string values (FIX.4.2 + FIX.4.4).
  - FR-003: assert tag 52 = effective_clock.now() formatted (not "00000000-00:00:00.000") on every admin-builder outbound frame; assert via mock_clock comparison.
  - FR-004: assert session.state() == NotConnected after Session::open() when role=acceptor AND no outbound Logon emitted; assert LogonSent + outbound Logon when role=initiator.
  - FR-005: assert session traverses NotConnected → LogonReceived → Active when valid peer Logon arrives; assert LogonReceived was observed (intermediate state, not just final).
  - FR-006: assert session.state() == Disconnected (not NotConnected) after refused inbound Logon on NotConnected row.
  - FR-007: assert outbound Reject with tag 373=10 AND tag 371=52, followed by Logout, followed by Disconnected, when Active/LogonReceived receives inbound frame with tag 52 absent. (In test source these assertions appear as `extract_field(frame, 371) == "52"` and `extract_field(frame, 373) == "10"` per T005's helper — grep on those substrings, NOT on the FIX wire-grammar literal `371=52`.)
  - FR-008: assert same Reject(373=10, 371=52) + Logout + Disconnected path when Active/LogonReceived receives inbound frame with tag 52 present but malformed. (Same `extract_field`-based grep substrings as FR-007.)
  - FR-009: assert outbound Logout with tag 58 present AND no standalone Reject (no outbound frame with tag 35=3 before Logout) when LogonSent receives Logon with missing or malformed tag 52; assert Disconnected.
  - FR-010: assert per-session counters come from disjoint sequences when two sessions run concurrently AND TSan reports zero races on the counter (not a static).
  - FR-011: assert no std::terminate when Session is closed with an active async_mutex holder; assert state == closed_drained post-close; assert never-opened destructor completes safely.
  - FR-012 (meta): verified by the presence of runtime-behavior assertions for all other FRs above; no direct assertion content.
  - FR-013: assert tag 8 AND tag 52 present on every outbound frame in each of the 5 named tc_*_test.cpp files.
  **For SC-006 coverage of the 005-inherited FRs (not owned by 009 but included in SC-006):** map each 005 FR using the same assertion-body rule against 005/spec.md §Functional Requirements — locate test bodies, grep for contract-specific field assertions (not just compile-time checks). The same "no SUCCEED placeholder" rule applies.
- [ ] T028 `/speckit-verify 009-session-fsm-finalize` — emit `library/.specify/decisions/009-session-fsm-finalize-verify.md` per `[const §XVII.8]`. Consume T023/T024/T025/T026 evidence + T027 completeness verdict. Required verdict for Gate B: non-RED **(closes SC-005 — non-RED required for Gate B label per analyze finding E2)**. The verify record MUST include W-1..W-4 disposition (still in force, in force with updated coverage, or no-longer-needed per T024) and a `## Completeness` section linking to T027's record. The W-1..W-4 numeric thresholds are quantified in `library/.specify/decisions/005-session-establishment-fsm-verify.md` — read the §Waivers table there to source the comparison values.
- [ ] T029 Update `library/spec/coverage-index.md` § "005 session-establishment — scope-deferral ledger" with a single close-out line: "**009-session-fsm-finalize (2026-05-22):** closed FR-001 / FR-002 / FR-003 / FR-011 / FR-013 drift on 005's rows; 005 deferral ledger unchanged (no new deferral, no green-via-009 of a deferred case)." Per `[const §VI.4]`/`[const §VI.5]` bidirectional traceability.
- [ ] T030 `/gate-b <PR>` invocation (either refresh PR #81 OR open a fresh PR for `009 → main` per user choice at merge time). Per Gate B preconditions: T028 verify record must be non-RED; T027 completeness audit verdict must be PASS or fully-waived. At Gate B convergence: apply `gate-a-done` (transitively from 005's gate-a sign-off per `[const §XVII.8]` paired evidence) + `gate-b-done`/`gate-b-waived` per outcome. Push 009 only on explicit user instruction.

**Checkpoint**: Slice ready for merge bookkeeping per quickstart.md §9 (catalogue row-flips → done, submodule pointer bump, project memory append).

---

## Dependencies

```text
Phase 1 (Setup) — T001, T002
  ↓
Phase 2 (Foundational) — T003, T004, T005 [all P parallel]
  ↓
  ├── Phase 3 (US1) — T006 → T007 → T008 → T009 [P9 parallel to next phase]
  │     (MVP delivery slice)
  ├── Phase 4 (US2) — T010 → T011 → T012 [P12 parallel to next phase]
  │     (sequential against phase 3 only because both edit session.cpp)
  ├── Phase 5 (US3) — T013 → T014 → T015 → T016 → T017 → T018
  │     (sequential against phase 3/4 because session.cpp same-file edits)
  └── Phase 6 (US4) — T019 → T020 → T021 → T022
        (sequential against phase 3/4/5 because session.cpp same-file edits;
         T019 + T021 are TDD-red tests, can be authored independently)
  ↓
Phase 7 (Polish) — T023, T024, T025, T026 [P parallel]
  ↓
  T027 (completeness, ORCHESTRATOR-INLINE)
  ↓
  T028 (/speckit-verify) — consumes T023..T027 evidence
  ↓
  T029 (coverage-index.md bookkeeping)
  ↓
  T030 (/gate-b)
```

**Practical parallelism**: T003 / T004 / T005 are truly parallel (different files). T009 (extending tc_* test assertions) can be authored in parallel with T010 (US2 test authorship) because they touch different test files. T015 + T017 (US3 test authorship) are parallel because they're in the same file but different test functions — author together in one editor session. T019 + T021 (US4 test authorship) are parallel (different files).

**Source-file conflict zone**: `src/session/session.cpp` is touched by T007, T008 (US1), T011 (US2), T014, T016, T018 (US3), T020, T022 (US4) — 8 of 17 phase-3-to-6 tasks. Sequential within session.cpp is recommended to avoid 3-way merge friction; the phases can complete in user-priority order (US1 first as MVP).

---

## Implementation strategy

**MVP scope:** Phase 1 + Phase 2 + **Phase 3 (US1) only** is a viable mid-flight checkpoint — the library becomes usable for outbound application traffic on FIX.4.2/4.4 initiator sessions. Could ship this slice as `009a-emit-only` if scoping pressure forces a split (per spec.md US1 "MVP" annotation).

**Incremental delivery:** US1 → US2 → US3 → US4 in priority order. After each user-story phase, run the affected ctest subset (`ctest --test-dir build/linux-clang-debug -V -R '<phase-specific-pattern>'`) to confirm GREEN before opening the next phase per `[[feedback_self_run_build_gate]]`. The 005-inherited tests must remain GREEN throughout (T002 baseline as the reference).

**Subagent phasing per `[[feedback_speckit_subagent_phasing]]`:** spawn one `phase-implementer-sonnet` per phase 3/4/5/6 (not per task). Each phase brief is the phase's task list + the FR citations + the Opus-triage RC reference + the test-bodies discipline reminder. Parent (orchestrator) runs Phase 1, Phase 2, and Phase 7 inline.

**TDD ordering per `[const §VII.1]` + `[[feedback_subagent_phase_verification_two_traps]]`:** in every user-story phase, the test task (T006 / T010 / T013+T015+T017 / T019+T021) MUST land first as RED. The impl task only commits AFTER demonstrating green-from-red. No SUCCEED-placeholder tests; no end-state-only assertions where intermediate-transition assertions are required.

**Anti-pattern reminder per `[[project_005_phase8_completeness_false_pass]]`:** at T027 audit time, every FR's "test exists" claim is verified by reading the test BODY against the contract — not the file name, not the FR-listing in tasks.md, not the `[X]` mark. The 005 audit failed because it stopped at existence-mapping; 009 will not.

---

## Validation summary

| Aspect | Status |
|---|---|
| Total task count | **30** (T001–T030) |
| Setup phase | 2 tasks (T001, T002) |
| Foundational phase | 3 tasks (T003, T004, T005) — all parallelizable |
| US1 (P1 MVP) | 4 tasks (T006–T009) |
| US2 (P1) | 3 tasks (T010–T012) |
| US3 (P2) | 6 tasks (T013–T018) |
| US4 (P2) | 4 tasks (T019–T022) |
| Polish phase | 8 tasks (T023–T030) — T023–T026 parallel, T027 inline orchestrator, T028 inline (consumes evidence), T029–T030 sequential bookkeeping |
| Parallel opportunities | 12 tasks marked [P]: T002, T003, T004, T005, T009, T012, T015, T017, T023, T024, T025, T026 |
| Independent test criteria per story | US1 spec.md AC 1–4 (`send_path_test`); US2 spec.md AC 1–4 (`logon_handshake_test` + `tc_establishment_test`); US3 spec.md AC 1–5 (`sending_time_test` + `fsm_transition_matrix_test`); US4 spec.md AC 1–3 (`test_test_request_id_cross_session_race` + `test_seqnum_drain_on_close`) |
| Tests-required per FR | YES — FR-012 + `[const §VII.1]` + test-bodies discipline mandate runtime-behavior tests, NOT optional |
| Format validation | ALL 30 tasks follow `- [ ] T### [P?] [Story?] description with file path` checklist format per skill rules |

---

## Next

`/speckit-analyze 009-session-fsm-finalize` (mandatory per `[const §XVI.4]`). Per `[[feedback_speckit_analysis_subagents]]`, spawn `subagent_type: spec-analyzer` (read-only, detection passes A-F). Expected: zero CRITICAL (no constitutional violation; Gate A inherited from 005 cleanly per the plan's Constitution Check Inheritance rule); HIGH/MEDIUM/LOW per the analyzer's calibration. After /analyze: `/speckit-checklist` (domain) → `/speckit-checklist-audit` (mandatory) → `/speckit-implement`.
