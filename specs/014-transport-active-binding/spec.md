# Feature Specification: Live Transport Wiring — Reconnect, Identity Binding, Credential-Rotation Events

**Feature Branch**: `014-transport-active-binding`
**Created**: 2026-05-29
**Status**: Draft
**Input**: User description: "Wire the live TlsTransport into the reconnect FSM, bind the authenticated peer identity into the session, and emit real credential-rotation events. Absorb the 013+012 Gate-B carry-forwards. The public multi-session runtime engine is carved out to a separate follow-on feature (015); the interop matrix and the repo-wide lint cleanup are out of scope."

## Overview

013 shipped the session-Phase-4 *control surface* — the reconnect FSM, the recovery sub-protocol, the CompID-authorization *policy*, the TLS-outcome `SessionEvent` shape, and the in-process credential-reload control plane — but with the **live-transport lifecycle stubbed**. Verified shipped reality: `ReconnectFsm::drive_reconnect_attempt()` mints a transport via the factory and immediately discards it (`reconnect_fsm.cpp:53-61`); the CompID-authorization Logon gate is a three-way guard at both call sites (`session.cpp:953-1008` acceptor, `:1757-1803` initiator) — (1) test-only identity override present → `authorize()`; (2) `else if (is_mtls)` → **fail CLOSED** (emit `compid_authorization_failed` + Disconnected); (3) non-mTLS → skip (permissive). So under mTLS-without-override 013 already fails CLOSED — but it cannot *admit* a legitimate peer on the live initiator path for want of any real identity source. And the credential-rotation event is defined-but-unemitted (no emit site exists in 013; `session.hpp:274`).

This feature makes those three behaviours real **at the session / reconnect-FSM / transport level**, using 012's `TransportFactory` and `handshake_result` directly:

1. a genuine initiator reconnect loop (connect → handshake → live transport → session resume);
2. the authenticated peer identity from the live handshake driving the CompID authorization decision — swapping the identity *source* (test-seam/stub → real `handshake_result.peer_id`) so the already-fail-CLOSED mTLS gate becomes *operable* with a live identity on the initiator path (it admits the on-list peer instead of unconditionally fail-closing for want of any identity); and
3. real `credentials_rotated` events carrying the computed certificate fingerprints.

It also discharges the test/quality carry-forwards from the 013 and 012 Gate-B waivers, most of which were deferred precisely because they need a live TLS handshake (which the real reconnect path now provides).

**Explicitly carved out to feature 015**: the public multi-session **Initiator/Acceptor runtime engine** (accept/connect loops as a public component, a `SessionConfig`-keyed session registry, programmatic multi-session lifecycle, the acceptor accept→Session-create→byte-feed production path, and the consequent removal of the test-only authorization override). 014 is the per-session wiring; 015 is the engine that productionizes both roles on top of it and fully closes the **T-041** row. The acceptor-binding boundary is resolved (Clarifications, Session 2026-05-29): 014 wires the live identity into `authorize()` and proves it **only on the live initiator reconnect path**; acceptor binding-logic is proven solely through the existing `logon_peer_identity_override` test seam; the live acceptor accept→handshake→`authorize()` production path and the test-seam removal are 015, where **T-041** fully closes.

## Clarifications

### Session 2026-05-29

- Q: When a live reconnect handshake completes at the TLS layer but the authenticated peer identity fails the binding CompID-authorization policy (off-list / absent), how should the reconnect loop treat that failure? → A: Reason-agnostic — it consumes one attempt and is retried per the backoff schedule to the configured cap (identical to any connect/handshake failure), then terminal-disconnected. No fail-fast and no distinct cap or terminal cause for the authorization-failure case.
- Q: What is the exact 014/015 boundary for the acceptor-side identity→authorize() binding? → A: 014 wires the live identity into `authorize()` and proves it only on the live initiator reconnect path; acceptor binding-logic is proven solely via the existing `logon_peer_identity_override` test seam (on-list / off-list / absent). The live acceptor accept→handshake→`authorize()` production path and test-seam removal are 015, where T-041 fully closes; T-041 stays `implementing` after 014.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Initiator session self-heals after a dropped connection (Priority: P1)

An operator runs an initiator session. The underlying connection drops. Without operator intervention, the reconnect FSM drives a real reconnect attempt — opening a new connection, completing the TLS handshake, and resuming the session — honouring the configured backoff schedule and attempt cap.

