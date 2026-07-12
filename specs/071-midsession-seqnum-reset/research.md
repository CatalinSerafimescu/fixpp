# Phase 0 Research: Mid-Session Sequence-Number Reset Originator (071)

All file:line refs are into the library submodule as of branch `071-midsession-seqnum-reset` (base `37612810`). Findings are from source reads (two Explore passes) and the QuickFIX-cpp / fix8 reference engines under `reference-engines/`.

## R0 — Mechanism: in-band live-socket reset vs logout+reconnect (settled at /clarify)

**Decision**: The manual trigger originates a `Logon(141=Y)` on the **live transport** (no disconnect), transitions Active→LogonSent, and completes on the peer's confirming `Logon(141=Y)`. Automatic/scheduled reset is **out of scope** (deferred).

**Rationale**: FIX-SL §4.4.2 ("24-hour connectivity") is a reset *without* dropping the connection. Interop-validated against QuickFIX-cpp: its dispatcher routes `MsgType_Logon` to `nextLogon` unconditionally (`Session.cpp:1231`), which on `141=Y` resets counters to 1 and confirms with its own Logon (`Session.cpp:199-208, 241-245`) — no disconnect on an already-established session.

**Alternatives considered & rejected**:
- *Logout + reconnect + ResetOnLogon* — this is how BOTH reference engines implement *scheduled* 24-hour reset (QuickFIX `Session::next`→`checkSessionTime`→`reset()`=`generateLogout()`+`disconnect()`+`m_state.reset()`, `Session.h:73-77`; fix8 `activation_service` schedule flag + `ResetSeqNumFlag=Y` only at `generate_logon`, `session.cpp:947`). fixpp already ships the pieces (`reconnect_policy` + `reset_on_logon`). It does **not** satisfy the "without reconnecting" requirement, so it is the wrong mechanism for the manual in-band trigger — but it is the reference-conformant path for any future auto-schedule feature.
- *Auto in-band timer* — neither reference engine *originates* an in-band 141=Y on a timer. Building one would invent behavior beyond the parity bar. Deferred (spec Context + FR-010).

## R1 — FSM transition + the callback model (REVISED after Gate A round 1)

**Decision (FR-014 + FR-015)**: Perform Active→LogonSent through the existing single transition helper `record_state_transition_(fsm_state)` (`src/session/session.cpp:217-258`; state is a plain member `fsm_state_`, `session.hpp:695`). Do the transition **synchronously before the first `co_await`** (FR-014). Add a `midsession_reset_in_progress_` flag that drives a **two-edge** callback policy inside the helper.

**Why FR-014 (atomic boundary)**: `send()` checks `fsm_state_ != Active` only at entry (`:4202`) then suspends deep in `send_impl`/`store_then_emit`; the liveness loop emits while `fsm_state_ == Active` (`:4769+`). If the reset stays Active through its emit `co_await`s, an already-past-guard `send()` or a liveness emit can interleave an outbound frame with/after the reset. Transitioning to LogonSent *before any await* closes the window for **new** sends and liveness emits. (An already-suspended in-flight send completing its write is covered by the R7 quiesce contract + FR-004 fail-closed.)

**Why FR-015 (two-edge callback model — the Gate A New-P1 fix)**: `record_state_transition_` fires `onLogout` on `was_active && new_state != Active && !onLogout_fired_` (`:242`). Two distinct edges must be handled, NOT one coarse "suppress while reset in progress":
- **Benign edge — Active→LogonSent (reset start)**: `was_active` is true → onLogout WOULD fire spuriously. **Suppress it** (call + `onLogout_fired_` latch write both skipped) when `midsession_reset_in_progress_` is set. The app stays "logged on"; the later LogonSent→Active (success) does not re-fire onLogon (`onLogon_fired_` still latched). App sees **neither** callback. ✓
- **Failure edge — LogonSent→(Disconnected/terminal) while a reset is outstanding**: `was_active` is now false (we already left Active), so `:242` would **NOT** fire onLogout — a logged-on session would die silently (the Gate A New-P1 defect). **Force onLogout** (once, setting `onLogout_fired_`) when `midsession_reset_in_progress_` is set and the new state is terminal. This is gated on the flag, so it does **not** affect the shipped connect-time LogonSent→Disconnected (initial-logon failure), where the app was never logged on and correctly gets no onLogout.

