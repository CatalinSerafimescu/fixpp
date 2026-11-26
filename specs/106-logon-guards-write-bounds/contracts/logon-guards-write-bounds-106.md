# Contract: 106 — what a peer, an operator and a caller observe

Observable rules only. The mechanisms are in [../data-model.md](../data-model.md), and the decisions in
[../research.md](../research.md) D1 to D11. "Base" means `origin/main` `9d81082a`. Each rule names the FR it serves.

## C-1. An unauthorised identity is disconnected without a message (FR-003, FR-004, FR-005)

- **C-1.1 The gates.** The identity gates are:
  - the mTLS CompID-authorisation gate, on a session with `mtls_ca` or `mtls_pinned`, in both roles;
  - the acceptor's credentials gate: `authorize_logon` over the `interpret_logon` result's Username(553) and
    Password(554), with the SenderCompID(49) that `interpret_logon` has matched to `cfg_.target_comp_id`. There is no
    second Logon reader (research D1 item 2).
  On a session with neither (no mTLS, and the default accept-all policy: no logon validator installed,
  `has_logon_validator()` false), C-1 changes nothing.
- **C-1.2 Before any answer and any sequence change.** In the acceptor's NotConnected arm and the initiator's
  LogonSent arm, the identity is judged before the first frame the arm would write and before the first sequence
  change, in memory or in the store. That covers:
  - the validate-first Reject;
  - the posture Logout;
  - the SendingTime Reject (acceptor) or Logout (initiator);
  - the strict refusal;
  - the `reset_on_logon` reset;
  - hydration;
  - the sequence check;
  - the too-low Logout;
  - 099's S3 Reject;
  - both 141=Y reset units;
  - the 789 honour.
- **C-1.3 A refused identity:** zero bytes are written after the TLS handshake (on a plaintext session, after the
  accept), no number is consumed in either direction, the store is untouched, no reset is made, and the session records
  `Disconnected` and returns success. The connection closes at once (105 C-1 row 11): the peer observes EOF, with no
  wait for a peer Logout and no 105 ending-Logout bound, because no Logout is written. The mTLS refusal emits
  `session_event_compid_authorization_failed`, as on the base. A credentials refusal and the fail-closed unreadable
  case (C-1.4) emit no event and write no record, as on the base (`logon_credentials_admitted_` emits none); this is
  disclosed in `B-106-1`. To the peer every identity refusal looks the same: no byte, then the close. No timing
  equivalence between the refusal classes is promised.
- **C-1.4 Unreadable gate inputs fail closed and silent.** On an acceptor with a logon validator installed, a Logon
  that `interpret_logon` refuses while the validate gate would answer it is refused silently. That holds for any
  `interpret_logon` refusal (a non-Logon, a CompID or BeginString mismatch, a missing or negative HeartBtInt), even when
  553/554 are readable and the mTLS identity is admitted (`B-106-1`). With the default accept-all policy, the
  validate-first Reject is sent as on the base, and an admitted mTLS identity with no validator draws it whether or not
  its CompIDs can be read.
- **C-1.4a A Reject that disconnects** is written only after the consuming gate has admitted the identity. A Reject
  that keeps the session in its arm follows the non-consuming precheck. Which one a Reject is, is decided before the send
  by `logon_reject_disconnects_`: on a FIXT session a validate-first Reject whose RefTagID is 1137, 1130 or 372
  disconnects, and every other one stays in the arm (research F-2).
- **C-1.5 An admitted identity:** every refusal's answer and ending are as the base's; NextNumIn follows FR-006 (C-2;
  spec.md Clarifications, R16). A
  validate-first Reject that keeps the session in its arm leaves the identity unconsumed, so a following Logon on the
  same connection is judged afresh. 106 does not bound how many stay-in-arm Rejects one connection draws, as on the
  base; each consumes an outbound number and calls the validator again (`L-106-c`), and the establishment deadline
  (`logon_timeout_ms`) ends such a connection.