**Why this priority**: This is the central value. Today `drive_reconnect_attempt` mints and discards its transport, so a dropped initiator session never recovers in production. Until this works, the live-transport path does not exist and every other behaviour here is unreachable.

**Independent Test**: Drive an initiator session over a loopback-TLS fixture, force the transport to drop, and observe a new live connection, handshake completion, and session resumption — governed by the configured backoff/cap. Drive failing attempts (unreachable peer, TLS failure) and confirm each consumes one attempt and the loop terminates at the cap.

**Acceptance Scenarios**:

1. **Given** an established initiator session whose transport drops, **When** the reconnect FSM runs, **Then** a new connection opens, the handshake completes, a live transport is handed to the session, and the session resumes.
2. **Given** an unreachable or persistently-failing peer, **When** the reconnect loop runs, **Then** each failed attempt consumes one attempt per the backoff schedule and the loop stops at the configured cap in the FSM's terminal disconnected outcome (no infinite retry).
3. **Given** a reconnect attempt in flight, **When** the session is stopped (cooperative / total-teardown cancellation), **Then** the in-flight attempt aborts promptly and the partially-constructed transport is released (no leak, no orphaned socket).

---

### User Story 2 - The live handshake identity drives the authorization decision (Priority: P1)

When a handshake yields an authenticated peer identity, that identity — taken from the real `handshake_result`, not a fabricated stand-in — is what the CompID authorization policy evaluates. Under a binding policy, an unauthorized or absent identity fails closed.

**Why this priority**: This is the substance of the **T-041** binding and the security backbone. 013's mTLS Logon gate already fails CLOSED when no identity is available (the three-way guard's `else if (is_mtls)` arm), but on the live initiator path it has *no* identity source — so it can only ever fail-close, never admit a legitimate peer. 014 supplies the real `handshake_result.peer_id`, making the already-fail-CLOSED gate *operable* with a live identity (admit the on-list peer, fail-close the off-list/absent one). It does **not** introduce fail-CLOSED from scratch and there is no fail-OPEN hole under mTLS; the non-mTLS skip (branch 3) is genuinely permissive and out of scope. 014 removes the dependence on the test seam / fabricated stand-in on the live initiator path. (Full production closure for the acceptor role — removing the test seam entirely — lands with the 015 engine; 014 wires the decision and proves it on the live initiator path and via the existing binding-logic test seam.)

**Independent Test**: On the live initiator reconnect path, confirm the server identity used for authorization originates from the real handshake (no fabricated payload). Using the binding-logic test seam, drive on-list / off-list / absent identities and confirm authorize() admits only the on-list case and fails closed (disconnect, authorization-failed event) otherwise.

**Acceptance Scenarios**:

1. **Given** a live initiator reconnect, **When** the handshake completes, **Then** the peer identity passed to `CompIdAuthorizationPolicy::authorize()` is the real `handshake_result.peer_id` — there is no fabricated identity on this path.
2. **Given** a binding allow-list policy, **When** an off-list or absent identity is evaluated, **Then** the decision fails closed — the session does not reach Active and a `compid_authorization_failed` event with `session_compid_unauthorized` is emitted.
3. **Given** the canonical extraction order and policy semantics from 013 (FR-019/020/022), **When** authorization runs, **Then** those semantics are inherited unchanged; 014 changes only the *source* of the identity (stub → live handshake).

---

### User Story 3 - Operators observe genuine credential rotation (Priority: P2)

