# Phase 1 Data Model — 012-2h-transport

**Branch**: `012-2h-transport` | **Date**: 2026-05-27 | **Plan**: [plan.md](plan.md) | **Spec**: [spec.md](spec.md) | **Research**: [research.md](research.md)

**Scope**: enumerate every entity (E-1..E-N) the public surface introduces, with field shapes, ownership, allocator policy, lifetime, validation rules, and applicable state transitions. The header bodies under `contracts/` re-emit these types verbatim per `[2h §4]`; this doc is the entity-level reference (cross-cuts: where ownership / allocator / state-transition rules live, NOT the field-level type definitions which the header bodies own).

---

## E-1 `fixpp::transport::Transport`

**Shape**: abstract pluggable interface. **5 pure-virtual methods AT** `[const §XIV.2]`'s ≤ 5 cap (deliberate; zero headroom on the base — see plan Summary).

**Fields**: none (interface only; concrete state lives on `asio_tls_transport` / `mock_transport`).

**Methods (binding)** per `[2h §4.1]`:

| # | Signature | Cancellation surface |
|---|---|---|
| 1 | `async_connect(Endpoint const&) -> asio::awaitable<expected_t<ConnectInfo>>` | `transport_connect_cancelled` on `cancellation_type::total` |
| 2 | `async_read_some(std::span<std::byte> [[clang::lifetimebound]]) -> asio::awaitable<expected_t<std::size_t>>` | `transport_read_cancelled` |
| 3 | `async_write(std::span<const std::byte> [[clang::lifetimebound]]) -> asio::awaitable<expected_t<std::size_t>>` | `transport_write_cancelled` |
| 4 | `cancel() noexcept -> expected_t<void>` | synchronous; bounded ≤ 1 µs p99 per `[2h §6.3]` |
| 5 | `close() noexcept -> expected_t<void>` | synchronous on strand; bounded by `Config::tls_close_timeout` (1 s default) |

**Annotations at declaration site**: every `expected_t<T>`-returning method `[[nodiscard]]`; every non-owning view parameter `[[clang::lifetimebound]]` (mirrors `[2b §6.4]` / `[2g §4.1]`).

**Ownership**: held by `Session::transport_` as `std::unique_ptr<Transport>` minted at session open via `TransportFactory::make(...)`; **frozen-at-open** per `[arch §5.6]`; **reconnect mints a FRESH `Transport`** via the same factory per Clarifications 2026-05-27 Q1=B (D-5 in research.md).

**Allocator policy**: not applicable on the interface itself; impls own PMR.

**Validation rules**:
- **In-flight exclusivity (API-level contract)**: at most one in-flight `async_read_some` AND one in-flight `async_write` per instance; concurrent second call returns IMMEDIATELY with `transport_read_in_progress` / `transport_write_in_progress`. Strand serialisation is defence-in-depth, NOT binding.
- **One-shot connect / handshake** per `Transport` lifetime; second call surfaces `transport_already_connected`.
- **Post-`close()`** every `async_*` returns `transport_already_closed`.

**State transitions** (held in impl state; see E-9 `asio_tls_transport`):

```
NotConnected → (async_connect) → Connecting
Connecting   → (success)        → Connected
Connecting   → (cancel/error)   → NotConnected | Closed
Connected    → (async_handshake) → Handshaking      [TlsTransport only]
Handshaking  → (success)        → Active
Handshaking  → (cancel/error)   → Closed
Active       → (close())        → Closed
Active       → (cancel mid-IO)  → Active            [cancel does NOT close]
*            → (close())        → Closed
```

---

## E-2 `fixpp::transport::TlsTransport`

**Shape**: sub-interface inheriting virtually from `Transport`. **1 additional pure-virtual method** (`async_handshake`); uses 1 of 5 sub-interface slots per `[const §XIV.2]`. 4 slots reserved empty headroom for post-v1 PSK (T-012) / explicit renegotiation control / early-data hook.

**Method (binding)** per `[2h §4.2]`:

| Signature | Cancellation surface |
|---|---|
| `async_handshake(fixpp::tls::SslCtxConfig const& [[clang::lifetimebound]]) -> asio::awaitable<expected_t<handshake_result>>` | `transport_handshake_cancelled` on `cancellation_type::total`; `transport_handshake_timeout` on `Config::tls_handshake_timeout` elapse |

