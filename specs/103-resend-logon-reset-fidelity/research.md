# Research: 103 resend and Logon-reset fidelity

**Provenance.** Drafted by two read-only Opus design runs through `.claude/scripts/claude-triage.sh` (pass 1 $10.87: D1–D8; pass 2 $5.21: D5′ and D9, after the owner widened #588 and #581/FR-018 on 2026-11-14), spot-checked by the orchestrator against source (Arm B's silent ending; QuickFIX's too-low text at QFC `doTargetTooLow` and QFJ `Session.java`). **Where "Pass-2 amendments" (last section) contradicts an earlier section, the amendment governs**; Gate A reviews the whole. **Gate A round 1 (2026-11-14)** edited the sections in place (D1, D4.2, D4.4, D4.6, D5′, D6, D7, D8, D9, the RED cells, the B&L delta, the Risks) and folded into them the pass-2 items it touched; the plan's Constitution Check replaces this file's. Each round-1 change names its finding (Codex #n, N-n, RC-n).


The code was read on branch `103-resend-logon-reset-fidelity`, based on `2c74af92`; its session code is identical to `0ae8e465`. Citations name symbols, not lines: re-locate each by name. Facts about QuickFIX C++ (QFC), QuickFIX/J (QFJ) and QuickFIX/Go (QFGo) come from the pre-spec research (`research/reviews/opus_103_preSpec_research_2026-11-14.md`) and were not re-read here. The owner's rulings are final. Where a ruling cannot be built as written, the Risks section says so.

Notation: **N** = NextNumIn. **S** = the MsgSeqNum of a too-high Logout. **T** = `logout_disconnect_timeout_ms`. **HB** = HeartBtInt. "The mark" = the pending-Logout mark.

---

## Decisions

### D1 — Resend state and the mark end with the connection (#578; FR-006, FR-007, FR-011's last sentence)

*Amended at Gate A round 1 (RC1): the clear moves to the transport install, because the public
`Session::drive_reconnect` can install a new transport from Active with no `Disconnected` edge.*

**Decision:**
- **The connection generation.** A private `std::uint32_t conn_gen_` of `Session`, incremented in
  `Session::install_reconnected_transport`, before its `record_state_transition_(LogonSent)`. Connection-scoped
  recovery work captures it and acts only while it is current (D4.4).
- **Clearing site for the resend state: `Session::install_reconnected_transport`.** There, before the `LogonSent`
  transition and therefore before `drive_reconnect`'s `emit_initiator_logon_` sends the new Logon (FR-006's own
  point):
  - `reconnect_fsm_.exit_awaiting_resend()` clears the awaiting flag. `ResendState::reset` clears the outstanding
    range, `started_at` and `inbound_filled_through`.
  - The mark is cleared (`peer_logout_.phase = none`), `conn_gen_` moves, and the bound's wake and the replay
    gate's `wake_all()` are invoked, so a bound of the old connection resumes, sees the new generation, and exits
    with no effect (D4.4).
  - Both calls are `noexcept` and allocate nothing, which `install_reconnected_transport`'s `noexcept` requires.
    `ResendState::reset` empties `inbound_held`, which nothing ever writes.
- **The `Disconnected` edge** (`Session::record_state_transition_`) keeps the two wakes (the bound's,
  `replay_gate_.wake_all()`), and clears nothing. The edge is where `close()`, every arm's fatal branch, the gap
  deadline and the Logout routes end the connection, and the bound and gate waiters must stop waiting there
  (D4.4, D5′). One enum comparison per state transition, not per frame.
