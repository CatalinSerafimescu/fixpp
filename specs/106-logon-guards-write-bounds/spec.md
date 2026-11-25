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

The identity gates are the mTLS CompID-authorisation gate and the acceptor's credentials gate (`authorize_logon`). Neither sends anything when it refuses. FIX Test Cases 1S(c) (§4.4.1) asks that a Logon from an unauthenticated or non-configured identity be answered by "Disconnect without sending a message", because a Reject or a Logout would consume a MsgSeqNum. QuickFIX C++ and Go answer an unknown session's Logon with no message, and QuickFIX/J with no message and no close.

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
6. **Given** an identity the gates admit, **When** any of these refusals applies, **Then** its answer is exactly as on `9d81082a`.

---

### User Story 2 — A refused Logon does not consume its MsgSeqNum (Priority: P1)

An acceptor's Logon, or an initiator's ordinary Logon acknowledgement (no 141=Y), is refused at the S3 DefaultApplVerID/ApplVerID test. NextNumIn stays where it was, in memory and in the store, as QuickFIX's Logon exemption does.

**Why this priority**: protocol fidelity on every FIXT session; it pairs with User Story 1's sequence-state guarantee.

**Independent Test**: refuse a Logon at S3 on each role, then read NextNumIn from the session and from the store.

**Acceptance Scenarios**:

1. **Given** an acceptor with FIXT routing and inbound validation on, **When** a Logon is refused at S3, **Then** the Reject is sent (as today) and NextNumIn is unchanged in memory and in the store.
2. **Given** a direct initiator, **When** its ordinary acknowledgement is refused at S3, **Then** NextNumIn is unchanged in memory and in the store.
3. **Given** the unasked 141=Y acknowledgement refused at S3, **Then** the result is as today: NextNumIn is unchanged.

---

### User Story 3 — A peer that stops reading cannot hold the session (Priority: P1)

A peer keeps its TCP connection open, stops reading, and stops sending. The session's writes back up. The TestRequest give-up still fires at its instant, and the session ends: a Logout under 105's ending-write limit, then the close. The close fails every blocked write.

