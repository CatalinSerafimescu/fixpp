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
{ tls, plaintext };` (D-5). Returned by the new `TransportFactory::kind()` **defaulted** virtual (default
`tls`; pure-virtual count stays 3). Drives the FR-008 consistency check: a TLS profile requires a factory
with `kind()==tls`; `insecure_plain_tcp` requires `kind()==plaintext`. Defaulted (not pure) so the ~11
`tests/session/` `TransportFactory` doubles keep compiling (they report `tls`).

## E-4 — `asio_plain_transport_factory` (concrete `TransportFactory`)
`transport_factory.hpp` decl + `transport_factory.cpp` body (D-3, FR-003/FR-004). The abstract base
publishes **exactly 3 pure-virtuals** (`make`, `reload_credentials`, `cert_source_snapshot` —
transport_factory.hpp:73/93/107) plus the **defaulted** `kind()` (D-5). `make_accepted(...)` is **NOT a
base virtual** — it is a *concrete* method on `asio_tls_transport_factory` only (transport_factory.hpp:159),
mirrored here as a *concrete* (non-`override`) method on the plain factory (contract
plain_transport_factory.hpp:59). The plain factory therefore implements: the 3 pure-virtuals + an `override`
of the defaulted `kind()` + a concrete `make_accepted()`:
| Member | Kind | Plaintext behaviour |
|--------|------|---------------------|
| `make(exec, ssl_cfg, mr)` | pure-virtual override | ignores `ssl_cfg`; mints `asio_plain_transport` (trap_throw → `transport_factory_failed`) |
| `reload_credentials(src)` | pure-virtual override | `error::session_invalid_argument` (no certs to rotate) |
| `cert_source_snapshot()` | pure-virtual override | `nullptr` (no cert source; FSM rotation-detect tolerant) |
| `kind()` | defaulted-virtual override | `transport_security_kind::plaintext` |
| `make_accepted(socket, mr)` | **concrete (not a base virtual)** | adopts the accepted plain socket → `asio_plain_transport` in `connected` state. Reached via the listener's concretely-typed accept factory (E-7), NOT through a base `TransportFactory*` — see E-7 for the acceptor construction contract. |
Constructed credential-free via `make_asio_plain_transport_factory(Transport::Config) noexcept` (no
`SslCtxConfig` argument); caches no SSL_CTX. `asio_tls_transport_factory::kind()` returns `tls`.

## E-5 — `ReconnectFsm` plaintext indicator + effective-factory setter
`include/fixpp/session/reconnect_fsm.hpp`. New `set_plaintext_profile(bool)` setter + `is_plaintext_`
field (D-7), mirroring `set_tls_profile`/`tls_profile_`. Step 6 (`reconnect_fsm.cpp:256-272`): when
`is_plaintext_`, skip the `dynamic_cast<TlsTransport*>` + `async_handshake` (connect → Logon). For
non-plaintext, the existing null-cast handling is unchanged (fail-closed; FR-005).
**New `set_transport_factory(TransportFactory*) noexcept` setter (D-4/D-6 wiring contract).** The private
`factory_` (reconnect_fsm.hpp:238) is set at construction to the override-only pointer
(`cfg.transport_factory_override.get()`, session.cpp:154) — **nullptr** for plaintext/no-override, and
`drive_reconnect_attempt()` fails closed at reconnect_fsm.cpp:113-115 on null. This setter (mirroring the
existing `set_tls_profile`/`set_reconnect_endpoint` idiom) repoints `factory_` to the resolved **effective**
factory; `Session::open()` calls it BEFORE any `drive_reconnect()`. `factory_` stays **non-owning** raw —
the Session's `effective_transport_factory_` (E-6) owns the object; the setter passes `.get()`. Covers both
the per-session AND the engine-managed (`run_connect_loop` → `drive_reconnect()`, engine.cpp:990/1009)
initiator paths with the one call; the acceptor mints via the listener (E-7), not the FSM.

## E-6 — `Session::open()` effective-factory resolution + validation arms (extended)
`src/session/session.cpp:875-920`. New Session-owned member
`std::shared_ptr<fixpp::transport::TransportFactory> effective_transport_factory_` holds the factory
resolved **once** at `open()` — the SAME object used for BOTH the FR-008 `kind()` validation AND reconnect
minting (the FSM is repointed at it via E-5's `set_transport_factory`). (1) `insecure_plain_tcp` accepted
(FR-001). (2) **Effective-factory resolution (D-6):** plaintext + no override ⇒ built-in
`asio_plain_transport_factory` (D-4, Session-owned, process-lifetime); otherwise the effective factory is
`transport_factory_override.value_or(engine_default)` (transport_factory.hpp:211-220). Store it in
`effective_transport_factory_`. (3) **FR-008 consistency** keys on the **effective** factory's `kind()`,
NOT the override alone: `kind()==tls` required for a TLS profile, `kind()==plaintext` for
`insecure_plain_tcp`; mismatch → `error::invalid_session_config` (slot 53) before FSM spawn. This catches a
plaintext *engine-default* factory wrongly installed for a TLS session (which would otherwise fail late at
the FSM cast), not just an explicit mismatched override. (4) **Wire the FSM (D-4/E-5):** call
`reconnect_fsm_.set_transport_factory(effective_transport_factory_.get())` BEFORE any `drive_reconnect()`
(alongside `set_tls_profile`, session.cpp:1178) — `factory_` stays non-owning, the member owns. (5) SK→TK
mapping (`:1161-1179`): plaintext leaves `tls_profile=unset` and does NOT build an `SslCtxConfig`; sets the
FSM plaintext indicator (D-7).

## E-7 — Acceptor path (engine + listener) — THREE coordinated sites + a Config-contract change
`run_accept_loop` (engine.cpp:647) + `asio_listener` (asio_listener.cpp:162). The acceptor is a
coordinated **three-site** change (D-4/D-8), not the two the bundle originally named:
1. **engine.cpp:664-681 profile map** — add an explicit `insecure_plain_tcp` arm that does NOT fall
   through the `else → mtls_ca` (engine.cpp:674-675); a plaintext acceptor MUST NOT build a TLS `ssl_cfg`.
2. **`asio_listener::Config` contract** — add a `transport_security_kind transport_kind{tls}` selector
   (reuse E-3's enum). Today the Config has only `ssl_cfg` (asio_listener.hpp:97) and `async_accept()`
   hard-wires `make_asio_tls_transport_factory(cfg_.accepted_transport_config, cfg_.ssl_cfg)` into a
   concrete `shared_ptr<asio_tls_transport_factory> accept_factory_` (asio_listener.hpp:148,
   asio_listener.cpp:162-168). For `transport_kind == plaintext`, mint via
   `make_asio_plain_transport_factory` and hold the accept factory **concretely-typed** — a TLS member and
   a plain member, or a `variant<shared_ptr<asio_tls_transport_factory>,
   shared_ptr<asio_plain_transport_factory>>` selected on `transport_kind`. NOT through a base
   `TransportFactory*`: `make_accepted()` is concrete-only (E-4), callable solely on the concrete type
   (the listener holds `shared_ptr<asio_tls_transport_factory>` at asio_listener.hpp:148 **because** it
   calls `accept_factory_->make_accepted(...)` at asio_listener.cpp:169; a base pointer would not compile).
   `run_accept_loop` sets `lcfg.transport_kind`.
3. **engine.cpp:783-797 handshake skip** — `dynamic_cast<TlsTransport*>` returns null on the plain
   transport; the cast+`async_handshake` block MUST be bypassed (not the `close(); continue;` reject) and
   the path proceeds with a default `hr{}` (D-10).
**TLS-validation event hooks (inert on plaintext):** a plaintext accepted transport runs no handshake, so
the `set_listener_events(...)` wiring (asio_listener.cpp:180; setter kept at contract
asio_plain_transport.hpp:77) is **inert** — plaintext accepted transports receive **no**
TLS-validation event hooks (no `session_event_tls_validation_failed`). Acceptor twin of D-10's initiator
inert-consumer analysis; documented as **L-043-x**. Symmetric with the initiator
(`[[feedback_half_restructure_symmetric_api]]`).

## Authorization (no new entity) — D-10 / FR-008a
On plaintext, `is_mtls` is false ⇒ the `compid_authorization_policy.authorize(peer_id, compid)` arm is
skipped (same as `one_way_ca`/non-mTLS, session.cpp ~2129); `live_peer_id_` never set. Cert-independent
`check_comp_id` (session.cpp:2499) is unaffected. No peer authentication ⇒ limitation **L-043-x**.

## Errors (no new slots — D-11 / FR-013)
Reused: `invalid_session_config` (53), `transport_factory_failed` (109), `session_invalid_argument` (119),
and the `transport_connect_*`/`read_*`/`write_*`/`*_cancelled` variants. NOT used on this path:
`transport_psk_unsupported`, `transport_handshake_*`, `transport_read_truncated`.