- **C-1.6 Event order** (disclosed, `L-106-c`). For an admitted mTLS identity, `session_event_peer_identity_bound`
  is emitted before any later refusal, where on the base the gate was not reached. A logon validator may be called
  twice per connection: for a stay-in-arm Reject's frame, and for the next Logon.
- **C-1.7 A direct mTLS initiator started by `open()`** holds no peer identity, so every acknowledgement it receives
  is refused silently. On the base the same session sent the posture and SendingTime Logouts, the S3 Reject and the
  asked reset first. An engine-managed initiator, and one that called `drive_reconnect()`, authorise at connect and
  are judged again in the arm, with the same identity.

## C-2. A refused Logon keeps NextNumIn (FR-006)

- **C-2.1** A Logon (acceptor) or an acknowledgement (initiator) refused before its success path leaves NextNumIn
  unchanged in FR-006's sense (the store at its value before the frame; in memory, the value the arm classified the frame against, which is the pre-frame value on a session hydrated before it), except where a reset already ran in the arm (C-2.2). This holds whether the refusal sends a Reject, sends a
  Logout or is silent. The refusals concerned are those of research D2's table marked "unchanged":
  - S3 / 1137, on both roles;
  - the initiator's strict refusal;
  - the initiator's 789 refusals;
  - the identity gates.
- **C-2.2 Unchanged by design:**
  - a reset that already ran in the arm: the acceptor's `reset_on_logon` reset (a too-high or S3 refusal after it
    leaves NextNumIn 1 and the store reset), or a 141=Y unit's targets, whatever follows the unit. Disclosed as
    `L-106-a`. Such a reset runs only for an identity the gates admit. QuickFIX C++ resets at the same place
    (`nextLogon`, before `verify`), and so does QuickFIX/Go (`handleLogon` resets the store before
    `verifyIgnoreSeqNumTooHigh`). QuickFIX/J resets after its `verify` (`nextLogon`), so there a refusal at `verify`
    keeps the numbers;
  - an acceptor whose reply Logon was written, after which its 789 honour, its `onLogon` or its persist fails:
    the Logon was answered, so its number is consumed;
  - an internal failure of the reply's build, number, `toAdmin` or write.
- **C-2.3** NextNumOut is unaffected by C-2: a refusal that sends consumes an outbound number, as on the base. Only an
  admitted identity draws a refusal that sends (C-1.3, C-1.5), so FIX-TC 1S(c)'s reason for silence (a Reject or a
  Logout would consume a MsgSeqNum) holds for every identity the gates refuse.

## C-3. The give-up with a write blocked (FR-008, FR-009)

- **C-3.1 The instant.** At the give-up instant of data-model E-4, with the give-up eligible, the session decides the
  give-up, whether or not any write is blocked. That includes:
  - the liveness loop's own TestRequest or Heartbeat;
  - an application send ahead of it;
  - a Reject;
  - a resend answer's frame.

  While the liveness loop's own TestRequest or Heartbeat has not returned from its store and write, the instant is
  the last inbound frame + 3T + 1 ns, the held cap (ruling R1). A written TestRequest keeps 105's fresh window
  (`tr_written_at`).
- **C-3.2 What it does** (with `logout_disconnect_timeout_ms` > 0 and a clock; with 0 or no clock the ending stays
  unbounded, `L-105-f`). First the ending claim (C-4.6), then 105's slot-74 record. Then the Logout owner, and the Logout of 105 C-3.1 under 105's
  write limit (C-2.7), whose budget counts from the decision. The Logout is written once the writes ahead of it have
  completed. When the budget runs out first, the transport closes at the decision plus `logout_disconnect_timeout_ms`,
  with no complete Logout. The session ends `Disconnected` once, with `onLogout` once from Active. When 105's ending
  bound cannot be spawned (its clarification D-g), the session records `Disconnected` and closes the transport at the
  decision itself, and writes no Logout. Whichever decides, the loop or the connection bound, the frames are 105's: the
  same Logout and Text(58), after the writes ahead of it. Only the instant (C-3.1) and the decider differ.