**Why this priority**: peer-reachable denial (#714). Today a single blocked application write stops the liveness loop.

**Independent Test**: use a peer that stops reading with a small receive window. Block an application write, and separately a Heartbeat, a TestRequest, a resend answer and a Reject. Assert that the session ends within the give-up instant plus `logout_disconnect_timeout_ms` plus a stated scheduling margin, and that every blocked write returns an error.

**Acceptance Scenarios**:

1. **Given** an Active session whose application write is blocked, **When** the peer sends nothing, **Then** the session ends at the give-up instant plus at most `logout_disconnect_timeout_ms` (plus the stated margin), and the blocked write returns an error.
2. **Given** the liveness loop's own TestRequest or Heartbeat write is blocked, **Then** the give-up still fires at its instant. That instant counts from the TestRequest's issue, written or not, as on `9d81082a`.
3. **Given** a resend answer (the walk holds the replay gate) whose write is blocked, **Then** the give-up still ends the session.
4. **Given** a peer that reads normally, **Then** no liveness timing changes: the TestRequest instant and the give-up instant are as on `9d81082a`.

---

### User Story 4 — An operator can opt in to a time limit on every write (Priority: P2)

An operator who wants a slow consumer cut sooner than the liveness give-up sets one per-write limit. A write that does not complete within it closes the transport and ends the session. With the limit unset, behaviour is User Story 3's.

**Why this priority**: the owner's ruling (b). It covers the peer that keeps sending but never reads, which no liveness rule cuts.

**Independent Test**: set the limit and, with a peer that stops reading but keeps sending Heartbeats, block one write of each class. Assert that the session ends within the limit plus the stated margin, records a distinct reason, and that the blocked write and every write queued behind it return an error.

**Acceptance Scenarios**:

1. **Given** the limit set, **When** any non-ending write does not complete within it, counted from that write's start on the transport, **Then** the transport is closed, the session ends Disconnected, and the reason is logged and observable as a write timeout.
2. **Given** the limit set and a resend answer of many frames, **Then** each frame's write is limited separately, and a walk that keeps making progress is not cut.
3. **Given** the limit unset (the default), **Then** behaviour is User Story 3's.
4. **Given** an ending Logout write, **Then** 105's `logout_disconnect_timeout_ms` limit applies to it, not this one.
5. **Given** a TOML config with the key set, out of range, or of the wrong type, **Then** it loads, or is refused with the loader's usual error, as `logout_disconnect_timeout_ms` is.

---

### User Story 5 — An initiator's TLS handshake bound is proven and configurable (Priority: P2)

An operator can set how long an initiator's TLS handshake may take, from TOML, and a C++ user from the factory's configuration. A peer that accepts TCP and never handshakes ends the attempt at that bound with `transport_handshake_timeout`, and the reconnect policy then decides what happens next.

**Why this priority**: closes #717 with evidence, and gives operators QuickFIX C++'s practical behaviour (a bounded handshake) at a chosen value.

**Independent Test**: run `drive_reconnect()` over the real TLS factory with `max_attempts = 1` and a short handshake bound, against a listener that never handshakes. Assert that the call returns with `transport_handshake_timeout` within the bound plus the stated margin. Then load a TOML config that sets the bound, and assert that the initiator's factory receives it.

**Acceptance Scenarios**:

1. **Given** the default configuration, **Then** the initiator's handshake bound is today's default.
2. **Given** a TOML config that sets the initiator handshake bound, **When** the initiator connects to a peer that never handshakes, **Then** each attempt ends at that bound with `transport_handshake_timeout`.
3. **Given** `max_attempts = 1`, **Then** `drive_reconnect()` returns after the one bounded attempt.
4. **Given** an out-of-range or wrong-typed value, **Then** the loader refuses it with its usual error.

---

### User Story 6 — The Windows asan leg finishes inside its cap (Priority: P1, CI)

A cold-cache run of the `windows-msvc-asan` leg, on a PR or on push:main, completes before the job's backstop cancels it. The debug and release legs keep their current backstop.

**Why this priority**: a cancelled leg blocks every PR's merge box. #720 needed an admin merge.

**Independent Test**: this PR's own asan leg. 106 changes widely included headers, so it builds cold, completes, and reports, under the raised cap.

**Acceptance Scenarios**:

1. **Given** a cold-cache asan run, **Then** the leg finishes or fails on its own steps, not on the backstop.
2. **Given** the workflow comment, **Then** it states the re-derive procedure (the whole job on a cold-cache push:main run, plus a margin, below the cap, and the cap below the hosted runner's per-job limit) and no measured values.

### Edge Cases

- A Logon whose CompIDs cannot be read: no gate can run, and it already disconnects silently. That is unchanged.
- A session that is not mTLS and has no credentials policy: the gates admit every identity, so User Story 1 changes nothing for it.
- 104's too-low Logout already runs after the gates; it is unchanged.
- A peer that stops reading but keeps sending: the give-up never fires, because inbound is live. Only User Story 4's limit cuts it. With the limit unset this stays open and is disclosed as a limitation, matching QuickFIX C++'s and Go's default.
- The give-up fires while a resend walk holds the replay gate: 105's rule applies (no Logout past the gate, then Disconnected).
- Both limits armed at once, for example an ending Logout queued behind a blocked non-ending write: whichever expires first closes the transport, and the session ends once.
- The write limit and the liveness give-up expire close together: one ending, one reason (the first to close).
- The acceptor's handshake bound (set by the engine's listener, not by a session): out of scope, disclosed.

## Requirements *(mandatory)*

### Functional Requirements

**B129 (#724)**
- **FR-001**: The `windows-msvc-asan` leg's job cap MUST cover a cold-cache push:main run of the whole job with a margin, and stay below the hosted runner's per-job limit. The other two legs' cap MUST be unchanged.
- **FR-002**: The workflow comment MUST state the re-derive procedure for the whole job and record no measured values.

**B110 (#702, #703)**
- **FR-003**: In every inbound-Logon arm (acceptor) and Logon-acknowledgement arm (direct initiator), no frame MUST be written and no sequence state MUST be changed (in memory or durably) before every identity gate that applies to the session has admitted the identity.
- **FR-004**: A Logon or acknowledgement refused by an identity gate MUST end the session with no message, whatever other refusal would also have applied.
- **FR-005**: For an identity the gates admit, every refusal's answer and ending MUST be as on `9d81082a`.
- **FR-006**: A Logon (acceptor) or an ordinary Logon acknowledgement (initiator) refused at the S3 DefaultApplVerID/ApplVerID test MUST leave NextNumIn unchanged in memory and in the store. Every other refusal that runs after the inbound advance MUST be listed, with its disposition, in the plan.
- **FR-007**: The identity-precedence cells pinned by 104 (`LogonTooLow104.Precedence_*`, `UnaskedResetAck104.Mtls_Unauthorised_NoReset`) MUST still pass, or be updated where this feature deliberately changes the order they pin, each update named in the plan.

**B124 (#714)**
- **FR-008**: The TestRequest give-up MUST fire at its instant, and the session MUST end within `logout_disconnect_timeout_ms` of it, whether or not any write (the liveness loop's own included) is blocked.
- **FR-009**: When the session ends with a write blocked, the blocked write and every write waiting behind it MUST return an error, not hang.
- **FR-010**: A per-write time limit MUST be configurable, off by default. When set, a non-ending write that does not complete within it, counted from that write's start on the transport, MUST close the transport and end the session with a distinct, logged reason.
- **FR-011**: Ending Logout writes MUST keep 105's limit (`logout_disconnect_timeout_ms`).
- **FR-012**: The per-write limit MUST be settable from C++ and from TOML, with the loader's usual range and type checks, and MUST have no C-ABI setter (owner ruling).

**B123 (#717)**
- **FR-013**: An initiator's TLS handshake bound MUST be settable from TOML and from the C++ configuration a factory is built from. Its default MUST be today's.
- **FR-014**: A session-level cell MUST show that `drive_reconnect()`, with one attempt, against a peer that never handshakes, returns `transport_handshake_timeout` within the configured bound.

**Cross-cutting**
- **FR-015**: Every behaviour change MUST be recorded in `spec/behaviors-and-limitations.md`: L-104-3, L-104-8 and L-105-f retired or narrowed, new B rows, and the disclosed residuals (a peer that keeps sending but never reads, with the limit off; the acceptor's handshake bound).
- **FR-016**: Every new test MUST be shown RED against `9d81082a` (or, for a test that pins unchanged behaviour, against a mutant) before it counts.

### Key Entities

- **Identity gate**: the mTLS CompID-authorisation gate, and the acceptor's credentials gate. Each admits or refuses with no message.
- **Refusal**: a check in a Logon arm that answers (Reject or Logout) or disconnects.
- **Write class**: the application send, Heartbeat, TestRequest, resend answer (many frames), Reject, Logon, and ending Logout.
- **Per-write time limit**: the new opt-in knob (FR-010).
- **Initiator handshake bound**: the knob that reaches the initiator factory's transport configuration (FR-013).

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: In every cell of User Story 1, the unauthorised peer receives zero bytes after the TLS handshake, and both sequence numbers (in memory and in the store) equal their values before the Logon.
- **SC-002**: In every cell of User Story 2, NextNumIn after the S3 refusal equals NextNumIn before it, in memory and in the store.
- **SC-003**: With a peer that stops reading, the session ends within the give-up instant plus `logout_disconnect_timeout_ms` plus the stated margin, in 100% of the runs of each write-class cell, on every sanitizer lane the cells run on.
- **SC-004**: With the per-write limit set, the session ends within the limit plus the stated margin in each write-class cell.
- **SC-005**: A one-attempt reconnect against a never-handshaking peer returns within the configured bound plus the stated margin.
- **SC-006**: This PR's `windows-msvc-asan` leg completes, green or red on its own steps, without reaching its cap.
- **SC-007**: The existing session, transport, config and C-ABI suites pass unchanged, apart from the precedence cells that FR-007 names.

## Assumptions

- Out of scope:
  - S3, which waits on Q2 and Q3;
  - S3a B126 (#719), which the owner did not add;
  - the acceptor's handshake bound;
  - a configurable TCP connect timeout;
  - the reconnect policy's default (unbounded attempts, QuickFIX-compatible);
  - `test_067_builder_roundtrip`'s duration (B93 / #664);
  - the Tier 2 sccache restore step's duration, a new finding filed separately.
- The C-ABI version is not bumped for a new C-ABI surface, because there is none (owner ruling). Any C-ABI-visible behaviour change, such as a write limit ending a session, is recorded in `version.h`'s changelog if the plan finds one.
- Gate A applies (#702 is security-affecting; session behaviour changes). Gate B applies as usual.
- QuickFIX readings: C++ v1.16.0, QuickFIX/J 3.0.2, QuickFIX/Go v0.9.12, from `reference-engines/` in the parent repo.
