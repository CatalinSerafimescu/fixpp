# Feature Specification: Transport-Active Session Lifecycle (Programmatic Runtime Engine + Identity Binding)

**Feature Branch**: `014-transport-active-binding`
**Created**: 2026-05-29
**Status**: Draft
**Input**: User description: "Transport-active session lifecycle: wire the live TlsTransport into the reconnect FSM and bind the authenticated peer identity into the session. Closes T-041 full row, emits credentials_rotated, absorbs 013+012 Gate-B carry-forwards. Interop matrix and repo-wide lint cleanup are OUT OF SCOPE."

## Overview

013 shipped the session-Phase-4 *control surface* — the reconnect FSM, the recovery sub-protocol, the CompID-authorization *policy*, the TLS-outcome `SessionEvent` shape, and the in-process credential-reload control plane — but with **no live-transport lifecycle**. Verified shipped reality: `ReconnectFsm::drive_reconnect_attempt()` mints a transport via the factory and immediately discards it; there is **no production path that creates a Session from an accepted or handshaked transport** for either role; CompID authorization in production is **fail-OPEN** (it runs only when a test-only identity override is set); and the credential-rotation event is unemitted (stub payload only).

This feature builds the missing layer: a **programmatic multi-session runtime engine** (initiator + acceptor) that accepts/connects live transports, drives the TLS handshake, creates Sessions, feeds inbound bytes, and owns per-session lifecycle through a `SessionConfig`-keyed registry. On top of that engine it makes the three stubbed behaviours real — a genuine reconnect loop, fail-CLOSED CompID binding from the live handshake identity (closing **T-041**), and real credential-rotation events — and discharges the 013/012 Gate-B carry-forwards.

It is **production-wiring of existing surfaces plus the runtime engine that connects them** — no new wire protocol. The engine is deliberately bounded *below* the Phase-5 service wrapper (no config-file parsing, no application-callback ecosystem, no store/log factories, no C ABI). Completing it makes the full session/transport surface live end-to-end for both roles, which in turn **unblocks** (but does not include) the per-release cross-engine interop conformance matrix.

## Clarifications

### Session 2026-05-29

- Q: Which session roles should the live transport↔session integration cover? → A: **Both initiator and acceptor.**
- Q: When a reconnect attempt fails, how are the different failure causes treated? → A: **Uniform** — every failure (connect error, TLS handshake failure, CompID authorization failure) consumes one attempt and is retried per the `ReconnectPolicy` backoff until the cap, then terminal.
- Q: How much public surface does the integration layer add? → A: **A full public Initiator/Acceptor engine/manager component** (not a minimal stub-closer).
- Q: Where is the engine's boundary against Phase-5 service-wrapper work? → A: **Programmatic runtime engine** — owns accept/connect loops, handshake, Session creation, byte-feed, and a `SessionConfig`-keyed session registry; **excludes** config-file/`SessionSettings` parsing, the application-callback ecosystem, `MessageStore`/`Log` factories, and the C ABI (those stay Phase-5).
- Q: `credentials_rotated` semantics (reconciled against merged 013 FR-032, not user-chosen). → A: The event reports **our own** rotated `cert_source` end-entity (leaf) SHA-256 (`{old_sha256, new_sha256}`), emitted on the session strand at the next `drive_reconnect_attempt` *before* the new snapshot is passed to `make()`; **not** change-gated (a no-op rotation still emits with `old==new`). This corrects an earlier draft that wrongly modelled it as peer-cert change-detection.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Acceptor engine admits only authenticated, authorized peers (Priority: P1)

An operator registers an acceptor session with the engine. When a counterparty connects, the engine completes the TLS handshake, derives the peer's authenticated identity, and — at the inbound Logon — admits the session to Active only if that identity is authorized by the configured policy. A peer whose authenticated identity is missing or not on the allow-list is refused; the session never reaches Active.

**Why this priority**: This is the security backbone and the **T-041** closure. Today, with no test-only override set (i.e. in production), the authorization check is skipped entirely — a fail-OPEN hole. Building the live acceptor path so the real handshake identity flows into `authorize()` is what makes CompID authorization actually enforced.

**Independent Test**: Register an acceptor over the loopback-TLS fixture and connect peers presenting (a) an on-list identity, (b) an off-list identity, (c) no client identity, with the test-only override UNSET. Confirm the session reaches Active only in (a) and fails closed (disconnect, no Active, authorization-failed event) in (b) and (c). Delivers an enforced acceptor on its own.

