# Implementation Plan: Transport and liveness — a session that ends closes its connection; TestRequest timing and pre-session rejects

**Branch**: `105-transport-liveness` | **Date**: 2026-11-20 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `/specs/105-transport-liveness/spec.md`, with owner rulings R-1 to R-4 and the
clarifications Q1 and Q2, the 2026-11-20 rulings on SI-1 to SI-3, OQ-1 and L-105-b, and the Gate A round 1 decisions
D-a to D-c (final). **Base**: `52aeed4e` (fixpp `main`, PR #711's squash); the branch is rebased onto `origin/main`
`310ff565` (#713, tests only; the session and engine code is unchanged). Decisions D1 to D12, risks, the ruled spec
issues and the open owner question OQ-3 are in [research.md](research.md); the state is
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
   - The latch has one setter, `owe_connection_end_()`, reached on a transition into `Disconnected` and at the R-2
     wait's own endings before Active; it stores the connection's generation.
   - The detached roots (the liveness loop, the too-high Logout's bound, the new R-2 bound) and the application
     calls (`send`, `drive_reconnect`, a direct initiator's `open`) spawn a close frame at their exit that holds only
     the transport and its join counter, never the Session, joined by `close()` and the reconnect drain. An Engine session then ends terminally through its pump; a
     direct Session keeps its lifecycle, so `drive_reconnect` still works.
   - Silent pre-Active refusals close at once and are no longer reported as an establishment timeout.
   - The unanswered TestRequest sends a Logout with Text "TestRequest unanswered, TestReqID <id>" (FIX-SL §4.5.5,
     R-1), closes after the write, and writes one log record carrying slot 74. `unanswered_tr_` is deleted.
2. **R-2's wait as a dispatch overlay (D2; contract C-2 "While the wait is armed"; data-model E-3).** One helper
   writes every Logout fixpp sends to end a session, owns its `Disconnected` write, and, when the Logout was
   transmitted (and the timeout and clock allow), arms a wait bounded by `logout_disconnect_timeout_ms` from the
   write. After Active the session stays Active under the existing Logout owner (`logout_in_flight_`): the peer's
   Logout is recognised ahead of every sequence test and ends the wait whatever its MsgSeqNum (104's reset point,
   then close); other frames are processed as in Active (a ResendRequest for a gap included, as a reply); every
   second ending condition, with a Logout or silent, is absorbed; sends are refused and the liveness loop
   originates nothing (one predicate after every suspension). Before Active the refusal records `Disconnected`
   after the arming, every frame but the peer's Logout is drained, nothing is reset, and the bound is capped at the
   establishment deadline. One state machine (E-3) fixes the latch, the bound's wake sources and live predicate, and
   who closes, for graceful and terminal `close()`, `Engine::stop()` and the drain.
3. **#582 (D5).** With `validate_sequence_numbers` off, an out-of-sequence Heartbeat reaches the TestReqID rule.
4. **#535 (D6).** T = `test_request_threshold`, default 1.2 × HeartBtInt; TestRequest at T of inbound silence,
   give-up after 2T of inbound silence counted from the last inbound frame (D-c), the 1 ns guard a third saturating
   step; T ≤ 0 refused at `open()` and `register_session`, a zero TOML duration as `out_of_range` (a negative token
   stays the parser's `malformed_value`).
5. **#559 (D7).** `ReconnectFsm`'s dead members, `drive_logout`, every other caller-less stub and two constructor
   parameters go; so does `parse_cn_from_dn_local`.
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
  the close frame (`close_transport_`) and its `co_spawn` (at most once per connection, data-model E-2 I-5), the
  R-2 bound's frame and spawn (at most once per connection), the R-1 text (stack buffer).
- `record_state_transition_` stays `noexcept` and suspension-free; no spawn there.
- One observable ending per condition; one close per connection.
- QuickFIX's observable behaviour is kept except the departures the spec and contract C-10 list.

**Scale/Scope**:
- `src/session/session.cpp`: `record_state_transition_`, `close` (latch clear, two joins), `drain_for_reconnect_`
  (joins), `run_liveness_loop` (T, 2T, loop condition, R-1), `on_inbound_active_out_of_sequence_` (#582, Arm B),
  `resend_gap_deadline_`, `on_inbound_active_sending_time_reject_`, `on_inbound_active_poss_dup_`,
  `refuse_logon_with_logout_` and its callers, `inbound_logon_sent_logout_leaf_`'s caller, the receipt arms (resolve
  the wait), `on_inbound_cold_` (the armed `Disconnected` row), `answer_pending_test_request_`, `send`/`send_deferred_`,
  `drive_reconnect`, `open`, the silent-ending sites of the Active arms and `dispose_unparseable_`'s D-3 (through
  the new helper), `on_inbound_active_` (the response test); new `send_ending_logout_`, `end_session_silently_`, `owe_connection_end_`, `release_connection_end_`,
  `close_transport_`, `run_ending_logout_bound_`, the off-NextNumIn response arm, `liveness_may_originate_`,
  `note_test_request_unanswered_`; `parse_cn_from_dn_local` deleted.
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
| XVII §1, §2, §7, §8 | Gate A triggered (public C++ API, C ABI, concurrency, session FSM, security); Gate B; local build; `/speckit-verify` | PASS (scheduled); see post-design for the tasks.md deviation |

### Post-design (against research, data-model and contract; re-run after Gate A round 1)

- **VIII §2, §3, §5 / XV §1.** Per message: one byte test in the pump (D1), and while an R-2 wait is armed only, the
  response test of D2 item 3 and the `Disconnected` row's header scan. Allocation only at endings (the close frame,
  the R-2 bound), bounded to one each per connection: the latch is owed at most once per connection (data-model E-2
  I-5) and the wait arms at most once. Benchmark obligation in D10, with the fallback statement 104 used when no
  bench covers a site. **PASS.**
- **IX §1, §2, §4.** Every new branch has a planned cell (research D3, D6, D10; quickstart section 3): R-1's five
  Logout-failure classes (contract C-3.2, persistent and volatile store), each row W-1 to W-14 of the overlay, each
  row of the E-3 state machine, the no-clock and zero-timeout endings, the listener catch's two arms and the
  saturating helpers, the guard's saturated value included. The verdict is a plan, not a result: it holds once those
  cells exist at the implementation head, and the regenerated tasks.md carries them. ASan, UBSan and TSan run the
  105 label (the latch, the spawned close and the bound are strand-confined; TSan is the proof, R1). clang-tidy,
  clang-format, cppcheck and IWYU on the touched files. **PASS (planned).**
- **X §1, §4, §6, §7.** C-ABI 1.18 BREAKING with in-place amendments and a `version.h` history entry; no error code
  added or renumbered (slot 74 already exists); the Python binding and pinned tests updated in the same PR (D9).
  Article X §7's predicate ("before the first public release") read empty at Gate A round 1 and is re-read at the PR
  head as a precondition (D9). Each affected declaration has a C-driven witness (C-9). **PASS, pending the Appendix A
  controls.**
- **XI §1, §2.** Each new coroutine is `asio::awaitable`. The close frame holds only the transport's `shared_ptr`
  and its join counter, decided synchronously at the spawn, so it never dereferences the Session (data-model E-2);
  it runs on its own chain, so `close_async`'s `disable_cancellation` does not leak into the liveness loop (F-6).
  The R-2 bound captures `this` as `run_peer_logout_bound_` does, under a join counter that `close()` and
  `drain_for_reconnect_` join; `~Session`'s assert on it is Debug-only, so the release-build guarantee is the
  caller precondition B-103-13 states, which D11 amends to name the bound (with the `session.hpp` comment), and
  the ASan lifetime cells of D10. A generation check (`conn_gen_`) keeps a stale close off a new connection. The
  bound's wake sources and live predicate are stated in full (E-3). **PASS.**
- **XI §3.** No new mutex; the latch and the wait state are plain members written on the session strand. **PASS.**
- **XII.** The TLS close stays `close_async` (orderly; C-8), including under a graceful `close()` during the wait
  (C-8.4). A refused, unauthenticated peer is held no longer than `logon_timeout_ms` allows today (C-2.6), and before
  Active none of its frames is delivered (clarification D-b). Peer-supplied CompIDs reach the log only through
  `ArgValue::from_inline`'s 15-byte copy, and records are rate-bounded per entry (D8). No new peer-controlled
  allocation. A peer that stops reading can hold an ending open (L-105-f), as it can hold a TestRequest today;
  disclosed, with OQ-3 to the owner. **PASS (with the disclosure).**
- **XIII §2, §3.** Records go through the resolved logger with the session's trace context (R-1) or the entry's
  initial trace context (pre-session, as `log_role_loop_open_failure`); formats registered. **PASS.**
- **XIV.** No interface change. **PASS.**
- **XVII §1, §8.** **Deviation, remedied.** `tasks.md` was generated before Gate A, against `.specify/pipeline.md`
  step 4 ("blockers resolved BEFORE /tasks", `[const §XVII.1]`). It is non-operative review input: it is regenerated
  from the corrected bundle after Gate A converges, without carrying its task numbers forward. Gate B and
  `/speckit-verify` before labels are scheduled. **PASS after regeneration.**

No violation remains open; Complexity Tracking is empty.

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
└── tasks.md                             # generated before Gate A: non-operative, regenerated after it converges
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
3. **US1 core (P1):** D1 (latch, pump, first frame, roots, `close_transport_`, joins), D4 (`expire()`'s event test), D3 (R-1).
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

- **OQ-3** (scope): bound R-1's Logout write, or disclose the blocked-write ending (L-105-f) with a follow-up; research
  "Owner questions (Gate A round 1)". SI-1, SI-2 and SI-3 = OQ-1 are ruled (research "Spec issues for the
  orchestrator").

## Gate A

- Round 1 applied 2026-11-20: Codex P1=5 P2=7 P3=2; Opus post-judging P1=1 P2=14 P3=9; rewrite addresses root causes #1 (the R-2 wait as a dispatch overlay), #2 (one state machine for the latch and the bound), #3 (timing re-derived from source), #4 (completeness and propagation). Reviews: research/reviews/codex_105-transport-liveness_gate_a_review.md, research/reviews/opus_105-transport-liveness_gate_a_adversarial_review.md.
- Where each root cause landed: #1 research D2, contract C-2 "While the wait is armed"; #2 data-model E-2, E-3; #3
  research F-12, F-13, D6 and contract C-6, from `run_liveness_loop` and `resend_gap_deadline_`; #4 the rulings
  propagated, R-1's failure classes, C-6.6, the Article X §7 predicate, L-105-e and L-105-f, the Constitution Check
  re-run. The review paths are in the parent repo. Codex's header says 6 P2; its list holds 7, which is the count
  used.

### Round 1 — disagreements

Findings the Opus review marked "Disagree", or whose counter-proposal it rejected; Codex's fix is not applied.

- **Codex #1, the pre-Active half of its counter-proposal** ("every non-Logout frame must take the Active processing
  rules while the wait is armed", before Active too). Not applied: before Active the Logon was refused, so Active
  processing would deliver `fromApp` and advance NextNumIn for a counterparty fixpp did not accept (Article XII).
  The owner ruled the drain (clarification D-b). The after-Active half is applied as the overlay (W-1 to W-14).
- **Codex #3, "implement the owner's 2T boundary literally"** (drop the 1 ns guard, or amend every 2T statement and
  obtain a ruling). Not applied: R-3 says the session gives up "after 2T", and a give-up one tick after the 2T
  instant is a faithful reading; SC-002's "at 2T and not before" holds within the clock's resolution, and no owner
  ruling is needed. What Codex found that holds is applied: the guard's overflow (now a third saturating step,
  research D6) and the cell name (`…EndedAt24sPlusGuard`).
- **Codex #2 and Codex #1, severity.** Both are taken as P2 (Opus's downgrade): the in-flight send's reordering
  exists on `main` for every Logout ending (103 promised numbering), and a silent ending's effect was confined to
  the bound's window. Codex #2's barrier is not adopted; the promise is narrowed to numbering and the wire order is
  disclosed (C-2.3, L-105-e; research D2 item 7).
- **Codex #3, #4, #5, severity.** Taken at P2 as Opus judged; each is applied as a design or test-plan fix.
- **N-2, fix shape** (Opus: "the arming wakes the loop"). Not applied as stated: the arming adds no wake. The
  predicate tested after every suspension is what keeps the loop from originating, `close()` and the drain still
  join the loop, and the only wake available, `root_cancel_.emit`, reaches everything bound to the root slot
  (research D2 item 6). The cells arm the wait one tick before the Heartbeat and give-up instants.
- **N-5, option chosen.** Option (a), disclose (L-105-f) with a follow-up issue; option (b), bounding R-1's write, is
  a design addition sent to the owner as OQ-3.
- **N-1's "C-2.2's frame cells scoped to the SendingTime-Reject route"** is applied, and its first half (the response
  test) too; Opus's question on whether an out-of-sequence response resets was settled by clarification D-a.

### Round 1 — not applied in this pass

- **Codex #13** (tasks.md references T107 and T109) and **T010's evidence** for the Article X §7 predicate: tasks.md
  is regenerated after convergence; the recipe is in research D9.
- **Codex #14** (the requirements checklist certifies disproved claims): `checklists/requirements.md` is outside
  this pass's edit set; its "unambiguous" and "RED-first cell per FR" items are to be re-opened until the
  regenerated tasks agree.

## Complexity Tracking

> No Constitution Check violation; nothing to justify.

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| — | — | — |
