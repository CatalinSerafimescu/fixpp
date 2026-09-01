# Transport quickstart (`fixpp::transport`)

> Operator-facing companion to [`specs/012-2h-transport/quickstart.md`](./specs/012-2h-transport/quickstart.md). The full Scenarios A/B/C/D walk-through (compilable code) lives in the spec quickstart; this page is the high-altitude orientation + the operator decision tree. Pairs with [TLS quickstart](./tls-quickstart.md) — 012 consumes `SslCtxConfig` / `verify_peer` / `Pinset` from `fixpp::tls`.

## What `fixpp::transport` ships in v1.0

| Type                          | Purpose                                                                                                                       | Header                                          |
| ----------------------------- | ----------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------- |
| `Transport`                   | Abstract byte-stream interface — 5 pure-virtuals (`async_connect`, `async_read_some`, `async_write`, `cancel`, `close`).      | `fixpp/transport/transport.hpp`                 |
| `TlsTransport`                | TLS-aware sub-interface (1 additional pure-virtual: `async_handshake → handshake_result`).                                    | `fixpp/transport/tls_transport.hpp`             |
| `asio_tls_transport`          | v1.0 default impl — ASIO `tcp::socket` + `asio::ssl::stream` over OpenSSL on Linux + Windows per `[const §XII.1]`.            | `fixpp/transport/asio_tls_transport.hpp`        |
| `Endpoint`                    | Value type: `(host, port)` for initiators; `(bind_addr, bind_port, backlog)` for acceptors.                                   | `fixpp/transport/endpoint.hpp`                  |
| `ReconnectPolicy`             | Schedule-array value type per FIXS — `{schedule, jitter, max_attempts}`. `defaults()` + `defaults_quickfix_compat()`.         | `fixpp/transport/reconnect_policy.hpp`          |
| `Listener` / `asio_listener`  | Multi-session acceptor — 1 pure-virtual (`async_accept`) + ASIO acceptor wrapper. T-005.                                      | `fixpp/transport/listener.hpp`                  |
| `TransportFactory`            | Frozen-at-open factory per `[arch §6]` rule 4; `make(...) noexcept`; engine-anchor + session-`*_override` per `[2d §4.4/4.5]`. | `fixpp/transport/transport_factory.hpp`         |
| `mock_transport`              | Public-test-header impl — deterministic in-memory `Transport` for FSM-under-test (`FIXPP_ALLOW_MOCK_TRANSPORT` guarded).      | `fixpp/transport/test/mock_transport.hpp`       |
| `error::transport_*`          | Error envelope per `[2h §6.6]` (22 variants — connect/read/write/handshake/cancel + reconnect-limit + factory failures).      | `fixpp/core/error.hpp` (transport block)        |

## Operator decision tree

```text
Are you running a FIX initiator (client) over TCP+TLS?
├── Yes, standard exchange/venue → Scenario A
│   (asio_tls_transport_factory + SslCtxConfig from 011 + Endpoint(host, port))
└── Yes, with a custom transport (SHM, kernel-bypass, DPDK, Onload) → post-v1; implement
    your own TransportFactory + Transport against the v1.0 surface (5 + 1 pure-virtuals).

Need bounded reconnect after a transient disconnect?
├── Default (industry-aligned, anti-thundering-herd) → ReconnectPolicy::defaults()
│   [100ms, 200ms, 400ms, 800ms, 1.6s, 3.2s, 6.4s, 12.8s, 25.6s, 30s] + 10% jitter, cap 10 attempts (~ 1 min 21 s).
├── QuickFIX-compatible cadence → ReconnectPolicy::defaults_quickfix_compat()
│   {[30s], 0.0 jitter, 0 max_attempts} — matches QuickFIX-cpp m_reconnectInterval=30 / Fix8 _login_retry_interval.
└── Unbounded retries → policy = defaults(); policy.max_attempts = 0;  // explicit operator opt-in per [const §XII.5].

Running an acceptor (server) — multiple inbound sessions on one bind?
└── Scenario C — asio_listener wrapping asio::ip::tcp::acceptor. Each accept mints a fresh
    Transport. Cancel surface is concrete (Option-A): close the listening socket, in-flight
    accepts surface transport_accept_cancelled, already-resumed Transports keep running.

Need to FSM-test the session module without a real socket?
└── Scenario D — include <fixpp/transport/test/mock_transport.hpp> (guarded by
    -DFIXPP_ALLOW_MOCK_TRANSPORT in tests/). Script inbound bytes + expected outbound
    writes + partial-write injection (FR-037) + handshake outcome; the mock honours every
    cancellation contract the production impl does.
```

## Reconnect semantics — fresh-mint per attempt

