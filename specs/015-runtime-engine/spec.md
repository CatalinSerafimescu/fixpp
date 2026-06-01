# Feature Specification: Public Initiator/Acceptor Runtime Engine & Full T-041 Closure

**Feature Branch**: `015-runtime-engine`
**Created**: 2026-05-30
**Status**: Draft — clarified 2026-05-30; productionizes both FIX roles on top of 014's per-session live wiring; closes T-041 (carve-out per `[[project_014_015_split]]`)
**Input**: User description: "next phase - 015 — the public Initiator/Acceptor runtime engine + full T-041 closure + logon_peer_identity_override seam removal"

## Clarifications

### Session 2026-05-30

- Q: How does the engine handle an inbound acceptor connection's Logon — static pre-configured matching, dynamic session creation, or both? → A: **Both, static by default.** Static pre-configured matching (an inbound Logon matched by *reversed* SenderCompID/TargetCompID to an already-registered acceptor `SessionConfig`) is the default and mandatory path on which T-041 fail-CLOSED binding is proven; an unmatched Logon under static-only is rejected with NO session created. An **optional dynamic-session-provider hook** MAY create a session on first Logon (mirroring QFJ `DynamicAcceptorSessionProvider`), routing through the *identical* `authorize()` gate. (Grounded against QFC `Acceptor::getSession` reverse-route + QFJ Static/Dynamic `AcceptorSessionProvider`.)
- Q: What defines "SessionConfig identity" for registry lookup and the FR-002 duplicate rejection? → A: The **FIX SessionID tuple** — BeginString + SenderCompID + TargetCompID — derived from each `SessionConfig`. Role-agnostic, canonical, matches QFC/QFJ SessionID keying; two `SessionConfig`s resolving to the same tuple are duplicates and the second registration is rejected.
- Q: How do `start()`/`stop()` and threading work for the public engine? → A: **Injected executor, non-blocking start.** The engine takes a caller-supplied asio executor (e.g. an `io_context`); `start()` launches the connect/accept loops and returns (does not block); `stop()` cancels all loops and is idempotent; the caller drives the executor. No engine-owned worker threads — matches the existing asio tree and `[const §XI]`.

### Session 2026-05-30 (Gate A round 1)

Decisions made during the Gate A re-plan (resolving ambiguities surfaced by the review; the prior Phase-0/1 baseline was partly fabricated and is re-derived from first-hand source — see plan.md `## Gate A`):

- Q: How is a `Session` constructed, and is there an `Application&` dependency? → A: Via the **public, synchronous ctor `Session(const EngineConfig&, const SessionConfig&)`** (`session.hpp:95`) + the awaitable `open()`. There is **NO `make_session` factory, no private ctor, and no `Application&` parameter** — the prior research.md baseline row was fabricated. `register_session` takes only a `SessionConfig`. (Resolves Gate A Codex-3 / New-2.)
- Q: How does the acceptor obtain a live peer identity, and how is the live transport attached for the FIRST connection? → A: `Listener::async_accept()` returns a TCP-connected transport with TLS NOT yet issued; the accept loop **runs `async_handshake` itself** and harvests `handshake_result.peer_id`. Attach uses a **new acceptor-specific primitive** (design-named `attach_accepted_transport`) that sets `live_peer_id_` + rebinds outbound **without** an FSM transition — NOT `install_reconnected_transport`, which re-enters `LogonSent` (initiator-only). (Resolves Gate A Codex-1/Codex-2/Codex-4.)
- Q: Which gate site gets the live-identity arm? → A: The **single acceptor gate `session.cpp:1048`** ONLY. `session.cpp:1913` is the initiator seam arm (in `case LogonSent`), NOT a second acceptor gate; the seam arm is *removed* from both `:1048` and `:1913`. (Resolves Gate A New-7.)
- Q: What guarantees the acceptor gate authorizes against the live identity? → A: A written **happens-before invariant** — `live_peer_id_` is set on the session strand strictly-happens-before the first `on_inbound_frame` reaching the acceptor gate — plus a delayed-identity fail-CLOSED regression test. (Resolves Gate A New-1.)
- Q: What bounds the pre-session window (handshake + first-frame read) against a slow-loris peer? → A: A byte budget + a handshake/Logon deadline owned by an engine-level accept-scope cancellation domain (FR-014 / SC-011). (Resolves Gate A Codex-10.)
- Q: How is engine teardown made UAF-safe? → A: `~Engine()` is a strict `assert(stopped())` (no synchronous best-effort path); `stop()` joins all loops/pumps **before** clearing the registry that owns the sessions; `open()` is awaited inside each loop (not in synchronous `start()`), so `lookup()` may return a not-yet-open / null session. (Resolves Gate A Codex-9 / New-3 / New-4.)
- Q: Is there an inbound application-message sink? → A: No — 015 is scoped to admin/session-layer flow; application-message delivery to a user is Phase-5 (Application is out of scope, FR-013). (Resolves Gate A New-2.)
- Q: How many new error slots? → A: Exactly one — `session_unknown_acceptor_session = 121` (next free; no reusable "unknown session" code exists). Duplicate registration reuses `session_invalid_argument = 119`; authz failure reuses `session_compid_unauthorized = 117`. (Resolves Gate A New-6.)
- Q: Read-pump framing surface? → A: The real `wire::Framer::feed(incoming, carry, out)` + `pending_bytes()` (no `feed(bytes)`/`next()`); over-capacity → `wire_frame_too_large`; backpressure is natural (synchronous `co_await on_inbound_frame`). (Resolves Gate A Codex-5.)

