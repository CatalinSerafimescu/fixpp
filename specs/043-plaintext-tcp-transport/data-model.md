# Phase 1 Data Model: Plaintext TCP transport (043)

Value/entity definitions for the feature. Cross-refs: [research.md](./research.md) D-n,
[spec.md](./spec.md) FR-n, [contracts/](./contracts/).

## E-1 — `SecurityProfile::kind::insecure_plain_tcp` (session layer)
`include/fixpp/session/security_profile.hpp`. New closed-enum value `= 4`, carrying a `[[deprecated(...)]]`
attribute (D-9, FR-006). Discriminant only; no payload. `unset (0)` sentinel reject is unchanged (FR-007).
- **Validation:** accepted by `Session::open()` (not `unset`); subject to the FR-008 consistency check.
- **Friction:** referencing the enumerator emits a deprecation diagnostic at the operator's selection site;
  fixpp-internal references suppress via `#pragma clang diagnostic ignored "-Wdeprecated-declarations"`.

## E-2 — `asio_plain_transport` (concrete `Transport`)
`src/transport/asio_plain_transport.{hpp,cpp}`. Implements the 5 base `Transport` pure-virtuals; NOT a
`TlsTransport` (D-1, FR-002). Fields (subset of `asio_tls_transport`, TLS stripped):
| Field | Type | Note |
|-------|------|------|
| `cfg_` | `Transport::Config` | frozen per-instance knobs (FR-010) |
| `exec_` | `asio::any_io_executor` | session strand |
| `socket_` | `asio::ip::tcp::socket` | the plain socket |
| `state_` | `state_t {fresh, connected, closed}` | no `handshaken` state |
| `read_in_flight_` / `write_in_flight_` | `bool` | in-flight exclusivity (base contract) |
| `listener_events_` | `ListenerEvents*` | non-owning; acceptor side (set on accepted transports) |

**State transitions:** `fresh → connected` (after `async_connect`, OR construction-with-accepted-socket);
`connected → closed` (`close()`). No handshake state. After `closed`, every `async_*` →
`transport_already_closed`.
**Method contracts (vs base):** `async_connect` = TCP connect + `apply_socket_options_`, then `connected`
(no handshake); `async_read_some`/`async_write` operate on `socket_`; `cancel()` = `socket_.cancel`;
`close()` = `socket_.close` with NO `SSL_shutdown` (D-2, FR-011). Cancellation → `transport_*_cancelled`.

## E-3 — `transport_security_kind` (factory-kind discriminant)
`include/fixpp/transport/transport_factory.hpp`. `enum class transport_security_kind : std::uint8_t
{ tls, plaintext };` (D-5). Returned by the new `TransportFactory::kind()` pure-virtual. Drives the FR-008
consistency check: a TLS profile requires a factory with `kind()==tls`; `insecure_plain_tcp` requires
`kind()==plaintext`.

## E-4 — `asio_plain_transport_factory` (concrete `TransportFactory`)
`transport_factory.hpp` decl + `transport_factory.cpp` body (D-3, FR-003/FR-004). Implements the abstract
base's 4 virtuals:
| Virtual | Plaintext behaviour |
|---------|---------------------|
| `make(exec, ssl_cfg, mr)` | ignores `ssl_cfg`; mints `asio_plain_transport` (trap_throw → `transport_factory_failed`) |
| `make_accepted(socket, mr)` | adopts the accepted plain socket → `asio_plain_transport` in `connected` state |
| `reload_credentials(src)` | `error::session_invalid_argument` (no certs to rotate) |
| `cert_source_snapshot()` | `nullptr` (no cert source; FSM rotation-detect tolerant) |
| `kind()` | `transport_security_kind::plaintext` |
Constructed credential-free via `make_asio_plain_transport_factory(Transport::Config) noexcept` (no
`SslCtxConfig` argument); caches no SSL_CTX. `asio_tls_transport_factory::kind()` returns `tls`.

## E-5 — `ReconnectFsm` plaintext indicator
`include/fixpp/session/reconnect_fsm.hpp`. New `set_plaintext_profile(bool)` setter + `is_plaintext_`
field (D-7), mirroring `set_tls_profile`/`tls_profile_`. Step 6 (`reconnect_fsm.cpp:256-272`): when
`is_plaintext_`, skip the `dynamic_cast<TlsTransport*>` + `async_handshake` (connect → Logon). For
non-plaintext, the existing null-cast handling is unchanged (fail-closed; FR-005).

## E-6 — `Session::open()` validation arms (extended)
`src/session/session.cpp:875-920`. (1) `insecure_plain_tcp` accepted (FR-001). (2) FR-008: if an explicit
`transport_factory_override` is present and `override->kind()` mismatches the profile category →
`error::invalid_session_config` (slot 53). (3) Auto-derive (D-4): plaintext + no override ⇒ Session owns a
built-in `asio_plain_transport_factory`. (4) SK→TK mapping (`:1161-1179`): plaintext leaves
`tls_profile=unset` and does NOT build an `SslCtxConfig`; sets the FSM plaintext indicator (D-7).

## E-7 — Acceptor path (engine + listener)
`run_accept_loop` (engine.cpp:647) + `asio_listener` (asio_listener.cpp:162). For `insecure_plain_tcp`:
build/mint via `make_asio_plain_transport_factory` instead of `make_asio_tls_transport_factory`, and skip
the post-`async_accept` `async_handshake` (D-4 twin / D-8). Symmetric with the initiator
(`[[feedback_half_restructure_symmetric_api]]`).

## Authorization (no new entity) — D-10 / FR-008a
On plaintext, `is_mtls` is false ⇒ the `compid_authorization_policy.authorize(peer_id, compid)` arm is
skipped (same as `one_way_ca`/non-mTLS, session.cpp ~2129); `live_peer_id_` never set. Cert-independent
`check_comp_id` (session.cpp:2499) is unaffected. No peer authentication ⇒ limitation **L-043-x**.

## Errors (no new slots — D-11 / FR-013)
Reused: `invalid_session_config` (53), `transport_factory_failed` (109), `session_invalid_argument` (119),
and the `transport_connect_*`/`read_*`/`write_*`/`*_cancelled` variants. NOT used on this path:
`transport_psk_unsupported`, `transport_handshake_*`, `transport_read_truncated`.