**Pinset capture binding** (per `[2g §6.5.1]` + spec FR-011): `async_handshake` captures `cfg.pinset->snapshot()` ONCE at handshake start (BEFORE the OpenSSL handshake protocol exchange begins) and stores the captured `shared_ptr<const pin_snapshot>` in transport-owned state for the duration of the handshake. The `SSL_VERIFY_PEER` callback reads the captured snapshot directly from that state — it MUST NOT call `cfg.pinset->find` / `contains` / `snapshot` mid-verification. Mid-handshake rotation per `[2d §7.5]` does NOT affect an in-flight handshake by construction.

**Single-site `dynamic_cast`**: `Session::transport_` is held as `unique_ptr<Transport>`; reached as `TlsTransport*` via EXACTLY ONE `dynamic_cast<TlsTransport*>(transport_.get())` at session open. Post-handshake reads of `peer_identity` / captured pinset / cipher are direct value reads on the FSM-held `handshake_result` — no virtual dispatch, no further casts.

**Ownership / lifetime / validation rules**: inherited from E-1.

---

## E-3 `fixpp::transport::ConnectInfo`

**Shape**: owning-by-value POD returned from `async_connect`.

**Fields**:

| Field | Type | Notes |
|---|---|---|
| `remote` | `Endpoint` | negotiated peer endpoint (post-resolution) |
| `local` | `Endpoint` | local-side bind endpoint |
| `family` | `int` | `AF_INET` or `AF_INET6` |

**Ownership**: by value; consumer (FSM) captures across reconnect cycles.

**Allocator policy**: the `Endpoint` members use the system default allocator for `host` strings (E-5).

**Validation rules**: none at the type level; the values are negotiated outputs of ASIO's resolver + connect chain.

---

## E-4 `fixpp::transport::handshake_result`

**Shape**: owning-by-value POD returned from `TlsTransport::async_handshake`.

**Fields**:

| Field | Type | Notes |
|---|---|---|
| `peer_id` | `fixpp::tls::peer_identity` | OWNING per `[2g §4.5]`; PMR-allocated SAN strings against `SslCtxConfig::mr` |
| `captured_pinset` | `std::shared_ptr<const fixpp::tls::pin_snapshot>` | NULL IFF `SecurityProfile::mtls_ca` / `one_way_ca`; non-null under `mtls_pinned` per `[2g §4.5.1]` |
| `negotiated_cipher` | `std::pmr::string` | e.g. `"TLS_AES_128_GCM_SHA256"`; PMR-allocated against `SslCtxConfig::mr` |

**Ownership**: by value; FSM holds `handshake_result` across the session lifetime per `[2c §4.8]` `owning_message_t<>` precedent.

**Allocator policy**: `peer_id` SAN strings + `negotiated_cipher` allocate against `SslCtxConfig::mr` at handshake time. PMR throws routed through `[2a §4.2]` `trap_throw` and surface as `transport_handshake_failed` / `tls_*` — NO PMR throw escapes as a C++ exception.

**Validation rules**: `captured_pinset` is null IFF profile is non-pinned; consumers MUST guard `if (result.captured_pinset)` before dereferencing.

**No accessor pure-virtuals**: the type ships value-typed members only per `[2h §4.2]` RC#2 close. Consumers reach view-shapes through `peer_id` (which carries `[[clang::lifetimebound]]` view accessors per `[2g §4.5]`).

---

## E-5 `fixpp::transport::Endpoint`

**Shape**: owning-by-value value type.

**Fields** per `[2h §4.3]`:

| Field | Type | Default | Notes |
|---|---|---|---|
| `host` | `std::string` | (none) | hostname OR IP literal OR `"host%zone"` for IPv6 link-local |
| `port` | `std::uint16_t` | `0` | initiator: peer port; acceptor: bind port |
| `backlog` | `std::uint32_t` | `128` | acceptor-only; listen queue depth; OS may cap silently |

**Constructors**: default; `(host, port)` for initiator; `(host, port, backlog)` for acceptor.

**Ownership**: by value; the `host` string uses system default allocator (NOT PMR — the v0.1 PMR-aware overload claim is retired per `[2h §4.3]`).

**Validation rules**:
- IPv6 zone-id form `fe80::1%eth0` admitted; ASIO's resolver handles them.
- Address family auto-detected at `async_connect` time via `asio::ip::resolver` (NOT pinned in the type).
- Initiator vs acceptor distinction is type-system-level (which method consumes the Endpoint), NOT a runtime predicate — the v0.1 `is_initiator_shape()` heuristic was dropped per `[2h §4.3]`.

