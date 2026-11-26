# Research: 106 — Logon guards before any answer, write bounds, initiator handshake bound, msvc-asan cap

**Base**: `origin/main` `9d81082a`, where every reading below was made; the branch adds spec commits only. Readings name
symbols, not lines: re-derive each with `codegraph_explore` (`projectPath` = this tree) or `git grep -n '<symbol>'`.
The spec's Clarifications are owner rulings and bind every decision here. Decisions are D1 to D11; Gate A's rulings
R1 to R5 (round 1) and R6 to R10 (round 2) are recorded in spec.md and applied here.

## Findings that shape the decisions

- **F-1. The mTLS gate reads no Logon field.** `Session::logon_identity_admitted_` authorises `live_peer_id_` for
  `cfg_.target_comp_id`; the frame's own 49/56 are compared with the configuration by `interpret_logon`, separately.
  So the gate can always run, even on a Logon whose CompIDs cannot be read. The credentials gate
  (`logon_credentials_admitted_`) reads Username(553)/Password(554) from the `interpret_logon` result only.
- **F-2. The mTLS gate consumes the identity.** Both its arms reset `live_peer_id_` (one-shot per Logon) and emit an
  event (`session_event_peer_identity_bound` or `session_event_compid_authorization_failed`). A validate-first Reject
  whose RefTagID is not 1137, 1130 or 372 on a FIXT session (`logon_reject_disconnects_`) **stays in the arm**: the
  session waits for another Logon on the same connection. A consuming gate run before that Reject would make the
  next Logon fail closed (arm 2).
- **F-3. A direct initiator started by `open()` never holds a peer identity.** `live_peer_id_` is assigned only in
  `install_reconnected_transport` (after `ReconnectFsm` step 7 authorised the peer) and `attach_accepted_transport`.
  `open()` emits the Logon through `cfg_.transport_send` with no handshake of its own. On mTLS every acknowledgement
  on that path therefore fails closed in the gate's arm (2), as 104's `Precedence_DirectMtlsInitiator` and
  `UnaskedResetAck104.Mtls_Unauthorised_NoReset` pin. Such an initiator cannot reach Active over mTLS.
  Every judgment MUST read the current connection's identity. Each `attach_accepted_transport` and
  `install_reconnected_transport` assigns `live_peer_id_` from that connection's handshake, overwriting any earlier
  value, and the consuming gate resets it, so a reconnected connection whose certificate differs is judged on its own
  certificate. An engine-managed or `drive_reconnect()` initiator's second judgment, in the arm, reads the identity its
  connect authorised. `open()` refuses unless the session was never opened (`session_already_open`), so the one path that could carry an
  older value is a `drive_reconnect()` before the first `open()` whose connection ended with its identity unconsumed,
  followed by `open()` over `cfg_.transport_send`. That ordering is pre-existing (the base's gate reads the same
  member); tasks T007 rules it unreachable, or `begin_connection_()` resets `live_peer_id_`.
- **F-4. The inbound advance has readers before Active.** The acceptor's reply Logon advertises
  `next_inbound_unsafe()` as its NextExpectedMsgSeqNum(789) (027 E-OBO: the advance has happened). The 141=Y reset
  units write absolute targets (`run_reset_unit_`), computed from `logon_inbound_advanced(_init)`.
- **F-5. The liveness loop awaits its own emits inline.** `run_liveness_loop_body_` awaits `store_then_emit` for its
  TestRequest and Heartbeat. Every live write takes `write_gate_` (`live_write_serialized_`, FIFO, one write in
  flight). A loop parked behind a blocked application write never reaches its give-up. 105's give-up paths work only
  when the loop is not inside an emit (the `WriteLimit105.R1_LogoutQueuedBehindBlockedSend_*` cells write the
  TestRequest first, then block the send). A TestRequest held behind a resend answer with none pending waits with
  no cap (`L-105-f`, 105 tasks T119b's implementation reading).
- **F-6. The C++ half of the handshake bound already exists.** `Transport::Config::tls_handshake_timeout` (default
  30 s) feeds the TLS transport's handshake timer in both roles. TOML builds every factory from a default
  `Transport::Config` (`build_tls_factory`, the plaintext factories in `resolve_transport`). The Engine sets 1500 ms
  on the listener's `accepted_transport_config` as a literal in `src/session/engine.cpp`. The minting loop of
  `resolve_transport` gives a session after the first its own factory only when its cert source or profile diverges.
- **F-7. No C entry point loads TOML** (`git grep -n toml include/fix/c_api` is empty), so a TOML-only key never
  reaches a C caller.
- **F-8. Size pins.** Recipe: `git grep -nE 'sizeof\((fixpp::)?(core::|session::|config::)?(Session|SessionConfig|EngineConfig|EngineEstablishment)\b' -- tests src include bench bindings`;
  seed it first with `git grep -n 'static_assert(sizeof(' -- tests`, which must report `tests/dictionary/ref_shape_test.cpp`.
  At plan time, and again widened at round 2 (`9d81082a`), the first grep reported nothing. The implementer re-runs both before the first layout change.
- **F-9. A spec edge case was narrower than stated.** `validate_inbound_` runs before `interpret_logon`, so on a
  validating session a Logon whose CompIDs cannot be read can draw the validate-first Reject, and under D1 an admitted
  mTLS identity with no validator still draws it (F-1). Gate A round 1 corrected round 0's edge case (C-8).

## D1 — The identity gates first, by construction (#702; FR-003 to FR-005, FR-007)

**Decision.** In both Logon arms, the identity decision is made at the first point where the arm would send or
change sequence state, and nothing before that point sends or changes state.

1. **Unchanged, silent, ahead of everything:** the header scan, the garbled-frame accounting, `logon_arm_superseded_`,
   `dispose_unparseable_` (silent in NotConnected and LogonSent), `establishment_frame_refused_`, and the late-parse
   close of the validate gate (`close_on_late_parse_failure_`, terminal, sends nothing). On the initiator, 104's
   arm S (the `reset_on_logout` Logout branch) already runs the gate first and is unchanged. This is the whole list of
   what runs before the gates at `9d81082a` (re-derive: walk each arm from its entry to `interpret_logon` and list every
   call and `co_return`). Each sends nothing and changes no sequence number, counter or store (the garbled-frame
   accounting counts frames, not sequence numbers), so FR-003 holds for each by construction.
2. **The validate-first Reject is decided, then held back** until the identity is known. The new callee
   `logon_identity_refuses_(frame, consume)` returns the decision. It is a callee so that no new local enters the
   arms' coroutine frames (104's T093 measurement).
   - The mTLS half evaluates the gate's arms without side effects when `consume` is false.
   - The credentials half runs on the acceptor only, whatever its security profile (plaintext and one-way TLS
     included); it does not depend on the mTLS half applying. It calls `interpret_logon` to read 553/554. When they are
     readable, it calls `authorize_logon`.
   - When `interpret_logon` refuses the frame, the half treats the credentials as unreadable. It refuses only if a
     validator is installed, which it learns through the new accessor `CompIdAuthorizationPolicy::has_logon_validator()`.
     The default accept-all policy sees no change.
   - That rule is wider than "553/554 cannot be read" (N-11). `interpret_logon` also refuses a non-Logon, a CompID or
     BeginString mismatch, and a missing or negative HeartBtInt. With a validator installed, any such frame that also
     draws a validate-first Reject ends silently, even for an admitted mTLS identity whose 553/554 are readable.
   - The rule fails closed (FIX-TC 1S(c)), and it is disclosed in `B-106-1` and the spec's Edge Cases. Reading 553/554
     by a field scan apart from `interpret_logon` was rejected: it would add a second Logon reader for one failure
     direction.
   - A refused identity ends the arm `Disconnected` with nothing sent. The consuming form of the mTLS gate runs there,
     so its event and the one-shot consumption are today's.
   - An admitted identity draws the Reject exactly as on the base.
     - Where the Reject ends the arm (`logon_reject_disconnects_`, computed before the send), the consuming form runs
       **before** the Reject is written, and a refusal there ends the arm silently (N-13.2). So the frame is written
       only to an identity the consuming gate admitted, with no purity condition on the policy.
     - Where the Reject stays in the arm, nothing is consumed and no bound event is emitted, so the next Logon meets the
       full gate (F-2).
3. **`interpret_logon` refusal:** silent `Disconnected`, with no gate and no event, as today.
4. **The consuming gates**, right after `interpret_logon` succeeds and before `ensure_hydrated_`: on the acceptor
   `logon_identity_admitted_()` then `logon_credentials_admitted_(*result)`; on the initiator
   `logon_identity_admitted_()`. A refusal records `Disconnected` and returns success. Hydration follows the gates,
   so a refused Logon leaves the in-memory counters as they were before it (SC-001).
5. **Every later call of the gates is deleted:** the acceptor's after `check_inbound` and after the 1137 block, the
   initiator's after the asked unit, and both 104 too-low branches' own calls. A second call would find the identity
   consumed and fail closed.

**Moved checks (old position at `9d81082a` → 106).** Every other check keeps its relative order.

