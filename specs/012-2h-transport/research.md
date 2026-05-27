# Phase 0 Research — 012-2h-transport

**Branch**: `012-2h-transport` | **Date**: 2026-05-27 | **Plan**: [plan.md](plan.md) | **Spec**: [spec.md](spec.md)

**Scope**: enumerate every binding decision (D-1..D-N) inherited from `.specify/2h-transport.md` v0.3 + the 5 `/speckit-clarify` resolutions on 2026-05-27 + the 011 Gate B F-1 carryover. Each decision is recorded as **Decision / Rationale / Alternatives considered**, with verbatim cites to the design-doc / constitution / architecture / sibling-doc anchors that bind it. The plan and the contracts re-emit these decisions; this doc is the central reference.

No NEEDS CLARIFICATION items remain. The `/speckit-clarify` pass on 2026-05-27 resolved all 5 ambiguities; the `[2h §10]` Open Questions table records 7 DEFERRED items, none of which impacts v1.0 scope, security, or UX such that a `[NEEDS CLARIFICATION]` marker would be load-bearing.

---

## §1 Security & TLS — central decisions

### D-1 — OpenSSL on Linux + Windows; no Schannel; no platform-native fallback

**Decision**: `asio_tls_transport` wraps `asio::ssl::stream<tcp::socket>` over **OpenSSL 3.x** on BOTH Linux and Windows.

**Rationale**: `[const §XII.1]` locked 2026-05-06 — Schannel dropped. `[2h §1] / [2h §4.5]` re-states. `asio::ssl::stream` is OpenSSL-only in the ASIO standalone build; no third-party TLS backend.

**Alternatives considered**:
- **Schannel on Windows** — rejected at constitution sign-off (2026-05-06); two TLS backends double the test matrix and create version-skew risk for FIXS-RC1 conformance.
- **wolfSSL / BoringSSL** — rejected (`[const §XII.1]` pins OpenSSL); BoringSSL has no stable API and is Google-internal-first; wolfSSL would force an additional Conan row + audit per `[const §III]`.

### D-2 — TLS posture pinned: 1.3 preferred + 1.2 ECDHE-AEAD fallback; renegotiation / compression / tickets / early-data off

**Decision**: `asio_tls_transport` configures `SSL_CTX` per `[2g §4.5.1]` mapping table EXACTLY: `SSL_CTX_set_min_proto_version = TLS1_2_VERSION`, `SSL_CTX_set_max_proto_version = TLS1_3_VERSION`. Sets `SSL_OP_NO_RENEGOTIATION | SSL_OP_NO_COMPRESSION | SSL_OP_NO_TICKET | SSL_OP_NO_EARLY_DATA` at `SSL_CTX` construction.

