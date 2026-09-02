# 2h — Transport interface + ASIO TCP/TLS default impl + mock seam

**Status:** Draft v0.4 — Gate A round 2 (Phase A) converged; **post-sign-off targeted amendment 2026-08-29 — Appendix D §D.1/§D.2 applied then superseded by feature 010 FR-001a; see "Appendix Z" at the END of this file (placed there because 12 line-number citations point INTO this document)**
**Date:** 2026-05-09
**Owner:** 2h-transport.md
**Inherits from:** `[arch §1.1]` (test seams via plugin interfaces; SOCKETs/clocks/exporters pluggable), `[arch §1.2]` (no SHM/DPDK/Onload in v1.0 non-goals), `[arch §2.3]` (transport may include from `core/`, `tls/`, `log/` interfaces only — no `session/` back-edge), `[arch §3]` (public namespaces — `fixpp::transport`), `[arch §4.4]` (session module surface — recipient of transport callbacks), `[arch §4.5]` (transport module surface — the spine of this doc), `[arch §4.6]` (tls/ surface consumed by the TLS sub-interface), `[arch §4.10]` (capi/ surface delegation), `[arch §5.1]` (executor model — `awaitable<T>` everywhere; per-session strand; ASIO native cancellation slots), `[arch §5.2]` (allocator policy — PMR everywhere; mimalloc default), `[arch §5.3]` (error model — `expected_t<T>` hot path, no exceptions, construction-time carve-out), `[arch §5.5]` (lifetime model — `[[clang::lifetimebound]]` on every view-returning constructor and accessor), `[arch §5.6]` (frozen-config rule — `Transport` factory frozen at session open), `[arch §5.7]` (logging hook), `[arch §5.8]` (backpressure — `block` / `disconnect_and_recover` only on app/session paths), `[arch §6]` (plugin pattern — five rules: pure-virtual class, ≤5 pure-virtual methods, one default impl, factory taking `pmr::memory_resource*`, compile-time selection), `[arch §10]` row 2h ("Transport interface — ≤5 pure-virtual surface, ASIO TCP/TLS default impl, mock seam"), `[arch §11]` (no open architectural question scoped to 2h at draft time)
**Cites:** `[const §I.2]` (in-process C++23 primary), `[const §II.2]` (no clang-cl on Windows), `[const §IV.2]` (C ABI as legal isolation seam), `[const §VI.5]` (exact-citation rule), `[const §VII.1]` (GoogleTest/GoogleMock), `[const §VII.4]` (no untested code), `[const §VII.7]` (parser-touching modules need fuzz; transport is parser-adjacent — fuzz the framing seam), `[const §VIII.1]` (perf-sensitive modules need benchmarks), `[const §VIII.2]` (perf regression budgets), `[const §VIII.5]` (zero allocation between parse and `fromApp`, extended to read-completion dispatch), `[const §X.4]` (out-of-range C-ABI code mapping), `[const §X.5]` (C-ABI thread-safety annotations), `[const §XI.1]` (`asio::awaitable<T>` composition), `[const §XI.2]` (ASIO native cancellation slots), `[const §XI.3]` (awaitable mutex required in coroutine context), `[const §XI.4]` (per-session strand default), `[const §XII.1]` (OpenSSL on both Linux and Windows), `[const §XII.5]` (`SecurityProfile` no-implicit-default), `[const §XIII.3]` (strand-stored trace context — log records carry trace_id from the session domain), `[const §XIV.1]` (v1.0 pluggable interfaces — Transport), `[const §XIV.2]` (≤5 pure-virtual on plugin interfaces), `[const §XIV.4]` (no `dlopen` plugin loading), `[const §XV.2]` (no thread-per-session blocking I/O — ASIO async only), `[const §XV.15]` (banned `drop-oldest` on app/session message paths), `[const §XVII.1]` (Codex Gate A required for Phase 2 design docs), `[FIX-SL §4.3.1]` Transport layer requirements, `[FIXS §1.1]` Scope, `[FIXS §2.2]` Mutual and Simple TLS protocol options, `[FIXS §2.4]` Certificate Validation with CA Pinning, `[FIXS §3.1]` Protocol version, `[FIXS §3.2]` Protocol features, `[FIXS §3.3]` Cipher suites, `[FIXS §3.4]` Certificate parameters, `[FIXS §4.1]` Sharing secrets, `[FIXS §4.4]` Authorization linked to authentication, `[SYN §3.2 Q6]` (HALO firing on inbound dispatch — relevant to read-path latency, owned by 2d/2f spike), `[SYN §3.4 Q16]` (transport interface — small surface, ASIO TCP/TLS default — DECIDED), `[2a §4.2]` (`trap_throw` for routing PMR throws to `expected_t`), `[2a §6.5]` (per-doc Tier 1 ceiling table precedent), `[2b §4.2]` (`Framer::feed` consumes `std::span<const std::byte>`; the read-path delivery shape), `[2b §4.5]` (`Writer::commit` finalises BodyLength + CheckSum; the post-commit span is the only valid outbound input), `[2b §6.4]` (declaration-site `[[clang::lifetimebound]]` precedent), `[2b §6.6]` (allocation discipline; three-arena PMR pinning; framer-carry-arena), `[2c §4.8]` (`owning_message_t<>` precedent for owning-value types crossing strand boundaries), `[2d §4.4]` (`EngineConfig::default_transport_factory`), `[2d §4.5]` (`SessionConfig` field shape — engine-anchor + session-`*_override`; `cert_source`; `security_profile`), `[2d §4.7]` (cancellation propagation API — two-phase close + per-mode effect table including transport `async_read` / `async_write` rows), `[2d §4.8]` (`session_executor` — project-owned wrapper class; transport callbacks rebind to it), `[2d §6.5]` (`cancellable_dispatch` — recipe for off-strand handoff), `[2d §6.7]` (per-doc-prefix `FIXPP_ERR_THREAD_*` discipline; `dispatch_aborted` joins the cancellation group), `[2d §7.5]` (TLS strand-safety boundary; mid-handshake rotation does not affect in-flight handshake), `[2d §7.6]` (transport ops run on session strand), `[2d §7.9]` (`effective_clock` for any timestamp the transport emits), `[2e §4.4]` (`MessageStoreFactory` factory ownership = `unique_ptr` per `[arch §5.6]`; mirror this for `TransportFactory`), `[2e §6.1.4]` (durable-before-transmit — outbound `store(committed_span)` must linearise BEFORE `transport.async_write`; a cancelled `async_write` MUST NOT roll back the persisted frame), `[2e §6.4]` (writer-mutex contract precedent), `[2f §4.1.1]` (`async_mutex::async_lock(...)` awaitable shape — referenced for the awaitable-API conventions, not consumed), `[2f §6.5]` (cancellation outcome at the 2f boundary; per-doc-prefix discipline; cancellation-group precedent), `[2g §1]` (Goals 1, 2, 3, 4, 5, 7, 8, 9, 10 — TLS policy core), `[2g §1.1]` (TLS DoS caps — RSA upper, cert DER, SAN cardinality), `[2g §3.7]` (two-phase close interaction with TLS), `[2g §4.1]` (`cert_source` interface — 2 pure-virtual methods), `[2g §4.2]` (`file_cert_source` default impl + `make_file_cert_source` factory), `[2g §4.3]` (`Pinset` — add-then-remove rotation; `snapshot()` and `find()` reader paths), `[2g §4.4]` (`CipherPolicy` — compile-time allow-list + `is_allowed(...)` runtime accessor), `[2g §4.5]` (`SecurityProfile` enum + `SslCtxConfig` adapter + `verify_peer` predicate + `peer_identity`), `[2g §4.5.1]` (normative `SecurityProfile`-to-OpenSSL-mode mapping table — 2h MUST configure `SSL_CTX` exactly per row), `[2g §4.6]` (construction / lifetime / ownership rules — `SessionConfig::pinset` is the published reachability), `[2g §6.1]` (TLS hot-path allocation discipline; static_assert chain on `CipherPolicy`), `[2g §6.4]` (cancellable_dispatch recipe for `load_credentials`), `[2g §6.5]` (FIXS rotation invariants; `[2g §6.5.1]` handshake-time `Pinset::snapshot()` captured-once contract), `[2g §6.6]` (TLS error variants — `tls_handshake_failed` group consumed by 2h's wiring), `[2g §7.1]` (T-039 / T-040 partition declaration — 2g owns parsed-cert validation; 2h owns the OpenSSL handshake-time hook + `SSL_CTX` construction), `[2g §7.7]` (drop-in for 2j control-plane reload), `[2g §7.8]` (drop-in for 2k OTel cert-event spans)
**Catalogue rows owned (sole):** **T-001** (TCP transport — initiator and acceptor roles, `[FIX-SL §4.3.1]`), **T-002** (TLS over TCP — OpenSSL on both Linux and Windows; ASIO has no Schannel backend, `[FIX-SL §4.3.1]` + `[FIXS §1.1]`), **T-003** (ASIO async I/O layer — non-blocking read/write with back-pressure, `[impl]`), **T-004** (Reconnect / exponential back-off — initiator retry on disconnect, `[FIX-SL §4.3.1]`), **T-005** (Multi-session TCP acceptor — accept multiple connections on one port, `[FIX-SL §4.3.1]`), **T-009** (FIXS Mutual TLS — CA pinning + leaf pinning, `[FIXS §2.4]`), **T-010** (FIXS Simple TLS — server-only auth, `[FIXS §2.2]`), **T-012** (FIXS PSK authentication — pre-shared key, `[FIXS §2.5]`; **disposition: P2 deferred to post-v1 per `[const §XII.6]`** carve-out; the transport surface is shaped to admit a future `psk_session_callback` without breaking the ≤5-pure-virtual cap — see §10 Q1).
**Catalogue rows owned (cross-cut, transport-side portions):** **T-039** (FIXS certificate parameters — 2h owns the OpenSSL `SSL_VERIFY_PEER` callback wiring that calls `2g`'s `verify_peer` predicate; partition per `[2g §7.1]`), **T-040** (FIXS secrets distribution — informational cross-cut: 2g owns the `cert_source` interface; 2h consumes loaded credentials via `cert_source::load_credentials()` + `load_trust_anchors()` only, no direct touch on the secret-distribution channel), **T-041** (FIXS authorization linked to authentication — 2h provides the surface that delivers `peer_identity` from the negotiated TLS session to the session/ Phase-4 spec; partition per `[2g §7.2]`).
**Convergence log pointer:** addresses Codex review (2 P1 / 2 P2 / 1 P3) and Opus adversarial review (combined 2 P1 / 3 P2 / 2 P3 with 0 new root causes — round 1 RCs structurally closed; 2 Codex disagreements shelved), see Appendix C.

---

## §1 Goals

1. **Lock the `fixpp::transport::Transport` plugin interface** at **5 pure-virtual methods** (`async_connect`, `async_read_some`, `async_write`, `cancel`, `close`) — exactly at the `[const §XIV.2]` ≤5 cap as published in `[arch §4.5]`. Every method (excluding `cancel` and `close`) returns `asio::awaitable<core::expected_t<T>>` per `[const §XI.1]`; cancellation propagates through ASIO native cancellation slots per `[const §XI.2]`. The interface is encryption-agnostic — TLS-aware extensions are folded into the `TlsTransport` sub-interface (§4.2) so a non-TLS plain-TCP impl implements `Transport` directly without a degenerate `async_handshake`.
2. **Lock the `fixpp::transport::TlsTransport` sub-interface** as the TLS-aware extension at **exactly 1 additional pure-virtual method** (`async_handshake`) returning a value-typed `handshake_result` POD (§4.2). The handshake's negotiated artefacts (`peer_identity`, the captured `Pinset` snapshot, the negotiated cipher-suite name) are returned by value inside `handshake_result`; the FSM holds `handshake_result` for the session lifetime as a value. **No accessor pure-virtuals** are published on the sub-interface — the v0.1 trio of `peer_identity_view()` / `captured_pinset_snapshot()` / `negotiated_cipher_suite()` accessors collapse into `handshake_result` per the `[2g §4.5]` `peer_identity` owning-by-value precedent and the `[2c §4.8]` `owning_message_t<>` cross-strand-boundary precedent. The sub-interface has its own ≤5 cap budget per `[const §XIV.2]` and uses **1 of 5** slots, leaving 4 slots of headroom for a future PSK session callback (T-012 P2 hook), an explicit renegotiation control point (post-v1), or an early-data hook (post-v1, currently banned per `[const §XII.3]` 0-RTT non-goal). Sub-interface inherits virtually from `Transport` so a `TlsTransport*` is also a `Transport*`.
3. **Lock the `fixpp::transport::asio_tls_transport` default impl** as the v1.0-shipped reference: ASIO `tcp::socket` + `asio::ssl::stream<tcp::socket>` over OpenSSL on both Linux and Windows per `[const §XII.1]`. The impl consumes `[2g §4.5]`'s `SslCtxConfig` value at session open to construct the OpenSSL `SSL_CTX`; the `SSL_VERIFY_PEER` callback dispatches into `[2g §4.5]`'s `verify_peer` predicate; the handshake-time `Pinset` access uses `[2g §6.5.1]`'s captured-once contract (capture `Pinset::snapshot()` at handshake start, scan the snapshot directly per peer-cert lookup). Reconnect with exponential back-off per `[FIX-SL §4.3.1]` is built into the impl as opt-in via `ReconnectPolicy` (§4.4).
4. **Lock the `fixpp::transport::Endpoint`** value type — `(host, port)` for client connections; `(bind_addr, bind_port, backlog)` for server (acceptor) sockets. Address family is auto-detected from the host string (`IPv4` / `IPv6` / hostname-resolved-via-`asio::ip::resolver`); IPv6 zone-id support is in scope. Acceptor / initiator distinction is reflected by two construction paths on the default impl (§4.6).
5. **Lock the `fixpp::transport::ReconnectPolicy`** value type per `[FIX-SL §4.3.1]` (Clarifications 2026-05-27 Q2=C / Appendix D §D.5 shape): `schedule` (`std::pmr::vector<std::chrono::milliseconds>` — caller-supplied per-attempt delays; QuickFIX/J-aligned shape), `jitter` (`±10%` default to avoid thundering-herd), and `max_attempts` (default **`10`** per §4.4.1 normative `defaults()` factory; numeric ceiling per `[const §XV]` thunder-herd-pattern intent; opt-in to unbounded by explicit override). `defaults()` materialises the v0.2 exponential envelope as the explicit schedule `[100ms, 200ms, 400ms, 800ms, 1.6s, 3.2s, 6.4s, 12.8s, 25.6s, 30s]` with `jitter = 0.10`, `max_attempts = 10`. New `defaults_quickfix_compat()` factory returns `{[30s], 0.0, 0}` for industry-canonical operators. The session FSM (Phase-4 spec) drives reconnection per Clarifications 2026-05-27 Q1=B (Appendix D §D.4) by destroying the dead `Transport`, sleeping for `delay_for_attempt(n)`, minting a fresh `Transport` via the same `TransportFactory`, and calling `async_connect` on the new instance — matching the QuickFIX-cpp / QuickFIX/J / Fix8 fresh-per-attempt pattern. The `ReconnectPolicy` is policy data that the session-module FSM consults — 2h owns the value type + the normative `defaults()`, the session-module Phase-4 spec owns the FSM that consumes it (per §7.2).
6. **Lock the `fixpp::transport::Listener`** acceptor surface (T-005) — multi-session TCP acceptor that produces `Transport` instances on each `accept()` completion. The listener is itself a small pluggable abstraction (one pure-virtual `async_accept`) that ships with one default impl (`asio_listener`) wrapping `asio::ip::tcp::acceptor`.
7. **Lock the `fixpp::transport::TransportFactory`** factory surface per `[arch §6]` rule 4: `make()` returns `expected_t<std::unique_ptr<Transport>>`. **Factory ownership** ⚠️ **SHIPPED TYPE DIFFERS: `shared_ptr`, NOT `unique_ptr` — feature 010 FR-001a; see `include/fixpp/session/session_config.hpp` and Appendix Z. What follows is the superseded design:** `unique_ptr<TransportFactory>` on `SessionConfig::transport_factory_override` matching the `[2e §4.4]` precedent for `MessageStoreFactory` (and the cross-doc-amendment pattern that landed there) — see §4.7 + Appendix D §D.1. Engine-anchor + session-`*_override` matches `[2d §4.4]` `default_transport_factory` (already typed) + `[arch §5.6]` frozen-at-open rule.
8. **Lock the `fixpp::transport::mock_transport`** test seam — a deterministic in-memory `Transport` impl living in a public test header `<fixpp/transport/test/mock_transport.hpp>` (consumed by `tests/`-only translation units). Drives the FSM-under-test per `[const §VII]` / `[arch §1.1]` test-seam goal. The mock honours every cancellation contract the production impl does, so the FSM tests catch cancellation-propagation regressions.
9. **Stay zero-allocation on the read-path completion-handler dispatch** per `[const §VIII.5]`: the read buffer is a caller-supplied `std::span<std::byte>` aliasing 2b's framer-carry buffer (or, more generally, a session-arena-backed PMR buffer the session FSM owns); the transport never allocates on the read path. The `async_read_some` completion handler fires on the session strand and returns the byte count + cancellation-aware `expected_t<...>` without heap touch. The `[const §VIII.5]` discipline extends to "between completion-handler entry and `Framer::feed` exit" — exactly the parse↔`fromApp` window 2b/2d already extended.
10. **Stay exception-free across the read/write hot path** per `[arch §5.3]`: every `expected_t<T>`-returning method is `[[nodiscard]]`; every accessor returning a non-owning view (`std::span<const std::byte>`, `std::string_view`, `peer_identity` views) carries `[[clang::lifetimebound]]` at the **declaration site of the abstract base** per `[arch §5.5]` and the `[2b §6.4]` / `[2g §4.1]` precedent. PMR throws on the rare cold path (e.g., the write-queue's per-frame node allocation under `block` backpressure mode) are routed through `[2a §4.2]` `trap_throw` and surface as `error::transport_*` variants per §6.6.
11. **Honour ASIO native cancellation slots** per `[const §XI.2]` end-to-end: every async transport entry point composes through `co_await` checkpoints and surfaces cancellation as `expected_t::unexpected{transport_*_cancelled}` on the awaitable result channel. Cancellation propagates through `[2d §6.5]`'s `cancellable_dispatch` precedent for any handoff that is NOT a bare ASIO `async_*` operation. The `Session::close` two-phase contract per `[2d §4.7]` drives transport cancellation as the per-mode effect table specifies (graceful: in-flight reads / writes continue under the root state until phase 1 resolves; terminal: root `cancellation_type::total` fires immediately).
12. **Operationalise the durable-before-transmit invariant** per `[2e §6.1.4]`: `Transport::async_write` MUST NOT be called until the outbound `store(committed_span, outbound)` per `[2e §6.1.4]` returns success; a cancelled `async_write` MUST NOT trigger any rollback of the persisted frame (the contract is "durable then transmit; cancel only cancels transmit"). 2h's surface guarantees this by API construction — the FSM (Phase-4 spec) sequences `Writer::commit → store → async_write`; 2h's `async_write` is purely the wire-side hop. Surfaced explicitly in §6 as a binding invariant.
13. **Partition T-039 / T-040 / T-041 with 2g and the session-module Phase-4 spec** explicitly per `[2g §7.1]` / `[2g §7.2]`: 2h owns the OpenSSL `SSL_CTX` construction + `SSL_VERIFY_PEER` callback wiring + handshake-time `Pinset::snapshot()` capture point + the `peer_identity` accessor that delivers the negotiated TLS identity to the session FSM for T-041 binding. 2g owns the policy core; the session-module Phase-4 spec owns the CompID-to-TLS-identity binding rule. 2h is the wiring layer between them.

### §1.1 Magnitude domain / scope boundary

The boundary 2h owns is **the byte stream over TCP[/TLS], framing-agnostic**: bytes flow into 2b's `Framer::feed` from `async_read_some`, and bytes flow out from 2b's `Writer::commit`'s span into `async_write`. The wire/ module owns FIX framing; 2g owns cert / policy / pinset; this doc owns connect / read / write / cancel / close + TLS handshake plumbing.

DoS bounds and resource ceilings (matching `[2g §1.1]` / `[2b §1.2]` / `[2e §1.2]` per-doc precedent — bench-time soft caps where I/O-bound, hard caps where allocation-bound):

- **Per-session in-flight read window.** The transport reads into a caller-supplied `std::span<std::byte>` of up to **`Transport::Config::max_read_window_bytes = 256 KiB`** (matches `[2b §1.2]` `default_max_frame_bytes` so a single `async_read_some` can deliver one full max-size frame plus partial-trailing carry). Larger windows do not improve throughput on a TCP socket — the kernel receive buffer dominates. Configurable per session.
- **Per-session in-flight write queue depth.** Outbound writes through `async_write` are serialised on the session strand by API construction (the strand is the queue of depth 1 for sequential `await`s); when the session FSM uses **`block` backpressure mode** per `[arch §5.8]` / `[const §XV.15]`, a second outbound coroutine that suspends on the strand simply waits its turn. **No internal `Transport`-owned queue** — backpressure is owned by the strand discipline. Hard cap: zero (the strand IS the queue). The v1.0 design does NOT introduce a transport-internal write queue; if a future async multiplexed write surface is needed (e.g., for parallel streams over a single TCP), it would be a post-v1 design (out of v1.0 scope).
- **Per-session in-flight write byte ceiling.** A single `async_write` is bounded by `Transport::Config::max_write_size_bytes = 1 MiB` (one frame at the v1.0 `[2b §1.2]` 256 KiB max plus 4× headroom for outbound batching by users on options/derivatives venues that raise their `[2b §1.2]` cap). Larger writes split into multiple `async_write` calls is the FSM's call (the FSM owns the application protocol), not the transport's. A peer-side advertised receive window smaller than this cap is honoured by ASIO/TCP at the kernel level.
- **TCP `SO_RCVBUF` / `SO_SNDBUF`.** Default to OS auto-tuning (`asio` does not touch them by default). `Transport::Config::tcp_recv_buf_bytes` and `tcp_send_buf_bytes` (default `0` = OS auto-tune) are exposed for HFT users who want to pin them. Tier 2 / Windows benches verify the auto-tune path doesn't pessimise throughput.
- **TLS handshake timeout.** `Transport::Config::tls_handshake_timeout = 30 s` (default; matches typical exchange/venue gateway). `verify_peer` runs synchronously inside the `SSL_VERIFY_PEER` callback so its cost (`[2g §6.3]` ≤ 50 µs p99) is rolled into the handshake's wall-clock budget. A handshake that exceeds the timeout completes with `transport_handshake_timeout`; cancellation propagates per `[const §XI.2]`.
- **TCP keepalive.** OS-default off; opt-in via `Transport::Config::tcp_keepalive = false` (default) / `tcp_keepalive_idle_seconds`, `tcp_keepalive_interval_seconds`, `tcp_keepalive_count`. FIX-level Heartbeat per `[FIX-SL §4.5.1]` is the primary keep-alive (owned by the session-module Phase-4 spec); TCP keepalive is defence-in-depth.
- **Reconnect back-off envelope.** Per Clarifications 2026-05-27 Q2=C / Appendix D §D.5: `ReconnectPolicy::defaults()` (per §4.4.1) materialises the schedule as `[100 ms, 200 ms, 400 ms, 800 ms, 1.6 s, 3.2 s, 6.4 s, 12.8 s, 25.6 s, 30 s]` with `jitter = 0.10` and **`max_attempts = 10`** (numeric ceiling — opt-in to unbounded by setting `max_attempts = 0` explicitly per `[const §XII.5]` no-implicit-default pattern). Total wall-clock to terminal stop at `max_attempts = 10` ≈ 1 min 21 s (sum = 81 100 ms; ±10% jitter widens to ≈ 73–89 s; v0.2's "≈ 4 min 25 s" figure was arithmetically wrong — that figure conflated the per-attempt delay at attempt 10 with the cumulative envelope and is corrected here per round-2 Opus N-P1). A new `defaults_quickfix_compat()` factory returns `{[30s], 0.0, 0}` for industry-canonical operators. **Rationale (DoS-surface explicit per `[const §XV]` thunder-herd-pattern intent):** the v0.1 default `max_attempts = 0` (unbounded) shipped an engine that could DDOS a venue under common operational scenarios (10⁵ peers reconnecting against an exchange gateway after a venue outage cluster reconnect attempts in narrow windows once the schedule plateaus at its 30 s tail — a known TCP/TLS thundering-herd amplifier). v0.2 picks a numeric ceiling per the `[2g §1.1]` DoS-cap precedent (every cap is numeric, not "unbounded by default"); the resulting ≈ 1 min 21 s default envelope is intentionally tight against the thunder-herd hazard (shorter than typical exchange-gateway recovery windows of 30 s – 5 min). Operators that need a longer envelope opt in by overriding `max_attempts` explicitly OR by extending the schedule tail; v1.0 ships the safe default. On exceeding the cap, the FSM surfaces `transport_reconnect_limit_exceeded` (§6.6).

These are operator-tunable bounds (not invariants of the FIX or FIXS specs) sized for v1.0's named workloads — FX retail / equities / equity-options. Users on derivatives venues (`[2b §1.2]` calls them out specifically) tune `max_read_window_bytes` upward; the §1.1 caps are documented operating points.

### §1.2 Scope boundary — what 2h owns vs what it doesn't

2h **owns**:

- The `fixpp::transport::Transport` pure-virtual interface (§4.1, 5 pure-virtual).
- The `fixpp::transport::TlsTransport` pure-virtual sub-interface (§4.2, exactly 1 additional pure-virtual returning `handshake_result` value type).
- The `fixpp::transport::Endpoint` value type (§4.3) and the `fixpp::transport::ReconnectPolicy` value type (§4.4).
- The `fixpp::transport::asio_tls_transport` default impl wrapping ASIO `tcp::socket` + `asio::ssl::stream<tcp::socket>` (§4.5).
- The `fixpp::transport::Listener` pure-virtual interface (§4.6) and the `fixpp::transport::asio_listener` default impl.
- The `fixpp::transport::TransportFactory` factory surface (§4.7); `EngineConfig::default_transport_factory` already declared in `[2d §4.4]`; `SessionConfig::transport_factory_override` field shape declared here.
- The `fixpp::transport::mock_transport` public-test-header impl (§4.8) for FSM-under-test.
- The OpenSSL `SSL_CTX` construction lifetime + the `SSL_VERIFY_PEER` callback wiring that consumes `[2g §4.5]` `verify_peer` (T-039 cross-cut transport-side).
- The handshake-time `Pinset::snapshot()` capture point (per `[2g §6.5.1]` binding contract) + the wiring that delivers the captured snapshot into `verify_peer` (`SslCtxConfig::pinset` is reachable from the snapshot capture).
- The `error::transport_*` variants per §6.6 and their per-doc-prefix `FIXPP_ERR_TRANSPORT_*` C-ABI coalescing groups (delegated to 2i).
- The transport-side hand-off shape that delivers `peer_identity` to the session-module Phase-4 spec for T-041 binding (§4.2 + §7.2).
- The latency Tier 1 ceilings on read-path completion-handler dispatch and write-path issue (§6.3).

2h **does not own**:

- FIX framing (`[2b §4.2]` Framer; `[2b §4.3]` Parser; `[2b §4.5]` Writer) — owned by **2b**. The transport delivers raw bytes; 2b interprets them.
- The `cert_source` interface, `Pinset` value, `CipherPolicy` allow-list, `SecurityProfile` enum, `verify_peer` predicate, `peer_identity` value type — owned by **2g**. 2h consumes them through `[2g §4.5]`'s `SslCtxConfig` adapter.
- The session FSM (Logon flow, gap fill, ResendRequest, sequence reset, reconnect-policy-driven retry decisions) — owned by the **session-module Phase-4 spec**.
- The CompID-to-TLS-identity binding (T-041 policy half) — owned by the **session-module Phase-4 spec**. 2h delivers `peer_identity` via the `TlsTransport` accessor; the session FSM applies the binding rule.
- The control-plane trigger that initiates a graceful drain on `Logout` or a cert-source reload — owned by **2j**. 2h provides the surface 2j calls into (graceful close + reconnect; see §7.6).
- TLS-event log/OTel record schema — owned by **2k**. 2h owns the call sites on the transport side (handshake start/success/failure spans; reconnect-attempt counters); 2k owns the schemas.
- The C ABI surface for `Transport` / `TransportFactory` / `Listener` — owned by **2i** (§5).
- SHM / DPDK / Onload / kernel-bypass impls — out of v1.0 per `[arch §1.2]` non-goals; the `Transport` interface is shaped to admit them in post-v1 if needed.
- Custom binary protocols (FIXP / SBE / FAST / SOFH) — out of v1.0 per `[const §XVIII.2]`; the v1.0 transport is FIX Tag=Value SOH only.
- UDP transport — out of v1.0 (`[FIX-SL §4.3.1]` mandates TCP; UDP is a post-v1 question if a venue requires it).
- PSK authentication (T-012) — P2 deferred per `[const §XII.6]`. The `TlsTransport` cap at 1 pure-virtual leaves headroom for a future `psk_session_callback` slot; the v1.0 default impl rejects `SSL_CTX_set_psk_*` configuration with `transport_psk_unsupported`.

---

## §2 Non-goals

- **No SHM / DPDK / Onload / kernel-bypass transports** in v1.0 per `[arch §1.2]`. The interface is shaped to admit them post-v1 (`Transport` is encryption-agnostic; the byte-stream contract maps to any reliable bidirectional channel) but no impl ships in v1.0.
- **No custom binary FIX encodings** (FIXP / SBE / FAST / SOFH / Orchestra) — out of v1.0 per `[const §XVIII.2]`.
- **No UDP transport** — `[FIX-SL §4.3.1]` mandates TCP. A future UDP-FIX-over-FIXP design would build a different `Transport` impl; the surface is unchanged.
- **No non-TLS encryption.** Application-layer encryption is banned per `[const §XII.7]` / `[const §XV.10]`; encryption lives at TLS only. A future plain-TCP-with-TLS-tunnel-via-stunnel deployment is the user's concern (the Transport sees plain TCP); v1.0 does not ship a third-party-tunnel adapter.
- **No Schannel / no Windows-native TLS backend.** OpenSSL on both Linux and Windows per `[const §XII.1]`.
- **No `dlopen`-based plugin loading** for `Transport` per `[const §XIV.4]`. Compile-time selection only.
- **No mid-session swap of `TransportFactory`.** The factory **selection** is frozen at session open per `[arch §5.6]`. Per Clarifications 2026-05-27 Q1=B (Appendix D §D.4) reconnect destroys the dead `Transport` and mints a fresh one via the same `TransportFactory` — the `Transport` **instance** is short-lived (per-attempt), not frozen; the FSM does not swap `TransportFactory` mid-session.
- **No transport-internal write queue.** The session strand is the depth-1 queue; outbound writes are serialised by API construction (per `[arch §5.8]`'s `block` mode). v1.0 does not introduce a parallel-writes-on-one-transport facility.
- **No 0-RTT / TLS 1.3 early data.** Banned per `[const §XII.3]`.
- **No PSK in v1.0 default impl.** `T-012` is P2 deferred. The `TlsTransport` sub-interface leaves headroom (4 of 5 pure-virtual slots) for a future `psk_session_callback` hook without a major-version bump.
- **No TCP-level retransmit policy override.** Linux / Windows kernel TCP retransmit is the operating system's job; FIX-level recovery (`ResendRequest`) is the session-module Phase-4 spec's job. 2h does not expose any TCP retransmit knobs (only `tcp_keepalive` and the Nagle/`TCP_NODELAY` / `SO_LINGER` defaults — see §4.5 Config).
- **No async DNS resolution caching policy.** ASIO's resolver is consulted per `async_connect`; users that need DNS-result caching wrap the resolver themselves before constructing the `Endpoint`. v1.0's default behaviour is "resolve at connect time, no cache."

---

## §3 Inherited surface

This section quotes — verbatim, short excerpts — every binding section that constrains 2h. Each excerpt names the doc it inherits from and why it binds. Per the §3-preamble convention established by `[2g §3]`: where a tightening of any sibling-published wording is needed, declare an Appendix D drop-in; do **not** silently reword.

### §3.1 From `[arch §4.5]` — the transport/ surface inventory (the spine)

> **Public surface:**
>
> - `fixpp::transport::Transport` — interface. **≤5 pure-virtual methods** `[const §XIV.2]`: `async_connect`, `async_read_some`, `async_write`, `cancel`, `close`. (`async_handshake` for TLS is folded into a TLS-aware sub-interface; see **2h**.)
> - `fixpp::transport::Endpoint`, `fixpp::transport::ReconnectPolicy` — value types.
> - `fixpp::transport::asio_tls_transport` — default ASIO TCP/TLS over OpenSSL impl `[const §XII.1]`.
>
> **Catalogue rows:** T-001 to T-013, plus T-039, T-040, T-041 (cross-cuts).

This is the spine of the present doc; §4 expands every bullet. Owned rows are claimed in Appendix A; the cross-cut rows (T-006, T-007, T-008, T-011, T-013) are owned by 2g per `[arch §4.6]`.

### §3.2 From `[arch §5.1]` — executor model

> **Per-session strand.** Construction wraps the user executor in `asio::make_strand(...)` unless the user supplies an explicit `executor` opt-out in `SessionConfig`. Application callbacks always dispatch onto the strand `[const §XI.4]`.
>
> **Coroutine composition.** `asio::awaitable<T>` is the return type of every async session/transport entry point `[const §XI.1]`. Cancellation flows via ASIO native cancellation slots `[const §XI.2]`.

This binds §4.1 / §4.2 / §6.4: every async transport method returns `awaitable<expected_t<T>>` and honours the cancellation slot.

### §3.3 From `[arch §5.2]` — allocator policy

> **Hot-path discipline.** Zero `new`/`delete` between parse and `fromApp` callback `[const §VIII.5]`. The `tools/check_alloc.py` post-link symbol scan and the bench-time `mallocnesia` interceptor guard against regressions.

This binds §6.1: the read-path completion-handler dispatch is part of the parse↔`fromApp` window (the bytes pass through 2h before 2b sees them), so 2h's read path is zero-global-heap.

### §3.4 From `[arch §5.3]` — error model

> **Hot path is exception-free.** No `throw` between parse and `fromApp` `[const §VIII.5]`. Exceptions are reserved for construction-time configuration errors (e.g., bad dictionary XML), where the alternative is `expected_t<Engine>` and we choose throw for ergonomics.
> **C ABI translates** `fixpp::core::error` → `fixpp_error_t` at the boundary.

This binds §6.6 (every transport variant lives under `FIXPP_ERR_TRANSPORT_*`) and §4.5 (the `asio_tls_transport` constructor throws on `SSL_CTX` construction failure under `[arch §5.3]`'s carve-out; the factory wraps in `expected_t<...>` for non-construction-time callers).

### §3.5 From `[arch §5.6]` — frozen-at-open rule

> **`SessionConfig` is value-typed and frozen at session open.** No mid-session reconfiguration of: dictionary, security profile, message store, executor, lock policy, dialect overlay.

`Transport` (and `TransportFactory`) is frozen at session open by direct extension of this rule. The only mid-session-mutable TLS surface is `Pinset` (per `[arch §5.6]`'s explicit carve-out and `[2g §3.2]`); 2h reaches the live `Pinset` through `SessionConfig::pinset` (per `[2g §4.6]`) at handshake start.

### §3.6 From `[arch §6]` — plugin pattern

> Each pluggable interface gets:
> 1. A pure-virtual class in the relevant module's public header.
> 2. **≤5 pure-virtual methods** `[const §XIV.2]`.
> 3. One default implementation shipped in v1.0.
> 4. A clear factory entry point that takes a `std::pmr::memory_resource*` plus interface-specific config.
> 5. Compile-time selection in v1.0; no `dlopen`.

§4.1 / §4.2 / §4.5 / §4.6 / §4.7 satisfy all five rules. The `TlsTransport` sub-interface honours rule 2 independently — it has its own ≤5 cap budget and uses 1 of 5.

### §3.7 From `[arch §10]` row 2h

> Transport interface — ≤5 pure-virtual surface, ASIO TCP/TLS default impl, mock seam. Cross-cutting hooks: §6 plugin pattern; §4.5.

This is the row's full handoff statement. The mock seam is operationalised in §4.8 + §9 seam #6.

### §3.8 From `[const §XII.5]` — `SecurityProfile` no-implicit-default

> **`Session` construction requires an explicit `SecurityProfile` choice — there is no implicit default.**

2h's `asio_tls_transport` consumes `SslCtxConfig::profile` (per `[2g §4.5]`). Construction-time validation (rejected sentinel + null `Pinset`-with-`mtls_pinned` + non-null `Pinset`-with-`one_way_ca` + null `clock`) is owned by `[2g §4.5]`'s `make_ssl_ctx_config(...)`; 2h's role is to propagate the resulting `expected_t<SslCtxConfig>` upward to the session-module Phase-4 spec at `Session::open` (the outer rejection is `error::invalid_session_config` per `[2d §6.7]`; the inner is `tls_invalid_security_profile` per `[2g §6.6]`; the partition is in `[2g §7.2]`).

### §3.9 From `[2b §4.2]` / `[2b §4.5]` — wire's read/write contract

> `frame_view::body() ... aliases the View's data; lifetime is the originating buffer's.`
> `Writer::commit() && noexcept ... fills in BodyLength (tag 9) and CheckSum (tag 10) and returns the total byte count written. After commit, the Writer is consumed.`

§7.1 binds the read- and write-path byte contracts: 2h's `async_read_some` writes into a caller-supplied buffer that the FSM (or 2b's `Framer::pmr_carry_buffer`) owns; 2h's `async_write` consumes the post-`commit` span the FSM passes in. **2h never instantiates `frame_view` or `MessageView`** — those are 2b's domain; 2h sees raw bytes only.

### §3.10 From `[2b §6.6]` — three-arena PMR pinning

> Three-arena split per [2b §6.6] / [2b §8] (per-message, framer-carry, session-lifetime; the parser-completion arena 2b folds into per-message — N-P3-1 editorial).

`Transport::async_read_some`'s caller-supplied buffer is typically the framer-carry arena (per `[arch §5.2]` allocator policy + `[2b §6.6]` framer-carry pinning). The transport never allocates the buffer — it writes into one the caller owns.

### §3.11 From `[2d §4.4]` / `[2d §4.5]` — `EngineConfig` / `SessionConfig` field shape

> `std::shared_ptr<fixpp::transport::TransportFactory> default_transport_factory;`

`[2d §4.4]` already published `default_transport_factory` as `shared_ptr<TransportFactory>`. **2h flags this as an inherited inconsistency** (mirrors the `[2e §4.4]` precedent on `MessageStoreFactory`): factory ownership should be `unique_ptr` per `[arch §5.6]` (no mid-session swap, no shared transport factory across sessions). Cross-doc amendment owed — declared in Appendix D §D.1 and applied at 2h sign-off per the `[2e App D §D.1]` / `[2g App D §D.1]` sibling-doc-edit precedent. Until that commit lands, 2h's factory CONSTRUCTION is `std::unique_ptr<Transport>`-returning regardless; the `EngineConfig` / `SessionConfig`-side field type is the gating sibling-doc edit.

### §3.12 From `[2d §4.7]` — cancellation propagation API + per-mode effect table

> **Phase 1 — graceful close.** … the Logout `async_write` and the Logout-wait `Clock::sleep_until(...)` run under the child slot. … **Phase 2 — teardown.** When phase 1 resolves … the root cancellation slot fires `asio::cancellation_type::total`, propagating to in-flight transport `async_read` / `async_write`, the heartbeat `Clock::sleep_until`, the awaitable-mutex acquire, the application-callback dispatch (via `cancellable_dispatch`), and the parser → `fromApp` chain.

§6.4 obeys: 2h's `async_read_some` / `async_write` are listed in `[2d §4.7]`'s effect table; phase 1 leaves them running; phase 2 fires `cancellation_type::total`. The Logout `async_write` runs under the child cancellation state per `[2d §4.7]`. 2h does not own the close sequencer; it owns the cancellation-aware completion contract.

### §3.13 From `[2d §6.5]` — `cancellable_dispatch` recipe

> `cancellable_dispatch(session_executor exec, asio::cancellation_slot slot, Handler&& handler)` returns `awaitable<expected_t<void>>` with `dispatch_aborted` on slot-signal-before-pickup.

`cancellable_dispatch` is consumed by 2h **only** when 2h's default impl posts work to a non-session executor (e.g., DNS resolve on a worker thread; the OpenSSL signing-callback path that 2g's `async_signer_ref` carries). The recipe is consumed verbatim per `[2g §6.4]` — this is the third pluggable awaitable-handoff in the project after `[2d §4.1.1]` `Clock::sleep_until` and `[2g §4.1]` `cert_source::load_credentials`, and inherits the same recipe-publication obligation; §6.5 publishes the body shape.

### §3.14 From `[2d §7.6]` — transport ops on session strand

> `Transport::async_connect`, `async_read_some`, `async_write`, `cancel`, `close` all run on the session strand. The default `asio_tls_transport` impl uses ASIO's own composed operations; cancellation propagates through ASIO's slot mechanism. Transport reset (e.g., reconnect after a network blip) destroys the dead `Transport` and mints a fresh one via the same `TransportFactory` (per Appendix D §D.4); `async_connect` is issued on the new instance on the same strand. 2d locks the strand-discipline; 2h owns the impl.

This is the locked contract surface 2h must deliver: every `Transport` async op binds to the awaiter's bound executor (the `[2d §4.8]` `session_executor` wrapper), completing on the same wrapper. Operationalised in §6.1.

### §3.15 From `[2e §6.1.4]` — durable-before-transmit

> **Outbound:** the FSM has not yet called `transport::async_write`, so no transmission happened — the cancellation is benign per root cause #1.
>
> Cancellation that lands AFTER `store()` linearises and BEFORE `transport::async_write` issues — frame **DURABLE** on disk per §6.3 algorithm; in-memory state **STABLE**; recovery contract **HONOURED**. `store_cancelled` is **NOT** surfaced — the frame did persist.

This is the binding invariant for §6.7 (`Transport::async_write` MUST NOT roll back a persisted frame on cancel). §6 surfaces it as an explicit contract; the §9 seam #8 (durable-before-transmit ordering, cross-doc with 2e) tests it.

### §3.16 From `[2g §3.7]` / `[2g §4.5]` / `[2g §4.5.1]` / `[2g §4.6]` / `[2g §6.5.1]` — TLS policy core

The full TLS policy core 2h consumes: `cert_source::load_credentials() -> awaitable<expected_t<local_credentials>>` (§4.1 of 2g, 2 pure-virtual interface); `Pinset::snapshot()` reader path (`[2g §4.3]`); `CipherPolicy::is_allowed(...)` runtime accessor (`[2g §4.4]`); `SecurityProfile` enum + `SslCtxConfig` adapter + `verify_peer` predicate + `peer_identity` value (`[2g §4.5]`); the **normative `SecurityProfile`-to-OpenSSL-mode mapping table** at `[2g §4.5.1]` — 2h MUST configure the OpenSSL `SSL_CTX` exactly per row; the `Pinset` reachability shape at `[2g §4.6]` (`SessionConfig::pinset` is the published reachability — 2h does NOT downcast `cert_source*` to a concrete impl); and the handshake-time `Pinset::snapshot()` captured-once contract at `[2g §6.5.1]` (capture once at handshake start, scan the snapshot directly per peer-cert lookup, do NOT call `find()` repeatedly).

This binds §4.5 (default impl behaviour) and §6.2 (handshake invariants).

This document refines the inherited surface; it does **not** diverge.

---

## §4 Public C++ API

### §4.1 `fixpp::transport::Transport` — abstract interface (5 pure-virtual; AT the `[const §XIV.2]` cap)

The interface is encryption-agnostic — the TLS-aware extension is the `TlsTransport` sub-interface (§4.2). The five methods are normatively published in `[arch §4.5]`; this section defines their exact signatures + lifetime contracts + cancellation shapes.

```cpp
// include/fixpp/transport/transport.hpp
#include <asio/awaitable.hpp>
#include <asio/cancellation_type.hpp>
#include <cstddef>
#include <span>
#include <fixpp/core/expected.hpp>

namespace fixpp::transport {

class Endpoint;     // §4.3 — value type.
struct ConnectInfo; // §4.1.1 — small POD with negotiated remote endpoint.

// Abstract interface. EXACTLY 5 pure-virtual methods AT the [const §XIV.2]
// ≤5 cap; matches [arch §4.5]'s normative published list.
//
// All methods run on the session strand per [2d §7.6]. Cancellation flows
// through ASIO native cancellation slots per [const §XI.2]; the awaiter's
// cancellation_state is read with `co_await asio::this_coro::cancellation_state`
// per the [2d §6.5] / [2g §6.4] recipe pattern.
class Transport {
public:
    virtual ~Transport() = default;

    // (1) Establish the connection. For TCP transports, this is the connect()
    //     handshake (kernel SYN → SYN-ACK → ACK); for TlsTransport (§4.2), the
    //     async_handshake is a SEPARATE step that the FSM issues after this
    //     completes successfully. The awaitable completes with ConnectInfo
    //     (negotiated remote endpoint, local endpoint, family) on success or
    //     transport_*_failed on a typed failure (resolution, refused, timeout,
    //     cancellation).
    //
    //     Cancellation: cancellation_type::total at any suspension point inside
    //     the resolver / connect chain causes the awaitable to complete with
    //     transport_connect_cancelled.
    //
    //     Idempotency: a second async_connect from a SUCCEEDED state, or while
    //     one is IN FLIGHT (#342), returns transport_already_connected; after a
    //     FAILED attempt the Transport is still fresh and really retries.
    [[nodiscard]] virtual asio::awaitable<core::expected_t<ConnectInfo>>
        async_connect(Endpoint const& ep) = 0;

    // (2) Read up to `buf.size()` bytes from the peer into the caller-supplied
    //     buffer. The buffer aliases caller-owned storage (typically 2b's
    //     framer-carry arena); the transport NEVER allocates a read buffer.
    //     The awaitable completes with the byte count actually read (always
    //     > 0 on success — the underlying ASIO async_read_some completes when
    //     at least one byte is available; partial reads are normal and expected
    //     by 2b's Framer). EOF (peer closed) completes with transport_read_eof.
    //
    //     Cancellation: cancellation_type::total causes the awaitable to
    //     complete with transport_read_cancelled. Partial reads up to the
    //     cancellation point are LOST per ASIO's contract — the FSM treats
    //     this as a torn read and (under the v1.0 close-on-cancel contract)
    //     drives a session disconnect.
    //
    //     The buf span's [[clang::lifetimebound]] is on the parameter.
    [[nodiscard]] virtual asio::awaitable<core::expected_t<std::size_t>>
        async_read_some(std::span<std::byte> buf [[clang::lifetimebound]]) = 0;

    // (3) Write the entire `bytes` span to the peer (composed write — the
    //     ASIO equivalent of asio::async_write, NOT async_write_some). The
    //     awaitable completes with bytes.size() on success or transport_*_failed
    //     on a typed failure.
    //
    //     [2e §6.1.4] DURABLE-BEFORE-TRANSMIT INVARIANT (binding): the caller
    //     MUST NOT call async_write until the corresponding outbound store(...)
    //     has linearised. A cancelled async_write MUST NOT roll back the
    //     persisted frame. The contract is "durable then transmit; cancel only
    //     cancels transmit" — 2h's surface guarantees this by API construction
    //     (the FSM sequences Writer::commit → store → async_write; 2h's
    //     async_write is purely the wire-side hop). The §9 seam #8 verifies.
    //
    //     Cancellation: cancellation_type::total causes the awaitable to
    //     complete with transport_write_cancelled. A short write (some bytes
    //     sent, some not) is treated as a torn write — the FSM disconnects
    //     and recovers via [FIX-SL §4.5.2] ResendRequest. The persisted
    //     frame is NOT rolled back per [2e §6.1.4].
    //
    //     The bytes span's [[clang::lifetimebound]] is on the parameter; the
    //     caller MUST keep the bytes alive past the awaitable's completion
    //     (typically the caller pins the per-message arena that holds the
    //     post-commit span — see [2e §6.1.4] / [2b §4.5]).
    [[nodiscard]] virtual asio::awaitable<core::expected_t<std::size_t>>
        async_write(std::span<const std::byte> bytes [[clang::lifetimebound]]) = 0;

    // (4) Cancel any in-flight async_connect / async_read_some / async_write
    //     / async_handshake. Synchronous; CALL IT ON THE SESSION STRAND (the
    //     "safe from any thread" claim here was struck 2026-09-02, #333/#340:
    //     no impl emits a cancellation_signal, and asio calls a shared socket
    //     Unsafe). Idempotent. Returns expected_t<void> for symmetry ONLY
    //     with the other failable ops (the only documented failure is
    //     transport_already_closed when called after close()).
    //
    //     cancel() is the synchronous half of the [const §XI.2] cancellation
    //     contract; the awaitable methods complete with their *_cancelled
    //     variants. cancel() does NOT close the socket — the FSM may decide
    //     to retry a cancelled connect/read/write.
    [[nodiscard]] virtual core::expected_t<void> cancel() noexcept = 0;

    // (5) Close the transport. Synchronous on the session strand. After close()
    //     returns, no further async_connect / async_read_some / async_write /
    //     async_handshake is permitted (each returns transport_already_closed).
    //     For TLS transports, close() initiates a graceful TLS shutdown
    //     (SSL_shutdown bidirectional close-notify) on a best-effort basis —
    //     a 1-second timeout bounds the wait per Transport::Config::tls_close_timeout
    //     to avoid blocking the strand on an unresponsive peer; a non-graceful
    //     close (counterparty unresponsive, network partition) is reported as
    //     transport_tls_close_truncated but not treated as a hard error.
    //
    //     Idempotency: calling close() twice on the same Transport returns
    //     expected_t<void>{} on the second call without side effects.
    [[nodiscard]] virtual core::expected_t<void> close() noexcept = 0;
};

// ConnectInfo — small POD describing the negotiated socket endpoints.
// Owning by value; no view fields. Returned by async_connect.
struct ConnectInfo {
    Endpoint remote;       // negotiated peer endpoint (post-resolution).
    Endpoint local;        // local-side bind endpoint.
    int      family;       // AF_INET or AF_INET6.
};

}  // namespace fixpp::transport
```

**Pure-virtual count:** 5. AT the `[const §XIV.2]` cap of 5 with zero slots of headroom — every slot is a normative semantic (connect / read / write / cancel / close); no slot is boilerplate. **Justification (Gate-A-eligible):** every method is named in the published `[arch §4.5]` surface; removing any one collapses a normative semantic the session FSM relies on (the `Application` callback interface used the same justification at `[arch §6]`'s six-method exception). The five names are the minimum non-overlapping set: connect (one-time setup), read (recurring inbound bytes), write (recurring outbound bytes), cancel (asynchronous operation interrupt), close (lifecycle teardown). A user implementing a custom transport (e.g., over an in-process IPC channel) needs all five; collapsing cancel into close removes the "interrupt without teardown" semantic the reconnect flow consumes. Reviewed at Gate A on this document.

**Concept-vs-virtual:** chosen virtual because (a) `Transport` is held by `std::unique_ptr<Transport>` in `Session::transport_` (per the `[arch §6]` plugin pattern) — binding at the C++ ABI is irreducible without a virtual surface; (b) the awaitable return types on each method are hard to express as a concept without leaking the implementation's promise types; (c) the C ABI (delegated to 2i) needs an opaque-handle shape (`fixpp_transport_t`) that requires a virtual surface to forward.

**Annotations at the declaration site (mirrors `[2g §4.1]` precedent):**

- Every `expected_t<T>`-returning method carries `[[nodiscard]]`.
- Every parameter carrying a non-owning view (`std::span<std::byte>` on `async_read_some`, `std::span<const std::byte>` on `async_write`) carries `[[clang::lifetimebound]]` at the **abstract-base declaration site**, exactly as the `[2b §6.4]` / `[2g §4.1]` precedent. Calls through `Transport&` / `unique_ptr<Transport>` see the annotation.
- The `Endpoint const&` parameter on `async_connect` is value-typed for the inner data but the reference itself carries `[[clang::lifetimebound]]` implicitly under Clang's reference-binding lifetime model — explicit annotation is unnecessary.

**`ConnectInfo` is OWNING by value** (mirrors the `[2g §4.5]` `peer_identity` precedent). The session FSM (Phase-4 spec) captures `ConnectInfo` by value across reconnect cycles; no view fields, no cross-doc lifetime contract. Only POD-shaped fields cross the boundary.

**In-flight exclusivity (NORMATIVE — API-level contract per RC#3 / Codex P1 #2 close).** At most one in-flight `async_read_some` and at most one in-flight `async_write` per `Transport` instance. Concurrent calls — a coroutine that issues `async_write` while a previous `async_write`'s awaitable has not yet completed (suspended or in-flight on the strand), or a coroutine that issues `async_read_some` while a previous `async_read_some` is in-flight — complete **immediately** with `expected_t::unexpected{transport_write_in_progress}` (for write overlap) or `expected_t::unexpected{transport_read_in_progress}` (for read overlap) per §6.6. Strand serialisation is defence-in-depth (it serialises completion handlers, not initiation — a strand-bound coroutine A can `co_await transport.async_write(...)` and suspend; a strand-bound coroutine B on the same strand can also `co_await transport.async_write(...)` and reach the underlying ASIO `async_write_some` initiation concurrently because the strand only serialises completion-handler dispatch); the **API-level exclusivity contract is the binding rule**. This matches the ASIO regularity contract for `tcp::socket::async_write_some` ("the caller's responsibility to ensure that this stream performs no other write operations until this operation completes"). The §9 seam #15 (in-flight exclusivity) asserts the second call fails deterministically with `transport_*_in_progress` rather than racing into the underlying ASIO socket. `async_connect` and `async_handshake` are one-shot per Transport lifetime (a Transport admits one connect attempt and one handshake; reconnect destroys this Transport and mints a fresh one via `TransportFactory` per §4.9 + Appendix D §D.4; a second connect-or-handshake while one is in flight, or a second handshake after the first succeeded, raises `transport_already_connected` per §6.6).

#### §4.1.1 Cancellation contract — recipe per method

Every async method on `Transport` is implemented natively over ASIO async ops (`async_connect`, `async_read_some`, `async_write` map directly to `asio::ip::tcp::socket`'s and `asio::ssl::stream`'s composed operations); cancellation flows through ASIO's native slot mechanism without any project-internal `cancellable_dispatch` hop on the standard path. The slot is read with `co_await asio::this_coro::cancellation_state`; the underlying ASIO operation honours `cancellation_type::total` per `[const §XI.2]` and produces `asio::error::operation_aborted`, which 2h maps to the appropriate `transport_*_cancelled` variant per §6.6.

For non-standard handoffs (e.g., DNS resolution on a worker thread for an HFT user that injects their own resolver, or the OpenSSL signing path through `[2g §4.1]`'s `async_signer_ref`), the implementer follows `[2d §6.5]`'s `cancellable_dispatch` recipe verbatim — see §6.5 for the body shape.

### §4.2 `fixpp::transport::TlsTransport` — TLS-aware sub-interface (1 additional pure-virtual; 1 of 5 sub-interface budget)

```cpp
// include/fixpp/transport/tls_transport.hpp
#include <fixpp/transport/transport.hpp>
#include <fixpp/tls/security_profile.hpp>   // [2g §4.5] SslCtxConfig + peer_identity
#include <fixpp/tls/pinset.hpp>             // [2g §4.3] Pinset (for snapshot capture)
#include <memory>
#include <memory_resource>
#include <string>

namespace fixpp::transport {

// handshake_result — value-typed POD returned by async_handshake. Carries the
// negotiated TLS artefacts as OWNING value-typed members so the FSM holds
// them by value across the session lifetime (the [2c §4.8] owning_message_t<>
// cross-strand-boundary precedent; the [2g §4.5] peer_identity owning-by-
// value precedent). The post-RC#2 close: the v0.1 trio of pure-virtual
// accessors (peer_identity_view / captured_pinset_snapshot /
// negotiated_cipher_suite) collapses into this value-typed return so the
// TlsTransport sub-interface drops to 1 pure-virtual (async_handshake), the
// dynamic_cast<TlsTransport*> count drops to exactly 1 site (handshake
// issue), and post-handshake reads (T-041 binding, 2k OTel spans, 2j reload
// identity readout) are direct value reads with no cast.
struct handshake_result {
    fixpp::tls::peer_identity                            peer_id;          // OWNING per [2g §4.5]; PMR-allocated SAN strings.
    std::shared_ptr<const fixpp::tls::pin_snapshot>      captured_pinset;  // null IFF SecurityProfile::mtls_ca / one_way_ca per [2g §4.5.1] — see "Accessor lifetime + nullability contracts" note below.
    std::pmr::string                                     negotiated_cipher; // PMR-allocated against SslCtxConfig::mr; e.g. "TLS_AES_128_GCM_SHA256".
};

// Accessor lifetime + nullability contracts (round-2 Codex P2-2 / Opus N-P2
// close — RC#1 declaration-site lifetime-bound lesson from 2g):
//
// (a) Lifetime-bound view accessors. handshake_result is a value-typed POD —
//     it publishes NO accessors of its own (RC#2 design: post-handshake reads
//     are direct member access on the FSM-held value, no virtual dispatch).
//     The view-returning accessors that ARE consumed are reached through
//     handshake_result.peer_id (e.g. result.peer_id.subject_dn_view(),
//     san_dns_names(), san_uris()): those carry [[clang::lifetimebound]] at
//     their abstract-base declaration site per [2g §4.5] (verified at
//     2g-tls.md §4.5 — "view accessors carry [[clang::lifetimebound]] bound to
//     *this") so consumer-side aliasing is bounded by the peer_identity sub-
//     object's lifetime (= handshake_result's lifetime by composition). The
//     handshake_result.negotiated_cipher member is owning std::pmr::string;
//     consumers materialising a non-owning view do so via
//     std::string_view{result.negotiated_cipher} and that view is bounded by
//     *result by ordinary C++ lifetime rules (no extra annotation needed at
//     the POD member). No additional accessors are published on
//     handshake_result itself by RC#2 design.
//
// (b) captured_pinset nullability contract under non-pinned profiles.
//     handshake_result.captured_pinset is null IFF the negotiated
//     SecurityProfile is mtls_ca or one_way_ca [[deprecated]] — these
//     profiles do NOT consume a pinset per [2g §4.5.1] normative table, so
//     there is no snapshot to capture. Under SecurityProfile::mtls_pinned the
//     shared_ptr is non-null and points at the captured-once snapshot per
//     [2g §6.5.1]. peer_id is always populated (peer cert subject + SAN are
//     extracted regardless of profile). Consumers — 2k OTel cert-event spans
//     per [2g §7.8], 2j ReloadCertSource handler-side identity readout per
//     [2g §7.7], the §9 seam #7 test code — MUST guard with
//     `if (result.captured_pinset)` before dereference. The nullability is
//     intentional (not a footgun): RC#2 collapsed the v0.1 "value present
//     but might fail to read" accessor surface into a value-typed POD,
//     and the one remaining optional-shaped member (captured_pinset under
//     non-pinned profiles) is documented at the type-definition site here
//     rather than via std::optional<std::shared_ptr<...>> wrapping (which is
//     anti-idiomatic since shared_ptr already has a null state).

// TLS-aware sub-interface. Inherits virtually from Transport so that a
// TlsTransport* IS-A Transport* (the session FSM holds Transport* uniformly;
// the TLS specialisation is reached through dynamic_cast at exactly ONE site
// — async_handshake issue at session open).
//
// Sub-interface ≤5 cap budget per [const §XIV.2]: this sub-interface uses
// 1 of 5 (async_handshake). The remaining 4 slots are headroom for a future
// PSK session callback (T-012 P2 hook), an explicit renegotiation control
// point (post-v1), or an early-data hook (post-v1; banned in v1.0 per
// [const §XII.3] 0-RTT).
class TlsTransport : public virtual Transport {
public:
    ~TlsTransport() override = default;

    // (1) Run the TLS handshake. Issued by the session FSM AFTER async_connect
    //     completes successfully (the FSM owns the order: connect → handshake →
    //     Logon). Returns a value-typed handshake_result containing the
    //     negotiated peer_identity (OWNING per [2g §4.5]), the captured
    //     Pinset::snapshot() shared_ptr (per [2g §6.5.1] capture-once
    //     contract), and the negotiated cipher-suite name. The FSM holds
    //     handshake_result by value across the session lifetime; downstream
    //     consumers (T-041 binding at session open, 2k OTel cert-event spans
    //     per [2g §7.8], 2j ReloadCertSource handler) read from the FSM-held
    //     value with no virtual dispatch and no dynamic_cast.
    //
    //     The SslCtxConfig comes from [2g §4.5] make_ssl_ctx_config(profile,
    //     cert_source, clock, pinset, mr) — the FSM (Phase-4 spec) builds it
    //     at session open and passes it in. 2h does NOT construct the
    //     SslCtxConfig; 2h consumes it.
    //
    //     [2g §6.5.1] HANDSHAKE-TIME PINSET CAPTURE BINDING (binding):
    //     async_handshake captures cfg.pinset->snapshot() ONCE at handshake
    //     start (BEFORE the OpenSSL handshake protocol exchange begins) and
    //     stores the captured snapshot in transport-owned state for the
    //     duration of the handshake. The SSL_VERIFY_PEER callback (set by 2h
    //     on the SSL_CTX before this method is called) reads the captured
    //     snapshot from that transport-owned state — never calls
    //     Pinset::find(...) repeatedly, never re-acquires the snapshot per
    //     peer-cert lookup. (`SslCtxConfig` itself does NOT carry a
    //     `pinset_snapshot` field per [2g §4.5]; the snapshot is captured
    //     inside the transport at handshake start and is carried in the
    //     returned handshake_result.) Mid-handshake rotation per [2d §7.5]
    //     does NOT affect an in-flight handshake by construction.
    //
    //     Cancellation: cancellation_type::total causes the awaitable to
    //     complete with transport_handshake_cancelled (the underlying OpenSSL
    //     handshake aborts mid-flight; the SSL* state is left in a broken
    //     state, and the caller MUST close() to clean up — see §6.4).
    //     The per-mode cancellation effect table for graceful (phase 1) /
    //     graceful (phase 2) / terminal modes is at §6.4.1.
    //
    //     Timeout: Transport::Config::tls_handshake_timeout (default 30 s)
    //     bounds the handshake's wall-clock duration; on timeout the
    //     awaitable completes with transport_handshake_timeout. The timeout
    //     is implemented via Clock::sleep_until composed under the awaiter's
    //     cancellation slot per [2d §6.5].
    //
    //     In-flight exclusivity: handshake is one-shot per Transport lifetime
    //     (like async_connect — see §4.1 in-flight exclusivity contract); a
    //     second `async_handshake` while one is in flight, or after a previous
    //     one has succeeded, raises `transport_already_connected` per §6.6
    //     (the variant covers connect-and-handshake one-shot overlap; the
    //     FSM's reconnect path issues `close()` then re-mints a fresh
    //     Transport via `TransportFactory::make(...)` to handshake again).
    [[nodiscard]] virtual asio::awaitable<core::expected_t<handshake_result>>
        async_handshake(fixpp::tls::SslCtxConfig const& cfg
                            [[clang::lifetimebound]]) = 0;
};

}  // namespace fixpp::transport
```

**Sub-interface pure-virtual count: 1** (`async_handshake`). The v0.1 three accessor pure-virtuals (`peer_identity_view`, `captured_pinset_snapshot`, `negotiated_cipher_suite`) are dropped from the published surface and replaced by the value-typed `handshake_result` return. The `TlsTransport` sub-interface uses **1 of 5** slots, leaving 4 slots of headroom — the headroom story §1 Goal 2 carries is honoured.

**Why a sub-interface and not a flag on `Transport`.** A non-TLS impl (plain TCP, in-process IPC, post-v1 SHM) does not need `async_handshake`; folding it into the base would force every impl to no-op it and would erode the `[const §XIV.2]` ≤5 cap on the base. The sub-interface keeps the base clean (5 of 5) and lets TLS-aware operations live where they semantically belong.

**Single-site `dynamic_cast`.** v1.0 ships only TLS-capable Transports (per `[FIX-SL §4.3.1]` + `[FIXS §1.1]` mandatory-TLS deployment reality and §4.5's drop of the v0.1 plain-TCP-via-empty-`SslCtxConfig` narrative); a v1.0 `Transport*` held in `Session::transport_` is reached as `TlsTransport*` via **exactly one** `dynamic_cast<TlsTransport*>(transport_.get())` at session open (`async_handshake` issue). The cast result is stored once on the session object as a typed pointer; no further casts happen on any code path. Post-handshake artefact reads (T-041 binding, 2k OTel cert-event spans per `[2g §7.8]`, 2j `ReloadCertSource` handler-side identity readout per `[2g §7.7]`) read from the FSM-held `handshake_result` value with no cast and no virtual dispatch. RTTI is therefore opportunistic, not structural — a future `-fno-rtti` deployment can fall back to a non-virtual `as_tls() noexcept -> TlsTransport*` member on `Transport` returning `nullptr` for non-TLS impls (post-v1; v1.0 ships RTTI-on per the standard Linux/Clang and Windows/MSVC defaults). On a hypothetical post-v1 non-TLS Transport, the cast returns null and the FSM rejects the session config (`error::invalid_session_config` per `[2d §6.7]`).

### §4.3 `fixpp::transport::Endpoint` — value type

```cpp
// include/fixpp/transport/endpoint.hpp
#include <cstdint>
#include <string>
#include <string_view>

namespace fixpp::transport {

// Address-family-agnostic endpoint representation. Holds a host (IP literal,
// hostname, or IPv6 zone-id-bearing string) + a port. Resolved at async_connect
// time via asio::ip::resolver; the resolver result determines IPv4 vs IPv6.
//
// Owning by value (the host string is std::string, owned by the system default
// allocator). v1.0 does NOT publish a PMR-aware overload — the v0.1 "PMR-aware
// overload is available" claim is retired (the struct does not carry a PMR
// allocator field; HFT users that want arena-allocated host storage layer that
// on top via a `pmr_endpoint` type — out of v1.0 scope per §1.2 / §10).
struct Endpoint {
    std::string   host;        // hostname OR IP literal OR "host%zone" for IPv6 link-local.
    std::uint16_t port = 0;
    // For acceptor-side construction:
    std::uint32_t backlog = 128;  // listen queue depth; OS may cap silently. Listener-only;
                                  // used at listen() time, not at connect() time.

    Endpoint() = default;
    Endpoint(std::string h, std::uint16_t p) : host(std::move(h)), port(p) {}
    Endpoint(std::string h, std::uint16_t p, std::uint32_t b) : host(std::move(h)), port(p), backlog(b) {}

    // Initiator vs acceptor distinction is at the type-system level: an
    // initiator passes Endpoint to Transport::async_connect; an acceptor passes
    // Endpoint to asio_listener::Config::bind_endpoint. The v0.1
    // is_initiator_shape() heuristic ("port != 0 && backlog == 128") was a
    // false-positive footgun (an acceptor explicitly setting backlog = 128 got
    // misclassified as initiator) and is dropped; no replacement accessor is
    // needed because the type-system distinguishes the two construction paths
    // already.
};

}  // namespace fixpp::transport
```

Notes:

- **Owning by value.** No view fields; no PMR allocator on the struct itself (the `host` string uses the system default allocator). HFT users that want an arena-allocated host string layer that on top via a `pmr_endpoint` type — out of v1.0 scope. The v0.1 inline-comment claim that a "PMR-aware overload is available" is retired in v0.2 (per Codex P3 #7); the struct shape is the v1.0 surface.
- **Family detection.** ASIO `resolver` resolves the host string at `async_connect` time; IPv4-mapped-IPv6 `::ffff:0.0.0.0/96` is honoured per OS resolver behaviour. The `family` field on `ConnectInfo` reports the negotiated family.
- **Path / Unix-domain endpoint** is out of v1.0 scope; `[FIX-SL §4.3.1]` mandates TCP, and v1.0 honours that strictly. A future Unix-domain or in-process IPC `Transport` impl would carry its own `Endpoint`-equivalent type.

### §4.4 `fixpp::transport::ReconnectPolicy` — value type

```cpp
// include/fixpp/transport/reconnect_policy.hpp
#include <chrono>
#include <cstdint>
#include <memory_resource>
#include <vector>

namespace fixpp::transport {

// Reconnection back-off envelope for initiator-side sessions per
// [FIX-SL §4.3.1]. Consumed by the session-module Phase-4 spec FSM that
// drives reconnect; 2h owns the value type, the FSM owns the schedule
// computation. The FSM consults Clock::steady_now() (per [2d §7.9]) to
// schedule retries.
//
// Shape per Clarifications 2026-05-27 Q2=C / Appendix D §D.5 (QuickFIX/J-
// aligned schedule-array surface): the caller supplies an explicit per-
// attempt delay array; the FSM indexes it by attempt number with plateau-
// at-last semantics (matches QuickFIX/J IoSessionInitiator::
// computeNextRetryConnectDelay():318-319). Drops the v0.3 5-field
// {initial_delay, max_delay, multiplier, max_attempts, jitter} shape.
//
// The numeric ceiling on max_attempts is normative (§4.4.1 ReconnectPolicy::
// defaults() returns the binding default-set); operators that need a longer
// envelope override max_attempts explicitly per [const §XII.5] no-implicit-
// default pattern. New defaults_quickfix_compat() returns the industry-
// canonical {[30s], 0.0, 0} for operators that opt out of the constitutional
// thunder-herd-pattern envelope.
struct ReconnectPolicy {
    // Per-attempt delay schedule. The FSM indexes by attempt number; once n
    // >= schedule.size() the FSM plateaus at schedule.back() (matches
    // QuickFIX/J plateau-at-last). MUST be non-empty.
    std::pmr::vector<std::chrono::milliseconds> schedule;
    double                                      jitter        {0.10}; // ±10% randomisation; anti-thundering-herd.
    std::uint32_t                               max_attempts  {10};   // numeric ceiling per §4.4.1; 0 = unbounded (opt-in only).

    // The schedule-formula helper (called by the FSM):
    //   actual = schedule[min(n, schedule.size() - 1)] * (1 + uniform(-jitter, +jitter))
    // Per Appendix D §D.6 the signature is one-parameter (attempt_n); the
    // policy owns the deterministic seed source (a session_id_seed field is
    // wired at /implement-time within 2h's existing surface, either as a
    // policy field set by the FSM at session open or as an overload taking
    // the seed as a second parameter). The determinism contract is
    // preserved (deterministic-per-attempt seed reproducible from session id
    // + attempt number alone per [const §VII.7] fuzz determinism).
    [[nodiscard]] std::chrono::milliseconds
        delay_for_attempt(std::uint32_t attempt_n) const noexcept;

    // Normative defaults factory per §4.4.1. Materialises the v0.2
    // exponential schedule explicitly: [100ms, 200ms, 400ms, 800ms, 1.6s,
    // 3.2s, 6.4s, 12.8s, 25.6s, 30s] with jitter = 0.10, max_attempts = 10.
    [[nodiscard]] static ReconnectPolicy defaults();

    // Industry-canonical defaults per Clarifications 2026-05-27 Q2=C:
    // {[30s], 0.0 jitter, 0 (unbounded) max_attempts} — matches
    // QuickFIX-cpp m_reconnectInterval=30 / Fix8 _login_retry_interval
    // single-fixed shape. Operators that need a QuickFIX-compatible
    // reconnect cadence pick this factory at session open.
    [[nodiscard]] static ReconnectPolicy defaults_quickfix_compat();
};

}  // namespace fixpp::transport
```

Notes:

- **Disabled by default at the transport surface.** The `Transport` interface itself does NOT carry a `ReconnectPolicy` field — reconnect is an FSM-level concern, not a wire-level concern. The `ReconnectPolicy` is held by `SessionConfig` (or, for HFT users that want per-Endpoint policy, attached to the `Endpoint` indirectly via a per-`Endpoint` config map owned by the FSM). 2h ships the value type; the session-module Phase-4 spec wires it.
- **Jitter source determinism.** Per Appendix D §D.6 the RNG moves from FSM-side (caller-supplied `rand01`) to policy-side (the policy hashes a deterministic-per-attempt seed derived from session id + attempt number per FR-021). The same seed produces the same retry schedule for replay testing; `[const §VII.7]` fuzz determinism extends to reconnect schedule (a fuzz that triggers reconnect must be replayable).

#### §4.4.1 `ReconnectPolicy::defaults()` — normative numeric ceilings (RC#3 / Opus N-P1-3 close)

**Binding contract (NORMATIVE).** Per Clarifications 2026-05-27 Q2=C / Appendix D §D.5 the defaults are materialised as an explicit schedule:

```cpp
// Returns ReconnectPolicy with the v0.2 exponential envelope baked out as
// an explicit per-attempt schedule. The cumulative wall-clock envelope at
// max_attempts = 10 is preserved at ≈ 1 min 21 s (73-89 s with ±10% jitter).
ReconnectPolicy ReconnectPolicy::defaults() {
    using namespace std::chrono_literals;
    ReconnectPolicy p;
    p.schedule = std::pmr::vector<std::chrono::milliseconds>{
        100ms, 200ms, 400ms, 800ms, 1600ms, 3200ms, 6400ms, 12800ms, 25600ms, 30000ms
    };
    p.jitter       = 0.10;
    p.max_attempts = 10;  // numeric ceiling per [const §XV] thunder-herd-pattern intent.
    return p;
}

// Industry-canonical (QuickFIX-cpp / Fix8 single-fixed) opt-out per
// Clarifications 2026-05-27 Q2=C: {[30s], 0.0 jitter, 0 (unbounded) max_attempts}.
ReconnectPolicy ReconnectPolicy::defaults_quickfix_compat() {
    using namespace std::chrono_literals;
    ReconnectPolicy p;
    p.schedule     = std::pmr::vector<std::chrono::milliseconds>{30000ms};
    p.jitter       = 0.0;
    p.max_attempts = 0;
    return p;
}
```

**Rationale.** v0.1's default `max_attempts = 0` (unbounded) shipped an engine that could DDOS a venue under common operational scenarios — 10⁵ peers reconnecting after a venue outage cluster reconnect attempts in narrow windows once the schedule plateaus at its 30 s tail, a known TCP/TLS thundering-herd amplifier observed in production at exchange-gateway recoveries. The v1.0 default picks a numeric ceiling per the `[2g §1.1]` DoS-cap precedent (every cap is numeric, not "unbounded by default"). With `max_attempts = 10`, total wall-clock to terminal stop spans ≈ 1 min 21 s under the materialised schedule (cumulative; the geometric envelope's tail caps at 30 s from attempt 9 onward — 100 ms → 200 ms → 400 ms → 800 ms → 1.6 s → 3.2 s → 6.4 s → 12.8 s → 25.6 s → 30 s = 81 100 ms; ±10% jitter widens to ≈ 73–89 s). The v0.2 rationale carried "≈ 4 min 25 s" as the published envelope; that figure was arithmetically wrong against this same exponential schedule (it conflated the per-attempt delay at attempt 10 with the cumulative sum) and is corrected here per round-2 Opus N-P1. The corrected ≈ 1 min 21 s envelope is intentionally tight against the thunder-herd hazard — shorter than typical exchange-gateway recovery windows (30 s – 5 min). Operators that need a longer envelope override `max_attempts` explicitly (e.g., 12 attempts gives ≈ 2 min 21 s) OR raise the schedule tail (e.g., replace the final `30000ms` with `90000ms` gives ≈ 3 min 1 s at `max_attempts = 10`); the rebaseline is a numbers question, not a contract question. v1.0 ships the safe default and preserves the engine's right to surrender to a session-FSM-driven escalation.

**Operator opt-in to unbounded.** Operators that need a longer envelope override `max_attempts` explicitly (e.g., `auto policy = ReconnectPolicy::defaults(); policy.max_attempts = 0;` for unbounded). This matches the `[const §XII.5]` no-implicit-default pattern for `SecurityProfile` — picking unbounded is an explicit operator decision, not the engine default. Per-venue tuning (FX retail, equity, equity-options, derivatives) is the Phase-4 session-module spec's call (or a post-v1 venue-presets library); 2h ships the safe default.

**On exceeding `max_attempts`.** When the FSM's reconnect loop exhausts the cap, the FSM surfaces `transport_reconnect_limit_exceeded` (§6.6) and the session terminates. The v0.1 §10 Q3 deferral ("FSM applies wall-clock cap separately") is closed: §10 Q3 disposition flips to **DECIDED — `max_attempts = 10` default per §4.4.1**.

### §4.5 `fixpp::transport::asio_tls_transport` — default impl (TCP + TLS over OpenSSL per `[const §XII.1]`)

```cpp
// include/fixpp/transport/asio_tls_transport.hpp
#include <asio/awaitable.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/ssl/stream.hpp>
#include <chrono>
#include <memory>
#include <memory_resource>
#include <fixpp/transport/transport.hpp>
#include <fixpp/transport/tls_transport.hpp>
#include <fixpp/transport/endpoint.hpp>

namespace fixpp::transport {

class asio_tls_transport final : public TlsTransport {
public:
    struct Config {
        // ── Connect-time ────────────────────────────────────────────────────
        std::chrono::milliseconds connect_timeout       {30'000};   // §1.1 cap.
        std::chrono::milliseconds tls_handshake_timeout {30'000};   // §1.1 cap.
        std::chrono::milliseconds tls_close_timeout     {1'000};    // §4.1's close() bidi shutdown cap.

        // ── Read/Write windows ──────────────────────────────────────────────
        std::size_t               max_read_window_bytes  {256 * 1024};   // §1.1 cap; matches [2b §1.2].
        std::size_t               max_write_size_bytes   {1024 * 1024};  // §1.1 cap; 1 MiB.

        // ── TCP knobs (default = OS auto-tune; opt-in to override) ──────────
        std::int32_t              tcp_recv_buf_bytes     {0};   // 0 = OS auto-tune.
        std::int32_t              tcp_send_buf_bytes     {0};   // 0 = OS auto-tune.
        bool                      tcp_nodelay            {true};  // FIX is latency-sensitive; Nagle off by default.
        bool                      tcp_keepalive          {false}; // Heartbeat is the primary keep-alive.
        std::int32_t              tcp_keepalive_idle_seconds     {120};
        std::int32_t              tcp_keepalive_interval_seconds {30};
        std::int32_t              tcp_keepalive_count            {3};
        bool                      so_reuseaddr           {false}; // acceptor-side opt-in.
        bool                      so_linger              {false}; // off by default; close() honours TLS bidi shutdown instead.
        std::int32_t              so_linger_seconds      {0};

        // ── PMR (per [arch §6] rule 4) ──────────────────────────────────────
        std::pmr::memory_resource* mr {nullptr};   // null → engine default; passed to make() factory parameter (§4.7).
    };

    // [arch §6] rule-4 factory entry point. Returns expected_t<...> for non-
    // construction-time callers (2i C ABI, future hot-reload). The factory
    // accepts the ASIO executor (the session_executor wrapper per [2d §4.8])
    // + the TLS SslCtxConfig (built by [2g §4.5] make_ssl_ctx_config) + the
    // PMR resource as an explicit parameter. (DNS resolution is owned inside
    // asio_tls_transport via asio::ip::resolver constructed from `exec`; a
    // custom-resolver-factory parameter is post-v1 — the v0.1 trailing
    // comment that mentioned a "resolver factory" parameter is retired in
    // v0.2 per Codex P3 #8.)
    //
    // v1.0 partition: every Transport returned by this factory is TLS-
    // capable. Plain-TCP-no-TLS is post-v1; the v0.1 "empty SslCtxConfig
    // produces a no-op handshake" narrative is retired in v0.2 (per Codex
    // P2 #5 / Opus N-P1-2). This converges with the FIXS-only v1.0
    // deployment reality per [FIXS §1.1] / [FIX-SL §4.3.1] and removes the
    // TlsTransport-vs-Transport partition ambiguity at the type-system
    // level. Downstream session-FSM code reaches `TlsTransport*` via a
    // single `dynamic_cast<TlsTransport*>(transport_.get())` at session
    // open; the cast result is stored once on the Session object as a typed
    // pointer (no further casts on any code path).
    [[nodiscard]] static core::expected_t<std::unique_ptr<Transport>>
        make_asio_tls_transport(asio::any_io_executor          exec,
                                Config                         cfg,
                                fixpp::tls::SslCtxConfig       ssl_cfg,
                                std::pmr::memory_resource*     mr) noexcept;

    // Direct-construction path for in-process C++ callers that prefer
    // throw-on-failure (per [arch §5.3]'s construction-time carve-out).
    explicit asio_tls_transport(asio::any_io_executor    exec,
                                Config                   cfg,
                                fixpp::tls::SslCtxConfig ssl_cfg);

    ~asio_tls_transport() override;

    // Transport overrides — all the [[clang::lifetimebound]] annotations
    // mirror the abstract-base declaration site (RC-style precedent from
    // [2b §6.4] / [2g §4.1]).
    [[nodiscard]] asio::awaitable<core::expected_t<ConnectInfo>>
        async_connect(Endpoint const& ep) override;

    [[nodiscard]] asio::awaitable<core::expected_t<std::size_t>>
        async_read_some(std::span<std::byte> buf [[clang::lifetimebound]]) override;

    [[nodiscard]] asio::awaitable<core::expected_t<std::size_t>>
        async_write(std::span<const std::byte> bytes [[clang::lifetimebound]]) override;

    [[nodiscard]] core::expected_t<void> cancel() noexcept override;
    [[nodiscard]] core::expected_t<void> close() noexcept override;

    // TlsTransport override — returns a value-typed handshake_result POD per
    // §4.2 (RC#2 close). The returned handshake_result is OWNING (peer_id by
    // value, captured_pinset as shared_ptr, negotiated_cipher as PMR-allocated
    // pmr::string against ssl_cfg.mr); the FSM holds it by value across the
    // session lifetime. The default impl populates the result from internal
    // state captured during the OpenSSL handshake (verify_peer_trampoline
    // populates peer_id_; SSL_CTX_set_verify_callback fires before the
    // handshake exchange and captures the pinset snapshot on first invocation
    // per [2g §6.5.1]).
    [[nodiscard]] asio::awaitable<core::expected_t<handshake_result>>
        async_handshake(fixpp::tls::SslCtxConfig const& cfg
                            [[clang::lifetimebound]]) override;

private:
    // OpenSSL SSL_CTX construction lifetime (constructor): consumes ssl_cfg.profile,
    // ssl_cfg.cs, ssl_cfg.pinset, ssl_cfg.clock, ssl_cfg.ciphers, ssl_cfg.mr per
    // [2g §4.5.1]'s normative table:
    //   - SSL_CTX_set_min_proto_version(SSL_CTX*, TLS1_2_VERSION) per [const §XII.2]
    //   - SSL_CTX_set_ciphersuites + SSL_CTX_set_cipher_list from CipherPolicy
    //     per [2g §4.4] (the joined allow-list strings)
    //   - SSL_CTX_set1_curves_list + SSL_CTX_set1_sigalgs_list from CipherPolicy
    //   - SSL_CTX_set_options(SSL_OP_NO_RENEGOTIATION | SSL_OP_NO_COMPRESSION |
    //     SSL_OP_NO_TICKET | SSL_OP_NO_EARLY_DATA) per [FIXS §3.2] / [const §XII.3]
    //   - SSL_CTX_set_verify(SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT
    //     [acceptor-side mtls_*]) per [2g §4.5.1] table
    //   - SSL_CTX_set_verify_callback(verify_peer_trampoline) — the trampoline
    //     extracts the peer_chain from the X509_STORE_CTX, calls
    //     [2g §4.5] verify_peer(cfg, peer_chain), and on accept stores the
    //     returned peer_identity into *this for inclusion in handshake_result
    //     (§4.2) on async_handshake completion.
    //   - For software_key_ref signers: SSL_CTX_use_certificate + SSL_CTX_use_PrivateKey
    //     bind the local credentials.
    //   - For async_signer_ref signers: an SSL_CTX_set_client_cert_cb-like
    //     custom callback dispatches to the awaitable signer per [2g §6.4]'s
    //     cancellable_dispatch recipe (the signing call suspends OFF the
    //     session strand per [2d §7.5]).
    Config                                cfg_;
    fixpp::tls::SslCtxConfig              ssl_cfg_;
    asio::any_io_executor                 exec_;            // session executor wrapper at construction.
    asio::ip::tcp::socket                 socket_;
    std::unique_ptr<asio::ssl::context>   ssl_ctx_;          // RAII-owned SSL_CTX*.
    std::optional<asio::ssl::stream<asio::ip::tcp::socket&>> ssl_stream_;  // wraps socket_ once handshake starts.
    std::shared_ptr<const fixpp::tls::pin_snapshot> captured_pinset_;     // captured at async_handshake start per [2g §6.5.1].
    fixpp::tls::peer_identity             peer_id_;          // populated on verify_peer accept.
    enum class state_t { fresh, connected, handshaken, closed };
    state_t                               state_ {state_t::fresh};
};

}  // namespace fixpp::transport
```

Notes:

- **Construction-time exceptions allowed.** Per `[arch §5.3]`'s carve-out (mirrors `[2g §4.2]` `file_cert_source` precedent). Once construction returns, every method is `expected_t<...>`. The `make_asio_tls_transport` factory wraps the throwing constructor in `expected_t<...>`.
- **`SSL_CTX` lifetime.** Owned by `asio_tls_transport` via `std::unique_ptr<asio::ssl::context>`. The OpenSSL `SSL_CTX*` underneath is reference-counted by OpenSSL itself; the RAII wrapper guarantees `SSL_CTX_free` at destruction.
- **Pinset snapshot capture (`[2g §6.5.1]` binding contract).** `async_handshake` captures `Pinset::snapshot()` ONCE at handshake start and stores the `shared_ptr<const pin_snapshot>` in `captured_pinset_`. The `verify_peer_trampoline` reads from `captured_pinset_` (via a closure / context pointer wired into the OpenSSL `SSL_CTX_set_verify_callback`); it does NOT call `Pinset::find(...)` or `Pinset::snapshot()` repeatedly.
- **`peer_identity` storage.** Owned by the transport (mirrors `[2g §4.5]`'s OWNING `peer_identity`). The session-module Phase-4 spec captures it BY VALUE for the T-041 binding; the transport's accessor returns a `const&` for the mid-step.
- **PMR.** `cfg.mr` is consumed for the `SSL_CTX` configuration arena (per `[2g §4.5]` `SslCtxConfig::mr`); the OpenSSL allocator path is set via `CRYPTO_set_mem_functions(...)` ONCE at engine init by the engine bootstrap (NOT by 2h on a per-transport basis — `CRYPTO_set_mem_functions` is process-global and must be called before any OpenSSL state is initialised). Documented in §8.

### §4.6 `fixpp::transport::Listener` — multi-session acceptor (T-005)

```cpp
// include/fixpp/transport/listener.hpp
#include <asio/awaitable.hpp>
#include <memory>
#include <fixpp/transport/transport.hpp>

namespace fixpp::transport {

// Multi-session TCP acceptor. Discharges T-005. Pluggable; one default impl
// (asio_listener) ships in v1.0. The Listener pure-virtual surface is small
// (1 method); keeping it pluggable lets HFT users substitute a custom acceptor
// (e.g., epoll-driven or io_uring-driven) without touching Transport.
class Listener {
public:
    virtual ~Listener() = default;

    // (1) Accept the next inbound connection and return a fresh Transport
    //     wrapping the accepted socket. The returned Transport is initially
    //     in the "connected" state (TCP handshake done by the OS); the FSM
    //     issues async_handshake (TLS) immediately.
    //
    //     Cancellation: cancellation_type::total causes the awaitable to
    //     complete with transport_accept_cancelled.
    [[nodiscard]] virtual asio::awaitable<core::expected_t<std::unique_ptr<Transport>>>
        async_accept() = 0;
};

// Default impl wrapping asio::ip::tcp::acceptor.
class asio_listener final : public Listener {
public:
    struct Config {
        Endpoint                   bind_endpoint;        // local bind address + port + backlog.
        bool                       so_reuseaddr {true};  // typical acceptor default.
        bool                       so_reuseport {false}; // Linux-only; off by default.
        // The accepted Transport's Config (passed through to each minted Transport).
        asio_tls_transport::Config accepted_transport_config {};
        // The SslCtxConfig is per-acceptor (one per Listener) — built by the
        // engine bootstrap from the EngineConfig + the SessionConfig that
        // applies to acceptor-side sessions.
        fixpp::tls::SslCtxConfig   ssl_cfg;
    };

    [[nodiscard]] static core::expected_t<std::unique_ptr<Listener>>
        make_asio_listener(asio::any_io_executor       exec,
                           Config                      cfg,
                           std::pmr::memory_resource*  mr) noexcept;

    explicit asio_listener(asio::any_io_executor exec, Config cfg);
    ~asio_listener() override;

    [[nodiscard]] asio::awaitable<core::expected_t<std::unique_ptr<Transport>>>
        async_accept() override;

private:
    Config                      cfg_;
    asio::any_io_executor       exec_;
    asio::ip::tcp::acceptor     acceptor_;
};

}  // namespace fixpp::transport
```

Notes:

- **Acceptor-side `SslCtxConfig`.** One `SslCtxConfig` per `Listener` (the acceptor-side TLS profile is fixed at engine open; per-session-cert-rotation is via `Pinset` mid-session-mutability per `[arch §5.6]`'s carve-out, NOT per-session `SslCtxConfig` swap). A multi-tenant acceptor that needs per-counterparty profiles uses one `Listener` per counterparty.
- **Inheritance.** `Listener` is its own pluggable interface (1 pure-virtual; well under the cap). Not folded into `Transport` because the lifecycle is different — a `Listener` outlives many `Transport` instances.

### §4.7 `fixpp::transport::TransportFactory` — factory shape

```cpp
// include/fixpp/transport/transport_factory.hpp
#include <asio/any_io_executor.hpp>
#include <memory>
#include <memory_resource>
#include <fixpp/core/expected.hpp>
#include <fixpp/tls/security_profile.hpp>
#include <fixpp/transport/endpoint.hpp>
#include <fixpp/transport/transport.hpp>

namespace fixpp::transport {

// Frozen-at-open factory consumed by EngineConfig::default_transport_factory
// + SessionConfig::transport_factory_override.
//
// Ownership shape (mirrors [2e §4.4] precedent for MessageStoreFactory):
//   - Factory is held by EngineConfig as std::unique_ptr<TransportFactory>
//     and by SessionConfig as std::unique_ptr<TransportFactory> override.
//   - make() returns std::unique_ptr<Transport> (ownership transferred to
//     the Session per [arch §5.6] frozen-at-open).
//
// FR-026 caching contract (per Clarifications 2026-05-27 / Appendix D §D.4):
// the factory is invoked at session open AND at every reconnect attempt
// per FR-028 (fresh-mint-per-attempt). Long-lived state shared across
// attempts MUST be cached at the factory level — never re-built per
// attempt. This includes:
//   - the SslCtxConfig carrying the OpenSSL SSL_CTX* (built ONCE at
//     session open via [2g §4.5] make_ssl_ctx_config from EngineConfig::
//     default_cert_source + per-session overrides);
//   - the engine's PMR root resource;
//   - the engine clock reference.
// The factory itself is long-lived (session lifetime); the Transport
// instances it produces are short-lived (per-attempt). make() MUST be
// cheap enough that the per-attempt mint cost is bounded by the back-off
// envelope, not by make() runtime.
//
// [2d §4.4] currently declares default_transport_factory as
// std::shared_ptr<TransportFactory> — flagged as a sibling-doc inconsistency
// (mirrors the [2e §3.11] / Appendix D §D.1 cross-doc amendment for
// MessageStoreFactory). 2h's Appendix D §D.1 is the corresponding amendment.
class TransportFactory {
public:
    virtual ~TransportFactory() = default;

    // Mint a fresh Transport for a session. The factory captures any per-
    // engine configuration at construction (e.g., the SslCtxConfig built from
    // EngineConfig::default_cert_source); per-session overrides
    // (SessionConfig::cert_source override, SessionConfig::pinset for
    // mid-session-mutable Pinset access) are passed through the SslCtxConfig
    // that the SESSION-OPEN sequencer builds via [2g §4.5] make_ssl_ctx_config.
    //
    // Errors: transport_factory_failed (impl reports inability to construct,
    // e.g., OS resource exhaustion, OpenSSL SSL_CTX_new failure).
    //
    // Lifetime: the returned unique_ptr is consumed by the Session; the
    // Transport instance is engine-anchored at Session::open and destructed
    // at Session::close (after async_close completes or the close timeout
    // fires per §6.4).
    //
    // noexcept (Codex P2 #6 close): the pure-virtual carries `noexcept` to
    // prevent third-party impls from throwing across the virtual boundary;
    // implementations MUST trap any internal PMR or system throws via the
    // [2a §4.2] trap_throw pattern and surface failure as
    // `expected_t::unexpected{transport_factory_failed}`. Matches the
    // [2e §4.4] MessageStoreFactory and [2g §4.2] make_file_cert_source
    // factory `noexcept` precedents.
    [[nodiscard]] virtual core::expected_t<std::unique_ptr<Transport>>
        make(asio::any_io_executor             exec,
             fixpp::tls::SslCtxConfig          ssl_cfg,
             std::pmr::memory_resource*        mr) noexcept = 0;
};

// Default factory wrapping asio_tls_transport.
class asio_tls_transport_factory final : public TransportFactory {
public:
    explicit asio_tls_transport_factory(asio_tls_transport::Config c = {}) noexcept;

    [[nodiscard]] core::expected_t<std::unique_ptr<Transport>>
        make(asio::any_io_executor             exec,
             fixpp::tls::SslCtxConfig          ssl_cfg,
             std::pmr::memory_resource*        mr) noexcept override;

private:
    asio_tls_transport::Config cfg_;
};

}  // namespace fixpp::transport
```

#### §4.7.1 `SessionConfig::transport_factory_override` field shape (engine-anchor + session-`*_override`)

```cpp
// Drop-in addition to [2d §4.5] SessionConfig (Appendix D §D.2 — applied at 2h sign-off):
//
//   // ── Transport (locked by 2h) ─────────────────────────────────────────
//   // Resolved factory = transport_factory_override.value_or(
//   //                       EngineConfig::default_transport_factory).
//   // Factory is unique_ptr per [arch §5.6] (no mid-session swap, no shared
//   // factory across sessions) — mirrors [2e §4.4] MessageStoreFactory shape.
//   std::unique_ptr<fixpp::transport::TransportFactory> transport_factory_override;
```

The §11 hand-off declares this as **Appendix D §D.2** for orchestrator application at sign-off, mirroring the `[2e App D §D.1]` / `[2g App D §D.2]` precedent. The `[2d §4.4]` `default_transport_factory` field type also flips from `shared_ptr` to `unique_ptr` (Appendix D §D.1).

### §4.8 `fixpp::transport::mock_transport` — public-test-header impl

```cpp
// include/fixpp/transport/test/mock_transport.hpp  (public test header — under
// include/fixpp/transport/test/, distinguished from production headers per
// the [2c §4.8] test-header convention).
//
// Used by tests/ ONLY; not linked into the production engine binary.

#include <asio/awaitable.hpp>
#include <deque>
#include <vector>
#include <fixpp/transport/transport.hpp>
#include <fixpp/transport/tls_transport.hpp>

namespace fixpp::transport::test {

// In-memory deterministic Transport for FSM-under-test scenarios. Drives the
// session FSM through a pre-recorded byte stream + handshake-success/failure
// scripted outcomes. Implements TlsTransport (so the FSM's TLS-aware paths
// fire) but the handshake is purely virtual — no real OpenSSL calls.
//
// Cancellation: every async method honours the awaiter's cancellation_state
// per [const §XI.2] — the mock does NOT short-circuit cancellation tests by
// completing instantly. Cancellation-aware completions surface
// transport_*_cancelled exactly as the production impl does.
class mock_transport final : public TlsTransport {
public:
    struct Script {
        std::vector<std::byte>      inbound_bytes;        // queued bytes for async_read_some.
        std::vector<std::vector<std::byte>> expected_outbound_writes;  // verified at async_write.
        bool                        handshake_succeeds {true};
        fixpp::tls::peer_identity   peer_identity_to_return;  // populated when handshake_succeeds.
        std::shared_ptr<const fixpp::tls::pin_snapshot> pinset_snapshot_to_return;
        std::string                 negotiated_cipher = "TLS_AES_128_GCM_SHA256";
        // Latency injection — tests for cancellation race conditions.
        std::chrono::milliseconds   read_latency  {0};
        std::chrono::milliseconds   write_latency {0};
        std::chrono::milliseconds   handshake_latency {0};
    };

    explicit mock_transport(asio::any_io_executor exec, Script script);
    ~mock_transport() override = default;

    // Transport + TlsTransport overrides (deterministic; bytes-for-bytes).
    [[nodiscard]] asio::awaitable<core::expected_t<ConnectInfo>>
        async_connect(Endpoint const& ep) override;
    [[nodiscard]] asio::awaitable<core::expected_t<std::size_t>>
        async_read_some(std::span<std::byte> buf [[clang::lifetimebound]]) override;
    [[nodiscard]] asio::awaitable<core::expected_t<std::size_t>>
        async_write(std::span<const std::byte> bytes [[clang::lifetimebound]]) override;
    [[nodiscard]] core::expected_t<void> cancel() noexcept override;
    [[nodiscard]] core::expected_t<void> close() noexcept override;
    [[nodiscard]] asio::awaitable<core::expected_t<handshake_result>>
        async_handshake(fixpp::tls::SslCtxConfig const& cfg [[clang::lifetimebound]]) override;

    // Test-only diagnostics
    [[nodiscard]] std::size_t bytes_read_so_far() const noexcept;
    [[nodiscard]] std::vector<std::byte> outbound_bytes_seen() const;
    [[nodiscard]] std::size_t async_writes_observed() const noexcept;

private:
    asio::any_io_executor exec_;
    Script                script_;
    std::size_t           read_cursor_ {0};
    std::vector<std::byte> outbound_seen_;
    bool                  closed_      {false};
    bool                  handshaken_  {false};
};

}  // namespace fixpp::transport::test
```

Notes:

- **Header lives under `include/fixpp/transport/test/`.** Distinguished from production headers; only test translation units include it. The build system excludes the directory from production target sources.
- **Handshake is faked.** No OpenSSL is linked into the test binary's mock path; the mock returns the pre-recorded `peer_identity` and `pinset_snapshot` without any cert validation. Tests for `verify_peer` use a different test seam (a direct `[2g §4.5]` `verify_peer` unit test, not via the mock transport).
- **Bytes-for-bytes determinism.** The mock script is bytes; the FSM-under-test produces the same outbound bytes deterministically across runs (matching the §9 conformance corpus seam).
- **Cancellation honour.** The mock's async methods compose `co_await asio::post(exec)` checkpoints so the awaiter's cancellation slot fires deterministically; this lets §9 seam #5 (cancellation propagation) test against the mock.

### §4.9 Construction / lifetime / ownership rules

- **`Transport` instance is session-owned and per-attempt.** `Session::transport_` holds `std::unique_ptr<Transport>` minted at session open via `TransportFactory::make(...)`. The factory **selection** is frozen-at-open per `[arch §5.6]` (the resolved `transport_factory_override.value_or(default_transport_factory)` is locked for the session lifetime); the `Transport` **instance** is per-attempt. Per Clarifications 2026-05-27 Q1=B / Appendix D §D.4 reconnect destroys the dead `Transport`, sleeps for `delay_for_attempt(n)`, mints a fresh `Transport` via the same `TransportFactory`, and calls `async_connect` on the new instance — matching the QuickFIX-cpp / QuickFIX/J / Fix8 fresh-per-attempt pattern. The FSM holds at most one live `Transport` per session at any time.
- **`TlsTransport` is reached via exactly ONE `dynamic_cast<TlsTransport*>(transport_.get())`** at session open (the `async_handshake` issue site). The cast result is stored once on the `Session` object as a typed pointer — `TlsTransport* tls_transport_` — and every subsequent code path uses the typed pointer with no further casts. Post-handshake reads of `peer_identity` / captured pinset / negotiated cipher consume the FSM-held `handshake_result` value (returned by value from `async_handshake` per §4.2 RC#2 close); these reads have no virtual dispatch and no cast. RTTI is therefore opportunistic, not structural — a future `-fno-rtti` build can replace the cast with a non-virtual `as_tls() noexcept -> TlsTransport*` member on `Transport` (post-v1; v1.0 ships RTTI-on per the standard Linux/Clang and Windows/MSVC defaults).
- **`Endpoint` is owning by value** (`std::string` host); FSM holds it for the session lifetime.
- **`ReconnectPolicy` is owning by value**; FSM holds it for the session lifetime. Per Appendix D §D.6 the `ReconnectPolicy::delay_for_attempt` helper is `noexcept` and takes a one-parameter `(attempt_n)` shape — the policy owns the deterministic seed source per `[const §VII.7]` fuzz determinism (the session FSM supplies the session-id seed at session open; the policy hashes `(session_id_seed, attempt_n)` into the jitter draw). The schedule is a `std::pmr::vector<std::chrono::milliseconds>` per Appendix D §D.5 — the PMR allocator binds at policy construction (engine or session arena per `[2d §6.6]`).
- **`Listener` instance is engine-owned.** `Engine::listeners_` (post-v1; v1.0 wires Listeners through `service/` per `[arch §4.11]`) holds `std::unique_ptr<Listener>`. Lifetime is engine lifetime.
- **`TransportFactory` instance is engine-owned (engine-anchor) or session-owned (session-override).** Per Appendix D §D.1 / §D.2: `EngineConfig::default_transport_factory` is `unique_ptr<TransportFactory>`; `SessionConfig::transport_factory_override` is `unique_ptr<TransportFactory>`. Resolved at session open per `[2d §4.5]` engine-anchor + session-`*_override` pattern.
- **`peer_identity`** is OWNING (per `[2g §4.5]`). It is returned by value inside `handshake_result` from `async_handshake` (§4.2); the FSM holds `handshake_result` by value across the session lifetime. Phase-4 reads `peer_identity` from the FSM-held `handshake_result` for the T-041 binding.
- **`pin_snapshot` shared_ptr** is OWNING via the snapshot's `shared_ptr` (per `[2g §4.6]`); returned inside `handshake_result.captured_pinset` from `async_handshake`; the matched-pin entries stay alive past concurrent rotation per `[2g §4.3]` / `[2g §6.5.1]`.
- **`handshake_result`** is OWNING by value (per §4.2). The struct's three members are all owning (peer_id by value with PMR-allocated SAN strings; captured_pinset shared_ptr; negotiated_cipher PMR-allocated `pmr::string`); the FSM holds it for the session lifetime. No view fields, no cross-doc lifetime contract.

Every `expected_t<T>`-returning method declared in §4.1–§4.8 carries `[[nodiscard]]`. Every parameter carrying a non-owning view (span parameters on `async_read_some` / `async_write`; `Endpoint const&` on `async_connect`; `SslCtxConfig const&` on `async_handshake`) carries `[[clang::lifetimebound]]` at the declaration site of the abstract base.

---

## §5 Public C ABI

**2h is not the C-ABI doc.** Per `[arch §4.10]` the C ABI surface for `transport/` is delegated to **2i**; 2h defines the C++ shapes 2i will expose.

**Shapes 2i will need:**

- `fixpp_transport_t` opaque handle wrapping `std::unique_ptr<fixpp::transport::Transport>` (consistent with 2i's other opaque-handle shapes per `[arch §4.10]`).
- `fixpp_tls_transport_t` opaque handle (downcast subtype, exposed via a separate accessor).
- `fixpp_listener_t` opaque handle wrapping `std::unique_ptr<fixpp::transport::Listener>`.
- `fixpp_transport_factory_t` opaque handle wrapping `std::unique_ptr<fixpp::transport::TransportFactory>`.
- `fixpp_endpoint_t` PoD struct mirroring `Endpoint` (`const char* host`, `uint16_t port`, `uint32_t backlog`).
- `fixpp_reconnect_policy_t` PoD struct mirroring `ReconnectPolicy` (per Appendix D §D.5 shape: schedule-array + jitter + max_attempts). The C-ABI surface carries the schedule as a `(const uint32_t* delays_ms, size_t delays_len)` pair (delays in milliseconds) plus `double jitter` + `uint32_t max_attempts`; 2i builds the PMR-backed `std::pmr::vector<std::chrono::milliseconds>` at the C-bridge boundary.
- `fixpp_connect_info_t` PoD struct mirroring `ConnectInfo`.
- `fixpp_transport_async_connect(...)`, `fixpp_transport_async_read_some(...)`, `fixpp_transport_async_write(...)`, `fixpp_transport_cancel(...)`, `fixpp_transport_close(...)` — async ops mapped through 2i's awaitable-to-C-callback bridge (the same shape 2i uses for `[2g §5]` `fixpp_cert_source_load_credentials`).
- `fixpp_tls_transport_async_handshake(...)` returning a `fixpp_handshake_result_t` PoD struct mirroring the C++ `handshake_result` value type (§4.2): the C-ABI shape carries the `peer_identity` (raw subject_dn + SAN list crossing as `(const char*, size_t)`), `negotiated_cipher` (string), and a `fixpp_pinset_snapshot_t` opaque handle wrapping `shared_ptr<const pin_snapshot>` per `[2g §5]`. The C-ABI consumer reads from the returned PoD; no separate accessor symbols are needed because the underlying C++ shape is value-typed.
- `FIXPP_ERR_TRANSPORT_*` C-ABI coalescing groups per §6.6.
- Reentrancy classifications per `[const §X.5]`: `cancel()` / `close()` are **strand-confined** (⚠️ read `thread-safe` until 2026-09-02 — #333/#340); the async methods are `single-thread` (invoked from the session's `session_executor` per `[2d §4.8]`).

**No 2h shape is fundamentally incompatible with the C ABI delegation.** The `Transport` virtual surface, the `Endpoint` / `ReconnectPolicy` PoD types, and the `peer_identity` accessor all map cleanly to opaque-handle / PoD / callback shapes 2i already uses.

---

## §6 Behavioral contract

### §6.1 Allocation / exceptions / threading

**Hot path.** For 2h, the "hot path" is the **read-completion-handler dispatch** (the bytes-from-kernel-to-`Framer::feed` window) and the **write-issue path** (the bytes-from-`Writer::commit-span`-to-kernel window). Both are zero-allocation when the caller supplies arena-backed buffers (the framer-carry arena per `[2b §6.6]` for reads; the per-message arena for writes — the post-commit span lives there).

- `async_read_some` — completion handler runs on the session strand (per `[2d §7.6]`). The completion-handler frame is HALO-target-elided in the warm path; PMR fallback uses the session's `session_arena` per `[const §XI.6]`. **Zero global heap** by API construction (the read buffer is caller-owned).
- `async_write` — same pattern; the bytes span is caller-owned (`[2b §4.5]` post-commit span; `[2e §6.1.4]` durable-before-transmit). The completion handler runs on the strand.
- `async_handshake` — runs on the session strand; the OpenSSL handshake protocol exchange uses ASIO async_read / async_write internally and inherits the same allocation discipline. The `verify_peer` call inside `SSL_VERIFY_PEER` is synchronous and zero-allocation when `SslCtxConfig::mr` is non-null per `[2g §6.1]`.

**Cold path.** Transport construction (`asio_tls_transport` constructor + `SSL_CTX` build); Listener construction (`acceptor.bind` + `acceptor.listen`); reconnect schedule (`Clock::sleep_until` between retries; allocations are at the session-FSM level, not transport-level).

**Exception-free across read/write/handshake.** Every async transport method returns `awaitable<expected_t<...>>` and never throws across the session-strand-bound hot-path window per `[arch §5.3]`. PMR allocation throws on the rare cold path (e.g., `peer_identity` SAN-string copies via `[2g §4.5]` `verify_peer` — already routed through `[2a §4.2]` `trap_throw` per `[2g §6.1]`) surface as `error::transport_*` or `error::tls_*` variants. The constructor is allowed to throw per `[arch §5.3]` carve-out; the factory wraps in `expected_t<...>`.

### §6.2 Handshake invariants — captured-once `Pinset` snapshot per `[2g §6.5.1]`

The handshake-time `Pinset` access binding contract per `[2g §6.5.1]`:

- **`async_handshake` captures `Pinset::snapshot()` ONCE at handshake start** (BEFORE the OpenSSL handshake protocol exchange begins; specifically, before the `SSL_do_handshake` call that ASIO's `asio::ssl::stream::async_handshake` issues internally).
- **The captured `shared_ptr<const pin_snapshot>` is stored in `asio_tls_transport::captured_pinset_`** for the duration of the handshake AND for the lifetime of the `TlsTransport` instance (so the same snapshot is included in the `handshake_result.captured_pinset` returned by `async_handshake`'s awaitable on success — the FSM holds the value for the connected session).
- **The `verify_peer_trampoline` (the OpenSSL `SSL_VERIFY_PEER` callback set on `SSL_CTX`) reads from `captured_pinset_`**, not from `Pinset::snapshot()` directly — there is no per-peer-cert `snapshot()` re-acquisition; there is no `Pinset::find(...)` call inside the trampoline. The captured snapshot is scanned in-place per peer-cert lookup.
- **Mid-handshake `Pinset::add(...)` / `Pinset::remove(...)`** per `[arch §5.6]` carve-out + `[2d §7.5]` strand-safety boundary do NOT affect the in-flight handshake — the `shared_ptr` keeps the captured snapshot alive past concurrent rotation per `[2g §4.3]`. The next handshake (e.g., on reconnect) captures the post-rotation snapshot.

The §9 seam #7 (TLS handshake against pinned/unpinned/rotated pinsets, cross-doc with 2g) verifies the captured-once contract through instrumentation. The §9 seam #11 (mid-session rotation does not affect in-flight handshake — extension of `[2g §9 seam #15]`) extends the contract test with a transport-driven scenario.

### §6.3 Latency Tier 1 ceilings

Per `[arch §5.6]` / `[const §VIII.1]` / `[const §VIII.2]` / `[const §VIII.3]` / `[2a §6.5]` / `[2b §6.6]` / `[2d §6.3]` / `[2e §6.6]` / `[2f §6.3]` / `[2g §6.3]` precedent (per-doc Tier 1 ceiling tables).

| Operation | Tier 1 ceiling | Rationale |
|---|---|---|
| `Transport::async_read_some` — completion-handler dispatch on the strand (warm cache, HALO-fired) | **≤ 200 ns p99** | Strand `dispatch` of the completion handler ≈ 50–100 ns (matches `[2d §6.3]` strand-dispatch row); ASIO completion-token plumbing ≈ 50–100 ns; awaitable resume ≈ 5–20 ns under HALO. The bytes-from-kernel cost is OS-side and not counted; this row covers the ASIO-and-engine plumbing only. |
| `Transport::async_write` — call-site issue on the strand (the cost the caller pays for `co_await async_write(...)` issue, NOT the wire transmit time) | **≤ 200 ns p99** | Same arithmetic as `async_read_some`. The actual transmit time is OS+network-bound and not budgeted here. |
| `Transport::async_connect` — TCP-only, warm DNS cache (loopback) | **≤ 5 µs p99** | OS-bound (loopback connect is microseconds-class); the engine plumbing on top is ≤ 200 ns. CI flags > 2× regression on this row (I/O-bound soft ceiling). |
| `TlsTransport::async_handshake` — TLS 1.3 1-RTT handshake against loopback peer (warm session — no `verify_peer` computation cost on a self-signed test cert) | **≤ 10 ms p99 (soft)** | Dominated by the OpenSSL handshake protocol cost (1.5 round trips + ECDH + RSA-PSS sign). Real-network handshakes against an exchange gateway are tens to hundreds of milliseconds; the loopback bench measures the engine + OpenSSL cost only. CI flags > 2× regression. |
| `verify_peer` policy core (the 2g `verify_peer` predicate; 2h's `SSL_VERIFY_PEER` callback dispatches into it) | **≤ 50 µs p99** per `[2g §6.3]` row 4 | Cited from `[2g §6.3]` directly — 2h does not re-budget the policy core. |
| `cancel()` — synchronous on the strand | **≤ 1 µs p99** | One ASIO cancellation_signal emit; bounded. |
| `close()` — TLS bidi shutdown to loopback peer (warm) | **≤ 100 ms p99 (soft)** | Bounded by `Transport::Config::tls_close_timeout` (default 1 s); CI flags any case where close exceeds the configured timeout (a tighter bound is enforced by the test harness, but the production budget is the configured timeout). |

CI flags > 5 % regression on the hot-path rows (`async_read_some` / `async_write` completion-dispatch); > 2× regression on the I/O-bound rows (`async_connect`, `async_handshake`, `close`). Per `[const §VIII.1]` / `[const §VIII.2]` / `[const §VIII.3]`, these are perf-sensitive and benched in `bench/transport/`.

### §6.4 Cancellation contract — two-phase close, ASIO native slots

Per `[const §XI.2]` / `[2d §4.7]`:

- **Phase 1 (graceful close).** Per `[2d §4.7]` per-mode effect table: `Transport::async_read` and `Transport::async_write` continue to run under the root cancellation state during phase 1 — the FSM lets in-flight reads drain (so any inbound bytes still in the kernel receive buffer are framed by 2b before the session terminates) and lets the outbound `Logout` `async_write` run under the **child** cancellation state (the child is composed below the root per `[2d §4.7]`'s graceful close mechanic).
- **Phase 2 (teardown).** Root `cancellation_type::total` fires; in-flight `async_read_some` / `async_write` / `async_handshake` complete with their `*_cancelled` variants. The transport's `close()` is invoked (synchronous, on-strand) to issue TLS bidi close-notify and tear down the OS-level socket.
- **`Session::close(terminal)`** skips phase 1 and goes directly to phase 2; in-flight transport ops are cancelled immediately.
- **Cancellation surfacing.** Each async method's awaitable completes with the appropriate `transport_*_cancelled` variant per §6.6 (`transport_connect_cancelled`, `transport_read_cancelled`, `transport_write_cancelled`, `transport_handshake_cancelled`, `transport_accept_cancelled`). The C-ABI bridge maps these into `FIXPP_ERR_CANCELLED` per `[const §XI.2]`.
- **Reconnect interaction.** When the session FSM (Phase-4 spec) drives reconnect, it calls `Transport::cancel()` to interrupt any in-flight op, then `Transport::close()` to tear down the TLS+TCP state, then **destroys the dead `Transport`** (the `unique_ptr` is reset), **sleeps for `delay_for_attempt(n)`**, **mints a fresh `Transport` via the same `TransportFactory`** (Clarifications 2026-05-27 Q1=B / Appendix D §D.4), and **calls `async_connect` on the new instance**. The reconnect schedule (`ReconnectPolicy::delay_for_attempt`) is FSM-side; 2h is the wire-side hop only. The factory caches `SslCtxConfig` / PMR root / engine clock at factory level per FR-026 — the per-attempt mint cost is therefore the `Transport`-only construction, not a full `SSL_CTX` rebuild.

#### §6.4.1 Per-mode cancellation effect table — 2h-owned ops (extends `[2d §4.7]`)

Per `[2d §4.7]` per-mode effect table precedent and `[2f §4.7]` / `[2g §3.7]` sibling-doc-extension precedent: 2h publishes its own per-op cancellation rows for the five 2h-owned async ops, extending — not replacing — `[2d §4.7]`'s parent table. Each row enumerates the effect of `graceful (phase 1)` / `graceful (phase 2)` / `terminal` cancellation on the named op. The rows below are the binding contract; the §9 seam #5 (cancellation propagation) verifies each row.

| Op | `graceful (phase 1)` (Logout outbound; in-flight reads drain) | `graceful (phase 2)` (root `cancellation_type::total` fires) | `terminal` (immediate, root `cancellation_type::total`) |
|---|---|---|---|
| `Transport::async_connect` | Continues if not yet completed (the FSM rarely closes a session whose connect is still in flight; if it does, the FSM has already reached terminal — see right-most column). On completion, the FSM will not transition to handshake under graceful close. | Cancels with `transport_connect_cancelled`; the partial socket state is torn down by `close()`. | Cancels immediately with `transport_connect_cancelled`. |
| `Transport::async_read_some` | **Continues under the root cancellation state** (per `[2d §4.7]`); the FSM lets the in-flight read drain so any inbound bytes still in the kernel receive buffer are framed by 2b before the session terminates. The strand serialisation guarantees no overlap. | Cancels with `transport_read_cancelled`; partial reads up to the cancellation point are LOST per ASIO's contract. | Cancels immediately with `transport_read_cancelled`. |
| `Transport::async_write` | The Logout `async_write` runs under the **child** cancellation state (per `[2d §4.7]`); other in-flight writes complete under the root. The persisted frame survives per `[2e §6.1.4]` durable-before-transmit; `transport_write_cancelled` is the wire-side abort only. | Cancels with `transport_write_cancelled`; persisted frame survives per `[2e §6.1.4]`. | Cancels immediately with `transport_write_cancelled`; persisted frame survives. |
| `TlsTransport::async_handshake` | **Runs to completion** — the OpenSSL handshake state cannot be partially rolled back cleanly; an in-flight handshake under graceful phase 1 completes (success or `transport_handshake_failed`). The captured pinset snapshot per `[2g §6.5.1]` is bound at handshake start; mid-handshake rotation does not affect it. | Cancels with `transport_handshake_cancelled`; the `SSL*` state is left in a broken state and the caller MUST `close()` to clean up. | Cancels immediately with `transport_handshake_cancelled`; same broken-`SSL*` constraint. |
| `Listener::async_accept` | Engine-scoped — does NOT respond to per-session graceful close (the Listener outlives many `Transport` instances). For a per-engine graceful close, treat as terminal. | Same — engine-scoped; per-session graceful phase 2 does not affect the Listener. | Completes with `transport_accept_cancelled` on cancellation. ⚠️ **This read "when engine-level `Listener::cancel()` fires" until 2026-08-31 (#333) — NOTHING CALLS `Listener::cancel()`.** An unfiltered, untruncated sweep of `src include tests` (2026-08-31) finds callers only in `tests/`; this row described a surface that was never wired. **No corrected mechanism is named in its place** — that would rot on the next teardown change with nothing to notice it. Derive it: `grep -n "accept_scope_signals_" src/session/engine.cpp`. `Listener::cancel()` remains a concrete-impl surface per FR-023 with no production caller; #333 settled this as a documentation defect, NOT by wiring the call. |

The `async_accept` rows differ from the per-session ops because the Listener's lifecycle is engine-bound (per §4.6's "Inheritance" note: the Listener outlives many `Transport` instances). The §9 seam #14 (acceptor round-trip) verifies the engine-scoped cancellation behaviour.

### §6.5 Cancellation contract — `cancellable_dispatch` recipe (off-strand handoffs)

For the OpenSSL signing path through `[2g §4.1]`'s `async_signer_ref` (HSM impls that suspend OFF the session strand), the `verify_peer_trampoline` 's SAN-extraction work (rare; covered by `[2g §6.1]`'s zero-alloc handshake budget under `SslCtxConfig::mr`), and any future DNS-resolve-on-worker-thread escape hatch, the implementer follows `[2d §6.5]`'s `cancellable_dispatch` recipe verbatim — mirroring `[2g §6.4]`'s `load_credentials` recipe publication. The recipe body:

```cpp
// Inside an off-strand handoff (pseudocode; only invoked when the impl
// genuinely needs to suspend off the session strand):
asio::awaitable<core::expected_t<T>>
asio_tls_transport::off_strand_handoff() {
    // 1. Read the awaiter's bound executor (the session_executor wrapper per [2d §4.8]).
    auto exec = co_await asio::this_coro::executor;

    // 2. Read the awaiter's cancellation_state per [SYN §3.2 Q6a] / [const §XI.2].
    auto cs = co_await asio::this_coro::cancellation_state;

    // 3. Reap pre-handoff cancellation: if the slot already shows total, complete
    //    immediately with transport_*_cancelled.
    if (cs.cancelled() != asio::cancellation_type::none)
        co_return core::expected_t<T>{ unexpect, error::transport_handshake_cancelled };

    // 4. For any work that must run off the session strand, post the handoff via
    //    [2d §6.5] cancellable_dispatch. cancellable_dispatch returns
    //    awaitable<expected_t<void>>; expected_t::unexpected{dispatch_aborted}
    //    from the dispatch maps to transport_*_cancelled at this boundary.
    auto dispatched = co_await fixpp::core::cancellable_dispatch(
        exec, cs.slot(),
        [this]() { /* off-strand work, e.g., await async_signer_ref::sign(...) */ });

    if (!dispatched)
        co_return core::expected_t<T>{ unexpect, error::transport_handshake_cancelled };

    co_return core::expected_t<T>{ /* the result */ };
}
```

The recipe is enforced at §9 seam #5 (cancellation propagation; tests cover both the standard ASIO native cancellation path AND the off-strand `cancellable_dispatch` path). 2h is the **third pluggable awaitable-handoff site** in the project after `[2d §4.1.1]` `Clock::sleep_until` and `[2g §4.1]` `cert_source::load_credentials`, and inherits the same recipe-publication obligation per the cross-doc precedent.

### §6.6 Errors introduced by this design

Per the per-doc-prefix discipline established by `[2b §6.7]` (`FIXPP_ERR_WIRE_*`), `[2c §6.7]` (`FIXPP_ERR_DICT_*`), `[2d §6.7]` (`FIXPP_ERR_THREAD_*`), `[2e §6.7]` (`FIXPP_ERR_STORE_*`), `[2f §6.5]` (`FIXPP_ERR_SYNC_*`), `[2g §6.6]` (`FIXPP_ERR_TLS_*`): 2h adopts the prefix **`FIXPP_ERR_TRANSPORT_*`** for its C-ABI mapping target, owned by 2i.

Numeric range allocation. Per the per-doc-prefix convention each doc owns a non-overlapping numeric block in the engine `error` enum's variant ordering. 2h claims a contiguous block of **22 variants** under the `transport_*` prefix; 2i is asked to assign the block adjacent to 2g's `tls_*` block.

| `fixpp::core::error` variant | Source section | Remediation class |
|---|---|---|
| `transport_resolve_failed` | §4.1 — `async_connect` resolver step failed (DNS NXDOMAIN, host string malformed, no AF match for the host's address-family). Wraps `asio::error::host_not_found` and similar. | Configuration error — fix the host string. |
| `transport_connect_refused` | §4.1 — TCP connect returned `ECONNREFUSED` (peer not listening on port). | Operator / counterparty error — peer down or port misconfigured. |
| `transport_connect_timeout` | §4.1 — TCP connect did not complete within `Config::connect_timeout` (default 30 s). | Network / counterparty error — peer unreachable or behind an unresponsive firewall. |
| `transport_connect_cancelled` | §4.1 — `async_connect` cancelled before completion (cancellation_type::total observed). | Cancellation; not an error in most contexts — caller decides. |
| `transport_already_connected` | §4.1 — `async_connect`/`async_handshake` refused by ENTRY STATE (#339, #342): from an already-SUCCEEDED state, or while an attempt is IN FLIGHT. A FAILED attempt leaves the Transport retryable and does NOT surface this variant; a CLOSED one answers `transport_already_closed`. | Programmer error — fix the FSM. |
| `transport_read_in_progress` | §4.1 — `async_read_some` called while a previous `async_read_some`'s awaitable has not yet completed (in-flight exclusivity violation per §4.1 normative contract). | Programmer error — fix the FSM (one in-flight read per Transport). |
| `transport_write_in_progress` | §4.1 — `async_write` called while a previous `async_write`'s awaitable has not yet completed (in-flight exclusivity violation per §4.1 normative contract). | Programmer error — fix the FSM (one in-flight write per Transport). |
| `transport_reconnect_limit_exceeded` | §4.4.1 — the FSM's reconnect loop exhausted `ReconnectPolicy::max_attempts` (default 10 per `ReconnectPolicy::defaults()`). The session terminates; operators that need a longer envelope override `max_attempts` explicitly. | Operational event — peer unreachable for the configured envelope; session terminates per `[const §XV]` thunder-herd-pattern intent. |
| `transport_read_eof` | §4.1 — peer closed the connection cleanly (TCP FIN received; TLS close-notify received). | Lifecycle event — FSM transitions to disconnect / reconnect. |
| `transport_read_truncated` | §4.1 — peer closed the connection without TLS close-notify (only relevant for `TlsTransport`); v1.0 TREATS this as `transport_read_eof` (logged at `warn` level by 2k per `[2g §7.8]`) but a strict-mode opt-in escalates to a hard error. The default mode is permissive. | Counterparty error — counterparty's TLS impl misbehaving. |
| `transport_read_error` | §4.1 — `async_read_some` returned a non-EOF, non-cancellation error (`ECONNRESET`, `EPIPE`, OS-level network error). | Network / OS error — FSM disconnects. |
| `transport_read_cancelled` | §4.1 — `async_read_some` cancelled before completion. | Cancellation; not an error. |
| `transport_write_short` | §4.1 — `async_write` completed with bytes-written < bytes-requested AND the awaitable did not return an error code (rare; ASIO's composed `async_write` should not produce this, but defensive handling for non-default impls). | Programmer / impl error — bug in the `Transport` impl. Surfaces a forced disconnect. |
| `transport_write_error` | §4.1 — `async_write` returned a non-cancellation error (`EPIPE`, `ECONNRESET`). | Network error — FSM disconnects; durable-before-transmit per `[2e §6.1.4]` ensures the persisted frame survives. |
| `transport_write_cancelled` | §4.1 — `async_write` cancelled. Per `[2e §6.1.4]` durable-before-transmit invariant: the persisted frame is NOT rolled back; the peer's later `ResendRequest` is honourable. | Cancellation; not an error. |
| `transport_handshake_failed` | §4.2 — TLS handshake failed at the OpenSSL level (cipher mismatch, cert chain unverifiable, peer cert rejected by `[2g §4.5]` `verify_peer` predicate). The diagnostic field carries the underlying OpenSSL error string + the `[2g §6.6]` `tls_*` sub-reason if `verify_peer` rejected. Joins `[2g §6.6]` `tls_handshake_failed` group at the C ABI. | Configuration / counterparty error. |
| `transport_handshake_cancelled` | §4.2 — `async_handshake` cancelled. The SSL* state is broken; caller MUST close(). | Cancellation. |
| `transport_handshake_timeout` | §4.2 — `async_handshake` exceeded `Config::tls_handshake_timeout` (default 30 s). | Network / counterparty error. |
| `transport_accept_cancelled` | §4.6 — `Listener::async_accept` cancelled. | Cancellation. |
| `transport_already_closed` | §4.1 — `async_connect` / `async_read_some` / `async_write` / `async_handshake` called after `close()` returned. ⚠️ NOT `cancel` — it defines no failure and returns {} unconditionally (#340). | Programmer error — fix the FSM. |
| `transport_factory_failed` | §4.7 — `TransportFactory::make(...)` reported failure (e.g., OS resource exhaustion at socket creation; OpenSSL `SSL_CTX_new` failure). | Configuration / runtime error. |
| `transport_psk_unsupported` | §4.5 — caller's `SslCtxConfig` requested PSK (T-012); v1.0 default impl rejects until T-012 P2 hook ships. | Configuration error — fall back to `mtls_*` profile or wait for v1.x. |

(22 variants total — covers the §6 brief's 8 axes (connect failure / read EOF / read truncation / read error / write short / write cancellation / handshake failure / handshake cancellation / backpressure / DoS bounds) plus the v0.2 RC#3 close additions: 2 in-flight-exclusivity rows (`transport_read_in_progress`, `transport_write_in_progress`) per §4.1's normative API-level contract, 1 reconnect-cap row (`transport_reconnect_limit_exceeded`) per §4.4.1's normative numeric ceiling, plus the lifecycle/idempotency/factory/PSK rows (`transport_already_connected`, `transport_already_closed`, `transport_factory_failed`, `transport_psk_unsupported`). v0.1's accessor-protection variant — see Appendix C convergence log for the named dropped symbol — was dropped in v0.2 (RC#2 close: the v0.1 trio of accessor pure-virtuals is replaced by the value-typed `handshake_result` returned by `async_handshake`; reading from the FSM-held value cannot fail "before handshake" because the FSM holds the value only after `async_handshake` succeeds; round-2 Codex P3-1 close — the explicit symbol-name mention is purged from this parenthetical, the convergence-log entries in Appendix C remain verbatim per `[const §VI.5]` exact-citation rule for change recording). The variant count IS the coverage measure; the §6 brief minima is satisfied with headroom.)

C-ABI mapping (delegated to **2i**) per the per-doc-prefix discipline:

- connect / lifecycle errors (`transport_resolve_failed`, `transport_connect_refused`, `transport_connect_timeout`, `transport_already_connected`, `transport_already_closed`, `transport_read_in_progress`, `transport_write_in_progress`, `transport_reconnect_limit_exceeded`) → **`FIXPP_ERR_TRANSPORT_LIFECYCLE`**;
- I/O errors (`transport_read_eof`, `transport_read_truncated`, `transport_read_error`, `transport_write_short`, `transport_write_error`) → **`FIXPP_ERR_TRANSPORT_IO`**;
- handshake errors (`transport_handshake_failed`, `transport_handshake_timeout`) → **`FIXPP_ERR_TRANSPORT_HANDSHAKE`** (joins `[2g §6.6]` `FIXPP_ERR_TLS_HANDSHAKE` at the C-ABI level — 2i decides the exact coalescing);
- configuration errors (`transport_factory_failed`, `transport_psk_unsupported`) → **`FIXPP_ERR_TRANSPORT_CONFIG`**;
- cancellation (`transport_connect_cancelled`, `transport_read_cancelled`, `transport_write_cancelled`, `transport_handshake_cancelled`, `transport_accept_cancelled`) → reuses the existing **`FIXPP_ERR_CANCELLED`** per `[const §XI.2]`.

Final coalescing is 2i's call.

### §6.7 Durable-before-transmit invariant per `[2e §6.1.4]` (binding)

This subsection surfaces the cross-doc invariant explicitly so a reader of 2h alone does not miss it.

**Binding contract:** the FSM (Phase-4 spec) sequences outbound dispatch as `toApp → Writer::commit → store(committed_span, outbound) → transport.async_write`. **`Transport::async_write` MUST NOT be called until the corresponding `MessageStore::store(...)` for the same outbound seqnum has linearised** (per `[2e §6.1.4]`). **A cancelled `async_write` MUST NOT trigger any rollback of the persisted frame** — the persisted frame survives, the peer's later `ResendRequest` per `[FIX-SL §4.5.2]` is honourable, and the cancellation surfaces purely as a wire-side abort.

2h's surface guarantees this by API construction — `async_write` does not call into the store, does not own any rollback path, and does not know the seqnum. The FSM owns the ordering; 2h is the wire-side hop only.

The §9 seam #8 (durable-before-transmit ordering, cross-doc with 2e) verifies the contract through a fault-injection scenario: store completes successfully, transport's `async_write` is cancelled mid-flight, the test asserts the persisted frame is intact AND the next-attempt `ResendRequest` from the peer reads the persisted frame.

### §6.8 Backpressure per `[arch §5.8]` / `[const §XV.15]`

Per `[arch §5.8]`: `block` and `disconnect-and-recover` are the only two modes on the app/session message paths. **`drop-oldest` is BANNED** on these paths per `[const §XV.15]`.

2h's role:

- **Read path.** No backpressure mechanism at the transport level — the read buffer is caller-supplied (typically the framer-carry arena per `[2b §6.6]`), and the rate-limit emerges from the strand's serialisation: the FSM does not call `async_read_some` again until it has handed the previous read's bytes to `Framer::feed`. If the kernel receive buffer fills up, TCP's window-flow-control engages at the OS level; `[const §XV]`'s ban does not apply to OS-level TCP back-pressure (this is window flow control, not message drop).
- **Write path.** The session strand IS the depth-1 queue (per §1.1 / §3.2). `block` mode is implemented by the strand's serialisation — a second outbound coroutine that suspends waits its turn. `disconnect_and_recover` is the FSM's call (it cancels the in-flight `async_write` via `Transport::cancel()` and tears down the session per `[arch §5.8]`); 2h provides the cancellation surface, the FSM owns the policy.

**No transport-internal write queue.** The v1.0 design does NOT introduce a transport-internal queue. Adding one is out of v1.0 scope (would need a backpressure mode design across `Transport` + `MessageStore`); the strand+`block` model handles every venue at v1.0 throughput targets.

---

## §7 Integration with adjacent modules

### §7.1 Wire (2b) — read into `Framer::feed`; write from `Writer::commit` span

Per `[2b §4.2]` `Framer::feed`'s contract: 2b accepts `std::span<const std::byte>` from the transport. The session FSM (Phase-4 spec) sequences:

```
co_await transport.async_read_some(carry_buf.tail_span())
  → Framer::feed(incoming_span, carry, frame_views_out)
  → for each frame_view: Parser::parse(...) → fromApp
```

The buffer (`carry_buf.tail_span()`) is sourced from the `framer_carry_arena` per `[2b §6.6]` / `[2b §8]`; 2h never allocates the read buffer. On the write side, per `[2b §4.5]`'s `Writer::commit() && -> expected_t<size_t>` the post-commit span is the only valid input to `transport::async_write` (durable-before-transmit per `[2e §6.1.4]`).

2h does NOT instantiate `frame_view` or `MessageView`; bytes pass through 2h as raw `std::span<const std::byte>` only.

### §7.2 Session (Phase-4 spec) — CompID-to-TLS-identity binding (T-041) + reconnect FSM + durable-before-transmit ordering

**T-041 partition.** Per `[2g §7.2]`: 2g supplies the `peer_identity` value (parsed peer-cert subject DN + SANs); 2h delivers it through the value-typed `handshake_result.peer_id` returned by `TlsTransport::async_handshake` (see §4.2). The session-module Phase-4 spec performs the CompID-to-TLS-identity binding policy and surfaces `error::session_identity_mismatch` on rejection. 2h is the wiring layer between 2g's value and the FSM's policy.

**Reconnect FSM.** The `ReconnectPolicy` value type is held by `SessionConfig` (or attached per-`Endpoint` if the FSM has multi-endpoint failover); the schedule computation is policy-side per Appendix D §D.6 (`ReconnectPolicy::delay_for_attempt(attempt_n)` one-parameter; the policy owns the deterministic jitter seed source). The FSM consults `[2d §7.9]` `effective_clock.steady_now()` + `effective_clock.sleep_until(...)` to schedule retries; on each attempt it destroys the dead `Transport` and mints a fresh one via the same `TransportFactory` per Appendix D §D.4. 2h provides the value type; the session-module Phase-4 spec drives the FSM.

**Durable-before-transmit ordering.** Per §6.7 + `[2e §6.1.4]`. The FSM sequences `Writer::commit → store → async_write`; 2h's `async_write` is the last hop.

### §7.3 TLS (2g) — `cert_source` / `Pinset` / `SecurityProfile` / `CipherPolicy` consumption

2h consumes 2g's policy core through `[2g §4.5]`'s `SslCtxConfig` value (built by `make_ssl_ctx_config(profile, cs, clock, pinset, mr)`):

- **`SslCtxConfig::profile`** drives the `SSL_CTX_set_verify` flags per `[2g §4.5.1]`'s normative table (mtls_pinned: `SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT`; mtls_ca: same plus required CA anchors; one_way_ca: `SSL_VERIFY_PEER` server-side only; unset: rejected at `make_ssl_ctx_config(...)` per `[2g §4.5.1]`).
- **`SslCtxConfig::cs`** → `SSL_CTX_use_certificate(...)` + `SSL_CTX_use_PrivateKey(...)` for software-key impls; `SSL_CTX_set_client_cert_cb`-style callback for HSM `async_signer_ref` impls.
- **`SslCtxConfig::pinset`** → captured via `Pinset::snapshot()` at `async_handshake` start (per §6.2 binding contract); the captured snapshot is bound to the `SSL_VERIFY_PEER` callback context for in-trampoline access.
- **`SslCtxConfig::clock`** → consumed by `[2g §4.5]` `verify_peer` directly (2h does not consult the clock; the policy core does).
- **`SslCtxConfig::ciphers`** → `SSL_CTX_set_ciphersuites` (TLS 1.3 list joined from `[2g §4.4]` `tls13_suites`) + `SSL_CTX_set_cipher_list` (TLS 1.2 list from `tls12_suites`) + `SSL_CTX_set1_curves_list` (from `kx_groups`) + `SSL_CTX_set1_sigalgs_list` (from `sig_algs`).
- **`SslCtxConfig::mr`** → routed into the `peer_identity` SAN-string copies inside `verify_peer` (2g's responsibility); 2h's role is to keep `mr` alive past the handshake (it lives in `asio_tls_transport::ssl_cfg_`).

The §4.5.1 table-row enforcement happens at `make_ssl_ctx_config(...)` (rejection of `unset` / null pinset under `mtls_pinned` / non-null pinset under `one_way_ca` / null clock); 2h propagates the `expected_t<SslCtxConfig>` upward to the session-module Phase-4 spec at `Session::open`. The outer rejection at `Session::open` is `error::invalid_session_config` per `[2d §6.7]` / `[2g §7.2]`.

### §7.4 Threading + Clock (2d) — per-session strand + SessionConfig field shape

Per `[2d §7.6]`: every async transport op runs on the session strand (the `[2d §4.8]` `session_executor` wrapper, holding either `asio::strand` under `per_session_strand` mode or attested-serialised `any_io_executor` under `direct_executor` mode). Completion handlers rebind to the same `session_executor`.

`SessionConfig::transport_factory_override` field shape per §4.7.1 — engine-anchor + session-`*_override` matching the `[2d §4.5]` pattern across the dictionary, executor, clock, store-factory, cert-source, and (now) transport-factory axes. The Appendix D §D.1 / §D.2 drop-ins materialise the cross-doc amendments.

The `ReconnectPolicy::delay_for_attempt(...)` helper consumes the session-FSM's RNG per `[const §VII.7]` fuzz determinism; the FSM owns the schedule, 2h owns the value type.

### §7.5 MessageStore (2e) — durable-before-transmit binding

Per `[2e §6.1.4]` and §6.7 of this doc: the FSM sequences `store → async_write` durably. 2h's `async_write` does NOT participate in the persistence path; the persisted frame is independent of the wire transmit outcome.

A cancelled `async_write` MUST NOT roll back the persisted frame (per `[2e §6.1.4]` cancellation table). 2h surfaces `transport_write_cancelled` purely as a wire-side abort; the persisted frame survives.

The §9 seam #8 verifies.

### §7.6 Service / control plane (2j) — graceful drain + cert-source reload triggers

Per `[arch §8.1]` / `[2g §7.7]`: the control plane (gRPC schema at `service/proto/fixpp_control.proto`) carries `CloseSession` (graceful), `RotatePinset`, and `ReloadCertSource` RPCs. The handler dispatches into the engine's session-management path; for transport-relevant ops:

- **`CloseSession`** triggers `Session::close(graceful)` per `[2d §4.7]` two-phase close. 2h's `async_read` / `async_write` continue under the root state during phase 1; phase 2 fires `cancellation_type::total` and 2h's ops complete with `*_cancelled`. The FSM closes the transport.
- **`RotatePinset`** triggers `Pinset::add(...)` / `Pinset::remove(...)` per `[2g §4.3]`; mid-session rotation does NOT affect the in-flight handshake per `[2g §6.5.1]` / §6.2 binding. The next handshake (e.g., on reconnect after a network blip) captures the post-rotation snapshot.
- **`ReloadCertSource`** is a session close-and-reopen pattern per `[arch §5.6]` (the `cert_source` is frozen at session open). 2h provides the close path; the engine's session-management layer handles the reopen.

2h owns the call sites; 2j owns the gRPC schema + handler dispatch.

### §7.7 Logger + OTel (2k) — TLS handshake spans + transport-latency spans (cert-event spans owned by 2g per `[2g §7.8]`)

Per `[arch §5.7]` / `[2g §7.8]`:

- **TLS-event spans** (handshake start, handshake success/failure with reason, peer cert subject, peer cert SHA-256, negotiated cipher) — call sites are in 2h's `async_handshake`; the schema is owned by 2k. 2h emits one `fixpp.session.tls_handshake` span per handshake; 2k defines the attributes.
- **Transport-latency spans** — call sites are in 2h's `async_read_some` / `async_write`; the schema is owned by 2k. 2h emits no per-frame spans by default (would dominate the per-message OTel cost on high-throughput sessions); a sampling strategy is 2k's call.
- **Reconnect-attempt counters** — 2h emits one counter increment per `async_connect` retry; 2k defines the metric name. The counter is bounded by `ReconnectPolicy::max_attempts` per session.

The producer-side log call extracts the awaiter's value synchronously from the session-domain `trace_context` per `[const §XIII.3]` / `[2d §7.9]` — every transport-emitted record carries `trace_id` / `span_id` from the session's `session_local<trace_context>` slot.

### §7.8 C ABI (2i) — handle shape

Per §5: 2h defines the C++ shapes; 2i defines the C symbols. The opaque-handle shapes (`fixpp_transport_t`, `fixpp_tls_transport_t`, `fixpp_listener_t`, `fixpp_transport_factory_t`), the PoD types (`fixpp_endpoint_t`, `fixpp_reconnect_policy_t`, `fixpp_connect_info_t`), and the `FIXPP_ERR_TRANSPORT_*` coalescing groups (§6.6) are 2h's hand-off to 2i.

---

## §8 PMR — recap

Storage classes for 2h-owned data:

| Storage | Lifetime | Holds | Reset by |
|---|---|---|---|
| `asio_tls_transport::cfg_.mr` (or engine default per `[2d §4.4]`) | `Transport` instance lifetime (= session lifetime per `[arch §5.6]` frozen-at-open) | The `SslCtxConfig`'s `peer_identity` SAN-string copies (per `[2g §4.5]`); the OpenSSL `SSL_CTX*` configuration arena (the bytes 2g's `make_ssl_ctx_config` owns); any `peer_identity` material `verify_peer` allocates at handshake time | `~asio_tls_transport` |
| `asio_listener::cfg_.mr` (engine default) | `Listener` instance lifetime (= engine lifetime) | The acceptor-side per-acceptor `SslCtxConfig` storage | `~asio_listener` |
| **OpenSSL allocator path (process-global)** | engine lifetime | Every OpenSSL allocation (handshake state, peer cert parsing, internal buffers) | engine teardown via `CRYPTO_set_mem_functions` set ONCE at engine init by the engine bootstrap (NOT by 2h on a per-transport basis) |

**Lifetime classes for non-arena objects:**

- **`Transport` instance** — session lifetime (held via `std::unique_ptr<Transport>` in `Session`). Frozen at session open.
- **`Listener` instance** — engine lifetime (held via `std::unique_ptr<Listener>` in `Engine` / `service/`).
- **`TransportFactory` instance** — engine lifetime (engine-anchor `EngineConfig::default_transport_factory`) OR session lifetime (session-override `SessionConfig::transport_factory_override`); per §4.7 + Appendix D §D.1 / §D.2.
- **`Endpoint` value** — owning by value; held by FSM for session lifetime.
- **`ReconnectPolicy` value** — owning by value; held by FSM for session lifetime.
- **`ConnectInfo` value** — owning by value; returned by `async_connect`; FSM captures by value.
- **`peer_identity` (held inside `asio_tls_transport`)** — OWNING per `[2g §4.5]`; bounded by `Transport` instance lifetime; the session-module Phase-4 spec captures BY VALUE for T-041.
- **`captured_pinset_` `shared_ptr<const pin_snapshot>`** — held inside `asio_tls_transport`; bounded by `Transport` instance lifetime. The PMR resource backing the snapshot's `pin` members (via `Pinset::cfg_.mr`) outlives the snapshot per `[2g §4.6]` / `[2g §6.2]` engine-anchored MR contract.

Per `[const §VIII.5]`: zero `new`/`delete` between parse and `fromApp`, **extended here to the read-completion-handler dispatch** because the bytes pass through 2h's completion handler before 2b's `Framer::feed` sees them. The §9 seam #4 (allocation guard) verifies under `mallocnesia` on Linux/Clang Tier 1 that the read-path completion-handler dispatch does not touch the global heap.

---

## §9 Test seams

Per `[arch §10]` requirement (4) and `[const §VII.4]`. v0.3 ships **15 seams** (v0.1 was 14; v0.2 added seam #15 `in-flight exclusivity` per RC#3; v0.3 line-edits seam #13 to pin the §4.4.1 `defaults()` numerics per round-2 Codex P3-1 but adds no new seam). The `§9 brief minima` from the prompt was 10; we exceed by 5 to cover RC-style cross-doc seams and the v0.2 exclusivity contract more thoroughly. Each seam is referenced by **name** per the `[2d §9]` / `[2g §9]` cross-referencing precedent — ordinals are not stable across review rounds; names are.

1. **Conformance corpus integration — interop runs against this transport.** Drive the `tests/conformance/` corpus (TC-001..TC-017) end-to-end against `asio_tls_transport` over loopback (a cooperating QuickFIX test peer; cross-doc with `[const §VII.6]` interop requirement). Verify Logon → NewOrderSingle → ExecutionReport → Logout round-trips and matches the FIX wire bytes. Lives in `tests/conformance/test_transport_interop.cpp`.

2. **Latency regression — read-path completion-handler dispatch (`Transport::async_read_some`).** Google Benchmark on the warm-cache loopback read; verify ≤ 200 ns p99 ceiling (§6.3 row 1). CI fails on > 5 % regression. Lives in `bench/transport/bench_async_read_some_dispatch.cpp`.

3. **Latency regression — write-path issue (`Transport::async_write`).** Google Benchmark on the warm-cache loopback write; verify ≤ 200 ns p99 ceiling (§6.3 row 2). CI fails on > 5 % regression. Lives in `bench/transport/bench_async_write_issue.cpp`.

4. **Allocation guard on the read-path hot path.** `tools/check_alloc.py` + `mallocnesia` (Linux/Clang Tier 1 per the `[2a §9]` / `[2b §9]` / `[2d §9]` / `[2g §9]` precedent). 10⁴-frame test; zero global-heap `new`/`delete`/`malloc` on the `async_read_some` completion-handler → `Framer::feed` chain when the read buffer is arena-backed. PMR-arena allocations are expected. Lives in `tests/perf/test_transport_read_alloc_guard.cpp`.

5. **Cancellation propagation — connect / read / write / handshake all cancel cleanly.** For each async method (`async_connect`, `async_read_some`, `async_write`, `async_handshake`), issue the await inside a coroutine bound to a strand; fire the awaiter's cancellation slot before completion; verify the awaitable returns the appropriate `transport_*_cancelled` variant per §6.6. Run under both `per_session_strand` and `direct_executor` modes per `[2d §4.8]`. Includes the `cancellable_dispatch` recipe path (§6.5) for the off-strand handoff cases. Lives in `tests/transport/test_cancellation_propagation.cpp`.

6. **Mock transport harness — FSM-under-test.** Construct `mock_transport` with a scripted byte stream; drive a `Session` FSM through the full Logon-to-Logout cycle; verify the FSM produces the expected outbound bytes byte-for-byte and dispatches the expected `fromApp` calls. This is the primary FSM-under-test seam per `[arch §1.1]` / `[const §VII]`. Lives in `tests/session/test_session_fsm_via_mock_transport.cpp`.

7. **TLS handshake against pinned/unpinned/rotated pinsets — cross-doc with 2g (`[2g §9 seam #15]` extension).** Three scenarios: (a) pinned: handshake succeeds with peer cert in the pinset; (b) unpinned (`mtls_ca` mode): handshake succeeds via CA chain validation; (c) rotated: the pinset is mutated mid-handshake via a separate thread; the in-flight handshake still observes the pre-rotation captured snapshot per §6.2 binding contract; the next handshake (e.g., on reconnect) observes the post-rotation snapshot. Verifies the `[2g §6.5.1]` captured-once contract from the transport side. Lives in `tests/transport/test_tls_handshake_pinset_rotation.cpp`.

8. **Durable-before-transmit ordering — cross-doc with 2e (`[2e §9 seam #6]` extension).** Scripted scenario: outbound `Writer::commit` produces a frame; FSM calls `store(committed_span, outbound)` (success); FSM calls `Transport::async_write`; injection cancels the `async_write` mid-flight; assert (a) the persisted frame is intact (read it back via `MessageStore::retrieve`); (b) the `async_write` awaitable completes with `transport_write_cancelled`; (c) NO rollback of the persisted frame happens. Verifies the §6.7 / `[2e §6.1.4]` invariant. Lives in `tests/transport/test_durable_before_transmit_ordering.cpp`.

9. **CompID-to-TLS-identity round-trip — cross-doc with T-041 / Phase-4 session.** Acceptor-side scenario: a peer presents a cert with subject DN `CN=peer42.example.com`; the FSM's CompID-binding policy maps `peer42` → `TARGETCOMPID = PEER42_PROD`; verify (a) the `handshake_result.peer_id` returned by `async_handshake` carries `subject_dn_view() == "CN=peer42.example.com"`; (b) the FSM rejects on Logon if `TARGETCOMPID` does not match; (c) the FSM accepts on match. Verifies the T-041 cross-cut from the transport-delivery side. The CompID binding policy itself is owned by the session-module Phase-4 spec; this seam tests the transport-side delivery only. Lives in `tests/transport/test_compid_tls_identity_binding.cpp`.

10. **Backpressure / write-queue-depth bound (`block` mode).** Drive 10³ outbound frames through a session strand with a slow `mock_transport` (high write_latency); verify (a) outbound coroutines suspend on the strand (depth-1 queue per §1.1) without spilling into a transport-internal queue; (b) `disconnect_and_recover` mode triggers `Transport::cancel()` correctly when the FSM exceeds its policy threshold; (c) NO `drop-oldest` behaviour ever fires per `[const §XV.15]`. Lives in `tests/transport/test_backpressure.cpp`.

11. **Fuzzer (parser-touching seam — malformed bytes, truncated TLS records, partial reads).** libFuzzer-driven random byte streams feeding `mock_transport` to a `Session` FSM through 2b's `Framer`; ASan + UBSan invariants; verify no crash, no UAF, no UB on adversarial inputs. Specifically targets the read-path → `Framer::feed` boundary (the natural fuzz frontier in 2h since 2h delivers the bytes 2b parses). Required by `[const §VII.7]` parser-touching surface. Lives in `tests/fuzz/fuzz_transport_read_path.cpp`.

12. **TLS-handshake fuzzer — truncated TLS records, partial handshakes.** A separate libFuzzer harness for the handshake hot path: scripted partial TLS records fed through `mock_transport`'s scripted bytes; verify the `async_handshake` completes with `transport_handshake_failed` on every adversarial input, never crashes, never UAFs the captured pinset snapshot. Cross-doc with `[2g §9 seam #6]` cert-parsing fuzzer. Lives in `tests/fuzz/fuzz_transport_handshake.cpp`.

13. **Reconnect schedule determinism — `ReconnectPolicy::delay_for_attempt`.** Property test with deterministic policy seeds (per Appendix D §D.6 the policy owns the seed source); verify the schedule produces the documented schedule-with-jitter envelope; verify replay against fuzz determinism per `[const §VII.7]` (same `session_id_seed`, same schedule). Pin the `ReconnectPolicy::defaults()` schedule declared in §4.4.1 (`schedule = [100ms, 200ms, 400ms, 800ms, 1.6s, 3.2s, 6.4s, 12.8s, 25.6s, 30s]`; `jitter = 0.10`; `max_attempts = 10`) as the in-test policy: assert the cumulative wall-clock at `max_attempts = 10` equals 81 100 ms before jitter (matches the §1.1 / §4.4.1 rationale arithmetic) so a future tweak to `defaults()` cannot silently desync the published envelope from this seam (round-2 Codex P3-1 close). Also pin `defaults_quickfix_compat()` returns `{[30s], 0.0, 0}` (Appendix D §D.5). Lives in `tests/transport/test_reconnect_policy_schedule.cpp`.

14. **`Listener::async_accept` round-trip (T-005).** Construct an `asio_listener` on `127.0.0.1:0` (OS-picked port); spawn a client `asio_tls_transport` connecting to the listener's bound port; verify the listener mints a fresh `Transport`, the handshake succeeds on both sides, and a Logon round-trip works. Lives in `tests/transport/test_listener_acceptor.cpp`.

15. **In-flight exclusivity — overlapping `async_read_some` / `async_write` fail deterministically (RC#3 / Codex P1 #2 close).** From two coroutines bound to the same session strand, issue `async_write` on coroutine A (suspends); before A's awaitable completes, issue a second `async_write` on coroutine B; verify B's awaitable completes immediately with `expected_t::unexpected{transport_write_in_progress}` per §6.6 — the second call MUST NOT race into the underlying ASIO `async_write_some` initiation. Repeat for `async_read_some` (variant `transport_read_in_progress`). Repeat for `async_connect` and `async_handshake` — verify both raise `transport_already_connected` on a one-shot overlap. Verifies the §4.1 normative API-level exclusivity contract. Lives in `tests/transport/test_inflight_exclusivity.cpp`.

(15 seams. Brief minima 10. The five extras are #12 (TLS-handshake fuzzer — `[const §VII.7]` extends to the handshake parse surface), #13 (reconnect determinism — `[const §VII.7]` fuzz determinism extension), #14 (acceptor round-trip — T-005 discharge), #15 (in-flight exclusivity — RC#3 close), and #2 split from #3 (read vs write latency — separate bench targets).)

---

## §10 Open questions

| # | Question | Disposition | Owner |
|---|---|---|---|
| 1 | **PSK authentication (T-012) — concrete API hook for the future P2 work.** The `TlsTransport` sub-interface leaves 4 of 5 pure-virtual slots open, and the v1.0 default impl rejects PSK with `transport_psk_unsupported`. The hook shape (`async_psk_session_callback(...)` vs an `SslCtxConfig::psk_*` config field consumed by 2g) is post-v1. | DEFER to post-v1 (T-012 is P2 per `[const §XII.6]`). | post-v1 follow-up; 2g + 2h jointly |
| 2 | **gRPC vs in-process transport for the control-plane (2j).** The control plane (`SVC-005`) is a separate `ControlPlane` plugin in 2j; whether that plugin is itself implemented over `Transport` (gRPC over TCP, gRPC over Unix domain socket, in-process direct call) is 2j's call. 2h ships only the FIX-session wire transport. | DEFER to **2j**. | 2j |
| 3 | **Reconnect-policy defaults (concrete numbers per venue class).** §1.1 records v1.0 defaults (100 ms / 30 s / 2.0× / 10 attempts / ±10%). Per-venue tuning (FX retail, equity, equity-options, derivatives) is operationally distinct; whether the engine ships a per-venue preset library is post-v1. | **DECIDED** in v0.2 (RC#3 / Opus N-P1-3 close): `ReconnectPolicy::defaults()` per §4.4.1 returns `max_attempts = 10` as the v1.0 numeric ceiling per `[const §XV]` thunder-herd-pattern intent; operators that need a longer envelope override `max_attempts` explicitly per `[const §XII.5]` no-implicit-default pattern. A per-venue preset library is post-v1 (DEFER). | Phase-4 session-module spec (presets only); v0.2 ships the safe default. |
| 4 | **TCP socket option overrides (`SO_RCVBUF` / `SO_SNDBUF`) — when does an HFT user need to tune them?** Defaults are OS auto-tune; the `Transport::Config` exposes the knobs. Whether to publish a "HFT tuning guide" is documentation-side; the engine surface is locked. | DEFER to docs (`docs/perf-tuning.md`). | post-v1 docs |
| 5 | **TLS bidi shutdown timeout default — 1 s tight enough?** §4.1's `close()` documents a 1-second `tls_close_timeout` default. Real-world TLS counterparties sometimes drag close-notify by tens of seconds (especially behind an unresponsive load balancer). Whether to raise the default is operationally driven. | DEFER to operational bench-time tuning at v1.x. | post-v1 follow-up |
| 6 | **IPv6 zone-id support — needed in v1.0?** The `Endpoint::host` field accepts `host%zone` strings; ASIO's resolver handles them. Whether the v1.0 conformance corpus covers IPv6 link-local is a separate question. | DEFER to conformance-corpus expansion at Phase 4. | Phase-4 session-module spec |
| 7 | **`Listener::async_accept` cancellation under high accept rates.** Backlog overflow + cancellation propagation under 10⁵-accepts/sec is not benched in v1.0. Acceptable for v1.0 (target deployments are 10²–10³ sessions per acceptor). | DEFER to post-v1 if a venue requires it. | post-v1 follow-up |

---

## §11 Hand-off

**Docs unblocked by 2h sign-off (downstream):**

- **Session-module Phase-4 spec** — needs the `Transport` interface (§4.1), the `TlsTransport` sub-interface (§4.2), the `handshake_result` value-typed return from `async_handshake` for T-041 binding (the FSM reads `handshake_result.peer_id` directly), the `ReconnectPolicy` value type (§4.4) + normative `defaults()` (§4.4.1), the durable-before-transmit invariant operationalisation (§6.7), and the per-mode cancellation effect table (§6.4.1). Without 2h, the session-module Phase-4 FSM cannot be drafted.
- **2j (control plane)** — needs the `Transport` graceful-close shape (§6.4) for the `CloseSession` RPC and the cert-source reload trigger (close-and-reopen pattern per `[arch §5.6]`) for the `ReloadCertSource` RPC. The `Pinset::add` / `Pinset::remove` thread-safety contract is delivered by 2g; 2h is the wiring layer.
- **2k (log + otel)** — needs the TLS event call sites (§7.7) to define the structured-log / OTel span schemas. Cert-event spans are owned by 2g per `[2g §7.8]`; transport-level spans (handshake-start / handshake-success / handshake-failure / reconnect-attempt) are owned by 2k with 2h providing the call sites.
- **2i (C ABI)** — needs the C++ shape inventory (§5) for `fixpp_transport_t`, `fixpp_tls_transport_t`, `fixpp_listener_t`, `fixpp_transport_factory_t`, the PoD types, and `FIXPP_ERR_TRANSPORT_*` coalescing groups.

**Cross-doc amendments declared at sign-off (orchestrator applies, per `[2c App D]` / `[2d App D]` / `[2e App D]` / `[2f App D]` / `[2g App D]` precedent — the rewrite agent does NOT edit sibling docs in this draft):**

- **Appendix D §D.1** — `[2d §4.4]` `EngineConfig::default_transport_factory` field: change `std::shared_ptr<TransportFactory>` to `std::unique_ptr<TransportFactory>` per `[arch §5.6]` frozen-at-open + the `[2e §4.4]` precedent for `MessageStoreFactory`. Sibling-doc inconsistency flagged in §3.11.
- **Appendix D §D.2** — `[2d §4.5]` `SessionConfig` field-list: append `std::unique_ptr<fixpp::transport::TransportFactory> transport_factory_override;` with the `[arch §5.6]` engine-anchor + session-`*_override` comment.
- **Appendix D §D.3** — `library/spec/coverage-index.md` §"FIXS RC1": **orchestrator MUST apply `[2g App D §D.3]` at or before 2h sign-off** (round-2 / v0.3 honesty fix per Codex P1-1 / Opus P1-1 — the live `coverage-index.md` lines 155 / 159 / 162 still read `MISSING → row added (T-039)` / `MISSING → row added (T-040)` / `MISSING → row added (T-041)` as of v0.3 authoring; 2g v0.4's Appendix-D drop-in language was recorded but the orchestrator's mechanical-apply step has not produced the claimed text in the live source). 2h's sign-off carries this orchestrator dependency forward; no 2h-owned drop-in is queued because that would duplicate the upstream owner's `[2g App D §D.3]`. T-039 / T-040 / T-041 catalogue-ID-level cross-cuts are traced via this doc's Appendix A.2 cross-cut rows independently of the Gap-note column state.

(2h does NOT edit `library/spec/feature-catalogue.md`, `library/spec/coverage-index.md`, `architecture.md`, `2d-threading.md`, or any signed-off sibling doc directly per the brief's hard rule; the drop-ins are recorded in Appendix D verbatim for the orchestrator.)

---

## Appendix A — Catalogue row coverage

Per `[const §VI.5]` exact-coverage rule and the per-doc precedent in `[2b Appendix A]`, `[2c Appendix A]`, `[2d Appendix A]`, `[2e Appendix A]`, `[2f Appendix A]`, `[2g Appendix A]`.

### A.1 Owned (sole)

| Row | Family | Catalogue text (verbatim from `library/spec/feature-catalogue.md`) | What 2h covers | 2h §/§§ |
|---|---|---|---|---|
| **T-001** | OFFICIAL — transport | "TCP transport — initiator and acceptor roles" — `[FIX-SL §4.3.1]` Transport layer requirements | The `Transport` interface + `asio_tls_transport` + `Listener` + `asio_listener` cover both initiator (client `async_connect`) and acceptor (server `Listener::async_accept`) roles. | §4.1, §4.5, §4.6 |
| **T-002** | OFFICIAL — transport | "TLS over TCP — OpenSSL on both Linux and Windows (Schannel dropped; ASIO has no Schannel backend)" — `[FIX-SL §4.3.1]` + `[FIXS §1.1]` Scope | The `TlsTransport` sub-interface + `asio_tls_transport`'s `asio::ssl::stream<tcp::socket>` over OpenSSL per `[const §XII.1]`. Schannel dropped per `[const §XII.1]`. | §4.2, §4.5 |
| **T-003** | OFFICIAL — transport | "ASIO async I/O layer — non-blocking read/write with back-pressure" — `[impl]` implementation | `Transport::async_read_some` + `Transport::async_write` over `asio::ip::tcp::socket` / `asio::ssl::stream`; back-pressure via session strand depth-1 queue per §1.1 / §6.8. | §4.1, §6.8 |
| **T-004** | OFFICIAL — transport | "Reconnect / exponential back-off — initiator retry on disconnect" — `[FIX-SL §4.3.1]` Transport layer requirements | `ReconnectPolicy` value type (§4.4) + `delay_for_attempt(...)` schedule helper. The session-FSM-side reconnect loop is owned by the Phase-4 session-module spec; 2h owns the value type. | §4.4, §7.2 |
| **T-005** | OFFICIAL — transport | "Multi-session TCP acceptor — accept multiple connections on one port" — `[FIX-SL §4.3.1]` Transport layer requirements | `Listener` interface + `asio_listener` default impl wrapping `asio::ip::tcp::acceptor`. Each accepted connection mints a fresh `Transport`. | §4.6 |
| **T-009** | OFFICIAL — transport | "FIXS: Mutual TLS — CA pinning (server) + leaf pinning (client)" — `[FIXS §2.4]` Certificate Validation with CA Pinning | The `SecurityProfile::mtls_ca` + `SecurityProfile::mtls_pinned` configurations driven by `[2g §4.5.1]` normative table; 2h wires `SSL_CTX_set_verify` per row. (T-008 / T-011 — leaf pinning + pinset rotation — are owned by 2g; T-009 is the `mtls_*` mode discharge from the transport side.) | §4.2, §4.5, §6.2 |
| **T-010** | OFFICIAL — transport | "FIXS: Simple TLS — server-only auth (Star topology; client auth deferred to FIXA/FIX session)" — `[FIXS §2.2]` Mutual and Simple TLS protocol options | The `SecurityProfile::one_way_ca` configuration (server-cert-only TLS) per `[2g §4.5.1]` normative table; 2h wires `SSL_CTX_set_verify(SSL_VERIFY_PEER)` (no `SSL_VERIFY_FAIL_IF_NO_PEER_CERT`) per row. | §4.2, §4.5 |
| **T-012** | OFFICIAL — transport (P2 — DEFERRED) | "FIXS: PSK authentication — pre-shared key (P2P; optional; 32-char min; out-of-band exchange)" — `[FIXS §2.5]` Pre-shared keys (PSKs) | **P2 deferred to post-v1** per `[const §XII.6]`. The v1.0 default `asio_tls_transport` rejects PSK config with `transport_psk_unsupported`. The `TlsTransport` sub-interface leaves 4 of 5 pure-virtual slots free for a future PSK callback hook. The catalogue row remains backlog; the placeholder shape is documented in §10 Q1. | §4.2 (sub-interface headroom), §6.6 (`transport_psk_unsupported` variant), §10 Q1 |

### A.2 Cross-cuts (partitioned with 2g and the session-module Phase-4 spec)

| Row | Family | Catalogue text (verbatim) | Partition declared in this doc | Side owned by 2h | Side owned elsewhere |
|---|---|---|---|---|---|
| **T-039** | OFFICIAL — transport | "FIXS: Certificate parameters — RSA 2048-bit min, ECDSA 256-bit, X.509 v2/v3, expiration validation at handshake" — `[FIXS §3.4]` Certificate parameters | §7.3 + `[2g §7.1]` | The OpenSSL `SSL_VERIFY_PEER` callback wiring on `SSL_CTX` that calls `[2g §4.5]`'s `verify_peer` predicate. The `verify_peer_trampoline` extracts the peer chain from `X509_STORE_CTX*` and dispatches into 2g's policy core. | The `verify_peer` predicate itself (RSA 2048 lower bound + 8192 upper bound, ECDSA P-256/P-384, X.509 v2/v3, expiration via `cfg.clock->now()`, DoS bounds) — owned by **2g** per `[2g §4.5]` / `[2g §7.1]`. |
| **T-040** | OFFICIAL — transport | "FIXS: Secrets management — distribute private keys/PSKs/pinned-certs via approved channels (HTTPS, GnuPG, PKCS#12, postal, in-person); store securely; support rotation" — `[FIXS §4.1]` Sharing secrets | §7.3 + `[2g §7.1]` | Informational cross-cut: 2h consumes `cert_source::load_credentials()` + `load_trust_anchors()` only; 2h does NOT touch the secret-distribution channel directly. Once credentials are loaded by 2g, 2h binds the `EVP_PKEY*` (software_key_ref path) or wires the awaitable signing callback (`async_signer_ref` path) into the OpenSSL `SSL_CTX`. | The `cert_source` interface, the `file_cert_source` default impl, the storage-security policy — owned by **2g** per `[2g §4.1]` / `[2g §4.2]`. |
| **T-041** | OFFICIAL — transport | "FIXS: Authorization linked to authentication — authenticated TLS identity must map to authorized FIX CompID; per-counterparty TLS tunnel" — `[FIXS §4.4]` Authorization linked to authentication | §7.2 + `[2g §7.2]` | The `handshake_result.peer_id` value-typed return from `TlsTransport::async_handshake` (§4.2) that delivers the negotiated peer identity (parsed from the verified peer cert by 2g's `verify_peer`) to the session-module Phase-4 spec by value. 2h is the wiring layer. | The CompID-to-TLS-identity binding policy and the `error::session_identity_mismatch` rejection — owned by the **session-module Phase-4 spec**. The parsed `peer_identity` value type and `verify_peer` predicate are owned by **2g** per `[2g §4.5]`. |

---

## Appendix B — Normative References

Per `[const §VI.5]` exact-coverage rule. Format `[DocAbbrev §X.Y.Z] Section title` per `[const §VI.2]`, drawn from `library/spec/coverage-index.md`.

### B.1 Coverage-index normative references (consumed by 2h)

| Spec area | Normative reference | 2h impact |
|---|---|---|
| Transport layer requirements (TCP/IP, FIXS mandatory) | `[FIX-SL §4.3.1]` Transport layer requirements | §4.1 (Transport interface), §4.5 (asio_tls_transport), §4.6 (Listener); T-001, T-002, T-004, T-005 |
| Heartbeat (keep-alive) | `[FIX-SL §4.5.1]` FIX connection keep-alive (heartbeat) | §1.1 — TCP keepalive is defence-in-depth; FIX Heartbeat is primary (owned by Phase-4 session-module spec); 2h provides the `tcp_keepalive` knob only |
| Recovery | `[FIX-SL §4.5.2]` Message recovery | §6.7 — durable-before-transmit invariant + ResendRequest honourability after `transport_write_cancelled` |
| TLS scope | `[FIXS §1.1]` Scope | §4.2, §4.5; T-002 |
| Mutual / Simple TLS | `[FIXS §2.2]` Mutual and Simple TLS protocol options | §4.2, §4.5; T-009, T-010 |
| Certificate validation with CA Pinning | `[FIXS §2.4]` Certificate Validation with CA Pinning | §4.5, §6.2; T-009 |
| Pre-shared keys | `[FIXS §2.5]` Pre-shared keys (PSKs) | §4.2 (sub-interface headroom), §6.6 (`transport_psk_unsupported`); T-012 (P2 deferred) |
| TLS protocol version | `[FIXS §3.1]` Protocol version | §4.5 — `SSL_CTX_set_min_proto_version(TLS1_2_VERSION)` per `[const §XII.2]` |
| TLS protocol features (compression, renegotiation, session caching) | `[FIXS §3.2]` Protocol features | §4.5 — `SSL_CTX_set_options(SSL_OP_NO_RENEGOTIATION | SSL_OP_NO_COMPRESSION | SSL_OP_NO_TICKET | SSL_OP_NO_EARLY_DATA)` |
| Cipher suites | `[FIXS §3.3]` Cipher suites | §4.5 — consumes `[2g §4.4]` `CipherPolicy`; T-013 (owned by 2g) |
| Certificate parameters | `[FIXS §3.4]` Certificate parameters | §4.5, §7.3; T-039 (cross-cut with 2g per §7.3) |
| Sharing secrets | `[FIXS §4.1]` Sharing secrets | §7.3 (informational cross-cut); T-040 (owned by 2g) |
| Authorization linked to authentication | `[FIXS §4.4]` Authorization linked to authentication | §4.2 (`handshake_result.peer_id` returned by `async_handshake`), §7.2; T-041 (cross-cut with session-module Phase-4 spec) |

### B.2 Constitutional clauses cited inline at point of use (exact, per `[const §VI.5]`)

`[const §I.2]` (in-process C++23 primary);
`[const §II.2]` (no clang-cl on Windows);
`[const §IV.2]` (C ABI as legal isolation seam);
`[const §VI.5]` (exact-citation rule);
`[const §VII]` (testing — every plugin needs a mock + test seam);
`[const §VII.1]` (GoogleTest/GoogleMock);
`[const §VII.4]` (no untested code);
`[const §VII.6]` (interop testing — TC-001..TC-017);
`[const §VII.7]` (parser-touching modules need fuzz; transport read-path is parser-adjacent);
`[const §VIII.1]` (perf-sensitive modules need benchmarks);
`[const §VIII.2]` (perf regression budgets);
`[const §VIII.3]` (perf bench frameworks);
`[const §VIII.5]` (zero allocation between parse and `fromApp`, extended to read-completion-handler dispatch);
`[const §X.4]` (out-of-range C-ABI code mapping);
`[const §X.5]` (C-ABI thread-safety annotations);
`[const §XI.1]` (`asio::awaitable<T>` composition);
`[const §XI.2]` (ASIO native cancellation slots);
`[const §XI.3]` (awaitable mutex required in coroutine context — recorded; 2h does not consume directly);
`[const §XI.4]` (per-session strand default);
`[const §XI.6]` (HALO-first frame allocation; PMR fallback);
`[const §XII.1]` (OpenSSL on both Linux and Windows; Schannel dropped);
`[const §XII.2]` (TLS 1.2 / 1.3 only);
`[const §XII.3]` (compile-time cipher allow-list; banned 0-RTT);
`[const §XII.5]` (`SecurityProfile` no-implicit-default);
`[const §XII.6]` (PSK authentication carve-out — T-012 P2);
`[const §XII.7]` (banned application-layer encryption — recorded by 2h; rejection wiring is the wire validator's responsibility);
`[const §XIII.3]` (strand-stored trace context — log records carry trace_id);
`[const §XIV.1]` (v1.0 pluggable interfaces — Transport);
`[const §XIV.2]` (≤5 pure-virtual on plugin interfaces — Transport at 5/5; TlsTransport sub-interface at 1/5);
`[const §XIV.4]` (no `dlopen` plugin loading);
`[const §XV.2]` (no thread-per-session blocking I/O — ASIO async only);
`[const §XV.10]` (banned application-layer encryption — recorded for completeness);
`[const §XV.15]` (banned `drop-oldest` on app/session message paths);
`[const §XVII.1]` (Codex Gate A required for Phase 2 design docs);
`[const §XVIII.2]` (FIXP / SBE / FAST / SOFH out of v1.0).

### B.3 Architectural sections cited (exact, per `[const §VI.5]`)

`[arch §1.1]` (Goals — pluggable I/O for testability);
`[arch §1.2]` (non-goals — no SHM/DPDK/Onload in v1.0);
`[arch §2.3]` (allowed include edges — `transport/` may include from `core/`, `tls/`, `log/` interfaces only);
`[arch §3]` (public namespaces — `fixpp::transport`);
`[arch §4.4]` (session module surface — recipient of transport callbacks);
`[arch §4.5]` (transport module surface — the spine of this doc);
`[arch §4.6]` (tls module surface — consumed by the TLS sub-interface);
`[arch §4.10]` (capi surface delegation);
`[arch §4.11]` (service module surface — Listeners flow through service/);
`[arch §5.1]` (executor model);
`[arch §5.2]` (allocator policy — PMR);
`[arch §5.3]` (error model — `expected_t` hot path);
`[arch §5.5]` (lifetime model — `[[clang::lifetimebound]]`);
`[arch §5.6]` (frozen-config rule — Transport factory frozen at session open);
`[arch §5.7]` (logging hook);
`[arch §5.8]` (backpressure — `block` / `disconnect_and_recover` only);
`[arch §6]` (plugin pattern — five rules);
`[arch §8.1]` (control plane — gRPC schema location);
`[arch §10]` row 2h (handoff: "Transport interface — ≤5 pure-virtual surface, ASIO TCP/TLS default impl, mock seam");
`[arch Appendix B]` (FIX-SL / FIXS / FIXT row mappings).

### B.4 SYNTHESIS Q-IDs cited (exact)

`[SYN §3.2 Q6]` (HALO firing on inbound dispatch — relevant to read-path latency; verify-by-spike, owned by 2d/2f);
`[SYN §3.4 Q16]` (transport interface — small surface, ASIO TCP/TLS default — DECIDED — drives §1, §4).

### B.5 Sibling-doc citations (exact, per `[const §VI.5]`)

`[2a §4.2]` (`trap_throw` for routing PMR throws to `expected_t`);
`[2a §6.5]` (per-doc Tier 1 ceiling table precedent);
`[2b §4.2]` (`Framer::feed` — read-path delivery shape);
`[2b §4.5]` (`Writer::commit` — write-path source of truth);
`[2b §6.4]` (declaration-site lifetime annotation precedent);
`[2b §6.6]` (allocation discipline; three-arena PMR pinning; framer-carry-arena);
`[2c §4.8]` (`owning_message_t<>` precedent for owning-value types crossing strand boundaries);
`[2d §4.4]` (`EngineConfig::default_transport_factory` — Appendix D §D.1 amends);
`[2d §4.5]` (`SessionConfig` field shape — Appendix D §D.2 amends to add `transport_factory_override`);
`[2d §4.7]` (cancellation propagation API — two-phase close);
`[2d §4.8]` (`session_executor` — project-owned wrapper class);
`[2d §6.5]` (`cancellable_dispatch` recipe);
`[2d §6.7]` (per-doc-prefix `FIXPP_ERR_THREAD_*` discipline; cancellation group);
`[2d §7.5]` (TLS strand-safety boundary — mid-handshake rotation does not affect in-flight handshake);
`[2d §7.6]` (transport ops on session strand — locked contract surface);
`[2d §7.9]` (`effective_clock` for any timestamp the transport emits);
`[2e §4.4]` (`MessageStoreFactory` factory ownership = `unique_ptr` — precedent for `TransportFactory` ownership);
`[2e §6.1.4]` (durable-before-transmit invariant);
`[2e §6.4]` (writer-mutex contract precedent);
`[2f §4.1.1]` (`async_mutex::async_lock(...)` awaitable shape — referenced for awaitable-API conventions, not consumed);
`[2f §6.5]` (per-doc-prefix discipline + cancellation-group precedent);
`[2g §1]` (Goals 1, 2, 3, 4, 5, 7, 8, 9, 10 — TLS policy core);
`[2g §1.1]` (TLS DoS caps);
`[2g §3.7]` (two-phase close interaction with TLS);
`[2g §4.1]` (`cert_source` interface — 2 pure-virtual);
`[2g §4.2]` (`file_cert_source` + `make_file_cert_source` factory);
`[2g §4.3]` (`Pinset` — add-then-remove rotation; `snapshot()` reader path);
`[2g §4.4]` (`CipherPolicy` allow-list + `is_allowed(...)`);
`[2g §4.5]` (`SecurityProfile` enum + `SslCtxConfig` adapter + `verify_peer` + `peer_identity`);
`[2g §4.5.1]` (normative `SecurityProfile`-to-OpenSSL-mode mapping table — binding for 2h's wiring);
`[2g §4.6]` (construction / lifetime / ownership rules — `SessionConfig::pinset` reachability);
`[2g §6.1]` (TLS hot-path allocation discipline);
`[2g §6.4]` (cancellable_dispatch recipe for `load_credentials` — precedent for §6.5);
`[2g §6.5]` / `[2g §6.5.1]` (FIXS rotation invariants; handshake-time `Pinset::snapshot()` captured-once contract — binding for §6.2);
`[2g §6.6]` (TLS error variants — `tls_handshake_failed` group);
`[2g §7.1]` (T-039 / T-040 partition declaration — binding for §7.3 + Appendix A);
`[2g §7.2]` (T-041 cross-cut declaration — binding for §7.2 + Appendix A);
`[2g §7.7]` (drop-in for 2j control-plane reload — informational);
`[2g §7.8]` (drop-in for 2k OTel cert-event spans — call-site cross-reference).

Engineering-judgment decisions whose primary driver is engineering judgment rather than a specific spec section — the precise field list of `Transport::Config`, the `ReconnectPolicy::defaults()` numeric ceiling (100 ms / 30 s / 2.0× / 10 attempts / ±10% per §4.4.1), the `SO_LINGER` / `TCP_NODELAY` defaults, the Tier 1 latency ceilings in §6.3, the DoS caps in §1.1 (256 KiB read window / 1 MiB write size), the `mock_transport` `Script` shape, the `Listener` 1-pure-virtual interface — cite `[const §X.y]` / `[arch §X.y]` / `[SYN §3.x Q#]` / `[2X §X.y]` inline at point of use; they are not spec normatives and are intentionally listed as design-constraint references rather than coverage-index normatives in §B.1.

---

## Appendix C — Convergence log

### Round 1 (Phase A): v0.1 → v0.2 (2026-05-09)

**Phase A round 1 designation.** First / only convergence pass for Phase A so far. No reset has been used; the round-cap budget remains 3 rounds with up to 1 full-rewrite reset per the `/gate-a` 3-phase A/B/C convention.

**Reviews input:**
- Codex Gate A (Phase A round 1; tally P1=2, P2=5, P3=4): `research/reviews/codex_2h_transport_review.md`
- Opus adversarial review (Phase A round 1; **post-judging combined tally 4 P1 / 6 P2 / 5 P3**; **3 root causes**; closing recommendation: **"v0.2 can ship after a single convergence pass"**): `research/reviews/opus_2h_transport_adversarial_review.md`

**Closing recommendation followed:** "v0.2 can ship after a single convergence pass."

**Root causes addressed (Opus, source of truth):**

- **RC#1 — Appendix D byte-faithfulness postponed AND the chosen "Before" content drifts against the post-2e-sign-off `[2d §4.5]` baseline.** Clusters Codex P1 #1 (Appendix D §D.1 / §D.2 deferred), Opus N-P1-1 (Before block content must match the post-2e-sign-off `[2d §4.5]` line 534 baseline where `store_factory` is `std::unique_ptr<MessageStoreFactory>` per `[2e App D §D.1]`), Codex P3 #10 (coverage-index "no edit needed" claim vs live state). **Single fix.** Author the complete Appendix D in `[2g App D]` exact-text format with byte-faithful "Before" blocks against the live `2d-threading.md` v0.4 source: §D.1 quotes line 448 verbatim (`shared_ptr<TransportFactory> default_transport_factory;`) and flips it to `unique_ptr`; §D.2 quotes lines 533–535 (the post-2e-sign-off plugin-overrides block in its three-line shape — `pinset` has not been applied to 2d at v0.2 authoring time per the `grep -n "pinset" 2d-threading.md` audit) and appends one new line for `transport_factory_override`; §D.3 cross-references `[2g App D §D.3]` (signed off at 2g v0.4) and confirms no further edit is owed at 2h sign-off because 2g closed the §3.4 / §4.1 / §4.4 row Gap-notes already. Choice over alternatives: only one option exists for byte-faithfulness — quote the live source verbatim. The `[2g App D]` exact-text precedent is binding (`[const §VI.5]`); paraphrasing is ruled out.

- **RC#2 — `TlsTransport` sub-interface design: pure-virtual mis-count + multi-site `dynamic_cast` + RTTI question + plain-TCP-via-empty-config narrative are one design decision.** Clusters Codex P2 #3 (escalated to P1 by Opus; pure-virtual count "1 of 5" prose vs 4-pure-virtual .hpp), Codex P2 #4 (RTTI dependency not flagged), Codex P2 #5 (plain-TCP-via-empty-`SslCtxConfig` contradicts the partition), Opus N-P1-2 (`dynamic_cast` "exactly two sites" claim wrong against the 4-accessor surface). **Single fix.** Pick option (b) per Opus's recommendation: fold the three accessor pure-virtuals (`peer_identity_view`, `captured_pinset_snapshot`, `negotiated_cipher_suite`) into a value-typed `handshake_result` POD returned by `async_handshake`. Concretely: §4.2 redefined — `async_handshake(...) -> awaitable<expected_t<handshake_result>>` is the only pure-virtual; `handshake_result` carries `peer_identity peer_id` (OWNING per `[2g §4.5]` precedent) + `shared_ptr<const pin_snapshot> captured_pinset` + `pmr::string negotiated_cipher`. §1 Goal 2 / §1.2 / §4.2 / §4.5 / §4.8 / §4.9 / §6.3 prose realigned: `TlsTransport` uses 1 of 5 pure-virtual slots (4 slots of headroom for PSK/renegotiation/early-data); the `dynamic_cast<TlsTransport*>` count drops from "two sites" (which was wrong; was actually four) to **exactly one** (handshake issue at session open; result stored as typed pointer); RTTI is opportunistic, not structural. §4.5's empty-`SslCtxConfig`-no-op narrative is dropped (v1.0 ships only TLS-capable Transports per `[FIXS §1.1]` / `[FIX-SL §4.3.1]`). §6.3 accessor latency rows dropped (no virtual dispatch = no row). Choice over alternatives: option (a) "admit 4/5 used, 1 slot remaining" is also valid as a line-edit but loses the headroom story §1 carries; option (b) recovers the headroom story AND eliminates the multi-site dynamic_cast question with a single structural change — two RC#2 findings closed with one fix. Codex P3 #9 (`SslCtxConfig::pinset_snapshot` field does not exist) is folded in: the v0.1 `async_handshake` doc-comment claim that the verify callback references "`SslCtxConfig::pinset_snapshot`" is rewritten to match `[2g §4.5]`'s actual surface (no such field; the snapshot is captured inside the transport at handshake start and is carried in the returned `handshake_result`).

- **RC#3 — "By strand discipline" assumptions never promoted to API-level contracts: in-flight overlap + reconnect cap + per-mode cancellation effect table.** Clusters Codex P1 #2 (in-flight overlap exclusivity not contractual), Opus N-P1-3 (reconnect-policy `max_attempts = 0` thunder-herd violation), Opus N-P2-1 (per-mode cancellation effect table absent for `async_handshake` / `async_accept` / `async_connect`). **Single fix.** Promote three implicit assumptions to explicit API-level contracts: (1) §4.1 — new normative paragraph: "At most one in-flight `async_read_some` and at most one in-flight `async_write` per Transport instance; concurrent calls complete with `transport_read_in_progress` / `transport_write_in_progress`; strand serialisation is defence-in-depth, the API-level contract is binding"; +2 error variants in §6.6; +1 §9 seam (#15). `async_connect` and `async_handshake` are one-shot per Transport (overlap raises `transport_already_connected`). (2) §4.4.1 — new normative `ReconnectPolicy::defaults()` returning `{100 ms, 30 s, 2.0×, max_attempts = 10, ±10%}`; +1 error variant `transport_reconnect_limit_exceeded`; §10 Q3 disposition flips to **DECIDED**; §1.1 last bullet rewritten with the rationale (the v0.1 default was a `[const §XV]` thunder-herd-pattern violation). Choice over alternatives: type-system enforcement of in-flight exclusivity (e.g., `async_write` consumes a write_token issued at one-per-strand) was considered but adds API surface for marginal safety gain over a runtime check; the `transport_*_in_progress` variants are the cheapest API-level contract that closes the FSM-bug-class window. (3) §6.4.1 — new per-mode cancellation effect table (5 rows × 3 columns) for `async_connect` / `async_read_some` / `async_write` / `async_handshake` / `async_accept` extending `[2d §4.7]`'s parent table; specifies `async_handshake` graceful phase 1 runs to completion (OpenSSL handshake state cannot be partially rolled back), graceful phase 2 cancels with `transport_handshake_cancelled`; `async_accept` is engine-scoped and does NOT respond to per-session graceful close.

**Per-finding resolution table:**

| Finding | Severity (after Opus judging) | Resolution | Section(s) edited |
|---|---|---|---|
| Codex P1 #1 — §D.1 byte-faithfulness failure (Appendix D postponed) | P1 (confirmed) | RC#1: Appendix D fully populated in `[2g App D]` exact-text format; §D.1 byte-faithful against `2d-threading.md` line 448. | Appendix D §D.1, §11 |
| Codex P1 #2 — in-flight operation overlap not contractually forbidden | P1 (confirmed) | RC#3: §4.1 normative in-flight exclusivity paragraph + 2 new error variants (`transport_read_in_progress`, `transport_write_in_progress`) + §9 seam #15. | §4.1, §6.6, §9 seam #15 |
| Codex P2 #3 — `TlsTransport` pure-virtual mis-count and headroom contradiction | **P1 (escalated by Opus)** — RC#2 cluster | RC#2: §4.2 reshape — single pure-virtual `async_handshake` returning `handshake_result` value type; the v0.1 trio of accessor pure-virtuals dropped. Sub-interface drops to 1 of 5 (4 slots of headroom restored). | §1, §1.2, §4.2, §4.5, §4.8, §4.9, §6.3 |
| Codex P2 #4 — RTTI dependency via `dynamic_cast<TlsTransport*>` not declared | P2 (confirmed) | RC#2: dynamic_cast count drops from "two sites" (wrong; was four) to exactly one site (handshake issue); cast result stored as typed pointer on Session; RTTI opportunistic not structural; v1.0 ships RTTI-on per Linux/Clang + Windows/MSVC defaults; future `-fno-rtti` falls back to `as_tls()` non-virtual member (post-v1). | §4.2, §4.9 |
| Codex P2 #5 — "Plain TCP via empty SslCtxConfig" contradicts the partition | P2 (confirmed) | RC#2: §4.5 lines 597–608's empty-`SslCtxConfig`-no-op narrative dropped; v1.0 ships only TLS-capable Transports per `[FIXS §1.1]` + `[FIX-SL §4.3.1]` deployment reality. | §4.5 |
| Codex P2 #6 — `TransportFactory::make` is not `noexcept` | P2 (confirmed) | §4.7 — `make(...) noexcept = 0` per `[2e §4.4]` / `[2g §4.2]` precedent; impls trap internal throws via `[2a §4.2]` `trap_throw` and surface `transport_factory_failed`. Same for the default `asio_tls_transport_factory::make` override. | §4.7 |
| Codex P3 #7 — `Endpoint` PMR language self-contradicts | P3 (confirmed) | §4.3 inline-comment claim "PMR-aware overload is available" retired; struct shape is the v1.0 surface; HFT users layer `pmr_endpoint` post-v1. | §4.3 |
| Codex P3 #8 — `make_asio_tls_transport` resolver-factory comment vs signature | P3 (confirmed) | §4.5 factory comment rewritten — the "resolver factory" mention is dropped; v1.0 owns DNS resolution inside `asio_tls_transport` via `asio::ip::resolver` constructed from `exec`. | §4.5 |
| Codex P3 #9 — `SslCtxConfig::pinset_snapshot` field does not exist | P3 (confirmed) | RC#2 fold: §4.2 `async_handshake` doc-comment rewritten to match `[2g §4.5]`'s actual surface — the snapshot is captured inside the transport at handshake start (per `[2g §6.5.1]` capture-once) and is carried in the returned `handshake_result.captured_pinset`; `SslCtxConfig` carries no `pinset_snapshot` field. | §4.2 |
| Codex P3 #10 — Coverage-index "no edit needed" claim vs live state | P3 (confirmed; subsumed by Opus N-P1-1) | RC#1: Appendix D §D.3 rewritten as a cross-reference confirmation pointing at `[2g App D §D.3]` (signed off at 2g v0.4); the §3.4 / §4.1 / §4.4 row Gap-notes are already updated; T-039 / T-040 / T-041 catalogue-ID-level cross-cuts traced via this doc's Appendix A.2. | §11, Appendix D §D.3 |
| Opus N-P1-1 — Appendix D §D.2 "Before" block must match the post-2e-sign-off `[2d §4.5]` baseline (lines 533–535) | P1 (NEW; RC#1 cluster) | RC#1: §D.2 "Before" block is the byte-faithful three-line block (post-2e-sign-off; `store_factory` as `unique_ptr` per `[2e App D §D.1]`; `pinset` not yet applied per the live-source audit at v0.2 authoring time); "After" block appends one new line for `transport_factory_override`. | Appendix D §D.2 |
| Opus N-P1-2 — `dynamic_cast` count claim "exactly two sites" wrong against 4-accessor surface; RTTI dependence becomes structural without the concept-vs-virtual escape | P1 (NEW; RC#2 cluster) | RC#2 single fix: option (b) — value-typed `handshake_result` return collapses the cast surface to exactly 1 site (handshake issue) and eliminates the accessor-driven multi-site count. | §1, §4.2, §4.5, §4.9 |
| Opus N-P1-3 — Reconnect-policy `max_attempts = 0` (unbounded) is a `[const §XV]` thunder-herd-pattern violation | P1 (NEW; RC#3 cluster) | RC#3: §4.4.1 normative `ReconnectPolicy::defaults()` returning `max_attempts = 10` (numeric ceiling per `[const §XV]` thunder-herd-pattern intent + `[2g §1.1]` DoS-cap precedent); +1 error variant `transport_reconnect_limit_exceeded`; §10 Q3 DECIDED; §1.1 last bullet rewritten. | §1.1, §4.4, §4.4.1, §6.6, §10 Q3 |
| Opus N-P2-1 — Per-mode cancellation effect table missing; 2d §4.7 does not enumerate `async_handshake` / `async_accept` | P2 (NEW; RC#3 cluster) | RC#3: §6.4.1 new per-mode effect table (5 rows × 3 columns) for the 2h-owned ops, extending `[2d §4.7]`. | §6.4.1 |
| Opus N-P2-2 — `peer_identity_view()` returns `expected_t<peer_identity const&>` (reference-typed expected fragility + awkward consumer expression) | P2 (NEW; RC#2 fold) | RC#2 single fix: the `expected_t<peer_identity const&>` accessor is dropped from the published surface; `peer_identity` is returned by value inside `handshake_result.peer_id` from `async_handshake`. The reference-typed-`expected_t` fragility is eliminated structurally. | §4.2 |
| Opus N-P3-1 — §6.6 error-variant count drift (paragraph "18" vs table 20 vs parenthetical "20") | P3 (NEW) | §6.6 paragraph aligned to `22 variants` (v0.2 net count); parenthetical rewritten to enumerate the v0.2 axes (`transport_read_in_progress` / `transport_write_in_progress` / `transport_reconnect_limit_exceeded` plus the v0.1 base set minus the dropped `transport_handshake_not_complete`). | §6.6 |
| Opus N-P3-2 — §6.3 row 7 latency rationale cite mis-aligned (cites `[2g §6.3] snapshot_acquire` for a non-atomic `shared_ptr` copy) | P3 (NEW; subsumed by RC#2) | RC#2 fold: §6.3 row 7 (`captured_pinset_snapshot` accessor) and rows 6 / 8 (`peer_identity_view`, `negotiated_cipher_suite`) are dropped — the accessors are no longer pure-virtual operations on `TlsTransport` (the artefacts are read from the FSM-held `handshake_result` value with no virtual dispatch). The mis-aligned `[2g §6.3]` cite disappears with the row. | §6.3 |
| Opus N-P3-3 — `Endpoint::is_initiator_shape()` heuristic footgun (false positive when acceptor explicitly sets backlog = 128) | P3 (NEW) | §4.3 — `is_initiator_shape()` accessor dropped; the type-system distinguishes initiator (`Endpoint` → `Transport::async_connect`) vs acceptor (`Endpoint` → `asio_listener::Config::bind_endpoint`) at the construction-path level. | §4.3 |

**Codex findings disagreed with — none.** Every Codex finding (2 P1 / 5 P2 / 4 P3) was judged by Opus as either confirmed at the rated severity or escalated (Codex P2 #3 → P1 by Opus). No Codex finding was judged "Disagree" by Opus; no Codex counter-proposal was rejected.

**Net-effect summary:** v0.2 lands the v1.0 spine intact (5-pure-virtual `Transport` AT the `[const §XIV.2]` cap; encryption-agnostic-base + `TlsTransport` sub-interface split per `[2g §7.1]` partition; `Endpoint` / `ReconnectPolicy` POD value types; `Listener` discharging T-005 with one pure-virtual; `mock_transport` discharging `[const §VII]` / `[arch §1.1]`; durable-before-transmit invariant per `[2e §6.1.4]`; two-phase close mirroring `[2d §4.7]`; T-039 / T-040 / T-041 partition reciprocal with `[2g §7.1]` / `[2g §7.2]`) and converges every finding through three root-cause-driven structural changes plus line-edits. **Net effect:** **+1 test seam** (14 → 15; new: #15 `in-flight exclusivity`); **+2 error variants net** (20 → 22; new: `transport_read_in_progress`, `transport_write_in_progress`, `transport_reconnect_limit_exceeded`; dropped: `transport_handshake_not_complete` because the accessor pure-virtuals it protected are gone); **−3 pure-virtual on `TlsTransport`** (counting accessors strictly: v0.1 = 4 → v0.2 = 1; the v0.1 trio of accessor pure-virtuals collapses into the value-typed `handshake_result` return); **±0 pure-virtual on `Transport`** (still 5 of 5 AT the cap); **−3 `dynamic_cast` sites** (v0.1 prose claimed "exactly two" but the actual surface had four; v0.2 surface has exactly one — handshake issue; cast result stored as typed pointer); **+1 normative §4.4.1 `ReconnectPolicy::defaults()` numeric ceiling**; **+1 normative §6.4.1 per-mode cancellation effect table**; **+1 NEW Appendix D** with three drop-ins (D.1 amends `[2d §4.4]` `default_transport_factory` shared_ptr → unique_ptr; D.2 appends `transport_factory_override` to `[2d §4.5]` `SessionConfig`; D.3 cross-references `[2g App D §D.3]`); §10 Q3 closed as DECIDED; `make(...) noexcept = 0` on `TransportFactory`. Touched sections: status block, header `Convergence log` line, §1 Goal 2, §1.1 (reconnect bullet), §1.2 (TlsTransport bullet), §4.1 (in-flight exclusivity normative paragraph), §4.2 (full reshape — `handshake_result` value type + 1 pure-virtual), §4.3 (PMR claim retired; `is_initiator_shape` dropped), §4.4 (default value flip), §4.4.1 (NEW normative defaults), §4.5 (factory comment rewrite; empty-config narrative dropped; accessor overrides dropped), §4.7 (`make` noexcept), §4.8 (mock accessor overrides dropped; `async_handshake` returns `handshake_result`), §4.9 (single-site `dynamic_cast`; ownership rules), §6.3 (accessor rows dropped), §6.4.1 (NEW per-mode effect table), §6.6 (variant table reshaped + count update + parenthetical rewrite), §9 preamble + new seam #15 + seam #9 update (handshake_result reference), §10 Q3 (DECIDED), §11 Hand-off bullet 3 (D.3 reshape), Appendix B engineering-judgment line (defaults update), Appendix C (this entry), Appendix D (full population — D.1 / D.2 / D.3).

### v0.2 → v0.3 (Gate A round 2, Phase A)

**Phase A round 2 designation.** This is the second convergence pass for Phase A. No reset has been used; the round-cap budget remains 3 rounds with up to 1 full-rewrite reset per the `/gate-a` 3-phase A/B/C convention. The round-2 reviews flag **0 new root causes** — round 1 RC#1 (Appendix D byte-faithfulness), RC#2 (`TlsTransport` collapse to 1 pure-virtual via value-typed `handshake_result`), RC#3 (strand-discipline contracts promoted to API-level: in-flight exclusivity, `ReconnectPolicy::defaults()` numerics, per-mode cancellation effect table) are all structurally closed in v0.2; round 2 is a single line-edit-class convergence pass.

**Reviews input:**
- Codex Gate A (Phase A round 2; tally P1=2, P2=2, P3=1): `research/reviews/codex_2h_2_transport_review.md`
- Opus adversarial review (Phase A round 2; **post-judging combined tally 2 P1 / 3 P2 / 2 P3**; **0 new root causes**; **2 Codex disagreements shelved**; closing recommendation: **"v0.3 can ship after a single convergence pass"**): `research/reviews/opus_2h_2_transport_adversarial_review.md`

**Closing recommendation followed:** "v0.3 can ship after a single convergence pass."

**Round-1 RCs carry-over verdict (round-2 confirmation):** Opus round-2 confirmed all three round-1 RCs are **structurally closed in v0.2** — RC#1 (Appendix D §D.1 / §D.2 byte-faithful blocks against `2d-threading.md` v0.4 lines 448 and 533–535 verified independently by Codex; only the §D.3 cross-reference *prose* was wrong against the live `coverage-index.md`, and that is a line-edit fix landed below as Codex P1-1 / Opus P1-1), RC#2 (value-typed `handshake_result` in place; sub-interface at 1 of 5 pure-virtuals; `dynamic_cast` count at 1 site by design — the residual N-P2 is a clarifying-paragraph fix on top of an otherwise structurally complete change), RC#3 (in-flight exclusivity §4.1, `defaults()` §4.4.1, per-mode cancellation table §6.4.1 all landed; the residual N-P1 is a wall-clock-arithmetic correction inside the §1.1 / §4.4.1 rationale, not a contract change). **Round-2 findings are line-edit class only.**

**Per-finding resolution table:**

| Finding | Severity (after Opus judging) | Resolution | Section(s) edited |
|---|---|---|---|
| Codex round-2 P1-1 / Opus round-2 P1-1 — Appendix D §D.3 cross-reference is materially false against the live `library/spec/coverage-index.md` (lines 155 / 159 / 162 still read `MISSING → row added (T-039)` / `MISSING → row added (T-040)` / `MISSING → row added (T-041)` despite v0.2's claim that `[2g App D §D.3]` already updated those rows) | P1 (confirmed) | **Option (a) chosen per Opus's adversarial-review recommendation** ("(a) is the cleaner pick because the 2g amendment is the upstream owner and 2h merely depends on it — 2h shouldn't queue a duplicate"). §D.3 rewritten to honestly state the live `MISSING → row added` state at v0.3 authoring time AND queue the orchestrator-MUST-apply note for `[2g App D §D.3]` at/before 2h sign-off. §11 Hand-off bullet 3 (Appendix D §D.3 summary) rewritten in lockstep. Appendix D header refreshed to flag the v0.3 honesty fix. No 2h-owned drop-in is queued — that would duplicate `[2g App D §D.3]`. | §11 Hand-off bullet 3, Appendix D header, Appendix D §D.3 |
| Codex round-2 P1-2 — §6.4.1 cancellation taxonomy should use `cancellation_type::{partial, total, terminal}` per `[2f §4.7]` / `[2d §4.7]` precedent instead of `graceful (phase 1) / graceful (phase 2) / terminal` | **Disagreed by Opus — NOT applied** | **Opus disagreed — Codex's counter-proposal NOT applied — reason: the `[2d §4.7]` parent table at line 824 uses the column shape `graceful (phase 1) / graceful (phase 2) / terminal` verbatim; `[2d §4.7]` v0.4 line 760 explicitly DROPPED `partial` from the v1.0 public surface ("ASIO's cancellation_type::partial does not have well-defined semantics"); `[2f §4.5]` line 957 ratifies the same; the v1.0 public close API is `close_mode::{graceful, terminal}` per `[2d §4.7]` lines 766–768, not `asio::cancellation_type`. A `cancellation_type::partial / total / terminal` taxonomy on §6.4.1 would either be dead text (the project never issues `partial`) or actively misleading. The v0.2 §6.4.1 column shape IS the binding extension shape per the parent `[2d §4.7]` table.** Recorded for orchestrator visibility; no edit owed. | (none) |
| Codex round-2 P2-1 — `ReconnectPolicy::defaults()` numerics should be rebased to `base = 1 s, cap = 60 s, jitter = ±20%, max_attempts = 10` per the round-2 checklist | **Disagreed by Opus — NOT applied** | **Opus disagreed — Codex's counter-proposal NOT applied — reason: round-1 Opus N-P1-3 explicitly recommended and ratified `{base = 100 ms, cap = 30 s, jitter = 0.10, multiplier = 2.0, max_attempts = 10}` (verified against `opus_2h_transport_adversarial_review.md` line 111); v0.2 §4.4.1 implements exactly that; the doc is internally consistent (§1.1 reconnect bullet, §4.4 struct defaults, §4.4.1 `defaults()` body, §6.6 variant rationale, §10 Q3 disposition, Appendix B engineering-judgment line all carry the same `100 ms / 30 s / 2.0× / 10 / 0.10` set); the "1 s / 60 s / ±20%" target is a phantom checklist requirement with no normative source. Codex's own counter-proposal acknowledges "v0.2 is internally consistent" — this is not a defect against the doc or its inputs.** The real defect in this same area (the v0.2 wall-clock-arithmetic error in the §1.1 / §4.4.1 rationale) is a separate finding, captured below as Opus N-P1. | (none for the numeric rebase; see Opus N-P1 row for the related real defect) |
| Codex round-2 P2-2 — `handshake_result` missing ALPN field and missing `[[clang::lifetimebound]]` on view accessors | P2 (confirmed partial — ALPN half DROPPED per Opus; lifetimebound clarifier ACCEPTED at editorial severity) | Opus dropped the ALPN half ("`T-012 / FIXS §3.6 ALPN/SNI` is out-of-scope, post-v1, dropped per the FIXS coverage gap" — verified against `library/.specify/2g-tls.md` coverage-index row at line 157 and `[2h §1.2]`'s non-goals; adding ALPN pre-emptively would be scope-creep against `[arch §1.2]`). The lifetimebound half is applied: §4.2 gains a one-paragraph "Accessor lifetime + nullability contracts" note immediately after the `handshake_result` struct definition documenting (a) `handshake_result` publishes no accessors of its own (RC#2 design); the lifetime-bound view accessors that ARE consumed are reached through `handshake_result.peer_id.subject_dn_view()` / `san_dns_names()` / `san_uris()` and those carry `[[clang::lifetimebound]]` at their abstract-base declaration site per `[2g §4.5]` (verified at `2g-tls.md` §4.5: "view accessors carry `[[clang::lifetimebound]]` bound to `*this`"); `negotiated_cipher` is owning `pmr::string` — view materialised via `std::string_view{...}` is bounded by the POD's lifetime by ordinary C++ rules. (b) Folds in Opus N-P2 nullability contract (see below) into the same paragraph. | §4.2 (one-paragraph note appended after `handshake_result` struct definition) |
| Codex round-2 P3-1 — §6.6 line 1158 still names `transport_handshake_not_complete` in the closing parenthetical despite the variant being dropped in v0.2 | P3 (confirmed partial) | The §6.6 closing parenthetical is rewritten to drop the explicit symbol-name mention while preserving the `−1 variant` rationale ("v0.1's accessor-protection variant — see Appendix C convergence log for the named dropped symbol — was dropped in v0.2 (RC#2 close…)"); the convergence-log mentions in Appendix C (per-finding row + round-1 Net-effect summary) MUST stay verbatim per `[const §VI.5]` exact-citation rule for change recording, and DO stay verbatim. | §6.6 closing parenthetical |
| Opus round-2 N-P1 (NEW) — §1.1 last bullet + §4.4.1 rationale state "≈ 4 min 25 s" total wall-clock at `max_attempts = 10` but the actual exponential schedule the doc itself publishes (100 ms → 200 ms → 400 ms → 800 ms → 1.6 s → 3.2 s → 6.4 s → 12.8 s → 25.6 s → 30 s) sums to 81 100 ms ≈ 1 min 21 s; the v0.2 figure was a 3× overstatement | P1 (NEW) | §1.1 reconnect bullet rewritten — wall-clock corrected to "≈ 1 min 21 s (sum of 100 ms + 200 ms + 400 ms + 800 ms + 1.6 s + 3.2 s + 6.4 s + 12.8 s + 25.6 s + 30 s = 81 100 ms; ±10% jitter widens to ≈ 73–89 s)"; the rationale re-defends the "operationally appropriate" claim against the corrected envelope (≈ 1 min 21 s is shorter than typical exchange-gateway recovery windows of 30 s – 5 min — operators that need a longer envelope override `max_attempts` explicitly OR raise `max_delay`; the v1.0 default is intentionally tight against the thunder-herd hazard). §4.4.1 rationale rewritten in lockstep — "≈ 1 min 21 s under the exponential schedule (cumulative; the geometric tail caps at `max_delay = 30 s` from attempt 9 onward)" — and adds an explicit re-baseline note (12 attempts ≈ 2 min 21 s; `max_delay = 90 s` at 10 attempts ≈ 3 min 1 s) so operators sizing escalation policies can pick the right knob. The numeric defaults themselves are NOT changed (per the Codex P2-1 disagreement above); only the published wall-clock claim is corrected. | §1.1 (reconnect bullet), §4.4.1 (rationale paragraph) |
| Opus round-2 N-P2 (NEW) — §4.2 `handshake_result.captured_pinset` is `nullptr`-permitted under `SecurityProfile::mtls_ca` / `one_way_ca` per the inline comment, but the consumer-facing nullability contract is undocumented; downstream consumers (2k OTel cert-event spans per `[2g §7.8]`, 2j ReloadCertSource handler-side identity readout per `[2g §7.7]`, the §9 seam #7 test code) need an explicit guard rule | P2 (NEW) | Folded into the same §4.2 "Accessor lifetime + nullability contracts" paragraph added under Codex P2-2 above. The contract: `handshake_result.captured_pinset` is null IFF the negotiated `SecurityProfile` is `mtls_ca` or `one_way_ca [[deprecated]]` — these profiles do NOT consume a pinset per `[2g §4.5.1]` normative table, so there is no snapshot to capture. Under `mtls_pinned` the `shared_ptr` is non-null and points at the captured-once snapshot per `[2g §6.5.1]`. `peer_id` is always populated (peer cert subject + SAN are extracted regardless of profile). Consumers MUST guard with `if (result.captured_pinset)` before dereference. The nullability is intentional — RC#2 collapsed the v0.1 "value present but might fail to read" accessor surface into a value-typed POD; the one remaining optional-shaped member is documented at the type-definition site rather than via `std::optional<std::shared_ptr<...>>` wrapping (anti-idiomatic since `shared_ptr` already has a null state). | §4.2 (same one-paragraph note as the Codex P2-2 lifetimebound clarifier) |
| Opus round-2 N-P3 (placeholder courtesy-check) — verify that round-1 Opus N-P3-3's `Endpoint::is_initiator_shape()` retirement landed cleanly in v0.2 | (no defect — courtesy-check) | **Verified clean, no edit needed.** §4.3 `Endpoint` definition (the struct itself) carries no `is_initiator_shape()` member; the inline comment block at the bottom of the struct documents the retirement rationale (the v0.1 heuristic `port != 0 && backlog == 128` was a false-positive footgun for acceptors that explicitly set `backlog = 128`; the type-system distinguishes initiator vs acceptor at the construction-path level — `Endpoint` → `Transport::async_connect` for initiator, `Endpoint` → `asio_listener::Config::bind_endpoint` for acceptor). No orphaned references elsewhere in the doc. The retirement is structurally complete. | (none) |
| Codex round-2 P3-1 supplementary (audit) — §9 reconnect-determinism seam (#13) does not pin the `ReconnectPolicy::defaults()` numeric ceilings as in-test policy | P3 (NEW supplementary; subsumed by Codex P3-1 close per the brief's instruction to "add a one-line mention of the defaults numeric to the relevant §9 test seam") | §9 seam #13 (`ReconnectPolicy::delay_for_attempt`) extended with a one-line pin of the §4.4.1 defaults (`100 ms / 30 s / 2.0× / 10 / 0.10`) and an assert that the cumulative wall-clock at `max_attempts = 10` equals 81 100 ms before jitter — so a future tweak to `defaults()` cannot silently desync the published envelope from the seam. Cross-references §4.4.1 directly. | §9 seam #13 |

**Codex findings disagreed with — 2.** Codex P1-2 (cancellation taxonomy `cancellation_type::{partial, total, terminal}`) and Codex P2-1 (`ReconnectPolicy::defaults()` numerics rebase to `1 s / 60 s / ±20%`) were both judged "Disagree" by Opus's adversarial review; both Codex counter-proposals are **NOT applied** in v0.3. Opus's reasoning is recorded verbatim in the per-finding rows above for orchestrator audit trail. The remaining 5 round-2 findings (Codex P1-1, Codex P2-2, Codex P3-1, Opus N-P1, Opus N-P2) are applied as proposed (3) or applied with adjustment (2 — Codex P2-2 with the ALPN half dropped per Opus, and Codex P3-1 with the convergence-log mentions preserved verbatim per `[const §VI.5]`).

**Net-effect summary:** v0.3 is a single line-edit-class convergence pass over v0.2's three structural root-cause closures — no new RC, no new feature, no new structural change. **Net effect:** **±0 test seams** (still 15; seam #13 is line-edited to pin the §4.4.1 defaults explicitly but no new seam is added); **±0 error variants** (still 22); **±0 pure-virtual on `Transport`** (still 5 of 5); **±0 pure-virtual on `TlsTransport`** (still 1); **±0 `dynamic_cast` sites** (still 1); **±0 Appendix D drop-ins** (still 3: D.1 / D.2 / D.3 — D.3 is rewritten honestly to queue the orchestrator-MUST-apply note for `[2g App D §D.3]` instead of falsely claiming the live rows are already updated; D.1 / D.2 unchanged); **+1 §4.2 accessor-lifetime + nullability contract paragraph** (Codex P2-2 lifetimebound clarifier + Opus N-P2 nullability contract folded into one paragraph); §1.1 / §4.4.1 wall-clock arithmetic corrected from "≈ 4 min 25 s" to "≈ 1 min 21 s" with the rationale re-defended; §6.6 line 1158 closing parenthetical rewritten to drop the explicit `transport_handshake_not_complete` symbol-name mention (the convergence-log mentions in Appendix C stay verbatim per `[const §VI.5]`); §9 seam #13 line-edited to pin the §4.4.1 defaults; 2 Codex disagreements shelved (cancellation taxonomy + defaults numerics) with Opus's reasoning recorded above. Touched sections: status block, header `Convergence log` line, §1.1 (reconnect-bullet wall-clock correction), §4.2 (accessor lifetime + nullability contracts paragraph), §4.4.1 (rationale wall-clock correction + rebase notes), §6.6 (closing parenthetical symbol-name purge), §9 seam #13 (defaults pin + cumulative-sum assert), §11 Hand-off bullet 3 (D.3 honesty refresh), Appendix C (this entry), Appendix D header (v0.3 honesty refresh tag), Appendix D §D.3 (full honesty rewrite + orchestrator-MUST-apply note). Same convergence path 2g v0.2 → v0.3 took at its round-2 line-edit pass (closing recommendation followed; 0 new RCs; line-edit residue only).

---

## Appendix D — Drop-in amendments for sibling-doc text touched by this rewrite (NEW v0.2 / RC#1; D.3 honesty refresh v0.3 / round-2 Codex P1-1 / Opus P1-1)

Per convergence rule 6 + the `[2c App D]` / `[2d App D]` / `[2e App D]` / `[2f App D]` / `[2g App D]` sibling-doc-edit precedent, sibling-doc text touched by this rewrite is surfaced as drop-in amendment language for the orchestrator to apply at sign-off. The 2h rewrite agent does not edit `architecture.md`, `2d-threading.md`, or `library/spec/coverage-index.md` directly. Per `[const §VI.5]`, every reference uses the exact `[DocAbbrev §X.Y.Z] Title` form; review-internal IDs (e.g., "RC#1", "Codex P1 #1", "Opus N-P1-1") are not carried into the sibling text.

### D.1 `[2d §4.4] fixpp::core::EngineConfig` — flip `default_transport_factory` from `shared_ptr` to `unique_ptr` (RC#1 close) — ⚠️ **APPLIED, THEN SUPERSEDED (2026-08-29) — see Appendix Z at the END of this file. The "Before" block below no longer exists in `2d`, and the "After" no longer matches shipped code.**

**Tension:** `[2d §4.4]` v0.4 publishes `default_transport_factory` as `std::shared_ptr<fixpp::transport::TransportFactory>` (line 448) — but `[arch §5.6]` frozen-at-open + the `[2e §4.4]` precedent for `MessageStoreFactory` ownership (factory ownership = `unique_ptr`; no mid-session swap, no shared factory across sessions) demands `unique_ptr`. The 2e cross-doc amendment that landed `unique_ptr` for `MessageStoreFactory` (per `[2e App D §D.1]`) is the binding precedent; 2h applies the same edit for `TransportFactory`.

**Before** (current `2d-threading.md` v0.4 text, `[2d §4.4]` `EngineConfig` plugin-default block — line 448 quoted verbatim from the source; whitespace and column alignment preserved):

```cpp
    // ── Default plugin selections (a session may override each in SessionConfig) ─
    std::shared_ptr<fixpp::session::MessageStoreFactory> default_store_factory;
    std::shared_ptr<fixpp::tls::cert_source>             default_cert_source;
    std::shared_ptr<fixpp::transport::TransportFactory>  default_transport_factory;
```

**After** (drop-in replacement — the `default_transport_factory` line has its `shared_ptr` flipped to `unique_ptr`; `default_store_factory` and `default_cert_source` are preserved byte-faithfully against the v0.4 baseline; `default_store_factory` will flip to `unique_ptr` at 2e sign-off-orchestrator-application time per `[2e App D §D.1]` if it has not already, but that edit is owned by 2e's Appendix D, not 2h's):

```cpp
    // ── Default plugin selections (a session may override each in SessionConfig) ─
    std::shared_ptr<fixpp::session::MessageStoreFactory> default_store_factory;
    std::shared_ptr<fixpp::tls::cert_source>             default_cert_source;
    std::unique_ptr<fixpp::transport::TransportFactory>  default_transport_factory;
```

The diff is a single-token edit (`shared_ptr` → `unique_ptr`) on line 448; column alignment is preserved (the `unique_ptr` token is one character shorter than `shared_ptr`, but the trailing whitespace before the field name is widened by one space to keep the `default_transport_factory` column aligned with the field-name column above). The orchestrator's apply step is mechanical.

The orchestrator applies this edit at 2h sign-off; the amendment is recorded in `[2d-threading.md App C]` as a cross-doc edit driven by 2h RC#1 / Codex P1 #1 / Opus N-P1-1.

### D.2 `[2d §4.5] fixpp::session::SessionConfig` — append `transport_factory_override` (RC#1 close) — ⚠️ **APPLIED, THEN SUPERSEDED (2026-08-29) — see Appendix Z at the END of this file. Its line citation is stale too, and was stale before this amendment.**

**Tension:** `[2d §4.5]` v0.4 publishes `SessionConfig` with the engine-anchor + session-`*_override` pattern across the dictionary, executor, clock, store-factory, cert-source axes — but no `transport_factory_override` field. 2h's §4.7.1 declares the field shape and queues the cross-doc amendment; v0.2 writes the byte-faithful drop-in. The "Before" block matches the live source at v0.2 authoring time: the post-2e-sign-off plugin-overrides block (`store_factory` as `std::unique_ptr<MessageStoreFactory>` per `[2e App D §D.1]`; `cert_source` as `std::shared_ptr<fixpp::tls::cert_source>`; `pinset` has NOT yet been applied per the live-source audit — `grep -n "pinset" 2d-threading.md` returns no field at line 536, only the `[2g App D §D.2]` queued amendment text).

**Before** (current `2d-threading.md` v0.4 text, `[2d §4.5]` plugin-overrides block at lines 533–535 — quoted verbatim from the source; whitespace, column alignment, and trailing comments preserved):

```cpp
    // ── Plugin overrides (each null → inherit from EngineConfig) ────────
    std::unique_ptr<MessageStoreFactory>           store_factory;   // unique ownership per [arch §5.6] / [2e §4.4]
    std::shared_ptr<fixpp::tls::cert_source>       cert_source;
```

**After** (drop-in replacement — appends one new line for `transport_factory_override` after `cert_source`; the `store_factory` and `cert_source` lines are preserved byte-faithfully against the post-2e-sign-off baseline; the column alignment of the new line follows the existing two-space gutter and matches the `store_factory` field-name column):

```cpp
    // ── Plugin overrides (each null → inherit from EngineConfig) ────────
    std::unique_ptr<MessageStoreFactory>           store_factory;   // unique ownership per [arch §5.6] / [2e §4.4]
    std::shared_ptr<fixpp::tls::cert_source>       cert_source;
    std::unique_ptr<fixpp::transport::TransportFactory> transport_factory_override; // unique ownership per [arch §5.6] / [2e §4.4] / [2h §4.7.1].
```

The diff is a single-line append at the end of the plugin-overrides block; the orchestrator's apply step is mechanical. If `[2g App D §D.2]` (the `pinset` field append) has applied at orchestrator level between 2g sign-off and 2h sign-off, the orchestrator interleaves the two edits — append `pinset` first per `[2g App D §D.2]`, then append `transport_factory_override` per this drop-in; the order of the two appended fields is not load-bearing (both are plugin-override null-to-inherit fields) but appending in queue-order keeps the Appendix-C cross-doc-edit history clean.

The orchestrator applies this edit at 2h sign-off; the amendment is recorded in `[2d-threading.md App C]` as a cross-doc edit driven by 2h RC#1 / Codex P1 #1 / Opus N-P1-1.

### D.3 `library/spec/coverage-index.md` §"FIXS RC1" — orchestrator-MUST-apply note for `[2g App D §D.3]` at/before 2h sign-off (RC#1 / Codex P3 #10 v0.2 close + round-2 Codex P1-1 / Opus P1-1 honesty fix)

**Tension:** v0.2 declared "no in-place edit owed by 2h" on the basis that `[2g App D §D.3]` (signed off at 2g v0.4) had already updated the §3.4 / §4.1 / §4.4 row Gap-notes from `MISSING → row added (T-XXX)` to `covered by [2g §X.Y]`. Round-2 review (Codex P1-1, confirmed by Opus P1-1) audited the live `library/spec/coverage-index.md` directly and found the v0.2 prose materially false: as of v0.3 authoring time (2026-05-09) the live file at lines 155 / 159 / 162 still reads:

- **line 155:** `| §3.4 | Certificate parameters (RSA 2048-bit min, ECDSA 256-bit, X.509, expiration) | Y | T-039 | MISSING → row added (T-039) |`
- **line 159:** `| §4.1 | Sharing secrets (approved channels: HTTPS, GnuPG, PKCS#12, postal, in-person) | Y | T-040 | MISSING → row added (T-040) |`
- **line 162:** `| §4.4 | Authorization linked to authentication (auth'd TLS identity ↔ FIX CompID) | Y | T-041 | MISSING → row added (T-041) |`

The 2g v0.4 sign-off recorded the Appendix-D drop-in language but the orchestrator's mechanical-apply step on `[2g App D §D.3]` either has not run or did not produce the claimed text in the live source. Appendix D is the orchestrator's mechanical-apply seam — a "no edit owed / already applied" claim that is false against the live source guarantees stale Gap-notes ship. Codex P1-1's two counter-proposal options were (a) queue an explicit orchestrator-MUST-apply note for `[2g App D §D.3]` at/before 2h sign-off and (b) acknowledge the live `MISSING → row added` state and document the resolution path; Opus's adversarial review picked **(a)** as cleaner ("the 2g amendment is the upstream owner and 2h merely depends on it — 2h shouldn't queue a duplicate"). v0.3 picks (a).

**Disposition (round-2 / v0.3 honest re-statement):** As of v0.3 authoring (2026-05-09) the live `library/spec/coverage-index.md` rows §3.4 / §4.1 / §4.4 at lines 155 / 159 / 162 still read `MISSING → row added (T-039)` / `MISSING → row added (T-040)` / `MISSING → row added (T-041)` respectively. **The orchestrator MUST apply `[2g App D §D.3]` at or before 2h sign-off — 2h's sign-off carries this requirement forward as a hard precondition** (in particular: 2h sign-off MUST NOT proceed until either `[2g App D §D.3]` has been mechanically applied OR a downstream sign-off that does discharge T-039 / T-040 / T-041 — e.g., 2j or whatever doc next touches `coverage-index.md` — re-queues the equivalent note). Once applied, the §3.4 row's Gap note will read `covered by [2g §4.5] verify_peer (cross-cut with 2h per [2g §7.1] / [2g §A.2])`; the §4.1 row's Gap note will read `covered by [2g §4.1] cert_source::load_credentials + [2g §4.2] file_cert_source`; the §4.4 row's Gap note will read `covered by [2g §4.5] peer_identity (cross-cut with session-module Phase-4 per [2g §7.2] / [2g §A.2])`. T-039 / T-040 / T-041 catalogue-ID-level cross-cuts are traced via this doc's Appendix A.2 cross-cut rows regardless of whether the orchestrator step has fired (the catalogue-ID column on the coverage-index rows already encodes the `T-039` / `T-040` / `T-041` mapping); the coverage-index Gap-note column is the place the spec-section-to-spec-section pointer lives.

**No 2h-owned drop-in is queued** for the same three rows — that would duplicate `[2g App D §D.3]` and create a maintenance hazard at the next coverage-tooling regeneration. 2h's role here is to flag the orchestrator dependency at sign-off; the upstream owner (2g) carries the actual drop-in language.

Per `[arch Appendix B]` precedent, the spec-section-level link to 2h's own section number for the T-039 wiring half (§7.3) and the T-041 delivery half (§7.2) is intentionally NOT named in the coverage-index Gap note column — the catalogue-ID-level cross-cut is preserved by the existing `T-039` / `T-040` / `T-041` Catalogue IDs entries and is fully traced via this doc's Appendix A.2.

## Appendix Z — post-sign-off amendment, 2026-08-29 (Appendix D §D.1 / §D.2)

*Appended at the END of the file on purpose: 12 line-number citations point into this document, and
an insertion higher up rots every one below it.*

### What happened to D.1 and D.2

Both were **applied to `2d-threading.md`** — and then **superseded by feature 010 (`FR-001a`)**, which
neither document absorbed. Three things are wrong with the Appendix D text as written, in different
directions:

| Appendix D says | Actually |
|---|---|
| D.1 **"Before"** — *"current `2d` v0.4 text … line 448 quoted verbatim"*, showing `std::shared_ptr<…TransportFactory>` | `2d` line 448 has said **`unique_ptr`** since the amendment was applied. The "Before" describes a state that no longer exists while calling itself *current* |
| D.1 **"After"** — `std::unique_ptr<…TransportFactory> default_transport_factory` | **Shipped code is `shared_ptr`** (`include/fixpp/core/engine_config.hpp`). The proposal won in the doc and lost in the code |
| D.2 **"Before"** — *"`2d` … plugin-overrides block at **lines 533–535**"* | That block is **not** at 533–535. The citation was already stale **before** this amendment — the 2026-08-29 Step-R edits to `2d` were made line-shift-free precisely so they could not be blamed for it |
| D.2 **"After"** — `std::unique_ptr<…> transport_factory_override` | **Shipped code is `shared_ptr`** (`include/fixpp/session/session_config.hpp`) |

### The supersession, stated once

**Feature 010, `FR-001a`** flipped `SessionConfig::store_factory` from `unique_ptr` to `shared_ptr` to
make `SessionConfig` **copy-constructible** (the W-5 fix). Its reasoning — *"the binding design used
`unique_ptr` for polymorphic ownership through indirection, NOT to forbid sharing; the factory is a
stateless interface, so sharing across Sessions is meaningful and the per-Session uniqueness
invariant is unaffected"* — applies to **every** polymorphic factory member, and the shipped code
followed it for all three. **2h's D.1/D.2 argued the opposite direction and the tree went the other
way.**

The authoritative declarations are the two headers. **No type is re-copied into this appendix**, for
the reason this whole amendment exists: a copy is what rots.

- `include/fixpp/core/engine_config.hpp` — `EngineConfig`
- `include/fixpp/session/session_config.hpp` — `SessionConfig`
- `specs/010-session-cfg-lifetime/spec.md` — `FR-001a`, with the rationale

### Not corrected here, deliberately

The `2d` code blocks now carry an **in-place** marker on each affected line pointing at the shipped
header. They are **not re-typed**: `2d` §4.4/§4.5 is a *published design contract* others cite by line
number, and rewriting the type would create a fourth copy to keep in sync. The marker is a pointer,
which does not rot.

### What still holds in Appendix D

**D.3** is untouched by this and was not re-checked — **UNVERIFIED, not verified-clean.** The
*Tension* paragraphs of D.1/D.2 remain a correct account of the ownership question as it stood at 2h
sign-off; they are history and do not rot. Only the *Before*/*After* blocks and the line citation are
stale.

### Cross-reference — Appendix D drop-in blocks (added 2026-08-29)

This document's `Before`/`After` drop-in blocks describe **sibling** documents and are not re-checked
by anything. Classify them rather than trusting them; no state is recorded here, because a recorded
state rots:

```bash
python3 tools/check_dropin_blocks.py --self-test
python3 tools/check_dropin_blocks.py --suspect
```

`APPLIED` + prose calling the stale half *"current"*, and `NEITHER` (target in a **third** state), are
the two combinations that are defects on their own. Both are **leads** — the matcher is
substring-based; confirm against the target and the shipped header.