Both behaviors are gated on `midsession_reset_in_progress_`, so **every existing (flag-unset) flow is byte/callback-identical** — lower blast radius than re-keying the helper off an `app_logged_on_` notion (the considered alternative, rejected for touching every flow's callback path on a heavily-tested helper).

**Flag lifecycle (Gate A P3 fix)**: `midsession_reset_in_progress_` is set synchronously at the trigger (before the transition). It is **cleared on every exit edge** — success (→Active) and failure (→terminal) — and the clear/suppress/force bookkeeping MUST run **before** the `engine_.application == nullptr` early return (`:233`), so app-less sessions do not leak the flag (only the callback *invocation* is application-gated).

**No FSM matrix edit needed**: `tests/session/fsm_transition_matrix_test.cpp` is a meta-attestation (asserts `is_valid_fsm_state(state())` + enum static-asserts); no per-cell table, LogonSent already valid. Adding Active→LogonSent needs a **new per-US seam test**, not a change here.

## R2 — Ack-arm reuse + the latched-fact outbound restore (REVISED after Gate A round 1)

**Decision (FR-004 relaxed)**: Reuse the initiator ack arm (`peer_ack_sent_reset_flag`, `src/session/session.cpp:3861-3975`) intact **except** for a minimal additive extension to its outbound-restore predicate: for the mid-session case the restore must key off a **latched fact**, not the brittle `peek_outbound()==seqnum_min+1` inference.

**Why the peek inference fails (Gate A P1-2 — a reachable dup-seq bug, NOT a documentable limitation)**: the shipped outbound restore is guarded `own_logon_sent_reset_flag && n_pre_outbound == seqnum_min+1` where `n_pre_outbound = peek_outbound()` (`:3911`, `:3955-3964`). If any non-reset outbound frame is emitted in the LogonSent window, `peek_outbound` advances past 2 and the restore **silently skips** → `next_outbound` stays 1 → the next app send duplicates seq 1. This is reachable: with `validate_inbound_messages` on, a malformed first confirming Logon triggers `emit_session_reject_` (`:3758-3765` → `assign_outbound` `:1906-1913`), then a valid confirming Logon hits the arm with `peek==3`.

**The fix (minimal, additive)**: the ack arm gets one extra branch discriminated by `midsession_reset_in_progress_` (the mid-session marker):
- mid-session reset AND `peek_outbound()==seqnum_min+1` → restore outbound to 2 (via the existing `set_next_outbound`+`persist_outbound_advance_`).
- mid-session reset AND `peek_outbound()!=seqnum_min+1` → a non-reset frame intervened → **fail closed** (disconnect + onLogout via FR-015), do NOT mis-restore.
- not a mid-session reset (connect-time) → existing `own_logon_sent_reset_flag && n_pre_outbound==seqnum_min+1` predicate, **unchanged**.

So the peek is used to *detect an intervening emission and fail closed*, never to silently skip. The connect-time path is untouched. This is the "minimal, additive extension" FR-004 now permits (the earlier "reuse strictly unchanged" was unsatisfiable — Gate A Root cause 2).

**Double-reset remains safe**: connect-time already resets at emit (`:818-824`) and at ack (`:3921`); `reset_seqnums_to_one_durable` (`:595-623`) is value-idempotent. Net {2,2} via inbound restore (`:3936-3945`, guarded `logon_inbound_advanced_init`) + the outbound restore above.

**Emit ordering (FR-011/012/013)**: the new path replicates the connect-time emit core `:818-865` — durable reset → `build_logon(reset_seqnum_flag=Y)` at seq 1 → `store_then_emit` → `assign_outbound` (1→2) → set `own_logon_sent_reset_flag_` — **omitting** the connect-time one-shot hydrate block (R3). Because FR-014 transitions to LogonSent *before* this, the emit runs under non-Active state.

## R3 — Emit-site safety (the 070 Gate-B defect classes)

**Decision**: Implement a dedicated `emit_midsession_reset_logon_()` rather than parameterize `emit_initiator_logon_()`, so the connect-time-only preamble is structurally excluded.

- **No hydrate re-run (FR-011)**: `emit_initiator_logon_` runs `ensure_hydrated_` (`:803`) + sets the one-shot `hydrated_` latch (`session.hpp:749`); re-running it mid-session would double-hydrate against a live counter. The new path must NOT call it. See [[feedback_new_fsm_emit_site_pre_hydration_wrong_seq]].
- **Clock guard (FR-012)**: SendingTime stamping is guarded `if (effective_clock_)` (`:829-836`); the new emit site must mirror this guard or risk a null-deref crash on direct-Session seams. See [[feedback_new_admin_emit_site_needs_effective_clock_guard]].
- **Post-reset sampling (FR-013)**: sample the outbound seq only after the durable reset. See [[feedback_new_fsm_emit_site_pre_hydration_wrong_seq]].
- **own_logon_sent_reset_flag_ single-shot**: set exactly once at emit; consumed/cleared one-shot at ack (`:3906-3907`). During the reset handshake the session is not Active, so the trigger cannot re-fire (R4) → the latch is never re-armed while outstanding.

## R4 — Trigger guard: Active is necessary but NOT sufficient (resend interaction)

**Decision (FR-007, REVISED after Gate A round 1 — add role conjunct)**: The trigger is refused unless `cfg_.role == session_role::initiator` **AND** `fsm_state_ == Active` **AND** `!reconnect_fsm_.is_awaiting_resend()`, returning a new non-fatal `error::session_invalid_state_for_reset`. Refusal leaves state and counters unchanged.

**Evidence**:
- **Role (Gate A P2)**: a fixpp *acceptor* also reaches `Active`; without the role conjunct an Active acceptor would pass and originate a `Logon(141=Y)` — malformed for the acceptor role. The feature is initiator-only, so the guard must check `cfg_.role`.
- **Resend**: resends are **not a distinct fsm_state** — `awaiting_resend_` is a transient bool riding on Active (`reconnect_fsm.hpp:304-305`, `reconnect_fsm.cpp:390-403`); an inbound-gap resend we requested runs under `Active + awaiting_resend_==true` (`session.cpp:2930-2975`). An Active-only guard would let a reset fire mid-gap-fill and silently abandon it. `is_awaiting_resend()` (`reconnect_fsm.cpp:443`) gates it. Servicing a *peer's* ResendRequest is a synchronous walk within one `on_inbound_frame` (`:3550-3573`) — the strand serializes it against the trigger, so no extra guard there.

**Re-entrancy (FR-007 / clarified)**: a second trigger during the handshake hits the same guard (state is LogonSent, not Active) → refused; no queue/coalesce.

## R5 — Peer non-confirmation: NO dedicated timeout exists (CORRECTED after Gate A round 1)

**Decision (FR-009 rewritten)**: There is **no** logon-response timeout in the engine, so none is "reused." A non-confirming peer leaves the session in LogonSent until transport EOF or an application close — the **same** disposition the shipped connect-time logon already has (no regression). On any such teardown, `onLogout` fires via the FR-015 failure edge, and `midsession_reset_in_progress_` is cleared. Recorded as limitation **L-071-3**.

**Evidence (Gate A verified 3 ways)**: initiator `open()` (`:1321-1340`) arms no ack timer; `run_read_pump` (`engine.cpp:481-577`) blocks on `async_read_some` with no read deadline; the liveness loop only spawns at the LogonSent→Active edge (`:4148`) and the R6 generation guard kills it in the LogonSent window. The only session timeouts are `logout_disconnect_timeout_ms` (`session_config.hpp:290-293`) and the transport handshake timeout — neither is a logon-response disposition. My earlier claim that FR-009 "reuses the connect-time logon-response timeout" was false; this correction removes a fabricated mechanism. Adding a real logon-response timer (connect-time + mid-session) is a separate deferred concern, NOT in 071's scope.

## R6 — Liveness-loop double-spawn (new defect the feature introduces)

**Decision**: add a liveness-loop **generation guard** so the ack arm's re-entry to Active does not leave two `run_liveness_loop` coroutines running.

**Evidence**: `run_liveness_loop` bodies loop `while (fsm_state_ == Active)` (`session.cpp:4769`) and terminate only lazily on the next post-sleep state check. Entering Active unconditionally `co_spawn`s a fresh loop (initiator site `:4148`). On the reset's Active→LogonSent→Active round-trip, the old loop is typically still sleeping when the new one spawns; when it wakes it sees Active *again* and continues → **two concurrent heartbeat/test-request loops** (double heartbeats, TSan-relevant). This does not occur on reconnect (the session passes through Disconnected first, killing the old loop). 

**Fix (minimal, contained)**: a monotonic `liveness_gen_` incremented on each Active-entry spawn; each loop captures its generation and runs `while (fsm_state_ == Active && my_gen == liveness_gen_)`, so a superseded loop exits promptly even if it observes Active. This is a concurrency change (Article XI) already inside Gate A scope. Prefer generation-compare over a plain bool (a bool cannot tell a lazily-waking old loop to die while state is Active again). Cf. [[feedback_lazy_cache_hit_path_skips_liveness_check]], [[feedback_strand_in_any_executor_refcount_race]].

## R11 — Inbound frames in the LogonSent reset window (NEW, pre-round-2; the inbound twin of R1/FR-014)

**Decision (FR-017)**: In the LogonSent reset window (marker set), a peer **liveness admin** frame (`Heartbeat`, `TestRequest`) MUST be drained/ignored (connection preserved), NOT disconnected. Inbound `Logon(141=Y)` → ack arm (success); inbound `Logout` → terminal (peer declined); in-flight peer **app** frames are dropped-as-abandoned per the §4.4.2 mutual-reset contract (R7). Gated on the marker so connect-time LogonSent is byte-identical.

**Evidence (verified in source)**: the `case fsm_state::LogonSent` inbound handler (`src/session/session.cpp:3731`) runs `interpret_logon` (`:3768`); on `!result` — which is **every** non-Logon inbound (Heartbeat, TestRequest, Reject, app message) — it does `record_state_transition_(fsm_state::Disconnected)` (`:3771-3778`). That is correct at connect time (the peer sends nothing but its Logon) but **fatal mid-session**: the peer heartbeats every HeartBtInt, so a reset would race the cadence and drop the connection on a routine heartbeat — falsifying SC-001. This is not an edge; it is the common case. (My initial re-plan missed this; it is the inbound twin of the outbound-quiescence P1-1 the round-1 review caught. Advisor-surfaced.)

**Fix (surgical, marker-gated)**: in the LogonSent handler, when `midsession_reset_in_progress_` is set and `interpret_logon` fails, discriminate by msg_type: `Heartbeat`/`TestRequest` → drain (`co_return` with no transition, no seqnum advance — the peer's old-seq liveness is ignored); `Logout` → terminal (existing); anything else (app / out-of-scope admin) → dropped-as-abandoned (`co_return`, no transition) per R7 (the caller quiesced business traffic). Connect-time (marker unset) keeps the shipped disconnect disposition unchanged. This also covers the `co_await` window between the FR-014 pre-emit transition and the wire emit (same handler).

**Article XV §15 note**: dropping in-flight peer app frames here is the *defined* mutual-reset semantics (both sides abandon prior sequence history on `141=Y`), not a drop-oldest-under-backpressure slow-consumer drop — the §15 ban targets the latter. Called out explicitly so Gate A can weigh it.

## R7 — In-flight / quiesce contract (§4.4.2 semantics)

**Decision (spec position, advisor #2)**: the reset **abandons unacknowledged sequence history on both sides by contract**; the caller is expected to quiesce business traffic before triggering. Concretely:
- **Queued/new outbound**: `send()` is Active-only (`:4202`); during the handshake the session is LogonSent, so new sends are refused with the existing `session_invalid_state_for_send` (no new behavior). Prior unacked outbound is abandoned by the reset (both sides reset to 1).
- **In-progress inbound gap**: excluded by the R4 `!is_awaiting_resend()` guard.
- **Inbound reordering edge**: frames the peer sent before processing our reset Logon that are already buffered at old high seqs are a documented edge — after the peer processes our `141=Y` it likewise resets, so it will not send further old-high frames. Captured as a limitation (L-071), not a blocker.

## R8 — Error semantics & no C-ABI change

**Decision**: add `error::session_invalid_state_for_reset` at the next non-renumbering slot in `include/fixpp/core/error.hpp` (enum ~`:52-635`) + a matching `to_string` case (~`:912`). This is a C++ `core::error` value, not a C-ABI `fixpp_error_t` value — **no C-ABI change** (FR-008; GA-frozen 1.5.0). There is no generic "invalid state for <op>" error to reuse; `session_invalid_state_for_send` is name/comment-scoped to `send` (reusing it would repeat the 005-era `session_invalid_logon` misfit the codebase already corrected). See [[feedback_classify_disposition_by_provenance_not_error_value]].

## R9 — Inert-by-default (FR-006)

The trigger is a new public method never called by existing flows; the emit path, the FSM guard flag, and the error value are all reachable only via that method. Inertness is therefore structural: with the trigger never invoked, no existing byte or disposition changes. The liveness-generation guard (R6) is behavior-preserving for all existing flows (generation only ever advances once per Active-entry today). The shipped reset suites (`test_reset_on_lifecycle.cpp` 31, `test_persistent_seqnum_hydrate.cpp` 28, `test_reset_seqnum_policy_matrix.cpp` 15) must stay green unchanged; the no-heap-on-reset invariant (`ResetKnobs_NoHeapOnResetPath`) extends to the new path (Article XV §1).

## Consolidated design (input to Phase 1 — REVISED after Gate A round 1)

1. Public `Session::reset_sequence_numbers()` → `asio::awaitable<expected_t<void>>`; guard `role==initiator && Active && !is_awaiting_resend()` else `session_invalid_state_for_reset` (R4/FR-007).
2. Trigger sets `midsession_reset_in_progress_` and transitions `Active→LogonSent` **synchronously before the first `co_await`** (FR-014), then calls `emit_midsession_reset_logon_()`.
3. `emit_midsession_reset_logon_()`: clock-guarded (FR-012), durable-reset-then-build-at-1, `store_then_emit`, `assign_outbound`→2, set `own_logon_sent_reset_flag_`; **no** hydrate block (FR-011); runs under the already-non-Active state.
4. `record_state_transition_` two-edge policy gated on `midsession_reset_in_progress_` (R1/FR-015): suppress onLogout (call + latch) on the benign Active→LogonSent edge; **force** onLogout once on a LogonSent→terminal edge; clear the flag on every exit edge, before the `application==nullptr` early return.
5. Ack arm (`peer_ack_sent_reset_flag`): minimal additive branch (R2/FR-004) — mid-session reset restores outbound to 2 when `peek_outbound()==seqnum_min+1`, else **fails closed**; connect-time predicate unchanged.
6. Liveness-loop generation guard (R6/FR-016).
7. LogonSent inbound handler: marker-gated tolerance of in-flight peer liveness frames (R11/FR-017) — drain HB/TR, Logout→terminal, app dropped-as-abandoned; connect-time unchanged.
8. New `error::session_invalid_state_for_reset` + `to_string` (R8).
9. Limitations to record (B&L): **L-071-3** no logon-response timeout (R5/FR-009); **L-071-2** in-flight peer app frames abandoned by the reset contract (R7/R11). **L-071-1 retired** — the peek-window dup-seq risk is now *fixed* by the latched-fact + fail-closed design (item 5), not a limitation.
