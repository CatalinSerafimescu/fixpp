# Research: 106 — Logon guards before any answer, write bounds, initiator handshake bound, msvc-asan cap

**Base**: `origin/main` `9d81082a`, where every reading below was made; the branch adds spec commits only. Readings name
symbols, not lines: re-derive each with `codegraph_explore` (`projectPath` = this tree) or `git grep -n '<symbol>'`.
The spec's Clarifications are owner rulings and bind every decision here. Decisions are D1 to D10; the plan's open
questions are the points the code cannot settle.

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
- **F-8. Size pins.** Recipe: `git grep -nE 'sizeof\((fixpp::)?(core::|session::)?(Session|SessionConfig|EngineConfig)\b' -- tests src include bench bindings`;
  seed it first with `git grep -n 'static_assert(sizeof(' -- tests`, which must report `tests/dictionary/ref_shape_test.cpp`.
  At plan time the first grep reported nothing. The implementer re-runs both before the first layout change.
- **F-9. A spec edge case is narrower than stated.** "A Logon whose CompIDs cannot be read … already disconnects
  silently": on a session with `validate_inbound_messages`, `validate_inbound_` runs before `interpret_logon`, so a
  Logon missing a required header field can draw the validate-first Reject today. D1 sends it only after the
  identity decision. (Reported to the orchestrator; no spec edit here.)

## D1 — The identity gates first, by construction (#702; FR-003 to FR-005, FR-007)

**Decision.** In both Logon arms, the identity decision is made at the first point where the arm would send or
change sequence state, and nothing before that point sends or changes state.

1. **Unchanged, silent, ahead of everything:** the header scan, the garbled-frame accounting, `logon_arm_superseded_`,
   `dispose_unparseable_` (silent in NotConnected and LogonSent), `establishment_frame_refused_`, and the late-parse
   close of the validate gate (`close_on_late_parse_failure_`, terminal, sends nothing). On the initiator, 104's
   arm S (the `reset_on_logout` Logout branch) already runs the gate first and is unchanged.
2. **The validate-first Reject is decided, then held back** until the identity is known. The new callee
   `logon_identity_refuses_(frame, consume)` returns the decision. It is a callee so that no new local enters the
   arms' coroutine frames (104's T093 measurement).
   - The mTLS half evaluates the gate's arms without side effects when `consume` is false.
   - The credentials half runs on the acceptor only. It calls `interpret_logon` to read 553/554. When they are
     readable, it calls `authorize_logon`. When `interpret_logon` refuses the frame, the credentials are unreadable
     and the half refuses only if a validator is installed, through the new accessor
     `CompIdAuthorizationPolicy::has_logon_validator()`. The default accept-all policy sees no change.
   - A refused identity ends the arm `Disconnected` with nothing sent. The consuming form of the mTLS gate runs there,
     so its event and the one-shot consumption are today's.
   - An admitted identity draws the Reject exactly as on the base. Where the Reject ends the arm
     (`logon_reject_disconnects_`), the consuming form runs. Where the Reject stays in the arm, nothing is consumed
     and no bound event is emitted, so the next Logon meets the full gate (F-2).
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
`session_event_peer_identity_bound` event now precedes a posture, SendingTime, strict, too-low or 1137 refusal, where
at the base the gate was not reached. A user logon validator is called for a stay-in-arm Reject's frame and again
for the next Logon. Both are disclosed (D8).

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

**Refusals after the base's advance** (re-derive: walk each arm from its `check_inbound` call to its `Active` record
and list every `co_return` that is not success-to-Active):