- **C-3.3 The blocked writes return.** When the session ends with a write blocked:
  - the blocked write returns an error;
  - every write waiting at the write gate returns an error;
  - an application `send` returns `dispatch_aborted` (`FIXPP_ERR_CANCELLED` in C), as 105's B-105-8, unchanged;
  - none of them hangs.

  The exception is the liveness loop's own emit while it is still inside its `MessageStore::store` call: the close
  does not fail it, and it returns when the store returns, which no peer controls. The give-up is still decided at the
  instant and the transport still closes at the decision plus `logout_disconnect_timeout_ms`; `close()`'s and
  `Engine::stop()`'s joins wait for the store (`L-093-12`, amended).
- **C-3.4 The held cap without a pending TestRequest.** A TestRequest due and still held behind a resend answer
  strictly after the last inbound frame + 3T is given up then, whether or not a TestRequest is pending. With none
  pending, its Text(58) is `TestRequest unanswered`, without the TestReqID clause (Q2).
- **C-3.5 A reading peer.** No liveness instant changes: when every write completes promptly, each TestRequest is
  written at its issue and 105's instants apply unchanged (spec US3 scenario 4, FR-008). The one exception is C-3.4,
  which applies only while a resend answer holds a due TestRequest past L + 3T.

## C-4. The opt-in write limit (FR-010, FR-011, FR-012)

- **C-4.1 The knob.** `write_timeout_ms` (C++ `SessionConfig`, TOML session scalar), a `uint32` number of
  milliseconds; 0, the default, is off. There is no C setter. A C session always runs with it off: every C session is
  built by `fixpp_session_open` from a C configuration, which has no setter for it, and no C entry point loads TOML
  (research F-7).
- **C-4.2 What it limits.** Every write fixpp makes on the live transport except a Logout's:
  - an application send;
  - a Heartbeat;
  - a TestRequest;
  - a Reject;
  - a resend answer's frame, each frame on its own;
  - a Logon or reply Logon;
  - a SequenceReset;
  - a ResendRequest.

  The clock starts when the write is handed to the transport. The wait at the session's write gate does not count.
- **C-4.3 Expiry.** When a write has not completed within the limit, and no other ending holds the claim (C-4.6),
  fixpp attempts no Logout. With the claim held elsewhere, the expiry does nothing. In order:
  - it writes one warn record, `session ended: write timed out: limit_ms=<W> error=<n>`, with `<n>` the value of
    `session_write_timeout`;
  - the session records `Disconnected` (`onLogout` once from Active; before Active, no `onLogout`);
  - the transport is closed at once.

  The write that timed out returns `session_write_timeout` to its caller. Its C value is `FIXPP_ERR_TRANSPORT_IO`,
  through `translate` in `src/capi/error.cpp`, the one map `fixpp_session_send` applies before its consumer-minor
  downgrade. The writes queued behind it return `dispatch_aborted` (`FIXPP_ERR_CANCELLED`). Neither is reachable from C
  while C-4.1 holds.

  The peer recovers as from any transport loss: at its next Logon the sequence check and a ResendRequest for the gap
  recover what it missed (`[FIX-SL §4.3.11]`). That is `[const §XV.15]`'s `disconnect-and-recover` mode; no frame is
  dropped.
- **C-4.4 Progress is not cut.** A resend answer whose frames each complete within the limit runs to its end, however
  long the whole answer takes. Progress means that each frame's write completes within the limit: a single frame whose
  write takes longer than the limit is cut, however steadily its bytes move.
