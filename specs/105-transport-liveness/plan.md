# Implementation Plan: Transport and liveness — a session that ends closes its connection; TestRequest timing and pre-session rejects

**Branch**: `105-transport-liveness` | **Date**: 2026-11-20 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `/specs/105-transport-liveness/spec.md`, with owner rulings R-1 to R-4 and the
clarifications Q1 and Q2 (final). **Base**: `52aeed4e` (fixpp `main`, PR #711's squash). Decisions D1 to D12, risks,
spec issues SI-1 to SI-3 and the owner question OQ-1 are in [research.md](research.md); the state is
[data-model.md](data-model.md); the observable rules are [contracts/transport-liveness-105.md](contracts/transport-liveness-105.md)
(C-1 to C-11); validation is [quickstart.md](quickstart.md).

## Summary

Batch B27 (lane S1): fixpp#534, #582, #535, #559 sites 2 and 3, and #687, with R-4's folds.

1. **Every ending closes its connection (#534; D1, D3, D4).** The `Disconnected` edge in
   `Session::record_state_transition_` sets a private latch, and nothing else: it stays synchronous and
   `noexcept`. The root that owns the ending consumes it.
   - The read pump tests the latch after every frame, the acceptor's first frame included (it now goes through the
     pump), and closes as it closes on an error: `close(terminal)`, after the arm has returned, so after its reply
     and its receipt reset unit.
   - The detached roots (the liveness loop, the too-high Logout's bound, the new R-2 bound) and the application
     calls (`send`, `drive_reconnect`, a direct initiator's `open`) spawn a transport-only `close_async()` at their
     exit, joined by `close()` and the reconnect drain. An Engine session then ends terminally through its pump; a
     direct Session keeps its lifecycle, so `drive_reconnect` still works.
   - Silent pre-Active refusals close at once and are no longer reported as an establishment timeout.
   - The unanswered TestRequest sends a Logout with Text "TestRequest unanswered, TestReqID <id>" (FIX-SL §4.5.5,
     R-1), closes after the write, and writes one log record carrying slot 74. `unanswered_tr_` is deleted.
2. **R-2's wait (D2).** One helper writes every Logout fixpp sends to end a session and, when it was transmitted, arms a wait bounded by
   `logout_disconnect_timeout_ms` from the write. After Active the session stays Active under the existing Logout
   owner (`logout_in_flight_`): frames are processed as in Active, sends are refused, nothing is originated, a second
   ending is absorbed. The peer's Logout is a response (104's reset point), then close. Before Active the refusal
   records `Disconnected` as today and the bound is capped at the establishment deadline.
3. **#582 (D5).** With `validate_sequence_numbers` off, an out-of-sequence Heartbeat reaches the TestReqID rule.
4. **#535 (D6).** T = `test_request_threshold`, default 1.2 × HeartBtInt; TestRequest at T, give-up at 2T,
   saturating; T ≤ 0 refused at `open()`, `register_session` and the TOML loader.
5. **#559 (D7).** `ReconnectFsm`'s dead members, `drive_logout` and two constructor parameters go; so does
   `parse_cn_from_dn_local`.
6. **#687 (D8).** One rate-bounded record per pre-session reject, and one per listener bind failure, through the
   entry's logger; the Session's rate gate is hoisted into a detail type and reused.
7. **Surfaces (D9).** C-ABI **1.18 BREAKING** (documented results change; no symbol change). C++ layout of `Session`
   and `ReconnectFsm` changes; `ReconnectFsm`'s constructor loses two parameters.
8. **Records (D11, D12).** The B&L delta, the supersession census, brain pages, header comments naming the new rule,
   and the owed brain half of the `<windows.h>` anti-pattern (docs task T-DOC-1 below).

## Technical Context

**Language/Version**: C++20 (library); C (ABI, version bump only).

**Primary Dependencies**: standalone asio (coroutines, strands, `co_spawn` detached with join counters, awaitable
operators for the bound's race, as `run_peer_logout_bound_` uses them); OpenSSL through
`asio_tls_transport::close_async`. No new dependency.

**Storage**: unchanged. The receipt reset unit (104, `run_reset_unit_`) runs before any close (C-1.b).

**Testing**:
- GoogleTest. New cells in `tests/session/test_105_*.cpp` under ctest `session_105_tests` (label `105`); config
  cells in `tests/config/test_load_negative_battery.cpp`; C-ABI cells in
  `tests/capi/recovery_liveness_capi_witness_test.cpp`.
- Engine cells on `engine_loopback_harness.hpp` / `plain_engine_rig.hpp`; the TLS arm on
  `loopback_tls_session_harness.hpp`. The close is asserted at the peer (EOF / close_notify), every competing
  timeout on the mock clock (research D10).
- Re-pins listed in research D10 and quickstart section 4.

**Target Platform**: Linux (clang, gcc, libc++), Windows MSVC — the Tier 1 to 3 matrices.

**Project Type**: library (FIX engine).

**Performance Goals**: per inbound frame, one byte test in `run_read_pump` (the latch). Nothing else per message:
the edge's store, the roots' exit tests, the wait's state and the `Disconnected` row's header scan (only while a
wait is armed) run at endings. Article VIII §3: a paired base-vs-candidate run of a bench that drives
`run_read_pump`, or a recorded statement that none does (research D10).

**Constraints**:
- Article XV §1 / VIII §5: no allocation on parse → validate → dispatch. New allocation sites, each at an ending only:
  the `close_connection_` frame and its `co_spawn` (at most once per connection), the R-2 bound's frame and spawn
  (at most once per connection), the R-1 text (stack buffer).
- `record_state_transition_` stays `noexcept` and suspension-free; no spawn there.
- One observable ending per condition; one close per connection.
- QuickFIX's observable behaviour is kept except the departures the spec and contract C-10 list.

**Scale/Scope**:
- `src/session/session.cpp`: `record_state_transition_`, `close` (latch clear, two joins), `drain_for_reconnect_`
  (joins), `run_liveness_loop` (T, 2T, loop condition, R-1), `on_inbound_active_out_of_sequence_` (#582, Arm B),
  `resend_gap_deadline_`, `on_inbound_active_sending_time_reject_`, `on_inbound_active_poss_dup_`,
  `refuse_logon_with_logout_` and its callers, `inbound_logon_sent_logout_leaf_`'s caller, the receipt arms (resolve
  the wait), `on_inbound_cold_` (the armed `Disconnected` row), `answer_pending_test_request_`, `send`/`send_deferred_`,
  `drive_reconnect`, `open`; new `send_ending_logout_`, `release_connection_end_`, `close_connection_`,
  `run_ending_logout_bound_`, `note_test_request_unanswered_`; `parse_cn_from_dn_local` deleted.
- `src/session/engine.cpp`: `run_read_pump` (latch test, `first_frame`, `expire()`'s event test), `run_accept_loop` (step 8
  deleted, reject records, the listener catch), `run_connect_loop` (call shape), `log_pre_session_reject`.
- `src/session/session_engine_access.hpp`, `include/fixpp/session/session.hpp`, `include/fixpp/session/reconnect_fsm.hpp`,
  `src/session/reconnect_fsm.cpp`, the installed `include/fixpp/session/detail/log_rate_gate.hpp` for the hoisted
  rate gate, `src/session/read_first_frame_bounded.hpp`
  (or its sibling) for the saturating default-threshold helper, `src/config/scalar_mappers.cpp`,
  `src/log/format_registry.cpp`, `include/fixpp/core/error.hpp` (comment), `include/fixpp/session/session_config.hpp`
  (comment), `include/fix/c_api/session.h` and `include/fix/c_api/version.h` (1.18).

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

### Pre-research (against the spec)

| Article | Question | Status |
|---|---|---|
| VI | Spec coverage: FIX-SL §3.1.25, §4.5.5, §4.6, §4.10 and TC 2c, 6 are cited by the spec | PASS |
| VII / IX §1 | Every FR has a test path; coverage is diff-scoped (95/85, every uncovered error path assessed) | PASS, planned in D10 |
| VIII §3, §5 | A per-frame change needs a benchmark; no hot-path allocation | PASS with the D10 bench obligation |
| X §1, §6, §7 | C-ABI results change: `/clarify` done (Q1, Q2), `/analyze`, Gate A, owner sign-off needed; minor bump decided in D9 | PASS (controls scheduled) |
| XI §1–§3, §7 | Concurrency and cancellation touched: strand confinement, no `std::mutex`, all four controls | PASS (controls scheduled) |
| XII §9 | Security surface (TLS close, unauthenticated pre-session peers): all four controls | PASS (controls scheduled) |
| XIII §2–§3 | Logs are async `FIXPP_SLOG` records with the session or entry trace context | PASS |
| XIV §2 | No pluggable interface gains a method (`Transport::close_async` exists) | PASS |
| XV §1, §5, §9 | No per-message allocation, no sync logging on the hot path, no `std::mutex` | PASS |
| XVI §3, §4 | `/clarify` done; `/analyze` mandatory (session FSM, ABI, threading, security) | PASS (scheduled) |
| XVII §1, §2, §7, §8 | Gate A triggered (public C++ API, C ABI, concurrency, session FSM, security); Gate B; local build; `/speckit-verify` | PASS (scheduled) |

### Post-design (against research, data-model and contract)

- **VIII §2, §3, §5 / XV §1.** Per message: one byte test in the pump (D1). Allocation only at endings
  (`close_connection_`, the R-2 bound), bounded to one each per connection. Benchmark obligation in D10, with the
  fallback statement 104 used when no bench covers a site. **PASS.**
- **IX §1, §2, §4.** Every new branch has a cell (quickstart section 3), including the error exits of R-1's Logout,
  the no-clock and zero-timeout waits, the listener catch's two arms and the saturating helpers. ASan, UBSan and TSan
  run the 105 label (the latch, the spawned close and the bound are strand-confined; TSan is the proof, R1).
  clang-tidy, clang-format, cppcheck and IWYU on the touched files. **PASS.**
- **X §1, §4, §6, §7.** C-ABI 1.18 BREAKING with in-place amendments and a `version.h` history entry; no error code
  added or renumbered (slot 74 already exists); the Python binding and pinned tests updated in the same PR (D9).
  **PASS, pending the Appendix A controls.**
- **XI §1, §2.** Each new coroutine is `asio::awaitable`; `close_connection_` runs on its own chain so
  `close_async`'s `disable_cancellation` does not leak into the liveness loop (research F-6); the bound resets its
  cancellation state as `run_peer_logout_bound_` does; joins are posts on the strand, as `close()`'s existing joins.
  No awaitable closure captures a reference that can outlive its Session: each spawn captures `this` under a join
  counter that `close()`, `drain_for_reconnect_` and `~Session`'s assert cover, and a generation check (`conn_gen_`)
  keeps a stale close off a new connection. **PASS.**
- **XI §3.** No new mutex; the latch and the wait state are plain members written on the session strand. **PASS.**
- **XII.** The TLS close stays `close_async` (orderly; C-8). A refused, unauthenticated peer is held no longer than
  `logon_timeout_ms` allows today (D2 item 3, C-2.11). Peer-supplied CompIDs reach the log only through
  `ArgValue::from_inline`'s 15-byte copy, and records are rate-bounded per entry (D8). No new peer-controlled
  allocation. **PASS.**
- **XIII §2, §3.** Records go through the resolved logger with the session's trace context (R-1) or the entry's
  initial trace context (pre-session, as `log_role_loop_open_failure`); formats registered. **PASS.**
- **XIV.** No interface change. **PASS.**
- **XVII §1, §8.** Gate A before `/speckit-tasks`; Gate B and `/speckit-verify` before labels. **PASS (scheduled).**

No violation; Complexity Tracking is empty.

## Project Structure

### Documentation (this feature)

```text
specs/105-transport-liveness/
├── spec.md
├── plan.md                              # this file
├── research.md                          # D1–D12, risks, SI-1–SI-3, OQ-1
├── data-model.md                        # E-1–E-9
├── quickstart.md                        # censuses, cells, mutants, lanes
├── contracts/
│   └── transport-liveness-105.md        # C-1–C-11
├── checklists/
└── tasks.md                             # /speckit-tasks (not created here)
```

### Source Code (repository root)

```text
include/
├── fix/c_api/session.h                  # 1.18 clauses on the observers (amend 1.11/1.12/1.17 sentences)
├── fix/c_api/version.h                  # 1.18 history; MINOR 18
└── fixpp/
    ├── core/error.hpp                   # slot 74's comment (new rule, no arithmetic)
    └── session/
        ├── session.hpp                  # latch, wait state, counters, new private members; unanswered_tr_ removed
        ├── session_config.hpp           # test_request_threshold's comment
        ├── detail/log_rate_gate.hpp     # hoisted log_rate_gate + admit (installed: session.hpp declares two gates)
        └── reconnect_fsm.hpp            # dead members, drive_logout, ctor parameters removed
src/
├── config/scalar_mappers.cpp            # T > 0 validation (out_of_range)
├── log/format_registry.cpp              # the new record formats
└── session/
    ├── session.cpp                      # D1–D7 (functions listed under Scale/Scope)
    ├── engine.cpp                       # pump, accept loop, connect loop, records
    ├── session_engine_access.hpp        # take_connection_end, ended
    ├── reconnect_fsm.cpp                # ctor, drive_logout removed
    └── read_first_frame_bounded.hpp     # default-threshold helper beside deadline_after (or a sibling detail header)
tests/
├── session/test_105_*.cpp, test_105_support.hpp   # new (quickstart section 3)
├── session/CMakeLists.txt               # session_105_tests, label 105
├── session/{test_537_gap_deadline,inbound_frame_dispositions_test,test_cancelled_outbound_store_engine,
│            test_538_resend_answer_store_failure_engine,engine_establishment_timeout_test,
│            heartbeat_testrequest_test,test_heartbeat_cadence_8cell,test_logout_timeout,...}.cpp   # re-pins
├── session/<ReconnectFsm constructors>   # constructor call sites (D7 recipe)
├── capi/recovery_liveness_capi_witness_test.cpp   # 1.18 observers; Capi537Witness re-pin
├── config/test_load_negative_battery.cpp          # T ≤ 0
└── fuzz/fuzz_session_recovery_admin_parse.cpp     # comment only
ci/red-arms/{batch11-labels.txt,batch18-lost-advance.sh}   # names of retired or renamed cells
bindings/python/                          # C-ABI minor expectations (D9 recipe)
spec/behaviors-and-limitations.md (+ -closed.md)   # D11 B&L plan
brain/{components/session.md,components/session-liveness-and-reconnect.md,components/engine-accept-path.md,
       failure-classes.md,log.md}         # D11, D12
.specify/536-537-538-recovery-liveness.md          # erratum naming 105 (living design note)
```

**Structure Decision**: single library project; the existing `src/`, `include/` and `tests/` layout. The list above
is the expected set; the censuses of research D1, D2, D7, D9, D10 and D11 are what decide it at the implementation
head.

## Phases (for /speckit-tasks)

1. **Censuses** (quickstart section 1) and the tasks.md ending-site table. No code.
2. **RED cells** on the base for every behaviour (quickstart section 2), per user story.
3. **US1 core (P1):** D1 (latch, pump, first frame, roots, `close_connection_`, joins), D4 (`expire()`'s event test), D3 (R-1).
4. **US1 R-2 (P1):** D2.
5. **US2 (P2):** D5. **US3 (P2):** D6 with the config validation.
6. **US4 (P3):** D8. **US5 (P3):** D7.
7. **Surfaces:** D9 (C-ABI 1.18, Python, pinned tests).
8. **Re-pins and supersession** (D10's list, D11's census), header comments, B&L, brain, and **T-DOC-1**.
9. **Polish:** mutants (quickstart section 5), lanes, bench, MSVC targeted build, `/simplify`, `/speckit-verify`.

### Included docs task T-DOC-1 (owed; research D12)

The brain half of the "`<windows.h>` macro identifiers" anti-pattern, from the parent repo's
`.claude/agents/phase-implementer.md` section "Cross-platform — <windows.h> macros rewrite common identifiers on
MSVC": a `brain/failure-classes.md` entry in that page's form (trigger and procedure; no instance, no PR) and one
`brain/log.md` line. Docs only. It goes in this PR before CI (a late docs push re-runs the matrix).

## Open items carried to the orchestrator

- **SI-1** (FR-013 vs R-4 (b): the other `ReconnectFsm` no-op stubs), **SI-2** (HeartBtInt ≤ 0 vs = 0),
  research "Spec issues for the orchestrator".
- **SI-3 = OQ-1** (one question: does the peer's Logout after a Logon refusal reset under `reset_on_logout`?) —
  research "Open questions for the owner", recommendation: no reset.

## Complexity Tracking

> No Constitution Check violation; nothing to justify.

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| — | — | — |