| Check | Arm | `9d81082a` | 106 |
|---|---|---|---|
| mTLS CompID gate | acceptor | after `check_inbound`, before 1137; again inside the too-low branch | after `interpret_logon`, before hydration |
| credentials gate | acceptor | after the 1137 block; again inside the too-low branch | right after the mTLS gate |
| mTLS CompID gate | initiator | after the asked 141=Y unit, before the unasked unit; again inside the too-low branch | after `interpret_logon`, before posture |
| validate-first Reject | both | first, with no identity decision | decided first, sent only after item 2 admits the identity |
| asked 141=Y unit | initiator | before the gate | after it (the gate moved; the unit keeps its place after 099's S3 test) |

So posture, SendingTime, strict, `reset_on_logon`, the sequence check, the too-low Logout, S3/1137, the units and the
789 honour now all follow the gates in both arms.

**The direct initiator (the brief's choice).** "Authorise at `open()`" is not available: `open()` holds no peer
identity (F-3). Refusing `open()` for an mTLS direct initiator would also break `open()` followed by a direct
`drive_reconnect()`, which authorises at connect and then works. So the gate moves up in `on_inbound_logon_sent_`.
The engine-managed and `drive_reconnect` paths then meet the gate twice: at connect, and in the arm, which
re-authorises the same identity. The outcome matches.

**Observable changes for an admitted identity.** Answers and endings are unchanged (FR-005). The
`session_event_peer_identity_bound` event now precedes a posture, SendingTime, strict, too-low or 1137 refusal, and a
user logon validator is called for a stay-in-arm Reject's frame and again for the next Logon. Both are disclosed (D8).

**Alternatives rejected.**
- Authorise at `open()`: no identity exists there (F-3).
- The mTLS gate before `interpret_logon` (it reads nothing from the frame, F-1): every silently refused first frame
  (a non-Logon, a CompID mismatch) would start emitting gate events and consuming the identity, with no protection
  gained, because those refusals send nothing.
- One consuming gate ahead of the stay-in-arm Reject: the peer's next, corrected Logon fails closed (F-2), against
  FR-005.
- Holding the Reject until after `interpret_logon` in every case: a frame `interpret_logon` refuses would lose the
  Reject it draws today, for admitted identities too (FR-005).
- Passing unreadable credentials to the validator as empty: an absent 553/554 is a readable "none", and the
  validator cannot tell the two apart, so it would not fail closed (spec Edge Cases).

## D2 — NextNumIn is consumed only on the success path (#703 and the owner's class ruling; FR-006)

**Decision: a tentative advance.** A new member `SeqnumManager::classify_inbound(seq)` takes the same lock, applies
the same `frozen_` refusal, and returns the same too-low / too-high / overflow classification as `check_inbound`,
without advancing.
- The Logon arms call it where they call `check_inbound` today. `logon_inbound_advanced(_init)` keeps its name and now
  means "this Logon's number will be consumed".
- The advance is committed by `check_inbound(seq)` at one point per arm:
  - **acceptor:** after the 1137 block, before `LogonReceived`, so the reply's 789 reads the advanced value (F-4).
  - **initiator:** after the 789 honour and its superseded test, before `Active`, and only when
    `!peer_ack_sent_reset_flag`. The units write absolute targets from the classification, as today, and the persist
    guard already skips that case.
- A refused commit (`frozen_`, or a drained lock) can follow only a close or a reset point. It records `Disconnected`
  unless the arm is superseded, and returns success silently.

**End state per success path** (each equals the base's):

| Path | `9d81082a` | 106 |
|---|---|---|
| in sequence, no reset | `check_inbound` N→N+1; persist after Active | classify N; commit N→N+1 at the commit point; same persist |
| acceptor `reset_on_logon` | reset to 1; 1→2; persist skipped | reset to 1; classify 1; commit 1→2; persist skipped |
| acceptor peer 141=Y (no knob) | 1→2; unit writes (2, 1) | classify 1; commit 1→2; unit writes (2, 1) |
| initiator asked 141=Y | 1→2; unit writes (2, x) | classify 1; unit writes (2, x); no commit |
| initiator unasked 141=Y | no sequence check; unit | unchanged |
| too high, `enable_next_expected_msg_seq_num` | no advance | no advance |

**Returns after the base's advance.** Census at `9d81082a` (round 2, root cause #3; `git diff --stat 9d81082a..HEAD --
src include tests` is empty). Recipe: walk `on_inbound_not_connected_` and `on_inbound_logon_sent_` from `check_inbound`
to `record_state_transition_(fsm_state::Active)`, listing every `co_return`; each maps to one row. In the 106 column,
"unchanged" means NextNumIn kept at its pre-frame value; "as the base" means the base's numbers, by design.

| Role | Return | Base | 106 |
|---|---|---|---|
| acceptor | mTLS gate refusal | consumed in memory | before the classification (D1) |
| acceptor | 099 S3 / 1137 Reject | consumed in memory, not in the store | unchanged |
| acceptor | 099 S3 / 1137 Reject with a peer 141=Y (no knob), before its unit | consumed in memory | unchanged; no unit runs |
| acceptor | the S3 / 1137 Reject's own write fails (`inbound_reject_leaf_`'s error returned) | consumed in memory | unchanged |
| acceptor | credentials refusal | consumed in memory | before the classification (D1) |
| initiator | 099 S3 Reject (ordinary acknowledgement) | consumed in memory (`L-104-8`) | unchanged |
| initiator | 099 S3 Reject (asked 141=Y acknowledgement, before its unit) | consumed in memory | unchanged; no unit runs |
| initiator | the S3 Reject's own write fails | consumed in memory | unchanged |
| initiator | `bilateral_strict` silent refusal (no 141=Y) | consumed in memory | unchanged |
| initiator | mTLS gate refusal | consumed in memory | before the classification (D1) |
| initiator | 789 too high or invalid: Logout, `Disconnected` | consumed in memory | unchanged |
| initiator | 789 below: the walk fails (the `!h789` error return included) or is superseded | consumed in memory | unchanged |
| initiator | the `logon_arm_superseded_` return before `Active` (`close()`, `Engine::stop()` or a reconnect drain) | consumed in memory | unconsumed: the commit did not run. Not a refusal: no cell, no FR-005 change (N-5). The next connection sees NextNumIn at the frame's number, which is the store's value on the base too (the persist runs only after Active), so the change makes memory agree with the store; disclosed in `B-106-2` |
| acceptor | a `logon_arm_superseded_` return after `LogonReceived` (after the unit, after the reply, before `Active`) | consumed in memory | as the base: the commit ran before `LogonReceived`. Not a refusal: no cell |
| acceptor | a refusal after the `reset_on_logon` reset (too high: silent; S3 / 1137 Reject) | NextNumIn 1 and the store reset | as the base: the reset ran before the refusal (residual `L-106-a`; Q3, resolved) |
| initiator | a refusal after a completed asked or unasked unit (the 789 ones; a superseded return after a unit) | the unit's targets stand | as the base: the unit's targets stand (residual `L-106-a`) |
| both | a reset unit's manager or persistent-store failure | the unit's targets | as the base: a store failure inside an admitted reset, not a refusal |
| acceptor | the reply Logon's build, number, `toAdmin` or write failure | consumed in memory | as the base: an internal failure after the commit |
| acceptor | 789 after the reply; `onLogon` threw; persist failure | consumed in memory | as the base: the Logon was answered with a Logon, so its number is consumed on the wire |

The returns before the advance (the too-low, too-high and drained-lock results of the check itself) consume nothing on
either side and are not rows.

The last row's store lag of one (029 INV-H1's under-persist) is pre-existing and not changed. The two residual rows
are resets that ran before the refusal. Reordering would not keep NextNumIn either: moving S3 ahead of the
`reset_on_logon` reset turns a knob Logon at a number other than 1 from today's silent too-high ending into an S3
Reject (FR-005). QuickFIX C++ v1.16.0 resets at the same place: `nextLogon` calls `m_state.reset()` for a received
141=Y and for `ResetOnLogon` before `verify` and before its 789 Logout.

**Alternatives rejected.**
- A restore at each refusal (`set_next_inbound(seq)` per site): every new refusal must remember it, and each restore
  is an await that a cancellation can skip.
- Moving S3 ahead of the sequence check: a too-low Logon with a bad 1137 would draw the S3 Reject instead of 104's
  too-low Logout, against FR-005. QuickFIX/J orders it that way; 104 kept QuickFIX C++'s order.
- A Session-local comparison through `next_inbound_unsafe()`: it bypasses the lock and 104's freeze (RR-5).

## D3 — The give-up does not wait behind a blocked write (#714a; FR-008, FR-009)

**Decision: a per-connection bound, which decides; the loop executes.** Gate A round 1 (ruling R2, findings N-1 and
N-9) replaced round 0's bound spawned per emit. The bound is shared with D5's watchdog.
- **The connection bound** (`run_connection_bound_`, data-model E-3) is one coroutine per connection.
  - It is spawned at most once per connection, by the first coroutine that needs it: the liveness loop at its start,
    or the first live write with `write_timeout_ms` set (D5). One respawn point: `drive_reconnect`'s failed-drain
    return, when a cancelled drain left the connection with a write or emit in flight (data-model E-3); at most one
    bound runs at a time.
  - It runs on that coroutine's executor, by 105's rule for its own bound: the entering coroutine's executor, never
    `executor()`. Every spawner (the liveness loop, a live write in `live_write_serialized_`, `drive_reconnect`'s
    respawn) runs on the session's serialised domain (`[const §XI.4]`; on a direct driver, the executor the user attests
    serialised), because every live write runs inside it under `write_gate_`; so the bound's members stay
    strand-confined whoever spawns it.
  - It is joined by its own counter, `connection_bound_counter_`. `close()` and `drain_for_reconnect_` wait for that
    counter to reach zero, `install_reconnected_transport` asserts that it is zero, and `~Session` asserts it is zero, as it asserts `ending_logout_bound_counter_` (N-4). Each joiner's exit term, derived from what it sets and waits on, is data-model E-3's joiner table.
- **What it times.** Two strand-confined records arm it, each set by its writer with no suspension: the liveness emit
  in flight (this decision) and the live write in flight (D5). Its deadline is the earlier armed instant; with neither
  armed, it is parked.
- **The loop arms it.** When the loop starts a TestRequest or Heartbeat emit, it sets `emit_in_flight` and wakes the
  bound. That is after the emit's replay-gate shared hold, and for a TestRequest after `pending_test_req_id_` is set.
  An RAII guard in the loop's frame clears the mark and wakes the bound when the emit returns or an exception leaves it.
  One bound follows the current emit, so two bounds can never adopt one emit (N-13.1 falls away).
- **Its wakes**, each a re-test: the loop's guard; a live write's start (D5); `close()` and `Engine::stop()`'s step 1,
  each before its join; `drain_for_reconnect_` at its entry, beside `wake_ending_logout_bound_()` (data-model E-3); the `Disconnected` edge.
- **The re-test.** The bound exits when the connection generation has changed, when the session is `Disconnected`, when
  `closing_or_stopped_()` holds, or when `reconnect_drain_in_flight_` holds (105's `ending_bound_live_` tests the same flag: the drain records `Disconnected` only from Active or LogonReceived, and `conn_gen_` advances only after it, so without this term a bound spawned by a pre-Active write keeps the drain's `FIXPP_JOIN_COUNTERS` spinning). The last condition is C-4's: a bound woken by `Engine::stop()`'s step 1 cannot
  decide a give-up during the stop. Otherwise it decides the give-up when the emit is in flight,
  `liveness_may_originate_()` holds and the clock is at or past G (D4); in every other case it re-arms. An inbound frame moves L, and so G. The bound sees the
  move at its next wake, at the old G at the latest, and re-arms.
- **The decision** is `decide_give_up_()`, which the loop's give-up block calls too (N-12). It takes the ending claim
  first (D11); refused, it records nothing and returns false. Taken, it writes the slot-74 record, takes the Logout
  owner and calls `enter_ending_write_(ending_kind::r1, …)`, whose budget starts at the decision.
- **The loop after its emit** tests the claim first. If it is the give-up's and the decision entered 105's ending
  phase, the loop takes the give-up's existing write path with the owner already claimed, whatever the emit returned.
  If it is the give-up's and no ending phase was entered (105's ending bound could not be spawned, so
  `decide_give_up_()` already closed; data-model E-3), the loop writes no Logout and returns. Otherwise its existing failure branches run,
  each now through `record_disconnected_unless_already_()` (D11's census).
- **The ending.** A peer that reads again within the budget gets the Logout after the frames ahead of it. Otherwise
  105's bound cuts the write at the decision plus `logout_disconnect_timeout_ms` and closes the transport, which fails
  an emit in its write phase (queued at `write_gate_` or inside `async_write`) and every write queued behind it (FR-009). An emit still inside its `MessageStore::store` call is not failed by the close: it returns when the store returns, which no peer controls (a `FileStore` offload that no cancellation reaches, D-598-1; a custom store). The give-up is still decided at G and the transport still closes at G + B; on an Engine session the pump's EOF then runs `close(terminal)`, which records `Disconnected` before its joins, and the joins wait for the store (`L-093-12`, amended); the Logout leaf finds the cut and writes nothing ("Logout
  not sent"), and `Disconnected` is recorded once. With `logout_disconnect_timeout_ms` 0 or no clock, `enter_ending_write_` is unlimited, so no cut comes. That case
  stays open and disclosed (`L-105-f`, D8; N-8).

**The primitive (ruling R2, shared with D5).** The bound's one wait is its own `asio::steady_timer`, a local of its
frame, constructed on its executor when the bound is spawned. `connection_bound_wait_(timer, deadline)` is the bound's
one private callee that waits. It holds both branches below, and everything the bound decides is shared between them.
- **On the shipping clock** (`effective_clock_` is `fixpp::core::system_clock_source`, a `final` class, tested once at
  the spawn with a `dynamic_cast` on that cold path, as `engine.cpp` casts its transports; `effective_clock_` is set
  from `cfg_.clock_override`, else the Engine's clock, and nothing changes it during a connection, so one test per
  spawn is enough):
  - The deadline is set on the timer directly. `system_clock_source::steady_now()` is `std::chrono::steady_clock::now()`,
    which is `asio::steady_timer`'s clock, so a deadline needs no conversion.
  - A park is `expires_at(time_point::max())`, a wake is the timer's `cancel()`, and each wait is one
    `async_wait(asio::as_tuple(asio::use_awaitable))`.
  - On this branch the bound never calls `Clock::sleep_until`. It never meets the per-session slot (#715) or the
    per-call fallback (`L-093-13`), and it builds no `||` group.
- **On any other `Clock`** (`mock_clock` in every functional cell, or a user `clock_override` or `EngineConfig::clock`): the wait is 105's
  idiom, `timer.async_wait(…) || detail::await_deadline(*effective_clock_, deadline)`, the timer parked at max as the
  wake. This is the seam that keeps the cells deterministic (R2(c)): the mock cells test the decisions, and D9's two
  shipping-clock cells bind the timer branch.
- **What it allocates (R2(a)).** Once per connection, at the spawn: the bound's frame, its timer inside it, and
  `co_spawn`'s state. Per wait on the shipping clock: one recycler block (a cache hit under `B-544-1`'s two conditions),
  nothing from the global heap. When either condition fails, the wait allocates as every gated path does there, which
  `L-497-1`'s conditions (1) and (2) already disclose. Re-derive: the bound's only `co_spawn` (`git grep -n 'run_connection_bound_' -- src`);
  the shipping branch names no `Clock` member. The binding witness is D9's mallocnesia cell; its known positive is M8.
- **Its own wait slot (R2(b)).** No other sleeper arms the timer; its `expires_at` and `cancel` reach only its own
  wait. Unregistered with the clock, it is not reached by `Clock::cancel_sleeps()`; every teardown wakes it.
- **#715 stays out of scope.** 106's new waits never call `Clock::sleep_until` on `system_clock_source`. The one new
  `sleep_until` caller, the held cap with none pending (data-model E-3), replaces the loop's own sleep at the gate, so
  it adds no concurrent sleeper. Re-derive: `git grep -n 'sleep_until\|await_deadline' -- src/session/session.cpp`, each
  hit read for which coroutine sleeps there and what else of this Session sleeps at the same time.
- **The condition, disclosed (`L-106-e`; a third condition on `B-544-1`'s zero, in R-7's form, ruled 2026-11-26 by the orchestrator under the owner's delegation (spec.md Clarifications, R14), flagged for the owner's review at merge).** When the session's effective clock (`SessionConfig::clock_override` when set, else `EngineConfig::clock`) is not `system_clock_source`, each wait of the bound is `timer.async_wait(…) || detail::await_deadline(*effective_clock_, deadline)`. With `write_timeout_ms` set, each spaced write arms one such wait; each liveness-emit edge arms one with the limit on or off. No zero is claimed on that branch. R14 covers both halves (its step 9 scope, spec.md Clarifications): `B-544-1` names the send half as its third condition, because the send is a path it gates; the emit half is on the heartbeat path `[2d §8]` extends `[const §VIII.5]` to, which `B-544-1` does not gate, and `L-497-1` (6) and `L-106-e` name both. On such a clock the own-wait claim (R2(b)) is the clock's to keep: a user clock whose `sleep_until` keeps one timer per Session (the shipping clock's slot design, #715) would let the bound's wait re-arm the loop's sleep, as M8 shows on the shipping clock; `L-106-e` discloses it. That covers both the clock's `sleep_until`, which is the clock's code, and fixpp's own `||` group (its parallel-group state, its two spawned arms and `await_deadline`'s frame), which no cell gates. The base already waits through that clock once per liveness cycle (`run_liveness_loop`), so the clock's per-emit cost is pre-existing. New in 106 are the per-write cost and the `||` group on the uncontended write and emit paths; the base's only per-cycle `||` (`run_liveness_loop`, behind a held resend answer) is the cold path of `B-103-8`.

**Cancellation and failure (C-4).**
- **No cancellation slot.** The bound binds none and awaits no lock (fixpp#583's trap needs a lock). The shipping
  wait completes by its timer; the other branch's `||` retires the losing arm as 105's bounds do. Teardown is a wake
  plus the strand-confined exit test, never an emitted signal. The non-shipping branch's `||` group runs inside the
  bound's own coroutine: it changes no other coroutine's cancellation filter (the liveness loop's, the pump's) and takes
  no lock, so the trap of a lock leaving its coroutine thread's filter terminal does not reach it.
- **No cancel of a write.** Nothing 106 adds cancels `store_then_emit` or an in-flight `async_write`. Cancelling
  `store_then_emit` takes the number back and records `Disconnected` (D-OUT-2) before any slot-74 record, and a
  cancelled `async_write` need not complete. A blocked write is ended only by the transport's close.
- **One close** (N-2). Every close below, and D5's, is `spawn_connection_close_(live_transport_shared_())` then
  `connection_end_owed_.reset()`, when `state_ == lifecycle::open`, as `expire_ending_wait_` does (105 E-2).
- **Spawn failure** (seam `connection_bound_spawn_fails_`, the shape of `ending_bound_spawn_fails_`) fails closed, as
  105's clarification D-g. At the loop's start, the loop takes the claim, records `Disconnected` and ends, and the
  connection closes at its exit. At a live write, the write is not started and returns `dispatch_aborted`, and the
  session takes the claim, records `Disconnected` and closes.
- **105's ending bound refused inside a give-up** (seam `ending_bound_spawn_fails_`, Gate A loop 2 round 2, N-1):
  `decide_give_up_()` records `Disconnected` and closes at the decision itself, whichever caller decided, because a
  bound that decides while the loop is suspended in a blocked emit has no loop exit to close at (data-model E-3).
- **Wait failure.** The seam is `connection_bound_wait_throws_`, the shape of `ending_bound_race_throws_`. Two events
  count as a failure: an exception out of the wait, and a deadline arm that completed before its instant (105's failure
  exit). With the claim free, the bound takes it, records `Disconnected` and closes the transport. With it taken, it closes
  nothing and records nothing: the claimant owns the ending (R10). It writes no Logout and no reason record. An armed write or emit is failed by the close, never left unbounded.

**The resend-answer case (US3 scenario 3).** A TestRequest held behind a running resend answer is capped at L + 3T,
whether or not a TestRequest is pending. This is the owner's T119b text: "If the TestRequest is still held (not yet
written) when the clock is past the last inbound frame + 3T, the session gives up then".
- It supersedes 105's implementation reading, which capped only with one pending (Q5, resolved).
- The loop's wait (data-model E-3) is the one new `sleep_until` caller ruled above.
- The give-up then runs 105's held path: its budget starts at the decision, and its expiry closes the transport under
  the walk.

**The Text when no TestRequest is pending** covers a blocked Heartbeat with none pending, and a held TestRequest with
none pending. The Text(58) is `TestRequest unanswered`, without the TestReqID clause, and the slot-74 record's
`test_req_id` is empty (Q2, resolved). A blocked Heartbeat emitted while a written TestRequest is still pending (an
inbound frame since it made the loop answerable) names that pending TestReqID, because `decide_give_up_()` builds the
Text from `pending_test_req_id_`, as the base's give-up block does (data-model E-4 row 3).

**Invariants kept:** 103 D5′'s replay-gate ordering (the emit keeps its hold; the loop takes the Logout's hold as
today); the slot-74 record before any Logout write (ruling D-e); one Logout owner; 105's ending state machine, entered at
the decision.

**Changed:** "the give-up is decided by the loop" becomes "by the loop, or by the connection bound while the loop is
suspended in its own emit". D11's claim makes the decision once.

**Allocation.** Nothing per emit: one bound per connection, and an emit's wake is one cancel and one re-arm. Round 0's
spawn per emit is what 2d's extension of `[const §VIII.5]` to the heartbeat path forbids (`.specify/2d-threading.md`).

**Alternatives rejected.**
- Racing the emit against G in the loop (`||`). That cancels `store_then_emit`, which takes the number back and
  records `Disconnected` (D-OUT-2) before any slot-74 record. A cancelled in-flight `async_write` need not complete
  either, so the race may never return.
- A `co_spawn` per emit (round 0): an allocation per liveness emit on the heartbeat path (N-9).
- The bound sleeping through `Clock::sleep_until` (round 0; N-1): per-call allocation on an Engine session; on a direct
  driver it re-arms the Session's one slot timer and ends liveness (#715).
- Fixing #715 inside `system_clock_source` (a timer per concurrent sleeper): an Engine session's type-erased strand
  still takes the per-call path, `state::inflight` (a `std::map`) still allocates per call, and naming the sleeper
  needs a sixth `Clock` virtual against `[const §XIV.2]` (I-01).
- Detached emits, not awaited inline. The loop would run beside its own emits, so `tr_written_at` and the TestReqID
  become shared state, two emits can queue, and 103 D5′.4's hold rules would need re-deriving.
- A close-only watchdog drops R-1's Logout (`B-105-3`); relying on D5's limit fails ruling (b), limit off.

## D4 — The give-up instant while a liveness write has not returned (spec US3 scenario 2; ruling R1)

**Decision (Gate A round 1, ruling R1).** While the loop's own emit has not returned, the give-up instant is the held
cap: G = L + 3T + 1 ns (L = `last_inbound_steady_`, `detail::held_test_request_cap`), each step saturating.
- That covers a TestRequest issued and not yet returned from its store and write (data-model E-4 row 2), and a
  Heartbeat emit that has not returned (row 3). The loop emits a Heartbeat only while answerable (no TestRequest
  pending, or an inbound frame since it), so a TestRequest still pending then has seen an inbound frame since its
  issue; the give-up's Text names it (data-model E-4 row 3).
- Through D3's held cap, the same instant applies to a TestRequest due and held behind a resend answer, with one
  pending or none (rows 4 and 5).
- A written TestRequest keeps 105's instant, max(L + 2T, `tr_written_at` + T) + 1 ns (row 1), decided by the loop. The
  bound is disarmed when the emit returns, so a write that completes at L + 2T + ε still gives a peer that has resumed
  reading T from that write.
- `tr_issued_at` keeps its one use, the eligibility test `answerable_now`. No new code reads it.

**Why.** The owner's T119b text gives the state "still held (not yet written)" the cap L + 3T. A TestRequest queued at
the write gate behind a blocked application write is not yet written either, so one owner-defined state gets one
instant. Counting from the issue was the defect that 105's Gate B round 1 (C-2(b)) removed by splitting `tr_sent_at`.
With an issue-time count, a slow store or write left the peer no window after the write (105 data-model E-4, the
`tr_written_at` row).

**Supersedes:**
- 105 data-model E-4's "`tr_written_at`: the instant the give-up counts T from", for an emit that has not returned.
- 105 contract C-1 row 8's stated reason ("so no give-up names a TestReqID that was never written"): a give-up the bound
  decides names a TestRequest whose write did not complete. Row 8's rule stays: a TestRequest that does not fit its
  buffer is never pending.
- Round 0's D4 (`tr_issued_at` + T), withdrawn by R1.

**Alternatives rejected:**
- The count from `tr_issued_at` (round 0). It repeats 105 C-2(b)'s defect and gives the owner's "not yet written" state
  a second instant (N-2).
- Keeping `tr_written_at` and bounding only the write: a TestRequest blocked forever never starts its clock, which is
  the defect of #714.

## D5 — An opt-in limit on every non-ending write (#714b; FR-010 to FR-012)

**Knob.** `SessionConfig::write_timeout_ms`, `std::uint32_t`, 0 = off (the default). There is no C-ABI setter (owner).
TOML key `write_timeout_ms`:
- a bare integer;
- out of the `uint32` range → `out_of_range`;
- any other type → `malformed_value` (`logon_timeout_ms`'s shape; Q4, resolved).

**Where the clock starts.** At the `async_write` call in `live_write_serialized_`, after the `write_gate_` grant and
after ruling W's hold-back test. The wait at the gate is not counted (FR-010). 105's ending limit counts from the
Logout leaf's entry, before the gate (C-2.7). The two starts differ on purpose.

**Which writes.** Every live write except a Logout's, the Logon classes before Active included (the initiator's Logon,
the acceptor's reply Logon, `honor_peer_next_expected_`'s resend frames), and the SequenceReset and ResendRequest (C-6).
- Every Logout fixpp writes ends a session, and is governed by 105's limit or by `close()`'s grace (FR-011).
- The class reaches `live_write_serialized_` as a defaulted `store_then_emit` argument, which the Logout sites pass
  (data-model E-6).
- The resend walk's frames are separate writes, each limited on its own (US4 scenario 2).
- Census of the live write sites: `git grep -n 'store_then_emit(\|live_write_serialized_(' -- src/session`, each
  followed to its class. Its known positive is `run_logout_phase1`.

**Mechanism: D3's connection bound, with nothing allocated per write.**
- With the limit on, a live write sets `write_started_at_` (one `steady_now()` read), bumps `write_epoch_` and sets
  `write_in_flight_`. It wakes the bound unless the bound is already armed on an earlier write's deadline, which is
  never later than this write's; that bound finds the newer epoch at its deadline and re-arms. The returning write
  clears `write_in_flight_`.
- At a wake, or at its deadline, the bound expires the write when all of these hold:
  - the same epoch is still in flight;
  - the clock is at or past `write_started_at_` + limit;
  - the claim is free (D11).
- A write that completes early does not wake the bound; at the deadline the bound finds it returned and parks.

**Expiry.** No Logout (owner ruling). In order:
1. `claim_ending_(write_timeout)` (D11). When it is refused, the bound does nothing: the claimant owns the ending.
2. One warn record, `session ended: write timed out: limit_ms={} error={}`, whose `error` is the value of
   `session_write_timeout`. The format is registered in `format_registry.cpp`.
3. `write_cut_epoch_ = write_epoch_`.
4. `record_disconnected_unless_already_()`. When the session was Active, that fires `onLogout` once. Before Active
   there is no `onLogout`, and the establishment deadline is not tested while the write is blocked (D11, N-1).
5. When `state_ == lifecycle::open`, `spawn_connection_close_(live_transport_shared_())` then
   `connection_end_owed_.reset()` (D3, "One close").

**The error, through every seam it crosses (N-3).**
- `live_write_serialized_` returns `session_write_timeout` for a write whose epoch is `write_cut_epoch_`, once the
  close fails it. Every other failure there stays `dispatch_aborted`.
- `store_then_emit`'s live path, today `if (!write_r.has_value()) co_return std::unexpected(dispatch_aborted)`, passes
  `session_write_timeout` through unflattened. Every other live-write error stays `dispatch_aborted`.
- `Session::send`'s F9 block records `Disconnected` on `dispatch_aborted`. It goes through
  `record_disconnected_unless_already_()`, and it treats `session_write_timeout` as it treats `dispatch_aborted`.
- Census of every branch on the value: `git grep -n 'dispatch_aborted' -- src/session/session.cpp`, keeping the `==`
  and `!=` lines. Each line is ruled in tasks.md: it takes `session_write_timeout` as it takes `dispatch_aborted`, or
  it is named unreachable for a cut write. The known positive is `Session::send`'s F9 block.
- **What a blocked send returns (R5).** At the give-up's close, `dispatch_aborted` (`FIXPP_ERR_CANCELLED` in C),
  unchanged (`B-105-8`). At the watchdog, the cut send returns `session_write_timeout` (new; `FIXPP_ERR_TRANSPORT_IO`
  through `translate`, the one map `fixpp_session_send` applies; unreachable from C while C-4.1 holds), and the writes queued behind it return
  `dispatch_aborted`, unchanged.
- The enumerator: `session_write_timeout = 136`, appended after `wire_undefined_tag` (`[const §X.4]`), with
  `error_message` and the translation in `src/capi/error.cpp`.
  - In C it maps to the existing `FIXPP_ERR_TRANSPORT_IO`, as `transport_write_error` does. There is no new C
    constant, so the occupancy file is untouched. The CSV `tests/capi/expected_error_map.csv` gains the row.
  - `[const §X.4]`'s downgrade frame is not involved: `translate_for_consumer` keys on the C code's introducing minor
    (`introducing_minor`, `src/capi/error.cpp`), and `FIXPP_ERR_TRANSPORT_IO` is a C-ABI 0.2.0 code, so no consumer
    minor maps it to `FIXPP_ERR_UNKNOWN`. §X.4's append-only audit trail (`tools/abi_history/error_codes_v1.txt`)
    lists published C codes and is untouched; the C++ append is pinned by the error-completeness tests T013's census
    lists.
  - `FIXPP_ERR_UNKNOWN`, the precedent of slots 73 and 74, was not taken. This code is returned by a call, the cut
    `send`; codes 73 and 74 are carried only by a log record. `translate` is a total switch (`-Wswitch`), so the code
    needs a mapping whether or not C can reach it, and that mapping is what a C caller would meet should C-4.1 change.
  - Recipe for every site that names the last enumerator: `git grep -n wire_undefined_tag -- include src tests tools
    bindings`.

**Allocation.**
- Limit off: one branch per write; no write wakes the bound. Limit on: one `steady_now()` read and three stores per
  write, plus one timer cancel and one re-arm when the bound is parked (D3's primitive: one recycler block per wait on
  the shipping clock, nothing from the global heap, under `B-544-1`'s two conditions; D9's mallocnesia cell binds it).

**Alternatives rejected.**
- A `co_spawn` bound per write: a per-write allocation on the send path that 544 made allocation-free.
- `async_write || deadline`: the deadline's cancellation need not complete a transport write, so the race may not
  return. Closing the transport is what ends a blocked write (105 D-e).
- A `Transport::Config` write timeout: per factory, not per session; on the transport's real timer, not on
  `effective_clock_`; and every pluggable transport (`[const §XIV]`) would need it.
- Making the liveness loop the watchdog: the loop is off at HeartBtInt 0, and its instants are not the write's.

## D6 — The handshake bounds (#717; FR-013, FR-013a, FR-014)

- **Initiator.** TOML key `tls_handshake_timeout_ms` in the session's `[session.transport]` table (or
  `[default.transport]`), an integer with 0 < v ≤ `UINT32_MAX`: 0, negative and too large are `out_of_range`, and
  another type is `malformed_value` (`logon_timeout_ms`'s shape). It sets `Transport::Config::tls_handshake_timeout`
  for the factory `build_tls_factory` builds.
  - `resolve_transport`'s minting loop adds a third divergence term, the value differing from session[0]'s, so a
    later session's value is never dropped (F-6).
  - Absent, the default stays 30 s.
  - A plaintext session and an acceptor session accept the key and it has no effect: there is no handshake, and
    the listener's bound governs. This is disclosed. Tolerated, unlike the root key at session scope, because
    `[default.transport]` is shared by both roles: a refusal would fail every mixed-role bundle that sets it (N-9). The C++ half already exists (F-6), so `SessionConfig` gains no
    field.
- **Acceptor.** `EngineConfig::acceptor_tls_handshake_timeout`, `std::chrono::milliseconds{1500}`, read where the
  Engine builds the listener's `accepted_transport_config`, in place of the literal.
  - TOML: a root-scope key, `acceptor_tls_handshake_timeout_ms`, with the same range checks, mapped into the new field
    `fixpp::config::EngineEstablishment::acceptor_tls_handshake_timeout` (`std::chrono::milliseconds`, default 1500 ms),
    which the host copies into `EngineConfig::acceptor_tls_handshake_timeout` as it copies `clock` (C-2). The TOML root
    is the engine scope, and there is no `[engine]` table.
  - Copy sites, census at `9d81082a`: `git grep -nE '= *[a-z_]+(\.|->)engine\.clock' -- tests examples bindings bench`
    reports `tests/config/test_098_dialect_overlay.cpp` and `tests/config/test_099_toml_live.cpp`, and no example or
    binding. Both are known positives; no helper builds an `EngineConfig` from a bundle.
  - At session scope it is refused, by the precedent of `dialect_overlay` at root: tolerating it would be a silent
    no-op.
  - `EngineConfig` is `fixpp::core` and installed, so this is a public layout change (D7).
- No C-ABI setter for either (owner). QuickFIX C++ v1.16.0 fixes 10 s (`SSLSocketInitiator`,
  `getSecondsFromHandshakeStart(now) > 10`); QuickFIX/J and QuickFIX/Go have no handshake setting.
- **What a cell can observe (N-7, ruling R4).**
  - The factory's `Transport::Config` is private, with no accessor (`transport_factory.hpp`). So the TOML cells observe
    the key through public calls: the resolved bundle's factory mints a transport (`TransportFactory::make`), and that
    transport's `async_handshake`, against a listener that accepts and never speaks TLS, returns
    `transport_handshake_timeout` at the key. No accessor is added.
  - `ReconnectFsm::drive_reconnect_attempt` turns a failed handshake into `continue`, with nothing recorded, and after
    `max_attempts` returns `transport_reconnect_limit_exceeded`, which `Session::drive_reconnect` returns unchanged.
    FR-014 asserts the transport's `transport_handshake_timeout`, and that return with `max_attempts = 1` within the bound
    plus the margin (attempt 0 has no backoff). The FSM's final error is not changed (C-observable; no D7 clause).

**Alternatives rejected.**
- A `SessionConfig` field: it duplicates `Transport::Config`, which the factory already owns.
- Changing `TransportFactory::make` to take a per-session bound: a pluggable interface change (`[const §XIV.2]`).
- An `[engine]` table: a new top-level structure for one key.

## D7 — Surfaces: C-ABI 1.19, BREAKING, behaviour only (`[const §X.7]`)

- `version.h`: MINOR 19, and a 1.19 history entry:
  - an unauthorised identity's Logon draws nothing and changes nothing;
  - a refused Logon or acknowledgement keeps NextNumIn;
  - the give-up fires with a write blocked, its Logout limited as 105's;
  - the C++/TOML-only write limit and handshake keys, unreachable from C.
- `session.h` clauses "amended in C-ABI 1.19, 106" (C-5): `fixpp_session_close` (a new `BREAKING (C-ABI 1.19,
  [const §X.7]; 106)` block: a refused Logon or acknowledgement keeps NextNumIn; and contract C-6's no-reach clause:
  an unauthorised identity's Logon draws nothing and changes nothing in C++ and TOML, no C session reaches an identity
  gate that can refuse a Logon the base let establish, and close's return code is the base's on every C session),
  `fixpp_session_is_established` (the give-up with a write blocked) and `fixpp_session_send` (a send
  blocked at the give-up's close returns `FIXPP_ERR_CANCELLED`). `fixpp_session_open` is unchanged.
  - Census at `9d81082a`: `git grep -n 'amended in C-ABI 1.18\|BREAKING (C-ABI 1.18' include/fix/c_api/session.h`
    lands in the docs of `fixpp_session_close`, `_is_established`, `_send`, `_register_callback` and
    `_register_send_callback`, none in `_open`'s. The two callback clauses are unchanged by 106: it changes which
    admin frames a Logon arm writes and when an ending comes, not which application messages are delivered or
    originated.
  - No C callback carries `onLogout`: `fixpp_session_register_callback` is the inbound receive callback and
    `fixpp_session_register_send_callback` the outbound `toApp` one (re-derive: `git grep -n -i logout include/fix/c_api`
    finds clause text only), so FR-017's at-most-once `onLogout` changes no C clause.
- Each change is breaking by `[const §X.7]`'s definition, "sees a call it could already make fail or return a result
  other than the one documented": `fixpp_session_send` blocked at the give-up returns `FIXPP_ERR_CANCELLED` where it
  hung; `fixpp_session_is_established` turns false at the give-up's ending where it stayed true; and the frames a C
  session's peer receives at a Logon change (C-2). `fixpp_session_close`'s return code is not among them, and C-1 has
  no reach from C: contract C-6's no-reach clause, `/speckit-analyze` re-run finding E1, ruled unreachable.
- §X.7's BREAKING marks, each with its task: the `version.h` history entry (T148), the documentation of each affected
  declaration in `session.h` (T149), the PR description (T178) and the B&L delta (`B-106-6`, T154).
- MINOR pins census: `git grep -nE 'MINOR 18|1\.18' -- include tests bindings tools ci examples` (`tests/interop` is
  the interop harnesses' row; `examples` is in the path list so a pin there is found). "Python binding: version pins
  only" is that census's reading for `bindings/`, recorded by T018 with a seeded positive.
- Article X §7's predicate: `gh release list --exclude-drafts` is empty at the PR head, so the MINOR bump stands.
  The result is recorded in the evidence file.
- **C++ BREAKING (public layout):** `SessionConfig` (+`write_timeout_ms`), `EngineConfig` and
  `fixpp::config::EngineEstablishment` (each +`acceptor_tls_handshake_timeout`; both in F-8's size-pin recipe) and `Session` (the connection bound's members, its counter and `ending_claim_`).
- **C++ additions:** `SeqnumManager::classify_inbound` and `CompIdAuthorizationPolicy::has_logon_validator`.
- **Error enum:** `session_write_timeout` appended.

## D8 — B&L delta (the LIVE `spec/behaviors-and-limitations.md`)

- **Retired** (moved to `spec/behaviors-and-limitations-closed.md` with "closed by 106"): `L-104-3` by D1, `L-104-8` by D2.
- **Amended, "(amended 2026-11-25, 106)", each changed clause named (N-6):**
  - `B-099-2`. The title's "an initiator's block runs ahead of mTLS" becomes "the identity gates run first in both
    roles" (D1). Its "Consume limit" clause says the gate Reject and the block both keep NextNumIn on every role (D2).
    It cites `L-104-8` as closed.
  - `B-105-3`. "The write counted from the return of the TestRequest's store and write" stays for a written
    TestRequest. A TestRequest or Heartbeat whose write has not returned is given up at L + 3T + 1 ns (D4), and the Logout's
    Text names the issued TestReqID even when that TestRequest's write did not complete, so the peer may never have
    received it (D4, "Supersedes"). "With none pending before the hold there is no cap, `L-105-f`" becomes "capped at
    L + 3T + 1 ns with one pending or none" (D3).
  - `L-093-12`: the store calls that hold `close()`'s and `stop()`'s joins include the liveness loop's TestRequest or
    Heartbeat store call; the give-up at G and the close at G + B still happen (106).
  - `B-544-1`: its zero for a sequential `Session::send` gains a third condition: "and, when `write_timeout_ms` is set, the session's effective clock is `system_clock_source`" (106, `L-106-e`; ruled 2026-11-26 by the orchestrator under the owner's delegation (spec.md Clarifications, R14), flagged for the owner's review at merge). `L-497-1` gains condition (6) *Clock*, with `L-106-e`'s text. Clause by clause (live text → new text):
    - `B-544-1` title. Old: "… and a sequential `Session::send` make no global-heap allocation in steady state, on two conditions: the Engine's executor's target type is exactly `asio::io_context::executor_type`, and the thread that runs the session's handlers stays inside one scheduler call (`io_context::run`) across messages." New: "… and a sequential `Session::send` make no global-heap allocation in steady state, on two conditions: the Engine's executor's target type is exactly `asio::io_context::executor_type`, and the thread that runs the session's handlers stays inside one scheduler call (`io_context::run`) across messages; and, for `Session::send` only, on a third: when `write_timeout_ms` is set, the session's effective clock is `system_clock_source` (106, `L-106-e`)." The inbound read, the hydrate apply step and `MemoryStore::store` take no wait of the bound and keep the two conditions.
    - `B-544-1` body. Old: "The two conditions are the `[const §VIII.5]` deviation's justification (`.specify/544-hot-path-zero-alloc.md` §5, owner ruling R-6); the slot-count obligation is a build precondition, not a third condition (owner ruling R-12)." New: "The two conditions are the `[const §VIII.5]` deviation's justification (`.specify/544-hot-path-zero-alloc.md` §5, owner ruling R-6); the third, on `Session::send` only, extends that justification under R-7's reading of §XV.1 (106 ruling R14, orchestrator under the owner's delegation, flagged for the owner's review at merge). The slot-count obligation is a build precondition, not a condition (owner ruling R-12). The liveness emit is on no path this row gates; on a non-shipping clock its per-emit-edge wait is `L-106-e`'s and `L-497-1` (6)'s."
    - `L-497-1` title. Old: "the zero allocation of `B-544-1` holds only under its two conditions and on the paths it gates". New: "the zero allocation of `B-544-1` holds only under its conditions and on the paths it gates".
    - `L-497-1` body. Insert after condition (5) *TLS*'s sentence "… tracked as fixpp#565." and before "**Status: disclosed**": "(6) *Clock* (106, `L-106-e`). When `write_timeout_ms` is set and the session's effective clock (`SessionConfig::clock_override` when set, else `EngineConfig::clock`) is not `system_clock_source`, each spaced `Session::send` write arms one wait of the connection bound, `timer.async_wait(…) || detail::await_deadline(*effective_clock_, deadline)`; and, with the limit on or off, each liveness-emit edge arms one such wait. Each may allocate per message, through the clock's `sleep_until` and fixpp's own `||` group (its parallel-group state, its two spawned arms and `await_deadline`'s frame); no zero is claimed for it. With the limit off the send path is as before; on `system_clock_source` both paths are as before."
  - Census, run before the edit: `grep -n 'L-093-12\|L-104-3\|L-104-8\|L-105-f\|B-544-1\|L-497-1\|tr_written\|runs ahead of mTLS'
    spec/behaviors-and-limitations.md`. Its known positive is `B-099-2`'s line. Each hit is ruled in tasks.md: amended,
    or named as unaffected.
- **Narrowed: `L-105-f`.**
  - Non-ending writes are bounded when `write_timeout_ms` is set.
  - The give-up no longer waits behind a blocked write, and the held no-pending case is capped (D3).
  - What stays open:
    - With the limit off, a peer that keeps sending and never reads holds a non-ending write until `close()` or
      `Engine::stop()`, as QuickFIX C++'s and Go's defaults do. A C session always runs with the limit off (contract
      C-4.1), so for a C caller this holds on every session.
    - With `logout_disconnect_timeout_ms` 0 or no clock, ending Logouts stay unbounded, and so does a give-up decided
      while a write is blocked. The write limit does not cut that write once the give-up holds the claim (D11).
- **New rows**, in section `## 106`:
  - `B-106-1`: the identity gates first (silent, no state change), including the credentials rule's width (D1, N-11), the observables of each refusal class (contract C-1.3), and that a direct mTLS initiator started by `open()` holds no peer identity and never reaches Active (contract C-1.7; start it with `drive_reconnect()`);
  - `B-106-2`: NextNumIn kept on every Logon refusal, with D2's table;
  - `B-106-3`: the give-up with a write blocked, D4's instant, and the one ending (D11);
  - `B-106-4`: `write_timeout_ms`;
  - `B-106-5`: the two handshake keys;
  - `B-106-6`: the surfaces (D7).
- **New limitation rows:**
  - `L-106-a`: a refusal after a reset that already ran in the arm keeps that reset's numbers (Q3, resolved). The resets
    are the acceptor's `reset_on_logon` and a 141=Y unit.
  - `L-106-b`: the write-timeout reason is carried only by its record and by the cut call's return, as `L-105-c`.
  - `L-106-c`: the bound event's new position, and a validator called twice per connection (D1).
  - `L-106-d`: the session-level handshake key has no effect on a plaintext or acceptor session.
  - `L-106-e`: the zero-allocation and own-wait claims of D3 and D5 hold on `system_clock_source` only. When the session's effective clock (`SessionConfig::clock_override` when set, else `EngineConfig::clock`) is another `Clock`, the connection bound waits through its `sleep_until` inside an `||` group: once per spaced write with `write_timeout_ms` set, and once per liveness-emit edge with the limit on or off. That may allocate per message, and no zero is claimed for it. `B-544-1` carries the send half as its third condition and `L-497-1` (6) carries both halves (amended rows above), under ruling R14 and its step 9 scope (2026-11-26, orchestrator under the owner's delegation, flagged for the owner's review at merge). On such a clock the own-wait claim is the clock's to keep: a user clock whose `sleep_until` keeps one timer per Session would let the bound's wait re-arm the liveness loop's sleep (#715's hazard); a user clock must serve concurrent sleepers of one Session independently, as `mock_clock` does.
  - `L-106-f` (N-1; pre-existing, not fixed by 106): before Active, with the limit off, a write blocked inside the Logon
    arm (the initiator's Logon in `open()`, the reply Logon, a 789 resend frame, a Logon-arm Reject) is not cut by
    `logon_timeout_ms`: the Engine tests that deadline only between deliveries and while it reads. `write_timeout_ms`
    cuts it. Pinned by `GiveUp106.PreActive_LimitOff_NotCutByEstablishment`.
- **B129: `B&L delta: none`.** A CI job cap is not shipped behaviour.

## D9 — Test design (FR-016: RED against `9d81082a`, or a named mutant)

**Rigs.** `t105::DirectRig` / `rgs::Scene` (direct Session, mock clock, `BlockingWriteTransport`, both roles) for D2
to D5 and D11; the 104 Logon rigs for D1 and D2; an Engine rig of 105's `PreActive105` shape for the pre-Active write cells;
real TLS sockets for D6; `system_clock_source` for the two shipping-clock cells only. Every `BlockingWriteTransport`
cell releases its blocked writes at teardown, so a base run fails by assertion, never by a hang.

**The seam-file rule (105's, N-4).**
- A cell that names a symbol the base lacks lives in `tests/session/test_106_seams.cpp`, ctest `session_106_seam_tests`
  (labels `session`, `106` and `106_seam`). Such symbols are `write_timeout_ms`, `session_write_timeout`, `EngineEstablishment::acceptor_tls_handshake_timeout`,
  `EngineConfig::acceptor_tls_handshake_timeout`, a test-access read of a new member, and a new seam.
- A base reading builds only `session_106_tests` and excludes the seam entry (`-LE 106_seam`).
- Each seam cell is marked **[seam: Mn]**: the base build lacks the field, so RED is shown on the named mutant at the
  head, and that RED is recorded in place of the base reading.
- Every other cell compiles on the base and is RED there, or is a **[guard: Mn]**.
- Each mock-clock budget is derived from the competing timeout, and each timer cell has its spurious-hit arm.
- **Instants (mock-clock cells; C-4).** G := `detail::held_test_request_cap`'s value, L + 3T + 1 ns, for an emit not
  returned, or 105's max(L + 2T, `tr_written_at` + T) + 1 ns for a written TestRequest. `mock_clock::advance` and
  `step_to` set the new time before they complete the due waiters, so a woken decider reads the advanced instant: every
  timer cell reaches G with `step_to(G)` and drains, never with a coarser `advance` (the one exception is
  `OneEnding106.BothDueAtOneWake`, row FR-017). It asserts the slot-74 record
  absent at G − 1 ns and present at G, then open at G + B − 1 ns and closed at G + B. The write-limit cells do the same
  from the write's start S: open at S + W − 1 ns, closed at S + W. W is `write_timeout_ms`; L stays
  `last_inbound_steady_` (N-6). The shipping-clock and real-socket cells keep their wall-clock budgets, on every lane they run on. A timing failure of
  one on a sanitizer lane is re-run once with its budget × 10 before it is read as a defect or as a budget change (a
  timeout is not a hang until then). Each timing row states its parameters and the inequality of its instant under test against the competing instant, with the margin (Gate A round 3, root cause #3).

| FR | Cells | RED / mutant |
|---|---|---|
| FR-003/004 | `IdentityFirst106.<Refusal>_<Gate>`, the cells the base's order can construct. Acceptor, gates off-list mTLS and refused credentials, the latter on a non-mTLS (plaintext) acceptor and on an mTLS acceptor whose identity is admitted: validate-first, posture, SendingTime, strict, `reset_on_logon`, 1137. Acceptor, gate unreadable credentials with a validator: validate-first only, on a non-mTLS acceptor and on an admitted mTLS one (every other refusal follows `interpret_logon`, which such a frame does not pass, so the base ends it silently before them). Initiator, a direct mTLS initiator started by `open()` (the credentials gate is acceptor-only): validate-first, posture, SendingTime, S3, asked 141=Y. Each asserts zero frames, both numbers unchanged in memory and store, no reset, `Disconnected` | RED on the base because the pre-gate action writes a frame, changes sequence or reset state, or both; each cell records the assertion that fails on the base. `reset_on_logon`, asked 141=Y and acceptor 1137 × off-list mTLS write no frame on the base (its acceptor gate precedes 1137): their RED is the reset or the in-memory NextNumIn. `ValidateFirst_RefusedCredentials_NonMtls` is RED under M1a |
| FR-003 plain (N-5) | `IdentityFirst106.Plain_<Role>_<Gate>`: a well-formed, in-sequence, unauthorised Logon or acknowledgement, with no other fault. Its gates are row FR-003/004's, less unreadable credentials; `Plain_Acceptor_RefusedCredentials` runs on a plaintext acceptor. `IdentityFirst106.Strict_Initiator_<Gate>`: the `bilateral_strict` silent refusal. Each asserts zero frames and NextNumIn unchanged in memory and store | RED on the base for the in-memory NextNumIn |
| FR-005 | `IdentityFirst106.Admitted_<Refusal>`: byte-identical answer. `IdentityFirst106.Admitted_StayInArmReject_NextLogonActive`: a stay-in-arm Reject, then a corrected Logon, reaches Active. `IdentityFirst106.Admitted_UnreadableNoValidator_ValidateFirstReject`: a non-mTLS acceptor with inbound validation on and no logon validator installed (`set_logon_validator` not called), a Logon `interpret_logon` refuses, draws the validate-first Reject byte-identical to the base's (the credentials half admits an unreadable input when no validator is installed) | guard M1 (the stay-in-arm cell), M1b (the rest); `Admitted_UnreadableNoValidator_ValidateFirstReject`: M1c |
| FR-006 | `NextNumIn106.<Role>_<Refusal>`, one per D2 row whose 106 column is "unchanged" or "unchanged; no unit runs", each asserting NextNumIn unchanged in FR-006's sense: in memory, equal to the value the arm classified the frame against (the refused frame's MsgSeqNum, in sequence and not consumed; an acceptor hydrates inside the arm, so a value read before the feed is not it), and the store's unchanged. The "no unit runs" rows, `NextNumIn106.Initiator_Asked141_S3_NoAdvanceNoReset` and `NextNumIn106.Acceptor_Peer141_S3_NoAdvanceNoReset`, also assert that the store's `reset_to` was not called. `NextNumIn106.Acceptor_ResetOnLogon_S3_KeepsTheReset` pins `L-106-a` | RED on the base for every such row (the base consumes in memory). The residual cell is a guard (its mutant moves the knob's reset after S3). At the head: M6a (the acceptor's commit) for every `NextNumIn106.Acceptor_*` cell, M6b (the initiator's) for every `NextNumIn106.Initiator_*` cell |
| FR-006 commit (N-4) | `NextNumIn106.CommitRefused_Disconnected` [seam: the manager frozen between classification and commit, through a `session_test_access` hook]: `Disconnected` once, nothing sent after it, NextNumIn unconsumed in memory and store | the seam; mutant: the commit's result ignored |
| FR-007 | the 104 `Precedence_*` cells and `UnaskedResetAck104.Mtls_Unauthorised_NoReset`, unfiltered; the one update below | GREEN; the update is named in plan.md, P1 |
| FR-008 | `GiveUp106.{TestRequestBlocked, HeartbeatBlocked, BehindBlockedSend, ResendHeldNoPending, RejectBlocked}`, limit off. H = 10 s, T = 10 s, B = `logout_disconnect_timeout_ms` = 500 ms. The Instants above: open at G + B − 1 ns, closed at G + B; no slot-74 record at G − 1 ns, one at G. No complete Logout at the peer. A peer read released inside B gets the Logout. **HeartbeatBlocked** (N-10): one peer frame at t = 5 s, so the Heartbeat falls due at 10 s and the TestRequest at 15 s; G = 35 s + 1 ns. Inequalities: in `TestRequestBlocked` the TestRequest instant L + T = 10 s ties the Heartbeat instant (last outbound + H = 10 s), and the loop's order (give-up, TestRequest, Heartbeat) emits the TestRequest; in `HeartbeatBlocked` the Heartbeat precedes the TestRequest by 5 s. Every cell's G is L + 3T + 1 ns (an emit not returned, or held). Nothing else closes by G + B: the peer is silent, the limit is off, and 105's ending limit is armed only at the decision, so G + B has no competitor (M2's arm) | RED on the base (no close at G + B), except `ResendHeldNoPending`'s pending twin, a guard |
| FR-008 spawn failure | `GiveUp106.RejectBlocked_EndingBoundSpawnFails_ClosedAtG`: `RejectBlocked`'s rig (a Reject written inside `on_inbound_frame`, its write blocked, the loop's emit queued behind it) with `session_test_access::set_ending_bound_spawn_fails` returning true. At G: one slot-74 record, the transport closed (the peer's read ends), one `Disconnected` entry, the blocked Reject write and the queued emit return an error, no Logout. The rig is the discriminating one: with no inbound frame in flight, a connection bound that releases the latch at its exit (105's roots do) would close anyway, and M14 would read GREEN. Inequality: 105's ending bound is never spawned, so no cut at G + B competes, and nothing else closes (the peer is silent, the limit is off); the spurious check at G + B + 1 s has no competitor | RED on the base (no give-up at G); M14 (`decide_give_up_()`'s `refused` branch records `Disconnected` without the close): still open at G + B + 1 s |
| FR-008 store phase (C-1) | `GiveUp106.EmitHeldInStore_GivesUpAtG_ClosesAtGPlusB`: GiveUp106's H, T and B; the loop's TestRequest held inside its `MessageStore::store` call (`t105::GateStoreFactory`, as `LivenessTiming105.FreshWindow_FromTheWrite_StoreDelayAtLeastT` uses it). No slot-74 record at G − 1 ns, one at G; open at G + B − 1 ns, the transport closed at G + B. It asserts no `Disconnected` and no join: on a direct rig nothing records either until the store returns (`L-093-12`, amended). The store is released at teardown. Inequality: as `TestRequestBlocked`'s; the held store is the only suspension, so G + B has no competitor | RED on the base (no slot-74 record at G); M2 |
| L-106-f (N-1) | `GiveUp106.PreActive_LimitOff_NotCutByEstablishment`: Engine rig, limit off, the reply Logon's write blocked, `logon_timeout_ms` 1 s; still open at `logon_timeout_ms` + 1 s. Inequality: the check at 2 s follows the establishment deadline by 1 s, so a fix that tests it mid-write closes first; the limit is off, no ending is decided and the liveness loop runs only from Active, so nothing else competes | guard of the disclosure; a fix turns it RED |
| FR-008 spurious | every `GiveUp106` cell under M2 (bound's emit arming deleted; for `ResendHeldNoPending`, the no-pending cap deleted) | RED: nothing else closes by G + B |
| FR-008 carve-out (N-8) | `GiveUp106.ZeroLogoutTimeout_StaysOpen`: B = 0, emit blocked; still open at G + 10 s. Inequality: G + 10 s follows G + 2000 ms (the default B, which a regression ignoring the 0 would use) by 8 s | guard of the disclosure; mutant: an unconditional close at G |
| FR-009 | `GiveUp106.{TestRequestBlocked, HeartbeatBlocked, BehindBlockedSend, ResendHeldNoPending, RejectBlocked, HeartbeatReplyBlocked, BusinessRejectBlocked, RejectBlocked_EndingBoundSpawnFails_ClosedAtG}` each assert that each blocked and queued write returned an error (`dispatch_aborted`), and FR-017 for each tail the close resumes (the blocked send's F9 block in `BehindBlockedSend`, the Heartbeat reply's write tail in `HeartbeatReplyBlocked`, the BusinessMessageReject's write tail in `BusinessRejectBlocked`, the Reject leaf in `RejectBlocked`, the loop's give-up path in the emit cells): exactly one new `Disconnected` entry in `fsm_visit_history()`, `onLogout` once, and exactly one slot-74 record. `HeartbeatReplyBlocked`: the peer's TestRequest at L draws the Heartbeat reply (`on_inbound_active_test_request_`'s `inbound_heartbeat_reply_leaf_`) and that write blocks. `BusinessRejectBlocked`: an application whose `fromApp` rejects the peer's NewOrderSingle at L draws the BusinessMessageReject (`on_inbound_active_from_app_failed_`'s `inbound_business_reject_leaf_`) and that write blocks. Both take `RejectBlocked`'s parameters (limit off, H = T = 10 s, B = 500 ms, the peer silent after L): the loop's first emit after L falls due by L + T = L + 10 s and queues behind the blocked write, so G = L + 3T + 1 ns = L + 30 s + 1 ns follows it by at least 20 s, and nothing else closes by G + B. Not the guards that never close (`ZeroLogoutTimeout_StaysOpen`, `PreActive_LimitOff_NotCutByEstablishment`), nor `EmitHeldInStore_*`, whose emit returns only at teardown | as FR-008; per tail, M13, one arm at each tail that records (tasks.md T015's table): the F9 block (`BehindBlockedSend`), the loop's TestRequest and Heartbeat tails (`TestRequestBlocked`, `HeartbeatBlocked`), `inbound_heartbeat_reply_leaf_` (`HeartbeatReplyBlocked`) and `inbound_business_reject_leaf_` (`BusinessRejectBlocked`), two `Disconnected` entries each. The Reject leaf's write-failure tail records nothing (`inbound_reject_leaf_` discards `emit_r`), so `RejectBlocked`'s one `Disconnected` is the close's and M13 has no arm there |
| US3 sc.4 | `LivenessTiming105.*`, `HeldTestRequestCap105.*`, `ReplayGate588.R588g*/R588h*`, unfiltered | guards |
| FR-010 | `WriteTimeout106.<Class>` [seam]. Classes: send, Heartbeat, TestRequest, resend frame, Reject, SequenceReset, ResendRequest (Active); initiator Logon, reply Logon, `honor_peer_next_expected_` frame (pre-Active; C-6). W = 300 ms and HeartBtInt 1 s in every class; in Active the peer sends a Heartbeat every 1 s, so the give-up cannot fire. The TestRequest class sets `test_request_threshold` = 500 ms. The TestRequest is issued at L + 500 ms, its write blocks at S = L + 500 ms, and the cut at S + W = L + 800 ms precedes both the next peer frame (L + 1 s) and the bound's give-up row (L + 3T + 1 ns = L + 1.5 s + 1 ns). Every other class keeps the default T = 1.2 s, which the 1 s peer cadence never reaches (margin 200 ms, exact on the mock clock). The TestRequest class's rig blocks the first TestRequest (35=1) write, so a Heartbeat due before it completes and one due after it queues behind it. Each asserts open at S + W − 1 ns and closed at S + W, the record, the cut call's `session_write_timeout`, `dispatch_aborted` for the queued writes, and no Logout, and FR-017 for the cut's resumed tail: exactly one new `Disconnected` entry in `fsm_visit_history()`, `onLogout` once from Active and none before Active, and exactly one write-timeout record. Pre-Active: the Engine tests the establishment deadline only between deliveries and while it reads (`run_read_pump`'s `expired()` sites), so no competing deadline runs while the write is blocked. W = 300 ms there too, with S within 100 ms of the accept: the cut at S + W precedes `logon_timeout_ms` = 10 s by at least 9.6 s, so the establishment deadline never competes. The race itself is `OneEnding106.Watchdog_PreActive_NoEstablishmentRecord`'s. The Reject class's rig draws the two-argument `emit_session_reject_` (SessionRejectReason 3), whose write tail records `Disconnected` (tasks.md T015's table): an application whose `fromAdmin` rejects an admin frame in Active (`on_inbound_active_from_admin_failed_`), or an application MsgType with no application registered (`on_inbound_active_unsupported_`); its M13 arm is that tail's. No `onLogout` | [seam: M3′] (the bound's write arming deleted) and M3″ (the cut does not mark the epoch: the cut call returns `dispatch_aborted`); M3‴ (`store_then_emit`'s pass-through deleted) for `WriteTimeout106.Send`, whose cut `send` returns through `send_impl`'s `store_then_emit`; per class, M13 (its tail's raw `Disconnected` restored: two entries) |
| FR-010 start | `WriteTimeout106.QueuedWrite_CountsFromItsOwnStart` [seam]: W = 300 ms; B queued at A's start S_A, A released at S_A + W − 1 ms, then B blocked, cut at B's start + W = S_A + 2W − 1 ms. Inequality: M3's cut, at S_A + W, precedes it by W − 1 ms = 299 ms; the cell spans under 2W < 1 s, inside the peer cadence's protection | M3 (start taken at the gate) |
| FR-010 walk | `WriteTimeout106.ResendWalk_ProgressNotCut` [seam]: three frames, W = 300 ms, each released at its own start + W − 1 ms; the walk completes. Inequality: each release precedes its own cut by 1 ms (exact on the mock clock); the walk's 897 ms is below T = 1.2 s, so no TestRequest falls due even with inbound held for the walk (margin 303 ms) | M3 |
| FR-010 spurious | each `WriteTimeout106` cell with `write_timeout_ms` 0: open at S + W + 1 s = S + 1.3 s. Inequality: with the 1 s peer cadence L is never more than 1 s old, so neither the TestRequest instant (L + 1.2 s) nor the bound's give-up row (L + 3T + 1 ns; L + 1.5 s + 1 ns for the TestRequest class) is reached (margins 200 ms and 500 ms); pre-Active, S + 1.3 s precedes `logon_timeout_ms` 10 s | — |
| FR-011 close | `WriteTimeout106.CloseLogout_KeepsGrace` [seam]: W = 300 ms, `logout_disconnect_timeout_ms` 5000 ms; Active, `close()` called, its `run_logout_phase1` Logout write blocked from S; the mock step to S + W + 1 ms issued at once after `close()`: no write-timeout record, the transport still open. Inequality: `close()`'s grace is a wall-clock `asio::steady_timer` of 5 s, so the check, made within the mock step's drain, precedes it by seconds of wall time; the mock step reaches the write limit's instant, so an `ordinary`-class Logout is cut first | M4 at `run_logout_phase1`'s site (its `write_class::logout` replaced by `ordinary`) with M9 and the closing term deleted from E-3's exit row: a write-timeout record at S + W. M4 alone is masked: `closing_or_stopped_()` exits the bound before its expiry row, so `write_class::logout` is the second line behind that term |
| FR-011 | `WriteTimeout106.EndingLogout_Keeps105Limit` [seam]: W = 300 ms, B = 500 ms, the give-up's Logout (GiveUp106's H and T, its TestRequest written) blocked from S = G; open at S + W and at S + B − 1 ns, closed at S + B. Inequality: S + B follows S + W by 200 ms, so M4 closes first at S + W; the Logout is not `close()`'s, whose grace timer would also end at B | M4 at the R-1 leaf's site (`inbound_logout_leaf_`, kind `r1`) with M9 and M9′. M4 alone is masked: the give-up holds the claim, so E-3's exit on a taken claim ends the bound first, and `write_class::logout` is the second line behind the claim |
| FR-012 | `TomlWriteTimeout106.{OutOfRange, WrongType}` (config suite; compiles on the base). `TomlWriteTimeout106.Set` [seam: M11] (N-3): the resolved `SessionConfig::write_timeout_ms` equals the key, set in a `[[session]]` table and, separately, in `[default]` (C-4.7). `OutOfRange` covers −1 and `UINT32_MAX` + 1. `TomlWriteTimeout106.KeyAbsent_Off` [seam: M11′]: a bundle with no `write_timeout_ms` key resolves `SessionConfig::write_timeout_ms == 0`, and `SessionConfig{}.write_timeout_ms == 0`; exact equality, since no timing cell loads the key absent | RED on the base (unknown key); `Set`: M11 (the mapper drops the value); `KeyAbsent_Off`: M11′ (`SessionConfig`'s default changed to 300 ms) |
| FR-017 (D11) | `OneEnding106.<First>Then<Second>` [seam where the limit is set], each pair with the suspension point it exercises (root cause #2): GiveUp/Watchdog (the loop, sleeping, decides at G while a send is blocked and armed); Watchdog/GiveUp (the bound cuts the send at S + W < G; the loop wakes at G); Watchdog/EndingLimit, EndingLimit/Watchdog (an R-2 Logout queued behind a blocked send); Watchdog/Close, Close/Watchdog, GiveUp/Close (`close()` called with the send blocked). Parameters and inequalities: GiveUp106's H = T = 10 s, W = 300 ms, B = 500 ms. GiveUp/Watchdog: the send blocks at S = G − 100 ms, so G < S + W = G + 200 ms < G + B (margins 200 ms and 300 ms: the expiry comes while the give-up's Logout is still bounded). Watchdog/GiveUp: S = G − 500 ms, so S + W = G − 200 ms. Watchdog/EndingLimit: the R-2 decision at D = S + 100 ms, S + W = S + 300 ms < D + B = S + 600 ms (300 ms). EndingLimit/Watchdog: W = 1 s, D + B = S + 600 ms < S + W = S + 1 s (400 ms). Watchdog/Close: `close()` at C = S + W + 100 ms. Close/Watchdog: C = S + 100 ms, so C < S + W < C + B, `close()`'s grace (200 ms and 300 ms). `close()`'s grace is a wall-clock `asio::steady_timer`: the pairs with `close()` set `logout_disconnect_timeout_ms` 5000 ms and issue the mock step at once after `close()`, so the grace cannot expire first in wall time; the 105 ending limit's instants stay on the mock clock. GiveUp/Close: C = G + 100 ms < G + B. `OneEnding106.BothDueAtOneWake` (N-7): the loop in its TestRequest emit, that write armed; one `step_to` from below both G and S + W to an instant at or past both (the one exception to the Instants rule; S = G − W, the step from G − 1 ms to G, both below G + B); the give-up claims. `OneEnding106.Watchdog_PreActive_NoEstablishmentRecord` [seam] (N-1; order inverted in Gate A loop 2 round 2, N-2): Engine rig, the reply Logon blocked, W = 2 s, `logon_timeout_ms` 1 s; at S + W one write-timeout record, no `session_event_establishment_timeout`, no `onLogout`, and still one record and no establishment record at S + W + 1 s. Inequality: the Logon is delivered at 0 and the reply written at S = 0, so the establishment deadline at 1 s precedes the cut at S + W = 2 s by 1 s. On the head the pump does not test the deadline mid-write (R7, `L-106-f`), so the cut is the first decider, and the pump's next test, after the cut, finds the claim taken; the check at 3 s follows the cut by 1 s, with nothing else armed (the liveness loop runs only from Active). Under M10 alone the deadline claims at 1 s, closes and fails the write: an establishment record and no write-timeout record. Each asserts FR-017 as written: one `Disconnected` entry in `fsm_visit_history()` (public on the base), `onLogout` at most once, one close, and the reason records FR-017 names: one where the first claimant is the give-up, the write limit or the establishment deadline, none where it is an ending Logout or `close()`. a pair whose second party is stopped by a state test or is not a claimant is a guard of that test and is not counted toward M9: `WatchdogThenEndingLimit` (the expiry records `Disconnected` with no suspension, so a later R-2 leaf returns at its state test before claiming), `WatchdogThenGiveUp` (the loop, waking at G, fails `liveness_may_originate_()` and returns before `decide_give_up_()`), `WatchdogThenClose` and `GiveUpThenClose` (`close()` takes no claim). In `CloseThenWatchdog` the bound meets E-3's `closing_or_stopped_()` exit row before its expiry row, so its mutant is M9 with that term deleted from the exit row | M9 (the claim test deleted) with M9′ (E-3's exit on a taken claim deleted): two reasons or two entries; a pair GREEN under both is reported as vacuous, not counted. The pre-Active cell: M10 (the pump races `on_inbound_frame` against the establishment deadline), RED without M9 |
| C-4 | `ConnectionBound106.{SpawnFails_AtLoop, SpawnFails_AtWrite, WaitThrows}` [seam]: fail closed, `Disconnected`, closed once, zero reason records (FR-017). `ConnectionBound106.Teardown_{Close, EngineStop, Drain, ConnectionEnd}_{Parked, Timing}`: the counter reads 0 after a bounded pump (N polls). Each cell starts in a state where its teardown's own wake and exit term are the only ones that end the bound. No `Disconnected` edge may come before the join, so `Drain` is a direct initiator in LogonSent with `write_timeout_ms` set, its reconnected Logon written and the acknowledgement withheld, and a second `drive_reconnect()` returns; that is `Teardown_Drain_Parked`, where no write is in flight and the drain's step 3 does not park. Every Drain cell sets `logout_disconnect_timeout_ms` 5000 ms: step 3's wall-clock bound is a competing ending (its force-close fails the blocked write, and the `Disconnected` that follows ends the bound), so the budget is derived from it and the pump reads the counter long before it. A teardown that has no such state is reported as covered by the `Disconnected` edge and is not counted: `Close` and `EngineStop` are (data-model E-3's joiner table). `_Timing`: the bound armed on a write at S, W = 300 ms, the teardown at S + 100 ms and the mock clock not advanced after it, so the armed deadline cannot end the bound during the pump. For `Drain` the armed write is a Logon-arm Reject blocked in LogonSent (`WriteExpiryDuringDrain_NoRecord`'s rig), the only write such a session makes after its Logon; the drain then parks at its step 3, so the counter reaches 0 during the pump only through the entry wake, and a wake placed at the join fails the cell. `Teardown_ConnectionEnd_*` reads the counter after the edge and before any `close()`, drain or stop. `ConnectionBound106.WriteExpiryDuringDrain_NoRecord` [seam]: a direct initiator in LogonSent (its reconnected Logon written and `drive_reconnect()` returned), FIXT with inbound validation on, W = 300 ms, `logout_disconnect_timeout_ms` 5000 ms. An acknowledgement fed through `on_inbound_frame` draws 099's S3 Reject (`NextNumIn106.Initiator_S3`'s rig) and the Reject's write blocks at S, so `inbound_depth_` is 1. A second `drive_reconnect()` then enters the drain, which records no `Disconnected` from LogonSent and parks at its step 3 on `inbound_depth_`. `step_to(S + W)` is issued as soon as the drain parks: no write-timeout record, no `Disconnected` from the bound, the bound's counter 0, and the Reject's write ended by the drain's own bound, a wall-clock `asio::steady_timer` that `step_to` does not reach (the cell waits at most 5 s + 2 s of wall time for it). Neither the reconnected Logon's write (`drive_reconnect` is single-flight and awaits `emit_initiator_logon_`) nor a drain from Active (its step 2 records `Disconnected` before step 3) gives this window. `ConnectionBound106.DrainCancelled_RespawnsOnBlockedWrite` [seam] (N-6, data-model E-3's respawn): the same rig, the second `drive_reconnect()` cancelled through its cancellation slot while the drain is parked at step 3, so it returns `dispatch_aborted`; then `step_to(S + W)`: one write-timeout record, `Disconnected`, closed once, and `session_write_timeout` from the Reject's write. `ConnectionBound106.DrainCancelled_RespawnFails_ClosedAtOnce` [seam] (data-model E-3's refused respawn): the same rig, `connection_bound_spawn_fails_` set after the bound's first spawn (the Reject's write armed) and before the cancellation, so the respawn is refused; at that return, with the clock not advanced, the claim taken, `Disconnected`, closed once, zero reason records, and the Reject's write returns `dispatch_aborted`, its epoch not cut. `ConnectionBound106.SpuriousWake_NoEnding`: a wake with nothing due ends nothing | spawn and wait seams (105's shape); teardown: M12, one arm per joiner: that teardown's wake and its exit term deleted (for `Drain`, the `reconnect_drain_in_flight_` term; for `ConnectionEnd`, the edge's wake, since its two terms are redundant), fails the counter assertion (never a hang); `WriteExpiryDuringDrain`: the drain term and `claim_ending_`'s drain refusal deleted, a write-timeout record; `DrainCancelled_RespawnsOnBlockedWrite`: the failed-drain respawn deleted, no write-timeout record at S + W; `DrainCancelled_RespawnFails_ClosedAtOnce`: the respawn's refused-spawn path deleted (the refusal returns without the claim, the record or the close), open, no `Disconnected` (the `WriteExpiryDuringDrain` and `DrainCancelled` arms are M12's too); `SpuriousWake_NoEnding`: "a wake ends the bound" (the bound exits on a wake with no exit term true), RED |
| R2(b) | `ShippingClock106.DirectDriver_BoundBesideLiveness_TestRequestAtT` [seam]. `system_clock_source`, the coroutines on `session->executor()` (the slot path), HeartBtInt 2 s (T = 2.4 s, `default_test_request_threshold`; `test_request_threshold` unset), W = 200 ms, `logout_disconnect_timeout_ms` 2000 ms. App sends every 300 ms (the bound parks and re-arms each time), and the peer sends nothing. Every send completes until the TestRequest is observed at the peer, in [T, 2T); then the next send blocks, at S ≤ `tr_written_at` + 300 ms plus scheduling, and it is cut within W + 1 s of S. The competing give-up is at `tr_written_at` + T or later, and 300 ms + W + 1 s = 1.5 s < T = 2.4 s leaves 0.9 s of scheduling margin (N-10). The cell also asserts one write-timeout record, no slot-74 record, and `session_write_timeout` from the cut send, so a give-up ending cannot satisfy it | M8 (`connection_bound_wait_`'s shipping branch replaced by `await_deadline`): the slot re-arm cancels the loop's sleep, so there is no TestRequest by 2T |
| R2(a) | `alloc_guard_106_connection_bound_mallocnesia` in `tests/alloc_guard/` (Release, label `106`): a real Engine session over `system_clock_source`, under `B-544-1`'s two conditions. **Send half:** HeartBtInt 30 s, longer than the counted window, so the loop's own sleep (the pre-existing per-call fallback, `L-093-13`) never re-arms in it; after warm-up, 20 spaced sends (100 ms apart, W = 50 ms), limit off and on: zero global allocations, and no write-timeout record. Inequality: the counted window, 20 × 100 ms = 2 s after warm-up, is below H = 30 s and T = 36 s, so neither the loop's sleep nor a TestRequest falls in it; each write to the reading peer completes inside W, and a cut would show as the record. **Emit half:** a differential, because every Heartbeat cycle re-arms the loop's own sleep on that fallback: allocations per Heartbeat cycle at the head minus the base's, under identical traffic, must be 0, with the limit off and again with it on (the head's limit-on reading against the base's limit-off one; the base has no knob). Identical traffic means the same rig, HeartBtInt and cycle count on both trees, the base built on `$B` with `FIXPP_106_ALLOC_BASE` (T004), and the count read per cycle over the same window; the parameters are recorded in the evidence file (T108). The zero covers the connection bound only | M8 (the bound's wait on the fallback) and M5 (a spawn per write or emit) make the send half non-zero and the delta positive |
| FR-013a TOML refusals | `TomlHandshake106.AcceptorKey_{OutOfRange, WrongType, AtSessionScope_Refused}` (config suite); `OutOfRange` covers 0, −1 and `UINT32_MAX` + 1 | RED on the base (unknown key at root; the session-scope cell's mutant: the key accepted at session scope) |
| FR-013 | `TomlHandshake106.{SessionKey, SecondSessionDiverges, OutOfRange, WrongType}`; `OutOfRange` covers 0, −1 and `UINT32_MAX` + 1. `TomlHandshake106.DefaultTransport_MixedRoles_Accepted`: the key in `[default.transport]`, one TLS initiator and one acceptor session; the bundle loads, and the initiator's minted transport times out at the key as `SessionKey` does (N-9's tolerance; the same budgets). Its mutant: the key refused on an acceptor or plaintext session. The bundle's factory mints a transport; its `async_handshake` against a silent listener returns `transport_handshake_timeout` at key 300 ms + 2 s. Spurious arm: key absent, still pending at 2.3 s. Inequality: 2.3 s precedes `Transport::Config::tls_handshake_timeout`'s default 30 s by 27.7 s, so the default satisfies neither arm | RED on the base: `OutOfRange` and `WrongType` get no diagnostic, because the base silently ignores the key (`recognize_keys` checks only a table's own keys, none inside `[session.transport]` or `[default.transport]`, and `build_tls_factory` always uses a default `Transport::Config t_cfg{}`); `SessionKey`, `SecondSessionDiverges` and `DefaultTransport_MixedRoles_Accepted` are still pending at key + 2 s (the 30 s default). Then M7 |
| FR-013a | `EngineHandshake106.Acceptor_ClosesAtTheKey` [seam] (C-2): the `EngineConfig` built from the loaded bundle, including `EngineEstablishment::acceptor_tls_handshake_timeout`; TOML key 300 ms, `logon_timeout_ms` 10 s; the bound read at the listener: a TCP client that never handshakes sees EOF in [300 ms, 1.2 s). Spurious arm (key 60 s): still connected at 2.3 s. Inequalities: the acceptor default, 1500 ms (what M7‴ and M7′ bring back), lands 300 ms after the main window, so the default cannot satisfy it, and 900 ms covers scheduling; the spurious check at 2.3 s follows 1500 ms by 800 ms and precedes `logon_timeout_ms` 10 s by 7.7 s. Its C++ field twin is [seam] too | M7‴ (the host's copy deleted); the twin under M7′ (field ignored) |
| FR-013a default | `TomlHandshake106.AcceptorKeyAbsent_Default1500` [seam]: a bundle loaded with no `acceptor_tls_handshake_timeout_ms` key has `bundle.engine.acceptor_tls_handshake_timeout == 1500ms`, and the `EngineConfig` the host copies from it reads 1500 ms; `EngineConfig{}.acceptor_tls_handshake_timeout` and `fixpp::config::EngineEstablishment{}.acceptor_tls_handshake_timeout` are each 1500 ms. Exact equality, not a timing window: `EngineFirstFramePhaseATls.Q17_*` builds `EngineConfig` directly and accepts a close in [1400, 4500) ms, and no other cell loads the key absent | M7⁗ (`EngineEstablishment`'s default changed, to 0 ms and to 2500 ms); separately, `EngineConfig`'s default changed |
| FR-014 | `HandshakeBound106.DriveReconnect_OneAttempt`: real TLS factory, `max_attempts` 1, bound 300 ms, a listener that accepts and never reads. It returns `transport_reconnect_limit_exceeded` within 300 ms + 2 s. Inequality: 2.3 s precedes the 30 s default a dropped bound falls back to by 27.7 s, and `max_attempts` 1 leaves no backoff to compete. `AsioTlsTransportErrorPaths.HandshakeTimeoutMapsToTransportHandshakeTimeout` proves the attempt's `transport_handshake_timeout` | guard: spurious arm (bound 60 s) not returned at 2.3 s; mutant M7″ (the factory's bound replaced by 60 s) |
**Carrier sweep (Gate A loop 2 round 2, root cause #2).** Every place the change passes through, with the cell that
asserts it and the mutant that turns that cell RED. A carrier with no cell is either named in plan.md "Deferred to
tasks.md" with its reason, or marked defensive here. Re-derive at tasks time: the write classes from E-6's census and
D5's live write sites, the endings from D11's table, the keys from E-7, the refusals from D1's matrix and D2's table;
each new row found is added here.

| Carrier | Cell | Mutant |
|---|---|---|
| write class: send, Heartbeat, TestRequest, resend frame, Reject, SequenceReset, ResendRequest (Active); initiator Logon, reply Logon, `honor_peer_next_expected_` frame (pre-Active) | `WriteTimeout106.<Class>`, with its FR-017 assertions | M3′, M3″; per class M13 (its tail); M3‴ for `WriteTimeout106.Send` (`send_impl` writes through `store_then_emit`) |
| write class `logout`: the R-1 leaf | `WriteTimeout106.EndingLogout_Keeps105Limit` | M4 at that site with M9 and M9′ (alone, masked by the claim) |
| write class `logout`: `close()`'s `run_logout_phase1` | `WriteTimeout106.CloseLogout_KeepsGrace` | M4 at that site with M9 and E-3's closing term deleted (alone, masked by `closing`) |
| write class `logout`: every other `build_logout(` site (E-6's census) | deferred (plan.md) | M4 at that site with M9 and M9′ (each site writes under a claim, D11 row 3) |
| the write's start (after the gate) | `WriteTimeout106.QueuedWrite_CountsFromItsOwnStart`, `ResendWalk_ProgressNotCut` | M3 |
| ending: the give-up, decided by the bound, write blocked | `GiveUp106.{TestRequestBlocked, HeartbeatBlocked, BehindBlockedSend, RejectBlocked, HeartbeatReplyBlocked, BusinessRejectBlocked}` | RED on the base; M2 |
| ending: the give-up, emit held in its store | `GiveUp106.EmitHeldInStore_GivesUpAtG_ClosesAtGPlusB` | RED on the base; M2 |
| ending: the give-up, held with none pending | `GiveUp106.ResendHeldNoPending` | M2's second arm |
| ending: the give-up, 105's ending bound refused, the bound decides | `GiveUp106.RejectBlocked_EndingBoundSpawnFails_ClosedAtG` | M14 |
| ending: the give-up, 105's ending bound refused, the loop decides | `LivenessTiming105.GiveUp_BoundSpawnFails_StillEof` | guard: under M14 the loop's exit still closes, so this cell guards the base path only |
| ending: the give-up, `logout_disconnect_timeout_ms` 0 | `GiveUp106.ZeroLogoutTimeout_StaysOpen` | guard; an unconditional close at G |
| ending: the give-up, written TestRequest (the loop, 105) | `LivenessTiming105.*`, `HeldTestRequestCap105.*` | guards: `decide_give_up_()`'s extraction breaks them |
| ending: the write limit, Active and pre-Active | `WriteTimeout106.<Class>` | M3′, M3″ |
| ending: the establishment deadline alone | `PreActive105.SilentPeer_StillClosedAtEstablishmentDeadlineWithEvent` | M9″ (the establishment expiry's `claim_ending` always refused): no event |
| ending: the establishment deadline against the write limit | `OneEnding106.Watchdog_PreActive_NoEstablishmentRecord` | M10 |
| ending: 105's ending Logouts, `close()`, the pairs | `OneEnding106.<First>Then<Second>` | M9 with M9′ |
| ending: `close()`'s claim refusal | `OneEnding106.CloseThenWatchdog` | M9 with the closing term deleted from E-3's exit row |
| ending: the reconnect drain | `ConnectionBound106.WriteExpiryDuringDrain_NoRecord`, `DrainCancelled_RespawnsOnBlockedWrite`, `DrainCancelled_RespawnFails_ClosedAtOnce` | the drain terms deleted; the respawn deleted; the respawn's refused-spawn path deleted |
| ending: `Engine::stop()` | none: no seam sits between its steps 1 and 2 (data-model E-3) | — |
| ending: the bound's failure exits | `ConnectionBound106.{SpawnFails_AtLoop, SpawnFails_AtWrite, WaitThrows}` | the seams |
| ending: each `Disconnected` tail a cut or a close returns through (D11's census) | the `WriteTimeout106.<Class>` and `GiveUp106` cell whose cut returns through it | M13, one arm per site; a site no cell reaches is named in tasks.md |
| teardown wakes and exit terms | `ConnectionBound106.Teardown_*` | M12: each wake and term deleted |
| the bound's wake with nothing due | `ConnectionBound106.SpuriousWake_NoEnding` | a wake ends the bound |
| the respawn's `connection_bound_.running` term | none: defensive (data-model E-3) | — |
| key `write_timeout_ms`: set / absent / invalid | `TomlWriteTimeout106.Set` / `KeyAbsent_Off` / `OutOfRange`, `WrongType` | M11 / M11′ / RED on the base |
| key `tls_handshake_timeout_ms`: set / absent / invalid / `[default.transport]` on mixed roles | `TomlHandshake106.SessionKey`, `SecondSessionDiverges` / the spurious arm / `OutOfRange`, `WrongType` / `DefaultTransport_MixedRoles_Accepted` | M7 / the exact 30 s deferred (plan.md) / RED on the base / RED on the base, and the key refused on an acceptor or plaintext session |
| key `acceptor_tls_handshake_timeout_ms` and its two fields: set / absent / invalid | `EngineHandshake106.Acceptor_ClosesAtTheKey` and its C++ twin / `TomlHandshake106.AcceptorKeyAbsent_Default1500` / `AcceptorKey_{OutOfRange, WrongType, AtSessionScope_Refused}` | M7‴, M7′ / M7⁗ / RED on the base |
| the error enumerator's C mapping | `tests/capi/error_surface_test.cpp` over the CSV row [seam] | M15 (`translate` maps `session_write_timeout` to `FIXPP_ERR_CANCELLED`) |
| refusal: each identity gate, each pre-gate action (D1) | `IdentityFirst106.<Refusal>_<Gate>`, `Plain_*`, `Strict_Initiator_*` | RED on the base; M1a for `ValidateFirst_RefusedCredentials_NonMtls` |
| refusal: none, an admitted identity | `IdentityFirst106.Admitted_*` | M1, M1b, M1c |
| refusal: each D2 row, by arm | `NextNumIn106.<Role>_<Refusal>` | RED on the base; M6a, M6b |
| refusal: the commit | `NextNumIn106.CommitRefused_Disconnected` | the commit's result ignored |
| refusal: `claim_ending_`'s taken, closing and drain terms | `OneEnding106.*`, `CloseThenWatchdog`, `WriteExpiryDuringDrain_NoRecord` | M9, as above, the drain refusal deleted |

Existing cells that pin the consumed number after an S3, strict or 789 refusal change to the unconsumed value, each
named by tasks T009's census and re-pinned by T059 (plan.md P2's "census rows"; SC-007 names them as the expected
changes). Census: `git grep -nE 'next_in\(\)|next_inbound' -- tests/session/test_099_*.cpp
tests/session/test_104_*.cpp`, read for refusal endings; first candidate `W5Scan.NextNumInIsTheHeadersAndNoReset`.

**FR-007 census at `9d81082a`** (N-3). Recipe: `grep -n 'r.open()' tests/session/test_104_logon_too_low.cpp`, plus each
acceptor cell there ending at a gate refusal (`open_hydrated` hydrates only the initiator), read for a NextNumIn
assertion after the refusal. Result: `assign_failure_arm` and `volatile_store_failure_arm` take admitted paths
(`set_counters`); `Precedence_RefusedCredentials` reads no NextNumIn; `Precedence_MtlsAcceptorOffList` asserts
`next_in() == kN` after an off-list refusal, which D1 falsifies: the one update. `UnaskedResetAck104.Mtls_Unauthorised_NoReset`
is an initiator, hydrated at open: unchanged.

**"Has a cell" census at `9d81082a`.** Recipe: each bundle claim that an error path or refusal has a cell
(`grep -n -i 'has a cell\|a cell for' *.md`, and plan.md's IX §1 row) is looked up in this table. Result: the commit
refusal had none (N-4, added above) and the root key's refusals had none (`AcceptorKey_*`, added); the rest resolve.

**Testing scope (owner, 2026-11-25).** Quickstart §5 is the one list; this is its summary.
- Targeted runs only: the labels `106`, `105`, `104` and `099`, and the touched binaries, on each lane that runs them.
  That includes `linux-clang-release` for the mallocnesia cell and the targeted sanitizer runs.
- Buildless and build-scoped checks that are not test runs: the `linux-gcc-release` `-Werror` build of the touched
  targets (T170), the whole pytest suite against the module rebuilt at C-ABI 1.19 (T171, `[const §XVII.7]`) and every
  buildless Tier 1 step (T172).
- One full `ctest`, on `linux-clang-debug` (Article XVII §7's evidence).

## D10 — B129 (#724) is done

Commits `febad771` (the `windows-msvc-asan` leg's own `timeout-minutes`, raised above the debug and release legs; the
workflow comment's re-derive recipe sums every step of a cold-cache push:main asan run, the restores, Configure,
Build, the cache Save and Test, plus a margin, below the hosted runner's per-job limit) and `aea0d5a5` (the owner's
permanence ruling covers both values) carry FR-001 and FR-002. They are this branch's first commits, so this PR's own
Tier 2 runs exercise the cap (SC-006). No further work; the PR evidence records the asan leg's outcome.

## D11 — One ending claim (ruling R3; FR-017)

**Decision.** `ending_claim_` is one strand-confined `Session` member, an `std::uint8_t` enum with the values `none`,
`give_up`, `write_timeout`, `ending_logout`, `establishment_timeout` and `internal`.
- It is reset to `none` in `begin_connection_()` (its callers are `open()`, `attach_accepted_transport` and
  `install_reconnected_transport`; `conn_gen_` advances only in the last). The `Disconnected` edge
  (`record_state_transition_`) takes it as `internal` when it is free, so a decider after any ending finds it taken,
  whatever state the connection began in.
- No decider of one connection can claim after the next connection's reset: before `begin_connection_()` runs again the
  bound is joined (`close()`, the drain) or asserted gone (`install_reconnected_transport`), and the loop's and 105's
  deciders are joined the same way (data-model E-3's joiner table), so the generation term matters only across
  `install_reconnected_transport`. A path that reaches `open()` or `attach_accepted_transport` with a decider of an
  earlier connection alive is a defect the joins MUST prevent.
- `claim_ending_(who) noexcept` refuses when the claim is taken, `closing_or_stopped_()` holds (a `close()` that has
  begun owns the ending), or `reconnect_drain_in_flight_` holds (the drain owns that connection's close). Otherwise it sets the claim and returns true, with no suspension between test and set.
- The claim is taken once, at the decision. A Logout written under its decider's claim neither takes nor tests it:
  `close()`'s `run_logout_phase1`, and `inbound_logout_leaf_` for kind `r1` (the give-up's own Logout).
- The first claimant owns the one `Disconnected` and, when it is a recording decider (rows 1, 2 and 4 below), the one
  reason record; an ending Logout, `close()`, `Engine::stop()`, a reconnect drain and the bound's failure exits record none. Every later decider records
  nothing and closes nothing. FR-017 states this invariant once; C-4.6, INV-106-2 and D9 quote it.

| Decider | Where it claims, before its reason record | Its reason record |
|---|---|---|
| D3's give-up (the loop or the bound) | `decide_give_up_()`'s first statement | slot 74 |
| D5's write timeout | the bound's expiry, step 1 | E-8's record |
| 105's other ending Logouts (R-2 kinds and the refusal Logouts) | where each takes the Logout owner or enters writing, whichever comes first (`git grep -n 'logout_in_flight_ = true\|enter_ending_write_(' -- src/session/session.cpp`, less the two exclusions above; each site listed in tasks.md) | none: the base writes no record at these sites (re-derive: `git grep -n 'FIXPP_SLOG\|session_event_' -- src/session/session.cpp`, read inside each site); the reason is the Logout's Text(58) |
| the establishment deadline | the Engine pump's expiry, through `session_engine_access`, before `note_establishment_timeout` | the timeout's event and record (105 wrote them only when the session had recorded no ending of its own; the claim replaces that test). Reached only between deliveries or during a read, when no write of the Logon arm is in flight; it never races D5's expiry on one write (N-1) |
| `close()` | implicitly: once it sets `closing`, `closing_or_stopped_()` refuses every claim | none |
| `Engine::stop()` | implicitly, through `engine_stop_requested_` in `closing_or_stopped_()` | none |
| `drain_for_reconnect_` | implicitly, through `reconnect_drain_in_flight_` in `claim_ending_` | none |
| the bound's failure exits (D3) | the exit | none |

- **105's ending limit is not a claimant:** its writing-phase expiry cuts for the claimant and records nothing, as today.
- **The census of unconditional `Disconnected` writes** that a claimed ending can reach:
  `git grep -n 'record_state_transition_(fsm_state::Disconnected)' -- src/session/session.cpp`. Read each hit for "can a
  claimed ending's cut write, or its close, return here". Known positives:
  - the liveness loop's emit-failure branches, after `fire_to_admin_` and after `store_then_emit`, for both the
    TestRequest and the Heartbeat;
  - `Session::send`'s F9 block.

  Each reachable site becomes `record_disconnected_unless_already_()`; each other is named unreachable in tasks.md.
  `close()`'s own write is already conditional (`if (fsm_state_ != fsm_state::Disconnected)`), so a `close()` after an
  ending adds no entry there.
- **The one change for a lone decider (N-12).** With another ending holding the claim and the owner, the loop's give-up
  block used to write the slot-74 record, then `Disconnected` with no Logout; it now records nothing and exits, as the
  bound does.
- **Cells:** D9's `OneEnding106.*`, one per race pair.

**Alternatives rejected.** Per-decider predicates (round 0): D5 read "no `Disconnected`, no 105 cut" while D3 records at
its decision, so a watchdog deadline inside D3's budget wrote a second reason (C-3). "The first to close records the
reason" (round 0's spec): ruling D-e records at the decision, not at a close. `close()` as a recording claimant (Codex
round 2): a log record on every close, which no ruling asked for. A refused expiry that still closes (N-8): the close
would also fail the claimant's Logout queued behind the write, so `write_timeout_ms` would bound an ending Logout,
against FR-011 and C-4.5; the unbounded case stays disclosed (`L-105-f`).

## Supersession (each named in the code comment that changes it)

| Decision | Where recorded | 106 |
|---|---|---|
| 104 N-8: "the asked (strict or knob) path keeps today's order, unit before the gate" | 104 spec Clarifications, FR-010 | the gate precedes every unit (D1) |
| 104 DQ-6 / `L-104-3`: the posture, strict, SendingTime and 1137 answers before the gates, accepted as pre-existing | 104 spec DQ-6 | removed (D1) |
| 104 T8: the too-low branches run the gates themselves | 104 contract T8 | one gate site per arm (D1) |
| 105 E-4: the give-up counts T from `tr_written_at` | 105 data-model E-4 | while the emit has not returned: L + 3T (D4, ruling R1) |
| 105 C-1 row 8's reason | 105 contract C-1 | superseded; the rule stays (D4) |
| 105 T119b reading: the 3T cap needs a TestRequest pending | 105 spec Clarifications, contract C-6.2 | capped with none pending too, per the owner's text (D3) |
| 103 D5′ / 105: the liveness loop alone decides the give-up | 103 research D5′.4, 105 research D3 | the connection bound may decide while the loop is suspended (D3), under one claim (D11) |
| 536-537-538 liveness design: the loop's emits are awaited inline with no bound | `.specify/536-537-538-recovery-liveness.md` §3 | inline still, now bounded by D3 |
| 105 D-e scope: only ending Logouts are limited | 105 research D2 item 11 | non-ending writes too, opt-in (D5) |
| engine.cpp's hard-coded 1500 ms (015 FR-014) | the listener's comment | `EngineConfig::acceptor_tls_handshake_timeout` (D6) |

## Risks

- **Coroutine frame growth in the Logon arms** (104 T093). D1's precheck and D2's classification live in callees, with
  no new local across an arm's await. That is a structural rule, checked by reading each arm's locals at review against
  the base's (T050 at `H1`, T178a at the head): the paired benches' steady-state sends run no Logon arm, so they do not measure it.
- **The bound's re-arm.** A peer that sends without reading keeps moving G; each re-arm is one wait, no spawn.
- **Two branches of one wait.** The mock cells cannot see the shipping branch; the two shipping-clock cells bind it, each
  RED under M8 first.