**Acceptance Scenarios**:

1. **Given** a registered acceptor with a binding allow-list policy and the test-only override unset, **When** a peer with an on-list authenticated identity Logons, **Then** the engine admits the session to Active and emits `peer_identity_bound`.
2. **Given** the same acceptor, **When** a peer with an off-list authenticated identity Logons, **Then** the engine fails closed — disconnects, never reaches Active, and emits `compid_authorization_failed` with `session_compid_unauthorized`.
3. **Given** the same acceptor under a binding policy, **When** a peer presents no client identity, **Then** the engine fails closed (the all-empty principal is not authorized unless the operator deliberately bound it, per 013 FR-019).
4. **Given** the production path (override unset), **When** any session is admitted, **Then** the identity used for the decision originates from the live handshake — there is no fabricated/stand-in identity and no dependency on the test-only override.

---

### User Story 2 - Initiator engine self-heals after a dropped connection (Priority: P1)

An operator registers an initiator session. The engine connects, completes the handshake, binds the server's identity, and runs the session. When the connection drops, the engine drives a real reconnect loop — opening a new connection, completing the handshake, and resuming the session — honouring the configured backoff and attempt cap, and treating every failure cause uniformly.

**Why this priority**: This closes the central reconnect stub (`drive_reconnect_attempt` mints+discards today) and the initiator half of the live path. Until it works, a dropped initiator session never recovers its transport in production.

**Independent Test**: Register an initiator over the loopback-TLS fixture, force the transport to drop, and observe a new live connection, handshake completion, identity binding, and session resumption — all governed by the configured backoff/cap. Drive failing attempts (unreachable peer, TLS failure, off-list server identity) and confirm each consumes one attempt and the loop terminates at the cap.

**Acceptance Scenarios**:

1. **Given** an established initiator session whose transport drops, **When** the engine runs the reconnect loop, **Then** a new connection opens, the handshake completes, a live transport is handed to the session, and the session resumes.
2. **Given** an unreachable or persistently-failing peer, **When** the reconnect loop runs, **Then** each failed attempt (connect / TLS / authorization) consumes one attempt per the backoff schedule and the loop stops at the configured cap in the FSM's terminal disconnected outcome (no infinite retry).
3. **Given** a reconnect attempt in flight, **When** the engine/session is stopped (cooperative / total-teardown cancellation), **Then** the in-flight attempt aborts promptly and the partially-constructed transport is released (no leak, no orphaned socket).
4. **Given** an initiator under a binding policy, **When** the connected server's authenticated identity is not authorized, **Then** the session fails closed for that attempt (and the attempt is retried per the uniform policy until the cap).

---

### User Story 3 - Operators observe genuine credential rotation (Priority: P2)