An operator rotates the local credentials in-process (013's `reload_credentials`). On the next reconnect handshake, before the rotated certificate is used, a credential-rotation event is emitted carrying the real old and new leaf fingerprints.

**Why this priority**: Observability of credential rotation (FR-032) rides on top of the live reconnect path and is not itself a safety gate. 013 shipped only the rotation control plane; 014 emits the event with the real computed fingerprints.

**Independent Test**: Stage a new `cert_source` via `reload_credentials`, force a reconnect, and confirm exactly one `credentials_rotated{old_sha256, new_sha256}` is emitted on the session strand before the rotated cert is used, carrying the real leaf fingerprints; confirm a no-op rotation still emits with `old==new`.

**Acceptance Scenarios**:

1. **Given** a session whose credentials were rotated via `reload_credentials`, **When** the next `drive_reconnect_attempt` runs, **Then** exactly one `credentials_rotated{old_sha256, new_sha256}` is emitted on the session strand, before the new snapshot is passed to `make()`, carrying the real SHA-256 leaf fingerprints of the old and new `cert_source`.
2. **Given** a no-op rotation (the new `cert_source` leaf fingerprint equals the current one), **When** the next reconnect runs, **Then** the event is still emitted with `old_sha256 == new_sha256` (not suppressed), per 013 FR-032.

---

### User Story 4 - Carry-forward hardening discharged (Priority: P3)

The engineering team closes the test/quality obligations carried as Gate-B waivers from 013 and 012, most of which need the live TLS handshake this feature introduces.

**Why this priority**: These are correctness-adjacent hardening items, not user-facing flows, but they are explicit waivers blocking catalogue closure.

**Independent Test**: Each obligation has its own witness — the `sigalg_disallowed` cell against an Ed25519/Ed448 fixture; the once-per-handshake credential-load counter and the handshake benchmark against a live TLS fixture; the deepened PMR-OOM fault-injection cell; the re-labelled fuzz scope; the corrected seqnum too-high error code.

**Acceptance Scenarios**:

1. **Given** the live TLS fixtures introduced here, **When** the carry-forward witnesses run, **Then** each previously-waived item produces a passing witness and its catalogue row is updated.

---

### Edge Cases

- **Cancellation mid-handshake**: a stop / total-teardown request during the handshake aborts the attempt and releases the in-flight transport without a leak (sanitizer-verified).
- **TCP up, identity unauthorized**: handshake completes at the transport layer but the identity is off-list under a binding policy → fail closed; the failed attempt counts as one reconnect attempt and is retried to the cap reason-agnostically (FR-003).
- **No identity under a binding policy**: no authenticated identity → fail closed (the all-empty principal is unauthorized unless deliberately bound), per 013 FR-019.
- **Permissive (no binding policy)**: behaviour inherited unchanged from 013.
- **Reconnect exclusivity**: a new attempt does not start while a prior attempt's transport is still in flight.
- **Cap exhausted**: the loop terminates at the configured cap in the FSM's terminal disconnected outcome.
- **No-op credential rotation**: `credentials_rotated` still emits with `old==new`.
- **Seqnum too-high during handshake states**: the seqnum manager reports a semantically-correct too-high condition, not the slot-74 liveness/TestRequest stand-in carried since 013.

## Requirements *(mandatory)*

### Functional Requirements — live reconnect (Story 1)

- **FR-001**: When an initiator session loses its transport (connection drop, handshake failure, or transport read/write error), the reconnect FSM MUST drive a real reconnect attempt that opens a new connection, completes the TLS handshake, and hands a live transport to the session — replacing the placeholder that mints and immediately discards a transport.
- **FR-002**: The reconnect loop MUST honour the configured `ReconnectPolicy` backoff schedule and attempt cap unchanged (it consumes 012's policy; it does not redefine it).
- **FR-003**: Every failed reconnect attempt MUST consume one attempt and be retried per the backoff schedule until the cap is reached, at which point the session transitions to the FSM's terminal disconnected outcome (no infinite retry). This is **reason-agnostic**: an attempt that fails authorization (off-list / absent identity under a binding policy) MUST consume one attempt and be retried to the same cap exactly like a connect or handshake failure — no fail-fast, and no distinct cap or terminal cause for the authorization-failure case.
- **FR-004**: The reconnect loop MUST honour cooperative cancellation — a stop / total-teardown request MUST abort an in-flight attempt promptly and release any partially-constructed transport (no leak, no orphaned socket).
- **FR-005**: The 013 stubbed reconnect-driver hooks MUST be fully realized — no production code path may remain that creates a transport and discards it.

### Functional Requirements — authenticated identity binding (Story 2)

- **FR-006**: When a handshake yields an authenticated peer identity (`handshake_result.peer_id`), the live identity MUST be supplied to `CompIdAuthorizationPolicy::authorize()` — there must be no fabricated/stand-in identity on the live path.
- **FR-007**: Under a binding policy, an absent or off-allow-list authenticated identity MUST fail closed — refuse Active and emit `session_compid_unauthorized` + `session_event_compid_authorization_failed`. The fail-closed/permissive semantics, canonical extraction order, and event/code *shapes* are inherited unchanged from 013 (FR-019/FR-020/FR-022). The *FSM disposition* differs from 013's open-Logon path: on 013's open path an authorize failure drives the terminal `Disconnected` transition (`session.cpp:1004-1005`/`:1799-1800`); on the live **reconnect** path it does **NOT** drive terminal Disconnected — it emits the inherited event/code, releases the in-flight transport, counts as one reconnect attempt, and loops per the backoff schedule (FR-003). Only loop-exhaustion at the cap transitions to terminal Disconnected.
- **FR-008**: The residual fabricated auth payload from 013 MUST be removed from the live path. (The test-only `logon_peer_identity_override` may remain as the binding-logic test seam until the 015 engine removes the dependency; full production closure of **T-041** for the acceptor role lands with 015.)

### Functional Requirements — credential-rotation observability / FR-032 (Story 3)

- **FR-009**: After `reload_credentials` has staged a new `cert_source`, the system MUST emit `SessionEvent::credentials_rotated{old_sha256, new_sha256}` on the session strand at the next `drive_reconnect_attempt`, immediately BEFORE the new `cert_source_snapshot()` is passed to `make()` (before the first handshake on the rotated source), per 013 FR-032.
- **FR-010**: The event MUST carry the REAL SHA-256 end-entity (leaf) fingerprints of the old and new `cert_source` as raw 32-byte arrays, computed inside the credential-load path — replacing 013's fabricated/all-zero stub payload.
- **FR-011**: The event MUST NOT be suppressed on a no-op rotation (`old_sha256 == new_sha256`), per 013 FR-032.

### Carry-forward obligations (Story 4 — absorbed from 013 + 012 Gate-B waivers)

- **FR-012** (013 item 3): Add the `sigalg_disallowed` `sub_reason` cell, exercised with an Ed25519/Ed448 (or unknown-`EVP_PKEY`) certificate fixture, bringing the 013 US3 `sub_reason` coverage to its full set.
- **FR-013** (013 item 2 / 012 RC#G): On a live TLS handshake fixture, add (a) the `cert_source::load_credentials()` once-per-handshake counter witness and (b) the handshake benchmark fixture.
- **FR-014** (012 RC#C): Extend the PMR-out-of-memory witness depth — a multi-SAN certificate fixture plus a trampoline-targeted fault-injection cell exercising mid/tail allocation sites, not only the boundary site.
- **FR-015** (012 RC#I): Re-label the fuzz-scope catalogue entry to reflect its actual post-MVP scope.
- **FR-016** (013 slot-74 cleanup): Replace the vestigial too-high branch in the seqnum manager that returns the slot-74 `session_test_request_unanswered` stand-in with a dedicated, semantically-correct seqnum-too-high error code (next free error slot; the retired slot remains a permanent numeric hole), updating the comments, the seqnum-manager test assertion, and the contract note.

### Key Entities

- **Reconnect attempt**: one governed cycle of opening a connection, completing the handshake, and yielding (or failing to yield) a live transport, bounded by the `ReconnectPolicy` backoff and cap; every failure cause counts as one attempt.
- **Authenticated peer identity**: the identity extracted from the completed handshake (canonical CN → SAN-DNS → SAN-URI → leaf-fingerprint order from 011/013), used as the input to the CompID authorization decision.
- **Credential-rotation observation**: our own `cert_source` end-entity SHA-256 (old and new), emitted at the rotated-source handshake site.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: An initiator session whose transport drops re-establishes a live, TLS-authenticated session with no operator intervention in 100% of reachable cases, within the configured backoff schedule.
- **SC-002**: Every failed reconnect attempt consumes exactly one attempt; the loop terminates at the configured cap; no infinite retry.
- **SC-003**: On the live path, the identity evaluated by `authorize()` originates from the real handshake — no test-seam/fabricated identity source remains on the live initiator path; under a binding policy, the on-list identity is admitted and off-list/absent identities fail closed (the already-fail-CLOSED mTLS gate becomes operable with a live identity, not a fail-OPEN hole being closed) — verified via the binding-logic test seam.
- **SC-004**: Across N consecutive failed-then-succeeded and cancelled-mid-handshake attempts, the system leaks no transport, socket, or memory, as verified under the sanitizer matrix.
- **SC-005**: Every staged credential rotation produces exactly one `credentials_rotated` event at the next handshake carrying the real new leaf fingerprint (including no-op rotations, which emit `old==new`).
- **SC-006**: All five carry-forward witnesses (sigalg cell, once-per-handshake counter, handshake bench, PMR-OOM depth, seqnum too-high code) pass, and the fuzz-scope catalogue entry is re-labelled.
- **SC-007**: The unfiltered Tier-1 test suite (including the label-scoped corpus/sync gates) passes — i.e., the suite-green claim is not produced by a name-filtered subset (lesson carried from the 013 close-out, per the standing `-L sync` rule).

## Out of Scope

- **The public multi-session Initiator/Acceptor runtime engine** → **feature 015** (next-planned after 014, before the chore/interop work): the public accept/connect-loop component, the `SessionConfig`-keyed session registry, programmatic multi-session lifecycle, the acceptor accept→Session-create→byte-feed production path, removal of the test-only authorization override, and the consequent **full T-041 production closure for both roles**.
- **Cross-engine interop conformance matrix** (QuickFIX-cpp / QuickFIX-J / Fix8) — a later feature unblocked by 014+015.
- **Repo-wide clang-format / clang-tidy / iwyu / coverage cleanup** — a separate `chore/` branch off `main`.
- **Phase-5 service-wrapper scope** — config-file/`SessionSettings` parsing, the application-callback ecosystem, `MessageStore`/`Log` factories, the C ABI.
- **`tls_load_cancelled` 6th master-enum cell** — permanently N/A for the listener event path (fires pre-accept at `load_credentials`, no handshake to emit from); documented and closed in 013.

## Assumptions

- **No public multi-session engine in 014** — the live path is wired through the existing `ReconnectFsm` (initiator) and 012's `TransportFactory`/`handshake_result` directly; the engine is 015.
- **Reconnect is initiator-side** — acceptor sessions re-accept rather than driving a reconnect loop; the acceptor live path is part of the 015 engine.
- **`credentials_rotated` semantics are locked by merged 013 FR-032** — our own rotated `cert_source` leaf fingerprints, emitted at `drive_reconnect_attempt` before `make()`, not change-gated.
- **CompID policy/extraction/events inherited from 013**; 014 changes only the identity *source* (stub → live handshake) and removes the fabricated payload on the live path. The test-only override may persist until 015.
- **`T-041` advances but does not fully close in 014** — 014 wires the live identity into the decision and proves it on the live initiator path only; acceptor binding-logic is proven via the existing test seam. Full production closure for the acceptor role (live acceptor path + test-seam removal) lands with the 015 engine. (014/015 binding boundary resolved — Clarifications, Session 2026-05-29.)
- **Uniform retry-to-cap** for all failed reconnect attempts (bounded by 012's cap; matches QFC/QFJ reason-agnostic reconnect) — including the authorization-failure cause, which is treated identically (resolved — Clarifications, Session 2026-05-29).
- **Error-slot allocation continues the existing envelope** — 013 occupies session slots 116..119; any new code (e.g. the seqnum-too-high replacement for FR-016) takes the next free slot; retired slots remain permanent numeric holes.
- **No new Phase-2 design doc** — anchored by 005's FSM spec plus the signed-off 010/011/012/013 surfaces and decisions 2g/2h/2j. Gate A is mandatory (touches the security boundary + reconnect lifecycle).
- **Existing test fixtures extended** — the 011/012 loopback-TLS and mock fixtures supply the live-handshake and drop/reconnect scenarios; new cert fixtures (Ed25519/Ed448; multi-SAN) are added.

## Dependencies

- 005 session-establishment FSM (Active/terminal-disconnected outcomes, Logon handling).
- 010 `SessionConfig` (programmatic session configuration; the `logon_peer_identity_override` test seam).
- 011 TLS validation surface (`verify_peer`, `peer_identity`, leaf-fingerprint extraction).
- 012 transport surface (`Transport` / `TlsTransport` / `TransportFactory` / `ReconnectPolicy` / `handshake_result.peer_id`).
- 013 reconnect FSM driver, `CompIdAuthorizationPolicy`, TLS-outcome `SessionEvent`, in-process credential-reload control plane.