**Rationale**: `[FIXS §3.2]` + `[const §XII.3]` — no 0-RTT (early data), no compression (CRIME), no renegotiation (multiple TLS-layer attacks), no session tickets (TLS 1.2 session tickets violate FIXS RC1's per-session secrecy requirement). The TLS-version posture is CONSUMED from 011's mapping table (`[2g §4.5.1]` is normative); 2h does NOT re-choose it (FR-015).

**Alternatives considered**:
- **Permit TLS 1.2 only** — rejected; FIXS RC1 explicitly endorses TLS 1.3 and v1.0 named venues are TLS-1.3-capable. Forcing 1.2-only would handicap performance.
- **Permit early data for known counterparties** — rejected per `[const §XII.3]`; 0-RTT replay attack surface is unacceptable for an order-routing engine.

### D-3 — PSK rejected at v1.0 default impl with `transport_psk_unsupported`

**Decision**: `asio_tls_transport` REJECTS PSK configuration (any caller-supplied `SslCtxConfig` setting `SSL_CTX_set_psk_*` callbacks) with `transport_psk_unsupported`. The `TlsTransport` sub-interface leaves 4 of 5 pure-virtual slots free for a post-v1 PSK callback hook without a major-version bump.

**Rationale**: T-012 is P2 deferred per `[const §XII.6]`. `[2h §4.5]` + FR-017 codify the v1.0 refusal. The headroom-as-design-decision is consistent with `[const §XIV.2]`'s "≤5 pure-virtuals" cap left as MARGIN.

**Alternatives considered**:
- **Ship PSK in v1.0** — rejected; PSK's operational surface (key distribution, rotation, lifetime) is its own design feature and would expand 2h's scope by ~30 %.
- **No headroom in `TlsTransport`** — rejected; without the empty slots a post-v1 PSK addition becomes a major-version bump.

### D-4 — 011 F-1 cancellation-seam witness BINDING for 012 Gate B (8-cell matrix)

**Decision**: FR-033 + SC-008 pin the `cancellable_dispatch` recipe witness for `file_cert_source::load_credentials` as an **8-cell test matrix: 4 deterministic cases × 2 executor modes** at THIS feature's Gate B (Clarifications Q3=C). Cells:
1. Cached-state fast path per `[2g §4.5]:926-929` — no dispatch, returns cached `local_credentials`.
2. Slot signalled BEFORE handler picked up per `[2d §6.5]` case 1 → `tls_load_cancelled`.
3. Slot signalled DURING handler execution per `[2d §6.5]` case 2 → `tls_load_cancelled`.
4. Slot NOT signalled (happy path) per `[2d §6.5]` case 3 → success.

Executor modes: `per_session_strand` (`make_session_executor` wrapping `asio::make_strand(resolved_exec)`) and `direct_executor` (bare attested-serialised). All 8 cells MUST be GREEN.

**Rationale**: `[[project_011_tls_policy_closed.md]]` records that 011 Gate B waived F-1 to 2h's Gate B because constructing the witness required a real `Session*` + `session_executor` fixture only available once the transport wiring exists. Clarifications Q3=C resolved the granularity (4 deterministic cases × 2 executor modes, matching the waiver text verbatim — "per_session_strand AND direct_executor matrix"). The 4 cases bind to `[2d §6.5]` cases 1-3 + the `[2g §4.5]:926-929` cached-state authorization.

**Alternatives considered**:
- **End-to-end witness only (1 cell)** — rejected; the 011 waiver explicitly required the matrix.
- **Per-deterministic-case (4 cells, single executor)** — rejected; the waiver text required BOTH executor modes.
- **Per-hop granularity** — over-spec; the 4 deterministic cases of `cancellable_dispatch` already enumerate the propagation surface.

---

## §2 Reconnect — fresh-Transport-per-attempt + QuickFIX/J-aligned schedule

### D-5 — Reconnect mints a FRESH `Transport` via the same `TransportFactory` per attempt (Clarifications Q1=B)

**Decision**: On each reconnect retry the session FSM destroys the dead `Transport`, sleeps for `delay_for_attempt(n)`, mints a fresh `Transport` via `TransportFactory::make(...)` (the same factory frozen at session open per FR-028), and calls `async_connect` on the new instance. The previous `Transport` is destroyed BEFORE the new one is requested; the FSM holds at most one live `Transport` per session at any time.

**Rationale**: Reference-engine sweep (Clarifications 2026-05-27 Q1):
- **QuickFIX-cpp** `SocketInitiator::doConnect:155-180` mints a new `SocketConnection` per attempt.
- **QuickFIX/J** `IoSessionInitiator::connect():238-272` mints a new `IoSession` per attempt.
- **Fix8** `ReliableClientSession::operator()` mints `new Poco::Net::StreamSocket` + `new ClientConnection` per attempt.

All three reference engines converge on the fresh-per-attempt pattern over 20+ years. No FIX protocol clause constrains this. Per `[[project_release_interop_quickfix_fix8]]`'s "stay close to reference engines unless protocol/complaints force otherwise" rule, 012 aligns with industry. The design doc's v0.3 wording ("re-issue `async_connect` against the same `Transport` instance") was made WITHOUT a reference-engine sweep and diverged silently — this is the kind of divergence `[[feedback_always_invoke_speckit_clarify]]` records (Q1 reference-engine sweep precisely caught the divergence before `/plan` locked it).

Drives Appendix D §D.4 amendment to `.specify/2h-transport.md` §1 item 5 + §1.3 + §4.9 + §6.4 reconnect wording. Per FR-026, the factory-level caching contract (`SslCtxConfig` carrying OpenSSL `SSL_CTX*`, the engine's PMR root resource, the engine clock) is preserved — long-lived state shared across attempts is cached at the factory level, NOT rebuilt per attempt. The per-attempt mint cost is bounded by the back-off envelope, NOT by `make(...)` runtime.

**Alternatives considered**:
- **Option A (re-issue `async_connect` on same Transport)** — rejected as the design doc's original choice; reference-engine sweep showed all 3 industry impls use the fresh-per-attempt pattern. Reusing the same instance complicates the OpenSSL `SSL*` cleanup (must call `SSL_free` + reset stream state between attempts) and the ASIO socket re-bind dance, and doesn't match operator mental models from QFC/QFJ/Fix8.
- **Hybrid (reuse on `connect_refused`, fresh on TLS failure)** — over-spec; no reference engine does this; adds a state-machine branch with no operational benefit.

### D-6 — `ReconnectPolicy` adopts QuickFIX/J schedule-array API shape with v0.2 exponential defaults (Clarifications Q2=C)

**Decision**: `ReconnectPolicy` value type carries:
- `std::pmr::vector<std::chrono::milliseconds> schedule` (required; ≥ 1 entry; per-attempt delay table indexed by attempt-number, with LAST entry plateau).
- `double jitter` (range `[0.0, 1.0]`; default `0.0` on raw construction).
- `std::uint32_t max_attempts` (default `0` on raw construction = unbounded).

The legacy v0.2 fields `initial_delay` / `max_delay` / `multiplier` are NOT present — the materialised schedule replaces them.

`ReconnectPolicy::defaults()` materialises the v0.2 exponential schedule as the default array `[100 ms, 200 ms, 400 ms, 800 ms, 1.6 s, 3.2 s, 6.4 s, 12.8 s, 25.6 s, 30 s]` (the last entry caps the per-attempt delay at `max_delay = 30 s` — entries beyond would have exceeded it and are clamped) with `jitter = 0.10` (±10 %) + `max_attempts = 10`. Cumulative envelope at `max_attempts = 10`: 73 – 89 s wall-clock with the ±10 % jitter spread.

`ReconnectPolicy::defaults_quickfix_compat()` returns `{schedule = [30 s], jitter = 0.0, max_attempts = 0}` for operators who want fully industry-canonical QuickFIX-cpp / Fix8 behaviour (single fixed 30 s interval, no jitter, no cap).

`delay_for_attempt(n) = schedule[std::min(n, schedule.size() - 1)] * (1 + uniform_real(-jitter, +jitter))`. The plateau-at-last semantics match QuickFIX/J `IoSessionInitiator::computeNextRetryConnectDelay():318-319` (`if (index >= reconnectIntervalInMillis.length) millis = reconnectIntervalInMillis[reconnectIntervalInMillis.length - 1]`). Jitter is deterministic-per-attempt for test repeatability (seeded from session id + attempt number).

Exceeding `max_attempts` surfaces `transport_reconnect_limit_exceeded`. Operators MUST opt in to unbounded reconnect via `max_attempts = 0` explicitly per `[const §XII.5]` no-implicit-default rule.

**Rationale**: Reference-engine sweep (Clarifications 2026-05-27 Q2):
- **QuickFIX-cpp** single fixed `m_reconnectInterval = 30` (no jitter, no cap).
- **QuickFIX/J** user-supplied `int[] reconnectIntervalInSeconds` indexed by failure count with plateau-at-last (no jitter, no cap by default).
- **Fix8** single fixed `_login_retry_interval` + optional `_login_retries` cap with 0 = unbounded (no jitter).

Industry uses fixed intervals without jitter and without default cap. Constitutional `[const §XV]` thundering-herd-pattern intent is the documented in-codebase "complaint" that permits divergence per the conditional rule. Hybrid C wins on three axes: (a) API a QFJ user reads at first glance (schedule array indexed by attempt); (b) constitutional defense preserved in `defaults()` (jitter + cap); (c) opt-out via `defaults_quickfix_compat()` returns industry-canonical behaviour exactly. Drops the `multiplier` field (implicit in the materialised schedule).

Drives Appendix D §D.5 amendment to `.specify/2h-transport.md` §4.4 / §4.4.1.

**Alternatives considered**:
- **Option A (pure industry-aligned)** — fixed interval, no jitter, no cap. Rejected; `[const §XV]` thundering-herd ban is a documented constitutional defense.
- **Option B (design-doc exponential)** — `{initial_delay, max_delay, multiplier, max_attempts, jitter}` 5-field shape. Rejected; the materialised schedule covers the same envelope with a more readable API.
- **Hybrid B (industry shape on top of `{schedule}` only; no jitter / cap fields)** — rejected; loses the constitutional defense.

The v0.3 design doc's 81 100 ms pre-jitter cumulative wall-clock claim was a numeric error in `[2h §4.4.1]` (corrected to 73 – 89 s in design-doc §1.1 prose). The corrected band is anchored by `[2h §1.1]`'s sentence "≈ 73 – 89 s wall-clock with the ±10 % jitter spread"; the bench `tests/transport/test_reconnect_policy_schedule.cpp` (seam #13) pins the corrected band, NOT the v0.3 §4.4.1 numeric.

---

## §3 Listener cancel scope — async-coroutine-natural ownership semantics

### D-7 — `Listener::cancel()` does EXACTLY three things (Clarifications Q4=A)

**Decision**: `Listener::cancel()` does three things and ONLY those three:
1. **Close the listening socket** so no new TCP connections complete (subsequent client connects receive TCP RST or connection-refused per OS).
2. **Complete any in-flight `async_accept` awaitable not yet resumed** with `listener_accept_cancelled` per `[2h §6.6]` (the `cancellation_type::total` slot fires on `asio::ip::tcp::acceptor::async_accept`; pending awaitables receive `operation_aborted` which 2h maps to the named variant).
3. **Leave already-resumed-but-not-yet-consumed `unique_ptr<Transport>` results UNAFFECTED** — ownership has passed at the `async_accept` return; the listener has no handle to them.

A consumer that wants the stronger "close everything I produced" semantics MUST track its own held Transports and call `transport.close()` on each — that is the consumer's contract, NOT the listener's.

**Rationale**: Reference-engine sweep (Clarifications 2026-05-27 Q4):
- **QuickFIX-cpp** `SocketAcceptor::onStop():146-150` closes only the listening socket.
- **Fix8** `~ServerSessionBase():511-514` only deletes server socket.
- **QuickFIX/J** `AbstractSocketAcceptor.stopAcceptingConnections():268-278` also kills managed sessions — BUT that kill happens at the SESSION layer, not the Transport layer (in our model, Session is the next-feature Phase-4 concern; QFJ's pattern doesn't translate down to Listener).

2 of 3 reference engines + the natural async-coroutine ownership semantics converge on A. Once `async_accept` resumes with a `unique_ptr<Transport>`, ownership has transferred to the consumer; the Listener has no surface to close those Transports (would require maintaining a back-reference table, which complicates lifetime AND introduces the lifecycle bug — what if the consumer has already passed the Transport into Session?).

FR-025 spells out the three sub-cases; Edge Cases gains an explicit "Listener.cancel() after async_accept has resumed" entry.

**Alternatives considered**:
- **Option B (track all produced Transports; close them on cancel)** — rejected; introduces a back-reference table, lifecycle complexity, and a closing-during-Session-open race. No reference engine does this at the listener level.
- **Option C (signal via cancellation_state propagation)** — over-engineered; ASIO's cancellation_slot doesn't reach already-resumed-but-not-yet-consumed values cleanly. Would require a custom cancellation channel on every produced Transport.

---

## §4 TCP socket-option defaults — Nagle OFF, no linger

### D-8 — `Transport::Config::tcp_nodelay = true`; `so_linger_enabled = false` (Clarifications Q5=A)

**Decision**: `Transport::Config::tcp_nodelay` defaults `true` (Nagle OFF). `Transport::Config::so_linger_enabled` defaults `false` (no linger).

**Rationale**: Reference-engine sweep (Clarifications 2026-05-27 Q5):
- **QuickFIX/J** `NetworkingOptions.java:82` `getBoolean(..., Boolean.TRUE)` — defaults Nagle OFF.
- **Fix8** `configuration.hpp:357` `def=true` — defaults Nagle OFF.
- **QuickFIX-cpp** `SocketServer.cpp` / `SSLSocketAcceptor.cpp:227` — defaults `false` (Nagle ON). HISTORICAL ANOMALY.

QFC's Nagle-ON default is the production-near-universally-overridden anomaly — Nagle's 40 ms delay interacts badly with FIX heartbeats (HeartBtInt=30s leaves 10s slack; missing one HB by 40ms can trip the 90s Test-Request timer chain) and single-tag updates (a 35=8 ExecutionReport with one tag changed gets buffered). 2 of 3 engines + production reality + every named v1.0 venue class converge on Nagle OFF.

For `SO_LINGER`:
- **Fix8** `runtime/connection.cpp:356` pins `setLinger(false, 0)` explicitly.
- **QuickFIX/J** + **QuickFIX-cpp** rely on OS default (typically off on Linux + Windows).

Fix8's explicit `setLinger(false, 0)` pins a defensive default; QFJ/QFC inherit OS default which is also off. Choosing `false` matches all three.

**Alternatives considered**:
- **Defer to OS** — rejected for Nagle; OS default ON is the wrong posture for FIX heartbeats per the reasoning above. For SO_LINGER, OS default off is already aligned with the chosen default, so explicit pin matches all 3 engines.
- **Mirror QFC anomaly (Nagle ON)** — rejected; production overrides converge against it.

These defaults are pinned in `Transport::Config` directly per FR-029, NOT deferred to `/plan` (Clarifications Q5 explicitly resolved against the defer option).

---

## §5 Cross-doc partition — what 2h owns vs what 011 / 2b / 2e / session-Phase-4 / 2i own

### D-9 — `cert_source` / `Pinset` / `CipherPolicy` / `SecurityProfile` / `verify_peer` / `peer_identity` OWNED BY 011 (consumed by 2h UNCHANGED)

**Decision**: 2h consumes these surfaces UNCHANGED:
- `fixpp::tls::cert_source` (`load_credentials` awaitable; `load_trust_anchors` returning `expected_t<std::span<const Certificate>>`).
- `fixpp::tls::Pinset` (`add(Certificate const&)` / `remove(sha256)` / `find(sha256)` / `snapshot()` — the last is the captured-once entry point 2h calls at handshake start).
- `fixpp::tls::CipherPolicy` (compile-time allow-list; `is_allowed` constexpr predicate).
- `fixpp::tls::SecurityProfile` enum + `SslCtxConfig` value type + `make_ssl_ctx_config` factory (the entry point `asio_tls_transport` calls to materialise `SSL_CTX*`).
- `fixpp::tls::verify_peer` predicate (the `SSL_VERIFY_PEER` callback dispatches into it; 2h MUST NOT re-implement validation per FR-012).
- `fixpp::tls::peer_identity` value type (returned in `handshake_result.peer_id`; consumed by session-Phase-4 for T-041 binding).

The 11 `tls_*` error variants from `[2g §6.6]` surface UNCHANGED through 2h — `transport_handshake_failed` joins `[2g §6.6]` `tls_handshake_failed` group at the C ABI per `[2h §6.6]` (2i coalesces), but 2h does NOT re-translate or coalesce them under a `transport_*` prefix (FR-034).

**Rationale**: `[2h §1.2]` + `[2h §3.16]` + spec FR-041 codify the negative-ownership boundary. 011 ships SHIPPED (closed 2026-05-26 per `[[project_011_tls_policy_closed.md]]`); its published surfaces are LOCKED. Re-translating tls_* under transport_* would defeat the per-doc-prefix coalescing discipline established by `[2b §6.7]` / `[2c §6.7]` / `[2d §6.7]` / `[2e §6.7]` / `[2f §6.5]` / `[2g §6.6]`.

**Alternatives considered**:
- **Re-translate `tls_*` → `transport_*` for ergonomics** — rejected; loses observability fidelity (an operator's log shows `transport_handshake_failed` and can't tell whether it was a cipher mismatch, a pin miss, or a chain failure without parsing the diagnostic sub-reason field).
- **Re-emit 011's surfaces under `fixpp::transport::` aliases** — rejected; would create two names for one type, complicate the dependency graph, and break 2g's T-041 cross-cut into session/.

### D-10 — `Framer::feed` / `Writer::commit` OWNED BY 2b (locked); 2h's `async_read_some` + `async_write` map directly

**Decision**: 2h's `async_read_some(std::span<std::byte> buf [[clang::lifetimebound]])` delivers bytes INTO `Framer::feed(std::span<const std::byte>)` per `[2b §4.2]`. 2h's `async_write(std::span<const std::byte> bytes [[clang::lifetimebound]])` consumes bytes FROM the `Writer::commit` post-commit span per `[2b §4.5]`. No 2b API change.

**Rationale**: `[2h §1.2]` codifies. 2b is shipped + locked since the 005/008 session-FSM work. The `[[clang::lifetimebound]]` annotation at the abstract-base declaration site per `[arch §5.5]` propagates the caller-owns-buffer contract through the public surface.

**Alternatives considered**:
- **Composite read returning an owning buffer** — rejected; would allocate on the hot path per request, violating `[const §VIII.5]`. The arena-backed caller-owned buffer pattern matches `[2b §6.6]`'s three-arena pinning.
- **Composite write consuming `Writer&` directly** — rejected; couples the transport to the wire encoder, breaks the mock-transport seam (US4), and complicates the 2i C ABI surface.

### D-11 — Durable-before-transmit invariant OWNED BY 2e (cross-cut binding)

**Decision**: The FSM sequences `toApp → Writer::commit → store(committed_span, outbound) → Transport::async_write`. **`Transport::async_write` MUST NOT be called until the corresponding `MessageStore::store(...)` for the same outbound seqnum has linearised** per `[2e §6.1.4]`. **A cancelled `async_write` MUST NOT trigger any rollback of the persisted frame** — the persisted frame survives, the peer's later `ResendRequest` per `[FIX-SL §4.5.2]` is honourable, and the cancellation surfaces purely as a wire-side abort.

**Rationale**: 2h's surface guarantees this by API CONSTRUCTION — `async_write` does not call into the store, does not own any rollback path, and does not know the seqnum. The FSM (post-this-feature) owns the ordering; 2h is the wire-side hop only. `[2h §6.7]` + spec FR-030 codify. The `[2h §9 seam #8]` (durable-before-transmit ordering, cross-doc with 2e) fault-injects the cancellation mid-write and asserts the persisted frame is intact.

**Alternatives considered**:
- **Transport-owned rollback on cancel** — rejected; would require transport to know about store + seqnum, coupling layers and undermining the FSM's recovery via `ResendRequest`.
- **Pre-store-then-mark-sent** — rejected; introduces a 3-state "pending" entry in the store that complicates `MessageStore::retrieve` for resend paths.

### D-12 — Session FSM (Logon/Heartbeat/Resend/Reset/Logout) + reconnect loop + CompID-to-TLS-identity binding policy OWNED BY session-Phase-4 (not in scope here)

**Decision**: 2h ships:
- The `Transport` + `TlsTransport` interfaces.
- The `ReconnectPolicy` value type + `delay_for_attempt()` helper.
- The `handshake_result.peer_id` value delivery (the FSM reads `peer_id` BY VALUE).

The session FSM module (post-this-feature) ships:
- The Logon → Active → Logout flow (already partially in 005/009/010 catalogue; the transport-coupled half lands in the post-this-feature spec).
- The reconnect LOOP (calls `Transport::cancel()`, `Transport::close()`, then re-mints via factory, then `Transport::async_connect`).
- The CompID-to-TLS-identity binding policy (consumes `peer_id.subject_dn_view()` / `peer_id.san_dns_view()` and applies operator-supplied mapping).

**Rationale**: 2h is the wiring layer; session-Phase-4 is the protocol layer. `[2h §7.2]` + spec FR-041 codify. The `[2h §9 seam #9]` (CompID-to-TLS-identity round-trip) tests the transport-delivery side; the session-Phase-4 spec will test the binding-policy side.

**Alternatives considered**:
- **Ship the FSM here** — rejected; 2h would balloon to 4 k+ LoC and couple two distinct architectural concerns. Industry pattern (QuickFIX-cpp `SocketInitiator` + `Session`; QuickFIX/J `IoSessionInitiator` + `Session`; Fix8 `ClientConnection` + `Session`) keeps these layers distinct.

### D-13 — C ABI surface OWNED BY 2i (no `extern "C"` emitted by 2h)

**Decision**: 2h publishes the **C++ source-of-truth** and the per-doc-prefix `FIXPP_ERR_TRANSPORT_*` coalescing-group naming rule (`FIXPP_ERR_TRANSPORT_LIFECYCLE`, `FIXPP_ERR_TRANSPORT_IO`, `FIXPP_ERR_TRANSPORT_HANDSHAKE`, `FIXPP_ERR_TRANSPORT_CONFIG`, and the re-use of `FIXPP_ERR_CANCELLED` for the 5 `*_cancelled` variants). 2i bridges the C ABI in a separate feature.

**Rationale**: `[const §X.2]` `nm` check inherited; `[const §IX.5]` abidiff N/A here. The per-doc-prefix discipline established by `[2b §6.7]` / `[2c §6.7]` / `[2d §6.7]` / `[2e §6.7]` / `[2f §6.5]` / `[2g §6.6]` continues here. `[2h §6.6]` + `[2h §7.8]` codify.

**Alternatives considered**: none.

### D-14 — Control-plane reload trigger + TLS-event log / OTel record schema OWNED BY 2j / 2k

**Decision**: 2h owns the CALL SITES on the transport side (handshake start/success/failure spans; reconnect-attempt counters; cert-source reload trigger entry point). 2j owns the reload control-plane RPC; 2k owns the schemas.

**Rationale**: `[2h §7.6]` + `[2h §7.7]` codify. The call sites are 2h-local (the handshake coroutine emits `handshake_start_span` at entry, `handshake_success_span` on return); the schema versioning is 2k's concern.

**Alternatives considered**: none.

---

## §6 ASIO + OpenSSL integration — best-practices reference

### D-15 — `asio::ssl::stream<tcp::socket>` is the wire-level shape; per-session `SSL*` lifecycle bound to `asio_tls_transport` lifetime

**Decision**: `asio_tls_transport` holds an `asio::ssl::stream<asio::ip::tcp::socket>` BY VALUE. The underlying `SSL*` is owned by the stream; `SSL_free` is called by `~ssl::stream` at `asio_tls_transport` destruction. The `SSL_CTX*` (process-wide) is owned by the `SslCtxConfig` value held in the factory (cached per FR-026); 2h does NOT own `SSL_CTX*` lifetime.

**Rationale**: Standard ASIO + OpenSSL integration pattern. `[2h §4.5]` cites the binding. The factory-level `SSL_CTX*` caching is critical per FR-026 — rebuilding `SSL_CTX*` per attempt would amortise badly against the bounded back-off envelope (each `SSL_CTX_new` + cert load + chain build is ~10-50 ms; 10 attempts × 50 ms = 500 ms unnecessary cold-path work). The factory's `make(...)` cost MUST be bounded by the back-off envelope, NOT by `make(...)` runtime.

**Alternatives considered**:
- **Per-attempt `SSL_CTX*`** — rejected for the cost reason above; reference engines all cache the OpenSSL context per Acceptor / Initiator instance.
- **Process-singleton `SSL_CTX*`** — rejected; would prevent per-session `SecurityProfile` override (a future enhancement) and is harder to reason about under hot-reload.

### D-16 — `SSL_VERIFY_PEER` callback trampolines into `fixpp::tls::verify_peer` via `SSL_CTX_set_verify`

**Decision**: At `SSL_CTX` construction (in `asio_tls_transport` setup), 2h calls `SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, &asio_tls_transport_verify_trampoline)` where the trampoline (a free C-style function) extracts the per-handshake context from OpenSSL's `SSL_get_ex_data` + `X509_STORE_CTX_get_ex_data`, parses the peer's leaf cert + chain into `fixpp::tls::Certificate` views, and dispatches into `fixpp::tls::verify_peer(cfg, peer_chain)`. The captured `Pinset::snapshot()` reaches `verify_peer` via the per-`SslCtxConfig` pinset_snapshot field (captured ONCE at handshake start per `[2g §6.5.1]` + spec FR-011).

**Rationale**: `[2h §4.5]` + `[2g §7.1]` T-039 partition. 2h MUST NOT re-implement validation per FR-012; the trampoline is the wire-side hop only. The `ex_data` pattern is standard OpenSSL.

**Alternatives considered**:
- **Skip `SSL_VERIFY_PEER` and validate post-handshake** — rejected; OpenSSL's post-handshake validation surface is incomplete (some FIXS-required checks like 011's RSA-key-bound + ECDSA-curve + SAN-cardinality MUST happen at the per-cert callback to short-circuit DoS vectors at ≤ 50 µs p99 per `[2g §6.3]` row 4).

### D-17 — ASIO native cancellation: `cancellation_type::total` slot reads + variant returns

**Decision**: Every async method (`async_connect`, `async_read_some`, `async_write`, `async_handshake`, `async_accept`) reads its cancellation slot via `co_await asio::this_coro::cancellation_state` and surfaces `cancellation_type::total` as the matching `transport_*_cancelled` variant per `[2h §6.6]`. NO thrown exception escapes the public surface per FR-031.

For `co_spawn` defaults, the implementation MUST `cs.slot().assign([&](cancellation_type t) { ... })` or call `cancellation_state::throw_if_cancelled(false)` and explicitly reset for `total` per `[[feedback_asio_cospawn_total_cancellation_default.md]]` — co_spawn defaults to terminal-only, and `cancellation_type::total` is silently filtered without this reset. Tests under `[2h §9 seam #5]` exercise the matrix.

**Rationale**: `[const §XI.2]` + `[2d §4.7]` + `[2h §6.4.1]`. The per-mode cancellation effect table extends `[2d §4.7]` with the 5 transport-owned rows. Phase 1 graceful drain runs reads/writes under the root cancellation state; phase 2 + terminal fire `cancellation_type::total` immediately.

The two-phase close interaction with reconnect: when the session FSM drives reconnect, it calls `Transport::cancel()` to interrupt any in-flight op, then `Transport::close()` to tear down TLS+TCP, then DESTROYS the dead Transport, then mints a fresh one via `TransportFactory::make(...)`, then `async_connect` on the new instance (per D-5).

**Alternatives considered**:
- **Throw `operation_aborted` across the public surface** — rejected per `[const §XI.2]`; named variant returns are required for C-ABI bridging (2i coalesces via `FIXPP_ERR_CANCELLED`).
- **Generic `transport_cancelled` variant** — rejected; loses observability fidelity. Per-method variants let an operator distinguish "connect cancelled before handshake" from "write cancelled mid-flight" from "accept cancelled because Listener shut down".

### D-18 — `co_await asio::post(other_exec, use_awaitable)` does NOT pin coroutine body to `other_exec`

**Decision**: When 2h needs to run a coroutine body on a specific executor (e.g., the session executor for cancellation-seam witnesses), it uses **nested `asio::co_spawn(other_exec, fn, use_awaitable)`**, NOT `co_await asio::post(other_exec, use_awaitable)`.

**Rationale**: `[[feedback_asio_post_resume_bounces_to_spawn_executor.md]]` — `co_await asio::post(other_exec, use_awaitable)` only runs the post COMPLETION HANDLER on other_exec; the coroutine RESUME bounces back to the awaitable's associated executor (the co_spawn'd one). Sampling `this_thread::get_id()` after the post yields the SPAWN executor's thread, not other_exec's. This is the deterministic-hop pattern issue caught during PR #80 (007 seam-6).

For the FR-033 8-cell witness, the `direct_executor` mode requires the cancellable_dispatch body to run on a deterministic non-strand executor — the test fixture co_spawns onto that executor directly rather than posting from a strand-bound coroutine.

**Alternatives considered**: none — this is a known ASIO quirk documented in feedback.

---

## §7 Test seams + completeness gate

### D-19 — Test list is the 15 binding seams from `[2h §9]` + 8-cell FR-033/SC-008 witness + 2 contract witnesses

**Decision**: The complete test list is:
1. **15 seams from `[2h §9]`** — re-emitted BY NAME (ordinals are not stable across review rounds per `[2d §9]` / `[2g §9]` precedent). See plan.md Project Structure for the file-to-seam mapping.
2. **FR-033 + SC-008 8-cell cancellation-seam witness** for `file_cert_source::load_credentials` (`test_load_credentials_seam13_witness.cpp`).
3. **`ReconnectPolicy::defaults_quickfix_compat` factory** test (folded into `test_reconnect_policy_schedule.cpp` as a parallel cell).
4. **`Listener::cancel()` Option-A ownership** witness (folded into `test_listener_acceptor.cpp` as a parallel cell).

Plus the 2 fuzz harnesses (`fuzz_transport_read_path.cpp` + `fuzz_transport_handshake.cpp`) + 1 conformance test (`test_transport_interop.cpp`) + 1 FSM-via-mock-transport test (`test_session_fsm_via_mock_transport.cpp`, lives under `tests/session/`).

**Rationale**: Per `[[feedback_speckit_pipeline_order_gate_a_before_tasks]]` — the plan.md test list is consumed from the design-doc inheritance contract BY NAME, not re-derived from the FRs. The 2 contract witnesses are FR-aligned additions that don't displace any `[2h §9]` seam — they exercise specific Clarification-driven decisions.

**Alternatives considered**:
- **Re-derive test list from FRs only** — rejected per `[[feedback_speckit_pipeline_order_gate_a_before_tasks]]`. The `[2h §9]` seam list is the Gate-A-signed-off binding test inventory.

### D-20 — Completeness audit + catalogue updates per `[[feedback_feature_completeness_gate]]`

**Decision**: `/speckit-tasks` (next pipeline step after Gate A) will append explicit tasks for:
1. **Completeness audit** — task list ↔ FR ↔ SC ↔ catalogue row, 100 % or waived; `/gate-b` precondition.
2. **`library/spec/feature-catalogue.md` update** — append 012-2h-transport row; flip T-001/002/003/004/005/009/010 to `implementing`; T-039/040 cross-cut wiring half annotated.
3. **`library/spec/coverage-index.md` update** — append 012 ledger; rotate "Active feature" pointer.

**Rationale**: `[[feedback_feature_completeness_gate]]` — every feature: explicit tasks for completeness audit + catalogue + coverage updates; nothing does this automatically. Recurring 8th-burn risk per `[[project_005_phase8_completeness_false_pass]]` — Phase-8 completeness audit by file-name PASS is NOT trustworthy; audit test BODIES not file names; for emit/cross-session/role FRs require production-shaped entry-point exercise.

Per `[[feedback_simplify_pass_catches_9th_burn]]` — `/simplify` between `/implement` and `/verify` is mandatory for catching binding-contract drifts (15 instances 009→011 across 5 features); 2h is high-risk for the `trap_throw` boundary class per `[[feedback_trap_throw_pmr_witness_enumerate_sites]]` (any future `noexcept → trap_throw` conversion for the OpenSSL signing path through `[2g §4.1]` MUST have PMR witnesses enumerating allocation sites — boundary AND mid AND tail — not just first).

**Alternatives considered**: none — codified as standard pipeline practice.

---

## §8 Recurring traps from prior features — applied to 012

### D-21 — Mallocnesia + counting_resource DUAL gate for the read-path alloc guard

**Decision**: The read-path completion-handler dispatch alloc guard at `tests/transport/test_transport_read_alloc_guard.cpp` (`[2h §9 seam #4]`) MUST use BOTH:
- `counting_resource` (routes allocs through the PMR arena) — counts allocs via the arena.
- `mallocnesia` LD_PRELOAD (intercepts global `operator new` / `malloc`) — counts allocs that ESCAPE the arena.

**Rationale**: `[[feedback_tracking_pmr_resource_false_pass]]` — 008 T028+T050 silently passed Phases 3–7 while leaking heap allocs via non-PMR `std::vector`. `counting_resource` only counts allocs ROUTED through it; non-PMR-allocator `std::vector` escapes via global `operator new`. Dual gate is the resolution; verified by /gate-a re-touch D-16/D-17 2026-05-21. Per `[[reference_mallocnesia_path]]` mallocnesia IS installed at `tools/mallocnesia/libmallocnesia.so`.

**Alternatives considered**: none — false-pass mode is a hard requirement to close.

### D-22 — Fork-inherited ASIO pool is DEAD in the child — construct executors INSIDE the child branch

**Decision**: If 2h ships any SIGKILL-fork test fixture (e.g., for `Transport::close()` survival across fork, future crash-survival harness), the parent-built `asio::thread_pool` MUST NOT be reused — construct executors INSIDE the child branch.

**Rationale**: `[[feedback_fork_inherited_asio_pool_deadlock]]` — 008 T014 seam 2: pool constructed in parent → fork → `co_spawn(pool, …)` in child wedges (workers don't cross fork); fixed by constructing executors inside the child branch.

NOTE: 2h does NOT currently ship fork-based test fixtures; this is a guard for any future 005/2g/fuzz crash-survival fork harness that would consume `Transport`.

**Alternatives considered**: none.

### D-23 — `gate-precheck` heading-match contract for Gate B PR body

**Decision**: When 2h's PR body is authored (post-`/speckit-verify`, pre-Gate-B-label-application), the PR body MUST include literal `## Gate A` AND `## Gate B` headings (NOT `## Gating` / `## Disposition`). Otherwise `gate-precheck.outputs.proceed='false'` silently skips the entire sanitizer matrix while `mergeStateStatus: MERGEABLE`.

**Rationale**: `[[feedback_gate_precheck_heading_match_contract]]` — PR #78 near-miss 2026-05-21. Check the SKIPPED count after applying `*-waived` labels, never just `mergeable`.

**Alternatives considered**: none — pure procedural lesson.

### D-24 — Codecov DA/BRDA carry-forwards from 011 may apply to 2h's `asio_tls_transport.cpp`

**Decision**: If 2h's `/speckit-verify` surfaces a Codecov/patch YELLOW (target ~88 % vs actual ~80 %) on `asio_tls_transport.cpp` due to OpenSSL fault-injection paths not reachable without fault hooks, record as an explicit waiver-with-rationale per `[const §IX.1]` lcov DA/BRDA written justifications + PR #73 / #74 / #77 / #82 / #84 precedent.

**Rationale**: `[[feedback_codecov_patch_vs_lcov_da_brda_gate]]` — binding gate is per-file lcov DA/BRDA with Article IX §1 written justifications, NOT Codecov/patch external soft gate. Codecov/patch reports against UNTOUCHED-by-tests lines (the FIXTURES, not the production code) and is consistently 5-15 pp below the lcov DA/BRDA basis on TLS-touching files.

**Alternatives considered**:
- **Bloat tests against impossible paths** — rejected per `[[feedback_coverage_gate_lcov_basis.md]]`; the gate basis is lcov DA/BRDA, not llvm-cov report aggregate.

### D-25 — `/gate-b` fixer MUST re-run `tools/check_layers.py` after adding new headers

**Decision**: If `/gate-b` fixer rounds add new headers (e.g., a missed internal `src/transport/asio_*.hpp`), the fixer MUST cross-reference `architecture.md` for canonical type location AND re-run `tools/check_layers.py` post-commit per `[[feedback_gate_b_check_layers_post_fixer]]`.

**Rationale**: 007 PR #74 Sonnet RC#1 placed SecurityProfile in `include/fixpp/tls/` but architecture.md:243 mandated `fixpp::session::SecurityProfile`; check-layers Tier-1 failed post-merge → PR #75 hotfix. The architecture doc is the layer authority; design doc prose is NOT binding for module placement. For 2h: `architecture.md` §4.5 mandates `fixpp::transport::` namespace for all 2h-owned types — verified at plan-author time.

**Alternatives considered**: none.

---

## §9 Open questions deferred to post-v1 per `[2h §10]`

| # | Question | Disposition |
|---|---|---|
| 1 | PSK API hook (T-012) | DEFER post-v1 — `[const §XII.6]` P2 carve-out. 4 free `TlsTransport` sub-interface slots reserved. |
| 2 | gRPC vs in-process control plane | DEFER to **2j**. |
| 3 | Reconnect per-venue presets | DECIDED v0.2 (defaults_quickfix_compat + defaults); per-venue preset library is post-v1 (DEFER). |
| 4 | TCP socket option override docs | DEFER to `docs/perf-tuning.md` post-v1. |
| 5 | TLS bidi shutdown timeout default (1 s tight enough?) | DEFER to operational bench-time tuning at v1.x. |
| 6 | IPv6 zone-id corpus | DEFER to Phase-4 conformance corpus expansion. |
| 7 | `Listener::async_accept` cancellation under 10⁵-accepts/sec | DEFER post-v1 (v1.0 target is 10² – 10³ sessions per acceptor). |

None impacts v1.0 scope / security / UX such that a `[NEEDS CLARIFICATION]` marker would be load-bearing; `/speckit-clarify` non-skip is the discharge per `[const §XVII.1]`.

---

## §10 References

- `.specify/2h-transport.md` v0.3 — bound upstream.
- `.specify/2g-tls.md` v0.4 — 011 producer surface (LOCKED).
- `.specify/2d-threading.md` v0.4 — `cancellable_dispatch` recipe + per-mode cancellation effect table.
- `.specify/2e-msgstore.md` v0.4 — durable-before-transmit invariant.
- `.specify/2b-wire.md` v0.2 — `Framer::feed` / `Writer::commit` shapes.
- `.specify/architecture.md` v0.2 — §4.5 transport surface inventory; §5.1/§5.3/§5.6/§5.8/§6.
- `.specify/constitution.md` v0.2 — Article XII / XI / XIV / XV / XVII.
- `[[project_011_tls_policy_closed.md]]` — 011 Gate B F-1 carryover.
- `[[project_release_interop_quickfix_fix8]]` — pre-v1.0 interop gate (post-2h).
- `[[feedback_asio_post_resume_bounces_to_spawn_executor]]` — D-18.
- `[[feedback_asio_cospawn_total_cancellation_default]]` — D-17.
- `[[feedback_tracking_pmr_resource_false_pass]]` — D-21.
- `[[feedback_fork_inherited_asio_pool_deadlock]]` — D-22.
- `[[feedback_gate_precheck_heading_match_contract]]` — D-23.
- `[[feedback_codecov_patch_vs_lcov_da_brda_gate]]` — D-24.
- `[[feedback_gate_b_check_layers_post_fixer]]` — D-25.
- `[[feedback_simplify_pass_catches_9th_burn]]` — `/simplify` pre-/verify mandatory.
- `[[feedback_trap_throw_pmr_witness_enumerate_sites]]` — trap_throw PMR witness depth.
- `[[feedback_subagent_phase_verification_two_traps]]` — phase verification.
- `[[feedback_phase_implementer_sonnet_runaway_scope]]` — Phase-implementer scope cap.
- Reference engines (cloned under `reference-engines/` per `[[project_reference_engines_setup]]`):
  - QuickFIX-cpp v1.16.0 — `SocketInitiator::doConnect` + `SocketAcceptor::onStop` + `SocketServer.cpp` defaults.
  - QuickFIX/J 3.0.1 — `IoSessionInitiator::connect` + `computeNextRetryConnectDelay` + `NetworkingOptions.java` + `AbstractSocketAcceptor.stopAcceptingConnections`.
  - Fix8 1.4.3 — `ReliableClientSession::operator()` + `~ServerSessionBase` + `configuration.hpp` + `runtime/connection.cpp` setLinger.