**Acceptor full-conformance corpus for IPv6 zone-id**: DEFERRED to Phase-4 conformance corpus per `[2h §10 Q6]`.

---

## E-6 `fixpp::transport::ReconnectPolicy`

**Shape**: owning-by-value value type (Clarifications 2026-05-27 Q2=C — QuickFIX/J-aligned schedule-array shape).

**Fields** per spec FR-019 (REPLACES the v0.3 design-doc 5-field shape per Appendix D §D.5):

| Field | Type | Default (raw) | Default via `defaults()` | Notes |
|---|---|---|---|---|
| `schedule` | `std::pmr::vector<std::chrono::milliseconds>` | (required, ≥ 1 entry) | `[100 ms, 200 ms, 400 ms, 800 ms, 1.6 s, 3.2 s, 6.4 s, 12.8 s, 25.6 s, 30 s]` | per-attempt delay table indexed by attempt number; LAST entry is plateau-at-last per QuickFIX/J `IoSessionInitiator::computeNextRetryConnectDelay():318-319` |
| `jitter` | `double` | `0.0` | `0.10` | range `[0.0, 1.0]`; ±10 % randomisation; anti-thundering-herd |
| `max_attempts` | `std::uint32_t` | `0` (= unbounded) | `10` | numeric ceiling per `[const §XV]`; `0` = unbounded (opt-in only) |

The legacy v0.3 fields `initial_delay` / `max_delay` / `multiplier` are NOT present — the materialised `schedule` replaces them.

**Factories**:
- `defaults() -> ReconnectPolicy` — v0.2 exponential schedule materialised, cumulative envelope 73 – 89 s wall-clock at `max_attempts = 10` with ±10 % jitter (per spec FR-020 + design-doc §1.1 corrected numeric).
- `defaults_quickfix_compat() -> ReconnectPolicy` — `{schedule = [30 s], jitter = 0.0, max_attempts = 0}` for industry-canonical QuickFIX-cpp / Fix8 / QuickFIX/J behaviour (single fixed 30 s interval, no jitter, no cap).

**Helper method**:
- `delay_for_attempt(std::uint32_t attempt_n) -> std::chrono::milliseconds` — returns `schedule[std::min(attempt_n, schedule.size() - 1)] * (1.0 + uniform_real(-jitter, +jitter))`. Jitter is **deterministic-per-attempt** (seeded from session id + attempt number) for test repeatability per `[const §VII.7]` fuzz determinism.

**Ownership**: by value; FSM holds across the session lifetime.

**Allocator policy**: `schedule` is `std::pmr::vector`; allocated against the engine's PMR root resource at construction.

**Validation rules**:
- `schedule.size() >= 1`.
- `jitter ∈ [0.0, 1.0]`.
- Exceeding `max_attempts` surfaces `transport_reconnect_limit_exceeded` per spec FR-022; FSM owns the reconnect loop.
- Operators MUST opt in to unbounded reconnect via `max_attempts = 0` explicitly per `[const §XII.5]` no-implicit-default rule.

---

## E-7 `fixpp::transport::Transport::Config`

**Shape**: per-session knobs value type.

**Fields** per `[2h §4.5]` Config + spec FR-029 (Clarifications 2026-05-27 Q5=A defaults):