| Role | Refusal | Base | 106 |
|---|---|---|---|
| acceptor | mTLS gate refusal | consumed in memory | before the classification (D1) |
| acceptor | 099 S3 / 1137 Reject | consumed in memory, not in the store | unchanged NextNumIn |
| acceptor | credentials refusal | consumed in memory | before the classification (D1) |
| initiator | 099 S3 Reject (ordinary acknowledgement) | consumed in memory (`L-104-8`) | unchanged |
| initiator | `bilateral_strict` silent refusal (no 141=Y) | consumed in memory | unchanged |
| initiator | mTLS gate refusal | consumed in memory | before the classification (D1) |
| initiator | 789 too high or invalid: Logout, `Disconnected` | consumed in memory | unchanged |
| initiator | 789 below: the walk fails or is superseded | consumed in memory | unchanged |
| acceptor | a refusal after the `reset_on_logon` reset (too high: silent; S3 / 1137 Reject) | NextNumIn 1 and the store reset | unchanged: the reset ran before the refusal (residual, plan Q3) |
| initiator | a refusal after a completed asked or unasked unit (the 789 ones) | the unit's targets stand | the unit's targets stand (residual, plan Q3) |
| both | a reset unit's manager or persistent-store failure | the unit's targets | unchanged: a store failure inside an admitted reset, not a refusal |
| acceptor | the reply Logon's build, number, `toAdmin` or write failure | consumed in memory | unchanged: an internal failure after admission |
| acceptor | 789 after the reply; `onLogon` threw; persist failure | consumed in memory | unchanged: the Logon was answered with a Logon, so its number is consumed on the wire |

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

**Decision: a liveness emit bound, which decides; the loop executes.**
- When the loop starts a TestRequest or Heartbeat emit (after its replay-gate shared hold, and for a TestRequest after
  `pending_test_req_id_` and `tr_issued_at` are set), it spawns `run_liveness_emit_bound_`, joined by
  `liveness_counter_`, which `close()`, the reconnect drain and `install_reconnected_transport` already wait on or
  assert.