- **Why the edge clears neither the resend state nor the mark.** Each clear needs a cell its mutant turns RED.
  After a `Disconnected` with no reconnect, neither has a reader that acts: `resend_gap_deadline_`, the too-high
  sub-arms and `close_filled_resend_gap_` run only in the Active arm, and every reader of the mark (`send_body_`'s
  refusal, `close(graceful)`'s claim inside its Active/LogonReceived phase-1 block, the bound's "live" test) also
  requires Active. A clear there would be an unkillable mutant. The plan's phase-1 census re-derives both reader
  sets (codegraph callers of `ResendState`, `ReconnectFsm`'s resend accessors and `peer_logout_`); if any reader
  runs outside Active, the edge clear is added with its own cell.
- **The acceptor.** `Session::attach_accepted_transport` is called once per Session: the accept loop builds a fresh
  `Session` for each accepted connection (`run_accept_loop`'s `local_session`), so no in-place re-attach exists.
  The phase-1 census confirms it by reading; no clear is added there.
- **Disclosed residual (the pre-existing in-place-swap class).** An emit already past its last generation re-test
  when a direct caller installs a new transport (a `Session::send` mid-store today; after 103 also a claimed
  acknowledgement in its store or write) writes its frame on the new transport, because `live_write_serialized_`
  reads the live transport at call time. 103 bounds the bound's part of it: after its emit the bound re-tests the
  generation before any state write or event (D4.4), so it never records `Disconnected` against the new
  connection. The frame itself is the pre-existing class, disclosed with B-103-9.
- **Doc fix in `resend_state.hpp`.** Replace "reset on gap close (awaiting_resend=false) or session exit" with:
  reset by `ReconnectFsm::exit_awaiting_resend`, which is called when the gap closes and when a new transport is
  installed (`Session::install_reconnected_transport`). Name feature 103 in that comment, so it points at the
  decision that replaced the old wording.
- **A side effect.** The defect in pre-spec (c)8 disappears: `close_filled_resend_gap_` refreshing `started_at`
  against an `outstanding_end` left from numbering before a reset. That stale end can no longer survive a
  reconnect.

**Rationale:**
- All three engines clear resend state when they disconnect: QFC's and QFJ's `disconnect()`, and QFGo's
  `latentState`. fixpp's equivalent of "the old connection is gone" for an in-place reconnect is the transport
  install, which every reconnect passes through whatever state it starts from.
- TC 1B(c) and TC 1S(a) expect a fresh ResendRequest on the new connection.
- One clearing site covers the reset route (`reset_on_logon`, and D2's strict reset) and the 789 route alike.

**Alternatives considered:**
- Clear only on the `Disconnected` edge (pass 1). Misses the main real shape of #578: a direct caller that loses
  the transport reaches `install_reconnected_transport` from Active, so the stale mark's bound passes its tests on
  the new connection and acknowledges the old Logout there (Gate A round 1, Codex #7).
- Make `Disconnected` an enforced precondition of `drive_reconnect`, returning an error otherwise. A new error
  path on a public call (a BREAKING C++ change) for a state the clear makes harmless.
- Clear in `emit_initiator_logon_`. This covers the initiator only, and it is the wrong layer for the mark.
- Clear in `close()`. `close()` is terminal; the in-place route never calls it.

**Reachability, disclosed with the B-row:** the Engine never reconnects a Session in place. `run_connect_loop` ends
with `close(terminal)`, which is permanent, and there is one Session per entry per `Engine::start`. #578 is
reachable only by a direct caller of the public `Session::drive_reconnect`, from `Disconnected` or from Active.

**Sites:**
- `Session::install_reconnected_transport`, `Session::record_state_transition_` (`src/session/session.cpp`).
- `ReconnectFsm::exit_awaiting_resend` (`src/session/reconnect_fsm.cpp`), unchanged.
- The `ResendState` comment in `include/fixpp/session/resend_state.hpp`.
- `include/fixpp/session/session.hpp`: `conn_gen_`.

---

### D2 — The default becomes lenient; strict resets first; the strict acceptor sends a Logout (#682; FR-001 to FR-005)

**D2.1 — The default, on all three surfaces.**
- **C++.** `SessionConfig::reset_seqnum_policy_field`'s member initializer becomes `bilateral_lenient`.
- **C ABI.** It inherits the change with no code edit:
  - `struct fixpp_session_config { fixpp::session::SessionConfig cfg; }` in `src/capi/capi_internal.hpp` wraps the C++ config.
  - `fixpp_session_config_create` (`src/capi/config.cpp`) value-initialises it and sets only `store_factory`.
- **TOML.** It inherits too:
  - `detail::map_scalars` (`src/config/scalar_mappers.cpp`) writes the field only for an explicit `"reset_seqnum_policy"` string, one of the three spellings.
  - `SessionDefinition def;` in `src/config/toml_config_loader.cpp` default-constructs the config.
- The source has no other place where the default is set. The enum values do not change.
- **Doc sites:**
  - `session_config.hpp`, the enum paragraph. Its "Default = bilateral_strict … QFJ-style" is false by source on QFJ as well (`isResetNeeded`).
  - `session_config.hpp`, the field comment.
  - `include/fix/c_api/session.h`: the `(default)` marker, the setter's "The default (bilateral_strict)" paragraph, and the enumerator descriptions. "Always reset regardless of peer 141=Y" and "Either peer presenting 141=Y is sufficient" both misstate the policy table.
  - `admin_messages.hpp`, the 141 comment.

**D2.2 — The strict initiator resets durably before its Logon.**
- **Change in `Session::emit_initiator_logon_`:** define `reset_before_logon = cfg_.reset_on_logon || policy == bilateral_strict`. Use it in two places:
  - `ensure_hydrated_`'s `apply_inbound_seed = !reset_before_logon`;
  - the `reset_seqnums_to_one_durable(reset_disposition::fatal)` call.
- **The 141 predicate** becomes `(strict || any_reset_knob) && seqnums_at_one`. It was `strict || (any_reset_knob && seqnums_at_one)`.
  - Under strict, `seqnums_at_one` holds right after the reset.
  - FR-002 then holds by construction under every policy: 141=Y implies that 34 = 1 and N = 1 in the manager.
- **"Durably":**
  - Strict and `reset_on_logon` use the `fatal` disposition: a store failure records `Disconnected`, and no Logon is sent.
  - `reset_on_logout` / `reset_on_disconnect` send 141=Y only when the numbers are at 1. On a persistent store those numbers come from the store, so 141=Y implies the store is at 1.
  - A teardown reset whose store call failed (logged-then-proceed, I-07) leaves the store unchanged. The next Session then reads non-1 numbers and sends no 141.
  - This is reasoned, not run; it becomes a cell in the US1 AS5 matrix.
- **This is exactly lenient + `reset_on_logon`'s path today**, which is already pinned:
  - `ResetSeqnumPolicyMatrixTest.ResetOnLogon_Initiator_BilateralLenient_PeerAck141_ByPeerRequestFalse`;
  - `tests/session/test_reset_on_lifecycle.cpp`.
- **INV-RoL-3 / `refresh_on_logon`.** Keep the gate that suppresses `refresh_on_logon` under strict at both call sites; the reset would overwrite a refresh anyway, so it has no observable effect. Rewrite the reason:
  - in `session_config.hpp`'s `refresh_on_logon` comment;
  - in the comments in `emit_initiator_logon_` and `on_inbound_not_connected_`.
  - The new reason: "under strict the Logon always resets, so a refresh has nothing to set".
- **Cost (ruled):** strict now gives up persistent recovery on every Logon, reconnects included.

**D2.3 — The LogonSent arm needs no reorder.**
- After D2.2, N = 1 when the acknowledgement arrives. `check_inbound(1)` is in sequence, so N becomes 2 and `logon_inbound_advanced_init` is set. The 141 read follows.
- The reset unit then runs with `unit_in = 2` and `unit_out = own_logon_sent_reset_flag && n_pre_outbound == 2 ? 2 : 1`, which is 2. The next application message carries 34=2 (US1 AS2).
- A strict acknowledgement without 141=Y is still refused with `session_seqnum_reset_mismatch`, as today.
- **The ordering question in the brief — a named residual.** Under lenient (now the default) or unilateral, consider a 141=Y acknowledgement at 34=1 that fixpp did not ask for, while the hydrated N is above 1.
  - `on_inbound_logon_sent_` checks the sequence before it reads 141, so this acknowledgement is too low and ends the session silently.
  - QFC and QFGo accept it (QFC: `verify(logon,false,true)`). QFJ takes 34=1 as a reset (QFJ-383).
  - It is not among FR-005's observables.
  - Option (i): pre-scan 141 ahead of `check_inbound` in the LogonSent arm, and withhold or reset as the acceptor does.
  - Option (ii): an L-row plus a follow-up issue.
  - **Recommend (ii)** for 103. No ruling covers (i), and (i) changes the reset unit's targets.
  - SC-002's live Logon cells meet this case only if a counterparty acceptor runs `ResetOnLogon=Y`. Check the interop matrix's configs before the live runs.

**D2.4 — The strict acceptor's refusal: move it, and send a Logout with Text.**
- **Today** the refusal in `on_inbound_not_connected_` sits after `check_inbound`. Three problems follow:
  - an out-of-sequence Logon without 141 ends silently before it reaches the refusal;
  - an in-sequence one is refused with N advanced in memory but not persisted;
  - a strict acceptor with `reset_on_logon` has already reset its store for a Logon it then refuses.
- **Decision:** move the refusal to the TestMessageIndicator posture guard's position. That is after `ensure_hydrated_` and the SendingTime guard, and before the `reset_on_logon` block and `check_inbound`. The posture guard's reasons apply unchanged: N is not advanced on a refusal, and `peek_outbound` gives the durable N for the Logout's number.
- **Body:**
  1. If `!peer_sent_reset && strict`, call `co_await refuse_logon_with_logout_(kStrictResetText, fsm_state::NotConnected)`.
  2. If the helper fails, return its error.
  3. If the arm was superseded (`logon_arm_superseded_`), return success (the D-CAN-5 convention).
  4. Otherwise return `std::unexpected(session_seqnum_reset_mismatch)`. The helper itself returns success, so the arm must return the mismatch explicitly.
- **Text.** QuickFIX has no text for this, because no engine refuses such a Logon; FIX-SL §4.4.3 asks for a Logout with Text. Proposed constant: `kStrictResetText = "ResetSeqNumFlag(141)=Y required"`. It is ASCII, carries no peer bytes, and its length is fixed.
- **Bound.** The helper builds into its 256-byte stack buffer. A Logout that does not fit, because of long CompIDs, is not sent; the arm still records `Disconnected` and returns the mismatch. That is the helper's existing fail-closed behaviour, disclosed in the row.
- **Wire effects of the move:**
  - an out-of-sequence Logon without 141 now draws the Logout;
  - a strict acceptor with `reset_on_logon` no longer resets its store for a refused Logon.

**D2.5 — `unilateral`: no change.** The initiator predicate stays equal to lenient's, the acceptor still does not mirror 141, and FR-002 holds for it through the same predicate.

**Rationale:**
- FIX-SL §4.4.2 and all three engines send 141=Y only at 34=1, after resetting.
- Reusing `reset_on_logon`'s proven path avoids a second reset path (`feedback_half_restructure_symmetric_api`).

**Alternatives considered:**
- QuickFIX's predicate under strict, option (b). Ruled out.
- Reordering 141 ahead of the sequence check for strict. Not needed after D2.2.
- A reset helper for strict only. It would duplicate the proven path.
- Dropping the INV-RoL-3 gate. No gain, and one more pin to change.
- Leaving the refusal after `check_inbound`. FR-004 would then hold only for in-sequence Logons.

**Sites:**
- `SessionConfig` (`include/fixpp/session/session_config.hpp`).
- `Session::emit_initiator_logon_`, `Session::on_inbound_not_connected_`, `Session::refuse_logon_with_logout_` (unchanged), `Session::on_inbound_logon_sent_` (unchanged).
- `include/fix/c_api/session.h`.

---

### D3 — BeginSeqNo(7)=0 and an absent tag 7 (#580; FR-012, FR-013)

**Decision:**
- **Parse site.** `Session::on_inbound_active_resend_request_` calls `parse_seqnum(hdr.begin_seqno)`. `scan_frame_header` (`src/session/scan_frame_header.hpp`, `case 7`) records the value only.
- **Presence.** Add `bool begin_seqno_present` to `detail::FrameHeader`, set in `case 7`, mirroring `next_expected_present`.
  - `FrameHeader` is internal (`src/session`), so this changes no public layout.
  - Rejected alternative: testing `begin_seqno.data() == nullptr`. A default-constructed `string_view` gives null today, so it works, but that is an implicit contract a rewrite of the scan could break silently.
- **Absent → Reject 373=1.**
  - A new sub-arm helper, `Session::answer_resend_request_(tic, hdr, in_sequence)`, also used by D4: when `!begin_seqno_present`, call `emit_session_reject_(seq, "2", 1, 7, session_reject_reason_text(1))` and do not walk.
  - The text is "Required tag missing", from 101's QuickFIX table.
  - **In sequence:** Guard (4)'s `check_inbound` has already advanced N, so call `persist_inbound_advance_()` first, then send the Reject. This is the #423 order that `on_inbound_active_unsupported_` and `on_inbound_active_from_admin_failed_` use.
  - **Too high** (D4): nothing is consumed and nothing persisted.
- **A present but empty or non-numeric 7** parses to 0 and is answered as 7=1. Disclosed. A validating session has already Rejected it at the gate.
- **The clamp.** At `Session::replay_outbound_range_`'s entry, before the tail read: `if (begin < seqnum_min) begin = seqnum_min;`. The empty-store branch's own `begin > 0 ? begin : 1U` then becomes dead and is removed.
  - With the clamp in the walk, no caller can make it write 34=0.
  - `honor_peer_next_expected_` refuses X = 0 before it calls the walk, so the clamp changes nothing on the 789 path.
- **On a validating session,** the gate's Reject (373=1, 371=7, the same text after 101) comes before the arm, but only if the session dictionary marks BeginSeqNo required in ResendRequest.
  - The plan verifies the vendored FIX42/FIX44/FIX50SP2 dictionaries, the minimal test dictionary and the validation test dictionary.
  - Otherwise the arm is reached only on a non-validating session.
  - The RED cell asserts that the two Rejects are byte-identical apart from 34 and 52.
- **`fromAdmin` order.** For an in-sequence frame the Active block calls `fromAdmin` before the sub-arm runs, so `fromAdmin` sees the frame before its Reject. That is QFC's order: `verify` → `fromCallback`, then the `getField` throw. Unchanged.

**Rationale:** the rulings, TC 14b (the Reject and the N increment), and QFC/QFJ's observable answer to 7=0.

**Alternatives considered:**
- Clamp at the callers. After D4 there are three; one clamp in the walk covers all of them.
- Keep "absent means 0". R-10 is superseded.
- Consume through `consume_rejected_seqnum_`. That helper serves Rejects that run before Guard (4); after it the number is already taken.

**Sites:**
- `detail::scan_frame_header` / `FrameHeader`.
- `Session::on_inbound_active_resend_request_`, `Session::replay_outbound_range_`, `Session::emit_session_reject_` (unchanged).
- `session_reject_reason_text` (anonymous namespace, `session.cpp`).

---

### D4 — A too-high ResendRequest is serviced; a too-high Logout is acknowledged, bounded (#577; FR-008 to FR-011)

**D4.1 — Dispatch inside the two too-high sub-arms, not ahead of the gate.** A gate-level MsgType test would sit on every frame's fast path. With `validate_sequence_numbers` on, a too-high frame in Active reaches one of two sub-arms.

- **(a) No resend outstanding:** Guard (4)'s gate → `Session::on_inbound_active_too_high_`. By MsgType:
  - `"2"`: `answer_resend_request_` (no persist), then today's `enter_awaiting_resend`, `mark_resend_progress` and `inbound_resend_request_leaf_`. The retransmission goes first, then fixpp's own ResendRequest (FIX-SL Table 3, TC 20).
  - `"5"`: D4.2.
  - A failed answer records `Disconnected`, as for an in-sequence request.
- **(b) A resend outstanding:** `check_inbound` refuses the frame → `Session::on_inbound_active_out_of_sequence_`. Insert the dispatch after `resend_gap_deadline_` (D6) and the Heartbeat arm, and before the PossDup arm and the `session_seqnum_too_high` ignore:
  - `chk_error == session_seqnum_too_high && msg_type == "2"` → answer it, and send no second ResendRequest (FR-009).
  - `"5"` → D4.2.
  - It goes before the PossDup arm because that arm's admin row would silently drop a ResendRequest carrying 43=Y, where route (a) answers it.

In both routes:
- N does not move and nothing is persisted.
- No `fromAdmin`: fixpp hands no out-of-sequence frame to `fromAdmin`. QFC calls it at receipt. Disclosed.
- With `validate_sequence_numbers` off, nothing changes: the knob-off branch delivers the frame to `fromAdmin` and answers nothing. Disclosed.

**D4.2 — The mark.**
- **State:** a private member `peer_logout_` of `Session`: `{ seqnum_t seq; steady_time_point at; enum class phase : uint8_t { none, pending, acking } }`.
- **Where it is set:**
  - In (a) for `"5"`, before fixpp's own ResendRequest.
  - In (b) for `"5"` when the phase is `none`. A second too-high Logout while the mark is set is ignored, as under B-537-1.
- `logout_seen_ = true` is set at receipt, as `on_inbound_active_logout_` does first, so a configured `reset_on_logout` fires whichever ending follows.
- **Active only, and LogonReceived cannot see one (by reading; re-confirmed by the phase-1 census).** `on_inbound_not_connected_` records `LogonReceived` and, in the same inbound chain, either records `Active` after the reply Logon (and the 789 honour) or ends at `Disconnected` (a refusal, a failed reply, a supersession by `close()`); `run_read_pump` awaits `on_inbound_frame` once per frame, serially (B-101-10's precondition). So no frame other than the Logon itself is processed in LogonReceived, and FR-010, E9, FR-018, E14, D9 and B-9 are scoped to Active on this one result. The census re-derives it: every `record_state_transition_(fsm_state::LogonReceived)` site and every exit of the chain after it.
- **T == 0, or no clock (`effective_clock_` null):** no mark. Acknowledge at once in the arm (D4.5): the QuickFIX ending, with no ResendRequest of fixpp's own (Risk R3).
- **0 does reach the session.** The TOML mapper accepts any uint32, and `SessionConfig` has no validator. The header's "Must be > 0; validated at SessionConfig-build time" is stale; fix that comment.

**D4.3 — What closes the mark: N passing S.**
- **The sites that advance N in Active:**
  - Guard (4)'s `check_inbound`, which is then followed by `close_filled_resend_gap_`;
  - `consume_rejected_seqnum_`, also followed by `close_filled_resend_gap_`;
  - `apply_inbound_sequence_reset`'s `set_next_inbound`, in the GapFill and Reset-mode sub-arms. This site calls no helper today.
- **Why `close_filled_resend_gap_` cannot host the check:** it returns early when no resend is outstanding.
  - Trace for route (a): the recorded end is S−1. The replay of S−1 closes the gap at N = S. The peer's GapFill at 34=S then moves N to S+1 with the flag already false, so a check in `close_filled_resend_gap_` would miss the moment N passes S.
- **Decision:** a new `noexcept` helper, `Session::note_inbound_advance_()`:
  - it calls `close_filled_resend_gap_()`;
  - then, if the phase is `pending` and `next_inbound_unsafe() > peer_logout_.seq`, it wakes the bound's wait.
  - The test depends on the mark alone. It is called from all three sites; in `apply_inbound_sequence_reset`, after `set_next_inbound` succeeds.
  - Fast-path cost when no mark is set: one byte compare, the same reasoning as `close_filled_resend_gap_`'s comment cites ([const §VIII.5]).
- **The frame path does not acknowledge inline.** It only wakes the bound's coroutine, which is the one emitter for both endings. This adds no await to the inbound fast path.

**D4.4 — The bound: a detached coroutine on the session strand.**
- **Why not the liveness loop:**
  - `run_liveness_loop` returns at once when HB is 0, and FR-010 has no HB exemption.
  - It sleeps toward its own deadlines, up to HB, and has no per-session wake: a sweep of the clock was rejected (#536, D-536-1).
- **Why not frame-driven, as #537 did for the gap deadline:** US3 AS4 needs the acknowledgement at expiry even if no frame arrives. #537's "a timer for the deadline" rejection was about the gap deadline, not this bound.
- **`Session::run_peer_logout_bound_()`** (protocol amended at Gate A round 1, RC1; the pass-2 claim placement
  stays):
  - It is `co_spawn`ed when the mark is set, on `co_await asio::this_coro::executor` in the arm: the session
    strand, as the liveness loop is spawned. It captures `gen = conn_gen_` (D1) at spawn.
  - **Its exit test, "live"**: `conn_gen_ == gen`, the phase is `pending`, `fsm_state_ == Active`, `state_` is
    neither `closing` nor `closed_drained`, and `engine_stop_requested_` is clear. Whether `co_spawn` runs the child
    up to its first suspension or posts its start depends on the asio version; the protocol relies on neither.
  - **Publish, then re-test.** It first publishes its wake (`peer_logout_wake_`), then tests "live"; if it fails, it
    exits with no effect. An ending that ran before the child started (a `Disconnected` edge, an install) found no
    wake to call, and this test is what sees it (Gate A round 1, Codex #3).
  - **The T race.** It races a frame-local `asio::steady_timer` at `time_point::max()` against
    `detail::await_deadline(*effective_clock_, at + T)`. This is `run_logout_phase1`'s pattern: the outcome is read
    from session state afterwards, never from which arm of the race won. When the race ends, the wake is cleared
    (set to null) **before** anything else, so no later `close()` or edge calls through a reference to the
    frame-local timer once the race is over.
  - **After the race:** re-test "live"; exit with no effect if it fails.
  - **The gate wait (D5′):** the shared hold for the acknowledgement's emit. On the contended path, "live" is
    re-tested after every wake of the gate wait, and it exits with no effect if it fails.
  - **The claim:** with the hold taken and "live" true, it sets the phase to `acking` synchronously, before its
    first suspension, then sends the acknowledgement (D4.5).
  - **After the emit:** it re-tests `conn_gen_ == gen` after every suspension of the emit, and performs the
    acknowledgement's state writes and event (D4.5's `emit_event`, `record_state_transition_(Disconnected)`) only
    if the generation is unchanged. A direct caller's install during the emit therefore cannot end the new
    connection; the frame already handed to the transport is D1's disclosed residual.
- **Cancellation and lifetime:**
  - It is **not** bound to `root_cancel_.slot()`. A `cancellation_signal`'s slot holds one handler; the liveness
    loop is bound there, and a second binding would displace it, so `close()`'s liveness join could wait out a
    whole HB.
  - **The wake:** a member `std::function<void()> peer_logout_wake_`, set by the coroutine and cleared when its race
    ends and on every exit by a guard. This is the idiom of `logout_wait_wake_`, `reset_unit_wake_` and
    `close_grace_wake_`. The `std::function` holds one reference capture, so it does not allocate.
  - **Wake sources:** `note_inbound_advance_` (N passed S), every D4.6 claim of `pending → none`, the
    `Disconnected` edge (D1), `install_reconnected_transport` (D1), `note_engine_stop_`, and `close()`, which calls
    it **unconditionally** at entry, not only through the edge. An edge's wake would be lost if it landed on the timer
    before the race's wait is initiated. In the vendored asio (1.38: `co_spawn_entry_point` begins with
    `co_spawn_dispatch`, so a child spawned from the strand starts inline) the wait is pending before the bound
    first suspends, so the window does not arise there; the protocol is written not to depend on that, and
    `close()`'s later wake is the backstop for an asio whose start posts (cell F2, whose hook stands in for such a
    start).
  - **The join:** a `std::shared_ptr<std::atomic<int>>` counter, incremented before `co_spawn` and decremented by a
    frame-local guard that touches only the shared counter (`liveness_counter_`'s FQ-C pattern). The guard also
    cancels the frame-local `bound_done` timer that `close()` may be racing on (D4.6), so a join waiter is woken
    when the bound exits.
  - `close()` wakes the coroutine at entry, joins an `acking` bound inside its grace (D4.6), and waits for the
    counter to reach 0 next to the liveness join, before the teardown reset and the drains.
  - `Engine::stop()`'s step 1 sets `engine_stop_requested_` and calls `note_engine_stop_`, which wakes the bound;
    its "live" test then fails, and stop's `close()` joins it.
  - **Precondition, stated in the plan:** `close()` runs before the Session is destroyed. The liveness loop already
    relies on this, because its sleep resumes into `this`.
- **Allocation:** one coroutine frame per too-high Logout, on a cold path. [const §XV.1]'s hot path is untouched.

**D4.5 — The acknowledgement.**
- **The frame:** `inbound_logout_leaf_(tic, st52, {})`, the same frame `on_inbound_active_logout_` sends. Its buffer is its own 256-byte stack array; it does not use the inbound reply slot, which belongs to the inbound chain alone (B-101-10).
- **Then the in-sequence route's side effects:**
  - `emit_event(session_event_sequence_numbers_reset{.by_peer_request = false})`;
  - `record_state_transition_(Disconnected)`, on whose Active edge `onLogout` fires.
- **What does not happen:**
  - No `fromAdmin`, because the frame is not kept.
  - No inbound persist, because the Logout's own number was never consumed. On the timeout route the durable N stays at or below S, so a reconnect asks for the gap again. Disclosed.
- **After the acknowledgement:** the in-sequence Logout's ending.
  - The session is `Disconnected` and the transport stays open.
  - `run_read_pump` closes only on EOF, a read error, a frame too large, or a failed `on_inbound_frame`, so the session waits for the peer to disconnect, or for `close()` or `Engine::stop()`.
  - This is not `close(graceful)`'s timed wait (Risk R2).

**D4.6 — At most one Logout, exactly one except FR-010's cases: the claim protocol.** Everything runs on the strand, and every claim is made synchronously, before the claimer's first suspension. (Amended at Gate A round 1, RC2; folds in pass 2's Arm B row and the claim after the gate wait.)

| Ending | Claim | Then |
|---|---|---|
| The bound (N passed S, or T expired) | `pending → acking`, after its gate wait (D4.4) | sends the acknowledgement |
| `resend_gap_deadline_` expires | `pending → none` and wake; if the phase is `acking`, return handled with nothing | its own B-537-2 Logout |
| `on_inbound_active_logout_` (the peer resends its Logout in sequence) | `pending → none` and wake | its own acknowledgement is the one Logout |
| Arm B (too low, 43≠Y; D9) | `pending → none` and wake | its own Logout is the one Logout |
| `close(graceful)`, at entry | `pending → none` and wake | phase 1's Logout is the one Logout |
| `close(graceful)`, at entry, phase `acking` | — | phase 1 is replaced by a join of the bound inside the same grace race (below); no Logout of its own |
| `close(terminal)`, the read pump's EOF close, `Engine::stop()` | `pending → none` and wake, or nothing if `acking` | no Logout of its own; a claimed acknowledgement may or may not reach the wire (FR-010 case 4) |
| `on_inbound_active_` in Active while the phase is `acking` | — | the frame is drained, as the LogoutSent row drains (one flag read per frame) |

**`close(graceful)` and a claimed acknowledgement (Gate A round 1, Codex #2).** `acking` means the acknowledgement is being emitted, not that it will reach the wire: `close()` records `Disconnected`, emits `root_cancel_` and closes the transport before its later joins. So when phase 1's block (Active or LogonReceived, not escalated) finds the phase `acking`, it races, in place of `run_logout_phase1()`, a frame-local `bound_done` timer at `time_point::max()` (cancelled by the bound's exit guard, D4.4) against the same `close_grace` timer. It reads the join counter first and skips the race if it is already 0; the `bound_done` wake is published before that read, with no suspension between them. With the vendored asio's inline start the race's waits are pending before `close()` suspends, so the bound's exit cannot fall between the read and the wait; under an asio whose start posts, a lost cancel costs a delay of `close()` up to the grace, never a lost Logout, because the acknowledgement has already completed when the guard runs. The bound's acknowledgement then completes, and records `Disconnected` itself, **before** `close()` records `Disconnected` and before it closes the transport. If the grace runs out first, the force-close path follows as for phase 1 (FR-010 case 5, L-103-6). Cell: US3 AS7 / R-588-e's second arm.

**D4.7 — `Session::send` while the mark is set (FR-011).**
- `send_body_`'s first test becomes `fsm_state_ != Active || peer_logout_.phase != none`, returning `session_invalid_state_for_send`. It is tested again after D5's wait.
- `send_deferred_` ends in `send_body_`, so a send started in the acknowledgement's `toAdmin` is deferred and then refused. That differs from L-563-2's in-sequence route on a `MemoryStore` or `FileStore`, where such a send is admitted. Disclosed.

**Rationale:** the rulings, FIX-SL Table 3 and §4.10 precedence 15 (bounded), and TC 20. Only a mark is kept, not the frame (L-013-2 stands).

**Alternatives considered:**
- Dispatch ahead of Guard (4). Every frame would pay for it, and it would mix with the PossDup Stage-1 ordering.
- Bind the bound's coroutine to `root_cancel_`. Rejected: the slot holds one handler (D4.4).
- An own `cancellation_signal` per coroutine. Viable, and closer to [const §XI.2]'s wording. Rejected in favour of the wake idiom for consistency with the three existing in-session waits; it would also need a cancellation reset before the acknowledgement's awaits.
- The liveness loop. Rejected: HB = 0, and no wake.
- Writing `Disconnected` before the acknowledgement, the R-8 cut-route precedent. Rejected: `onLogout` would then run before the acknowledgement's `toAdmin`, the reverse of the in-sequence route's callback order.
- Acknowledging inline in the frame path. Two emitters means two claim sites, and it adds an await to the fast path.

**Sites:**
- `Session::on_inbound_active_too_high_`, `Session::on_inbound_active_out_of_sequence_`, `Session::on_inbound_active_logout_`, `Session::on_inbound_active_`.
- `Session::close_filled_resend_gap_`, `Session::consume_rejected_seqnum_`, `Session::apply_inbound_sequence_reset`, `Session::resend_gap_deadline_`, `Session::close`, `Session::send_body_`, `Session::record_state_transition_`.
- New: `Session::note_inbound_advance_`, `Session::run_peer_logout_bound_`, `Session::answer_resend_request_`.
- `include/fixpp/session/session.hpp`: new private members. This is a C++ layout change, made before the first release.

---

### D5′ — Outbound serialisation against a resend answer (#588, widened; FR-014 to FR-016)

*Replaces pass 1's D5 (design pass 2; owner ruling 2026-11-14 widened #588).* Notation: **A** = the walk's entry, **Z** = the end of its hold.

#### Decision

**D5′.1 The primitive.** A new, small, strand-local gate: `detail::replay_gate`, in an internal header. `include/fixpp/session/detail/` or `src/session/`; the plan picks. It holds:
- `std::uint32_t shared`: the number of open shared holds.
- `bool exclusive`: set at the walk's entry and cleared at its end. It covers both the walk waiting and the walk holding.
- `std::uint32_t waiters`: the number of coroutines suspended on the gate.
- `std::optional<asio::steady_timer> wake`: created by the first waiter on `co_await this_coro::executor`, with expiry `time_point::max()`. It is lazy because `exec_` is bound at `open()`, not at construction.
- **`wake_all()`:** calls `wake->cancel()` only when `waiters != 0`. It is a no-op otherwise.

The gate has no atomics. `Session::send`'s path is already strand-local: `send_slot_in_use_` and `outbound_callback_depth_` are plain members, and `Engine::send` hops onto the session strand. The gate is a class of its own so that a unit cell can test it without a Session.

**D5′.2 The shared side: the fast path, then a contended wait.**
1. **Fast path.** `if (!gate.exclusive) { ++shared; }`. The RAII holder's destructor runs `--shared; if (shared == 0 && exclusive) wake_all();`. No await, no allocation, no change to the cancellation filter.
2. **Contended wait.** While the gate is exclusive, each iteration does the following:
   1. Apply by hand the pre-check a co_await performs (`send_deferred_`'s OD-1 idiom): if `throw_if_cancelled()` is set and the coroutine is already cancelled, return without waiting. **Placement rule (Gate A round 1, Codex #1):** the pre-check sits in the same coroutine frame as the `co_await` it guards. Every emitter of a per-operation or root slot runs on the strand, and neither `co_await` of the pre-check suspends, so nothing runs between the pre-check and step 3's `await_transform`: there is no race in that form. If the wait is factored into a child awaitable (for example `gate.acquire_shared()`), a pre-check inside the child comes after the parent's `await_transform`, which throws first; `send_body_`'s wait sits outside its `try`, so that throw would leave a `noexcept` coroutine. A factored wait is therefore awaited through the split idiom (`FIXPP_INBOUND_SPLIT_AWAIT` / `FIXPP_INBOUND_SPLIT_ENTRY`), or the pre-check stays in the parent's frame before that `co_await`.
   2. Otherwise run `++waiters` under RAII, so a wait ended by `||` or by frame destruction cannot leak a count.
   3. `co_await wake->async_wait(asio::as_tuple(asio::use_awaitable))`.
   4. `--waiters`.
   5. If `cancellation_state.cancelled() != none`, return `dispatch_aborted`, leaving the caller's cancellation pending (no `reset_cancellation_state`; see "Cancellation").
   6. Re-test the emitter's own predicate (D5′.4).
   7. Loop.
3. **Taking the hold.** The hold is taken with no suspension between the loop's last test and `++shared`.

**D5′.3 The exclusive side, in `Session::replay_outbound_range_`.**
1. **Entry, before the first suspension:** assert `!exclusive` (walks never overlap; see D5′.6), set `exclusive = true`, and build an RAII guard. The guard's destructor clears `exclusive` and calls `wake_all()`. It runs on every ending: completed, superseded, every error `co_return`, an exception, and destruction of the frame.
2. **Wait for open holds:** `while (shared != 0)`, the same pre-check, wait and RAII count as D5′.2. After each wake, if `fsm_state_ == Disconnected`, or the walk's arm was superseded, return `dispatch_aborted`. Callers keep their existing failure handling; any `record_state_transition_(Disconnected)` they make is guarded by `fsm_state_ != Disconnected`.
3. **Where the wait sits:** before the store tail read (`store_->next_seqnum`), not just before the first frame. The tail read sets `our_last` and `eff_end`. A send that took S before A and is still in its store call must be stored before that read, or the answer stops at S−1 while S is written ahead of it. Cell R-588-b.
4. **Coverage:** one site covers every caller: the in-sequence ResendRequest sub-arm, D4's two too-high routes through `answer_resend_request_`, and `honor_peer_next_expected_`'s 789 walk.

**D5′.4 The emitters: census and holds.**

*How the list was derived; re-derive it this way:*
- `grep -rn "assign_outbound\|store_then_emit\|live_write_serialized_\|async_write(\|transport_send_(" src include --include='*.cpp' --include='*.hpp'`
- Known positive: `Session::send_impl` must appear.
- Every outbound number is taken by `seqnum_mgr_.assign_outbound()` in `session.cpp`. Every write goes through `store_then_emit`, or through the walk's own `transmit_async` → `live_write_serialized_` / `transport_send_`. The only `async_write(` in session code is inside `live_write_serialized_`.
- Classify each enclosing function by the root of its call path: codegraph callers up to `on_inbound_frame`, `run_liveness_loop`, `close`, `Session::send`, or a `co_spawn`.

*Where the hold starts.* Every builder stamps 52 and calls `peek_outbound()` before `toAdmin`/`toApp` and before `assign_outbound`. So the contended wait and the hold start **before the 52 stamp and the peek**, and end after `store_then_emit` returns.

*The rule.* A hold must not span any wait whose end depends on inbound processing. The walk waits for exclusive inside `on_inbound_frame`, so such a span is a deadlock.

| Emitter (enclosing function) | Root | Hold |
|---|---|---|
| `send_body_` → `send_impl` (`Session::send`, `Engine::send`, `send_deferred_` after its hop) | caller | **Shared.** The contended wait goes after the Active / mark test and the depth test, before the slot is taken, **outside** the `try`. The holder is declared as the `try`'s first statement, ahead of the slot holder. It spans `build_send_frame_` (peek and stamp), `toApp`, `assign_outbound` and `store_then_emit`. |
| `run_liveness_loop`: the Heartbeat block | detached (`root_cancel_`) | **Shared, per emit.** A contended wait sends the loop back to its top (`continue`), which recomputes both deadlines. The hold spans stamp, peek, `toAdmin`, assign and `store_then_emit`. It is released before the next sleep. |
| `run_liveness_loop`: the TestRequest block | same | **Shared, per emit.** The same wait-then-`continue`. `pending_test_req_id_` is assigned inside the hold. The hold is released **before the grace sleep**, whose outcome depends on an inbound Heartbeat. |
| `run_logout_phase1` (from `close(graceful)`) | `close` | **Shared.** It starts at the function's top, before the stamp and the peek. It is released when `store_then_emit` returns: before `logout_seen_`, `LogoutSent`, and the `logout_wait_wake_` race, which needs the peer's Logout from the inbound chain. |
| `run_peer_logout_bound_`, D4's acknowledgement through `inbound_logout_leaf_` | detached (D4.4) | **Shared, around the acknowledgement's emit only.** It is taken after the T race. The D4.6 claim moves to after the gate wait (§4). |
| `emit_session_reject_` (both overloads), `refuse_logon_with_logout_`, `inbound_reject_leaf_`, `inbound_reject_best_effort_leaf_`, `inbound_logout_leaf_` (inbound callers), `inbound_logon_sent_logout_leaf_`, `inbound_resend_request_leaf_`, `inbound_heartbeat_reply_leaf_`, `inbound_business_reject_leaf_`, `inbound_acceptor_reply_logon_leaf_`, `apply_inbound_sequence_reset`'s Reject, `honor_peer_next_expected_`'s two Logouts. New: D6's deadline Logout, Arm B's Logout, D4's T=0 acknowledgement. | `on_inbound_frame` | **None.** They run in the inbound chain, which is the walk's own chain and is serialised (B-101-10's precondition, asserted). They cannot overlap a walk. Each is awaited before the chain can start one. |
| `emit_initiator_logon_` | `open()` / `drive_reconnect` | **None, by causality** (Gate A round 1, Codex #15; replaces pass 2's "provided that" and its caller census). A walk needs a ResendRequest processed in Active, or the Logon acknowledgement's 789. Both are the peer's answers to this Logon, which `store_then_emit` has numbered, stored and written before the peer can send them, so no walk can start between the Logon's numbering and its write. The only exception is a caller that feeds the old connection's frames during `drive_reconnect`, which breaks the serialised-inbound precondition (B-101-10). Fallback, if Gate A prefers construction to argument: take the shared hold; this path is cold. |
| The walk's `emit_gapfill_async` and its replay `transmit_async` | inside the walk | **None.** They carry the replayed numbers and take no number. |

*Verification that walks run only in the inbound chain.* The callers of `replay_outbound_range_` are `on_inbound_active_resend_request_` and `honor_peer_next_expected_`; after D4, also `answer_resend_request_`. All of them are reached only from `on_inbound_frame` (re-derive: codegraph callers of `replay_outbound_range_`). Because `on_inbound_frame` is serialised, walks never overlap, and an inbound-chain emitter never runs during one. That covers Arm B's Logout and D6's deadline Logout.

**Re-tests after a contended wake.** These run only on the contended path, so the fast path pays nothing.
- **`send_body_`:**
  - Refuse with `session_invalid_state_for_send` unless all of these hold: `fsm_state_ == Active`, `state_ != lifecycle::closing` (`send_deferred_`'s step 5: Active stays set while phase 1 emits its Logout), `!engine_stop_requested_` (`Engine::stop()`'s OD-3), and `peer_logout_.phase == none` (FR-011).
  - A cancelled wait returns `dispatch_aborted`. It leaves the session's state untouched and consumes no number.
- **Liveness loop:** `continue` (the loop condition tests Active).
- **`run_logout_phase1`:** if the state is no longer `Active`/`LogonReceived`, take D-OUT-6's "not sent" exit.
- **D4 bound:** exit with no effect unless `Active`, `phase == pending`, not closing, and not stopping.

**Wake sources.** `wake_all()` is called from:
1. the exclusive guard's destructor;
2. the last shared release while exclusive;
3. the Disconnected edge in `record_state_transition_`, next to the bound's wake (D1);
4. `note_engine_stop_`;
5. `install_reconnected_transport`, next to D1's clears.

Each is one compare when nobody waits.

#### Deadlock analysis

- **The walk waits for exclusive inside `on_inbound_frame`.** It holds nothing while it waits:
  - `check_inbound`'s seqnum lock is scoped to that call;
  - the inbound persist and every reply have completed;
  - the reply slot is idle;
  - `write_gate_` is taken by the walk only per frame, after exclusive is granted.

  So the question is whether a shared holder can wait on the inbound chain:
  - **`assign_outbound`'s seqnum lock.** Its other holders are inbound-chain calls, which have completed, or other shared holders, which are progressing.
  - **The store executor.** It is independent of the strand.
  - **`write_gate_`.** It is held only by shared holders or by the walk after its grant.
  - **The reply slot.** Inbound chain only.
  - **Callback depth.** A send started in `toApp` or `toAdmin` defers and is never awaited by the emitter it started in. A send started inline from `fromApp` takes a free gate, because no walk runs during `fromApp`.
  - **The two waits that do depend on inbound processing** (the TestRequest grace and phase 1's confirmation race) are outside the hold, by D5′.4's rule.
- **Lock order.** Replay gate, then the seqnum mutex, then `write_gate_`. No path acquires the gate while holding either async_mutex, so there is no cycle.
- **Transport backpressure.** A holder blocked in `async_write` delays the walk. Today the walk's own write waits on the same `write_gate_` and socket, so this is not a new class. Its bound is unchanged: `close()` / `Engine::stop()` close the transport.
- **`send_impl`'s `app_callback_threw` → `co_await close(terminal)` inside the hold.** `close()` joins neither the read pump nor the gate.
  - Its liveness join completes because the Disconnected write wakes the gate; the loop is also bound to `root_cancel_` and is at total before its sleep.
  - `write_gate_.cancel_and_drain()` and `seqnum_mgr_.drain()` are not held by the waiting walk.
  - `toApp` runs before `assign_outbound`, so nothing is numbered on that path.
- **`close(graceful)`.**
  - Phase 1's wait is a branch of the `run_logout_phase1() || close_grace` race. The timer winning cancels it (per-operation slot), and the transport is force-closed.
  - Disclosed: a walk longer than T uses up the grace, so no Logout is sent and the force close follows (FR-010 case 5, L-103-6).
  - With the phase `acking`, the grace race joins the bound in place of phase 1 (D4.6). The bound is past its gate wait and holds a shared hold; it waits on no inbound processing, and a walk waits for it, not the reverse. The join is bounded by the grace.
  - Otherwise `close()` writes Disconnected before its joins, so the bound's join (D4.4) and the liveness join never wait behind a walk.
- **`Engine::stop()`.**
  - Step 1 calls `note_engine_stop_`, which wakes the gate. A waiting `Engine::send` then refuses, so step 3's `send_counter_` drain does not wait on the walk.
  - The walk is inside the role loop that step 3 joins. It ends at its next write, which fails after step 2 closed the transport, or when its store read completes. A wedged `FileStore` job (B-598-1) holds step 3 today as well.
- **D4's bound.**
  - It takes a shared hold for the acknowledgement.
  - It claims `pending → acking` **after** the gate wait, synchronously and before its first suspension, which is `assign_outbound`. While it waits, every other ending can still claim `pending → none`.
  - A claimed `acking` therefore means the acknowledgement is being emitted. That is not "will reach the wire", which is why `close(graceful)` joins an `acking` bound inside its grace before it records Disconnected (D4.6).
- **D6's deadline Logout, Arm B's Logout, and D4's T=0 acknowledgement** are in the inbound chain, so they cannot run during a walk. No hold is needed.

#### Cancellation (#583)

- The gate never calls `reset_cancellation_state` and uses no `async_mutex`. A wait inherits its caller's filter and leaves it as it found it.
- **The state after a cancelled wait (Gate A round 1, N5): the cancellation stays pending.** A wait ended by its caller's own cancellation returns `dispatch_aborted` with the session's state unchanged, no number consumed, and the caller's cancellation state still cancelled, so the caller's next pre-checked `co_await` throws. This is `send_deferred_`'s OD-1 return, the same "nothing consumed" ending. `send_body_`'s D-OUT-9 tail, which resets the state and records Disconnected, is a different condition: it runs only after an exception, when something may have been consumed. B-103-8 states the choice; R-588-f(3) asserts it.
- What reaches a wait:
  - **The per-operation slot.** Phase 1's `||` sends a terminal cancellation. The liveness loop's sleep-time total reaches it, because the filter is reset to total before each sleep.
  - **The wake idiom.** The Disconnected edge and engine stop.
- **Disclosed:** a consumer coroutine that has already sent once carries #583's terminal filter, because `assign_outbound`'s `async_lock` leaves it there. A total emit from that consumer is not recorded during the wait, and the wait ends only on a wake (Disconnected, stop, or the walk's end). This is the same reach as that consumer's next `async_lock` today.
- **Compared with pass-1:**
  - `logout_wait_wake_`, `reset_unit_wake_` and `close_grace_wake_` serve one waiter each, through a frame-local timer and a `std::function`.
  - The gate has N waiters, so it keeps one lazily created member timer whose `cancel()` wakes all of them.
- **The witness is a unit cell on the gate alone** (R-588-g). On the send path, the `async_lock` that follows the wait would mask the result.

#### Allocation (Article XV §1)

- **Fast path:** a bool test, an increment, a decrement and a compare. No heap and no await. It meets the #544 zero-allocation send target and FR-015. The XV §1 hot path (parse → validate → dispatch, `MemoryStore`) is not touched.
- **Contended path:** cold, and outside XV §1's scope (the exclusive wait sits only at `replay_outbound_range_`'s entry, reached from a ResendRequest or a 789 walk, never per frame).
  - The `std::optional` timer is constructed in place, once per Session.
  - Each `async_wait` operation goes through the awaitable's recycling handler allocator. That is a claim, not a proof, and N concurrent waiters exceed the recycler's per-thread cache; the contended witness below measures it.
- **The send-path witness (Gate A round 1, N2).** `tests/alloc_guard/test_session_alloc_guard.cpp` cannot be it: its two cells drive `SeqnumManager::assign_outbound` and the pure admin builders, never `send_body_`. The witness is the Release, Linux-only mallocnesia registration `alloc_guard_544_we_sends_interleaved_mallocnesia` (gtest case `B35RunThreadWindows.WE_InboundPairInterleavedWithSends`, whose expected entry is empty, so the case must read zero); its counter build is `print_only` and does not count as the witness. Its `rt::send_body` calls `Session::send` through the real Engine read pump. Its rig sets no `store_factory`, so it runs with no store; 103 adds a twin registration with `shape_session` setting a `MemoryStoreFactory` (`alloc_guard_103_we_sends_memorystore_mallocnesia`), measured on the base first. If the base twin is not zero, the witness is that twin's base-vs-head equality and the cause is reported, not hidden.
  - **Proof it can fire:** in a scratch copy, plant one allocation in the gate's shared fast path (a `new` inside the RAII holder's constructor); both registrations must go RED with the interceptor's entry. Then remove the plant and require zero. Command and result in the evidence file.
- **The contended-wait witness (Gate A round 1, Codex #9, evidence only).** In `replay_gate_test.cpp`, under the TU operator-new counter of `test_544_run_thread_windows.cpp`'s counter build: one warm-up contended exclusive wait and one contended shared wait on a strand, then N more of each, with the count recorded. A nonzero count is disclosed in the B&L row, not a gate failure, since the path is outside XV §1's scope. Proof it can fire: a planted `new` inside the wait loop reads at least N.

#### The ordering guarantee

1. **Numbers.** No outbound number is assigned in [A, Z].
   - Numbers are assigned only inside shared holds or in the inbound chain.
   - No shared hold can begin while `exclusive` is set.
   - The inbound chain is inside the walk.

   So `peek_outbound()` is constant from the tail read to Z. A through-current GapFill's NewSeqNo therefore equals the 34 of the first frame numbered after the answer: the peer sees no gap.
2. **Before the answer.** Every frame numbered before A is written before the walk's tail read, so before the answer's first frame, or it is never written. Never written means a store take-back (D-OUT-1) or a cut Logout (fixpp#590), which is stored but not transmitted. Shared holds open at A keep `shared > 0` until after their write; inbound-chain frames were awaited before the walk began.
3. **After the answer.** Every frame whose 34 is above the answer's range (above `eff_end`, or at or above the GapFill's NewSeqNo) is numbered after Z, so it is written after the answer's last frame.
4. **During the answer.** No frame other than the answer's own is written in [A, Z].

**Still uncovered: none** among fixpp's emitters, given the census recipe above. One condition:
- **Inbound-chain frames.** These can still interleave with a live send that is mid-store when no answer is running. No ruling orders those, and FR-014 does not cover them.

#### Rationale

- QFC holds `m_mutex` through `sendRaw` (persist and send) and across the whole resend. QFGo takes `resendMutex.RLock` in `send`/`queueForSend` through persist and enqueue, and `Lock` for the resend in `in_session.go`. D5′ is QFGo's shape, scoped to the walk.
- Each of #544 R-10's objections to a send gate is answered:
  - **Sends queuing behind each other's store and write:** shared holds do not exclude each other.
  - **A drain in `close()` and `Engine::stop()`:** none is needed. The gate has no unlock-before-destruction precondition (unlike `async_mutex`'s `not_locked`) and needs no `cancel_and_drain`; its waiters are woken by Disconnected, by stop and by an install. Its lazily built timer is an ordinary Session member, destroyed with the Session before the io_context, like the session's other timers.
  - **`close()` awaited inside `send_impl`:** analysed above; it joins nothing that waits on the gate.
  - **A pending cancellation throwing early, turning a veto into a disconnect:** the fast path has no await; the contended wait sits outside the `try` with the pre-check applied by hand and returns with no state change.

#### Alternatives considered

- **Pass-1's D5** (a flag, and a wait in `send_body_` only). Superseded by the widened ruling: it misses both (i) and (ii).
- **`async_mutex` as the gate.**
  - Exclusive only, so sends would queue behind each other.
  - Its lock sequence leaves a terminal filter (#583).
  - It needs a `cancel_and_drain` in both teardowns.
- **Two `async_mutex`es emulating RW.** The same filter and drain costs, and a lock-order hazard.
- **QFC's single session mutex.** A coarse session lock (XV §3).
- **A shared hold taken inside `store_then_emit`.** Too late: the peek and assign happen first. In the walk's empty-store branch, `peek_outbound()` would then read a number whose frame is written after the answer's GapFill → the peer reads it as too low.
- **Exclusive taken after the tail read.** Loses an in-flight S (R-588-b).
- **Frame-local timers in an intrusive waiter list.** Viable: no member timer and no executor at construction. Kept as the fallback if the lazy timer conflicts with executor binding.
- **Queueing sends or ordered emission.** Rejected in 563-564 §5.

#### Sites

- New: `detail::replay_gate`.
- `Session::replay_outbound_range_`, `Session::send_body_`, `Session::run_liveness_loop`, `Session::run_logout_phase1`, `Session::run_peer_logout_bound_` (D4), `Session::record_state_transition_`, `Session::note_engine_stop_`.
- `include/fixpp/session/session.hpp`: the gate member. This changes the layout, before the first release.

---


---

### D6 — The gap deadline at every frame that does not advance N; the too-low Heartbeat (#581; FR-017, FR-018)

**Decision:**
- **The predicate.** In `Session::resend_gap_deadline_`, replace test 1 ("34 above N") with "the frame will not advance N". It returns `nullopt` only when `parse_seqnum(34) == N && msg_type ∉ {"A","4"}`, which is `consume_rejected_seqnum_`'s consume rule.
  - **Validate-gate site** (`on_inbound_active_validate_failed_`) and **Stage-1 Arm C / RC#1 sites** (`on_inbound_active_poss_dup_`): the check runs before `consume_rejected_seqnum_`. So "advances" means "will be consumed"; it is not "below or above N".
  - **`on_inbound_active_out_of_sequence_`:** every frame there failed `check_inbound` (overflow returns first), so the predicate always reads "does not advance". A drained-lock error at 34 = N falls through to Arm B, as today.
  - **`on_inbound_active_sequence_reset_` (Reset mode):** a reset that moved N has cleared the flag inside `apply_inbound_sequence_reset`, so test 2 returns. One that moved nothing, or that `fromAdmin` rejected (type 4 is excluded from consumption), fires.
- **The exits covered:** these are the exits of the Active arm, after the liveness writer, that leave the session up without advancing N. Each already calls `resend_gap_deadline_`, so no call site is added.
  - a validate-gate Reject that is not consumed;
  - a Stage-1 Arm C or RC#1 Reject that is not consumed;
  - a Reset-mode SequenceReset that moved nothing, or that `fromAdmin` rejected;
  - every out-of-sequence frame: a too-low Heartbeat, a too-low PossDup frame, and a too-high frame while a resend is outstanding, including D4's.
  - A too-high frame with no resend outstanding returns at test 2.
- **Deliberately not added:** `dispose_unparseable_`. Faulty frames do not refresh liveness (B-093-9), so a peer that sends only those ends at the TestRequest grace check, and the L-537-1 shape cannot arise. Garbled frames are disregarded; FIX-SL §4.5.2 treats a garbled message as never received. Every well-formed frame that does not advance N does trigger the deadline, so the L-537-1 shape, a gap with no ending, cannot arise; only the *kind* of ending differs for a peer that sends nothing but faulty frames. FR-017 and E12 state the exclusion and its reason (Gate A round 1, Codex #6; Risk R12 closed).
- **Restart:** unchanged. `mark_resend_progress` runs at each advance, through `note_inbound_advance_` → `close_filled_resend_gap_`.
- **The too-low Heartbeat.** In `on_inbound_active_out_of_sequence_`'s Heartbeat arm, keep the early return only for:
  - a too-high Heartbeat (B-537-3's TestRequest rule, then the frame is ignored);
  - `validate_sequence_numbers` off (today's drop, which keeps `ValidationCompatToggles.Seq_KnobOff_TooLowHeartbeatStillDropped`).
  - A too-low Heartbeat **with** 43=Y then reaches the PossDup arm and is ignored as an admin PossDup.
  - A too-low Heartbeat **without** 43=Y reaches Arm B, which sends QuickFIX's too-low Logout (D9; R1 resolved).
- **Interplay with D4:** D4.6's claim protocol settles it. The gap deadline acts before the Logout bound only when 2 × HB is less than T.
- **HB ≤ 0 or no clock:** no gap deadline, as today. Disclosed.

**Rationale:** the rulings (R-11 is superseded), and a single predicate. Existing call sites are reused.

**Alternatives considered:**
- "34 ≠ N" at every site. Wrong at the Reset-mode site, where an SR at N that moves nothing must fire.
- A per-frame N compare taken at the top of `on_inbound_active_` and tested at the exits. It needs the post-processing N, but the check has to run before the Reject and the consume, as #537 D-537-5 orders it.
- A timer for the gap deadline. Still rejected (#537); D4's bound is a different requirement.

**Sites:** `Session::resend_gap_deadline_` and its comment; `Session::on_inbound_active_out_of_sequence_`.

---

### D7 — C ABI 1.17, BREAKING (Article X §7)

- **`include/fix/c_api/version.h`:**
  - `FIXPP_C_ABI_VERSION_MINOR` goes from 16 to 17.
  - Add a 1.17 paragraph to the version comment (BREAKING, [const §X.7], 103), on 102's 1.16 model:
    - **The default reset policy.** What a consumer that never calls `fixpp_session_config_set_reset_seqnum_policy` now sees:
      - an initiator sends no 141=Y without a reset setting and both numbers at 1;
      - an acceptor accepts a Logon without 141;
      - strict resets before every Logon;
      - a strict acceptor sends a Logout with Text.
    - **The observers, one line per change**, each naming the declarations of the session.h table below that carry it (1.12's model): the too-high Logout's mark and `fixpp_session_send`'s refusal; the too-low Heartbeat ending; the gap deadline at every non-advancing frame; a send that waits for a resend answer; `fixpp_session_close` and the mark.
    - **The counterparty's side, with no carrying declaration:**
      - 7=0 is answered as 7=1, and an absent 7 draws a Reject;
      - a too-high ResendRequest is serviced, and a too-high Logout is acknowledged within the bound;
      - every frame below NextNumIn without 43=Y draws a Logout with Text;
      - liveness, Logout and in-flight sends are ordered around a resend answer;
      - resend state and the mark end with the connection (no reach through the C ABI: it has no `drive_reconnect`).
    - **Migration:** set `FIXPP_RESET_SEQNUM_BILATERAL_STRICT` to keep a reset on every Logon (now conforming).
    - No symbol, type or error code changes.
  - The introducing-minor table is unchanged, because no error code is added. Re-derive with `tools/abi_history/error_codes_v1.txt` and `tools/check_capi_occupancy.sh`.
- **`include/fix/c_api/session.h` (amended at Gate A round 1, N3: Article X §7 asks for the mark "in the documentation of each affected declaration", and 1.12's model puts a per-call clause on every observer):**
  - the `(default)` marker moves from STRICT to LENIENT; the enumerator descriptions are corrected to the policy table;
  - the setter's paragraph is rewritten;
  - each declaration below gets a "BREAKING (C-ABI 1.17; 103)" clause naming what changes **for that call**. The list is derived from the behaviour changes, one row per change; re-derive it the same way, not from this table.

  | Behaviour change (decision) | What a C consumer sees | Declarations that carry it |
  |---|---|---|
  | Default policy lenient (D2.1) | a Logon without 141=Y is accepted where it was refused; an initiator's Logon no longer carries 141=Y (34=1 either way: a session opened through the C ABI runs on the in-memory store, so "no 141=Y at a stored number above 1" has no reach as shipped) | `fixpp_reset_seqnum_policy`, `fixpp_session_config_set_reset_seqnum_policy`, `fixpp_session_config_create` (the config it creates now carries lenient), and the five observers: `fixpp_session_is_established` (true where it stayed false), `fixpp_session_close` (returns `FIXPP_ERR_OK`, not `FIXPP_ERR_THREAD_SESSION_LIFECYCLE`, for a session that now establishes), `fixpp_session_send`, `fixpp_session_register_callback`, `fixpp_session_register_send_callback` |
  | Strict resets before every Logon; strict acceptor's Logout with Text, before the sequence check (D2.2, D2.4) | a strict acceptor's refusal of a Logon without 141=Y carries a Logout with Text, and an out-of-sequence one is refused the same way. No reach as shipped for the reset itself: on the in-memory store the numbers are already 1, so "gives up persistent recovery" and "no store reset for a refused Logon" are not observable through the C ABI | `fixpp_reset_seqnum_policy` (the STRICT enumerator), the setter, `fixpp_session_config_set_reset_on_logon` |
  | A too-high Logout pending (D4.2, D4.7) | `fixpp_session_send` returns the invalid-state code where it returned `FIXPP_ERR_OK`; the session ends at the acknowledgement | `fixpp_session_send`, `fixpp_session_is_established`, `fixpp_session_close`, `fixpp_session_register_callback`, `fixpp_session_register_send_callback` |
  | A too-low Heartbeat without 43=Y ends the session (D6, D9) | `fixpp_session_is_established` turns false where it stayed true | the five observers |
  | The gap deadline at every non-advancing frame (D6) | the session ends at a frame at or below N where it stayed established | the five observers |
  | Replay serialisation (D5′) | `fixpp_session_send` may return later (after a resend answer), and a send waiting there returns the invalid-state code on close, stop or a pending mark, where it would have succeeded | `fixpp_session_send`, `fixpp_session_close` (its graceful Logout waits for a running answer; a walk longer than the logout timeout means no Logout, L-103-6) |
  | `close(graceful)` and the mark (D4.6) | a close while a too-high Logout is pending sends the close's own Logout; one that finds the acknowledgement claimed waits for it inside the logout timeout | `fixpp_session_close` |

  - **Sentences amended in place, marked 1.17.** 103 falsifies sentences of the 1.12 clause that each of the five observers repeats: "the next too-high frame that neither ends the session by another check nor advances the expected number draws a Logout" (now any frame that does not advance it, FR-017) and "a too-high frame that reaches the sequence check while a resend is outstanding is ignored" (not for a ResendRequest, which is serviced, or a Logout, which sets the mark; FR-008, FR-010). Per the 1.11/1.12 model, each such sentence is amended in place in every declaration that carries it, marked "(amended in C-ABI 1.17, 103: …)". Re-derive the sites: grep `session.h` for the 1.12 clause's sentences.
  - **No carrying declaration (the counterparty's side), in `version.h`'s 1.17 paragraph only:** 7=0 answered as 7=1 and an absent 7 Rejected; a too-high ResendRequest serviced; every too-low frame without 43=Y draws a Logout with Text (a non-Heartbeat one already ended the session, so only the wire changes); outbound frames ordered around a resend answer; #578's clear (no reach: the C ABI has no `drive_reconnect`).
- **Pinned literals:** `CapiVersion.CApiVersionIsExactly_1_16_0` (rename it to `_1_17_0`, value 17) and the literal in `CapiVersion.CompositeMacroValue` (`tests/capi/version_test.cpp`). Readers of the macro follow it without edits: `src/capi/version.cpp`, `tests/capi/capi_version_smoke.c`, `bindings/python/fixpp.i`, and the `fixpp_engine_create` callers. Recipe: grep `FIXPP_C_ABI_VERSION_MINOR` and `minor` literals under `tests/` and `bindings/`.
- **In-repo consumers, updated in the same PR:**
  - **Interop helpers that force lenient.** Remove the forcing so that SC-002's live Logon cells run the default (the live set is re-derived from `tests/interop/expected-skips-without-counterparty.txt` minus `tests/interop/live-cells-excluded.txt`; the live suites that take their config from `hp_support.hpp`'s `make_session_config` are the ones SC-002 names, and `parity_support.hpp`'s users are standalone thorny and parity cells, none of which is in the live set). Of SC-002's cells only `Fix44/HappyDisconnectReconnectNoReset.ReconnectPreservesSequenceContinuity` (live as `QFcpp_init` and `QFj_init`) logs on again at a stored number above 1 (by its scenario; the plan confirms by reading the cell); the others log on fresh and pass under strict and lenient alike. So it is SC-002's discriminating cell: the plan shows it RED with the strict default restored on top of D2's strict reset (a reset-to-1 mutant), and the in-process FileStore cell (both roles, US1) carries the same mutant. Files: `tests/interop/happy/hp_support.hpp`, `tests/interop/parity/parity_support.hpp`, `tests/interop/happy/hp_fix44_restart_resume_test.cpp`, `tests/interop/thorny/recovery/qfj-626-…_test.cpp`. A cell whose subject is the policy keeps naming it.
  - **C ABI helpers:**
    - `tests/capi/capi_loopback_support.hpp`: its "the default is bilateral_strict" comment is now false.
    - `tests/capi/recovery_liveness_capi_support.hpp`, `tests/capi/inbound_frame_dispositions_capi_support.hpp`, `tests/capi/capi_099_route_set_oom_test.cpp`: re-read.
    - `tests/capi/public_roundtrip_test.cpp`: its "production-default BILATERAL_STRICT" (D-4) comments are now false. Its cells name strict explicitly, so they still pass: on a fresh pair strict resets {1,1} to {1,1}.
  - **Python:** `bindings/python/tests/oo_test_support.py`, `bindings/python/tests/test_roundtrip.py` (its comment that strict establishes), and their `tests/wheel/` copies.
  - **TOML:**
    - `tests/config/fixtures/happy_full.toml` and `tests/config/test_load_happy_path.cpp`: their "default bilateral_strict" comments;
    - a new cell: an absent key gives `bilateral_lenient`;
    - `spec/coverage-index.md`'s same comment;
    - the fuzz corpus copies stay as they are, since they are inputs.
  - **Unchanged:** `tests/session/resend_gap_scene.hpp` names lenient explicitly, and keeps it.
- **Controls:** all four Appendix A controls (clarify done, analyze, Codex Gate A, the owner's `/plan` sign-off).

---

### D8 — The FR-020 census (tests that rely on the default policy without naming it)

**The static half (lexical; it proves non-zero first).** For each test source, compute three sets:

| Set | Meaning |
|---|---|
| **A** | Builds a session config: `SessionConfig`, a `make_*cfg` helper, `fixpp_session_config_create`, a TOML `[session]` table, or a Python session config. |
| **B** | Names the policy: `reset_seqnum_policy_field`, `fixpp_session_config_set_reset_seqnum_policy`, the TOML key, or `RESET_SEQNUM_`. |
| **C** | Asserts something the policy decides: tag 141, a Logon's 34, `session_seqnum_reset_mismatch`, `by_peer_request`, or the fate of a Logon without 141. |

- The census is (A ∖ B) ∩ C, after resolving helpers: a helper that names the policy covers its users, and one that does not passes A on to them.
- **Known positive (amended at Gate A round 1, Codex #11):** a scratch-only seeded sentinel, `tests/session/test_103_census_sentinel.cpp`, never committed: one gtest cell that builds a `SessionConfig` without naming the policy, opens an initiator over a store at {in=37, out=42} and asserts what the base default does there: its Logon carries 141=Y. It passes on the base. It is in A and C and not in B, so the static matcher must report it; it reaches a Session with an unnamed policy, so Pass P must list it; and its outcome depends on the default, so Pass F must fail it. If the static half also finds a genuinely unnamed-default cell in the tree, it is recorded as a second positive; the sentinel is required either way.
- **Negative control:** `StoreFailReconcileTest.VariantC_BilateralStrictDefault_RegressionGuardNotClean` must **not** be reported. Its helper `make_initiator_cfg` assigns `cfg.reset_seqnum_policy_field = policy`, so it is in B. Its comment "bilateral_strict IS the production default" goes on the re-read list (it becomes false).
- **Limits:** a lexical match cannot see a helper defined in another file, or a config built at run time. The measured half covers those.

**The measured half.** Run in a scratch copy of the tree (Article XVI §6's mutation proofs), never committed.

- **Pass P, the poison default (non-fatal; amended at Gate A round 1, N1).**
  - The default becomes `static_cast<reset_seqnum_policy>(0xFF)`. When `Session`'s constructor sees that value it writes one line, `CENSUS-HIT`, to stdout and flushes it, then continues with the base value (`bilateral_strict`), so the rest of the run is the base tree's.
  - It must not abort: Article VII §8 groups isolation-safe tests into whole-binary buckets (for example `session_pure_tests`, one `add_test`), so an abort would kill every later cell of the bucket and ctest would report one failure per binary.
  - The library does not link gtest. Each hit is attributed to the gtest case whose `[ RUN      ]` line precedes it in the binary's own output (run each binary directly, or through `ctest -V`, with its stdout captured); a C test to its ctest entry; a pytest case through `pytest -s -v`. The sentinel proves the attribution: its `CENSUS-HIT` must land under its own `[ RUN ]` line.
  - Every cell that reaches a Session with an unnamed policy is listed. This is the superset.
  - Known positive: the sentinel must be in it; VariantC must not. Otherwise the instrument is broken.
- **Pass F, the flip.** The base tree with only the default changed to lenient. The cells that fail are those whose outcome depends on the default. Known positive: the sentinel must fail here; VariantC must not fail for the flip (it names strict).
- **Every Pass-P hit gets a row** in the census record, with one disposition:
  - **flipped:** also failed in F;
  - **insensitive:** in P but not in F, and re-read to confirm it asserts nothing the policy decides;
  - **named by a helper.**
- **A static grep for direct reads of a default-constructed `SessionConfig`'s field** closes Pass P's blind spot: a test that never builds a Session.
- **Suites:**
  - `ctest -L session`, `-L capi` (C tests included), `-L config`;
  - the interop labels: standalone parity and thorny cells locally, and the live QuickFIX cells for SC-002;
  - `tests/session/conformance`;
  - `pytest bindings/python/tests`;
  - the examples CI runs;
  - a bench smoke run (the bench drivers build `SessionConfig`).
  - Re-derive each label's binaries with `ctest -N -L <label>`; record no count.

### D9 — Arm B's too-low Logout (FR-018, widened by the owner 2026-11-14)

#### Decision

- **Site:** `Session::on_inbound_active_out_of_sequence_`, Arm B, its last block.
- **Which frames:**
  - Only `chk_error == session_seqnum_too_low` sends the Logout.
  - Any other refusal that falls through stays a silent `Disconnected`: a drained seqnum lock while closing, where "too low" would be false.
  - Too-high frames return earlier: ignored under B-537-1, or answered by D4.
- **Which states:** `on_inbound_active_` serves both `LogonReceived` and `Active`, but no frame other than the Logon itself is processed in LogonReceived (D4.2's reachability result), so Arm B's Logout is sent in Active. `onLogout` fires on the edge out of Active. B-9 is dropped as unreachable by reading; the phase-1 census that re-confirms D4.2 re-confirms it.
- **Text:** `"MsgSeqNum too low, expecting " + N + " but received " + M`. This is QFC's `doTargetTooLow` and QFJ's `doTargetTooLow`, byte for byte; QFGo per the research.
  - **N:** `seqnum_mgr_.next_inbound_unsafe()` read in the arm. `check_inbound` refused the frame, so it is NextNumIn before the frame.
  - **M:** `parse_seqnum(hdr.msg_seq_num)`, written in decimal by `std::to_chars`. The peer's bytes are never echoed.
  - **Buffer:** the text is built in the arm's frame, in a `std::array<char, K>` that outlives the leaf's await, as `resend_gap_deadline_` builds its text. K is fixed by `static_assert(phrase1.size() + phrase2.size() + 2 * (digits10 + 1) <= K)`. No number is written into a comment.
- **Build:** `inbound_logout_leaf_(tic, st52, text)`.
  - It uses its own 256-byte stack buffer, not the inbound reply slot (B-101-10).
  - 52 comes from `effective_clock_ ? stamp_sending_time(...) : SendingTimeStamp{}`, `on_inbound_active_logout_`'s pattern, because a session may run without a clock.
  - **If it does not fit** (long CompIDs): the builder fails, the leaf sends nothing and returns success, and the arm still records `Disconnected`. This is the existing fail-closed behaviour of B-537-2 and of the SendingTime route. Disclosed.
- **Order of effects:**
  1. Any `peer_logout_` claim.
  2. `toAdmin(Logout)`.
  3. The outbound store, then the write.
  4. `record_state_transition_(Disconnected)`, whose Active edge runs `onLogout`.
- **Callbacks not called:** no `fromAdmin`/`fromApp` for the too-low frame, unchanged. QFC and QFJ throw inside `verify` before `fromCallback`.
- **D4 claim:** if `peer_logout_.phase == pending`, claim `pending → none` and wake the bound before the leaf. An `acking` phase never reaches Arm B, because D4.6 drains frames in Active while acking.
- **Precedence:** `resend_gap_deadline_` runs earlier in the same function. When the gap deadline has expired (D6), its "ResendRequest unanswered" Logout is the one Logout.
- **Event:** none. B-537-2 and the SendingTime fatal route emit none, and there is no event type for this.
- **Return value:** success, as today. If the leaf fails (`app_callback_threw` from `toAdmin`, or an overflow at assign), it has already recorded `Disconnected` and that error is returned, as `resend_gap_deadline_` returns it.
  - Disclosed: "disconnect" means FSM `Disconnected`. The transport stays open until the peer's EOF, `close()` or stop, as with B-537-2 and the SendingTime route. QFC/QFJ throw and close at once; QFGo waits in `logoutState`.
- **Persistence:**
  - N is not advanced and nothing inbound is persisted.
  - The Logout is stored as an outbound frame by `store_then_emit`.
  - `logout_seen_` is **not** set, as for the other fatal Logouts (the SendingTime reject, the Stage-1 PossDup reject, the gap deadline), so `reset_on_logout` does not fire. Disclosed.
- **Rows that are unchanged:**
  - **TestMessageIndicator:** the Logout carries no 464, and posture is not consulted. The 464 posture refusal is a Logon-arm rule.
  - **`validate_sequence_numbers` off:** the knob-off branch runs first, so the frame is delivered and no Logout is sent.
  - **Validation on, a too-low frame that fails the dictionary:** Rejected at the validate gate before Guard (4), not consumed, and the session stays Active. This is QuickFIX C++'s order (`Session::next` validates before `verify`; a validation failure goes to `generateReject`, which advances only an in-sequence number), and the owner ruled it the FR-018 carve-out (Gate A round 1). Cell B-8.
  - **A too-low frame failing CompID or SendingTime:** the earlier guards' endings apply.
  - **A too-low frame with 43=Y:** the PossDup arm ignores it.
  - **A too-low Heartbeat:** after D6 it no longer returns early, so it reaches Arm B like any other frame.
- **Disclosed, not new:** while the Logout is in its store call the state is still Active, so a live send can land after the Logout. B-537-2 and the SendingTime route have the same window.

**Sites:** `Session::on_inbound_active_out_of_sequence_`; `Session::inbound_logout_leaf_` (unchanged).

#### Existing cells that flip, or must be re-read

Census recipe:
- **Static half (leads only):** find cells that feed, in Active or LogonReceived with `validate_sequence_numbers` on, a frame whose 34 is below N and that carries no 43=Y. Of those, keep the ones that assert the written frames, the absence of a 35=5, the `toAdmin` count, or Active for a Heartbeat.
- **Measured half (authoritative):** apply the Arm B Logout in a scratch tree and run `ctest -L session`, `-L capi`, the standalone interop labels and `tests/session/conformance`. Every failure is a row.
  - Known positive: `PossDupToleranceTest.ArmB_Regression_Pin_NoLogout` must fail, or the instrument is broken.

Flips:
- `PossDupToleranceTest.ArmB_Regression_Pin_NoLogout` (`test_inbound_poss_dup_tolerance.cpp`): asserts no Logout. Rename it, and assert one 35=5 with the text.
- `SeqnumGapFatalTest.ActiveTooLowBecomesSessionFatal`: asserts Active for a too-low Heartbeat. Flip.
- `TcSeqnumTest.Tc2c_MsgSeqNumTooLow`: becomes TC 2c's anchor, asserting the Logout text then Disconnected.
- `FsmMatrixWitness.Active_SeqnumTooLow_FatalTransitionsToDisconnected`: asserts Active. Flip; its name becomes true again.
- `TooHighHeartbeatDuringTheGap.Control_HeartbeatBelowNextNumIn_LeavesTheTestRequestPending`: feeds `heartbeat(1)` with no 43=Y. Re-aim it to a 43=Y Heartbeat to keep the control's meaning.
- Already in pass-1 (FR-018):
  - `UnparseableFrameDisposition.Refresh_TooLowHeartbeat_NoTestRequestAtLogonInterval`;
  - `CapiC7Witness.Row6_LivenessOnlyTrafficKeepsTheSessionUp` and `…KeepsAnAcceptorUp`;
  - the `test_537_gap_deadline.cpp` cells driven by `too_low_heartbeats_past_the_bound`.

Re-read:
- `ValidationCompatToggles.Seq_Default_OutOfOrder_RecoveryAndDisconnect` and `ThornyRecoveryFixture.Qfj750_LogoutTooLowSeqnumStillDisconnects`: state only, so they hold.
- `test_store_fail_reconcile.cpp`'s "too-low fallback arm" (result not asserted).
- The comments in `ValidateGateInbound.InSequenceReject_ConsumesSeqnum` and W8b ("too low and silently dropped"), which become false. Comment fixes.
- `hp_fix44_validation_compat_test.cpp`'s "too-low-Heartbeat silent-drop retained": true only with the knob off. Re-read.
- `UnparseableFrameDisposition.D6Edge_*_TooLow*` and `OffExpectedSequenceReset_*_BelowN`: faulty frames are disposed of before Arm B, so they hold.
- Pass-1's #578 cell, Arm A: it reaches `Disconnected` through Arm B, which now writes a 35=5. Scope its "no 35=5" to frames written after the reconnect mark.

---


---

## RED cells

Each cell must be RED on `2c74af92`, by reading. #577 and #578 are unconfirmed: if their cells are GREEN on the base, the issue closes as not-a-bug, with the evidence recorded.

**#578 — resend state across an in-place reconnect**
- **File:** new `tests/session/test_103_resend_state_reconnect.cpp`, using `MockReconnectFactory` (`tests/session/mock_reconnect_factory.hpp`) and the `make_reconnect_initiator` shape. Setup: lenient, `reset_on_logon`, mock clock, HB set.
- **Arms:**
  - A: reach Active; a frame at N+10 draws a 35=2; a too-low frame without 43=Y gives Arm B → `Disconnected`; `drive_reconnect()`; ack 141=Y at 34=1; a frame at 34=4 within 2 × HB.
  - B: as A, with the clock moved past 2 × HB during the downtime.
  - C: strict with no knob, after D2. It reaches the same state with no setting at all.
  - D: the mark is cleared across the reconnect (after D4), through `Disconnected` first.
  - **E (Gate A round 1, RC1; no `Disconnected` edge):** reach Active; a frame at N+10 draws a 35=2; a too-high Logout sets the mark (T > 0, mock clock); the mock transport is dropped with no `Disconnected` recorded; `drive_reconnect()` installs a new transport; the new Logon's acknowledgement reaches Active; advance the clock past the old mark's `at + T`; `Session::send` an application message.
  - **G (an install during a claimed acknowledgement):** the bound has claimed and its acknowledgement's `store(S)` is held (the 538 store wrapper); a direct `drive_reconnect` installs a new transport from Active; release the store. Assert: after the reconnect mark no `Disconnected` is recorded, no `sequence_numbers_reset` event is emitted, and the new connection's Logon exchange reaches Active. The acknowledgement's frame itself is D1's disclosed residual and is not asserted.
  - **H (a new mark on the new connection):** a test hook holds the old bound before its pre-claim test; a direct `drive_reconnect` installs a new transport; the new Logon exchange reaches Active; a too-high Logout at t2 sets a new mark (phase `pending` again); release the old bound with the clock at the old `at + T`, earlier than `t2 + T`. Assert: no 35=5 is written until `t2 + T` (or until N passes the new S).
  - **F1 (the bound's late start):** the mark is set and a test hook holds the child bound before its first run until `close(terminal)` has begun and is at its join; release; `close()` must complete within a bounded pump (no wait of T).
  - **F2 (a wake lost before the race starts):** the hook holds the bound after its publish-and-re-test and before its race's wait is initiated; a wrong-CompID frame records `Disconnected` (the edge's wake lands on a timer with no wait pending); release; `close()` must complete within a bounded pump.
- **Assertions:** `resend_outstanding()` is false after the reconnect's Active, and only 35=2 frames written after the reconnect mark count: exactly one with 7=2 (A, B), none from the old range. Arm E: no 35=5 written after the reconnect mark at or after `at + T`, the send returns success and its frame is written, and the bound's join counter is 0. Arms F1, F2: `close()` returns within the pump; the join counter is 0. Frames written before the reconnect mark (Arm A's Arm-B Logout, D9) are not scoped by these assertions.
- **Control:** a fresh Session, steps 3 and 4 only, writes the same 35=2.
- **Mutants, one per site:**
  - delete the resend-state clear in `install_reconnected_transport` → A RED (nothing written: the first too-high frame is "already requested"), B RED (a 35=5 "ResendRequest unanswered"), E RED;
  - delete the mark clear at install, keep the generation → E RED (the send is refused);
  - delete the bound's pre-claim generation test → H RED (a 35=5 at the old mark's `at + T` on the new connection). E does not kill it: the install's mark clear already fails the bound's `phase == pending` test;
  - delete the bound's post-emit generation re-test → G RED (a `Disconnected` recorded against the new connection);
  - delete the bound's wake on the `Disconnected` edge → (j) of the too-high Logout cells RED (the join counter stays 1 until T);
  - delete the bound's publish-then-re-test → F1 RED; delete `close()`'s unconditional wake → F2 RED (each: the pump deadline expires, with no hang).

**#682 — the default policy and strict's meaning**
- **Files:** a new `tests/session/test_103_default_reset_policy.cpp`; `test_reset_seqnum_policy_matrix.cpp` (extend); a TOML cell; a C ABI cell. Fixture: `make_reconnect_initiator` with a persistent store at {in=37, out=42}, and a store wrapper that logs its calls.
- **Arms:**
  - AS1: default; cold open.
  - AS2: strict; same store.
  - AS3: strict acceptor; Logon without 141, both in and out of sequence.
  - AS4: default acceptor; Logon without 141.
  - AS5: the matrix of policy × knobs × store {1,1} / {37,42}.
  - The C ABI default pair, and TOML with the key absent.
- **Assertions:**
  - AS1: the Logon carries 34=42 and no 141, and an ack at 37 reaches Active.
  - AS2: the store log shows the reset before `store(1, 35=A)`; the Logon carries 34=1 and 141=Y; an ack with 141=Y at 34=1 reaches Active; the next application message carries 34=2.
  - AS3: a 35=5 with 58 = `kStrictResetText`; the arm returns `session_seqnum_reset_mismatch`; N is not advanced.
  - AS4: Active.
  - AS5: every 141=Y Logon carries 34=1.
- **Controls:** strict over {1,1} reaches Active on the base, which proves AS2's acknowledgement leg can pass. The lenient + `reset_on_logon` cell is GREEN on the base.
- **Mutants:**
  - restore the strict default → AS1 and AS4 RED;
  - drop strict from `reset_before_logon` → AS2 RED (`Disconnected` at the ack);
  - restore the silent refusal → AS3 RED;
  - restore the old predicate → AS5 RED.

**#580 — BeginSeqNo 0 and absent**
- **File:** `tests/session/test_538_resend_answer_store_failure.cpp` (`ActiveSession`; store holds the Logon at 1 and application messages at 2 and 3).
- **Arms:**
  - 7=0, 16=0;
  - 7=1, 16=0, for the byte comparison;
  - tag 7 absent, in sequence;
  - tag 7 absent, with validation on;
  - tag 7 absent and too high (after D4).
- **Assertions:**
  - The 7=0 answer equals the 7=1 answer apart from 52: a GapFill with 34=1 and 36=2, then replays of 2 and 3.
  - No frame in `a.written` carries 34=0 (SC-003).
  - Absent 7: Reject 373=1, 371=7, 58="Required tag missing"; no replay; N+1, persisted.
  - With validation on: the same Reject frame.
  - Too high: Reject, N unchanged.
- **Control:** the existing `Control_SlotNotRetained_*` cell, and the 7=1 arm.
- **Mutants:** delete the clamp → the 7=0 arm is RED (34=0); delete the presence test → the absent arm is RED (a replay).
- **Instrument note:** the existing `run_slot_zero` aims at slot 0. After the clamp the walk never reads slot 0, so the cell's precondition fails before any assertion can flip. Re-aim the cell; don't just flip its last assertion.

**#577 — a too-high ResendRequest**
- **File:** new `tests/session/test_103_too_high_resend_request.cpp`, on `Scene` (`resend_gap_scene.hpp`), both roles.
- **Arms:**
  - Arm 1: Active; two `send()`s; `resend_request(n+3)`.
  - Arm 2: `open_gap`; `resend_request(n+6)`.
- **Assertions:**
  - Arm 1: a GapFill and two replays with 43=Y, then exactly one 35=2 with 7=n, written after the replays; `next_in()==n`.
  - Arm 2: the same replay, and no second 35=2.
- **Control:** the same request at 34=n draws the replay.
- **Mutants:** delete the dispatch in `on_inbound_active_too_high_` → Arm 1 RED; delete it in `on_inbound_active_out_of_sequence_` → Arm 2 RED.

**#577 — a too-high Logout**
- **File:** new `tests/session/test_103_too_high_logout.cpp`, on `Scene`.
- **Arms:**
  - (a) `logout(n+3)`, then `gap_fill(n, n+4)`, **with no clock move**;
  - (b) the mark set, `pass(T−1ms)` then `pass(1ms)`;
  - (c) T=0;
  - (d) `send()` while pending;
  - (e) the Logout arrives while a resend is outstanding;
  - (f) the bound against the deadline, with T > 2 × HB and with T < 2 × HB;
  - (g) `close(terminal)` and `close(graceful)` while pending;
  - (h) the Logout resent in sequence while pending;
  - (i) no clock;
  - (j) the mark pending, then a frame that ends the session with no Logout and no `close()` (a wrong CompID), with the clock not moved;
  - (k) the mark pending, then a TestRequest and an in-sequence ResendRequest (FR-011's "administrative traffic continues").
- **Assertions:**
  - (a) one 35=5, written after the GapFill; then `Disconnected`, with the transport callback left open.
  - (b) no 35=5 at T−1ms; one at T.
  - (c) and (i) one 35=5 at once and no 35=2.
  - (d) `session_invalid_state_for_send`, and nothing written.
  - (e) no second 35=2.
  - (f) exactly one 35=5 in each order.
  - (g) `close(terminal)`: no 35=5 (FR-010 case 4); `close(graceful)`: exactly one 35=5, phase 1's; in both, `close()` returns, the join counter is 0, and `drain()` finds no handler left.
  - (h) one 35=5.
  - (j) `Disconnected`; the join counter reaches 0 within a bounded pump.
  - (k) a Heartbeat with the TestReqID, then the replay; the session stays Active with the mark pending.
- **Control:** an in-sequence Logout is acknowledged at once.
- **Mutants:**
  - delete `note_inbound_advance_`'s mark test → (a) RED. The clock is never moved, so the timer cannot be what wrote the acknowledgement: this is the forced spurious-hit arm.
  - delete the bound → (b) RED;
  - delete the claim → (f) or (h) RED, with two Logouts;
  - delete the send refusal → (d) RED.

**#588 — the replay serialisation**
- **File:** `tests/session/test_538_resend_answer_store_failure.cpp` (`ActiveSession` plus the store wrapper that aims at one slot); the store holds application messages at 2 to 4.
- **Arms:**
  - hold `retrieve(3)`; feed 7=1, 16=0; `co_spawn(send)`; pump; release;
  - the held read fails with `store_io_failure`;
  - SC-005's repeat count runs under the sanitizer lanes.
- **Assertions:**
  - The send's future is not ready while the read is held. That proves the send started during the walk.
  - Every frame with 34 above the answer's end comes after the last frame with 43=Y or 123=Y, and the send carries the next outbound number.
  - Store-fault arm: the send ends, with `session_invalid_state_for_send`, within a bounded pump. A mutant hangs, so the pump needs a deadline to show RED instead of hanging.
- **Control:** the same send with no ResendRequest is written at once, with no added suspension.
- **Mutant:** delete the wait → RED (the live frame lands between replays 2 and 3). Delete the guard's release → the fault arm is RED.

**#581 — the deadline at every non-advancing frame**
- **File:** `tests/session/test_537_gap_deadline.cpp`. In `run_residual_pattern`, split the loop at E + `kGapBound`.
- **Arms:**
  - the PossDup frame with a valid 122;
  - the Reset at N that moves nothing;
  - the PossDup frame with no 122;
  - AS2: a GapFill that advances, arriving after the bound;
  - AS3: frames that keep advancing.
- **Assertions:**
  - Before the bound: `still_waiting`.
  - The first frame at or after the bound: `deadline_logout_written`, with no Reject for the no-122 cell.
  - AS2: processed; Active; no 35=5.
- **Control:** `GapDeadlineProgress.*`, unchanged.
- **Mutant:** restore test 1 ("above N") → the residual cells are RED.

**#581 — the too-low Heartbeat**
- **Files:** the residual Heartbeat cell; a new no-gap cell.
- **Arms:** `heartbeat(1)` with no 43=Y, with and without a gap; `heartbeat(1)` with 43=Y; the knob-off case.
- **Assertions:**
  - The session ends at the first frame, with Arm B's ending per Risk R1.
  - With 43=Y: ignored.
  - Knob off: dropped.
- **Control:** a too-low PossDup frame is still ignored.
- **Mutant:** restore the Heartbeat's early return → RED.

### RED cells added by design pass 2 (D5′, D9)

| # | File / fixture | Arms and setup | Assertion | Control | Mutant → RED |
|---|---|---|---|---|---|
| R-588-a | `test_538_resend_answer_store_failure.cpp` (`ActiveSession`; the store holds application messages at 2–4; the wrapper holds `retrieve(3)`) | Feed 7=1, 16=0; `co_spawn(send)`; pump; release. | While held, the live frame is **not in `written`**, a stronger check than the future not being ready. After release, every frame with 34 above the answer's range follows the last 43=Y / 123=Y frame, and the live 34 equals the through-current GapFill's NewSeqNo or `eff_end + 1`. | The same send with no ResendRequest is written in the same pump. | Delete `send_body_`'s wait → the frame appears between replays 2 and 3. |
| R-588-b | Same; the wrapper also holds `store(S, outbound)` | `co_spawn(send)` takes S and suspends in its store; feed 7=1, 16=0; release the store. | The original frame S is written before the answer's first frame. The answer covers S: S is replayed with 43=Y, or gap-filled through S+1. No answer frame precedes S. | — | (1) Start the hold after `store_then_emit` → the answer's first frame precedes S. (2) Move the exclusive wait after the tail read → the answer stops at S−1. |
| R-588-c | Same; mock clock; HB set | Hold `retrieve(3)`; advance HB so the loop wants a Heartbeat; separately, idle past HB so it wants a TestRequest; release. | No 35=0 or 35=1 inside the answer. Each one written afterwards carries the next 34. | Liveness frames with no answer, unchanged (`GapDeadlineProgress.*`). | Delete the liveness wait → a 35=0 or 35=1 lands between replays. |
| R-588-d | Same | Hold `retrieve(3)`; `co_spawn(close(graceful))`; pump. | No 35=5 while held. After release, the 35=5 follows the answer and carries the next 34. A second arm uses a walk longer than T: no 35=5, and the transport is force-closed. | `close(graceful)` with no answer. | Delete phase 1's wait → the 35=5 lands inside the answer. |
| R-588-e | `test_103_too_high_logout.cpp` (after D4) | (1) The mark is set; a too-high ResendRequest starts a held walk; advance to T; release. (2) A `close(graceful)` while the bound waits on the gate. (3) The bound has claimed and its acknowledgement's `store(S)` is held; `co_spawn(close(graceful))`; release (US3 AS7). | (1) The acknowledgement follows the answer; exactly one 35=5. (2) One 35=5, phase 1's. (3) Exactly one 35=5, the acknowledgement, written before the transport closes; `close()` returns. | Pass-1 (b). | (1)/(2) claim before the wait → `close` skips its Logout, the bound exits on Disconnected, and **zero** 35=5 are written. (3) Remove `close()`'s join of an `acking` bound → zero 35=5 (the transport closes under the held store). |
| R-588-f | 538 fixture | A send contended behind a held walk, then: (1) `close(terminal)`; (2) `Engine::stop()`; (3) the send's own slot emits terminal. | (1) and (2): the send returns `session_invalid_state_for_send` within a bounded pump, and nothing is written. (3): `dispatch_aborted`, the state stays Active, `peek_outbound()` is unchanged, the waiter count is back to 0, and the caller's cancellation state is still cancelled (N5). | — | (1)/(2) remove the Disconnected / stop wakes → the pump deadline expires (RED, with no hang). (3) put the wait inside the `try` → Disconnected; add a `reset_cancellation_state` after the cancelled wait → the cancellation-state assertion is RED. Drop the `closing` re-test → a cell with `close(graceful)` plus a contended send shows the application frame after the Logout. |
| R-588-g | New `replay_gate_test.cpp` (the gate alone) | Shared/exclusive semantics; writer preference; the last release wakes exclusive; RAII on a cancelled wait; **the filter witness**: after a contended wait, a total emit on the caller's slot aborts the next timer wait. | All hold. | The uncontended path performs no `async_wait`, counted with a test hook on the gate. | Replace the wait with an `async_mutex` lock → the total emit is filtered (#583) and the cell is RED. |
| R-588-h | Sanitizer lanes (TSan / ASan) | A multi-threaded `io_context` Engine with sends from several threads during a held walk, repeated n times. | 100 % ordered; no report. | — | Delete `send_body_`'s wait → an application frame inside the answer in at least one run. **The repeat count n (Gate A round 1, N7):** R-588-a/b force the interleaving with a hold, so one run each plus their mutants suffices. R-588-h's interleaving is the scheduler's: run the mutant 200 times per lane and measure its miss rate p (runs in which the defect does not show); n is the least integer with p^n ≤ 0.01, so a live defect shows in at least one of n runs with 99 % probability. If the mutant shows in every run (p = 0), n = 1; if it never shows in the 200 runs, R-588-h is not a witness, SC-005 rests on R-588-a/b, and that is disclosed. The measured p, n and command go in the evidence file, not in code or a comment. |
| B-1 | New `test_103_too_low_logout.cpp`, `ActiveSession`; N driven to 5 | An application frame at 34=2, no 43=Y. | One 35=5 with `58=MsgSeqNum too low, expecting 5 but received 2`; then Disconnected; N=5 and not persisted; `toAdmin` once, before `onLogout`; no `fromApp`. | — | Remove the Logout → none written. Swap N and M → the text is wrong. Pass N after an increment → the text is wrong. |
| B-2 | Same | A Heartbeat at 34=1 without 43=Y, with no gap and with a gap before the deadline. | As B-1. | A Heartbeat with 43=Y is ignored; knob off → dropped (`Seq_KnobOff_TooLowHeartbeatStillDropped`). | Restore the Heartbeat's early return (D6) → Active. |
| B-3 | Same | A gap open past 2×HB, then a too-low frame. | Exactly one 35=5, with the deadline's text. | — | Reorder Arm B ahead of the deadline → the wrong text. |
| B-4 | Same | `toAdmin` throws on the Logout. | `app_callback_threw` returned; Disconnected. | — | — |
| B-5 | Same | CompIDs long enough that the leaf's build fails (sized by computing the builder's overhead, not hard-coded). | No 35=5; Disconnected. | B-1. | — |
| B-6 | Same, via `session_test_access::seqnum_mgr(sess).drain()` | A frame at 34=N with the seqnum lock drained. | No 35=5 (silent). | — | Drop the `chk_error == too_low` test → a 35=5 reading "expecting N but received N". |
| B-7 | Same (after D4) | The mark is pending; a too-low frame whose Logout store call is held; advance to T; release. | Exactly one 35=5. | — | Drop Arm B's claim → two. |
| B-8 | Same | Validation on, a dictionary-invalid too-low frame; and a too-low 43=Y application frame. | A Reject and Active, no 35=5; ignored and Active. | A dictionary-valid too-low frame draws B-1's Logout. | Route validate-gate failures of too-low frames into Arm B (a sequence check ahead of the gate's Reject) → a 35=5 and Disconnected. |
| B-9 | — | Dropped: LogonReceived processes no frame but the Logon (D4.2). | — | — | — |

The pass-1 #588 cell is replaced by R-588-a…h.

### FR ↔ cell ↔ mutant (Gate A round 1, Codex #16)

Every behavioural FR has a cell and a mutant that turns it RED for the FR's own reason. FR-007 and FR-019 are documentation and records: they are read at Gate B against the site they describe and need no behavioural mutant. Characterisation cells of unchanged behaviour carry a control instead.

| FR | Cell(s) | Mutant → RED |
|---|---|---|
| FR-001 | #682 AS1, AS4; C ABI and TOML default cells | restore the strict default |
| FR-002 | #682 AS5 | restore the old 141 predicate |
| FR-003 | #682 AS2 | drop strict from `reset_before_logon` |
| FR-004 | #682 AS3 | restore the silent refusal |
| FR-005 | #682 AS1, AS4, AS5 | restore the strict default |
| FR-006 | #578 A, B, E, G, H, F1, F2 | delete the install clear (A, B, E); delete the bound's post-emit generation re-test (G) or its pre-claim generation test (H); delete the publish-then-re-test (F1) or `close()`'s unconditional wake (F2) |
| FR-007 | the `resend_state.hpp` comment, read against D1's site | none (documentation) |
| FR-008 | #577 RR Arms 1, 2 | delete either too-high dispatch |
| FR-009 | #577 RR Arm 2; Logout (e) | send a second ResendRequest from the out-of-sequence route |
| FR-010 | Logout (a)–(j); R-588-e; B-7 | delete `note_inbound_advance_`'s mark test; delete the bound; delete a claim; remove `close()`'s `acking` join |
| FR-011 | Logout (d), (k); #578 E | delete the send refusal; delete the install mark clear |
| FR-012 | #580 7=0 arm; SC-003 assertion | delete the clamp |
| FR-013 | #580 absent arms | delete the presence test |
| FR-014 | R-588-a…e | delete each emitter's wait; start a hold after `store_then_emit`; move the exclusive wait after the tail read |
| FR-015 | R-588-g; the send-path allocation witness; R-588-f(3) | `async_mutex` in place of the wait; a planted allocation in the fast path; a reset after a cancelled wait |
| FR-016 | R-588-a's store-fault arm; R-588-f | delete the guard's release; delete the Disconnected / stop wakes |
| FR-017 | #581 residual cells; B-3 | restore test 1 ("above N") |
| FR-018 | B-1, B-2, B-6, B-8; #581 too-low Heartbeat | remove the Logout; restore the Heartbeat's early return; drop the `too_low` test; route validate-gate failures into Arm B |
| FR-019 | the B&L delta rows, read against the live and closed B&L files | none (records) |
| FR-020 | D8's census with its sentinel | remove the sentinel → the census's known-positive check fails |

Characterisation cells, no behavioural mutant: B-4 (`toAdmin` throws; control B-1), B-5 (CompID overflow; control B-1). R-588-h's mutant is in its row.

---


---

## Pins that flip or must be re-read

**#682**
- **Flip:**
  - `StoreFailReconcile…VariantC_BilateralStrictDefault_RegressionGuardNotClean` (`test_store_fail_reconcile.cpp`): it now sees 34=1 and 141=Y.
  - `RefreshOnLogon…W5b_BilateralStrict_KnobOff_L029_3_Gap_Witness` (`test_refresh_on_logon.cpp`): tag 34 is now "1".
- **Re-read the premise:** `W5a_BilateralStrict_KnobOn_SuppressRehydrate`. Its equality holds; its reason does not.
- **Re-read** in `ResetSeqnumPolicyMatrixTest`:
  - `BilateralStrict_Initiator_PeerConfirms141Y`, `BilateralStrict_Initiator_PeerOmits141Y`, `…_PeerOmits141Y_DirectReject`, `BilateralStrict_Initiator_CountersResetToOne`, `BilateralStrict_Acceptor_*` (all four);
  - `PeerSpontaneous_Initiator_NonReset_Logon_AtNGt1_OutboundUnchanged`, `FreshNoKnob_Initiator_BilateralLenient_PeerSpontaneous141Y_OutboundStaysOne`.
- **Re-run:** `test_reset_on_lifecycle.cpp`, `engine_reset_unit_stop_test.cpp`, and `tests/capi/public_roundtrip_test.cpp` (re-read its comments).
- **Comments only:** the lenient-forcing helpers listed under D7.
- **The rest:** whatever D8's census finds.

**#580**
- `ResendAnswerStoreFailure.Guard_BeginSeqNoZero_SlotZeroIsFoldedAndTheSessionStaysActive`: flip, re-aim and rename.
- `ResendAnswerStoreFailure.Guard_BeginSeqNoAbsent_SlotZeroIsFoldedAndTheSessionStaysActive`: becomes the Reject cell.

**#577**
- `ThornyRecoveryFixture.Qfj750_LogoutTooHighSeqnumRecoversGapNotDisconnect`: re-read.
  - Its comment's "re-processed after the gap is filled" is false.
  - Its outcome depends on whether `ParityAcceptorFixture` gives the Session a clock. With no clock, D4 acknowledges at once, the state becomes `Disconnected`, and the pin flips.
- `SecondTooHighResendRequestAndLogout.{ResendRequest,Logout}`: they still hold; re-read.
- `FixTcCoverageGaps.SimultaneousResendRequest_ServicesPeerWhileAwaitingOwn`: a control; it holds.
- `FixTcCoverageGaps.TooHighInboundSeqnum_EmitsResendRequest_StaysActive`: re-read which MsgType it feeds.
- `SecondTooHighPerType.*`: they hold.

**#581 (FR-017)**
- `GapDeadlineResidualPattern.Pin_*`, all four: flip.
- These cells use `too_low_heartbeats_past_the_bound` as their traffic and must change it, because after FR-018 a too-low Heartbeat ends the session:
  - `GapDeadlineGuards.G1_WrongCompIdAfterTheBound_EndsSilently`;
  - `GapDeadlineGuards.G1_SendingTimeOutsideTheThresholdAfterTheBound_RejectThenLogout`;
  - `GapDeadlineResetMode.S3_ResetThatAdvancesAfterTheBound_EndsTheResendAndNoLogout`;
  - `GapDeadlineHelpersTooHighTest.InSequenceFrameConsumedAtTheValidateGate_NextFrameIgnored`;
  - `GapDeadlineHelpersTooHighTest.InSequenceFrameConsumedAtStage1ArmC_NextFrameIgnored`.
  - Suggested replacement traffic: a too-high Heartbeat that answers the liveness TestRequest before the bound (B-537-3).
  - After FR-017 no frame can carry the session past the bound without either advancing N or firing the deadline.
  - The HelpersTooHigh assertions still hold under FR-017: a consumed frame advances N, and the deadline restarts.
- Re-read the L-092-1 and L-092-2 witnesses, `UnparseableFrameDisposition…Disclosed_L1_ReplayedFaultBefore34_ResendLoop` and `…Disclosed_L2_FaultyGapFillDuringAwaitingResend_Disconnects`: the deadline can now fire at a frame that is not above N.

**#581 (FR-018) — flip**
- `SeqnumGapFatalTest.ActiveTooLowBecomesSessionFatal`: today it asserts Active.
- `TcSeqnumTest.Tc2c_MsgSeqNumTooLow`: a conformance cell that asserts "silently ignored".
- `UnparseableFrameDisposition.Refresh_TooLowHeartbeat_NoTestRequestAtLogonInterval`.
- `CapiC7Witness.Row6_LivenessOnlyTrafficKeepsTheSessionUp` and `CapiC7Witness.Row6_LivenessOnlyTrafficKeepsAnAcceptorUp`.
- `ValidationCompatToggles.Seq_KnobOff_TooLowHeartbeatStillDropped`: it holds under D6.
- If R1 resolves to option (b), every cell that pins Arm B's silence flips too.
  - Census recipe: cells feeding a too-low frame without 43=Y in Active, then asserting the frames written.
  - `Qfj750_LogoutTooLowSeqnumStillDisconnects` asserts only the state; it holds.

**#578 and #588:** no existing pin. `FrameSentDuringTheReplay.IsIgnoredAndASecondResendRequestFetchesIt` holds.

---

## B&L delta

**Retired** (moved to `behaviors-and-limitations-closed.md`):
- `L-029-3`: by D2.2.
- `L-537-1`: by D6.

**Amended:**
- `B-013-6`: lenient is the default; strict resets before every Logon; the strict acceptor sends a Logout with Text; the "QFJ-style" label is struck.
- `B-013-1`: "too-LOW … still Logs out" becomes true (Logout with Text, B-103-10); "with `session_seqnum_too_low`" is false, because the arm returns success: strike it.
- `B-016-1`: a too-high Logout draws a ResendRequest and a bounded acknowledgement (B-103-7). "recovering the gap before the Logout is dispatched" is struck: no frame is kept.
- `B-537-1`: the exceptions for a too-high ResendRequest and Logout (B-103-6, B-103-7).
- `B-537-2`: "the next frame above NextNumIn" becomes "the next frame that does not advance NextNumIn". The list of endings loses its `L-537-1` line.
- `B-538-2`: the clause on 7=0 or absent at slot 0 changes: 7=0 is answered from 1; absent draws a Reject (B-103-4, B-103-5).
- `L-028-3`: a cross-reference to #578's reset route (B-103-9).
- `L-092-1`, `L-093-7`, `L-092-2`: drop "not above NextNumIn … does not end (L-537-1)", and state the new deadline trigger.

**Added:**
- `B-103-1` (**BREAKING**, C-ABI 1.17): the default `reset_seqnum_policy` is `bilateral_lenient` on C++, the C ABI and TOML.
- `B-103-2`: a strict initiator durably resets both numbers to 1 before every Logon; every 141=Y Logon carries 34=1.
- `B-103-3`: a strict acceptor refuses a Logon without 141=Y with a Logout, `58="ResetSeqNumFlag(141)=Y required"`, before the sequence check. No Logout where the CompIDs leave no room for it.
- `B-103-4`: BeginSeqNo(7)=0 is answered as 7=1.
- `B-103-5`: an absent 7 draws Reject 373=1, 371=7 and is consumed in sequence. A present but non-numeric 7 is read as 0.
- `B-103-6`: a too-high ResendRequest is serviced; at most one ResendRequest of fixpp's own; no `fromAdmin`. With `validate_sequence_numbers` off, unchanged.
- `B-103-7`: the pending-Logout ending (Active only): the acknowledgement when N passes S or at T, or at once with T=0 or no clock; sends refused while the mark is pending; TestRequests and ResendRequests answered meanwhile; no `fromAdmin`; the durable N stays at or below S on the timeout route; a `close(graceful)` that finds the acknowledgement claimed waits for it inside its grace. At most one Logout; none where the CompIDs overflow the Logout buffer, where `toAdmin` throws on it, where its assignment or store call fails, or where the transport ends first (the peer's EOF, `close(terminal)`, `Engine::stop()`) (FR-010 cases 1–4).
- `B-103-8`: the four emitter kinds (`Session::send`, the liveness Heartbeat and TestRequest, `close()`'s Logout, the pending-Logout acknowledgement) wait for a running resend answer and are numbered after it; a frame numbered before an answer is written before it; a waiting send is refused (`session_invalid_state_for_send`) on close, stop, a non-Active state or the mark, or returns `dispatch_aborted` with no state change and its caller's cancellation still pending on its own cancellation.
- `B-103-9`: resend state and the mark end with the connection: cleared when a new transport is installed, whether or not the session passed through `Disconnected`; the mark's bound acts only on its own connection. Reachable in place only through a direct `Session::drive_reconnect`. Disclosed: an emit already past its last check when a direct caller installs a new transport (a `Session::send` mid-store, or a claimed acknowledgement) writes its frame on the new transport, as before 103.
- `B-103-10`: every frame below NextNumIn without 43=Y, the Heartbeat included, in Active with sequence validation on, that reaches the sequence check draws `Logout 58="MsgSeqNum too low, expecting N but received M"`, then `Disconnected`. N is not advanced. There is no `fromAdmin`/`fromApp` and no event. The gap deadline's Logout takes precedence. Not covered: a frame Rejected at the validate gate (it stays Active, as QuickFIX C++), a frame failing the CompID or SendingTime check, an unparseable or garbled frame. No Logout where the CompIDs overflow the Logout buffer or `toAdmin` throws.
- `L-103-2`: a peer-spontaneous 141=Y acknowledgement at 34=1 with a hydrated N above 1 ends the session (D2.3).
- `L-103-3`: HB ≤ 0: no gap deadline (restated).
- `L-103-4`: a strict acceptor that receives a 141=Y Logon with 34 ≠ 1: unchanged (the spec's Edge Cases).
- `L-103-5`: after a too-low Logout, as after B-537-2, the transport stays open until the peer disconnects, `close()` or `Engine::stop()`, and `reset_on_logout` does not fire.
- `L-103-6`: a `close(graceful)` whose `logout_disconnect_timeout_ms` grace runs out before its Logout is written (a resend answer longer than the grace, or a claimed acknowledgement whose store and write outlast it) sends no Logout and force-closes (FR-010 case 5).
- `L-103-7`: a too-low frame in the Logon arms (NotConnected, LogonSent) still ends silently. That is out of FR-018's Arm B; QuickFIX sends a Logout there.
- (`L-103-1` of pass 1 is not added: pass 2 covers both of its cases.)

---

## Constitution check

*Superseded at Gate A round 1 (RC5): the check is redone in plan.md "Constitution Check", with each article's text quoted. The pass-1 table and its pass-2 amendments are removed here so that they cannot be read as current.*

---

## Risks and open findings

- **R1 — BLOCKING: FR-018 rests on a false premise.**
  - Arm B in `on_inbound_active_out_of_sequence_` disconnects **silently**: it records `Disconnected` and sends no Logout. No too-low Logout text exists anywhere in `session.cpp`. B-013-1's "Logs out with `session_seqnum_too_low`" is false by source.
  - So "the same Logout and disconnect as any other frame below NextNumIn", US6 AS4 and the Assumption on the too-low text refer to nothing.
  - Option (a): the Heartbeat joins today's Arm B. This is implementable as written in its comparative clause; the session ends silently.
  - Option (b): Arm B gains QFC's Logout, `"MsgSeqNum too low, expecting " + N + " but received " + seq`, for **every** too-low frame without 43=Y. This matches FIX-SL §4.8.1 and all three engines, and the spec's intent ("draws a Logout"). It is a wider wire change no ruling names, it flips every pin on Arm B's silence, and B-013-1 becomes true.
  - **Recommend (b), with an explicit owner or clarify confirmation before `/plan` signs off.** If declined, (a), with B-013-1 corrected to "silent".
- **R2 — FR-010 and the Assumption say "as a graceful close does today".**
  - Today's ending after acknowledging a peer's Logout (`on_inbound_active_logout_`) is: the acknowledgement, then `Disconnected` with the transport open until the peer's EOF. `run_read_pump` does not close on `Disconnected`.
  - It is not `close(graceful)`'s timed wait. D4.5 uses the in-sequence ending.
  - Amend the wording to "as after an in-sequence peer Logout". No new wait.
- **R3 — FR-010 with T = 0.** "Draw a ResendRequest per FR-009" conflicts with "acknowledges at once (the QuickFIX ending)", which sends no ResendRequest. Recommend: no ResendRequest at T = 0, or with no clock; amend FR-010's parenthesis.
- **R4 — #578 in production.** The Engine never reconnects in place (`run_connect_loop`), so the defect is reachable only through a direct `Session::drive_reconnect`, from `Disconnected` or from Active (D1). Fix it anyway; the B-row states the reachability.
- **R5 — The bound needs a timer.** #537 rejected a timer for the gap deadline; D4's bound is a different requirement, with its own justification under XI §7. The hazard of the one-handler slot (D4.4) must be in the plan's threading section, with a cell showing that the liveness join completes while the bound's coroutine is alive.
- **R6 — At most one Logout across the competing endings** (exactly one except FR-010's enumerated cases). The endings are the bound, the gap deadline, the Logout resent in sequence, Arm B, `close(graceful)` (with its join of an `acking` bound), and the `Disconnected` edge. D4.6's claim table is normative; Gate A should review it as a unit. Cell (f) covers both orders; R-588-e(3) the `close()` join.
- **R7 — D5's scope.** The two uncovered cases (D5 "Not covered") are disclosed, not fixed. Confirm with the owner that "scoped to the replay" excludes a send already mid-store, and the liveness loop's frames.
- **R8 — No `fromAdmin`** for a too-high ResendRequest or Logout, where QFC calls it at receipt. Disclosed.
- **R9 — `validate_sequence_numbers = false`.** A too-high ResendRequest or Logout is still only handed to `fromAdmin`: not answered, not acknowledged. That gap predates 103 and stays outside it.
- **R10 — The peer-spontaneous 141=Y acknowledgement** (D2.3, L-103-2). It is in scope only if the owner reads FR-005 widely. Check the counterparty configs of SC-002's live Logon cells for `ResetOnLogon=Y` acceptors before the live runs.
- **R11 — On a validating session, a too-high frame that fails validation** (for example an absent 7) is Rejected at the gate and opens no gap, because the gate runs before Guard (4). A non-validating session sends the Reject and fixpp's own ResendRequest. A pre-existing asymmetry, disclosed.
- **R12 — FR-017's "any MsgSeqNum".** Closed at Gate A round 1: FR-017 and E12 now state the exclusion of faulty and garbled frames and its reason (D6).
- **R13 — The re-aim of the 538 slot-zero cell** (the instrument note under #580's RED cell). Flipping only its last assertion would leave a cell that fails at its precondition.
- **R14 — Moving the strict acceptor's refusal** (D2.4) changes two more cases: an out-of-sequence Logon without 141 now gets a Logout, and a strict acceptor with `reset_on_logon` does not reset its store for a refused Logon. Both go in B-103-3. Gate A should confirm the move.
- **R15 — The stale header comment** on `logout_disconnect_timeout_ms` ("Must be > 0; validated"). Fix it in the same PR; FR-010's 0 clause depends on 0 being reachable, and it is.
- **R16 — Records that rot.**
  - `brain/components/session.md` records two decisions this feature supersedes: "clamping the walk's start to 1" as declined (#538 bullet), and R-11 (#537 bullet). Mark each with a header note naming 103.
  - The frozen `specs/013-session-reconnect-binding` FR-017 and the "QFJ-style" label get a flag in `brain/components/session.md`.
  - `resend_state.hpp` and the comment in `resend_gap_deadline_` name 103 as the decision that replaced them.
- **R17 — Order of work, by dependency:**
  1. D1;
  2. D2 (its reset makes #578 reachable with no setting);
  3. D3;
  4. D4's ResendRequest half (it uses D3's helper);
  5. D5 (same 538 fixture);
  6. D4's Logout half;
  7. D6 (its predicate assumes D1's "the flag means a gap on this connection", and D4.6's claim covers the deadline).

  D7 and D8 run alongside D2.

---

## Pass-2 amendments (normative over the sections above)


**Decisions**
- **D5:** replaced by D5′ in full. Pass-1's "Not covered (i), (ii)" is deleted; both are now covered.
- **D4.4 / D4.6** (folded into D4.4 and D4.6 at Gate A round 1):
  - The bound claims `pending → acking` **after** its gate wait, immediately before the acknowledgement's emit.
  - Add claim-table row: "Arm B (too low, 43≠Y) | `pending → none` and wake | its own Logout is the one Logout".
  - The bound is a shared-hold emitter (D5′.4).
- **D4.7:** after a contended wake, `send_body_` re-tests the mark together with Active, `closing` and engine stop.
- **D6:** "What Arm B then sends is Risk R1" becomes: Arm B sends QuickFIX's too-low Logout (§2). (Folded into D6.)
- **D1:** `record_state_transition_`'s Disconnected edge also calls `replay_gate_.wake_all()`. (Folded into D1 at round 1, which adds the same wake at `install_reconnected_transport`.)
- **D7, the 1.17 paragraph:** add "every frame below NextNumIn without 43=Y now draws a Logout with Text" and "liveness, Logout and in-flight sends are ordered around a resend answer".

**Spec amendments for clarify / analyze**
- **FR-014:** widen to "any outbound frame fixpp numbers outside the inbound chain (`Session::send`, liveness Heartbeat/TestRequest, `close()`'s Logout, the pending-Logout acknowledgement)". Add: a frame numbered before the answer is written before the answer's first frame.
- **Key Entity "resend answer in progress":** from the walk's entry, before its store tail read, to its end.
- **FR-015:** state that the normal path does not wait.
- **The Assumption on the too-low text:** false at the base. Change it to "QuickFIX's text, which Arm B sends from 103".
- **US6 AS4:** now implementable as written.

**Pins**
- Add §2's flip and re-read list: `ArmB_Regression_Pin_NoLogout`, `FsmMatrixWitness.Active_SeqnumTooLow_*`, `TooHighHeartbeatDuringTheGap.Control_HeartbeatBelowNextNumIn_*`, the `ValidateGateInbound` comments, and the #578 cell's 35=5 scoping.
- "If R1 resolves to (b) …" becomes a list, plus the measured census.
- "#578 and #588: no existing pin" stays true for #588.

**B&L delta**
- Folded into "B&L delta" above at Gate A round 1 (L-103-1 not added; B-103-8 and B-103-10 rewritten; B-013-1 amended; L-103-5, L-103-6, L-103-7 added).

**Constitution check**
- Folded into plan.md "Constitution Check" at Gate A round 1.

**Risks**
- **R1:** resolved as option (b) by the owner on 2026-11-14.
- **R7:** resolved by the widened ruling; delete it.
- **New R18, writer preference:** back-to-back peer ResendRequests can hold off live sends. Each walk is driven by a peer frame, so this is bounded by the peer, and QFGo behaves the same. Disclose.
- **New R19, inbound stall:** the inbound chain now also waits for in-flight holders' store calls before a walk. That costs at most one store operation per holder; a wedged `FileStore` job already stalls the walk today (B-598-1).
- **New R20, liveness slip:** liveness emits slip by the length of a walk, and a TestRequest decided before a walk is re-evaluated after it. Usually it is dropped, because the ResendRequest refreshed inbound liveness.
- **R21:** closed at Gate A round 1: `emit_initiator_logon_`'s exclusion rests on causality, not on a caller census (D5′.4's row).
- **New R22, layout:** `Session` grows by the gate (an optional timer and three scalars) and, at round 1, by `conn_gen_`, before the first release.
- **New R23:** the frozen `specs/013-session-reconnect-binding` FR-009 / T020-A (the too-low Heartbeat exemption) and B&L `B-013-1` are superseded. Flag both in `brain/components/session.md`. The #544 section's "a session send gate" rejection gains a note naming 103's scoped shared/exclusive gate, and the Article XI §3 amendment that permits it (contracts/constitution-xi3-amendment.md), as the decision that narrowed it.
- **R17, the order of work:** D5′ goes before D4's Logout half (R-588-e needs D4). Arm B goes with D6.