An operator rotates the local credentials in-process (013's `reload_credentials`). On the next reconnect handshake, before the rotated certificate is used, the engine emits a credential-rotation event carrying the real old and new leaf fingerprints, so operators can confirm the rotation took effect.

**Why this priority**: Observability of credential rotation (FR-032) is valuable for operations/audit but rides on top of the live reconnect path (Stories 1–2) and is not itself a safety gate. 013 shipped only the rotation control plane; 014 emits the event with the real computed fingerprints.

**Independent Test**: Register an initiator, call `reload_credentials` to stage a new `cert_source`, force a reconnect, and confirm exactly one `credentials_rotated{old_sha256, new_sha256}` event is emitted on the session strand before the rotated cert is used, carrying the real leaf fingerprints; confirm a no-op rotation (same cert) still emits with `old==new`.

**Acceptance Scenarios**:

1. **Given** a session whose credentials were rotated via `reload_credentials`, **When** the next `drive_reconnect_attempt` runs, **Then** exactly one `credentials_rotated{old_sha256, new_sha256}` is emitted on the session strand, before the new snapshot is passed to `make()`, carrying the real SHA-256 leaf fingerprints of the old and new `cert_source`.
2. **Given** a no-op rotation (the new `cert_source` leaf fingerprint equals the current one), **When** the next reconnect runs, **Then** the event is still emitted with `old_sha256 == new_sha256` (not suppressed), per 013 FR-032.

---

### User Story 4 - Carry-forward hardening discharged (Priority: P3)

The engineering team closes the test/quality obligations carried as Gate-B waivers from 013 and 012, so the catalogue rows they block can reach `done`.

**Why this priority**: These are correctness-adjacent hardening items, not user-facing flows, but they are explicit waivers that block catalogue closure and were deferred precisely because they need the live-transport lifecycle this feature introduces.

**Independent Test**: Each obligation has its own witness — the `sigalg_disallowed` cell against an Ed25519/Ed448 fixture; the once-per-handshake credential-load counter and handshake benchmark against a live TLS fixture; the deepened PMR-OOM fault-injection cell; the re-labelled fuzz scope; the corrected seqnum too-high error code.

**Acceptance Scenarios**:

1. **Given** the live TLS fixtures introduced here, **When** the carry-forward witnesses run, **Then** each previously-waived item produces a passing witness and its catalogue row is updated.

---

### Edge Cases

- **Cancellation mid-handshake**: a stop / total-teardown request during the handshake aborts the attempt and releases the in-flight transport without a leak (sanitizer-verified).
- **TCP up, identity unauthorized**: handshake completes at the transport layer but the peer identity is off-list under a binding policy → fail closed before the session reaches Active (acceptor) / for that attempt (initiator).
- **No client identity under a binding policy**: no authenticated identity → fail closed (the all-empty principal is unauthorized unless deliberately bound), per 013 FR-019.
- **Permissive (no binding policy)**: behaviour inherited unchanged from 013 — absence of a binding policy does not by itself block establishment.
- **Reconnect exclusivity**: a new reconnect attempt does not start while a prior attempt's transport is still in flight.
- **Cap exhausted**: the loop terminates at the configured cap in the FSM's terminal disconnected outcome.
- **No-op credential rotation**: `credentials_rotated` still emits with `old==new`.
- **Seqnum too-high during handshake states**: the seqnum manager reports a semantically-correct too-high condition, not the slot-74 liveness/TestRequest stand-in carried since 013.

## Requirements *(mandatory)*

### Functional Requirements — runtime engine (Stories 1 & 2)

- **FR-001**: The system MUST provide a programmatic multi-session runtime engine that owns the lifecycle of FIX sessions over live transports for both initiator and acceptor roles, with sessions registered programmatically and keyed by their `SessionConfig`.
- **FR-002** (acceptor path): For a registered acceptor session, the engine MUST accept an inbound connection, drive the TLS handshake to completion, create a `Session` from the handshake result, and feed inbound transport bytes into the session's frame-processing entry point.
- **FR-003** (initiator path): For a registered initiator session, the engine MUST establish an outbound connection, drive the TLS handshake, create/attach a `Session`, and feed inbound transport bytes into the session.
- **FR-004**: The engine MUST support multiple concurrent registered sessions and expose lifecycle control (start/stop) per session and for the engine as a whole, including orderly teardown that releases all transports.
- **FR-005** (boundary): Engine scope MUST EXCLUDE config-file/`SessionSettings` parsing, the application-callback ecosystem, `MessageStore`/`Log` factories, and the C ABI; these remain Phase-5 service-wrapper work. Sessions are registered via `SessionConfig` objects (as 010 provides).

### Functional Requirements — live reconnect (Story 2)

- **FR-006**: When an initiator session loses its transport, the engine MUST drive a real reconnect attempt (open connection → complete handshake → hand live transport to the session), replacing the 013 stub that mints and discards a transport.
- **FR-007**: The reconnect loop MUST honour the configured `ReconnectPolicy` backoff schedule and attempt cap unchanged (it consumes the 012 policy; it does not redefine it).
- **FR-008** (uniform retry, Clarification Q2): Every failed reconnect attempt — connect error, TLS handshake failure, OR CompID authorization failure — MUST consume one attempt and be retried per the backoff schedule until the cap is reached, at which point the session transitions to the FSM's terminal disconnected outcome. No failure cause is treated as immediately terminal before the cap.
- **FR-009**: The reconnect loop MUST honour cooperative cancellation — a stop / total-teardown request MUST abort an in-flight attempt promptly and release any partially-constructed transport (no leak, no orphaned socket).
- **FR-010**: The 013 stubbed reconnect-driver hooks MUST be fully realized — no production code path may remain that creates a transport and discards it.

### Functional Requirements — authenticated identity binding / T-041 (Story 1, both roles)

- **FR-011**: When a handshake yields an authenticated peer identity (`handshake_result.peer_id`), the engine MUST supply that LIVE identity to the session's CompID authorization gate (`CompIdAuthorizationPolicy::authorize`) at inbound Logon, before the session transitions to Active.
- **FR-012** (fail-OPEN closure): The production authorization path MUST NOT depend on the test-only `SessionConfig::logon_peer_identity_override`; with the override unset (production), `authorize()` MUST run against the live identity. This closes the 013 fail-OPEN gap and is the substance of the **T-041** production wiring.
- **FR-013**: Under a binding policy, an absent or off-allow-list authenticated identity MUST fail closed — disconnect, refuse Active, and emit `session_compid_unauthorized` + `session_event_compid_authorization_failed`. The fail-closed/permissive semantics, the canonical extraction order, and the event shapes are inherited unchanged from 013 (FR-019/FR-020/FR-022).
- **FR-014**: The residual fabricated auth payload from 013 MUST be removed from the production session path.
- **FR-015** (both roles, Clarification Q1): Binding MUST apply to both roles — the acceptor binds the peer's `SenderCompID` ↔ certificate identity (013 FR-019); the initiator binds the connected server's presented identity to the configured `TargetCompID` expectation, fail-closed under a binding policy. The initiator-side binding extends 013's acceptor-side policy/extraction to the initiator role.

### Functional Requirements — credential-rotation observability / FR-032 (Story 3)

- **FR-016**: After `reload_credentials` has staged a new `cert_source`, the engine MUST emit `SessionEvent::credentials_rotated{old_sha256, new_sha256}` on the session strand at the next `drive_reconnect_attempt`, immediately BEFORE the new `cert_source_snapshot()` is passed to `make()` (i.e. before the first handshake on the rotated source), per 013 FR-032.
- **FR-017**: The event MUST carry the REAL SHA-256 end-entity (leaf) fingerprints of the old and new `cert_source` as raw 32-byte arrays, computed inside the credential-load path — replacing 013's fabricated/all-zero stub payload.
- **FR-018**: The event MUST NOT be suppressed on a no-op rotation (`old_sha256 == new_sha256`), per 013 FR-032.

### Carry-forward obligations (Story 4 — absorbed from 013 + 012 Gate-B waivers)

- **FR-019** (013 item 3): Add the `sigalg_disallowed` `sub_reason` cell, exercised with an Ed25519/Ed448 (or unknown-`EVP_PKEY`) certificate fixture, bringing the 013 US3 `sub_reason` coverage to its full set.
- **FR-020** (013 item 2 / 012 RC#G): On a live TLS handshake fixture, add (a) the `cert_source::load_credentials()` once-per-handshake counter witness and (b) the handshake benchmark fixture (post-cache transport-factory shape is stable and measurable now).
- **FR-021** (012 RC#C): Extend the PMR-out-of-memory witness depth — a multi-SAN certificate fixture plus a trampoline-targeted fault-injection cell exercising mid/tail allocation sites, not only the boundary site.
- **FR-022** (012 RC#I): Re-label the fuzz-scope catalogue entry to reflect its actual post-MVP scope.
- **FR-023** (013 slot-74 cleanup): Replace the vestigial too-high branch in the seqnum manager that returns the slot-74 `session_test_request_unanswered` stand-in with a dedicated, semantically-correct seqnum-too-high error code (next free error slot; the retired slot remains a permanent numeric hole), updating the comments, the seqnum-manager test assertion, and the contract note to match.

### Key Entities

- **Session engine (Initiator/Acceptor runtime)**: the new public component that owns accept/connect loops, drives handshakes, creates Sessions from handshake results, feeds inbound bytes, and maintains a `SessionConfig`-keyed registry with per-session and engine-wide lifecycle control. Bounded below the Phase-5 service wrapper.
- **Reconnect attempt**: one governed cycle of opening a connection, completing the handshake, and yielding (or failing to yield) a live authenticated transport, bounded by the `ReconnectPolicy` backoff and cap; every failure cause counts as one attempt.
- **Authenticated peer identity**: the identity extracted from the completed handshake (canonical CN → SAN-DNS → SAN-URI → leaf-fingerprint order from 011/013), used as the input to the CompID authorization decision for both roles.
- **Credential-rotation observation**: our own `cert_source` end-entity SHA-256 (old and new), emitted at the rotated-source handshake site.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A registered acceptor session admits an authorized peer over a live TLS transport and runs an end-to-end session (Logon → application message → Logout) with no hand-fed bytes, in 100% of authorized-peer cases.
- **SC-002**: A registered initiator session whose transport drops re-establishes a live, authenticated session with no operator intervention in 100% of reachable-and-authorized cases, within the configured backoff schedule.
- **SC-003**: Zero unauthorized sessions reach Active in production (test-only override unset) — fail-closed verified for both off-list and absent-identity cases, for both roles.
- **SC-004**: Every failed reconnect attempt (connect / TLS / authorization) consumes exactly one attempt; the loop terminates at the configured cap; no infinite retry.
- **SC-005**: Across N consecutive failed-then-succeeded and cancelled-mid-handshake attempts, the system leaks no transport, socket, or memory, as verified under the sanitizer matrix.
- **SC-006**: Every staged credential rotation produces exactly one `credentials_rotated` event at the next handshake carrying the real new leaf fingerprint (including no-op rotations, which emit `old==new`).
- **SC-007**: The **T-041** catalogue row is `done` with full production wiring — no fabricated authentication payload and no dependency on the test-only override remain in the production session path.
- **SC-008**: The unfiltered Tier-1 test suite (including the label-scoped corpus/sync gates) passes — i.e., the suite-green claim is not produced by a name-filtered subset (lesson carried from the 013 close-out, per the standing `-L sync` rule).

## Out of Scope

- **Cross-engine interop conformance matrix** (QuickFIX-cpp / QuickFIX-J / Fix8, both roles, multiple FIX versions, historical thorny-bug replay). 014 *unblocks* it by making the session/transport surface live end-to-end, but the matrix is a separate later feature.
- **Phase-5 service-wrapper scope**: config-file/`SessionSettings` parsing, the application-callback ecosystem, `MessageStore`/`Log` factories, and the C ABI (per Clarification Q4).
- **Repo-wide clang-format / clang-tidy / iwyu / coverage cleanup** — a separate `chore/` branch off `main`.
- **`tls_load_cancelled` 6th master-enum cell** — permanently N/A for the listener event path (it fires pre-accept at `load_credentials`, with no handshake to emit from); documented and closed in 013, NOT 014 work.
- **New wire protocol surface** — no new FIX message types or FSM states beyond what the engine lifecycle requires.

## Assumptions

- **Engine is programmatic** (Clarification Q4): sessions are registered via `SessionConfig` objects and tracked in a `SessionConfig`-keyed registry; no config-file parsing or service-wrapper ecosystem is introduced.
- **Both roles in scope** (Clarification Q1); reconnect remains an initiator concern (acceptor sessions re-accept rather than driving a reconnect loop).
- **Uniform retry-to-cap** (Clarification Q2), bounded by the 012 `ReconnectPolicy` cap — fixpp already diverges from QuickFIX/Fix8's unbounded reconnect by capping; the cap bounds the cost of retrying deterministic failures.
- **`credentials_rotated` semantics are locked by merged 013 FR-032** — our own rotated `cert_source` leaf fingerprints, emitted at `drive_reconnect_attempt` before `make()`, not change-gated.
- **CompID policy/extraction/events inherited from 013**; the only new behaviour is the initiator-side server-identity binding (extending the acceptor-side policy to the initiator role).
- **Error-slot allocation continues the existing envelope** — 013 occupies session slots 116..119; any new code (e.g. the seqnum-too-high replacement for FR-023) takes the next free slot, and retired slots remain permanent numeric holes per the constitution's error-taxonomy rule.
- **No new Phase-2 design doc** — anchored by 005's session FSM spec plus the signed-off 010 (`SessionConfig`), 011 (TLS verify/peer-identity), 012 (`Transport` / `TransportFactory` / `ReconnectPolicy` / `handshake_result.peer_id`), and 013 (reconnect FSM / policy / `SessionEvent`) surfaces, and decisions 2g/2h/2j. Gate A is mandatory and substantial because the engine is a new public component with new threading/lifecycle surface.
- **Existing test fixtures extended** — the 011/012 loopback-TLS and mock fixtures supply the live-handshake and drop/reconnect scenarios; new cert fixtures (Ed25519/Ed448 for the sigalg cell; multi-SAN for PMR depth) are added.

## Dependencies

- 005 session-establishment FSM (Active/terminal-disconnected outcomes, establishment gate, Logon handling).
- 010 `SessionConfig` (programmatic session configuration + the registry key).
- 011 TLS validation surface (`verify_peer`, `peer_identity`, leaf-fingerprint extraction).
- 012 transport surface (`Transport` / `TlsTransport` / `TransportFactory` / `ReconnectPolicy` / `handshake_result.peer_id`, cached SSL_CTX, `asio_listener`).
- 013 reconnect FSM driver, `CompIdAuthorizationPolicy`, TLS-outcome `SessionEvent`, in-process credential-reload control plane.
