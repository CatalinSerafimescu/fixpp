# Contract: 106 — what a peer, an operator and a caller observe

Observable rules only. The mechanisms are in [../data-model.md](../data-model.md), and the decisions in
[../research.md](../research.md) D1 to D11. "Base" means `origin/main` `9d81082a`. Each rule names the FR it serves.

## C-1. An unauthorised identity is disconnected without a message (FR-003, FR-004, FR-005)

- **C-1.1 The gates.** The identity gates are:
  - the mTLS CompID-authorisation gate, on a session with `mtls_ca` or `mtls_pinned`, in both roles;
  - the acceptor's credentials gate (`authorize_logon` over SenderCompID, Username(553) and Password(554)).
  On a session with neither (no mTLS, the default accept-all policy), C-1 changes nothing.
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
- **C-1.3 A refused identity:** zero bytes are written after the TLS handshake, no number is consumed in either
  direction, the store is untouched, no reset is made, and the session records `Disconnected` and returns success.
  The connection closes at once (105 C-1 row 11). The mTLS refusal emits
  `session_event_compid_authorization_failed`, as on the base.
- **C-1.4 Unreadable gate inputs fail closed and silent.** On an acceptor with a logon validator installed, a Logon
  that `interpret_logon` refuses while the validate gate would answer it is refused silently. That holds for any
  `interpret_logon` refusal (a non-Logon, a CompID or BeginString mismatch, a missing or negative HeartBtInt), even when
  553/554 are readable and the mTLS identity is admitted (`B-106-1`). With the default accept-all policy, the
  validate-first Reject is sent as on the base, and an admitted mTLS identity with no validator draws it whether or not
  its CompIDs can be read.
- **C-1.4a A Reject that disconnects** is written only after the consuming gate has admitted the identity. A Reject
  that keeps the session in its arm follows the non-consuming precheck.
- **C-1.5 An admitted identity:** every refusal's frame, number and ending are byte-for-byte the base's. A
  validate-first Reject that keeps the session in its arm leaves the identity unconsumed, so a following Logon on the
  same connection is judged afresh.
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
    (`nextLogon`, before `verify`);
  - an acceptor whose reply Logon was written, after which its 789 honour, its `onLogon` or its persist fails:
    the Logon was answered, so its number is consumed;
  - an internal failure of the reply's build, number, `toAdmin` or write.
- **C-2.3** NextNumOut is unaffected by C-2: a refusal that sends consumes an outbound number, as on the base.

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
  with no complete Logout. The session ends `Disconnected` once, with `onLogout` once from Active.
- **C-3.3 The blocked writes return.** When the session ends with a write blocked:
  - the blocked write returns an error;
  - every write waiting at the write gate returns an error;
  - an application `send` returns `dispatch_aborted` (`FIXPP_ERR_CANCELLED` in C), as 105's B-105-8, unchanged;
  - none of them hangs.
- **C-3.4 The held cap without a pending TestRequest.** A TestRequest due and still held behind a resend answer
  strictly after the last inbound frame + 3T is given up then, whether or not a TestRequest is pending. With none
  pending, its Text(58) is `TestRequest unanswered`, without the TestReqID clause (Q2).
- **C-3.5 A reading peer.** No liveness instant changes: when every write completes promptly, each TestRequest is
  written at its issue and 105's instants apply unchanged (spec US3 scenario 4).

## C-4. The opt-in write limit (FR-010, FR-011, FR-012)