- The bound is not bound to `root_cancel_` (its slot holds the loop's one handler; 103 D4 rejected sharing it). Its
  wakes:
  - an RAII guard in the loop's frame, so it fires on the emit's return and on an exception out of it;
  - `close()`, `Engine::stop()` and `drain_for_reconnect_`, each of which fires it before joining. `close(graceful)`
    goes to LogoutSent, not `Disconnected`, and the drain need not pass `Disconnected` (103 D1), so the edge alone
    is not enough;
  - the `Disconnected` edge.
- The bound sleeps on `effective_clock_` to the would-be give-up instant G of D4 and re-tests on each wake.
- It exits ordinarily when the emit has returned, the session has left Active, or an inbound frame has made the
  give-up ineligible (it then re-arms to the recomputed G).
- At G with the emit still suspended, it **decides** the give-up through the helper the loop's give-up block now
  calls too, `decide_give_up_()`: the slot-74 record first, the Logout owner claim (`logout_in_flight_`), then
  `enter_ending_write_(ending_kind::r1, …)`, which starts 105's budget at the decision. It sets
  `liveness_give_up_decided_`.
- The loop tests that flag first when its emit returns, and takes the give-up's existing write path with the owner
  already claimed. If the peer read again within the budget, the Logout goes out after the frames ahead of it.
  Otherwise 105's bound cuts the write at the decision plus `logout_disconnect_timeout_ms` and closes the transport.
  The close fails the blocked emit and every write queued behind it (FR-009), the loop finds the cut, writes no
  Logout ("Logout not sent") and records `Disconnected`. One decision, one ending.
- **The resend-answer case (US3 scenario 3).** A TestRequest held behind a running resend answer with **no**
  TestRequest pending is capped at the last inbound frame + 3T, as the owner's T119b text reads ("If the TestRequest
  is still held (not yet written) when the clock is past the last inbound frame + 3T, the session gives up then").
  This supersedes 105's implementation reading, which capped only with one pending. The give-up then runs 105's held
  path: its budget starts at the decision, and its expiry closes the transport under the walk.
- **The Text when no TestRequest was written** (a blocked Heartbeat, or a held one with none pending) is plan Q2.
  The default proposed: `TestRequest unanswered`, without the TestReqID clause.

**Invariants.** Kept:
- 103 D5′'s replay-gate ordering: the emit keeps its hold, and the Logout's hold is taken by the loop as today.
- The slot-74 record before any Logout write (ruling D-e).
- One Logout owner.
- 105's ending state machine, entered at the decision.

**Changed:** "the give-up is decided by the loop" becomes "by the loop, or by its emit bound while the loop is
suspended in its own emit". The decision is still made once.

**Allocation.** One `co_spawn` per liveness emit. That is the liveness cadence (at most one per HeartBtInt or T),
not the per-message path of `[const §XV.1]`, as 105's per-ending bound. No new member is held across a per-frame
await.

**Alternatives rejected.**
- Racing the emit against G in the loop (`||`): that cancels `store_then_emit`, which takes the number back and
  records `Disconnected` (D-OUT-2), before any slot-74 record. A cancelled in-flight `async_write` need not complete
  either, so the race may never return.
- Detached emits (not awaited inline): the loop would run beside its own emits, `tr_written_at` and the TestReqID
  become shared state, two emits can queue, and 103 D5′.4's hold rules would need re-deriving. That is a larger
  change for the same result.
- A watchdog that closes without deciding a Logout: it drops R-1's Logout (`B-105-3`) and adds a second decider.
- Relying on D5's limit: the owner's ruling (b) requires the give-up to work with the limit off.

## D4 — The instant for a TestRequest whose write has not returned (spec US3 scenario 2)

**Decision.** For a TestRequest issued and not yet returned from its store and write, the give-up instant G counts
from `tr_issued_at`: `test_request_give_up_instant(last_inbound_steady_, tr_issued_at, T)`, the later of the last
inbound frame + 2T and `tr_issued_at` + T, plus the 1 ns guard, each step saturating.
- A TestRequest that was written keeps 105's `tr_written_at` (the fresh-window ruling).
- While a Heartbeat emit is blocked, no TestRequest can be issued. The one that would fall due at the last inbound
  frame + T counts as issued then, so G = last inbound + 2T + 1 ns.
- The rule lives in the emit bound only. The loop's own formula is unchanged, because the loop runs only after its
  emit has returned, so it never sees an unwritten TestRequest.

**Supersedes:** 105 data-model E-4's "tr_written_at: the instant the give-up counts T from" for the unwritten case.
It also supersedes 105 contract C-1 row 8's stated reason ("so no give-up names a TestReqID that was never written"):
a give-up the bound decides names a TestRequest whose write did not complete. Row 8's rule stays: a TestRequest that
does not fit its buffer is never pending.

**Alternative rejected:** keep `tr_written_at` and bound only the write. A TestRequest blocked forever never starts
its clock, which is the defect of #714.

## D5 — An opt-in limit on every non-ending write (#714b; FR-010 to FR-012)

**Knob.** `SessionConfig::write_timeout_ms`, `std::uint32_t`, 0 = off (the default). TOML `write_timeout_ms`, a bare
integer, with the range check of `logout_disconnect_timeout_ms` (out of `uint32` → `out_of_range`). The spec's wrong
type case is plan Q4. No C-ABI setter (owner).

**Where the clock starts.** At the `async_write` call in `live_write_serialized_`: after the `write_gate_` grant and
after ruling W's hold-back test. The wait at the gate is not counted (FR-010). 105's ending limit counts from the
Logout leaf's entry, before the gate (C-2.7). The two starts differ on purpose.

**Which writes.** Every live write except a Logout's. Every Logout fixpp writes ends a session and is governed by 105's
limit or `close()`'s grace (FR-011). The class reaches `live_write_serialized_` as a defaulted `store_then_emit`
argument that the Logout sites pass. The resend walk's frames are separate writes, each limited on its own (US4
scenario 2).

**Mechanism: one write watchdog per connection, no allocation per write.**
- With the limit on, the first live write of a connection spawns `run_write_watchdog_`, joined by a new counter that
  `close()` and the drain join and that `install_reconnected_transport` asserts is zero.
- Each write records `write_started_at_` (a `steady_now()` read) and bumps `write_epoch_`, and clears the in-flight
  mark when it returns.
