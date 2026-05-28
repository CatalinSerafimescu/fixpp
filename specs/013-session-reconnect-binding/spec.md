# Feature Specification: 013 — Session Reconnect FSM + Recovery + CompID↔TLS-Identity Binding

**Feature Branch**: `013-session-reconnect-binding`
**Created**: 2026-05-28
**Status**: Draft
**Input**: User description: session-Phase-4 (next planned after 012 merge) — FIX session reconnect FSM (Logon / Heartbeat / TestRequest / ResendRequest / SequenceReset / Logout) + recovery semantics + CompID↔TLS-identity binding policy + TLS handshake-outcome wiring. Closes four cross-feature obligations in one bundle: T-041 full row (FIXS §4.4 authorization-linked-to-authentication), T-039 second half (TLS validation outcome → SessionEvent), T-040 second half (2j `ReloadCertSource` control-plane), and the 2e-recovery upgrade obligation (amends 005 FR-008 from fatal-disconnect to recovery-active; flips S-005 / S-006 / S-014-FSM-half / S-024 catalogue rows; drops 44 `fixpp gap` scenario tags; removes 4 in-code 2e-recovery markers).

## Clarifications

### Session 2026-05-28

*`/speckit-clarify` in progress per pipeline.md step 2 + `[const §XVI.3]`. Reference-engine sweep (QuickFIX-cpp v1.16.0 / QuickFIX/J v3.0.1 / Fix8 v1.4.3) executed BEFORE each question per `[[feedback_always_invoke_speckit_clarify]]`. Answers recorded inline below; spec FRs updated as each is integrated.*

