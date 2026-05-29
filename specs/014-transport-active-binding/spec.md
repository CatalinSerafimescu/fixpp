# Feature Specification: Transport-Active Session Lifecycle (Live Transport ↔ Identity Binding)

**Feature Branch**: `014-transport-active-binding`
**Created**: 2026-05-29
**Status**: Draft
**Input**: User description: "Transport-active session lifecycle: wire the live TlsTransport into the reconnect FSM and bind the authenticated peer identity into the session. Closes T-041 full row, emits credentials_rotated, absorbs 013+012 Gate-B carry-forwards. Interop matrix and repo-wide lint cleanup are OUT OF SCOPE."

## Overview

013 shipped the session-Phase-4 *control surface* — the reconnect FSM, the recovery sub-protocol, the CompID-authorization *policy*, the TLS-outcome `SessionEvent` shape, and in-process credential reload — but with the **live transport lifecycle stubbed**. The reconnect driver mints a transport and immediately discards it; the authenticated peer identity that gates a session is supplied from a fabricated stand-in rather than the real handshake; and the credential-rotation event carries a placeholder payload.

This feature replaces those stubs with the real wiring so that a session actually re-establishes a live, TLS-authenticated connection on its own, only proceeds when the connected peer is authorized, and reports genuine credential rotation. It is **production-wiring only** — no new protocol surface and no new external dependency. Completing it makes the full session/transport surface live end-to-end, which in turn **unblocks** (but does not include) the per-release cross-engine interop conformance matrix.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Initiator session self-heals after a dropped connection (Priority: P1)

An operator runs an initiator FIX session against a counterparty. The underlying connection drops (network blip, peer restart, transient TLS failure). Without operator intervention, the session establishes a brand-new connection, completes the TLS handshake, and resumes the FIX session — backing off and capping retries exactly as the configured reconnect policy dictates.

**Why this priority**: This is the central value of the feature. Today the reconnect driver is a stub that throws away the transport it creates, so a dropped session never actually recovers its transport. Until this works, the session/transport surface is not live and every other behaviour in this feature is unreachable in production.

**Independent Test**: Drive an initiator session over a loopback TLS fixture, force the transport to drop, and observe that a new live connection is established, the handshake completes, and the session returns to its established state — all governed by the configured backoff schedule and attempt cap. Fully testable with the existing mock/loopback transport fixtures; delivers a self-healing session on its own.

**Acceptance Scenarios**:

1. **Given** an established initiator session whose transport drops, **When** the reconnect driver runs, **Then** a new connection is opened, the TLS handshake completes, a live transport is handed to the session, and the session re-establishes.
2. **Given** a counterparty that is unreachable, **When** the reconnect driver runs, **Then** attempts follow the configured backoff schedule and stop at the configured attempt cap, ending in the FSM's terminal disconnected outcome (no infinite retry).
3. **Given** a reconnect attempt in flight, **When** the session is asked to stop (cooperative cancellation / total teardown), **Then** the in-flight attempt aborts promptly and the partially-constructed transport is released (no leak, no orphaned socket).

---

### User Story 2 - A session only proceeds with an authorized, TLS-authenticated peer (Priority: P1)

An operator configures a CompID authorization policy (an allow-list of permitted peers). When a connection's TLS handshake yields an authenticated peer identity, the session establishes only if that identity is on the allow-list. A connection whose authenticated identity is missing or not on the allow-list is refused — the session never reaches its established state.

**Why this priority**: This closes the **T-041** full row and is the security backbone of the feature. 013 shipped the policy and the identity-extraction logic plus the "fail-closed when mTLS is present" gate, but the *source* of the identity in the production path is a fabricated stand-in. Wiring the real handshake identity into the gate is what makes the authorization meaningful; a fabricated source is a silent security hole.

**Independent Test**: With a binding allow-list policy, drive handshakes that produce (a) an on-list identity, (b) an off-list identity, and (c) no identity, and confirm the session establishes only in case (a) and fails closed (disconnects, no establishment) in (b) and (c). Testable with the loopback TLS fixture presenting different peer certificates.

**Acceptance Scenarios**:

1. **Given** a binding allow-list policy and a handshake yielding an on-list authenticated identity, **When** the session establishes, **Then** establishment is permitted and the bound identity is recorded for the session.
2. **Given** a binding allow-list policy and a handshake yielding an off-list identity, **When** establishment is attempted, **Then** the session fails closed — it disconnects and emits the appropriate session authorization error, never reaching established.
3. **Given** a binding allow-list policy and a handshake yielding no authenticated identity, **When** establishment is attempted, **Then** the session fails closed.
4. **Given** the production session path, **When** a session establishes, **Then** the peer identity used for the authorization decision originates from the live handshake result — there is no fabricated/stand-in identity anywhere in the production path.

---

### User Story 3 - Operators observe genuine credential rotation (Priority: P2)

An operator monitoring session events sees a credential-rotation notification carrying the actual peer-certificate fingerprint whenever the counterparty's certificate changes between connections. They do not see spurious notifications when nothing changed.