Per Clarifications 2026-05-27 Q1=B (Appendix D §D.4), the session FSM reconnects by:

1. **Destroying** the dead `Transport` (`unique_ptr` reset).
2. **Sleeping** for `policy.delay_for_attempt(attempt_n)` (one-param shape per Appendix D §D.6; the policy owns the deterministic jitter seed per `[const §VII.7]` fuzz determinism).
3. **Minting** a fresh `Transport` via the same `TransportFactory` (the factory is frozen-at-open; the instances it produces are per-attempt).
4. **Calling** `async_connect(...)` on the new instance.

The `SslCtxConfig` / engine PMR root / engine clock are **cached at the factory level** (FR-026) — the per-attempt mint cost is the `Transport`-only construction, not a full `SSL_CTX` rebuild. This matches the QuickFIX-cpp `SocketInitiator::doConnect`, QuickFIX/J `IoSessionInitiator::connect()`, and Fix8 `ReliableClientSession::operator()` patterns. The previous design-doc choice of "re-issue `async_connect` on the same instance" diverged silently and was caught by `/clarify` Q1.

## Cancellation contract — two-phase close

Per `[const §XI.2]` + `[2d §4.7]` per-mode effect table:

| Phase                 | What 2h does                                                                                                                       |
| --------------------- | ---------------------------------------------------------------------------------------------------------------------------------- |
| `graceful (phase 1)`  | In-flight reads continue (drain inbound bytes); outbound `Logout` runs under the **child** cancellation state.                     |
| `graceful (phase 2)`  | Root `cancellation_type::total` fires; `async_read_some` / `async_write` / `async_handshake` complete with their `*_cancelled` variants. |
| `terminal`            | Immediate `cancellation_type::total`; persisted frames survive per `[2e §6.1.4]` durable-before-transmit.                          |

Each async method surfaces its cancellation variant on the awaitable: `transport_connect_cancelled`, `transport_read_cancelled`, `transport_write_cancelled`, `transport_handshake_cancelled`, `transport_accept_cancelled`. The C-ABI bridge maps these into `FIXPP_ERR_CANCELLED` per 2i.

## In-flight exclusivity (API-level contract)

At most **one** in-flight `async_read_some` and **one** in-flight `async_write` per `Transport`. Overlap surfaces:

- `transport_read_in_progress` — second `async_read_some` while the first is in flight.
- `transport_write_in_progress` — second `async_write` while the first is in flight.
- `transport_already_connected` — second `async_connect` or `async_handshake` (one-shot per lifetime) on a still-OPEN Transport. Once the Transport is closed these return `transport_already_closed` instead — see B-339-1, which also notes that an in-protocol TLS handshake failure reaches the closed state without `close()`.

The strand serialisation is defence-in-depth; the API-level exclusivity contract is the binding rule (the strand only serialises completion-handler dispatch, not initiation).

## What this feature does **not** do

- FIX framing (`Framer::feed` / `Writer::commit`) — owned by **2b**; 2h ships raw bytes.
- TLS policy (`cert_source`, `Pinset`, `CipherPolicy`, `SecurityProfile`, `verify_peer`, `peer_identity`) — owned by **2g** (see [TLS quickstart](./tls-quickstart.md)); 2h consumes them through `SslCtxConfig`.
- Session FSM (Logon, Heartbeat, ResendRequest, sequence reset, reconnect-decision logic) — owned by the **post-012 session-Phase-4** spec; 2h provides the wire-side hop only.
- CompID-to-TLS-identity binding (T-041 policy half) — owned by **session-Phase-4**; 2h delivers `peer_identity` via `handshake_result`.
- C-ABI surface (`fixpp_transport_t`, `fixpp_listener_t`, `fixpp_transport_factory_t`, `FIXPP_ERR_TRANSPORT_*`) — owned by **2i**.
- Control-plane drain / reload triggers — owned by **2j**.
- SHM / DPDK / Onload / kernel-bypass / UDP impls — out of v1.0 per `[arch §1.2]` non-goals.

## Reference

- Spec quickstart (full Scenarios A/B/C/D with compilable code): [`specs/012-2h-transport/quickstart.md`](./specs/012-2h-transport/quickstart.md)
- TLS companion: [tls-quickstart.md](./tls-quickstart.md)
- Design doc: [`../../.specify/2h-transport.md`](../../.specify/2h-transport.md) + [`../../.specify/architecture.md`](../../.specify/architecture.md) §4.5
- Constitution: [`../../.specify/constitution.md`](../../.specify/constitution.md) Article XI (cancellation), Article XII (security/TLS), Article XIV (pluggable interfaces), Article XV (banned patterns).