- Parked, the watchdog waits on its own `steady_timer` (the wake idiom); a write that finds it parked cancels that
  timer. Timing, it waits `effective_clock_->sleep_until(start + limit)` alone, with no `||` group, and re-tests the
  epoch on waking. A write that completes early does not wake it: it finds the write returned at the deadline and
  parks.
- On a session whose writes are spaced apart, the watchdog parks between them. So each such write costs the cancel,
  the watchdog's resumption and one `sleep_until` frame from asio's recycler, not only two stores. Back-to-back
  writes never park it.
- Its teardown: it is bound to its own `asio::cancellation_signal`, never to `root_cancel_` (whose slot holds one
  handler, 103 D4's rejected alternative). `close()`, `Engine::stop()`, `drain_for_reconnect_` and the connection
  end emit it before they join the watchdog's counter. A per-coroutine signal is the alternative 103 D4 recorded as
  viable.

**Expiry.** No Logout (owner). In order:
1. One warn record, `session ended: write timed out: limit_ms={} error={}`, with `error` the value of
   `session_write_timeout`, registered in `format_registry.cpp`.
2. The cut write's epoch is marked, so its `live_write_serialized_` returns `session_write_timeout` when the close
   fails it.
3. `Disconnected` (`onLogout` once, from Active).
4. `spawn_connection_close_(live_transport_shared_())`.

Writes queued behind it fail and return `dispatch_aborted` (`B-105-8`'s rule).

**One ending.** The watchdog decides only while the session is not already ending (no `Disconnected` recorded, no
105 cut). Whichever of the watchdog, 105's ending bound and D3's decision closes first records the reason. The others
find the ending and record nothing (one condition, one observable ending).

**The error.** `session_write_timeout = 136`, appended after `wire_undefined_tag` (`[const §X.4]`), with
`error_message` and `src/capi/error.cpp`'s translation. In C it maps to the existing `FIXPP_ERR_TRANSPORT_IO`, as
`transport_write_error` does. There is no new C constant, so the occupancy file is untouched. The CSV
`tests/capi/expected_error_map.csv` gains the row.
- `FIXPP_ERR_UNKNOWN` (the precedent of slots 73 and 74) was not taken: this code is returned by a call (the cut
  `send`), where 73 and 74 are carried only by a log record.
- Recipe for every site that names the last enumerator: `git grep -n wire_undefined_tag -- include src tests tools bindings`.

**Allocation.**
- Off: none, one branch per write.
- On: one frame per connection. A spaced write re-arms one `sleep_until`, whose frame comes from asio's recycler
  (`asio_recycler_config.hpp`). The allocation cell with the limit on decides it with spaced writes (D9). If it shows
  an allocation per write, the design is not done.

**Alternatives rejected.**
- A `co_spawn` bound per write: a per-write allocation on the send path that 544 made allocation-free.
- `async_write || deadline`: the deadline's cancellation need not complete a transport write, so the race may not
  return. Closing the transport is what ends a blocked write (105 D-e).
- A `Transport::Config` write timeout: it is per factory, not per session. It would run on the transport's real
  timer, not on `effective_clock_`, and every pluggable transport (`[const §XIV]`) would need it.
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
    the listener's bound governs. This is disclosed. The C++ half already exists (F-6), so `SessionConfig` gains no
    field.
- **Acceptor.** `EngineConfig::acceptor_tls_handshake_timeout`, `std::chrono::milliseconds{1500}`, read where the
  Engine builds the listener's `accepted_transport_config`, in place of the literal.
  - TOML: a root-scope key, `acceptor_tls_handshake_timeout_ms`, mapped into `bundle.engine`, with the same range
    checks. The TOML root is the engine scope, and there is no `[engine]` table.
  - At session scope it is refused, by the precedent of `dialect_overlay` at root: tolerating it would be a silent
    no-op.
  - `EngineConfig` is `fixpp::core` and installed, so this is a public layout change (D7).
- No C-ABI setter for either (owner). QuickFIX C++ v1.16.0 fixes 10 s (`SSLSocketInitiator`,
  `getSecondsFromHandshakeStart(now) > 10`); QuickFIX/J and QuickFIX/Go have no handshake setting.

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
- `session.h` clauses "amended in C-ABI 1.19, 106": `fixpp_session_open` (the Logon refusals' answers on mTLS, and
  NextNumIn after a refusal), `fixpp_session_is_established` (the give-up with a blocked write) and
  `fixpp_session_send` (a send blocked at the give-up's close returns, `FIXPP_ERR_CANCELLED`). Start the census from
  105's sites: `git grep -n 'amended in C-ABI 1.18' include/fix/c_api/session.h`.
- MINOR pins census: `git grep -nE 'MINOR 18|1\.18' -- include tests bindings tools ci`.
- Article X §7's predicate: `gh release list --exclude-drafts` is empty at the PR head, so the MINOR bump stands.
  The result is recorded in the evidence file.
- **C++ BREAKING (public layout):** `SessionConfig` (+`write_timeout_ms`), `EngineConfig`
  (+`acceptor_tls_handshake_timeout`) and `Session` (the emit bound's and watchdog's members and counter).
- **C++ additions:** `SeqnumManager::classify_inbound` and `CompIdAuthorizationPolicy::has_logon_validator`.
- **Error enum:** `session_write_timeout` appended.

## D8 — B&L delta (the LIVE `spec/behaviors-and-limitations.md`)

- **Retired** (moved to `spec/behaviors-and-limitations-closed.md` with "closed by 106"):
  - `L-104-3`, by D1;
  - `L-104-8`, by D2.
- **Narrowed:** `L-105-f`. Non-ending writes are bounded when `write_timeout_ms` is set. The give-up no longer waits
  behind a blocked write, and the held no-pending case is capped (D3). What stays: with the limit off, a peer that
  keeps sending and never reads holds a non-ending write until `close()` or `Engine::stop()`, as QuickFIX C++'s and
  Go's default; and with `logout_disconnect_timeout_ms` 0 or no clock the ending Logouts stay unbounded.
- **New rows**, section `## 106`:
  - `B-106-1`: identity gates first (silent, no state change);
  - `B-106-2`: NextNumIn kept on every Logon refusal, with the table of D2;
  - `B-106-3`: the give-up with a write blocked, and D4's instant;
  - `B-106-4`: `write_timeout_ms`;
  - `B-106-5`: the two handshake keys;
  - `B-106-6`: surfaces (D7).
- **New limitation rows:**
  - `L-106-a`: a refusal after a reset that already ran in the arm (the acceptor's `reset_on_logon`, or a 141=Y unit)
    keeps that reset's numbers, unless plan Q3 rules otherwise;
  - `L-106-b`: the write-timeout reason is carried by the record and by the cut call's return only, as `L-105-c`;
  - `L-106-c`: the bound event's new position, and a validator called twice per connection (D1);
  - `L-106-d`: the session-level handshake key has no effect on a plaintext or acceptor session.
- **B129: `B&L delta: none`.** A CI job cap is not shipped behaviour.

## D9 — Test design (FR-016: RED against `9d81082a`, or a named mutant for pinned behaviour)

Rigs:
- `t105::DirectRig` / `rgs::Scene` (direct Session, mock clock, `BlockingWriteTransport`, both roles) for D2 to D5.
- The 104 Logon rigs of `test_104_logon_too_low.cpp` and `test_104_unasked_reset_ack.cpp` for D1 and D2.
- Real TLS sockets for D6 only: no receive-window rig anywhere.

Each mock-clock budget below is derived from the competing timeout, and each timer guard has its spurious-hit arm.

| FR | Cells (new file `tests/session/test_106_*.cpp`) | RED evidence / mutant |
|---|---|---|
| FR-003/004 | `IdentityFirst106.<Refusal>_<Gate>`: for each base pre-gate refusal (acceptor: validate-first, posture, SendingTime, strict, `reset_on_logon`, 1137; initiator: validate-first, posture, SendingTime, S3, asked 141=Y) × gate (off-list mTLS, refused credentials, unreadable credentials with a validator): zero frames written, NextNumOut/NextNumIn in memory and store as before, no reset, `Disconnected` | RED on the base: each refusal writes its frame |
| FR-005 | `IdentityFirst106.Admitted_<Refusal>`: the same refusals with an admitted identity; byte-identical answer; a stay-in-arm Reject followed by a corrected Logon reaches Active | guard: mutant M1 (the consuming gate before the stay-in-arm Reject) makes the second Logon fail closed |
| FR-006 | `NextNumIn106.<Role>_<Refusal>`, one per D2 table row marked "unchanged NextNumIn": memory and store after == before; plus `NextNumIn106.Acceptor_ResetOnLogon_S3_KeepsTheReset`, which pins the disclosed residual (NextNumIn 1, store reset) | RED on the base for S3 (both roles), strict, 789 (initiator); the residual cell is a guard (mutant: the knob's reset moved after the S3 test, which leaves the store unreset) |
| FR-007 | the 104 `Precedence_*` cells and `UnaskedResetAck104.Mtls_Unauthorised_NoReset` re-run unfiltered | expected GREEN; each cell that changes is named in tasks.md with its reason |
| FR-008 | `GiveUp106.{TestRequestBlocked, HeartbeatBlocked, BehindBlockedSend, ResendHeldNoPending, RejectBlocked}`: T = 10 s, H = 10 s, budget B = `logout_disconnect_timeout_ms` = 500 ms; open at G + B − 1 ms, closed at G + B, one slot-74 record at G, no complete Logout at the peer; a peer read released inside B gets the Logout | RED on the base (no close at G + B) except `ResendHeldNoPending`'s pending twin, a guard |
| FR-008 spurious | each `GiveUp106` cell under mutant M2 (the emit bound deleted; for `ResendHeldNoPending`, the no-pending cap deleted) | must be RED: nothing else closes by G + B (105's bound is not armed before the decision; H's next instant is behind the blocked emit) |
| FR-009 | `GiveUp106.*` assert every blocked and queued write returned an error | as FR-008 |
| US3 sc.4 | `LivenessTiming105.*`, `HeldTestRequestCap105.*`, `ReplayGate588.R588g*/R588h*` unfiltered | guards; R588h releases before 3T, so the new no-pending cap does not reach it |
| FR-010 | `WriteTimeout106.<Class>` (send, Heartbeat, TestRequest, resend frame, Reject) with L = 300 ms; the peer sends a Heartbeat every 1 s, so the give-up cannot fire; open at start + L − 1 ms, closed at start + L; the record; the cut send returns `session_write_timeout`, the queued ones `dispatch_aborted` | RED on the base (no knob) |
| FR-010 start | `WriteTimeout106.QueuedWrite_CountsFromItsOwnStart`: write A released at L − 1 ms, B then blocked, cut at B's start + L | guard: mutant M3 (the start taken at the gate) cuts at A's start + L |
| FR-010 walk | `WriteTimeout106.ResendWalk_ProgressNotCut`: each frame released at L − 1 ms; the walk completes | guard: M3 |
| FR-010 spurious | every `WriteTimeout106` cell with `write_timeout_ms` 0 | must stay open at start + L + 1 s |
| FR-011 | `WriteTimeout106.EndingLogout_Keeps105Limit`: L = 300 ms, B = 500 ms, the Logout's write blocked: closed at B, not L | guard: mutant M4 (Logouts not excluded) closes at L |
| FR-012 | `TomlWriteTimeout106.{Set, OutOfRange, WrongType}` in the config suite | RED on the base (unknown key) |
| alloc | `WriteTimeout106.On_NoAllocationPerWrite`: the 544 counting allocator over N sends after warm-up, 0; the sends are spaced, with a mock-clock advance between each, so the watchdog parks and re-arms each time (back-to-back sends never park it, so they could not show the cost) | guard: mutant M5 (a spawn per write) |
| FR-013 | `TomlHandshake106.{SessionKey, SecondSessionDiverges, OutOfRange, WrongType}` | RED on the base |
| FR-013a | `EngineHandshake106.Acceptor_ClosesAtTheKey`: key 300 ms, `logon_timeout_ms` 10 s (the competing deadline from accept); a TCP client that never handshakes sees EOF within 300 ms + 2 s | RED on the base for the TOML key; the spurious arm (key 60 s) must still be connected at 2.3 s, which is under 10 s, so the establishment deadline cannot give the hit |
| FR-014 | `HandshakeBound106.DriveReconnect_OneAttempt`: real TLS factory, `max_attempts` 1, bound 300 ms, a listener that accepts and never reads; returns `transport_handshake_timeout` within 300 ms + 2 s (`connect_timeout` 30 s and the establishment deadline, which starts after the return, do not compete) | guard of existing behaviour: the spurious arm (bound 60 s) must not have returned at 2.3 s |

Existing cells that change: rows of the 099 W5 suites and the 104 suites that assert `next_in()` after an S3,
strict or 789 refusal pin the consumed number. Census: `git grep -nE 'next_in\(\)|next_inbound' -- tests/session/test_099_*.cpp tests/session/test_104_*.cpp`,
read for refusal endings (first candidate: `W5Scan.NextNumInIsTheHeadersAndNoReset`'s refusal rows). Each one
changes to the unconsumed value and is named in tasks.md.

## D10 — B129 (#724) is done

Commits `febad771` (the `windows-msvc-asan` leg's own `timeout-minutes`, raised above the debug and release legs; the
workflow comment's re-derive recipe sums every step of a cold-cache push:main asan run, the restores, Configure,
Build, the cache Save and Test, plus a margin, below the hosted runner's per-job limit) and `aea0d5a5` (the owner's
permanence ruling covers both values) carry FR-001 and FR-002. They are this branch's first commits, so this PR's own
Tier 2 runs exercise the cap (SC-006). No further work; the PR evidence records the asan leg's outcome.

## Supersession (each named in the code comment that changes it)

| Decision | Where recorded | 106 |
|---|---|---|
| 104 N-8: "the asked (strict or knob) path keeps today's order, unit before the gate" | 104 spec Clarifications, FR-010 | the gate precedes every unit (D1) |
| 104 DQ-6 / `L-104-3`: the posture, strict, SendingTime and 1137 answers before the gates, accepted as pre-existing | 104 spec DQ-6 | removed (D1) |
| 104 T8: the too-low branches run the gates themselves | 104 contract T8 | one gate site per arm (D1) |
| 105 E-4: the give-up counts T from `tr_written_at` | 105 data-model E-4 | unwritten: `tr_issued_at` (D4) |
| 105 C-1 row 8's reason | 105 contract C-1 | superseded; the rule stays (D4) |
| 105 T119b reading: the 3T cap needs a TestRequest pending | 105 spec Clarifications, contract C-6.2 | capped with none pending too, per the owner's text (D3) |
| 103 D5′ / 105: the liveness loop alone decides the give-up | 103 research D5′.4, 105 research D3 | the emit bound may decide while the loop is suspended (D3) |
| 536-537-538 liveness design: the loop's emits are awaited inline with no bound | `.specify/536-537-538-recovery-liveness.md` §3 | inline still, now bounded by D3 |
| 105 D-e scope: only ending Logouts are limited | 105 research D2 item 11 | non-ending writes too, opt-in (D5) |
| engine.cpp's hard-coded 1500 ms (015 FR-014) | the listener's comment | `EngineConfig::acceptor_tls_handshake_timeout` (D6) |

## Risks

- **Coroutine frame growth in the Logon arms** (104 T093's send-bench slowdown): D1's precheck and D2's classification
  live in callees, and no new local is held across an arm's await. Measured by the paired `engine_send_bench`, and
  by `session_send_bench` locally.
- **The emit bound's re-arm.** A peer that sends without reading keeps moving G, so the bound may re-arm many times
  over one blocked emit. Each re-arm is a clock wait in the existing frame, with no new spawn.
- **Two deciders** (D3) on one strand: the flag is set and read with no suspension between the test and the claim.