- **C-4.1 The knob.** `write_timeout_ms` (C++ `SessionConfig`, TOML session scalar), a `uint32` number of
  milliseconds; 0, the default, is off. There is no C setter. A C session always runs with it off.
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

  The write that timed out returns `session_write_timeout` to its caller (`FIXPP_ERR_TRANSPORT_IO` through
  `fixpp_session_send`'s mapping, unreachable from C while C-4.1 holds). The writes queued behind it return
  `dispatch_aborted`.
- **C-4.4 Progress is not cut.** A resend answer whose frames each complete within the limit runs to its end, however
  long the whole answer takes.
- **C-4.5 Ending Logouts.** A Logout's write keeps 105's limit (`logout_disconnect_timeout_ms` from the Logout's
  write start, C-2.7), and `close()`'s Logout keeps its grace. Neither is cut by `write_timeout_ms`.
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
  - A session whose value differs from the first session's gets its own factory.
  - In C++ the bound is `Transport::Config::tls_handshake_timeout` of the factory the session uses, as on the base.
  - On a plaintext or acceptor session the key loads and has no effect (`L-106-d`).
- **C-5.2 Acceptor.** `acceptor_tls_handshake_timeout_ms` at the TOML root, or `EngineConfig::acceptor_tls_handshake_timeout`
  in C++, sets the bound the Engine listener applies to every accepted TLS connection. The default is 1500 ms, as
  on the base. The loader puts the TOML value in `fixpp::config::EngineEstablishment::acceptor_tls_handshake_timeout`,
  and the host copies it into `EngineConfig` as it copies `clock`.
  - The TOML value follows C-5.1's checks.
  - At session scope the key is refused (`unknown_key` class, as `dialect_overlay` is at root).
  - A client that connects and never completes the handshake is dropped at the bound.
- **C-5.3 The bounded attempt.** A peer that accepts TCP and never handshakes ends each connect attempt at the bound:
  the transport's handshake returns `transport_handshake_timeout`. The attempt records no error of its own. With
  `max_attempts = 1`, `drive_reconnect()` returns `transport_reconnect_limit_exceeded` (unchanged) within the bound
  plus the margin. The reconnect policy decides what follows; its default (unbounded attempts) is unchanged.
- **C-5.4** No C setter for either key.

## C-6. Surfaces (FR-015, D7)

- **C-ABI 1.19, BREAKING, behaviour only** (`[const §X.7]`). There is no new symbol, constant or type.
  - `version.h`: MINOR 19, with a 1.19 history entry naming C-1, C-2 and C-3.
  - Clauses "amended in C-ABI 1.19, 106" in `fixpp_session_close` (a new `BREAKING (C-ABI 1.19, [const §X.7]; 106)`
    block: an unauthorised identity's Logon draws nothing and changes nothing; a refused Logon or acknowledgement
    keeps NextNumIn; and close's return code for a session that a now-silent refusal keeps from establishing),
    `fixpp_session_is_established` (the give-up with a write blocked) and `fixpp_session_send` (a send blocked at the
    give-up's close returns `FIXPP_ERR_CANCELLED`). `fixpp_session_open` is unchanged.
  - `session_write_timeout` maps to the existing `FIXPP_ERR_TRANSPORT_IO`.
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
| NextNumIn after a rejected Logon | unchanged | unchanged (`generateReject` exempts `MsgType_Logon`) | unchanged (Logon exemption) | not read for this plan |
| A heartbeat timer behind a blocked write | the give-up still fires (C-3) | the timer never blocks on a write | not read for this plan | the timer never blocks on a write |
| A per-write limit | opt-in, closes (C-4) | none | opt-in `SocketSynchronousWriteTimeout`: the send returns false, nothing disconnects; `MaxScheduledWriteRequests`: slow-consumer disconnect (`IoSessionResponder.send`) | none |
| Initiator TLS handshake bound | configurable, 30 s default | fixed 10 s (`SSLSocketInitiator`) | none (LogonTimeout covers it) | none |

"Not read for this plan" means that engine's source was not read for the row, so its behaviour is unknown.

## C-8. Disclosed (B&L limitation rows)

- `L-105-f`, narrowed: with the limit off, a peer that keeps sending and never reads holds a non-ending write until
  `close()` or `Engine::stop()`. QuickFIX C++'s and Go's defaults behave the same.
- `L-106-a`: a refusal after a reset that already ran in the arm keeps that reset's numbers (C-2.2).
- `L-106-b`: the write-timeout reason is carried by its record and by the cut call's return only.
- `L-106-c`: C-1.6.
- `L-106-d`: the session-level handshake key's scope.
- `L-106-e`: C-9's allocation and own-wait claims hold on `system_clock_source`; a user `clock_override` of another
  type is waited through its own `sleep_until`.
- `L-106-f` (pre-existing, not fixed by 106): before Active, with the limit off, a write blocked inside the Logon arm
  (the initiator's Logon in `open()`, the reply Logon, a 789 resend frame, a Logon-arm Reject) is not cut by
  `logon_timeout_ms`, because the Engine tests that deadline only between deliveries and while it reads.
  `write_timeout_ms` cuts it.

## C-9. Performance (Article VIII §2, §3)

- **Per outbound live write:** one test of the knob (off). With the limit on: one clock read and three stores, plus,
  when the connection bound is parked (spaced writes), one cancel and one re-arm of its own timer. On
  `system_clock_source` that allocates nothing from the global heap, under `B-544-1`'s two conditions (research D3,
  D9's mallocnesia cell).
  Measured by the paired `engine_send_bench` (a `paired` row of `bench/ci-suite.txt`, whose writes reach
  `live_write_serialized_`).
- **Per Logon:** D1's callee and D2's second lock acquisition, on the establishment path only.
- **Per liveness emit:** one cancel and one re-arm of the connection bound's timer; no spawn (D3). **Per connection:**
  one `co_spawn` of the connection bound.
- Locally, the paired `session_send_bench` (not in `ci-suite`; its sends use `transport_send`, so it guards the arms'
  frame growth and `store_then_emit`'s new argument). The method is 105's T138: merge-base release tree, path padded,
  A-B-A-B, min per tree, +5 % one-sided.