| Field | Type | Default | Notes |
|---|---|---|---|
| `connect_timeout` | `std::chrono::milliseconds` | `30 000` (30 s) | `async_connect` upper bound |
| `tls_handshake_timeout` | `std::chrono::milliseconds` | `30 000` (30 s) | `async_handshake` upper bound; surfaces `transport_handshake_timeout` |
| `tls_close_timeout` | `std::chrono::milliseconds` | `1 000` (1 s) | `close()` bidi shutdown bound; truncated close surfaces `transport_tls_close_truncated` |
| `max_read_window_bytes` | `std::size_t` | `262 144` (256 KiB) | matches `[2b §1.2]` `default_max_frame_bytes` |
| `max_write_size_bytes` | `std::size_t` | `1 048 576` (1 MiB) | one v1.0-max frame + 4× headroom for derivatives venues |
| `tcp_recv_buf_bytes` | `std::int32_t` | `0` | `0` = OS auto-tune |
| `tcp_send_buf_bytes` | `std::int32_t` | `0` | `0` = OS auto-tune |
| `tcp_nodelay` | `bool` | `true` | **Nagle OFF** per Clarifications Q5=A (aligns with QFJ + Fix8; diverges from QFC's Nagle-ON anomaly) |
| `tcp_keepalive` | `bool` | `false` | FIX `HeartBtInt` is the primary keep-alive |
| `tcp_keepalive_idle_seconds` | `std::int32_t` | `120` | consulted only when `tcp_keepalive` is true |
| `tcp_keepalive_interval_seconds` | `std::int32_t` | `30` | consulted only when `tcp_keepalive` is true |
| `tcp_keepalive_count` | `std::int32_t` | `3` | consulted only when `tcp_keepalive` is true |
| `so_reuseaddr` | `bool` | `false` | acceptor-side opt-in |
| `so_linger_enabled` | `bool` | `false` | **no linger** per Clarifications Q5=A (matches Fix8 explicit + QFJ/QFC OS-default) |
| `so_linger_seconds` | `std::int32_t` | `0` | consulted only when `so_linger_enabled` is true |
| `mr` | `std::pmr::memory_resource*` | `nullptr` | null = engine default; passed through to factory `make(...)` |

**Ownership**: by value; held inside `asio_tls_transport` for the Transport lifetime.

**Allocator policy**: `mr` is the PMR root the impl uses for SSL_CTX configuration arena + handshake-time SAN string copies.

**Validation rules**: defaults are the named-deployment-class operationally-correct posture for FX retail / equities / equity-options / derivatives. Operator overrides via `make_asio_tls_transport(..., cfg, ...)`.

---

## E-8 `fixpp::transport::Listener`

**Shape**: abstract pluggable interface. **1 pure-virtual method** (`async_accept`); well under `[const §XIV.2]` cap.

**Method** per `[2h §4.6]`:

| Signature | Cancellation surface |
|---|---|
| `async_accept() -> asio::awaitable<expected_t<std::unique_ptr<Transport>>>` | `transport_accept_cancelled` (mapped from `listener_accept_cancelled` per `[2h §6.6]`) on `cancellation_type::total` |

**`cancel()` Option-A contract** per spec FR-025 + Clarifications 2026-05-27 Q4=A: does EXACTLY three things and ONLY those three:
1. Close the listening socket so no new TCP connections complete.
2. Complete any in-flight `async_accept` awaitable not yet resumed with `listener_accept_cancelled`.
3. Leave already-resumed-but-not-yet-consumed `unique_ptr<Transport>` results UNAFFECTED.

**Ownership**: engine-owned (`std::unique_ptr<Listener>` held by `service/` per `[arch §4.11]`); lifetime is engine lifetime (outlives many `Transport` instances).

**Allocator policy**: impl's PMR is engine default per `[arch §6]` rule 4.

**Validation rules**: each accepted connection MUST produce a freshly-minted `Transport` instance per spec FR-023.

**State transitions**:

```
Bound      → (async_accept loop start) → Accepting
Accepting  → (each accept success)     → Accepting  [yields a fresh Transport]
Accepting  → (cancel())                → Cancelled
Cancelled  → (~Listener)                → -
```

---

## E-9 `fixpp::transport::asio_tls_transport`

**Shape**: concrete impl of `TlsTransport` over ASIO `tcp::socket` + `asio::ssl::stream<tcp::socket>` over OpenSSL.

**Fields** per `[2h §4.5]`:

| Field | Type | Notes |
|---|---|---|
| `cfg_` | `Config` (E-7) | per-session knobs |
| `ssl_cfg_` | `fixpp::tls::SslCtxConfig` | TLS configuration built by 011's `make_ssl_ctx_config` |
| `exec_` | `asio::any_io_executor` | session executor wrapper per `[2d §4.8]` |
| `socket_` | `asio::ip::tcp::socket` | TCP socket; owned by stream once handshake starts |
| `ssl_ctx_` | `std::unique_ptr<asio::ssl::context>` | RAII-owned `SSL_CTX*` |
| `ssl_stream_` | `std::optional<asio::ssl::stream<asio::ip::tcp::socket&>>` | wraps `socket_` once handshake starts |
| `captured_pinset_` | `std::shared_ptr<const fixpp::tls::pin_snapshot>` | captured at handshake start per `[2g §6.5.1]` |
| `peer_id_` | `fixpp::tls::peer_identity` | populated on `verify_peer` accept |
| `state_` | `enum class state_t { fresh, connected, handshaken, closed }` | concrete state for E-1 transitions |

**Construction**:
- Direct constructor `asio_tls_transport(exec, cfg, ssl_cfg)` permitted to throw per `[arch §5.3]` carve-out (engine bootstrap before any session is open).
- Factory `make_asio_tls_transport(exec, cfg, ssl_cfg, mr) -> expected_t<std::unique_ptr<Transport>>` wraps the throwing constructor for non-construction-time callers (2i C ABI / future hot-reload / runtime reconnect mint).

**OpenSSL configuration** (at constructor time, before `async_handshake`):
- `SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION)` per `[const §XII.2]`.
- `SSL_CTX_set_max_proto_version(ctx, TLS1_3_VERSION)`.
- `SSL_CTX_set_ciphersuites` (TLS 1.3) + `SSL_CTX_set_cipher_list` (TLS 1.2) from `CipherPolicy` per `[2g §4.4]`.
- `SSL_CTX_set1_curves_list` + `SSL_CTX_set1_sigalgs_list` from `CipherPolicy`.
- `SSL_CTX_set_options(SSL_OP_NO_RENEGOTIATION | SSL_OP_NO_COMPRESSION | SSL_OP_NO_TICKET | SSL_OP_NO_EARLY_DATA)` per `[FIXS §3.2]` + `[const §XII.3]`.
- `SSL_CTX_set_verify(SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT [acceptor-side mtls_*])` per `[2g §4.5.1]` table.
- `SSL_CTX_set_verify_callback(verify_peer_trampoline)` — trampoline dispatches into `fixpp::tls::verify_peer(cfg, peer_chain)` per `[2g §7.1]` T-039 partition.

**Allocator policy**: `cfg_.mr` for SSL_CTX configuration arena + handshake-time peer_identity SAN strings. OpenSSL allocator path is process-global (`CRYPTO_set_mem_functions` set ONCE at engine init by engine bootstrap — NOT by 2h per-instance).

**Validation rules**: state machine enforces the E-1 transitions; in-flight exclusivity per E-1.

---

## E-10 `fixpp::transport::asio_listener`

**Shape**: concrete impl of `Listener` wrapping `asio::ip::tcp::acceptor`.

**Fields** per `[2h §4.6]`:

| Field | Type | Notes |
|---|---|---|
| `cfg_` | `Config` (sub-type; see below) | acceptor-side knobs |
| `exec_` | `asio::any_io_executor` | service strand executor (engine-scoped per `[2h §6.4.1]`) |
| `acceptor_` | `asio::ip::tcp::acceptor` | OS-level TCP acceptor |

**`asio_listener::Config` fields**:

| Field | Type | Default | Notes |
|---|---|---|---|
| `bind_endpoint` | `Endpoint` | (required) | local bind address + port + backlog |
| `so_reuseaddr` | `bool` | `true` | typical acceptor default |
| `so_reuseport` | `bool` | `false` | Linux-only; off by default |
| `accepted_transport_config` | `asio_tls_transport::Config` | (defaults) | passed through to each minted Transport |
| `ssl_cfg` | `fixpp::tls::SslCtxConfig` | (required) | per-acceptor TLS profile |

**Construction**: direct constructor `asio_listener(exec, cfg)` permitted to throw; factory `make_asio_listener(exec, cfg, mr) -> expected_t<std::unique_ptr<Listener>>` wraps for non-construction-time callers.

**State transitions** per E-8.

**Validation rules**:
- One `SslCtxConfig` per Listener (acceptor-side TLS profile fixed at engine open); multi-tenant acceptor uses one Listener per counterparty.
- `cancel()` Option-A contract per E-8 + spec FR-025.

---

## E-11 `fixpp::transport::TransportFactory`

**Shape**: abstract pluggable interface — the `[arch §6]` rule-4 factory.

**Method**:

| Signature | Notes |
|---|---|
| `make(asio::any_io_executor exec, fixpp::tls::SslCtxConfig ssl_cfg, std::pmr::memory_resource* mr) noexcept -> expected_t<std::unique_ptr<Transport>>` | `noexcept` per `[2h §4.7]` Codex P2 #6 close; impls MUST `trap_throw` internal PMR / system throws and surface `transport_factory_failed`. |

**Default impl**: `asio_tls_transport_factory` constructs `asio_tls_transport` instances; caches the `SslCtxConfig` (and OpenSSL `SSL_CTX*` inside it) at factory level per spec FR-026 — long-lived state shared across reconnect attempts is cached at the factory, NEVER re-built per attempt.

**Ownership** per spec FR-027 + Appendix D §D.1 / §D.2:
- `EngineConfig::default_transport_factory` is `std::unique_ptr<TransportFactory>` (Appendix D §D.1 flips from `shared_ptr` → `unique_ptr` at sign-off; mirrors `[2e §4.4]` `MessageStoreFactory` precedent).
- `SessionConfig::transport_factory_override` is `std::unique_ptr<TransportFactory>` (Appendix D §D.2 adds the field per engine-anchor + session-`*_override` pattern).
- Resolved factory = `transport_factory_override.value_or(EngineConfig::default_transport_factory)`.

**Reconnect contract** per spec FR-028 + Clarifications 2026-05-27 Q1=B: the factory is invoked at session open AND at every reconnect attempt. Each `async_connect` attempt MUST operate on a `Transport` instance newly minted by the factory. The previous `Transport` MUST be destroyed BEFORE the new one is requested. The FSM holds at most one live `Transport` per session at any time.

**Validation rules**: factory failure surfaces `transport_factory_failed`; OS resource exhaustion / OpenSSL `SSL_CTX_new` failure routed through `trap_throw` → `transport_factory_failed`.

---

## E-12 `fixpp::transport::test::mock_transport`

**Shape**: concrete impl of `TlsTransport` for FSM-under-test scenarios; PUBLIC TEST HEADER at `include/fixpp/transport/test/mock_transport.hpp`, consumable from `tests/`-only translation units per `[const §VII]`.

**`Script` shape** per `[2h §4.8]`:

| Field | Type | Default | Notes |
|---|---|---|---|
| `inbound_bytes` | `std::vector<std::byte>` | empty | queued bytes for `async_read_some` |
| `expected_outbound_writes` | `std::vector<std::vector<std::byte>>` | empty | verified at `async_write` against `outbound_seen_` |
| `handshake_succeeds` | `bool` | `true` | controls `async_handshake` outcome |
| `peer_identity_to_return` | `fixpp::tls::peer_identity` | default | populated when `handshake_succeeds` |
| `pinset_snapshot_to_return` | `std::shared_ptr<const fixpp::tls::pin_snapshot>` | null | optional; null OK under `mtls_ca` mode |
| `negotiated_cipher` | `std::string` | `"TLS_AES_128_GCM_SHA256"` | returned in `handshake_result.negotiated_cipher` |
| `read_latency` | `std::chrono::milliseconds` | `0` | injection point for cancellation race tests |
| `write_latency` | `std::chrono::milliseconds` | `0` | injection point for cancellation race tests |
| `handshake_latency` | `std::chrono::milliseconds` | `0` | injection point for cancellation race tests |

**Fields** (internal state):

| Field | Type | Notes |
|---|---|---|
| `exec_` | `asio::any_io_executor` | strand the FSM-under-test runs on |
| `script_` | `Script` | test-author-supplied; copied at construction |
| `read_cursor_` | `std::size_t` | progress through `inbound_bytes` |
| `outbound_seen_` | `std::vector<std::byte>` | grows as `async_write` is observed |
| `closed_` | `bool` | tracks E-1 `Closed` state |
| `handshaken_` | `bool` | tracks E-1 `Active` state (post-handshake) |

**Test diagnostics** (read-only accessors): `bytes_read_so_far()`, `outbound_bytes_seen()`, `async_writes_observed()`.

**Cancellation honour**: every async method composes `co_await asio::post(exec)` checkpoints so the awaiter's cancellation slot fires deterministically — the mock does NOT short-circuit cancellation by completing instantly. Cancellation-aware completions surface `transport_*_cancelled` EXACTLY as the production impl does (per spec FR-037).

**Ownership**: test-binary-owned `unique_ptr<Transport>` (matches `TransportFactory::make` return shape).

**Allocator policy**: system default allocator (the mock does NOT need PMR; tests that exercise PMR seams use `asio_tls_transport`).

---

## E-13 `error::transport_*` variant family

**Shape**: appended to `fixpp::core::error` enum at next free contiguous block per `[[project_2e_design_doc_only_seqnum_handoff]]` slot-pinning rule (slots 94..115; 22 variants; 011 took 78..93; never renumber existing slots).

**Variants** per `[2h §6.6]` + spec FR-034 (22 total):

**Connect / lifecycle** (8) — `FIXPP_ERR_TRANSPORT_LIFECYCLE` group:
- `transport_resolve_failed` — DNS NXDOMAIN / malformed host / no AF match
- `transport_connect_refused` — TCP `ECONNREFUSED`
- `transport_connect_timeout` — TCP connect exceeded `Config::connect_timeout`
- `transport_connect_cancelled` — `async_connect` cancelled
- `transport_already_connected` — `async_connect` or `async_handshake` called twice (one-shot violation)
- `transport_already_closed` — any `async_*` called after `close()` returned
- `transport_read_in_progress` — concurrent second `async_read_some` (API-level exclusivity violation)
- `transport_write_in_progress` — concurrent second `async_write` (API-level exclusivity violation)
- `transport_reconnect_limit_exceeded` — FSM exhausted `ReconnectPolicy::max_attempts`

Wait — that's 9. Let me re-count per `[2h §6.6]`:

**Re-grouped per `[2h §6.6]` C-ABI coalescing** (22 variants total):

| Group | Variants |
|---|---|
| `FIXPP_ERR_TRANSPORT_LIFECYCLE` | `transport_resolve_failed`, `transport_connect_refused`, `transport_connect_timeout`, `transport_already_connected`, `transport_already_closed`, `transport_read_in_progress`, `transport_write_in_progress`, `transport_reconnect_limit_exceeded` (8) |
| `FIXPP_ERR_TRANSPORT_IO` | `transport_read_eof`, `transport_read_truncated`, `transport_read_error`, `transport_write_short`, `transport_write_error` (5) |
| `FIXPP_ERR_TRANSPORT_HANDSHAKE` | `transport_handshake_failed`, `transport_handshake_timeout` (2; joins `FIXPP_ERR_TLS_HANDSHAKE` at C ABI per 2i's call) |
| `FIXPP_ERR_TRANSPORT_CONFIG` | `transport_factory_failed`, `transport_psk_unsupported` (2) |
| `FIXPP_ERR_CANCELLED` (reuse) | `transport_connect_cancelled`, `transport_read_cancelled`, `transport_write_cancelled`, `transport_handshake_cancelled`, `transport_accept_cancelled` (5) |

Total: 8 + 5 + 2 + 2 + 5 = 22. ✓

**`transport_handshake_failed` GROUPING variant**: carries a diagnostic-field sub-reason (the underlying OpenSSL error string + the `[2g §6.6]` `tls_*` sub-reason if `verify_peer` rejected). Joins `[2g §6.6]` `tls_handshake_failed` group at the C ABI per 2i's coalescing call. The 11 `tls_*` variants from `[2g §6.6]` surface UNCHANGED through 2h — 2h MUST NOT re-translate or coalesce them (spec FR-034).

**`transport_tls_close_truncated`** per spec FR-006 / Edge Cases: it's an INFORMATIONAL outcome of `close()`, not a hard error; the spec text mentions it as a `close()` outcome on truncated peer-close. Whether it's a NAMED variant or folded into an existing one is a final-coalescing 2i call. The design doc lists it as `transport_read_truncated` in `[2h §6.6]` (mapping the v0.1 `transport_tls_close_truncated` semantics into `transport_read_truncated` with a strict-mode opt-in). 22 total stands.

**Storage**: in `include/fixpp/core/error.hpp` at slots 94..115; re-exported under `fixpp::transport::errors` namespace via `include/fixpp/transport/transport_errors.hpp` for ergonomic at-site use.

**Validation rules**:
- Numeric block is **contiguous** at 94..115; respects prior 005/008/009/010/011 pinning.
- C-ABI coalescing is **delegated to 2i** per `[2h §6.6]`; 012 publishes the C++ source-of-truth + per-doc-prefix `FIXPP_ERR_TRANSPORT_*` naming rule only.

---

## E-14 Appendix D cross-doc amendments (orchestrator-applied at 012 sign-off)

Per `[2c App D]` / `[2d App D]` / `[2e App D]` / `[2f App D]` / `[2g App D]` precedent, 2h declares these drop-ins for the orchestrator to apply at sign-off (the rewrite agent does NOT edit sibling docs in this draft):

**§D.1** — `[2d §4.4]` `EngineConfig::default_transport_factory` field type changes from `std::shared_ptr<TransportFactory>` to `std::unique_ptr<TransportFactory>` per `[arch §5.6]` frozen-at-open + `[2e §4.4]` `MessageStoreFactory` precedent.

**§D.2** — `[2d §4.5]` `SessionConfig` gains `std::unique_ptr<fixpp::transport::TransportFactory> transport_factory_override;` per engine-anchor + session-`*_override` pattern.

**§D.3** — `library/spec/coverage-index.md` §"FIXS RC1" MUST already have gained T-039 / T-040 / T-041 rows per `[2g App D §D.3]` (carried-forward 2g-owned orchestrator dependency; 2h does NOT duplicate the drop-in).

**§D.4** (NEW — Clarifications 2026-05-27 Q1=B) — `.specify/2h-transport.md` §1 item 5 + §1.3 + §4.9 + §6.4 reconnect wording MUST be amended to replace "re-issue `async_connect` against the same `Transport` instance" with "destroy the dead `Transport`, sleep for `delay_for_attempt(n)`, mint a fresh `Transport` via the same `TransportFactory`, call `async_connect` on the new instance". Also documents the FR-026 caching contract (SslCtxConfig + PMR root + engine clock cached at factory, not rebuilt per attempt). Driver: reference-engine sweep convergence on fresh-per-attempt pattern (QFC + QFJ + Fix8).

**§D.5** (NEW — Clarifications 2026-05-27 Q2=C) — `.specify/2h-transport.md` §4.4 `ReconnectPolicy` value-type definition + §4.4.1 `defaults()` factory MUST be amended:
- Replace the `{initial_delay, max_delay, multiplier, max_attempts, jitter}` 5-field shape with the QuickFIX/J-aligned schedule-array shape `{std::pmr::vector<std::chrono::milliseconds> schedule, double jitter, std::uint32_t max_attempts}`.
- `defaults()` materialises the v0.2 exponential schedule as the explicit array `[100ms, 200ms, 400ms, 800ms, 1.6s, 3.2s, 6.4s, 12.8s, 25.6s, 30s]` + `jitter=0.10` + `max_attempts=10` (cumulative 73-89 s envelope preserved per design-doc §1.1 corrected numeric).
- New factory `defaults_quickfix_compat()` returns `{[30s], 0.0, 0}`.
- `delay_for_attempt(n)` semantics changes from `initial_delay × multiplier^n` to `schedule[min(n, schedule.size()-1)] × (1 + uniform(-jitter, +jitter))` matching QuickFIX/J `IoSessionInitiator::computeNextRetryConnectDelay():318-319`.
- Drops `multiplier` field (implicit in schedule).

Driver: reference-engine sweep — QFC single fixed `m_reconnectInterval = 30`; QFJ `int[] reconnectIntervalInSeconds` plateau-at-last; Fix8 single fixed `_login_retry_interval`. None have jitter; none have default cap. Industry-shape API + constitutional defaults preserves `[const §XV]` thundering-herd defense + operator-familiar surface.

---

## E-15 Cross-cuts (informational; entity ownership stays where listed)

| Cross-cut | Entity owner | 2h's wiring role |
|---|---|---|
| T-039 (FIXS cert validation predicate) | `verify_peer` owned by **011** | 2h wires the `SSL_VERIFY_PEER` trampoline that dispatches into `verify_peer(cfg, peer_chain)` |
| T-040 (FIXS secrets distribution / `cert_source`) | `cert_source` owned by **011** | 2h consumes `SslCtxConfig` through `make_ssl_ctx_config(...)`'s `cert_source` field |
| T-041 (FIXS auth↔authorization linkage / CompID-to-TLS-identity) | binding policy owned by **session Phase-4** | 2h delivers `handshake_result.peer_id` BY VALUE to the FSM |
| T-005 (multi-session acceptor) | E-8 + E-10 OWNED HERE | sole-owner |
| Durable-before-transmit invariant | `MessageStore` owned by **008** | 2h's `async_write` is purely the wire-side hop; FSM sequences `Writer::commit → store → async_write`; cancelled `async_write` does NOT rollback persisted frame |
| `Framer::feed` / `Writer::commit` | **2b** | 2h's `async_read_some` delivers bytes INTO `Framer::feed`; `async_write` consumes FROM `Writer::commit` span |
| Cancellation seam #13 witness | 011 Gate B F-1 carryover | FR-033 + SC-008 8-cell matrix (4 deterministic cases × 2 executor modes); witnessed at 012 Gate B |
| C ABI surface | **2i** | 012 publishes C++ source-of-truth + `FIXPP_ERR_TRANSPORT_*` coalescing-group naming rule only |
| Control-plane reload trigger | **2j** | 012 owns the call sites; 2j owns the RPC |
| TLS event log / OTel record schema | **2k** | 012 owns the call sites; 2k owns the schemas |