- **C-4.5 Ending Logouts.** A Logout's write keeps 105's limit (`logout_disconnect_timeout_ms` from the Logout's
  write start, C-2.7), and `close()`'s Logout keeps its grace. Neither is cut by `write_timeout_ms`. The two starts
  differ on purpose (research D5): 105's counts from the Logout leaf's entry, before the write gate; this limit's
  (C-4.2) counts from the `async_write` call, after the gate.
- **C-4.6 One ending** (FR-017, quoted). Every ending MUST have exactly one `Disconnected` entry, and `onLogout` MUST fire at most once, whichever of the give-up, the write limit, 105's ending Logouts, the establishment deadline, `close()`, `Engine::stop()` and the connection bound's failure exits comes first (ruling R3). It MUST have exactly one reason record when the first claimant is the give-up, the write limit or the establishment deadline, and none when an ending Logout, `close()`, `Engine::stop()`, a reconnect drain or a bound failure exit comes first. An ending Logout's reason is its Logout's Text(58) on the wire; the base writes no record at those sites, and 106 adds none. The claim is one per connection; a later decider records nothing and
  closes nothing, and 105's ending limit cuts for its claimant and records nothing.
- **C-4.7 TOML.** `write_timeout_ms` in a `[[session]]` or `[default]` table:
  - an integer in `[0, UINT32_MAX]` loads;
  - an integer outside that range is `out_of_range`;
  - another type is `malformed_value`.

## C-5. The TLS handshake bounds (FR-013, FR-013a, FR-014)

- **C-5.1 Initiator.** `tls_handshake_timeout_ms` in `[session.transport]` (or `[default.transport]`) sets the
  handshake bound of that session's TLS connects. Absent, the bound is 30 000 ms, as on the base.
  - The value is an integer in `(0, UINT32_MAX]`. Out of range is `out_of_range`, another type is `malformed_value`.
  - A session whose value differs from the first session's gets its own factory. The comparison is with session[0]'s
    value only, as for the minting loop's other divergence terms: each later session whose value differs gets a factory
    of its own, even when two such values are equal.
  - In C++ the bound is `Transport::Config::tls_handshake_timeout` of the factory the session uses, as on the base.
  - On a plaintext or acceptor session the key loads and has no effect (`L-106-d`). It is tolerated there because
    `[default.transport]` is shared by both roles, so a refusal would fail every mixed-role bundle that sets it (N-9,
    R10).
- **C-5.2 Acceptor.** `acceptor_tls_handshake_timeout_ms` at the TOML root, or `EngineConfig::acceptor_tls_handshake_timeout`
  in C++, sets the bound the Engine listener applies to every accepted TLS connection. The default is 1500 ms, as
  on the base. The loader puts the TOML value in `fixpp::config::EngineEstablishment::acceptor_tls_handshake_timeout`,
  and the host copies it into `EngineConfig` as it copies `clock`.
  - The TOML value follows C-5.1's checks.
  - At session scope the key is refused (`unknown_key` class, as `dialect_overlay` is at root): tolerated there, it
    would be a silent no-op.
  - A client that connects and never completes the handshake is dropped at the bound.
- **C-5.3 The bounded attempt.** A peer that accepts TCP and never handshakes ends each connect attempt at the bound:
  the transport's handshake returns `transport_handshake_timeout`. The attempt records no error of its own. With
  `max_attempts = 1`, `drive_reconnect()` returns `transport_reconnect_limit_exceeded` (unchanged) within the bound
  plus the margin. The reconnect policy decides what follows; its default (unbounded attempts) is unchanged.
  - Each bound counts from the start of the TLS handshake on its connection: the transport arms its timer in
    `async_handshake`, after the TCP connect or accept. The establishment deadline does not cover the initiator's
    handshake: the Engine computes it once `drive_reconnect()` has returned, after the handshake and the Logon
    (`src/session/engine.cpp`). On the acceptor it is computed when the listener's accept returns (`run_accept_loop`).
  - Observability, as on the base: the attempt records no error of its own, and `drive_reconnect()` returns
    `transport_reconnect_limit_exceeded` for every failed attempt, so an operator cannot tell a handshake timeout from
    another connect failure through that return. The transport's own `transport_handshake_timeout` is the only
    observable; 106 adds no record.
