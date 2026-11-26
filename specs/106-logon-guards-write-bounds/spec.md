# Feature Specification: Logon guards before any answer, a time limit on every write, a configurable initiator handshake bound, and the msvc-asan job cap

**Feature Branch**: `106-logon-guards-write-bounds`

**Created**: 2026-11-25

**Status**: Draft

**Input**: User description: "Feature 106: lane S batches S2 (B110: #702, #703), S2a (B123: #717) and S2b (B124: #714), plus B129 (#724), bundled into one PR (owner 2026-11-25), ordered by blocking."

## Context & Problem

The batches come from `phases/phase-4/issue-batches.md` in the parent repo: lane S (S1 = B27 merged as PR #720), then S2, S2a and S2b, with B129 as the next CI item (owner, 2026-11-25: "the next PR, before S2"). Every premise below was re-derived on `origin/main` `9d81082a`. 104 and 105 rewrote the Logon, reset, Logout and liveness paths, and three of the four issue texts did not survive the re-derivation unchanged. The differences are recorded here so that Gate A ratifies the problem the code has, not the one the issue described.

### B129 (#724): the Windows asan job's cap

Tier 2's `windows` matrix job carries one hung-job backstop shared by its three legs. On PR #720 the `windows-msvc-asan` leg was cancelled at the cap, with every finished test passing. The re-derive recipe in the workflow comment sums only the Build and Test steps. It leaves out the cache restores, Configure, and the push:main cache-save step, so a cold-cache run exceeds the cap while the recipe still says it fits. 106 itself changes headers that every translation unit includes, so its own asan leg builds cold and exercises the new cap.

### B110 (#702, #703): what a Logon can cause before the identity gates

The identity gates are the mTLS CompID-authorisation gate and the acceptor's credentials gate (`authorize_logon`). Neither sends anything when it refuses. FIX Test Cases scenario 1S(c) (`[FIX-TC §4 scenario 1S]`, section 4.4.1 of the June 2020 document) asks that a Logon from an unauthenticated or non-configured identity be answered by "Disconnect without sending a message", because a Reject or a Logout would consume a MsgSeqNum. QuickFIX C++ and Go answer an unknown session's Logon with no message, and QuickFIX/J with no message and no close.

At `9d81082a`, these answer or change state before the gates:
- **mTLS acceptor:**
  - the validate-first Reject;
  - the posture Logout;
  - the SendingTime Reject;
  - the strict Logout;
  - a durable `reset_on_logon` reset;
  - the inbound sequence advance;
  - the 1137 Reject, which sits between the mTLS gate and the credentials gate.
- **Direct mTLS initiator (started by `open()`):**
  - the validate-first Reject;
  - the posture Logout;
  - the SendingTime Logout;
  - the inbound sequence advance (Guard (4)'s `check_inbound`);
  - the `bilateral_strict` silent refusal, which follows that advance;
  - the S3 1137 Reject;
  - the asked 141=Y reset unit.

#702 listed only part of this. The durable reset is the most serious item, and #702 did not name it: a peer whose certificate validates but is not authorised for the CompID can reset a legitimate session's sequence state. An engine-managed initiator, and a direct `drive_reconnect()`, authorise the peer at connect, before any Logon is sent, so they do not reach this.

#703: on both roles, the inbound sequence advance runs before 099's S3 DefaultApplVerID/ApplVerID test. A Logon (acceptor) or a Logon acknowledgement (initiator) refused there is consumed in memory but not in the store, because the persist runs only on the success path. QuickFIX C++ v1.16.0 and QuickFIX/J 3.0.2 exempt a Logon from the Reject path's increment.

### B123 (#717): the initiator's TLS handshake bound

#717 said the initiator's handshake is not bounded. That is refuted. The client transport arms a handshake timer from `Transport::Config::tls_handshake_timeout` (default 30 s) in both roles, and an existing transport test drives the exact case: a TCP peer that never speaks TLS ends with `transport_handshake_timeout`. 105's observation, no return within 50 s, fits one 30 s attempt plus the default 30 s reconnect interval with unbounded attempts, which is QuickFIX's reconnect-forever behaviour.

What remains is configuration. Every factory built from TOML, and every test rig, uses a default `Transport::Config`, so an operator cannot set the initiator's handshake bound. QuickFIX C++ hard-codes 10 s, QuickFIX/J has no handshake setting (its LogonTimeout covers it), and QuickFIX/Go has none.

### B124 (#714): a write that does not end the session

105 limits every ending Logout write (ruling D-e). Every other write waits as long as the transport lets it. All writes are serialised behind one write lock, and the liveness loop awaits its own Heartbeat and TestRequest writes through that lock. So a peer that stops reading while an application write is blocked stops the liveness loop too: the TestRequest give-up never runs, and the session hangs.

#714 said QuickFIX has no precedent; that is refuted.
- QuickFIX C++ and Go never block their heartbeat timer on a write, so a silent peer is cut at the heartbeat timeout.
- QuickFIX/J has an opt-in `SocketSynchronousWriteTimeout` (off by default; the send returns false and nothing disconnects) and `MaxScheduledWriteRequests` (a slow-consumer disconnect).

## Clarifications

### Session 2026-11-25 (owner rulings before the spec)

- Q: Scope of the PR? → A: B129 (#724) + S2 (B110: #702, #703) + S2a (B123: #717) + S2b (B124: #714), in one PR. B129 is the first commit, because Tier 2 runs in `pull_request` context and the raised cap then covers this PR's own runs.
- Q: #717, refuted as filed — what does 106 do? → A: a session-level cell proving the bound, **and** a configurable initiator handshake bound. #717 closes with both.
- Q: #714 design? → A: option (b). The liveness give-up and its close never wait behind a blocked write (QuickFIX-faithful), **plus** an opt-in per-write time limit, off by default, whose expiry closes the transport as 105's D-e cut does.
- Q: Where is a new knob exposed? → A: C++ and TOML only, with no C-ABI setter (as `logout_disconnect_timeout_ms`).

### Session 2026-11-25 (spec-time reading, not owner rulings)

- The give-up instant counts from `tr_written_at` at `9d81082a`, so a blocked TestRequest never starts its clock. 106 counted an unwritten TestRequest from `tr_issued_at`. *Superseded by Gate A round 1, ruling R1 (below): such a TestRequest is given up at the held cap, L + 3T.*
- `authorize_logon(asserted_compid, creds)` reads SenderCompID(49) and Username(553)/Password(554). Unreadable inputs fail closed and silent (Edge Cases).
- C-ABI 1.19 BREAKING, behaviour only, by §X.7 precedent (Assumptions).

### Session 2026-11-25 (`/speckit-clarify`)

- Q: When the opt-in per-write limit expires, close at once or try a Logout first? → A: close at once, with no Logout. The session ends at the limit, and the reason is logged and observable as a write timeout.
- Q: Should every Logon or Logon-acknowledgement refusal leave NextNumIn unchanged, or only the S3 refusal? → A: every refusal (the class, as QuickFIX C++/J's Logon exemption). The plan lists each refusal that runs after the inbound advance, with a cell for each.
- Q: Does the TOML handshake-timeout setting also replace the acceptor's hard-coded 1500 ms bound? → A: both roles. A session-level key sets the initiator's bound, with today's default. An Engine-level key sets the acceptor listener's bound, with 1500 ms as its default.

### Session 2026-11-25 (Gate A round 1)

Rulings of the orchestrator; the owner delegated these design calls on 2026-11-25. Reviews:
`research/reviews/codex_106-logon-guards-write-bounds_gate_a_review.md` and
`research/reviews/opus_106-logon-guards-write-bounds_gate_a_adversarial_review.md` (parent repo).
- R1 (D4): a TestRequest whose write has not returned, and a Heartbeat emit that has not returned, are given up at the held cap L + 3T + 1 ns (L = the last inbound frame). That is the same instant as the 3T cap with none pending. A written TestRequest keeps 105's instant. This matches the owner's T119b ruling and avoids the issue-time count 105's Gate B rejected; `tr_issued_at` is not used for the give-up.
- R2 (timers): D3's give-up bound and D5's write watchdog are one per-connection bound with its own timer (research D3).
  - On the shipping clock they never call `Clock::sleep_until`, and they allocate nothing per write or per emit, with the limit off and on.
  - On any other `Clock`, such as `mock_clock`, they wait through its `sleep_until` and stay deterministic. No zero-allocation claim is made there (`L-106-e`), and the condition it adds to `B-544-1` is ruled under R14 (Session 2026-11-26, Gate A loop 2 round 3).
  - fixpp#715 stays out of scope: 106's new waits never meet the per-session slot.
- R3 (one ending): one ending claim per connection (research D11). The give-up, the write limit, 105's ending Logouts, the establishment deadline, `close()` and `Engine::stop()` race for it. The first claim owns the one `Disconnected` and, when it is a recording decider, the one reason record, and the others do nothing (FR-017; restated in round 2, R6).
- R4 (FR-014): the requirement is changed to what the code returns. The transport's handshake ends with `transport_handshake_timeout` at the bound, and `drive_reconnect()` with `max_attempts = 1` returns its unchanged `transport_reconnect_limit_exceeded` within the bound plus the margin. The reconnect FSM's final error is not changed.
- R5: every other finding the Opus review confirmed or escalated is applied: Normative References, the error a blocked send returns, the TOML cell's observable, the seam file for cells the base cannot compile, the reconciliation of Q1–Q5, SC-002's reset exception, the malformed-CompID edge case, and the B&L amendments to `B-099-2` and `B-105-3`.
- Plan Q1–Q5 (orchestrator resolutions, plan.md) are normative here:
  - Q1: `has_logon_validator()` is accepted.
  - Q2: a give-up with no TestRequest written sends Text(58) `TestRequest unanswered`, without the TestReqID clause.
  - Q3: resets that already ran in the arm are kept and disclosed (`L-106-a`).
  - Q4: a wrong-typed `write_timeout_ms` is `malformed_value`.
  - Q5: the 3T held cap applies with none pending.
- Testing scope (owner): targeted runs only, plus ONE full `ctest` on `linux-clang-debug`. The set is listed once, in quickstart §5 (with the `linux-gcc-release` build of the touched targets, the whole pytest suite and the buildless Tier 1 steps); every other restatement points there.

### Session 2026-11-26 (Gate A round 2)

Rulings of the orchestrator under the same delegation. Reviews: `research/reviews/codex_106-logon-guards-write-bounds_gate_a_2_review.md` and `research/reviews/opus_106-logon-guards-write-bounds_gate_a_2_adversarial_review.md` (parent repo).
- R6 (C-1): FR-017 is re-derived from research D11's table and stated once; C-4.6, INV-106-2 and D9 quote it. `close()`, `Engine::stop()` and the bound's failure exits own the `Disconnected` with no reason record. Codex's alternative, `close()` as a recording claimant, is not taken: it adds a log record to every close.
- R7 (N-1): the Engine tests the establishment deadline only between frames and while it reads, so it never races a blocked write. The establishment race cells are dropped. With the limit off, a write blocked before Active is not cut by `logon_timeout_ms`; that is pre-existing, disclosed as `L-106-f`, and not fixed by 106.
- R8 (C-2): the root key `acceptor_tls_handshake_timeout_ms` lands in `fixpp::config::EngineEstablishment::acceptor_tls_handshake_timeout`, which the host copies into `EngineConfig` as it copies `clock`.
- R9 (N-7): when the give-up and a write expiry are due at one re-test, the give-up claims.
- R10 (N-8, N-9): a write expiry refused by the claim closes nothing, so `write_timeout_ms` never bounds an ending Logout (FR-011). The session handshake key stays tolerated on an acceptor or plaintext session, because `[default.transport]` is shared by both roles.

### Session 2026-11-26 (Gate A Step F targeted edit)

Rulings of the orchestrator under the same delegation, applying round 3's exact text. Reviews: `research/reviews/codex_106-logon-guards-write-bounds_gate_a_3_review.md` and `research/reviews/opus_106-logon-guards-write-bounds_gate_a_3_adversarial_review.md` (parent repo).
- R11 (C-1): an ending Logout is a non-recording claimant. The base writes no record at those sites, and its reason is the Logout's Text(58) on the wire (105's D-e). FR-017, C-4.6, INV-106-2 and D11 row 3 say so. Codex's alternative, a new registered record at every ending-Logout claim, is not taken, on R6's precedent: it adds a log record to every Logon refusal and every peer-initiated Logout. The pairs whose second party never claims are guards of that party's state test, and are not counted toward M9.
- R12 (N-1): `reconnect_drain_in_flight_` is an exit term of the connection bound and a refusal of `claim_ending_`, as it already is for 105's `ending_bound_live_`. Each joiner's exit term is derived from what that joiner sets (data-model E-3's joiner table). A teardown whose join always comes after a `Disconnected` edge (`close()`, `Engine::stop()`) is reported as covered by the edge and is not counted.
- R13 (root cause #3): each timing cell states its parameters and the inequality against its competing instant (research D9). Two cells change. `ShippingClock106` runs at HeartBtInt 2 s, and `WriteTimeout106`'s TestRequest class sets `test_request_threshold` 500 ms. The acceptor handshake cell's main window narrows to EOF before 1.2 s, because the 1500 ms default fell inside 300 ms + 2 s.

### Session 2026-11-26 (Gate A loop 2 round 3)

Ruling of the orchestrator under the same delegation, applying round 6's exact text. Reviews: `research/reviews/codex_106-logon-guards-write-bounds_gate_a_6_review.md` and `research/reviews/opus_106-logon-guards-write-bounds_gate_a_6_adversarial_review.md` (parent repo).
- R14 (C-1): Q: Does R-7's reading of §XV.1 (the 544 note's conditional zero) extend to a third condition, write_timeout_ms set with a user-supplied clock (clock_override or EngineConfig::clock)? → A: yes. The write limit is opt-in and off by default, and the zero holds on the shipping clock. With the limit set and a user clock, each spaced write may allocate through that clock's sleep_until and fixpp's wait group. This is disclosed in L-106-e, which amends B-544-1's condition list and L-497-1 in the same feature. Orchestrator ruling under the owner's delegation, flagged for the owner's review at merge.
  - Scope, amended at step 9 (checklist audit, 2026-11-26, nfr CHK007), under the same delegation: R14 covers both halves `L-106-e` discloses. (1) The send half: with `write_timeout_ms` set and a user clock, each spaced `Session::send` write arms one wait of the bound; `B-544-1` carries it as its third condition and `L-497-1` as condition (6). (2) The emit half: on a user clock each liveness-emit edge arms one such wait, with the limit on or off. That path is on no path `B-544-1` gates; it is the heartbeat path `[2d §8]` extends `[const §VIII.5]` to, a zero 2d realises through `system_clock_source`'s per-session slot, where the bound builds no `||` group. So `B-544-1` names only the send half, `L-497-1` (6) and `L-106-e` name both, and plan.md's XV §1 row is the justification `[const §VIII.5]` asks for. A refusal of R14 at merge reopens Gate A on the connection bound's non-shipping wait (research D3) before the merge.

### Session 2026-11-26 (analyze)

- Owner ruling 2026-11-25: autonomous end-to-end work; local builds are pre-approved by the owner for this feature (`[const §XVII.7]` satisfied by that ruling). Scope rule (owner 2026-11-25): targeted tests only, plus ONE full ctest on linux -debug; one build at a time.

### Session 2026-11-26 (analyze re-run)

- R15 (the R14 escalation; orchestrator ruling under the owner's delegation): R14's disclosure reading holds. On a user clock the base's liveness loop already waits through `Clock::sleep_until` once per cycle (`src/session/session.cpp`, `Session::run_liveness_loop_body_`, the `effective_clock_->sleep_until(deadline)` in its `while (liveness_may_originate_())` loop), so the emit half was never a zero on a user clock. R14 records that; its emit half is not a new `[const §XV.1]` deviation. Its `Session::send` half, with `write_timeout_ms` set on a user clock, stays a new conditional zero, flagged for the owner's review at merge (T178).
- E1 (C-ABI, ruled unreachable): `fixpp_session_close`'s return code is the base's on every C session, because no C session reaches an identity gate that can refuse a Logon the base let establish (contract C-6's no-reach clause). The clause and its condition replace the `FIXPP_ERR_THREAD_SESSION_LIFECYCLE` change that C-6, research D7 and checklist c-abi CHK007 stated. No cell is owed while the condition holds.

### Session 2026-11-26 (implementation)

Rulings of the orchestrator under the owner's delegation, on the Phase 4 implementation report.
- R16 (the two `Admitted_S3` cells): FR-005, contract C-1.5 and tasks T042 govern the answer and the ending: the frames and the `Disconnected`, as on the base. FR-006, contract C-2.1 and research D2 govern the inbound number: kept. `IdentityFirst106.Admitted_S3_1137_Acceptor` and `IdentityFirst106.Admitted_S3_Initiator` therefore pin NextNumIn 5, the frame the base wrote, and the base's ending. C-1.5's and T042's wording now reads "the answer and ending are as the base's; NextNumIn follows FR-006".
- R17 (tasks T059; the owner's class ruling, every Logon refusal keeps NextNumIn, FR-006): the 099 cells that pinned a consumed NextNumIn after a refused Logon are re-pinned to kept, as research D9's second FR-007 census amendment records.

## User Scenarios & Testing *(mandatory)*

### User Story 1 — An unauthorised identity is disconnected without a message and changes nothing (Priority: P1)

An operator runs an mTLS acceptor, or a direct mTLS initiator. A peer presents a certificate that validates but is not authorised for the Logon's CompID, or an acceptor's credentials policy refuses the Logon. The peer receives nothing, no outbound MsgSeqNum is consumed, and the session's stored sequence state is untouched. This holds whatever else is wrong with the Logon.

**Why this priority**: security-affecting (Article XII §9). An unauthorised peer can today consume outbound numbers and, on the acceptor, reset an authorised session's sequence state.

**Independent Test**: for each refusal that today runs before a gate, drive a Logon that both triggers that refusal and fails the gate. Assert that no frame is written, that the outbound and inbound numbers in memory and in the store are unchanged, and that the session ends Disconnected.

**Acceptance Scenarios**:

1. **Given** an mTLS acceptor whose peer certificate is not authorised for the Logon's SenderCompID, **When** the Logon also fails the posture check, the strict check, the SendingTime check, the validate-first check or the S3 (1137) check, **Then** nothing is sent, and NextNumOut, NextNumIn and the store are unchanged.
2. **Given** that acceptor with `reset_on_logon` set, **When** the unauthorised Logon arrives, **Then** no reset takes place (in memory or durably).
3. **Given** an acceptor whose credentials policy refuses the Logon, **When** the Logon also fails a check that answers today, **Then** nothing is sent and nothing is changed.
4. **Given** a direct mTLS initiator started by `open()` whose peer is not authorised, **When** the acknowledgement also fails the posture, SendingTime, validate-first or S3 check, or asks for a 141=Y reset, **Then** nothing is sent and no reset takes place.
5. **Given** a Logon that is malformed in a way that draws a validate-first Reject today, from an unauthorised identity, **When** it arrives, **Then** the session disconnects silently.
6. **Given** an identity the gates admit, **When** any of these refusals applies, **Then** its answer is exactly as on `9d81082a`. A direct mTLS initiator started by `open()` holds no peer identity, so its mTLS gate admits none and this scenario does not arise there.
7. **Given** an unauthorised identity, **When** its Logon (acceptor) or acknowledgement (initiator) is well-formed and in sequence, with no other fault, **Then** nothing is sent and NextNumIn is unchanged in memory and in the store.

---

### User Story 2 — A refused Logon does not consume its MsgSeqNum (Priority: P1)

An acceptor's Logon, or an initiator's ordinary Logon acknowledgement (no 141=Y), is refused: at the S3 DefaultApplVerID/ApplVerID test (#703), or at any other refusal. NextNumIn stays where it was, in memory and in the store, as QuickFIX's Logon exemption does.

**Why this priority**: protocol fidelity on every FIXT session; it pairs with User Story 1's sequence-state guarantee.

**Independent Test**: refuse a Logon on each role, at S3 and at each other refusal the plan lists, then read NextNumIn from the session and from the store, and compare them as FR-006 defines "unchanged".

**Acceptance Scenarios**:

1. **Given** an acceptor with FIXT routing and inbound validation on, **When** a Logon is refused at S3, **Then** the Reject is sent (as today) and NextNumIn is unchanged in memory and in the store.
2. **Given** a direct initiator, **When** its ordinary acknowledgement is refused at S3, **Then** NextNumIn is unchanged in memory and in the store.
3. **Given** the unasked 141=Y acknowledgement refused at S3, **Then** the result is as today: NextNumIn is unchanged.
4. **Given** any other refusal of a Logon or an acknowledgement, on either role, **Then** NextNumIn is unchanged in memory and in the store, except after a reset the arm already applied (FR-006, L-106-a).

---

### User Story 3 — A peer that stops reading cannot hold the session (Priority: P1)

A peer keeps its TCP connection open, stops reading, and stops sending. The session's writes back up. The TestRequest give-up still fires at its instant, and the session ends: a Logout under 105's ending-write limit, then the close. The close fails every blocked write, apart from FR-009's store-phase exception.

**Why this priority**: peer-reachable denial (#714). Today a single blocked application write stops the liveness loop.

**Independent Test**: use a transport whose write does not complete (105's `WriteLimit105` cells use `BlockingWriteTransport`; the rig is the plan's choice, and it must not depend on real socket buffers). Block an application write, and separately a Heartbeat, a TestRequest, a resend answer and a Reject. Assert that the session ends within the give-up instant plus `logout_disconnect_timeout_ms` plus a stated scheduling margin, and that every blocked write returns an error.

**Acceptance Scenarios**:

1. **Given** an Active session whose application write is blocked, with `logout_disconnect_timeout_ms` > 0 and a clock, **When** the peer sends nothing, **Then** the session ends at the give-up instant plus at most `logout_disconnect_timeout_ms` (plus the stated margin), and the blocked write returns `dispatch_aborted` (the value of `B-105-8`, unchanged).
2. **Given** the liveness loop's own TestRequest or Heartbeat write is blocked, **Then** the give-up still fires. **This changes `9d81082a`.** There, the give-up instant counts from when the TestRequest's store and write returned (`tr_written_at`, data-model E-4 of 105), so a TestRequest whose write never returns never starts the give-up clock. In 106, a TestRequest or Heartbeat whose write has not returned is given up at the held cap, the last inbound frame + 3T + 1 ns (ruling R1; + 1 ns is 105's strictly-after guard). A TestRequest that was written keeps 105's instant.
3. **Given** a resend answer (the walk holds the replay gate) whose write is blocked, **Then** the give-up still ends the session.
4. **Given** a peer that reads normally, **Then** no liveness timing changes: the TestRequest instant and the give-up instant are as on `9d81082a`. The one exception is contract C-3.4's held cap with none pending, which applies only while a resend answer holds a due TestRequest past the last inbound frame + 3T.

---

### User Story 4 — An operator can opt in to a time limit on every write (Priority: P2)

An operator who wants a slow consumer cut sooner than the liveness give-up sets one per-write limit. A write that does not complete within it closes the transport and ends the session. With the limit unset, behaviour is User Story 3's.

**Why this priority**: the owner's ruling (b). It covers the peer that keeps sending but never reads, which no liveness rule cuts.

**Independent Test**: set the limit and, with a transport whose write does not complete and a peer that keeps sending Heartbeats, block one write of each class. Assert that the session ends within the limit plus the stated margin, records a distinct reason, and that the blocked write and every write queued behind it return an error.

**Acceptance Scenarios**:

1. **Given** the limit set, **When** any non-ending write does not complete within it, counted from that write's start on the transport, **Then** no Logout is attempted, the transport is closed at once, the session ends Disconnected, and the reason is logged and observable as a write timeout. The exception is an ending that already holds the claim (FR-017): that ending's own close applies, and the expiry does nothing.
2. **Given** the limit set and a resend answer of many frames, **Then** each frame's write is limited separately, and a walk that keeps making progress is not cut.
3. **Given** the limit unset (the default), **Then** behaviour is User Story 3's.
4. **Given** an ending Logout write, **Then** 105's `logout_disconnect_timeout_ms` limit applies to it, not this one.
5. **Given** a TOML config with the key set, **Then** it loads. **Given** it out of range, **Then** it is refused with `out_of_range`; **given** it of the wrong type, **Then** it is refused with `malformed_value`, as `logon_timeout_ms` is.

---

### User Story 5 — The TLS handshake bounds are proven and configurable (Priority: P2)

An operator can set, from TOML, how long an initiator's TLS handshake may take, and separately the acceptor listener's bound. A C++ user can set both from the configuration the factory, or the Engine, is built from. A peer that accepts TCP and never handshakes ends the attempt at that bound with `transport_handshake_timeout`, and the reconnect policy then decides what happens next.

**Why this priority**: closes #717 with evidence, and gives operators QuickFIX C++'s practical behaviour (a bounded handshake) at a chosen value.

**Independent Test**: run `drive_reconnect()` over the real TLS factory with `max_attempts = 1` and a short handshake bound, against a listener that never handshakes. Assert that it returns `transport_reconnect_limit_exceeded` (the reconnect FSM's terminal error, unchanged) within the bound plus the stated margin, and that the transport's own handshake ends with `transport_handshake_timeout`. Then load a TOML config that sets the bound, mint a transport from the resolved factory, and assert that its handshake against the same listener ends at the key.

**Acceptance Scenarios**:

1. **Given** the default configuration, **Then** the initiator's handshake bound is today's default.
2. **Given** a TOML config that sets the initiator handshake bound, **When** the initiator connects to a peer that never handshakes, **Then** each attempt ends at that bound with `transport_handshake_timeout`.
3. **Given** `max_attempts = 1`, **Then** `drive_reconnect()` returns `transport_reconnect_limit_exceeded` after the one bounded attempt, within the bound plus the stated margin.
4. **Given** an out-of-range or wrong-typed value, **Then** the loader refuses it with its usual error.
5. **Given** an Engine acceptor, **When** no acceptor key is set, **Then** its bound is 1500 ms, as today. **When** the key is set, **Then** a client that connects and never handshakes is dropped at that bound.

---

### User Story 6 — The Windows asan leg finishes inside its cap (Priority: P1, CI)

A cold-cache run of the `windows-msvc-asan` leg, on a PR or on push:main, completes before the job's backstop cancels it. The debug and release legs keep their current backstop.

**Why this priority**: a cancelled leg blocks every PR's merge box. #720 needed an admin merge.

**Independent Test**: this PR's own asan leg. 106 changes widely included headers, so it builds cold, completes, and reports, under the raised cap.

**Acceptance Scenarios**:

1. **Given** a cold-cache asan run, **Then** the leg finishes or fails on its own steps, not on the backstop.
2. **Given** the workflow comment, **Then** it states the re-derive procedure (the whole job on a cold-cache push:main run, plus a margin, below the cap, and the cap below the hosted runner's per-job limit) and no measured values.

### Edge Cases

- A Logon whose CompIDs cannot be read. On a non-validating session it already disconnects silently. On a validating session the validate-first Reject runs before the CompIDs are read. The mTLS precheck reads no Logon field, so it still runs: an unauthorised mTLS identity is disconnected silently. An admitted mTLS identity with no validator installed draws the Reject exactly as on `9d81082a`. With a validator installed the credentials cannot be read, so the gate fails closed and the session disconnects silently.
- A Logon that `interpret_logon` refuses, on an acceptor with a logon validator installed (credentials policy). The gate treats the credentials as unreadable and fails closed, silently (FIX-TC 1S(c)). This covers every `interpret_logon` refusal (a non-Logon, a CompID or BeginString mismatch, a missing or negative HeartBtInt), even when Username(553) and Password(554) are readable, and it is disclosed in `B-106-1`. An acceptor with the default accept-all policy sees no change.
- The posture refusal tests TestMessageIndicator(464), not the TLS posture, so the identity is known when it runs, and moving it after the gates is always possible.
- A session that is not mTLS and has no logon validator installed (the default accept-all policy, `has_logon_validator()` false): the gates admit every identity, so User Story 1 changes nothing for it.
- 104's too-low Logout already runs after the gates; it is unchanged.
- A peer that stops reading but keeps sending: the give-up never fires, because inbound is live. Only User Story 4's limit cuts it. With the limit unset this stays open and is disclosed as a limitation, matching QuickFIX C++'s and Go's default.
- The give-up fires while a resend walk holds the replay gate: 105's rule applies (no Logout past the gate, then Disconnected).
- Both limits armed at once, for example an ending Logout queued behind a blocked non-ending write, or the write limit and the liveness give-up expiring close together. FR-017 applies: the first decider to take the ending claim owns the one `Disconnected` and, when FR-017 gives it one, its reason record; the others do nothing. Due at one re-test, the give-up claims (R9). An expiry after the claim writes no record and closes nothing.
- `logout_disconnect_timeout_ms` = 0, or no clock: 105's ending limit is off, so a give-up decided while a write is blocked waits for that write. It stays open, as `L-105-f` discloses; the write limit does not cut it once the give-up holds the claim.

## Requirements *(mandatory)*

### Functional Requirements

**B129 (#724)**
- **FR-001**: The `windows-msvc-asan` leg's job cap MUST cover a cold-cache push:main run of the whole job with a margin, and stay below the hosted runner's per-job limit. The other two legs' cap MUST be unchanged. The margin is not a fixed fraction: both values are owner-set and permanent (research D10, `aea0d5a5`), and the recipe checks that a cold-cache run's summed steps plus a margin stay below them. The per-job limit is GitHub's documented execution limit for hosted runners, which the workflow comment names.
- **FR-002**: The workflow comment MUST state the re-derive procedure for the whole job and record no measured values.

**B110 (#702, #703)**
- **FR-003**: In every inbound-Logon arm (acceptor) and Logon-acknowledgement arm (direct initiator), no frame MUST be written and no sequence state MUST be changed (in memory or durably) before every identity gate that applies to the session has admitted the identity. Sequence state means NextNumOut and NextNumIn in memory, the store's counters, and every reset (a `reset_on_logon` reset, a 141=Y reset unit); on the acceptor the store is not read either, because hydration follows the gates (research D1 item 4). Events, log records and logon-validator calls are not state changes under FR-003; their new order is disclosed (contract C-1.6, `L-106-c`).
- **FR-004**: A Logon or acknowledgement refused by an identity gate MUST end the session with no message, whatever other refusal would also have applied. When that other refusal is itself silent (an `interpret_logon` refusal, research D1 item 3), it ends the arm first, with no gate call and no event, and the ending is the same: no message.
- **FR-005**: For an identity the gates admit, every refusal's answer and ending MUST be as on `9d81082a`.
- **FR-006**: A Logon (acceptor) or an ordinary Logon acknowledgement (initiator) refused at the S3 DefaultApplVerID/ApplVerID test MUST leave NextNumIn unchanged in memory and in the store, and so MUST every other refusal of a Logon or acknowledgement, on either role. One exception: a refusal that runs after a reset the arm already applied (a configured `reset_on_logon` reset, or a 141=Y reset unit) keeps that reset. Such a reset runs only for an identity the gates admit, as QuickFIX C++ resets at the same place; it is disclosed as L-106-a. The plan MUST list each refusal that runs after the inbound advance, with a cell for each. "NextNumIn unchanged" means: the store's NextNumIn equals its value before the frame, and NextNumIn in memory equals the value the arm classified the frame against, not that value plus one. On a session hydrated before the frame, that is also its in-memory value before the frame. An acceptor's first Logon hydrates inside the arm, after the identity gates, so there the comparison is with the classified value and not with the unhydrated default read before the frame; an identity refusal precedes hydration and leaves the in-memory value as it was (SC-001).
- **FR-007**: The identity-precedence cells pinned by 104 (`LogonTooLow104.Precedence_*`, `UnaskedResetAck104.Mtls_Unauthorised_NoReset`) MUST still pass, or be updated where this feature deliberately changes the order they pin, each update named in the plan.

**B124 (#714)**
- **FR-008**: The TestRequest give-up MUST fire at its instant, whether or not any write (the liveness loop's own included) is blocked. While the liveness loop's own emit has not returned, that instant is the last inbound frame + 3T + 1 ns (ruling R1; + 1 ns is 105's strictly-after guard); otherwise it is 105's. When every write completes promptly, the TestRequest and give-up instants are 105's, unchanged (contract C-3.5), apart from C-3.4's held cap with none pending. With `logout_disconnect_timeout_ms` > 0 and a clock, the session MUST end within `logout_disconnect_timeout_ms` of it. With 0 or no clock the ending is unbounded and disclosed (`L-105-f`).
- **FR-009**: When the session ends with a write blocked, the blocked write and every write waiting behind it MUST return an error, not hang. One exception, disclosed under `L-093-12` (amended): the liveness loop's own emit while it is still inside its `MessageStore::store` call is not yet a write. The close does not fail it, and it returns when the store returns, which no peer controls (contract C-3.3). The give-up is still decided at its instant and the transport still closes at the decision plus `logout_disconnect_timeout_ms`.
- **FR-010**: A per-write time limit MUST be configurable, off by default. When set, a non-ending write that does not complete within it, counted from that write's start on the transport, MUST close the transport at once, with no Logout attempted, and end the session with a distinct, logged reason, unless another ending already holds the claim (FR-017).
- **FR-011**: Ending Logout writes MUST keep 105's limit (`logout_disconnect_timeout_ms`).
- **FR-012**: The per-write limit MUST be settable from C++ and from TOML, with the loader's usual range and type checks, and MUST have no C-ABI setter (owner ruling).

**B123 (#717)**
- **FR-013**: An initiator's TLS handshake bound MUST be settable from TOML and from the C++ configuration a factory is built from. Its default MUST be today's.
- **FR-013a**: An Engine acceptor's TLS handshake bound MUST be settable from TOML (Engine level) and from the C++ Engine configuration. Its default MUST stay 1500 ms. Neither key has a C-ABI setter.
- **FR-014**: A session-level cell MUST show that `drive_reconnect()`, with `max_attempts = 1`, against a peer that never handshakes, returns `transport_reconnect_limit_exceeded` within the configured bound plus the stated margin (research D9 row FR-014: bound + 2 s). A transport-level cell MUST show that the attempt's handshake ends with `transport_handshake_timeout` at that bound. The reconnect FSM's final error MUST NOT change.

**Cross-cutting**
- **FR-015**: Every behaviour change MUST be recorded in `spec/behaviors-and-limitations.md`:
  - `L-104-3` and `L-104-8` retired, and `L-105-f` narrowed;
  - `B-099-2`, `B-105-3`, `L-093-12`, `B-544-1` and `L-497-1` amended in the clauses 106 changes (research D8);
  - new B rows;
  - the disclosed residuals: a peer that keeps sending but never reads, with the limit off; the unbounded ending with `logout_disconnect_timeout_ms` 0; and, with the limit off, a write blocked before Active that `logon_timeout_ms` does not cut (`L-106-f`, R7).
- **FR-016**: Every new test MUST be shown RED before it counts:
  - against `9d81082a`;
  - for a test that pins unchanged behaviour, against a named mutant;
  - for a cell that names a symbol the base lacks, against a named mutant at the head. Such cells live in the seam file (research D9): the base build lacks the field, so RED is shown on the mutant.
- **FR-017**: Every ending MUST have exactly one `Disconnected` entry, and `onLogout` MUST fire at most once, whichever of the give-up, the write limit, 105's ending Logouts, the establishment deadline, `close()`, `Engine::stop()` and the connection bound's failure exits comes first (ruling R3). It MUST have exactly one reason record when the first claimant is the give-up, the write limit or the establishment deadline, and none when an ending Logout, `close()`, `Engine::stop()`, a reconnect drain or a bound failure exit comes first. An ending Logout's reason is its Logout's Text(58) on the wire; the base writes no record at those sites, and 106 adds none.

### Key Entities

- **Identity gate**: the mTLS CompID-authorisation gate, and the acceptor's credentials gate. Each admits or refuses with no message. The mTLS gate applies on a session with `mtls_ca` or `mtls_pinned`, in either role; the credentials gate applies on every acceptor, whatever its security profile (plaintext and one-way TLS included), and admits every Logon when no logon validator is installed (data-model E-1).
- **Refusal**: a check in a Logon arm that answers (Reject or Logout) or disconnects before the arm's success path completes: the rows of research D2's returns table and the identity gates. A failure after an acceptor's reply Logon is written, and an internal failure (contract C-2.2), is not a refusal.
- **Write class**: the application send, Heartbeat, TestRequest, resend answer (many frames), Reject, SequenceReset, ResendRequest, Logon and reply Logon, and ending Logout (contract C-4.2).
- **Per-write time limit**: the new opt-in knob (FR-010).
- **Initiator handshake bound**: the knob that reaches the initiator factory's transport configuration (FR-013).
- **Acceptor handshake bound**: the Engine-level knob that replaces the listener's hard-coded value (FR-013a).

## Success Criteria *(mandatory)*

### Measurable Outcomes

"The stated margin" in SC-003 to SC-005 and FR-014 is each cell's, from research D9's "Instants" paragraph and its row: exact instants on the mock clock; bound + 2 s for the real-socket cells (the acceptor cell's EOF before 1.2 s); W + 1 s for `ShippingClock106`.

- **SC-001**: In every cell of User Story 1, the unauthorised peer receives zero bytes after the TLS handshake (on a plaintext session, after the accept), and both sequence numbers (in memory and in the store) equal their values before the Logon.
- **SC-002**: In every cell of User Story 2, NextNumIn after the refusal is unchanged in FR-006's sense: the store's value equals its value before the frame, and the in-memory value equals the value the arm classified the frame against, not that value plus one. The exception is a reset already applied in the arm (FR-006, `L-106-a`): there NextNumIn equals the reset's target.
- **SC-003**: With a peer that stops reading, `logout_disconnect_timeout_ms` > 0 and a clock, the session ends within the give-up instant plus `logout_disconnect_timeout_ms` plus the stated margin. This holds in 100% of the runs of each write-class cell, on every sanitizer lane the cells run on. The write-class cells run on the mock clock, so their instants are exact on every lane.
- **SC-004**: With the per-write limit set and no other ending holding the claim, the session ends within the limit plus the stated margin in each write-class cell.
- **SC-005**: A one-attempt reconnect against a never-handshaking peer returns `transport_reconnect_limit_exceeded` within the configured bound plus the stated margin. An Engine acceptor drops a never-handshaking client within its configured bound plus the stated margin.
- **SC-006**: This PR's `windows-msvc-asan` leg completes, green or red on its own steps, without reaching its cap. Its `sccache statistics` step shows whether the run was cold; a warm run is recorded as warm in the PR evidence and does not discharge the cold case, which then rests on the workflow comment's push:main recipe (FR-001).
- **SC-007**: The existing session, transport, config and C-ABI suites pass unchanged, apart from the precedence cells that FR-007 names, the cells that pin an answer or a consumed number ahead of an identity gate, which research D9's FR-007 census amendment re-pins (tasks T051: 099's `NC-h`/`LS-h` rows, `W25Initiator.OffListIdentity_MtB_SilentRefusal`, 038's Cell 11), the cells research D9's second FR-007 census amendment re-pins to the unconsumed NextNumIn (tasks T059: the 099 Logon-arm refusals that pinned a consumed number), the pins of the slot after `wire_undefined_tag` that T013's census lists (the error-completeness and ABI-golden tests, the error-map CSV), `CancelledOutboundStoreMeasurement.Returned_StoreCancelledAtItsLock`, re-pinned to one `Disconnected` entry (FR-017, research D11; Phase 5 ruling 3), and the cells that destroy an Active session with neither `close()` nor a drain, which declare `tests/support/close_before_destroy.hpp` (data-model E-3, the `~Session` row; Phase 5 rulings 1 and 2).

## Normative References

Entries are the exact keys of `spec/coverage-index.md` (Article VI §5), in the form 104's spec uses.

- `[FIX-TC §4 scenario 1S] Receive Logon (Acceptor)`: sub-case 1S(c), an unauthenticated or non-configured identity is disconnected without a message (FR-003, FR-004).
- `[FIX-TC §4 scenario 1B] Connect and Send Logon (Initiator)`: the acknowledgement's refusals, which 106 orders after the identity gate (FR-003, FR-006).
- `[FIXS §4.4] Authorization linked to authentication (auth'd TLS identity ↔ FIX CompID)`: catalogue row T-041; the coverage index has it as a full row of its FIXS section, and also names it in the "Added T-041" gap note (FR-003, FR-004).
- `[FIX-SL §4.3.11] Initial synchronization of messages (Logon seqnum check, ResendRequest on gap)`: the Logon's sequence check, whose advance 106 commits only on success (FR-006).
- `[FIX-SL §4.5.1] FIX connection keep-alive (heartbeat)`: the Heartbeat emit the give-up no longer waits behind (FR-008).
- `[FIX-SL §4.5.5] Test Request processing`: the TestRequest and its give-up (FR-008, FR-009).
- `[FIX-SL §4.6.4] When to terminate without Logout(35=5) (invalid BeginString/CompID cases)`: the silent ending of an unauthorised identity (FR-004). Its first exception terminates the transport without a Logout "when the BeginString(8), SenderCompID(49), TargetCompID(56), or IP address in the Logon(35=A) request of the session initiator is invalid", because "this logon attempt could be an unauthorized access attempt". It is not cited for FR-010: §4.6.4 asks for a Logout "in all other cases, if sending a Logout(35=5) message does not create risk", and no FIX-SL section addresses a peer that stops reading.

The write limit's close with no Logout (FR-010) is anchored instead in `[const §XV.15]`'s `disconnect-and-recover` mode (terminate the session and rely on FIX ResendRequest semantics on reconnect; the peer's recovery is `[FIX-SL §4.3.11]`'s Logon sequence check and ResendRequest on gap). A Logout would queue behind the write that has not completed and could not reach the peer, so none is attempted (`/speckit-clarify`, 2026-11-25).

Informative, not coverage-index entries: QuickFIX C++ v1.16.0, QuickFIX/J 3.0.2 and QuickFIX/Go v0.9.12 session source, for observable behaviour. The write limit's close follows QuickFIX/J's slow-consumer disconnect (`MaxScheduledWriteRequests`, `IoSessionResponder.send`).

## Assumptions

- Out of scope:
  - S3, which waits on Q2 and Q3;
  - S3a B126 (#719), which the owner did not add;
  - a configurable TCP connect timeout;
  - the reconnect policy's default (unbounded attempts, QuickFIX-compatible);
  - `test_067_builder_roundtrip`'s duration (B93 / #664);
  - the Tier 2 sccache restore step's duration, a new finding filed separately;
  - fixpp#715 (one timer slot per Session under a direct driver): 106's new waits never call `Clock::sleep_until` on `system_clock_source`, so they never meet it (research D3).
- No new C-ABI symbol (owner ruling). C-ABI MINOR is still bumped to **1.19, BREAKING, behaviour only** ([const §X.7]; precedent 1.17 and 1.18). A C caller can observe the changes: an unauthorised Logon draws no answer, a blocked send returns an error at the give-up, and NextNumIn holds after an S3 refusal. The C ABI has no NextNumIn accessor: a C caller sees the last one through the session's next Logon exchange (a peer Logon at the refused number is in sequence) and through a file store's contents. Each changed observer in `session.h` gets a C-ABI 1.19 clause, as 105 did.
- Before the plan adds a `SessionConfig` field, grep `tests/` for size pins on `SessionConfig` and `Session`.
- Gate A applies (#702 is security-affecting; session behaviour changes). Gate B applies as usual.
- QuickFIX readings: C++ v1.16.0, QuickFIX/J 3.0.2, QuickFIX/Go v0.9.12, from `reference-engines/` in the parent repo.