**Why this priority**: Observability of credential rotation (FR-032) is valuable for operations and audit, but it rides on top of the live handshake established by Stories 1–2 and is not itself a safety gate. 013 emits this event with a fabricated payload; this story makes the payload real.

**Independent Test**: Drive two successive handshakes for the same session — first with certificate A, then with certificate B — and confirm exactly one rotation event is emitted on the change, carrying B's real fingerprint; drive a reconnect with the same certificate and confirm no rotation event is emitted.

**Acceptance Scenarios**:

1. **Given** a session that has established once with a given peer certificate, **When** a subsequent connection presents a different certificate, **Then** exactly one credential-rotation event is emitted carrying the real fingerprint of the new certificate.
2. **Given** a session that reconnects with the same peer certificate, **When** the new connection establishes, **Then** no credential-rotation event is emitted.

---

### Edge Cases

- **Cancellation mid-handshake**: a stop/total-teardown request during the TLS handshake aborts the attempt and releases the in-flight transport without a leak (sanitizer-verified).
- **TCP up, identity unauthorized**: handshake completes at the transport layer but the peer identity is off-list under a binding policy → fail closed before session establishment.
- **One-way TLS / no client identity under a binding policy**: no authenticated identity available → fail closed (consistent with 013's fail-closed-when-binding semantics).
- **No policy configured (permissive)**: behaviour is inherited unchanged from 013 — absence of a binding policy does not by itself block establishment.
- **Reconnect exclusivity**: a new reconnect attempt does not start while a prior attempt's transport is still in flight (one live attempt at a time).
- **Policy cap exhausted**: the loop terminates at the configured cap in the FSM's terminal disconnected outcome rather than retrying forever.
- **Seqnum too-high during handshake states**: the seqnum manager reports a *semantically correct* too-high condition (not the slot-74 liveness/TestRequest stand-in carried since 013).

## Requirements *(mandatory)*

### Functional Requirements — live reconnect (Story 1)

- **FR-001**: When an initiator session loses its transport (connection drop, handshake failure, or transport read/write error), the system MUST drive a real reconnect attempt that opens a new connection, completes the TLS handshake, and hands a live transport to the session — replacing the current placeholder that mints and immediately discards a transport.
- **FR-002**: The reconnect loop MUST honour the configured reconnect policy's backoff schedule and attempt cap unchanged (it consumes the existing policy; it does not redefine it).
- **FR-003**: The reconnect loop MUST honour cooperative cancellation — a stop / total-teardown request MUST abort an in-flight attempt promptly and release any partially-constructed transport (no leak, no orphaned socket/connection).
- **FR-004**: When the attempt cap is exhausted without a successful authenticated handshake, the system MUST transition to the FSM's terminal disconnected outcome and stop retrying.
- **FR-005**: The stubbed reconnect-driver hooks introduced in 013 MUST be fully realized — no production code path may remain that creates a transport and discards it.

### Functional Requirements — authenticated identity binding / T-041 (Story 2)

- **FR-006**: When a handshake yields an authenticated peer identity, the system MUST supply that identity from the live handshake result to the session's CompID authorization gate before the session is permitted to establish.
- **FR-007**: Under a binding authorization policy, if the authenticated identity is absent or not on the configured allow-list, the system MUST fail closed — disconnect, refuse establishment, and surface the appropriate session authorization error.
- **FR-008**: The production session path MUST contain no fabricated/stand-in peer identity; the residual fabricated auth payload from 013 MUST be removed.
- **FR-009**: The CompID authorization policy semantics (allow-list matching, the canonical identity-extraction order, and the fail-closed-when-binding rule) MUST be inherited unchanged from 013; this feature changes only the *source* of the identity (stub → live handshake), closing the **T-041** catalogue row to `done`.

### Functional Requirements — credential-rotation observability / FR-032 (Story 3)

- **FR-010**: When a connection establishes, the system MUST capture the peer certificate's real SHA-256 fingerprint at the handshake site.
- **FR-011**: The credential-rotation session event MUST carry the real captured fingerprint — the fabricated stub payload from 013 MUST be removed.
- **FR-012**: The system MUST emit the credential-rotation event when the captured fingerprint differs from the fingerprint recorded for the most recent prior successful handshake on that session, and MUST NOT emit it when the fingerprint is unchanged. *(Rotation = change; see Assumptions — candidate for `/speckit-clarify` confirmation.)*

### Carry-forward obligations (absorbed from 013 + 012 Gate-B waivers)

These are tracked obligations this feature discharges; each is testable and must be reflected in the catalogue at close-out.

- **FR-013** (013 slot-74 cleanup): The vestigial too-high branch in the seqnum manager that returns the slot-74 `session_test_request_unanswered` code as a documented stand-in MUST be replaced with a dedicated, semantically-correct seqnum-too-high error code (allocated at the next free error slot; the retired slot remains a permanent numeric hole), with the corresponding comments, the seqnum-manager test assertion, and the contract note updated to match.
- **FR-014** (012 RC#C): The PMR-out-of-memory witness coverage MUST be extended to the depth deferred in 012 — a multi-SAN certificate fixture plus a trampoline-targeted fault-injection cell exercising the mid/tail allocation sites, not only the boundary site.
- **FR-015** (012 RC#G): A live handshake benchmark fixture MUST be added now that the post-cache transport-factory shape is stable and measurable.
- **FR-016** (012 RC#I): The fuzz-scope catalogue entry MUST be re-labelled to reflect its actual post-MVP scope.

### Key Entities

- **Reconnect attempt**: one governed cycle of opening a connection, completing the TLS handshake, and yielding (or failing to yield) a live transport, bounded by the reconnect policy's backoff and cap.
- **Authenticated peer identity**: the identity extracted from the completed TLS handshake (the canonical CN → SAN-DNS → SAN-URI → certificate-fingerprint order established by 011/013), used as the input to the CompID authorization decision.
- **Credential-rotation observation**: the peer certificate's SHA-256 fingerprint captured at the handshake, compared against the prior recorded fingerprint to decide whether rotation occurred.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: An initiator session whose transport drops re-establishes a live, TLS-authenticated session with no operator intervention in 100% of cases where the peer is reachable and authorized, within the bounds of the configured backoff schedule.
- **SC-002**: Zero unauthorized sessions reach the established state — fail-closed is verified for both the off-list-identity and the absent-identity cases under a binding policy.
- **SC-003**: Every credential change across a reconnect produces exactly one rotation event carrying the real new fingerprint; an unchanged-credential reconnect produces zero rotation events.
- **SC-004**: Across N consecutive failed-then-succeeded reconnect attempts (including cancelled-mid-handshake attempts), the system leaks no transport, socket, or memory, as verified under the sanitizer matrix.
- **SC-005**: The **T-041** catalogue row is `done` with full production wiring, and no fabricated authentication payload remains anywhere in the production session path.
- **SC-006**: The unfiltered Tier-1 test suite (including the label-scoped corpus/sync gates) passes — i.e., the suite-green claim is not produced by a name-filtered subset (lesson carried from the 013 close-out).

## Out of Scope

- **Cross-engine interop conformance matrix** (QuickFIX-cpp / QuickFIX-J / Fix8, both roles, multiple FIX versions, historical thorny-bug replay). This feature *unblocks* that work by making the session/transport surface live end-to-end, but the matrix itself is a separate later feature.
- **Repo-wide clang-format / clang-tidy / iwyu / coverage cleanup** — a separate `chore/` branch off `main`.
- **Acceptor-side "reconnect"** — reconnect is an initiator-side concern; acceptor sessions re-accept inbound connections rather than driving a reconnect loop.
- **New protocol surface** — no new public message types, FSM states, or external dependencies; this is wiring of existing surfaces.

## Assumptions

- **Reconnect is initiator-side.** The reconnect driver applies to initiator sessions; acceptor sessions are out of scope for the reconnect loop (they re-accept). This matches the existing driver's role.
- **CompID authorization semantics are inherited from 013** unchanged (allow-list `CompIdAuthorizationPolicy`, canonical CN → SAN-DNS → SAN-URI → SHA-256 extraction order, fail-closed when a binding policy is in effect). 014 changes only the identity *source* (stub → live handshake).
- **Permissive default preserved.** Absence of a configured binding policy does not by itself block establishment — 013's behaviour is preserved.
- **Rotation = fingerprint change** (FR-012). Emitting on observed-fingerprint change (versus on every handshake) is the chosen default; this is the most likely `/speckit-clarify` question and may be revised there.
- **Error-slot allocation continues the existing envelope.** 013 occupies session slots 116..119; any new code introduced here (e.g., the seqnum-too-high replacement for FR-013) takes the next free slot, and retired slots remain permanent numeric holes per the constitution's error-taxonomy rule.
- **No new Phase-2 design doc.** The feature is anchored by 005's session FSM spec plus the already-signed-off 011 (TLS verify/peer-identity), 012 (Transport / TransportFactory / ReconnectPolicy / handshake_result.peer_id), and 013 (reconnect FSM / policy / SessionEvent) surfaces, and decisions 2g/2h/2j.
- **Existing test fixtures suffice.** The loopback-TLS and mock-transport fixtures from 011/012 provide the live-handshake and drop/reconnect scenarios needed; no new external test harness is required.

## Dependencies

- 005 session-establishment FSM (terminal/disconnected outcomes, establishment gate).
- 011 TLS validation surface (`verify_peer`, `peer_identity`, certificate-fingerprint extraction).
- 012 transport surface (`Transport` / `TlsTransport` / `TransportFactory` / `ReconnectPolicy` / `handshake_result.peer_id`, cached SSL_CTX).
- 013 reconnect FSM driver, `CompIdAuthorizationPolicy`, TLS-outcome `SessionEvent`, in-process credential reload.