- **C-5.4** No C setter for either key. A C caller keeps the base's bounds: the C engine builds a default
  `EngineConfig` (`src/capi/engine.cpp`), so its acceptor's bound is 1500 ms and its initiator factories' 30 000 ms.

## C-6. Surfaces (FR-015, D7)

- **C-ABI 1.19, BREAKING, behaviour only** (`[const §X.7]`). There is no new symbol, constant or type. The C ABI
  holds `Session` and `Engine` only behind opaque handles (`fixpp_session_t`, `fixpp_engine_t`), so the C++ layout
  changes below reach no C-visible type. Each amended symbol's `[const §X.5]` reentrancy contract is unchanged: the
  connection bound runs on the session's domain and adds no C entry point and no lock a C call waits on.
  - `version.h`: MINOR 19, with a 1.19 history entry naming C-1, C-2 and C-3, and the C++/TOML-only write limit and
    handshake keys, unreachable from C (C-4.1, C-5.4). C-1 is named as C++ and TOML behaviour with no reach from C
    (the no-reach clause below). Research D7 and T148 give the same four items.
  - Clauses "amended in C-ABI 1.19, 106" in `fixpp_session_close` (a new `BREAKING (C-ABI 1.19, [const §X.7]; 106)`
    block: a refused Logon or acknowledgement keeps NextNumIn; and the no-reach clause below, for an unauthorised
    identity's Logon, which draws nothing and changes nothing in C++ and TOML, and for close's return code, which is
    the base's on every C session),
    `fixpp_session_is_established` (the give-up with a write blocked: it reads false from the `onLogout` that the
    `Disconnected` record fires, since the C slot's `established` flag follows the trampoline's `onLogon` and
    `onLogout`; that record follows 105's cut at G + B at the latest, where on the base it stayed true while the write hung; C-3.3's
    store-phase exception aside) and `fixpp_session_send` (a send blocked at the give-up's close returns
    `FIXPP_ERR_CANCELLED`: the value is `B-105-8`'s, unchanged, and the clause is owed because on the base that send
    hung, a call that "could already" be made returning "a result other than the one documented", `[const §X.7]`).
    `fixpp_session_open` is unchanged.
  - `session_write_timeout` maps to the existing `FIXPP_ERR_TRANSPORT_IO`, through `translate` (C-4.3).
  - **No reach from C: the identity gates, and close's return code** (`/speckit-analyze` re-run, finding E1, ruled
    unreachable). No identity gate (C-1.1) can refuse a C session's Logon that the base let establish:
    - A C acceptor's credentials half always admits, because no C setter installs a logon validator and the default
      `authorize_logon` accepts.
    - `FIXPP_SECURITY_TLS` maps to `mtls_ca` (`fixpp_session_config_set_security`), and no C setter adds a
      `CompIdAuthorizationPolicy` binding. The empty policy is default-deny (`CompIdAuthorizationPolicy::authorize`),
      so that identity is refused on the base as it is at 106, and the base's stay-in-arm Reject followed by a Logon
      that establishes cannot happen.
    So `fixpp_session_close`'s code, computed from `Session::close`'s `session_already_closed` and the slot's
    `ever_established` latch (`src/capi/session.cpp`, `fixpp_session_close`), is the base's for every C session.
    The `session.h` block and the `version.h` entry state the identity item as C++ and TOML behaviour with no reach
    from C, in the form of the STRICT clause's "no reach as shipped" in `session.h`. The clause holds while
    `git grep -n -e compid_authorization_policy -e set_logon_validator -- src/capi include/fix/c_api` finds nothing.
    Its known positive is the same grep over `src/config`, which finds the TOML mapper. A C setter for either one
    invalidates the clause, and it then owes a C-ABI cell: a silent refusal, then close returning
    `FIXPP_ERR_THREAD_SESSION_LIFECYCLE`, RED on the base. There is no cell now. On the base and at 106 alike, every
    C session that reaches a refusing gate never establishes, so such a cell could not read RED on the base.
