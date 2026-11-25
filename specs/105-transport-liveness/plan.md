# Implementation Plan: Transport and liveness — a session that ends closes its connection; TestRequest timing and pre-session rejects

**Branch**: `105-transport-liveness` | **Date**: 2026-11-20 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `/specs/105-transport-liveness/spec.md`, with owner rulings R-1 to R-4 and the
clarifications Q1 and Q2, the 2026-11-20 rulings on SI-1 to SI-3, OQ-1 and L-105-b, the Gate A round 1 decisions
D-a to D-c and the Gate A round 2 decisions D-d to D-f, and the Gate A round 3 decision D-g and its scope ruling (final), the loop 2 round 1 scope ruling (no new decision), and the
post-Gate-A plan re-run's clarification "Session 2026-11-21 (post-Gate-A plan re-run)" ([seam] and [guard] cells; the base). **Base**: the branch's
merge-base with `origin/main`; its `src/` and `include/` are identical to `52aeed4e` (PR #711's squash), where the
source was read; the rebase onto #713 changed tests only (tasks.md T001 re-checks the identity). Decisions D1 to D12, risks and the ruled spec
issues (OQ-3 closed by ruling D-e) are in [research.md](research.md); the state is
[data-model.md](data-model.md); the observable rules are [contracts/transport-liveness-105.md](contracts/transport-liveness-105.md)
(C-1 to C-12); validation is [quickstart.md](quickstart.md).

## Summary

Batch B27 (lane S1): fixpp#534, #582, #535, #559 sites 2 and 3, and #687, with R-4's folds.

1. **Every ending closes its connection (#534; D1, D3, D4).** The `Disconnected` edge in
   `Session::record_state_transition_` sets a private latch, and nothing else: it stays synchronous and
   `noexcept`. The root that owns the ending consumes it.
   - The read pump tests the latch after every frame, the acceptor's first frame included (it now goes through the
     pump), and closes as it closes on an error: `close(terminal)`, after the arm has returned, so after its reply
     and its receipt reset unit.
   - The latch has one setter, `owe_connection_end_()`, reached on a transition into `Disconnected` (placed above the
     edge's no-application return) and at the R-2 wait's own endings before Active; it stores the connection's
     generation, and it refuses only during a wait armed before Active, so every `Disconnected` written during a wait
     armed after Active ends that wait and closes (root cause #1 of round 2).
   - The detached roots (the liveness loop, the too-high Logout's bound, the new R-2 bound) and the application
     calls (`send`, `drive_reconnect`, a direct initiator's `open`) spawn a close frame at their exit that holds only
     the transport and its join (the join is the coroutine's parameter; the latch is cleared after the spawn), never
     the Session, joined by `close()` and the reconnect drain (twice: before their own close and after the ending
     bound's join). A spawn that fails (allocation) falls back to the synchronous `Transport::close()` (ruling D-g). An Engine session then ends terminally through its pump; a
     direct Session keeps its lifecycle, so `drive_reconnect` still works.
   - Silent pre-Active refusals close at once and are no longer reported as an establishment timeout.
   - The unanswered TestRequest writes one log record carrying slot 74, then sends a Logout with Text "TestRequest
     unanswered, TestReqID <id>" (FIX-SL §4.5.5, R-1), and closes after the write. `unanswered_tr_` is deleted.
2. **The ending Logout: write limit and R-2 wait as one state machine (D2; contract C-2; data-model E-3).** Every
   Logout fixpp writes to end a session enters one machine at its leaf's write point: its write is limited by
   `logout_disconnect_timeout_ms` from the write's start (ruling D-e), and at that instant an unfinished write is
   ended by closing the transport through `close_async`, with nothing cancelled. One helper writes every R-2 Logout,
   owns its `Disconnected` write, and, when the Logout was transmitted (and the timeout and clock allow), arms a wait
   on the same budget. After Active the session stays Active under the existing Logout owner (`logout_in_flight_`): the peer's
   Logout is recognised ahead of every sequence test and ends the wait whatever its MsgSeqNum (104's reset point,
   then close); other frames are processed as in Active (a ResendRequest for a gap included, as a reply); every
   second ending condition from a peer frame, with a Logout or silent, is absorbed, and every internal failure ends the
   wait (absorption by cause); sends are refused and the liveness loop
   originates nothing (one predicate after every suspension). Before Active the refusal records `Disconnected`
   after the arming, every frame but the peer's Logout is drained, nothing is reset, and on an Engine session the
   pump hands its establishment deadline to the Session once, which caps the write and the wait on the engine clock (a
   direct Session has none). The bound's expiry, like the write limit, closes the transport whatever arm is in
   flight; so does the bound's own failure (its `catch (...)`, or a deadline wait that completes early on an absorbed
   exception), when it fails and not at its deadline, through the same expiry exit (loop 2 round 1, root cause #1); a bound that cannot be spawned means the Logout is not written (ruling D-g). One state machine (E-3) fixes the latch, the bound's wake sources and live predicate, and
   who closes, for graceful and terminal `close()`, `Engine::stop()` and the drain.
3. **#582 (D5).** With `validate_sequence_numbers` off, an out-of-sequence Heartbeat reaches the TestReqID rule.
4. **#535 (D6).** T = `test_request_threshold`, default 1.2 × HeartBtInt; TestRequest at T of inbound silence,
   give-up after 2T of inbound silence counted from the last inbound frame (D-c; amended 2026-11-24, the owner's
   fresh-window ruling: or T after the pending TestRequest's write when that is later, and the cap ruling: a TestRequest still held behind a resend answer past 3T from the last inbound frame is given up then, contract C-6.2), the 1 ns guard a third saturating
   step; one sleep per iteration to the earliest instant of a set that keeps the not-yet-reached Heartbeat instant (a
   Heartbeat instant passed while ineligible is dropped, so the loop cannot spin), so a frame after the TestRequest
   has the Heartbeat sent at its instant, or at the next wake, at most max(0, T − H) late (round 3, root cause #3); the
   TestReqID(112) rule of ruling D-d (an earlier TestRequest's 112 neither answers nor ends); T ≤ 0 refused at `open()` and `register_session`, a zero TOML duration as `out_of_range` (a negative token
   stays the parser's `malformed_value`).
5. **#559 (D7).** `ReconnectFsm`'s dead members, `drive_logout`, every other caller-less stub and two constructor
   parameters go; so does `parse_cn_from_dn_local`.
6. **#687 (D8).** One rate-bounded record per pre-session reject, and one per listener bind failure, through the
   entry's logger; the Session's rate gate is hoisted into a detail type and reused.
7. **Surfaces (D9).** C-ABI **1.18 BREAKING** (documented results change; no symbol change). C++ layout of `Session`
   (`sizeof(Session)` changes: FR-013 removes members, D1, D2 and D5 add them) and `ReconnectFsm` changes; `ReconnectFsm`'s constructor loses two parameters; one new installed implementation
   header, `detail/log_rate_gate.hpp`.
8. **Records (D11, D12).** The B&L delta, the supersession census, brain pages, header comments naming the new rule,
   and the owed brain half of the `<windows.h>` anti-pattern (docs task T-DOC-1 below).

## Technical Context

**Language/Version**: C++20 (library); C (ABI, version bump only).

**Primary Dependencies**: standalone asio (coroutines, strands, `co_spawn` detached with join counters, awaitable
operators for the bound's race, as `run_peer_logout_bound_` uses them); OpenSSL through
`asio_tls_transport::close_async`. No new dependency.

**Storage**: unchanged. The receipt reset unit (104, `run_reset_unit_`) runs before any close (C-1.b).

**Testing**:
- GoogleTest. New cells in `tests/session/test_105_*.cpp` under ctest `session_105_tests` (label `105`); the [seam]
  and [guard] cells that name a symbol the base lacks in `tests/session/test_105_seams.cpp` under a second ctest,
  `session_105_seam_tests` (labels `session`, `105` and `105_seam`), which base readings exclude (`-LE 105_seam`)
  and never build (spec clarification "Session 2026-11-21 (post-Gate-A plan re-run)"); config cells in `tests/config/test_load_negative_battery.cpp`;
  C-ABI cells in `tests/capi/recovery_liveness_capi_witness_test.cpp`.
- Plaintext Engine cells on `plain_engine_rig.hpp` (mock engine clock, raw peer socket). TLS Engine cells on
  `engine_loopback_harness.hpp` (`EngineLoopbackHarness`: a real `make_asio_tls_transport_factory` under
  `mtls_ca`, the peer a `LoopbackTlsFixture::make_client` mTLS client as `engine_firstframe_test.cpp`'s
  `probe_post_handshake` drives it), acceptor role, with a `mock_clock` passed in `EngineConfig::clock`. Direct-Session
  TLS cells on `loopback_tls_session_harness.hpp` (an initiator Session fed through `on_inbound_frame`, no pump). The
  close is asserted at the peer (EOF / close_notify), every competing timeout on the mock clock (research D10).
- Re-pins listed in research D10 and quickstart section 4.

**Target Platform**: Linux (clang, gcc, libc++), Windows MSVC — the Tier 1 to 3 matrices.

**Project Type**: library (FIX engine).

**Performance Goals**: per inbound frame, two tests: the latch's byte test in `run_read_pump`, and the ending-phase
test in `on_inbound_active_` (the response test, made on every frame, armed or not). Per outbound write,
`store_then_emit`'s transmitted out-parameter. Per Heartbeat with a TestRequest pending, the TestReqID
classification. At endings only: the edge's store, the roots' exit tests, the ending's state, its bound and close
frame, and the `Disconnected` row's header scan. Article VIII §2, §3 (ruling D-f): paired A-B-A-B runs of
`on_inbound_frame_bench`, `session_send_bench` and `engine_send_bench`, and a new pump case in `engine_send_bench`'s
harness for the latch test (research D10, contract C-12).

**Constraints**:
- Article XV §1 / VIII §5: no allocation on parse → validate → dispatch. New allocation sites, each at an ending only:
  the close frame (`close_transport_`) and its `co_spawn` (at most once per connection for the latch, once for a
  write-limit close and once for the bound's expiry; a spawn that fails falls back to a synchronous close, which
  allocates nothing), the ending bound's frame and spawn (once per ending Logout, at most once per connection), the
  R-1 text (stack buffer). Combined, per connection: at most one ending bound and at most two close frames (a
  deadline's or the bound's failure exit's, then the latch's, which finds the transport closed and is subsumed),
  since one ending per connection enters `writing` (data-model E-3's absorption) and the write limit, the expiry and
  the failure exit are exits of that one bound. No cell counts them: they are off the per-message path Article XV §1
  governs.
- The existing allocation guards of the inbound path (`tests/alloc_guard`, the pump's active-read guard
  `test_093_pump_active_read_alloc_guard.cpp` among them) run at the PR head in tasks.md T142's full ctest, with their
  result recorded in the verify evidence; the per-frame changes are read against them, not argued "at endings only".
- `record_state_transition_` stays `noexcept` and suspension-free; no spawn there.
- One observable ending per condition; one close per connection.
- QuickFIX's observable behaviour is kept except the departures the spec and contract C-10 list.

**Scale/Scope**:
- `src/session/session.cpp`: `record_state_transition_`, `close` (latch clear, two joins), `drain_for_reconnect_`
  (joins), `run_liveness_loop` (T, 2T, loop condition, R-1), `on_inbound_active_out_of_sequence_` (#582, Arm B),
  `resend_gap_deadline_`, `on_inbound_active_sending_time_reject_`, `on_inbound_active_poss_dup_`,
  `refuse_logon_with_logout_` and its callers, `inbound_logon_sent_logout_leaf_`'s caller, `honor_peer_next_expected_`'s
  two Logouts (ruling R-789), the receipt arms (resolve
  the wait), `on_inbound_cold_` (the armed `Disconnected` row), `answer_pending_test_request_` and the in-sequence Heartbeat arm
  (the 112 rule), `inbound_logout_leaf_` and the pre-Active Logout leaves (the write limit), `on_inbound_active_logout_`
  and `acknowledge_peer_logout_` (the reply's limit), `install_reconnected_transport` and `attach_accepted_transport`
  (the TestReqID floor and the ending's reset), `send`/`send_deferred_`,
  `drive_reconnect`, `open`, the silent-ending sites of the Active arms and `dispose_unparseable_`'s D-3 (through
  the new helper), `on_inbound_active_` (the response test); new `send_ending_logout_`, `end_session_silently_`, `owe_connection_end_`, `release_connection_end_`,
  `close_transport_`, `run_ending_logout_bound_`, the off-NextNumIn response arm, `liveness_may_originate_`,
  `ending_in_progress_`, `classify_test_req_id_`, `note_test_request_unanswered_`; `parse_cn_from_dn_local` deleted.
- `src/session/engine.cpp`: `run_read_pump` (latch test, `first_frame`, `expire()`'s event test), `run_accept_loop` (step 8
  deleted, reject records, the listener catch), `run_connect_loop` (call shape), `log_pre_session_reject`.
- `src/session/session_engine_access.hpp` (`set_establishment_deadline`), `include/fixpp/session/session.hpp`, `include/fixpp/session/reconnect_fsm.hpp`,
  `src/session/reconnect_fsm.cpp`, the installed `include/fixpp/session/detail/log_rate_gate.hpp` for the hoisted
  rate gate, `src/session/read_first_frame_bounded.hpp`
  (or its sibling) for the saturating default-threshold helper and the TestReqID window function (data-model E-4), `src/config/scalar_mappers.cpp`,
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
| X §1, §6, §7 | C-ABI results change: `/clarify` done (Q1, Q2), `/analyze`, Gate A; the fourth Appendix A control, the user `/plan` sign-off ([const §X.6]), GIVEN by the owner 2026-11-21 on head `b63456a6` (record: decisions/speckit/105-transport-liveness-gatea.md); minor bump decided in D9 | PASS (all four controls done; evidence pointers in the post-design X entry) |
| XI §1–§3, §7 | Concurrency and cancellation touched: strand confinement, no `std::mutex`, all four controls | PASS (controls scheduled) |
| XII §9 | Security surface (TLS close, unauthenticated pre-session peers): all four controls | PASS (controls scheduled) |
| XIII §2–§3 | Logs are async `FIXPP_SLOG` records with the session or entry trace context | PASS |
| XIV §2 | No pluggable interface gains a method (`Transport::close_async` exists) | PASS |
| XV §1, §5, §9 | No per-message allocation, no sync logging on the hot path, no `std::mutex` | PASS |
| XVI §3, §4 | `/clarify` done; `/analyze` mandatory (session FSM, ABI, threading, security) | PASS (scheduled) |
| XVII §1, §2, §7, §8 | Gate A triggered (public C++ API, C ABI, concurrency, session FSM, security); Gate B; local build; `/speckit-verify` | PASS (scheduled); see post-design for the tasks.md regeneration |

### Post-design (against research, data-model, contract and the regenerated tasks.md; re-run at the post-Gate-A plan re-run, 2026-11-21)

- **VII §3, §8.** TDD: every behaviour cell is written and read RED before its mechanism (tasks.md: each story's RED
  tasks precede its implementation tasks). The [seam] cells are the stated exception of clarification "Session
  2026-11-21": their seam does not exist on the base, so they are written with the mechanism and read RED at the
  implementation head under a mutant that removes exactly the mechanism the cell tests; where a [seam] cell twins a
  peer-visible RED cell, that cell is read on the base first. The mutant reading is the failing test that precedes the
  mechanism's acceptance, so §3 holds. §8: cells are selected by label (`-L 105`, `-L 105 -LE 105_seam` on the base),
  never `-R`. The seam entry exists so that a base reading stays buildable, not for isolation; grouping its cells in one
  binary is admissible because each seam is a hook its own cell arms and disarms, not a replacement of the global
  allocator (the isolation class §8 keeps standalone). A seam that turns out to be process-global state is reset by
  every cell that arms it, as 093's `file_store.cpp` fault-seam counters are, or its cell goes to a standalone entry.
  **PASS.**
- **VII §7.** The wire parser (the Framer, `scan_frame_header`, the offset table) is unchanged. The TestReqID(112)
  classification is a new reading of a peer-supplied field's bytes (`TR` plus canonical decimal); by 027's precedent
  (a new field case in `scan_frame_header` met §7 by a seed extension of
  `tests/fuzz/fuzz_session_recovery_admin_parse.cpp`, not a new harness), it is treated as parser-touching, and that
  harness's seeds gain Heartbeats carrying 112 forms (tasks.md T085). The classification runs only while a TestRequest
  is pending, which random bytes fed to `on_inbound_frame` do not create without a clock advance, so the parse-and-classify
  step (`TR` prefix, canonical decimal, window) is a free `constexpr` `detail` function,
  `detail::is_earlier_test_req_id`, in `src/session/read_first_frame_bounded.hpp` (orchestrator decision on
  `/speckit-analyze` C1; data-model E-4): `classify_test_req_id_` calls it, and the harness calls it directly on each fed
  payload's 112 bytes, so the parse is fuzzed without a pending TestRequest. Its guard cells are tasks.md T086, its
  mutants T088, and a libFuzzer run of at least 10 minutes is recorded (T148). The other new reads of peer bytes are
  not parser changes: W-1's response test and the pre-Active `Disconnected` row read the `FrameHeader` fields that
  `scan_frame_header` already produces (the scan is unchanged; the row adds a call site, not a parser), and the
  pre-session record copies the BeginString and CompIDs that `scan_first_frame_ids` already extracts (unchanged), with
  each non-printable byte replaced (research D8). **PASS (planned).**

- **VIII §2, §3, §5 / XV §1.** Per inbound frame: two tests (the pump's latch, the ending-phase test in
  `on_inbound_active_`); per write, one out-parameter; per Heartbeat with a TestRequest pending, the 112
  classification (a bounded parse of at most twelve bytes, `TR` and ten digits, then the two-integer window; no
  allocation). tasks.md T014 records whether `on_inbound_frame_bench`'s Heartbeat case runs with a TestRequest pending;
  if it does not, the PR adds that case (Article VIII §3). Allocation only at endings (the close frame, the ending bound), each
  bounded per connection (data-model E-2 I-5, E-3); the spawn-failure fallback allocates nothing. The liveness
  loop's sleep set and the establishment hand-off (one store per connection) add no per-frame work. Benchmarks: the paired A-B-A-B runs of ruling D-f, with the new
  pump case written in this PR, so no site is left without a bench (research D10, contract C-12). Round 1's fallback
  ("a recorded statement that none does") is withdrawn: Article VIII §3 admits no perf change without a benchmark in
  the same PR. **PASS (planned; the runs are evidence at the PR head).**
- **IX §1, §2, §4.** Every new branch has a planned cell (research D3, D5, D6, D10; quickstart section 3), each marked
  RED on the base, [guard] with its mutant, or [seam] with its mutant (clarification "Session 2026-11-21 (post-Gate-A plan re-run)"): R-1's seven Logout-failure classes (contract C-3.2), each row W-1 to W-15 of the
  overlay, each row of E-3 including the write limit's shapes where they are reachable (the Logout itself blocked,
  for R-1, R-2 before and after Active and a reply; the Logout queued at the write gate, for R-1, R-2 after Active
  and a reply; the queued shape before Active is unreachable on an Engine session, research D10) and its TLS arm,
  the bound's expiry under a blocked arm, the establishment cap (write, store and wait), the four W-9 dispositions,
  the spawn-failure seams (each asserting the peer's EOF), the bound's failure exit per cause and phase
  (`EndingBound_RaceThrows_*`, `EndingBound_DeadlineArmAbsorbsException_*`; loop 2 round 1, root cause #1), the `cut`
  marker (`DirectSession_WriteCompletesAfterTheBoundFires_NoWaitArmed`), the liveness sleep set (a frame before and after the
  Heartbeat instant, no spin, counted by a `Clock` decorator under a bounded drive so its RED arm terminates), the TestReqID window's boundary cells, the no-application cell, the 112 outcomes per site, the no-clock and zero-timeout endings,
  the listener catch's two arms (the `system_error` arm by a port in use, the generic arm by a listener-construction
  seam, since no configuration reaches it; research D8) and the saturating helpers. The verdict is a plan, not a result: it holds once those
  cells exist at the implementation head; the regenerated tasks.md carries them (its traceability tables). ASan, UBSan and TSan run the
  105 label (the latch, the close frame and the bound are strand-confined; TSan is the proof, R1). clang-tidy,
  clang-format, cppcheck and IWYU on the touched files. **PASS (planned).**
- **X §1, §4, §6, §7.** C-ABI 1.18 BREAKING with in-place amendments and a `version.h` history entry (the write limit
  and the 112 rule included); no error code added or renumbered (slot 74 and `session_testreqid_mismatch` exist); the
  Python binding and pinned tests updated in the same PR (D9). Article X §7's predicate read empty at Gate A rounds 1
  and 2 and is re-read at the PR head as a precondition (D9). Each affected declaration has a C-driven witness (C-9).
  Evidence of the controls: `/clarify`, the spec's Clarifications (sessions 2026-11-20 and 2026-11-21); `/analyze`, its
  C1 decision (data-model E-4) and its re-run after the step 9 checklist audit's SPEC-FIXED edits (`.specify/pipeline.md`
  step 9), which is part of this control; Codex Gate A, `## Gate A` below (converged at loop 2 round 2, the review paths
  there). **PASS, pending the Appendix A controls.** The fourth, the user `/plan` sign-off ([const §X.6]), is requested from the
  owner 2026-11-21 and is a hard precondition of Phase 3 (US1 part A; tasks.md "Dependencies and execution order").
- **XI §1, §2.** Each new coroutine is `asio::awaitable`. Both new spawns take
  `on_inbound_active_too_high_logout_`'s join-as-parameter shape (the join is released by its destructor if the frame
  is never published), with a `catch` that, unlike that precedent's (which rethrows), does not rethrow and fails
  closed (ruling D-g): a close frame that cannot be spawned is replaced by the synchronous `Transport::close()`, and a
  bound that cannot be spawned means the Logout is not written, so no counter leaks, no state is left armed and no
  ending is left without its close (round 3, root cause #1). The close
  frame holds only the transport's `shared_ptr` and its join, so it never dereferences the Session (data-model E-2);
  it runs on its own chain, so `close_async`'s `disable_cancellation` does not leak into the liveness loop (F-6). The
  ending bound's own failure takes its expiry exit, never the depth-gated `release_connection_end_` (data-model E-3,
  "The bound's exits"; loop 2 round 1, root cause #1), so it leaves no ending without its close. The
  ending bound captures `this` as `run_peer_logout_bound_` does, under a join that `close()` and
  `drain_for_reconnect_` join after their transport close, followed by a second join of the close frame's counter
  (round 3, N-3); `~Session`'s assert on it is Debug-only, so the
  release-build guarantee is the caller precondition B-103-13 states, which FR-016 and D11 amend to name the bound
  (with the `session.hpp` comment), and the ASan lifetime cells of D10. The write limit and the bound's expiry cancel
  nothing: they close the transport, so every suspended frame resumes and runs to its end (D2 item 11). A generation check (`conn_gen_`)
  keeps a stale close off a new connection. The bound's wake sources and live predicate by phase are stated in full
  (E-3). **PASS.**
- **XI §6.** The two new coroutines (the close frame and the ending bound) are detached `co_spawn` frames, which HALO
  cannot elide, as the precedent `run_peer_logout_bound_`'s is; each is spawned only at an ending, a bounded number of
  times per connection (data-model E-2 I-5, E-3), never per message, and an allocation failure at either spawn fails
  closed (ruling D-g). No per-awaiter frame is added on the per-frame path. **PASS.**
- **XI §3.** No new mutex; the latch, the ending's state and the TestReqID floor are plain members written on the
  session strand. **PASS.**
- **XII.** The TLS close stays `close_async` (orderly; C-8), except the allocation-failure fallback (C-8.6), including under a graceful `close()` during the wait
  (C-8.4). The new abortive closes are the write limit's behind a peer that does not read (C-8.5; off Windows, L-105-g), permitted by
  ruling D-e and disclosed (L-105-g), before 105 that peer held the connection with no bound; and the synchronous
  fallback when a close frame cannot be spawned (an allocation failure; ruling D-g, C-8.6, L-105-h). A refused,
  unauthenticated peer is held on an Engine session no longer than the earlier of the establishment deadline
  (`logon_timeout_ms` from the connection's establishment start) and `logout_disconnect_timeout_ms` from the refusal
  Logout's owner claim, plus `tls_close_timeout` on TLS (C-2.6): the establishment
  deadline now caps the refusal Logout's store call and write as well as the wait, by construction (round 3, root
  cause #2), on the stated condition that the cap's wait and the bound's are independent sleeps, which holds on an
  Engine session (its coroutines run on the type-erased session strand, never in `system_clock_source`'s per-session
  slot; data-model E-3, "The establishment cap", gives the recipe; loop 2 round 1, root cause #2); with `logout_disconnect_timeout_ms` 0 the refusal's write is unbounded as on `main` (L-105-f), and
  before Active none of its frames is delivered (clarification D-b). A peer that stops reading can no longer hold
  an ending Logout's write (C-2.7); it can still hold a non-ending write (L-105-f, narrowed, with a follow-up issue).
  Peer-supplied BeginString and CompIDs reach the log only with each byte outside printable ASCII (0x20 to 0x7E) replaced
  by `?` and then through `ArgValue::from_inline`'s 15-byte copy (C-7.1), and records are
  rate-bounded per entry (D8). The 112 rule reads a peer value through a bounded parse into two integer comparisons, with no allocation. No
  new peer-controlled allocation. **PASS (with the disclosures).**
- **XIII §2, §3.** Records go through the resolved logger with the session's trace context (R-1, written before the
  Logout's write) or the entry's initial trace context (pre-session, as `log_role_loop_open_failure`); formats
  registered. **PASS.**
- **XIV.** No pluggable interface gains a method (`Transport::close_async` exists). The installed C++ surface gains
  one implementation header, `include/fixpp/session/detail/log_rate_gate.hpp`, with `detail/replay_gate.hpp`'s
  status, accounted in contract C-9 and B-105-7 (round 3, Codex #6). **PASS.**
- **XVII §1, §8.** Gate A converged at loop 2 round 2 (P1 0, P2 0). The pre-Gate-A `tasks.md` (generated before Gate A,
  against `.specify/pipeline.md` step 4 and `[const §XVII.1]`) was replaced by one regenerated from the converged
  bundle, with no task number carried forward. Gate B, `/speckit-verify` (tasks.md T144) and the completeness audit
  (T147) are scheduled before labels. **PASS.**

No violation remains open; no owner question is open (OQ-3 closed by ruling D-e); Complexity Tracking is empty.

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
│   └── transport-liveness-105.md        # C-1–C-12
├── checklists/requirements.md
└── tasks.md                             # regenerated from the converged bundle after Gate A loop 2 round 2
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
    ├── session_engine_access.hpp        # take_connection_end, ended, set_establishment_deadline
    ├── reconnect_fsm.cpp                # ctor, drive_logout removed
    └── read_first_frame_bounded.hpp     # default-threshold helper, TestReqID window and parse functions beside deadline_after
                                         #   (or a sibling detail header)
tests/
├── session/test_105_{connection_close,logout_wait,write_limit,liveness_timing,heartbeat_knob_off,
│            presession_log,tls_close}.cpp   # new: ctest session_105_tests (quickstart section 3)
├── session/test_105_seams.cpp           # new: [seam] and [guard] cells, ctest session_105_seam_tests
├── session/test_105_support.hpp         # new: observers, bounded drive, counting Clock, held-write and
│                                        #   deferred-completion transports, held/failing stores
├── session/CMakeLists.txt               # session_105_tests (labels session, 105); session_105_seam_tests (+ 105_seam)
├── session/engine_loopback_harness.hpp  # the Engine-over-TLS rig; touched only if a cell needs a hook it lacks
├── support/session_test_access.hpp      # reads of the new members (phase, cut, counters) for reach proofs
├── support/engine_test_access.hpp       # arming the listener-construction seam, if engine-side
├── session/{test_537_gap_deadline,inbound_frame_dispositions_test,test_cancelled_outbound_store_engine,
│            test_538_resend_answer_store_failure_engine,engine_establishment_timeout_test,
│            heartbeat_testrequest_test,test_heartbeat_cadence_8cell,test_logout_timeout,...}.cpp   # re-pins
├── session/<ReconnectFsm constructors>   # constructor call sites (D7 recipe)
├── capi/recovery_liveness_capi_witness_test.cpp   # 1.18 observers; Capi537Witness re-pin
├── capi/version_test.cpp (+ every minor pin research D9's grep finds)   # MINOR 18
├── config/test_load_negative_battery.cpp          # T ≤ 0
├── config/test_quickfix_parity_table.cpp          # the TestRequest timing row (FR-012)
└── fuzz/fuzz_session_recovery_admin_parse.cpp     # the stale drive_logout comment; 112 seeds and a direct call of the 112 parse ([const §VII.7])
ci/red-arms/{batch11-labels.txt,batch18-lost-advance.sh}   # names of retired or renamed cells
bindings/python/                          # C-ABI minor expectations (D9 recipe)
tools/capi_freeze.sha256                  # regenerated by its generator (tasks.md T012 finds it)
bench/session/engine_send_bench.cpp       # the new pump case (D10, ruling D-f)
spec/behaviors-and-limitations.md (+ -closed.md)   # D11 B&L plan
spec/feature-catalogue.md, spec/coverage-index.md  # close-out (tasks.md T146)
brain/{components/session.md,components/session-liveness-and-reconnect.md,components/engine-accept-path.md,
       failure-classes.md,log.md}         # D11, D12
.specify/536-537-538-recovery-liveness.md          # erratum naming 105 (living design note)
```

The production test seams (the allocation-failure seams at the close frame's and the bound's spawns, the bound's
in-body race seam, the listener-construction seam) sit in `src/session/session.cpp` and `src/session/engine.cpp` in an
existing seam idiom (093's unconditionally compiled fault seams in `src/session/file_store.cpp`, or 035's
`FIXPP_TEST_HOOKS` probe), chosen at tasks.md T027.

**Structure Decision**: single library project; the existing `src/`, `include/` and `tests/` layout. The list above
is the expected set; the censuses of research D1, D2, D7, D9, D10 and D11 are what decide it at the implementation
head.

## Phases (for /speckit-tasks)

1. **Censuses** (quickstart section 1) and the tasks.md ending-site table. No code.
2. **RED cells** on the base for every behaviour (quickstart section 2), per user story; [seam] and [guard] cells read
   under their mutants at the implementation head (clarification "Session 2026-11-21 (post-Gate-A plan re-run)").
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

- **The user `/plan` sign-off** ([const §X.6], the fourth Appendix A control): GIVEN by the owner 2026-11-21 on head `b63456a6`; it was requested as a
  hard precondition of Phase 3 (US1 part A, tasks.md T018 onward). Not given yet. It counts only as the owner's own
  statement, recorded in `.specify/decisions/105-transport-liveness-evidence.md` with its date, its words and the bundle
  head it was given on; an orchestrator's or an agent's report of it does not count. It covers the bundle at that head:
  a later edit that changes an observable rule (spec, contract, data-model, research) needs a fresh sign-off, and an
  edit that changes no rule (a citation, a task's wording, a step 9 audit fix that only states what the bundle already
  decides) does not; the orchestrator records which it is.
- No open question. **OQ-3** is closed by ruling D-e (spec Clarifications, Gate A round 2); SI-1, SI-2 and SI-3 = OQ-1 are ruled
  (research "Spec issues for the orchestrator"). Owed before Gate B, not a question: the follow-up issues of the next
  section, filed and named where they are cited.

## Follow-ups to file before Gate B

Each is a fixpp issue, labelled at creation, filed before Gate B and named where it is cited. None is 105 scope.

- **Bounding every non-ending write** (L-105-f): a write that does not end the session stays unbounded; the issue
  is named in L-105-f's row.
- **The per-session timer slot under a direct driver** (`system_clock_source`; loop 2 round 1, Codex #1 downgraded,
  root cause #2). `sleep_until` takes the per-session `SessionTimerSlot` when the awaiter's executor is exactly a
  `session_executor` (`target<session_executor>()`), and that slot holds one timer, so a direct driver that spawns
  a Session's coroutines on `sess.executor()` itself has concurrent sleeps of one Session reset each other's
  expiry: 103's `run_peer_logout_bound_` beside a parked liveness sleep, `close()`'s wait, and 105's bound. The
  `Clock::sleep_until` contract does not promise independent concurrent sleeps; the slot's own comment says the
  design "implicitly requires user-side serialisation". Inferred from the code, not observed. Pre-existing on `main`; an Engine session does not reach it.
- **The read pump's establishment race re-races an absorbed exception** (loop 2 round 1, N-1). In `run_read_pump`,
  the establishment race's `outcome.index() == 1` takes `continue` ("The race only wakes a blocked read (C-4); the
  loop head decides expiry"), and `detail::await_deadline`'s `catch (...)` completes that arm early on any
  non-`system_error` exception, so a lasting allocation failure spins the session strand until the establishment
  deadline. Pre-existing since 093. 105's ending bound does not copy the shape (data-model E-3, "The bound's loop").
- **An initiator's TLS handshake against a peer that accepts and never handshakes is not bounded** (tasks.md T024,
  the T035 ruling). `Session::drive_reconnect()` over the real TLS factory, against a listener whose server end never
  runs its handshake, does not return; measured at 10 × the cell's 5 s wall budget on the base and at the 105 head
  alike, so pre-existing and not 105 scope. With the server end's handshake running it returns at once.

## Gate A

- Round 1 applied 2026-11-20: Codex P1=5 P2=7 P3=2; Opus post-judging P1=1 P2=14 P3=9; rewrite addresses root causes #1 (the R-2 wait as a dispatch overlay), #2 (one state machine for the latch and the bound), #3 (timing re-derived from source), #4 (completeness and propagation). Reviews: research/reviews/codex_105-transport-liveness_gate_a_review.md, research/reviews/opus_105-transport-liveness_gate_a_adversarial_review.md.
- Where each root cause landed: #1 research D2, contract C-2 "While the wait is armed"; #2 data-model E-2, E-3; #3
  research F-12, F-13, D6 and contract C-6, from `run_liveness_loop` and `resend_gap_deadline_`; #4 the rulings
  propagated, R-1's failure classes, C-6.6, the Article X §7 predicate, L-105-e and L-105-f, the Constitution Check
  re-run. The review paths are in the parent repo. Codex's header says 6 P2; its list holds 7, which is the count
  used.
- Round 2 applied 2026-11-20: Codex P1=2 P2=5 P3=1; Opus post-judging P1=0 P2=7 P3=7; rewrite addresses root causes #1 (one ending state machine whose owe refuses only for a wait armed before Active, absorption by cause, both spawns in the too-high bound's shape), #2 (one liveness sleep per iteration, the Heartbeat eligible again after a frame, the late-answer consequence resolved by ruling D-d), #3 (the blocked-write ending, superseded by ruling D-e's write limit), #4 (the test oracles and the cross-document lists re-derived). Reviews: research/reviews/codex_105-transport-liveness_gate_a_2_review.md, research/reviews/opus_105-transport-liveness_gate_a_2_adversarial_review.md.
- Where each round-2 root cause landed: #1 data-model E-2 (the setter, the close frame's spawn, I-2, I-5) and E-3 (rewritten,
  with the phase `writing`), research D1 items 1 and 4, D2 items 1, 4 and 8, contract C-2 W-8, W-9; #2 data-model
  E-4, research D6, contract C-6.2, C-6.3, FR-010; #3 ruling D-e in spec FR-003, FR-004, FR-004b, FR-005, FR-008,
  research D2 item 11 and D3, data-model E-3 rows 1 to 5 and E-8, contract C-2.7, C-3, C-8.5, C-11 (L-105-f narrowed,
  L-105-g); #4 quickstart sections 2, 3 and 5 (every cell marked, positive controls, `SecondDisconnectedWrite` dropped),
  research D10 (benches per ruling D-f, the W-13 clock split, the five wall-clock timers), FR-016's list, C-2.6 and
  FR-004 scoped to Engine sessions, SC-001's "at once", the owe's placement above the no-application return. N-5:
  research D2 item 1, contract C-2.8. Ruling D-d: spec FR-009, research D5, data-model E-4, contract C-5.4.
- Loop 1 exhausted at round 3 (P1 0 / P2 4); owner chose a targeted edit + one fresh round. Targeted edit applied 2026-11-20: Codex P1=3 P2=3 P3=1; Opus post-judging P1=0 P2=4 P3=8; rewrite addresses root causes #1 (spawn failure fails closed: a close frame that cannot be spawned falls back to the synchronous close, a bound that cannot be spawned means no Logout is written; ruling D-g), #2 (the bound's expiry closes like the write limit, whatever arm is in flight; the Engine's establishment deadline handed to the ending machine as a cap), #3 (the liveness loop sleeps to the earliest instant of a set that keeps the not-yet-reached Heartbeat instant, with the spin trap named), #4 (lifetime, second join, row 5 per mode, the reply's queued cell, the installed header, the 2³² condition, the budget's store-inclusive origin, the base). Reviews: research/reviews/codex_105-transport-liveness_gate_a_3_review.md, research/reviews/opus_105-transport-liveness_gate_a_3_adversarial_review.md.
- Where each round-3 root cause landed: #1 spec Clarifications (D-g), FR-002, FR-004b; data-model E-2 ("The close frame"),
  E-3 ("Entering `writing`", row 2a, row 4); research D1 item 4, D2 items 8 and 11; contract C-2 "No wait is armed",
  C-2.7, C-3.2, C-8.2, C-8.6, C-11 (L-105-b's allocation clauses deleted, L-105-h added). N-5 rides with it: the
  precedent's `catch` rethrows and these do not (data-model E-2, E-3; research D1 item 4, D2 item 8; XI above). #2
  data-model E-2 (I-2's exceptions), E-3 ("The establishment cap", the field `establishment_cap_`, rows 4, 10, 11,
  12, 17), E-5 (the hand-off); research D2 items 8 and 11; contract C-1.b, W-12, W-15, C-2.5, C-2.6, C-11 L-105-f;
  spec FR-004, FR-008. #3 data-model E-4; research D6; contract C-6.3; spec FR-010; Summary item 4. #4 data-model E-2
  (I-4, the second join), E-3 (the `writing` predicate, rows 5 and 5c), E-4 (the 2³² condition), E-7; research D1
  items 5 and 6, D2 items 9 and 11, D5, D8, D9, D10, D11; contract C-2.5 (the budget's origin), C-2.7, C-5.4, C-9;
  spec FR-016, FR-017 and the Base line; quickstart sections 2, 3 and 5.
- Loop 2 round 1 applied 2026-11-20: Codex P1=1 P2=2 P3=1; Opus post-judging P1=0 P2=1 P3=6; rewrite addresses root causes #1 (D-g's fail-closed rule extended to the running bound: one failure exit, its `catch (...)` and a deadline arm completed before its instant, routed through the expiry exit of its phase, with the `cut` marker), #2 (the establishment cap's two waits stated as independent sleeps on an Engine session, with the condition and its recipe; the optional single wait not taken), #3 (the listener's generic-arm cell and seam, the no-spin counting decorator and bounded drive, the cap cell list aligned, the checklist's D-g refresh). Reviews: research/reviews/codex_105-transport-liveness_gate_a_4_review.md, research/reviews/opus_105-transport-liveness_gate_a_4_adversarial_review.md.
- Where each loop-2 round-1 root cause landed: #1 data-model E-1 (class W), E-2 (the latch's clear list, the second
  setter, the close frame's spawners, I-2's exception, I-5), E-3 (the field `ending_wait_.cut`, "Leaving
  `writing`", "The bound's loop", "The bound's exits", rows 3, 4 and 16); research D2 items 8 and 11; contract C-1.b,
  C-2 W-9, C-2.5, C-2.7; spec FR-004b; quickstart sections 3 and 5; Constitution Check XI and IX. #2 data-model E-3
  ("The establishment cap"); research D2 item 8; Constitution Check XII; "Follow-ups to file before Gate B". #3
  research D8 and D10; quickstart sections 3 and 5; Constitution Check IX; `checklists/requirements.md`. Two
  follow-ups (the direct driver's shared timer slot, the pump's establishment-race spin) join L-105-f's in
  "Follow-ups to file before Gate B". Beyond the review: the marker `ending_wait_.cut` (data-model E-3) is this
  pass's addition. Routing the failure exit through row 4 exposed that row 4 itself had no decided "cut": a write
  completing between the close frame's spawn and its `close_async()` reported transmitted and would arm a wait with
  no bound running. It has its own guard cell and mutant, and is for the next review to judge.
- Loop 2 round 2 CONVERGED 2026-11-21: Codex P1=0 P2=0 P3=3; Opus post-judging P1=0 P2=0 P3=6; P3s applied post-convergence. Reviews: codex_105-transport-liveness_gate_a_5_review.md, opus_105-transport-liveness_gate_a_5_adversarial_review.md.
- Post-Gate-A `/speckit-plan` re-run 2026-11-21 (consistency and amendment pass, no design change): the orchestrator's
  [seam]/[guard] decision and the base wording (spec clarification "Session 2026-11-21 (post-Gate-A plan re-run)"; FR-017; quickstart section 2;
  tasks.md Execution rules), the Engine-over-TLS rig named (research D10), the Constitution Check re-derived against
  the regenerated tasks.md (VII §3, §7, §8, XI §6 added), Project Structure completed, and tasks.md's contract-clause
  traceability added.

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

- **Codex #13** (the pre-Gate-A tasks.md's references to its own T107 and T109) and **its T010's evidence** for the
  Article X §7 predicate: tasks.md is regenerated after convergence, so those numbers name nothing now; the recipe is in
  research D9 and the reading is the regenerated tasks.md's T134.
- **Codex #14** (the requirements checklist certifies disproved claims): `checklists/requirements.md` is outside
  this pass's edit set; its "unambiguous" and "RED-first cell per FR" items are to be re-opened until the
  regenerated tasks agree.

### Round 2 — disagreements

Findings the Opus review marked "Disagree", or whose fix shape it or this rewrite chose differently from Codex's;
Codex's fix is not applied as proposed.

- **Codex #3, "suppress the Heartbeat whenever `pending_test_req_id_` is non-empty"** (Opus: Disagree). Not applied:
  FR-010 and C-6.3 qualify the suppression with inbound silence, and QuickFIX C++ resumes Heartbeats after any frame
  (`Session::verify` runs `testRequest(0)` on every accepted frame; `SessionState::needHeartbeat` is
  `(now - lastSentTime()) >= heartBtInt() && !testRequest()`). Suppressing them would be a new departure that ruling
  D-c does not ask for. What survives is Opus's N-2, applied the other way: the Heartbeat is due again at once after
  such a frame (data-model E-4), with the cell asserting it appears (restated in round 3, root cause #3: at its
  instant, or at the loop's next wake).
- **Codex #1, fix shape** (an RAII publication guard that transfers the decrement; on the bound's failure "resolve
  the wait, undo the Logout owner, record the ending, owe the close"). Opus's precedent is applied instead: the join
  is the coroutine's parameter (`on_inbound_active_too_high_logout_`'s `peer_logout_bound_join`), the latch is cleared
  only after the spawn returns, and the bound's spawn now happens before the write, so a failure there sets the phase
  back to `none` and the ending proceeds as with timeout 0 (data-model E-3 row 2; superseded in round 3 by ruling
  D-g: the Logout is not written, row 2a). Severity taken at P2 (Opus's
  downgrade: an allocation failure, not peer-reachable).
- **Codex #2, severity and shape.** Taken at P2 (Opus's downgrade); ruling D-f fixes the runs and adds the pump case
  to `engine_send_bench` instead of a new pump benchmark binary.
- **Codex #4, fix shape** ("store the establishment deadline in Session state"). Not applied: a direct Session has no
  establishment deadline at all (`logon_timeout_ms` reaches only the pump), so no cap is missing; C-2.6 and FR-004 are
  scoped to Engine sessions and a direct-Session cell pins the bound alone (Opus's downgrade to P3).
- **Codex #5, fix shape** (Opus: add "except a write that never completes" to FR-003 and FR-004). Superseded by ruling
  D-e: the write is now limited, so FR-003's "MUST still close" holds without an exemption. Opus's other items are
  applied: the slot-74 record is written before the write (the `logout_sent` field dropped), OQ-3 is closed in plan
  and research, and L-105-f names its follow-up.
- **Codex #6, severity.** Taken at P3 (Opus's downgrade, widened to B-103-10 and B-104-6); the `session.hpp` comment
  is named in FR-016 as Codex asks.
- **Codex #7, fix shape** (one cell per error source). Opus's axis is applied instead: one cell per disposition (an
  error with or without `Disconnected`, a success with it, a non-pump root), since what decides the outcome is the
  disposition, not the source.
- **N-7** (disclose D-c's late-answer disconnect, or rule). Ruled by D-d: the late answer is liveness, so the case
  no longer disconnects and needs no disclosure; its cell flips to `..._StaysActive`.
- **N-3's `SecondDisconnectedWrite_OwesNothing`.** Dropped with its mutant, as Opus's second option; data-model E-2 I-5
  is a census row.

### Round 2 — not applied in this pass

- tasks.md is still non-operative and is regenerated after convergence (header note added).
- `checklists/requirements.md` is refreshed in this pass (round 1's Codex #14): items that depend on the regenerated
  tasks are unchecked.

### Round 3 — disagreements

No finding of the round-3 Opus review is marked "Disagree". The counter-proposals it judged against, or that this edit
did not take, are recorded here; Codex's fix is not applied in these forms.

- **Severity of Codex #1, #2 and #3 (P1).** Taken at P2 (Opus's downgrade): #1 needs an allocation failure and the
  constitution has no out-of-memory article; #2 delays one Heartbeat by at most T − H (0.2 × HeartBtInt at the default T) and disconnects nothing; #3's
  overrun is bounded by `logout_disconnect_timeout_ms`, where `main` is unbounded in the same case. Codex #4, #5 and
  #6 are taken at P3, #7 at P3.
- **Codex #1, "adopt terminate-on-OOM".** Not applied: the owner ruled the synchronous-close fallback (D-g), so the
  spawn seams and their cells stay.
- **Codex #2, "give inbound refresh an explicit liveness wake".** Not applied: a new mechanism with a per-frame
  test, for a Heartbeat whose delay disconnects nothing. Its other option, the sleep set with the not-yet-eligible
  Heartbeat instant, is applied, with the spin trap Opus names; FR-010 and C-6.3 state the max(0, T − H) delay.
- **Codex #3, "an independent pump watchdog".** Not applied: a third detached frame where the ending bound already
  exists; the cap is a second race arm of that bound. Opus's text-only alternative (the cap holds once the write
  has returned) is not taken either: the mechanism is applied.
- **Codex #4, "remove the destructor-zero assertion".** Not applied: the assert is kept, every "may outlive",
  "is legal" and "exempt" sentence is deleted, and `close()` joins the close frame twice (N-3).
- **Codex #5, "construct the actual reachable predecessor write" before Active.** Not applied on an Engine session,
  where the shape is unreachable (`run_connect_loop` awaits the Logon write inside `drive_reconnect` before the pump
  starts; the acceptor writes nothing before its refusal). D10 says so; the reply's queued cell is added as Codex
  proposes.
- **Codex #6, "keep the gate in a non-installed internal header".** Not applied: `session.hpp` is installed and
  holds the gates by value, so the type must be complete in an installed header. The header is accounted instead.
- **N-2, "read the deadline after the store step".** Not applied: R-1's limit and the pre-Active cap must hold for
  a Logout held in its store call. The two consequences are disclosed (C-2.5, B-105-2).

### Round 4 — disagreements

Loop 2 round 1 (review files suffixed `_4`). No finding of the Opus review is marked "Disagree". Codex's fix is not
applied in these forms:

- **Codex #1, severity and counter-proposal** ("two waits race on one single-slot clock timer", P1; "register one
  `await_deadline` when the clocks are the same object", with a slot-faithful clock cell). Taken at P3 (Opus's
  downgrade): an Engine session's coroutines run on the type-erased session strand, so `system_clock_source` gives
  each sleep its own timer, and `mock_clock` keeps one waiter per sleep. The single wait is not a correctness fix: if
  session coroutines did take the slot, the bound's wait would also clobber a parked liveness sleep and the pump's
  establishment race, which one wait at `min()` does not close. The condition is stated instead (data-model E-3, "The
  establishment cap"), the optional single wait is not taken, and the direct-driver case is a follow-up.
- **Codex #2, input.** Taken at P2 with Opus's corrected input: a held store call alone only delays the close; the
  unbounded case is a bound that fails while the Logout's write is held behind a peer that does not read. The fix is
  Opus's shape (the failure exit is the expiry exit), not "spawn the transport-only close frame" for the `catch`
  alone, so the absorbed-exception cause (N-1) takes the same exit.
- **Codex #3, severity.** Taken at P3 (Opus's downgrade: Article IX §1 is assessed at `/speckit-verify`); the cell
  and its seam are added rather than a waiver.

## Complexity Tracking

> No Constitution Check violation; nothing to justify.

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| — | — | — |