### Session 2026-05-31

Decisions resolving the initiator outbound-send wiring + initial-Logon emission sequencing, which data-model E-1 under-specified for the engine's lazy-connect model (surfaced during US2 `/speckit-implement`; reference-engine sweep done first per `[[feedback_always_invoke_speckit_clarify]]`):

- Q: In the engine's lazy-connect model, how is the initiator's outbound sink wired and its initial Logon emitted? → A: **Connect-first, emit-Logon-after.** The connect loop first drives `drive_reconnect_attempt` to establish the live transport; a **symmetric initiator-attach** (inside the private `install_reconnected_transport`, mirroring the acceptor's `attach_accepted_transport`) rebinds `transport_send_` to the live `Transport::async_write`; and ONLY THEN is the initial Logon emitted. The engine-driven initiator does **NOT** emit its Logon at `open()` time — `open()` is restructured so the Logon-emit no longer fires for an engine-driven initiator. (013/014 emitted at `open()` only because the per-session-direct model bound `transport_send` at config time — invalid under lazy-connect.) Grounded in **QuickFIX-cpp** (`Initiator::getSession` `setResponder`-on-connect → `Session::next()` → `generateLogon`, Session.cpp:139) and **Fix8** (`Session::start` → `_connection->connect()` → `send(generate_logon)`, runtime/session.cpp:180/201): both emit the Logon strictly **after** the connection and outbound sink exist.
- Q: How does the engine connect loop access the initiator reconnect driver + live transport (SC-010 public-surface delta)? → A: **Two new public `Session` methods** — `Session::drive_reconnect()` (awaitable wrapper over the private `reconnect_fsm_.drive_reconnect_attempt()`) and `Session::live_transport()` (live-transport accessor for the read-pump) — mirroring the **public** `attach_accepted_transport` precedent (avoids the half-restructure asymmetry per `[[feedback_half_restructure_symmetric_api]]`). SC-010 is updated to enumerate both.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Live acceptor production path (accept → Session → live identity → authorize) (Priority: P1)

A deployed FIX **acceptor** MUST listen for inbound connections, accept a peer, complete the TLS handshake, create (or resolve) the matching session, and feed the live byte stream into that session — with the authenticated peer identity (not a test override or fabricated stand-in) driving the CompID authorization decision at the acceptor Logon gate.

**Why this priority**: This is the structural mirror of 014's live *initiator* path and the half of **T-041** that 014 explicitly deferred. 013 made the acceptor Logon gate fail CLOSED under mTLS-without-identity; 014 supplied a live identity on the initiator path only, leaving the acceptor gate operable solely through the `logon_peer_identity_override` test seam. 015 supplies the real `handshake_result.peer_id` on the live acceptor path, making the acceptor gate operable in production (admit on-list, fail-close off-list/absent) and removing the seam dependence — the final step that lets **T-041** close for both roles.

**Independent Test**: Stand up a live acceptor and connect a test initiator over real TLS yielding a known peer identity; assert an on-list identity admits the session and an off-list/absent identity fails the acceptor Logon CLOSED with `session_compid_unauthorized`.

**Acceptance Scenarios**:

1. **Given** a running acceptor listening for connections, **When** an initiator connects and completes a TLS handshake with an on-list peer identity, **Then** the acceptor creates/resolves the session, admits it at the Logon gate, and the session reaches its established state.
2. **Given** a running acceptor under mTLS, **When** an initiator connects with an off-list or absent peer identity, **Then** the acceptor Logon gate fails CLOSED with `session_compid_unauthorized` and emits `session_event_compid_authorization_failed`.

---

### User Story 2 - Continuous inbound read-pump for both roles (Priority: P1)

Once a session is established (whether the local role is initiator or acceptor), the engine MUST continuously read the live transport and feed every inbound frame to `Session::on_inbound_frame`, in order and on the session strand, until the session closes — not a single frame, as 014 proved on the reconnect path.

**Why this priority**: A session that only consumed one inbound frame would never process the steady-state message flow (heartbeats, resend handling, and the rest of the admin/session-layer protocol). The continuous read-pump is what turns the wired-up transport into a live, running session. It is the shared engine substrate both roles depend on. (Scope note: 015 has no user sink for inbound *application* messages — the `Application` callback surface is out of scope, FR-013; the read-pump delivers every frame to `Session::on_inbound_frame`, which stores it and runs the admin/session-layer handling. App-message delivery to a user is a Phase-5 concern.)

**Independent Test**: Drive a stream of N inbound frames into an established session's transport and assert all N are dispatched to `Session::on_inbound_frame`, in order, on the session strand, with no drops, duplicates, or off-strand dispatch.

**Acceptance Scenarios**:

1. **Given** an established session, **When** the peer sends a sequence of frames, **Then** each frame is delivered to `Session::on_inbound_frame` exactly once, in arrival order, on the session strand.
2. **Given** an established session whose transport reports end-of-stream or a read error, **When** the read-pump observes it, **Then** the pump stops and the session transitions through its existing disconnect handling (no new disposition).

---

### User Story 3 - Programmatic multi-session lifecycle via a SessionConfig-keyed registry (Priority: P1)

An operator-facing program MUST be able to construct the runtime engine, register multiple sessions from their `SessionConfig`s, start the engine (initiators begin their connect loops; acceptors begin listening), and stop it cleanly — with each session addressable in a registry keyed by its `SessionConfig` identity.

**Why this priority**: This is the public engine surface that productionizes both roles. Without it, sessions can only be driven one-off through internal wiring (014). Multi-session lifecycle (create/start/stop) keyed by `SessionConfig` is the minimum viable public runtime.

**Independent Test**: Register two sessions (one initiator, one acceptor) from distinct `SessionConfig`s, start the engine, observe both run their respective loops, then stop the engine and assert clean teardown (no leaks, no dangling work, cancellation-safe) and that each session is retrievable from the registry by its `SessionConfig` key.

**Acceptance Scenarios**:

1. **Given** an engine with two registered sessions, **When** the engine is started, **Then** the initiator begins its connect loop and the acceptor begins listening, and both are retrievable from the registry by `SessionConfig` key.
2. **Given** a running engine, **When** it is stopped, **Then** all sessions and loops tear down cleanly (no leaked work, no use-after-free, cancellation-safe) and a second stop is a no-op.
3. **Given** two `SessionConfig`s that resolve to the same session identity, **When** both are registered, **Then** the duplicate is rejected (registry keys are unique).

---

### User Story 4 - Test-seam removal and full T-041 closure (Priority: P1)

The test-only `logon_peer_identity_override` authorization seam MUST be removed from the production surface once the live acceptor path supplies the real handshake identity to `authorize()`, and catalogue row **T-041** MUST flip from `implementing` to `done` (both roles fail-CLOSED in production with live identities).

**Why this priority**: The seam is the last piece of scaffolding standing in for a live acceptor identity. Removing it — and proving every binding-logic test now runs against a live identity rather than the override — is what makes **T-041** genuinely closed rather than partially wired. Leaving the seam in place would re-introduce the half-fix asymmetry that 014 documented per `[[feedback_half_restructure_symmetric_api]]`.

**Independent Test**: Confirm `logon_peer_identity_override` no longer exists on the public/config surface; confirm the acceptor and initiator binding-logic tests (on-list / off-list / absent) drive `authorize()` from a live handshake identity; confirm the catalogue T-041 row reads `done`.

**Acceptance Scenarios**:

1. **Given** the live acceptor path supplies the real handshake identity, **When** the codebase is searched for `logon_peer_identity_override`, **Then** the seam is absent from the production surface and no test depends on it.
2. **Given** both roles' Logon gates now bind a live identity, **When** the feature catalogue is inspected, **Then** **T-041** reads `done`.

---

### Edge Cases

- **Acceptor connection → SessionConfig matching**: an inbound connection is associated with exactly one session by the *reversed* SenderCompID/TargetCompID of its inbound Logon (the FR-002 SessionID tuple). Routing is **static** for this feature: an inbound Logon matching no registered acceptor session is ALWAYS rejected at the connection level (`session_unknown_acceptor_session`, no session created) — there is no fail-open path. The optional dynamic-session-provider hook is deferred (R2); when later configured it may create the session on first Logon, still subject to the identical FR-006/FR-007 authorization gate. (Confirmed against QFC reverse-route + QFJ Static/Dynamic providers during `/speckit-clarify`.)
- **Acceptor TLS handshake + live identity source**: `Listener::async_accept()` returns a TCP-connected transport with TLS NOT yet issued and no peer identity; the accept loop itself runs `async_handshake` and harvests the live `handshake_result.peer_id`, then sets it on the session (via the acceptor attach primitive) strictly before the Logon gate runs. This is the sole source of the live acceptor identity that closes T-041.
- **Authorization failure on the acceptor path**: dispositioned by the *open*-path rule (013's terminal fail-CLOSED at the acceptor Logon gate), not the reconnect retry-to-cap rule (which is the initiator reconnect path from 014). The two dispositions are inherited unchanged.
- **Engine stop while a handshake or reconnect attempt is in flight**: teardown is cancellation-safe; an in-flight attempt is aborted without leak or use-after-free (per the asio total-cancellation lesson — a coroutine that must honor root-total teardown resets to enable total cancellation).
- **Concurrent sessions sharing the engine executor**: per-session work stays on its own strand; no cross-session data races.
- **Non-mTLS acceptor path**: genuinely permissive (the three-way guard's non-mTLS skip), unchanged and out of scope for the binding logic.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The system MUST expose a public runtime engine that constructs, registers, starts, and stops FIX sessions for both the initiator and acceptor roles. The engine MUST run on a **caller-supplied asio executor** (e.g. an `io_context`) and MUST NOT own or spawn worker threads; the caller drives the executor. `start()` MUST be **non-blocking** — it launches the connect/accept loops and returns.
- **FR-002**: The engine MUST maintain a session registry keyed by the **FIX SessionID tuple** (BeginString + SenderCompID + TargetCompID) derived from each `SessionConfig`; two `SessionConfig`s resolving to the same tuple are duplicates and the second registration MUST be rejected.
- **FR-003**: Starting the engine MUST begin each initiator session's connect loop (driven by 014's reconnect/connect path) and each acceptor session's accept loop (listening for inbound connections), launched on the caller-supplied executor. On the initiator path the establishment ordering MUST be **connect-then-Logon**: the connect loop first drives `drive_reconnect_attempt` to establish the live transport and (via the symmetric initiator-attach inside `install_reconnected_transport`) rebinds `transport_send_` to the live `Transport::async_write`, and ONLY THEN is the initial Logon emitted — the engine-driven initiator MUST NOT emit its Logon at `open()` time (matching QuickFIX-cpp/Fix8, which send the Logon only after the connection and outbound sink exist; Clarifications 2026-05-31). The engine drives the initiator handshake loop and reads the live transport via the public `Session::drive_reconnect()` and `Session::live_transport()` methods (symmetric to the acceptor's public `attach_accepted_transport`).
- **FR-004**: For every established session of either role, the engine MUST continuously read the live transport and deliver each inbound frame to `Session::on_inbound_frame`, in arrival order, on the session strand, until the session closes.
- **FR-005**: The acceptor MUST, on accepting a connection, **drive the TLS handshake itself** (the `Listener::async_accept()` result is TCP-connected with TLS not yet issued and carries no peer identity — the accept loop runs `async_handshake` and harvests `handshake_result.peer_id`), then resolve the matching session and feed the live byte stream into it (the `accept → handshake → Session-resolve → byte-feed` production path). Session resolution MUST use **static pre-configured matching**: the inbound Logon is matched by its *reversed* SenderCompID/TargetCompID to a registered acceptor `SessionConfig` (the FR-002 tuple); an unmatched Logon MUST be rejected at the connection level with NO session created — routing is static for this feature, with no fail-open path. An optional dynamic-session-provider hook is deferred (R2); if later added, a dynamically-created session MUST route through the identical authorization gate (FR-006/FR-007). The acceptor's bind/listen endpoint is sourced from the acceptor `SessionConfig`'s `reconnect_endpoint` (repurposed as the bind endpoint on the acceptor role); the engine builds the per-acceptor `Listener` from it and exposes the OS-assigned bound port via `Engine::acceptor_bound_endpoint(SessionId)` (decided at implement 2026-05-30, user-approved).
- **FR-006**: On the live acceptor path, the post-handshake authenticated peer identity (not the test override or a fabricated stand-in) MUST be the source that drives the CompID authorization decision at the acceptor Logon gate.
- **FR-007**: The acceptor CompID authorization decision MUST be fail-CLOSED under mTLS when the identity is absent or off-list, mirroring the initiator path and inheriting 013's semantics.
- **FR-008**: The canonical identity-extraction order (CN→SAN-DNS→SAN-URI→SHA-256) and the `session_event_compid_authorization_failed` / `session_compid_unauthorized` event and code shapes MUST be inherited unchanged from 013/014 (no new shapes).
- **FR-009**: The test-only `logon_peer_identity_override` seam MUST be removed from the production surface; no production code path and no test MUST depend on it after the live acceptor path lands.
- **FR-010**: Catalogue row **T-041** MUST flip from `implementing` to `done` once both roles bind a live identity and fail CLOSED in production.
- **FR-011**: Stopping the engine MUST tear down all sessions, loops, listeners, and in-flight work cleanly — no leaks, no use-after-free, cancellation-safe (including total-cancellation teardown of in-flight handshake/reconnect attempts). A second stop MUST be a no-op.
- **FR-012**: The engine MUST NOT alter the established disposition rules: the *open*-path acceptor Logon failure stays terminal fail-CLOSED (013); the initiator *reconnect*-path authorization failure stays retry-to-cap (014). 015 adds no new disposition.
- **FR-013**: The feature MUST remain bounded below the Phase-5 service wrapper — no configuration-file parsing, no `Application`-style user callbacks, no store/log factory abstractions, and no C ABI / control-plane / observability / pybind surface. (Consequently there is **no user sink for inbound *application* messages** in 015; the read-pump delivers every frame to `Session::on_inbound_frame`, which stores it and processes admin/session-layer types internally — see FR-004. App-message delivery to a user is Phase-5.)
- **FR-014**: The engine MUST bound the pre-session window on an inbound acceptor connection — both the TLS handshake and the first-frame (Logon) read — with a maximum byte budget AND a handshake/Logon deadline. A peer that exceeds either (slow-loris, partial frame, or an unbounded stream before a valid Logon) MUST be closed and its accept slot reclaimed, without affecting other peers. This window precedes any `Session` object, so it cannot rely on per-session limits; it is owned by an engine-level accept-scope cancellation domain wired into `stop()`.

### Key Entities

- **Runtime engine**: the public component bound to a caller-supplied executor, owning the session registry and the per-role loops (connect loops for initiators, accept loops for acceptors); the start/stop lifecycle owner. Does not own worker threads.
- **Session registry**: the collection of live sessions keyed by the FIX SessionID tuple (BeginString + SenderCompID + TargetCompID); the addressing/lookup surface and the duplicate-rejection authority.
- **Accept loop**: the acceptor-side loop that accepts inbound connections, completes handshakes, and resolves each to a session by reversed-CompID SessionID match (static default) or via the optional dynamic-session-provider hook.
- **Read-pump**: the continuous inbound reader that feeds `Session::on_inbound_frame` for an established session of either role.
- **Live peer identity**: the authenticated TLS identity extracted in canonical order on the acceptor path; the authorization input that closes T-041.

## Success Criteria

### Measurable Outcomes

- **SC-001**: A running acceptor accepts an inbound connection, completes the handshake, binds it to a session, and admits an on-list live identity to its established state.
- **SC-002**: An off-list or absent live identity on the acceptor path fails the Logon CLOSED with `session_compid_unauthorized` and emits `session_event_compid_authorization_failed`.
- **SC-003**: An established session of either role consumes a stream of inbound frames continuously — all delivered to `Session::on_inbound_frame`, in order, on-strand, with no drops or duplicates.
- **SC-004**: Multiple sessions can be registered, started, and stopped through the public engine on a caller-supplied executor; each is addressable by its FIX SessionID tuple and a `SessionConfig` resolving to an already-registered tuple is rejected.
- **SC-005**: Engine stop tears down all sessions and loops cleanly under the full sanitizer matrix (no leaks, no UAF, cancellation-safe); a second stop is a no-op.
- **SC-006**: `logon_peer_identity_override` is absent from the production surface and no test depends on it.
- **SC-007**: Catalogue row **T-041** reads `done`; both roles bind a live identity and fail CLOSED in production.
- **SC-008**: The full sanitizer ctest matrix is green (ASan/UBSan/TSan), 0 findings.
- **SC-009**: Out-of-scope items (Phase-5 service wrapper, C ABI, control-plane, observability, pybind) are not present.
- **SC-010**: The public surface delta is limited to the documented runtime-engine additions and the seam removal (the `Engine` type, the `SessionId` value type, `Session::attach_accepted_transport`, the initiator-path public methods `Session::drive_reconnect()` and `Session::live_transport()`, the `Engine::acceptor_bound_endpoint` accessor, error slot 121, and the removed `logon_peer_identity_override` seam — nothing more).
- **SC-011**: An inbound acceptor connection that stalls the TLS handshake, sends nothing, sends a partial first frame, or sends more than the first-frame byte budget before a valid Logon is closed within the configured deadline, its transport and accept slot are reclaimed (no leak under sanitizers), and other peers are unaffected (FR-014).

## Assumptions

- **Acceptor session matching convention** (clarified 2026-05-30): static pre-configured matching is the default — an inbound connection is matched to a registered acceptor `SessionConfig` by the reversed SenderCompID/TargetCompID of its inbound Logon (the FR-002 SessionID tuple; QuickFIX-cpp `Acceptor::getSession` reverse-route + QuickFIX-J `StaticAcceptorSessionProvider`). An **optional dynamic-session-provider hook** (mirroring QFJ `DynamicAcceptorSessionProvider`) may create a session on first Logon and is bounded as an opt-in extension — the static path carries the T-041 fail-CLOSED proof; the dynamic path reuses the same authorization gate. Confirmed against the reference engines per `[[feedback_always_invoke_speckit_clarify]]`.
- **Engine concurrency & lifecycle model** (clarified 2026-05-30): the engine binds to a caller-supplied asio executor (e.g. an `io_context`) and owns no worker threads; `start()` is non-blocking (launches loops and returns); `stop()` is idempotent and cancellation-safe. Per-session work runs on its own strand over that executor; the engine adds no new threading primitives beyond the 007 threading/clock and 006 async-mutex surfaces already in the tree.
- **Reuse of 014's per-session wiring**: the initiator connect path reuses 014's realized `drive_reconnect_attempt` loop and live `handshake_result.peer_id → authorize()` binding; 015 wraps it in the public engine and adds the symmetric acceptor path. Beyond pure wrapping, 015 also restructures the initiator's **outbound** establishment to the engine's lazy-connect model: the live `transport_send_` is rebound at connect (symmetric initiator-attach) and the initial Logon is emitted **post-connect**, not at `open()` (Clarifications 2026-05-31; FR-003). This corrects the 013/014 per-session-direct assumption (`transport_send` bound at config time) which does not hold when the engine connects lazily.
- **012/013/014 surfaces are merged and stable**: the transport surface (asio_tls_transport, asio_listener, TransportFactory), the reconnect FSM + CompID policy, and the per-session live wiring are all present on `main`.
- **No new error slots are required by the binding logic** (it inherits 013/014 codes); any engine-lifecycle code needs are minimized and justified at `/speckit-plan`.
- **`Session::on_inbound_frame` and the strand model are the integration points**; the engine does not change session-internal FSM behavior.

## Out of Scope

- **Phase-5 service wrapper** — configuration-file parsing, `Application` user callbacks, store/log factory abstractions, C ABI. Far out of scope.
- **Control-plane, observability (log/OTel), TAP, C-API, and pybind surfaces** — later phases.
- **Non-mTLS permissive binding path** — genuinely permissive (three-way guard non-mTLS skip), unchanged.
- **New FIX message semantics, recovery sub-protocol changes, or FSM disposition changes** — 015 wires the engine; it does not change 005/009/013 session behavior.
- **The per-release interop matrix** (fixpp vs QuickFIX-cpp / QuickFIX-J / Fix8) — unblocked *by* 015 but executed afterward per `[[project_release_interop_quickfix_fix8]]`; it is not part of this feature.

## Dependencies

- 012 transport surface (merged) — `Transport`/`TlsTransport`, `asio_tls_transport`, `asio_listener`, `TransportFactory`, `ReconnectPolicy`.
- 013 reconnect FSM + CompID authorization policy + recovery sub-protocol (merged).
- 014 per-session live wiring (merged) — initiator `drive_reconnect_attempt` loop + live `handshake_result.peer_id → authorize()` + real `credentials_rotated`.
- 015 builds on all three; no new external dependencies.

## Normative References

Per `[const §VI.5]`, the exact normative entries informing this spec:

- `[FIXS §4.4]` *FIX Session Layer — Authorization linked to authentication* — drives FR-006/FR-007/FR-008 and the **T-041** binding (authenticated TLS identity → authorized FIX CompID).
- `[FIX-SL §4.2.2]` *Logon / CompID identification* — drives FR-002 (SessionID tuple) and the reversed-CompID acceptor resolution in FR-005.
- `[FIX-SL §4.3]` *Connection establishment* — drives FR-003/FR-005 (accept → handshake → resolve → byte-feed) and FR-014 (bounded pre-session window).
- `[FIX-SL §4.5.2]` *Heartbeat / steady-state message flow* — drives FR-004 (continuous read-pump).
- Inherited unchanged from 013/014 (the binding semantics, canonical CN→SAN-DNS→SAN-URI→SHA-256 extraction order, and the `session_event_compid_authorization_failed` / `session_compid_unauthorized` shapes): see the 013/014 specs' Normative References; 015 adds no new FIX-protocol semantics (FR-008/FR-012).

## Anchors / Catalogue

- `[FIXS §4.4]` *Authorization linked to authentication* — the binding closed by **T-041** (authenticated TLS identity must map to an authorized FIX CompID). After 015, **T-041 → `done`** for both roles.
- Carve-out authority: `specs/014-transport-active-binding/spec.md` Out of Scope §1 (line 153) + `plan.md` carve-out (line 28); split decision `[[project_014_015_split]]`.
- Inherited unchanged from 013/014: the three-way authorization guard, canonical identity extraction order, and `session_event_compid_authorization_failed` / `session_compid_unauthorized` shapes.
- Standing lessons to carry into implementation: full sanitizer ctest before sign-off (`[[feedback_gateb_full_sanitizer_before_signoff]]`), asio total-cancellation teardown (`[[feedback_asio_cospawn_total_cancellation_default]]`), symmetric-API completeness (`[[feedback_half_restructure_symmetric_api]]`).