- **C++ BREAKING, layout:** `Session`, `SessionConfig` (+`write_timeout_ms`), `fixpp::core::EngineConfig` and
  `fixpp::config::EngineEstablishment` (each +`acceptor_tls_handshake_timeout`).
- **C++ additive:**
  - `SeqnumManager::classify_inbound`;
  - `CompIdAuthorizationPolicy::has_logon_validator`;
  - `core::error::session_write_timeout` (136).

## C-7. Against QuickFIX (C++ v1.16.0, QuickFIX/J 3.0.2, QuickFIX/Go v0.9.12)

| Behaviour | fixpp 106 | QuickFIX C++ | QuickFIX/J | QuickFIX/Go |
|---|---|---|---|---|
| An unknown or unauthorised identity's Logon | no message, disconnect (FIX-TC 1S(c)) | no message, disconnect | no message, no close | no message, disconnect |
| NextNumIn after a rejected Logon | unchanged | unchanged (`generateReject` exempts `MsgType_Logon`; in `nextLogon` the 789-too-high Logout and every `disconnect()` return precede the increment) | unchanged (Logon exemption) | unchanged (`handleLogon` increments only at its end, `IncrNextTargetMsgSeqNum`, after every refusal's return) |
| A heartbeat timer behind a blocked write | the give-up still fires (C-3) | the timer never blocks on a write | the timer never blocks on a write by default: MINA writes are queued (`IoSessionResponder.send`); with `SocketSynchronousWrites` a send waits at most `SocketSynchronousWriteTimeout` | the timer never blocks on a write |
| The give-up's ending (a TestRequest unanswered) | a Logout with Text(58), then the close (105 R-1, `B-105-3`; Q2 for the Text with none pending) | no Logout: `disconnect()` at `SessionState::timedOut` (2.4 × HeartBtInt of inbound silence) | no Logout: `disconnect("Timed out waiting for heartbeat")` | no Logout: `pendingTimeout` → `latentState` (two 1.2 × HeartBtInt peer timers) |
| A per-write limit | opt-in, closes (C-4) | none | opt-in `SocketSynchronousWriteTimeout`: the send returns false, nothing disconnects; `MaxScheduledWriteRequests`: slow-consumer disconnect (`IoSessionResponder.send`) | none |
| Initiator TLS handshake bound | configurable, 30 s default | fixed 10 s (`SSLSocketInitiator`) | none (LogonTimeout covers it) | none |

Notes on the rows:
- Row 1: fixpp closes where QuickFIX/J leaves the connection open, deliberately: FIX-TC 1S(c) asks to "Disconnect
  without sending a message", and `[FIX-SL §4.6.4]` to terminate the transport layer connection. `B-106-1` carries the
  same reason (T154 copies this table).
- Row 3: QuickFIX C++ and Go cut a silent peer at their own heartbeat timeout counted from the last inbound frame;
  fixpp's instant is data-model E-4's G (L + 3T + 1 ns while an emit has not returned, otherwise 105's).
- Row 5: fixpp's knob closes, as QuickFIX/J's `MaxScheduledWriteRequests` does and its `SocketSynchronousWriteTimeout`
  does not, because the owner's ruling (b) asks that the write limit end the session (spec Clarifications,
  2026-11-25).
- Row 6: the 30 s default is the base's, a departure from QuickFIX C++'s fixed 10 s, now configurable (C-5.1).
  QuickFIX/J's LogonTimeout covers its handshake; fixpp's `logon_timeout_ms` does not cover the initiator's (C-5.3), so
  the handshake bound is the only bound on it.

Every engine cell above names the source read (versions in the heading).

## C-8. Disclosed (B&L limitation rows)

- `L-105-f`, narrowed: with the limit off, a peer that keeps sending and never reads holds a non-ending write until
  `close()` or `Engine::stop()`. QuickFIX C++'s and Go's defaults behave the same. A C session always runs with the
  limit off (C-4.1), so for a C caller this holds on every session: the knob is C++ and TOML only (owner ruling,
  `[const §IV]`).
- `L-106-a`: a refusal after a reset that already ran in the arm keeps that reset's numbers (C-2.2).
- `L-106-b`: the write-timeout reason is carried by its record and by the cut call's return only.
- `L-106-c`: C-1.6.
- `L-106-d`: the session-level handshake key's scope.
- `L-106-e`: C-9's allocation and own-wait claims hold on `system_clock_source`. When the session's effective clock (`SessionConfig::clock_override` when set, else `EngineConfig::clock`) is of another type, the bound waits through its `sleep_until` in an `||` group, once per spaced write with the limit on, and once per liveness-emit edge with the limit on or off. No zero is claimed there. `B-544-1` carries the send half as its third condition; `L-497-1` (6) carries both halves (ruling R14 and its step 9 scope). On such a clock the own-wait claim is the clock's to keep: a user clock whose `sleep_until` keeps one timer per Session (the shipping clock's slot design, fixpp#715) would let the bound's wait re-arm the liveness loop's sleep, the hazard M8 shows on the shipping clock. 106 does not exclude it there; a user clock must serve concurrent sleepers of one Session independently, as `mock_clock` does.
- `L-106-f` (pre-existing, not fixed by 106): before Active, with the limit off, a write blocked inside the Logon arm
  (the initiator's Logon in `open()`, the reply Logon, a 789 resend frame, a Logon-arm Reject) is not cut by
  `logon_timeout_ms`, because the Engine tests that deadline only between deliveries and while it reads.
  `write_timeout_ms` is the one bound on such a write; with the limit off only `close()` or `Engine::stop()` ends it.

## C-9. Performance (Article VIII §2, §3)

- **Per outbound live write:** one test of the knob (off). With the limit on: one clock read and three stores, plus,
  when the connection bound is parked (spaced writes), one cancel and one re-arm of its own timer. On
  `system_clock_source` that allocates nothing from the global heap, under `B-544-1`'s two conditions (research D3,
  D9's mallocnesia cell).
  No bench times it (ruling R19): `engine_send_bench`'s timed sends are refused before any write (its reach checks
  require no write after the first, untimed send), and `session_send_bench`'s sends go to `transport_send`, so none
  reaches `live_write_serialized_`. The limit-on cost is bounded by the mallocnesia cell's zero; the limit-off branch is
  untimed.
- **Per Logon:** D1's callee and D2's second lock acquisition, on the establishment path only. No bench runs a Logon
  arm, so this cost is not timed; the arms' locals are read against the base's at review (research Risks, "Coroutine
  frame growth in the Logon arms"; T050, T178a).
- **Per liveness emit:** one cancel and one re-arm of the connection bound's timer; no spawn (D3). **Per connection:**
  one `co_spawn` of the connection bound.
- Locally, the paired `session_send_bench` (not in `ci-suite`; its sends use `transport_send`, so it guards
  `store_then_emit`'s new argument) and `on_inbound_frame_bench` (dispatcher frame growth: it times the inbound dispatcher whose
  Logon arms D1 and D2 edit; its steady-state frames run no Logon arm, so it measures the dispatcher, not the arms). The method is 105's T138: merge-base release tree, path padded, A-B-A-B, min per tree, +5 % one-sided.
- No bench row sets `write_timeout_ms`: the limit is off by default and the paired budget governs the default path. The
  limit-on cost above is bounded by the mallocnesia cell's zero and is not timed.