- Q: ResetSeqNumFlag(141)=Y protocol — bilateral consent (strict QFJ-style, lenient QFC-mirror), unilateral (Fix8-style), or operator-config per session? Engine sweep: QFC bilateral-mirror, QFJ bilateral-strict, Fix8 unilateral (2-of-3 favour bilateral). → A: **Operator-config controlled per session.** New `SessionConfig::reset_seqnum_policy` enum with three modes: `bilateral_strict` (refuse Logon if peer's response lacks 141=Y), `bilateral_lenient` (auto-mirror 141=Y in our Logon response), `unilateral` (honour any received 141=Y regardless of our outbound flag). Default = `bilateral_strict` per `[const §VI]` security-default and 2-of-3 industry convergence.
- Q: Principal-extraction order from `peer_identity` for CompID-binding — canonical-fixed (CN → SAN-DNS → SAN-URI → fingerprint), SAN-first per RFC 5280, per-binding operator declaration, or fingerprint-only? Engine sweep: NO reference engine implements CompID↔cert binding (QFC `SSLSocketAcceptor.cpp:311` validates chain only; QFJ `X509TrustManagerWrapper` standard trust only; Fix8 same — fixpp greenfield). → A: **Canonical-fixed order (CN → SAN-DNS → SAN-URI → SHA-256-fingerprint, first-non-empty wins).** Single deterministic rule; minimal operator config surface; matches conventional mTLS operator reasoning while still permitting cryptographic fallback to fingerprint when CN/SAN are absent.
- Q: `CompIdAuthorizationPolicy` mode — allow-list only (default-deny), deny-list only (default-allow), hybrid per-entry, or two-tier? Engine sweep: NO reference engine has CompID↔cert binding at all (same finding as Q2). `[const §VI]` security-default rules absent precedent. → A: **Allow-list only (default-deny).** Empty policy rejects ALL Logons; operator MUST enumerate every `{principal → {compid_set}}` binding. Misconfigured deploys fail CLOSED. Matches 011's `Pinset` + `SecurityProfile::mtls_pinned` allow-list shape — same operator mental model, single audit surface. v1.0 ships allow-list only; deny-list / hybrid MAY be added in a later feature if operator demand emerges.
- Q: `reload_credentials` concurrency vs in-flight handshake — defer-until-completes, abort+restart, cooperative (operator-guaranteed quiescence), or two-phase stage+commit? Engine sweep: NO reference engine supports in-process cert rotation at all (QFC + QFJ initialize SSL_CTX once at startup; Fix8 same; all require full restart). → A: **Defer until in-flight handshake completes — atomic swap at `transport_factory::make()` entry.** In-flight handshake finishes on OLD cert; NEXT handshake uses NEW. No mid-handshake `SSL_CTX` mutation (avoids OpenSSL UB). Worst-case rotation latency = one-handshake duration (~50-500 ms typical). Promotes §A.6 from defaulted to explicit decision.
- Q: Logout(5) disconnect timeout default — 0 ms immediate (QFC + Fix8), 2000 ms (QFJ), 5000 ms (prior §A.7 default), or operator-required-no-default? Engine sweep: QFC `Session.cpp:617` immediate, QFJ `SessionState.java:51` `logoutTimeoutMs=2000L`, Fix8 no logout-specific wait. Industry diverges 2-of-3 effectively-immediate vs QFJ's 2 s. → A: **2000 ms default (matches QuickFIX/J).** Single engine precedent + balanced trade-off (long enough to capture clean Logout-replies on typical RTT, short enough to not stall operator-driven shutdowns). Operator overrides via `SessionConfig::logout_disconnect_timeout_ms`. Replaces §A.7's prior 5000 ms default.

---

## User Scenarios & Testing *(mandatory)*

### User Story 1 — Recovery-active reconnect after transient disconnect (Priority: P1)

A FIX engine operator runs a long-lived initiator session against a venue. The TCP connection drops mid-session for a transient reason (network blip, venue restart, peer LB shuffle). Today (under 005 FR-008's pre-amendment semantics), the FSM treats ANY sequence-number gap on re-Logon as fatal — the session goes Disconnected, the operator loses the rest of the trading window, and the only path back is a full operator-driven re-establishment with a forced sequence-reset. Under this feature, the FSM does what every reference FIX engine has done for 20+ years: on disconnect, walk the `ReconnectPolicy` schedule (consumed from 012-2h-transport), mint a fresh `Transport` per attempt, re-issue Logon, observe the peer's sequence-number state, and run the recovery sub-protocol — issue ResendRequest(2) for missing inbound, reply to the peer's ResendRequest with replay-from-store (or SequenceReset-GapFill(4) for admin / skip ranges), and converge both sides on a coherent stream without operator involvement. Heartbeat(0) and TestRequest(1) cadence keep the link healthy after recovery. Logout(5) — either initiator-graceful or acceptor-force-disconnect on a stuck peer — terminates the session cleanly.

**Why this priority**: This is the single biggest v1.0-GA precondition tracked across four surfaces (44 cross-comms scenarios tagged `fixpp gap`, 4 in-code markers, 4 catalogue rows `gap`, the 005 FR-008 amendment obligation). Without it: every venue blip kills the session permanently; the interop harness (per `[[project_release_interop_quickfix_fix8]]`) cannot run 44 of 108 cross-comms scenarios; the engine is unsuitable for any 24/7 named v1.0 venue class. Other obligations in this feature (US2/US3/US4) are P2/P3 because the operator can work around them; recovery is the only one that gates real-world deployment.

**Independent Test**: Bring up a stub FIX peer (or QuickFIX-cpp / QuickFIX/J / Fix8 reference engine) that accepts a Logon, exchanges N application messages (e.g. NewOrderSingle ↔ ExecutionReport pairs), then drops the TCP connection without Logout. fixpp must: (a) destroy the dead Transport; (b) walk the `ReconnectPolicy` schedule per attempt; (c) mint a fresh Transport via `TransportFactory::make(...)` per attempt; (d) re-Logon; (e) issue ResendRequest for the missing-inbound range; (f) reply to the peer's ResendRequest with the in-store outbound range (or GapFill for admin); (g) reach the Active state with both sides on coherent sequence numbers; (h) exchange a final round-trip to prove recovery is complete. Repeat the test against each of the three reference engines as both initiator-driven (we reconnect to them) and acceptor-driven (they reconnect to us).

**Acceptance Scenarios**:

1. **Given** an Active initiator session at inbound MsgSeqNum=42, outbound MsgSeqNum=37, **When** the peer drops the TCP connection without Logout and accepts the next reconnect after sending 3 application messages (peer-side outbound 43,44,45), **Then** fixpp completes Logon, issues `ResendRequest{BeginSeqNo=42, EndSeqNo=44}` (or `EndSeqNo=0` per FIX-SL §4.3.2 for "infinity"), receives the 3 replayed messages, reaches Active, and the operator sees no observable interruption beyond the reconnect wall-clock delay.
2. **Given** an Active initiator session where the peer sent admin (Heartbeat) inside the missing range, **When** we issue ResendRequest, **Then** the peer replies with `SequenceReset{NewSeqNo=N+1, GapFillFlag=Y}` for the admin span (per FIX-SL §4.3.4), our SeqnumManager advances `next_expected_inbound` to `N+1` without storing the synthetic gap-fill body, and recovery proceeds.
3. **Given** an Active initiator session where WE need to replay outbound to a peer's ResendRequest, **When** the peer sends `ResendRequest{BeginSeqNo=M, EndSeqNo=0}`, **Then** we replay every outbound application message from MessageStore[M..our-last], `SequenceReset-GapFill` over admin / skip spans, and set `PossDupFlag(43)=Y` + `OrigSendingTime(122)` on every replayed body per FIX-SL §4.3.5.
4. **Given** a healthy Active session with `HeartBtInt(108)=30`, **When** no inbound traffic arrives for 30 s, **Then** we emit Heartbeat(0) outbound; if no inbound arrives for `1.2 × HeartBtInt`, we emit TestRequest(1); if no inbound arrives for `2 × HeartBtInt`, we Logout and disconnect with `error::session_heartbeat_timeout`.
5. **Given** an Active session, **When** the operator calls `Session::logout()`, **Then** we send Logout(5), wait up to `logout_disconnect_timeout_ms` (Assumptions §A.7) for the peer's Logout reply, close the Transport, and transition to Disconnected; if the timeout elapses, we close anyway and surface `error::session_logout_disconnect_timeout`.
6. **Given** an Active session at inbound MsgSeqNum=N, **When** the peer sends a message with `MsgSeqNum < N` and `PossDupFlag(43)≠Y`, **Then** we Logout with `SessionStatus(1409)=session_seqnum_too_low` (slot 69, unchanged from 005 — recovery does not weaken the too-low rule).
7. **Given** an initiator session configured with `SessionConfig::reset_seqnum_policy = bilateral_strict` (the default per Clarifications Q1 + FR-017), **When** we send Logon with `ResetSeqNumFlag(141)=Y` and the peer's response Logon omits `141=Y`, **Then** we Logout with `error::session_seqnum_reset_mismatch` and disconnect; no sequence numbers are reset on our side. Other modes (`bilateral_lenient` auto-mirrors; `unilateral` honours unconditionally) are operator-selectable per session.

---

### User Story 2 — CompID-to-TLS-identity authorization at Logon (Priority: P1)

A broker operates a multi-tenant FIX acceptor on a single TCP port + TLS endpoint. 50 buy-side clients connect with mTLS, each presenting a distinct client cert. The broker's operator-supplied policy says: "the client cert with `CN=ACME-PROD-01` MAY assert `SenderCompID=ACME01`; the client cert with `CN=ACME-PROD-02` MAY assert `SenderCompID=ACME02`; any other cert↔CompID pairing MUST be rejected at Logon." Without this feature, an attacker with a valid (but unrelated) client cert could assert any CompID they liked — the TLS handshake authenticates the cert, but nothing binds the cert's identity to the FIX application-layer counterparty. This feature closes that gap by binding the `peer_identity` captured at handshake (from 011-tls-policy's `verify_peer`, surfaced to session by 012-2h-transport's `handshake_result.peer_id`) to the FIX CompIDs the peer asserts in Logon(A), and rejecting any mismatch under an operator-configured authorization policy.

**Why this priority**: This is the FIXS §4.4 "Authorization linked to authentication" requirement (catalogue T-041) and the most direct security gap in current fixpp. P1 (co-equal with US1) because acceptors deployed to a multi-tenant venue cannot ship without it; without binding, mTLS is theatre at the application layer. Co-prioritised P1 with US1 because the two surfaces interact (binding happens on Logon, which is the entry into the reconnect FSM) and shipping one without the other leaves a half-feature.

**Independent Test**: Configure an acceptor with a binding policy `{cn="ACME-PROD-01"} → SenderCompID="ACME01"`. Stand up two stub initiators: (a) presents `CN=ACME-PROD-01` + Logons with `SenderCompID=ACME01` → expect Active session; (b) presents `CN=ACME-PROD-01` + Logons with `SenderCompID=ACME99` → expect Logon rejected with `SessionStatus(1409)=session_compid_unauthorized`, session never enters Active, observable `SessionEvent::compid_authorization_failed{cn, asserted_compid, expected_compids[]}` surfaces to the operator's event handler. Repeat for the symmetric initiator-side rule (we authorize the peer's CompID against its server cert when WE're the initiator).

**Acceptance Scenarios**:

1. **Given** an acceptor with binding-policy `{cn="ACME-PROD-01"} → SenderCompID="ACME01"`, **When** a peer with cert `CN=ACME-PROD-01` Logons asserting `SenderCompID=ACME01`, **Then** the session reaches Active and `SessionEvent::peer_identity_bound{cn="ACME-PROD-01", sans=[...], sha256_fingerprint=..., cipher="..."}` is emitted before the first application message.
2. **Given** the same policy, **When** a peer with cert `CN=ACME-PROD-01` Logons asserting `SenderCompID=ACME99`, **Then** the Logon is rejected with `session_compid_unauthorized`, the session goes Disconnected, no `peer_identity_bound` is emitted, and `SessionEvent::compid_authorization_failed{cn="ACME-PROD-01", asserted_compid="ACME99", expected_compids=["ACME01"]}` is emitted with operator-readable reason.
3. **Given** a binding policy in deny-list mode `{cn="REVOKED-01"} → DENY any CompID`, **When** a peer with cert `CN=REVOKED-01` attempts Logon with ANY CompID, **Then** Logon is rejected with `session_compid_unauthorized` regardless of CompID value.
4. **Given** an initiator session against a peer whose server cert asserts `CN=VENUE-A`, **When** the peer Logons-back asserting `TargetCompID=VENUE-A-PROD`, **Then** the initiator-side binding rule applies symmetrically (per `[[feedback_half_restructure_symmetric_api]]` — initiator AND acceptor paths covered by the same policy contract).
5. **Given** principal-extraction in canonical fixed order `CN → SAN-DNS → SAN-URI → SHA-256-fingerprint, first-non-empty wins` per Clarifications Q2 + FR-022, **When** a peer presents a cert with CN populated but SAN absent, **Then** the policy keys on the CN value; `SessionEvent::peer_identity_bound{...principal_source=CN, principal_value="ACME-PROD-01"}` carries the source field name so operators can audit.
6. **Given** allow-list-only policy mode (default-deny) per Clarifications Q3 + FR-023, **When** the operator-supplied `CompIdAuthorizationPolicy` is empty (no bindings declared), **Then** EVERY Logon is rejected with `session_compid_unauthorized` — the engine fails CLOSED on a misconfigured deploy, never OPEN.

---

### User Story 3 — TLS validation outcome as session-level event (Priority: P2)

A FIX engine operator runs an acceptor and wants visibility into failed TLS handshakes at the application layer — not just transport-layer logs. When a peer's cert is expired, has a SAN mismatch, is pinned-but-changed, or violates the cipher policy, the operator wants a structured `SessionEvent` (logged, metriced, possibly paged-on) — not a transport-level error variant that gets dropped on the floor because no session ever opened. Today, 011-tls-policy's `verify_peer` returns the precise rejection variant; 012-2h-transport's `verify_peer_trampoline` surfaces it via the OpenSSL `SSL_VERIFY_PEER` callback; but the surface stops at the transport layer — the session FSM never sees it because no session ever existed. This feature wires the validation outcome through to a session-level event channel so operators can observe TLS-policy violations as session-level signals.

**Why this priority**: P2 because operators today can read transport-layer logs; promoting the outcome to a session-level event is a UX / observability improvement, not a correctness gap. The catalogue row T-039 second half is naturally co-located with this feature (binding & validation-outcome both attach to the same SessionEvent stream); deferring to a separate feature would mean touching the same SessionEvent surface twice.

**Independent Test**: Stand up an acceptor with `SecurityProfile::mtls_pinned`. Attempt to connect from peers presenting (a) an expired cert, (b) a SAN mismatching the configured peer-CN, (c) a cert whose fingerprint isn't in the Pinset, (d) a cipher outside `CipherPolicy`. For each, verify a distinct `SessionEvent::tls_validation_failed{variant, peer_endpoint, reason_string}` surfaces — even though no session ever Logons.

**Acceptance Scenarios**:

1. **Given** an acceptor with `SecurityProfile::mtls_pinned` and a configured `Pinset`, **When** a peer presents a cert whose SHA-256 fingerprint is not in the Pinset, **Then** `SessionEvent::tls_validation_failed{variant=pin_mismatch, peer_endpoint, sha256_received}` is emitted; the TCP socket closes; no Session enters any state above Disconnected.
2. **Given** the same acceptor, **When** a peer presents an expired cert (notAfter < `cfg.clock.now()`), **Then** `SessionEvent::tls_validation_failed{variant=cert_expired, peer_endpoint, notAfter}` is emitted with the precise rejection variant from 011's `tls_verify_error` enum.
3. **Given** an acceptor configured with `SecurityProfile::mtls_pinned` and an EMPTY `Pinset`, **When** the first peer attempts connection, **Then** `SessionEvent::tls_validation_failed{variant=operator_config_error, reason=tls_pin_empty_at_open}` is emitted — distinguishing operator-config errors from peer-cert errors per 011 `/clarify` Q2 ruling.

---

### User Story 4 — In-process credential rotation without session restart (Priority: P3)

A broker rotates client-cert PKI quarterly. Today, rotating the broker's server cert (or a venue's client cert) requires an operator-driven full session restart (Logout → close transport → reload config → re-open). This is an SLA-visible interruption: every connected counterparty sees a Logout and must reconnect. This feature exposes a control-plane path: the operator calls `Session::reload_credentials()` (or its `Listener` equivalent for acceptors), which atomically swaps the `cert_source` on the underlying `SessionConfig`; the NEXT reconnect cycle (whether peer-initiated or driven by a routine reconnect-on-no-traffic policy) picks up the new credentials transparently. Active sessions stay Active; only the next handshake observes the rotation.

**Why this priority**: P3 because operators today CAN do credential rotation via full session restart; this feature reduces the operator-burden + SLA-impact but doesn't unlock anything qualitatively new. The catalogue row T-040 second half is naturally bundled here because the underlying `cert_source` shared-ownership model already exists in 010 / 011; only the control-plane reload entry-point is new.

**Independent Test**: Open an Active initiator session against a stub peer. Trigger `Session::reload_credentials(new_cert_source)`. Verify (a) the Active session is unaffected; (b) `SessionConfig::cert_source()` now returns the new source; (c) on the next reconnect (drive a peer-side disconnect), the new transport_factory consumes the rotated source and the new handshake presents the new cert.

**Acceptance Scenarios**:

1. **Given** an Active session and a new `cert_source` constructed from rotated PEM files, **When** the operator calls `Session::reload_credentials(new_source)`, **Then** the Active session continues exchanging application messages without interruption, no Logout is sent, and the underlying TLS session is NOT torn down.
2. **Given** the same session after `reload_credentials`, **When** the peer drops the TCP connection and triggers a reconnect, **Then** the new Transport (minted by the factory after rotation) presents the rotated cert on the handshake; `SessionEvent::credentials_rotated{old_fingerprint, new_fingerprint}` is emitted before the next Logon.
3. **Given** `reload_credentials` is called concurrently with an IN-FLIGHT handshake (the dead-Transport's replacement is mid-handshake on a fresh socket), **Then** the rotation defers until after the in-flight handshake completes (success or fail) per Clarifications Q4 + FR-033 — the in-flight handshake observes the OLD cert_source; the NEXT `transport_factory::make(...)` call observes the NEW. No mid-handshake `SSL_CTX` mutation; worst-case rotation latency ≈ one-handshake duration.

---

### Edge Cases

- **Resend range with all-admin span**: peer asks us to resend `[10..20]` and every message in that span is admin (Heartbeat, TestRequest). We MUST collapse to a single `SequenceReset-GapFill{NewSeqNo=21}` rather than re-sending admin bodies (per FIX-SL §4.3.5; admin replay is forbidden).
- **Resend range past our store horizon**: peer asks us to resend `[10..20]` but our store only has `[15..20]`. We `SequenceReset-GapFill{NewSeqNo=15}` then replay `[15..20]` (per FIX-SL §4.3.5).
- **ResendRequest EndSeqNo=0**: per FIX-SL §4.3.2, `0` means "infinity" / "the latest we have". Honour as `EndSeqNo = our_last_outbound`.
- **Logon with MsgSeqNum below our expected**: standard `session_seqnum_too_low` per 005 FR-008 (unchanged). Recovery does NOT weaken this.
- **Logon with MsgSeqNum ABOVE our expected (the recovery case)**: enter Resend protocol immediately — issue ResendRequest, stay in a transient `AwaitingResend` sub-state until the gap closes. This is the FR-008 amendment surface.
- **Repeating-group nested in a replayed body**: the wire encoding of FIX repeating groups (e.g. NoMDEntries) must survive replay byte-identical to the original; `PossDupFlag(43)=Y` + `OrigSendingTime(122)` appended without breaking the group structure.
- **TestRequest while in AwaitingResend**: peer probes us mid-recovery. Respond with Heartbeat normally; AwaitingResend does NOT block admin replies.
- **Concurrent reload_credentials + disconnect**: see US4 acceptance scenario 3 and Assumptions §A.6.
- **CompID-binding policy is empty / unset**: default-deny (no peer can Logon). Operator must declare bindings before opening sessions. See Assumptions §A.4.
- **Listener.cancel() during the AwaitingResend phase of an accepted session**: the accepted session continues its recovery (already owned by the FSM); cancel only stops NEW accepts — same as 012 FR-025 (carry-forward).
- **Logout(5) during recovery**: if peer sends Logout while we're issuing ResendRequest, we Logout-reply, abandon recovery, transition to Disconnected. Pending stored messages are NOT retried until the next session.
- **Cipher-policy violation surfaces during reconnect, not initial open**: e.g. a peer rotates to an unapproved cipher. The reconnect handshake fails, `SessionEvent::tls_validation_failed{variant=cipher_policy_violation}` emits, the FSM falls through the `ReconnectPolicy` schedule normally.

---

## Requirements *(mandatory)*

### Functional Requirements

#### Reconnect FSM cadence + admin-message handling

- **FR-001**: System MUST drive Logon(A) re-issuance on every reconnect attempt, consuming the `ReconnectPolicy` envelope from 012-2h-transport per `[2h §4.4]` (schedule-array + jitter + max_attempts semantics).
- **FR-002**: System MUST mint a fresh `Transport` via `TransportFactory::make(...)` per reconnect attempt (matches 012 FR-026 contract; aligns with QuickFIX-cpp / QuickFIX/J / Fix8 fresh-per-attempt pattern per 012 Clarifications 2026-05-27).
- **FR-003**: System MUST emit Heartbeat(0) outbound when no outbound traffic has been sent for `HeartBtInt(108)` seconds (FIX-SL §4.5.1).
- **FR-004**: System MUST emit TestRequest(1) when no inbound traffic has been received for `1.2 × HeartBtInt` seconds; if no inbound traffic arrives within `2 × HeartBtInt` total, the System MUST Logout(5) and disconnect with `error::session_heartbeat_timeout` (FIX-SL §4.5.2).
- **FR-005**: System MUST honour the `HeartBtInt(108)` value negotiated in the inbound Logon (peer-asserted value for acceptors; operator-configured value for initiators).
- **FR-006**: System MUST process inbound Heartbeat(0) by advancing the inbound liveness clock; if the Heartbeat carries `TestReqID(112)`, the System MUST validate that the ID matches the most recent outbound TestRequest's `TestReqID` (per FIX-SL §4.5.4); mismatch is a session-level error (`session_testreqid_mismatch`).
- **FR-007**: System MUST process inbound TestRequest(1) by emitting Heartbeat(0) with `TestReqID(112)` echoed verbatim (FIX-SL §4.5.3).
- **FR-008**: System MUST support both initiator-graceful Logout (we send Logout(5), wait `SessionConfig::logout_disconnect_timeout_ms` for peer reply, then close) AND acceptor-force-disconnect (peer-initiated Logout reply within timeout window; else close anyway) — symmetric coverage per `[[feedback_half_restructure_symmetric_api]]`. Default `logout_disconnect_timeout_ms = 2000` ms per Clarifications Q5 (2026-05-28; matches QuickFIX/J `SessionState.java:51`). Operator MAY override per session.

#### Recovery sub-protocol (ResendRequest / SequenceReset / GapFill) — discharges 2e-recovery upgrade

- **FR-009**: System MUST amend 005 FR-008 from "fatal-disconnect on any inbound sequence gap" to **recovery-active**: when `inbound MsgSeqNum > next_expected_inbound`, System MUST issue ResendRequest(2) for `[next_expected_inbound, inbound_MsgSeqNum - 1]` and transition the session into an `AwaitingResend` sub-state. The session is Active for outbound application traffic during AwaitingResend, but inbound traffic above `next_expected_inbound` is held until the gap closes (per FIX-SL §4.3.2). *Anchors: 005 FR-008 (amended in this feature).*
- **FR-010**: System MUST reply to inbound ResendRequest(2) by walking MessageStore `[BeginSeqNo, EndSeqNo]` (with `EndSeqNo=0` → `our_last_outbound`); for each stored application message, emit it on the wire with `PossDupFlag(43)=Y` and `OrigSendingTime(122)` set to the original send time per FIX-SL §4.3.5.
- **FR-011**: System MUST collapse all-admin spans inside a ResendRequest range into a single `SequenceReset(4){GapFillFlag(123)=Y, NewSeqNo(36)=N+1}` rather than replaying admin bodies (FIX-SL §4.3.5 — admin replay forbidden).
- **FR-012**: System MUST issue `SequenceReset(4){GapFillFlag=Y, NewSeqNo=N+1}` when a ResendRequest range begins before our store horizon (e.g., post-store-purge re-Logon).
- **FR-013**: System MUST process inbound SequenceReset(4) with `GapFillFlag(123)=Y` by advancing `next_expected_inbound` to `NewSeqNo(36)` without storing the synthetic gap-fill body (per FIX-SL §4.3.4).
- **FR-014**: System MUST process inbound SequenceReset(4) with `GapFillFlag(123)≠Y` (the "Reset" form) as a forced-reset: advance `next_expected_inbound` to `NewSeqNo(36)`; emit a warning event because forced-reset is operationally exceptional (FIX-SL §4.3.4).
- **FR-015**: System MUST honour `PossDupFlag(43)=Y` on inbound application messages by deduplicating against MessageStore (suppress duplicate delivery to application callback); messages with `PossDupFlag=Y` and unknown MsgSeqNum MUST be re-stored to preserve the recovered timeline.
- **FR-016**: System MUST flip catalogue rows S-005 (ResendRequest issue), S-006 (ResendRequest reply), S-014-FSM-half (recovery FSM transitions), S-024 (SequenceReset GapFill) from `gap` to `done` upon merge; cross-comms scenario lists with the 44 `fixpp gap` markers MUST be amended to drop the markers; the 4 in-code 2e-recovery upgrade markers MUST be removed (per `[[project_2e_recovery_v1_upgrade_obligation]]`).

#### ResetSeqNumFlag(141) handshake

- **FR-017**: System MUST honour `ResetSeqNumFlag(141)=Y` on Logon according to the operator-configured `SessionConfig::reset_seqnum_policy` — three modes per Clarifications Q1 (2026-05-28):
  - `bilateral_strict` (default; QFJ-style; aligns with `[const §VI]` security-default): when we send Logon with `141=Y` and the peer's response Logon lacks `141=Y`, we Logout with `error::session_seqnum_reset_mismatch` (new error slot per §A.8); inbound `141=Y` is honoured only if our outbound Logon also carried `141=Y`.
  - `bilateral_lenient` (QFC-style mirror): when we receive Logon with `141=Y`, we auto-mirror `141=Y` in our response Logon and BOTH sides reset; symmetric on the acceptor side.
  - `unilateral` (Fix8-style): we reset BOTH inbound and outbound sequence numbers to 1 on receipt of any Logon carrying `141=Y`, regardless of our own outbound `141` flag; our outbound `141` is driven only by config (e.g., `resetOnLogon` style flags).
  In all three modes, a successful reset advances `next_expected_inbound` and `next_expected_outbound` to 1; the operator observes the reset via `SessionEvent::sequence_numbers_reset` (FR-018). *Anchors: FIX-SL §4.4.1; QuickFIX-cpp `Session.cpp:203-207,684,705`, QuickFIX/J `Session.java:2282-2285`, Fix8 `session.cpp:479,581-585,946`.*
- **FR-018**: System MUST surface the chosen `ResetSeqNumFlag` semantics as a documented `SessionEvent::sequence_numbers_reset{by_peer_request: bool}` for operator observability.

#### CompID↔TLS-identity binding (T-041 full row)

- **FR-019**: System MUST bind `peer_identity` (from 011's `verify_peer` outcome, surfaced via 012's `handshake_result.peer_id`) to the asserted `SenderCompID(49)` (acceptor-side: peer's SenderCompID maps to our TargetCompID) at the inbound Logon, BEFORE the session transitions to Active. *Anchor: FIXS §4.4.*
- **FR-020**: System MUST consult an operator-supplied binding policy (a value type `CompIdAuthorizationPolicy`) at Logon-time; the policy maps from `peer_identity` fields to authorized CompID set(s); a successful binding emits `SessionEvent::peer_identity_bound{cn, sans, sha256_fingerprint, cipher, bound_compid}`.
- **FR-021**: System MUST reject Logon with `error::session_compid_unauthorized` (new error variant, slot allocation per Assumptions §A.8) when the asserted CompID is NOT in the authorized set for the captured `peer_identity`; the System MUST emit `SessionEvent::compid_authorization_failed{cn, asserted_compid, expected_compids[]}` before closing the Transport.
- **FR-022**: System MUST extract the principal from `peer_identity` using the canonical fixed order `CN → SAN-DNS → SAN-URI → SHA-256-fingerprint`, first-non-empty wins, per Clarifications Q2 (2026-05-28). The resolved principal value is what `CompIdAuthorizationPolicy` keys against; the extraction order is invariant and not operator-overridable in v1.0 (a future feature MAY introduce per-binding-entry overrides if reference-engine industry pattern emerges). The chosen field name (CN / SAN-DNS / SAN-URI / fingerprint) MUST be surfaced in `SessionEvent::peer_identity_bound{...principal_source: enum}` so operators can audit which field bound the session.
- **FR-023**: `CompIdAuthorizationPolicy` is **allow-list only** in v1.0 per Clarifications Q3 (2026-05-28): an empty policy rejects ALL Logons (default-deny); the operator MUST enumerate every `{principal → {compid_set}}` binding before opening any session; unmatched principal OR unmatched principal→compid pair returns `error::session_compid_unauthorized` (FR-021). Matches the allow-list shape of 011's `Pinset` + `SecurityProfile::mtls_pinned`. Deny-list / hybrid modes are deferred to a later feature; this is a one-way restriction — adding modes is backward-compatible, removing the allow-list semantic is not. *Anchors: `[const §VI]` security-default-deny; 011 FR-023 (Pinset allow-list precedent).*
- **FR-024**: System MUST apply the binding rule symmetrically on the initiator side (peer's server-cert identity bound to peer's asserted `TargetCompID(56)`) — same code path, same policy contract, no half-restructure (per `[[feedback_half_restructure_symmetric_api]]`).
- **FR-025**: System MUST flip catalogue row T-041 from `backlog` to `done` upon merge.

#### TLS validation outcome → SessionEvent (T-039 second half)

- **FR-026**: System MUST surface every `tls_verify_error` variant from 011's `verify_peer` (RSA-low / RSA-high / ECDSA-curve / chain-depth / SAN-card / X.509-version / expiration / pinning / cipher / SAN-mismatch / SHA-1-sig) as a distinct `SessionEvent::tls_validation_failed{variant, peer_endpoint, reason_string}` — the variant is the precise 011 `tls_verify_error` enum value, NOT a coalesced "tls error". *Anchor: 011 `[2g §6.6]` 15-variant table.*
- **FR-027**: System MUST distinguish operator-config errors (e.g. `tls_pin_empty_at_open`) from peer-cert errors in the SessionEvent payload (per 011 `/clarify` Q2 — `variant=operator_config_error{reason=tls_pin_empty_at_open}` vs `variant=cert_expired{notAfter=...}`).
- **FR-028**: System MUST emit `SessionEvent::tls_validation_failed` even when no session ever opens (i.e., handshake fails before Logon); the event channel is bound to the `Listener` / `Session` config, not to a Session-instance lifecycle.
- **FR-029**: System MUST flip catalogue row T-039 second half from `implementing` to `done` upon merge.

#### 2j ReloadCertSource control-plane (T-040 second half)

- **FR-030**: System MUST expose `Session::reload_credentials(new_cert_source)` (initiator-side) and `Listener::reload_credentials(new_cert_source)` (acceptor-side) — both atomically swap the underlying `cert_source` on the SessionConfig; Active session / listener state is NOT torn down.
- **FR-031**: System MUST ensure the rotated `cert_source` is consumed by the next `transport_factory::make(...)` call — i.e., the next reconnect cycle picks up the new cert; in-flight Active sessions remain on the OLD cert until their NEXT handshake.
- **FR-032**: System MUST emit `SessionEvent::credentials_rotated{old_sha256, new_sha256}` before the first handshake on the rotated `cert_source`.
- **FR-033**: When `reload_credentials(new_cert_source)` is called concurrently with an in-flight TLS handshake, System MUST defer the swap until the in-flight handshake completes (success or fail) per Clarifications Q4 (2026-05-28). The atomic-swap point is `transport_factory::make(...)` entry — the in-flight handshake observes the OLD `cert_source`; the NEXT call to `transport_factory::make(...)` observes the NEW. System MUST NOT mutate the OpenSSL `SSL_CTX` mid-handshake (would invoke OpenSSL undefined behaviour). Worst-case rotation latency = one-handshake duration (typically 50-500 ms).
- **FR-034**: System MUST flip catalogue row T-040 second half from `implementing` to `done` upon merge.

#### Cross-cutting: SessionEvent surface + anchor citations + symmetry discipline

- **FR-035**: System MUST surface every new event introduced by this feature (`peer_identity_bound`, `compid_authorization_failed`, `tls_validation_failed`, `credentials_rotated`, `sequence_numbers_reset`) on the existing `SessionEvent` ring-buffer accessor (from 010's F-04 observability close-out); no new event channels.
- **FR-036**: Every FR in this spec MUST cite its binding contract surface (FIX-SL §X / FIXS §X / catalogue row T-X / 005 FR-X) per anchor-citation discipline.
- **FR-037**: Every FR involving initiator / acceptor or send / receive symmetry MUST explicitly cover BOTH paths in the implementation + tests, per `[[feedback_half_restructure_symmetric_api]]` — the 012 RC#B saga (factory caching covered only the initiator half) cost 2 extra Gate B rounds and the full Codex 2/2 attempt budget.
- **FR-038**: Production-shaped entry-point exercise for every cross-session, role, or emit FR per `[[project_005_phase8_completeness_false_pass]]` — completeness-audit step (pipeline step `T052`-equivalent) MUST audit test BODIES, not file names; `SUCCEED()`-placeholder tests count as MISSING coverage.

### Key Entities

- **ReconnectFsm**: A driver layer on top of 005 / 009's 6-state session-establishment FSM. Owns the `ReconnectPolicy` walk, the per-attempt `Transport` minting, the recovery sub-state `AwaitingResend`, the Heartbeat / TestRequest cadence timers, and the Logout / force-disconnect timeout.
- **SessionConfig::reset_seqnum_policy**: Operator-supplied enum (`bilateral_strict` / `bilateral_lenient` / `unilateral`; default `bilateral_strict`) added to the SessionConfig surface from 010; controls the FR-017 ResetSeqNumFlag(141) handshake behaviour. Per Clarifications Q1 (2026-05-28).
- **CompIdAuthorizationPolicy**: A value type carrying the operator's bindings from `peer_identity` to authorized CompID set(s). Constructed at SessionConfig build time; consulted per Logon. Modes: allow-list-only / deny-list-only / hybrid (subject to FR-023 clarify).
- **SessionEvent (extended)**: The existing `SessionEvent` variant set (from 010 F-04) gains five new variants — `peer_identity_bound`, `compid_authorization_failed`, `tls_validation_failed{variant}`, `credentials_rotated`, `sequence_numbers_reset`. All surfaced on the same ring-buffer accessor.
- **ResendState**: Per-session state machine for the Resend sub-protocol: tracks the outstanding `ResendRequest` we issued (BeginSeqNo, EndSeqNo, started_at), the inbound replay we are receiving, the outbound replay we are sending. Lives across the AwaitingResend transient sub-state.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: 24/7 operator runs an initiator session for ≥ 1 month across an arbitrary number of venue-side reconnects (peer disconnects, network blips, scheduled maintenance windows ≤ `ReconnectPolicy::defaults` envelope) without operator intervention; the session reaches Active again on every reconnect that succeeds before `max_attempts` exhausts.
- **SC-002**: 44 of 44 cross-comms scenarios currently tagged `fixpp gap` (per `[[project_2e_recovery_v1_upgrade_obligation]]`) execute end-to-end against each of QuickFIX-cpp / QuickFIX/J / Fix8 (both initiator and acceptor roles, both FIX 4.2 and FIX 4.4+, per `[[project_release_interop_quickfix_fix8]]`); the markers drop from the cross-comms scenario list at merge.
- **SC-003**: A multi-tenant acceptor with 100 distinct mTLS-bound counterparty CompIDs rejects every cross-CompID Logon attempt (cert presented by tenant A asserting tenant B's CompID) within the handshake-to-Logon-reject latency budget defined by `[const §VIII.2]` (≤ 5 ms p99), and emits `SessionEvent::compid_authorization_failed` with operator-readable reason 100 % of the time.
- **SC-004**: Heartbeat / TestRequest cadence on a healthy Active session emits ≤ 1.05 × the configured `HeartBtInt` outbound heartbeats per second on a quiescent link (no false-positive TestRequest under normal jitter); detects a peer dead-link within `2 × HeartBtInt` ± 100 ms wall-clock.
- **SC-005**: Resend protocol recovers a 1000-message inbound gap (mixed application + admin span) within 2 × the wall-clock the original peer took to send those messages (i.e., recovery throughput ≥ 50 % of nominal throughput) — measured against the slowest of the three reference engines.
- **SC-006**: Credentials rotation via `Session::reload_credentials` causes ZERO observable interruption on the existing Active session (zero Logouts emitted, zero application messages delayed beyond their normal send latency); the next reconnect handshake presents the new cert 100 % of the time.
- **SC-007**: TLS validation failures surface as session-level events 100 % of the time (no `tls_verify_error` from the 011 catalogue is dropped on the floor at the transport layer) — measured by fault-injecting each of the 15 variants from `[2g §6.6]` against an acceptor and verifying the matching `SessionEvent::tls_validation_failed` variant arrives at the operator's event handler.
- **SC-008**: After merge, catalogue rows T-039 (full row), T-040 (full row), T-041 (full row) are `done`; rows S-005, S-006, S-014-FSM-half, S-024 are `done`; the 44 `fixpp gap` cross-comms scenario tags are dropped; the 4 in-code 2e-recovery upgrade markers are removed; 005 FR-008 is amended to the recovery-active form.

## Assumptions

- **A.1 — Preconditions are all merged**: 005-session-establishment-fsm + 009-session-fsm-finalize (via PR #82), 010-session-cfg-lifetime (PR #83), 011-tls-policy (PR #84), 012-2h-transport (PR #85). This feature consumes their published surfaces; no precondition feature is open or in-flight at `/speckit-specify` time.
- **A.2 — Reference-engine grounding (per `[[project_reference_engines_setup]]`)**: QuickFIX-cpp v1.16.0, QuickFIX/J v3.0.1, Fix8 v1.4.3 are cloned in `reference-engines/` (gitignored) and serve as the binding contract for `/speckit-clarify` reference-engine sweeps (clarify questions touching protocol / recovery / binding surfaces MUST sweep all three before presenting options to the user, per `[[feedback_always_invoke_speckit_clarify]]`).
- **A.3 — SessionEvent ring-buffer surface from 010 F-04** is the canonical event channel; this feature extends the variant set but does NOT introduce a parallel event surface.
- **A.4 — Default-deny binding policy** (confirmed by Clarifications Q3 + FR-023): an empty `CompIdAuthorizationPolicy` rejects all Logons. v1.0 ships allow-list only — deny-list / hybrid modes are explicitly out of scope per Q3. The reference-engine sweep confirmed no industry precedent exists for this feature; the security-default-deny call is invariant.
- **A.5 — Half-restructure discipline**: every initiator / acceptor symmetric contract is implemented + tested + audited in BOTH halves in this feature (per `[[feedback_half_restructure_symmetric_api]]`). 012 RC#B's saga cost 2 extra Gate B rounds + the full Codex 2/2 attempt budget; the rule is binding.
- **A.6 — `reload_credentials` defers until in-flight handshake completes** (confirmed by Clarifications Q4 + FR-033): rotation calls during an in-flight handshake are queued until the handshake completes (success or fail); the rotated source is consumed at the next `transport_factory::make(...)`. Atomicity is at the factory entry-point, not mid-handshake (avoids OpenSSL UB from mid-handshake `SSL_CTX` mutation). Reference-engine sweep confirmed no precedent — fixpp greenfield, decision invariant.
- **A.7 — Logout disconnect timeout default = 2000 ms** (confirmed by Clarifications Q5 + FR-008): matches QuickFIX/J's `SessionState.logoutTimeoutMs=2000L` — the only engine of the three with an explicit logout-reply wait. QFC + Fix8 close immediately after sending Logout (effectively 0 ms); QFJ's 2 s is the conservative middle ground that captures typical RTT clean-shutdown windows without stalling operator-driven Logouts. Operator overrides via `SessionConfig::logout_disconnect_timeout_ms`.
- **A.8 — `error::session_compid_unauthorized` slot allocation**: a new `error::session_*` variant is added at the next free slot in `include/fixpp/core/error.hpp` (slot TBD at `/speckit-tasks` time); the slot allocation goes through the same `[const §X.2]` ABI-hygiene gate as prior error variants.
- **A.9 — No new C-ABI surface**: this feature stays in `fixpp::session::`; no `extern "C"` symbols added. 2i C-ABI bridges are a separate later feature.

## Dependencies and Outgoing Hooks

- **Inbound (consumed)**:
  - 005 FR-008 (amended in this feature)
  - 005 6-state FSM + admin builders (FR-001 / FR-003 / FR-007)
  - 008 MessageStore (FR-010 outbound replay, FR-015 dedup)
  - 010 SessionConfig + SessionEvent ring-buffer accessor (FR-035)
  - 011 `verify_peer` outcome + `peer_identity` + `tls_verify_error` enum (FR-019 / FR-026)
  - 012 `Transport` + `TransportFactory` + `ReconnectPolicy` + `verify_peer_trampoline` (FR-001 / FR-002 / FR-033)
- **Outgoing (unblocked by this feature)**:
  - 014+ interop harness (`[[project_release_interop_quickfix_fix8]]`) — full QuickFIX-cpp / QuickFIX/J / Fix8 matrix; cross-comms scenario sweep dropping the 44 `fixpp gap` markers.
  - Catalogue rows T-039 full / T-040 full / T-041 full / S-005 / S-006 / S-014-FSM-half / S-024 — all flip `done` post-merge.
  - 005 FR-008 amended in-place.
