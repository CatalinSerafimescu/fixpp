# Phase 0 Research: Plaintext TCP transport (043)

All spec clarifications were resolved in `/speckit-clarify` (spec §Clarifications, Session 2026-06-17).
No `NEEDS CLARIFICATION` markers remain. This file records the design decisions (D-1…D-13) that the
clarified spec implies, each grounded in the existing TLS-transport code (anchors from the code sweep).

## Reference-engine grounding

All three incumbents default to **plaintext** and make TLS opt-in (mirror image of fixpp's TLS-default
posture): quickfix-cpp uses **separate classes** (`SocketInitiator` vs `SSLSocketInitiator`); quickfix-j
uses **one class + a `SocketUseSSL` config flag** (per-session); fix8 uses a declarative **`ssl_context=`**
session attribute (per-session). fixpp keeps TLS as the default-and-recommended and surfaces plaintext as
a loud, explicit opt-in profile — the config-derived selection (QFJ/Fix8 style) informs D-4 (auto-derive).

## Decisions

### D-1 — `asio_plain_transport : public Transport` (NOT `TlsTransport`)
**Decision.** New `src/transport/asio_plain_transport.{hpp,cpp}` implementing the **five base** `Transport`
pure-virtuals only; it does NOT inherit `TlsTransport` and exposes no `async_handshake`.
**Rationale.** The base `Transport` is already "encryption-agnostic byte-stream contract"
(`transport.hpp:31`). Inheriting only the base keeps the `[const §XIV.2]` ≤5-pure-virtual budget at 5/5
with **zero** `TlsTransport` extension slots consumed. The `dynamic_cast<TlsTransport*>` in the FSM
(reconnect_fsm.cpp:259) then legitimately returns null on this type.
**Shape (mirror `asio_tls_transport`, strip TLS).** Keep: `socket_` (`asio::ip::tcp::socket`), `exec_`,
`Transport::Config cfg_`, `read_in_flight_`/`write_in_flight_` exclusivity flags, `apply_socket_options_()`,
the connect-timeout timer arm. **Drop**: `ssl_ctx_`, `ssl_stream_`, `captured_pinset_`, `peer_id_`, the
handshake `role_` steering. State enum collapses `{fresh, connected, handshaken, closed}` →
`{fresh, connected, closed}` (no `handshaken`). `async_read_some`/`async_write` target `socket_` directly
instead of `*ssl_stream_`; `async_connect` is byte-identical (TCP connect, no post-connect handshake);
`cancel()` is identical (`socket_.cancel`).
**Alternatives rejected.** (a) A `TlsTransport` with an "empty SslCtxConfig" plain mode — rejected by 2h
itself (`tls_transport.hpp:71` dropped the v0.1 plain-TCP-via-empty-SslCtxConfig narrative) and would
falsely advertise TLS capability. (b) A runtime `bool tls_enabled_` branch inside `asio_tls_transport` —
rejected: pulls OpenSSL link + the whole TLS state machine into the plain path, violates "surgical".

### D-2 — `close()` omits the TLS bidi shutdown
**Decision.** `asio_plain_transport::close()` transitions to `closed` and calls `socket_.close(ec)`
directly; it omits the `SSL_shutdown()` close-notify block (asio_tls_transport.cpp:1282-1289) and the
`tls_close_timeout` wait. `transport_read_truncated` (a close-notify concept) cannot arise. (FR-011)
**Rationale.** No TLS layer ⇒ no close-notify. Plain TCP close is the FIN.

### D-3 — plaintext factory: implement the abstract `TransportFactory` 3-virtual surface, no certs
**Decision.** New `asio_plain_transport_factory final : public TransportFactory` (declared in
`transport_factory.hpp`, body in `transport_factory.cpp`) + a `make_asio_plain_transport_factory(
Transport::Config) noexcept` free function (no `SslCtxConfig` argument). It satisfies the abstract base's
three pure-virtuals as:
- `make(exec, ssl_cfg, mr)` — **ignores `ssl_cfg`**, mints an `asio_plain_transport` via the same
  `trap_throw` try/catch as the TLS factory (`transport_factory.cpp:210-221`); `transport_factory_failed`
  on throw.
- `make_accepted(accepted_socket, mr)` — adopts the accepted plain socket (mirror
  `transport_factory.cpp:224-244`, minus the SSL_CTX). **(FR-004 acceptor symmetry.)**
- `reload_credentials(new_source)` — returns `error::session_invalid_argument` (slot 119): rotating
  certs on a credential-free transport is a programming error; reuses the existing slot the TLS factory
  already uses for `nullptr` (no new error surface, FR-013).
- `cert_source_snapshot()` — returns `nullptr` (no cert source). The FSM rotation-detect tolerates null
  (it compares snapshots; null==null ⇒ no rotation).
**Rationale.** The abstract `TransportFactory` is the polymorphic seam the FSM + listener already hold;
implementing it verbatim keeps the call sites unchanged. The plain factory caches nothing TLS (no
SSL_CTX), so its per-mint cost is socket-construction only.

### D-4 — auto-derive the plaintext factory from the profile (FR-003a)
**Decision.** When `security_profile.k == insecure_plain_tcp` and there is **no**
`transport_factory_override`, the engine/session supplies a built-in `asio_plain_transport_factory` rather
than the TLS `default_transport_factory`. TLS profiles resolve to the configured factory unchanged.
**Initiator path.** Today `session.cpp:154` hands `cfg.transport_factory_override.get()` to the FSM. Add
a session-open step (after the §919 profile validation): if plaintext-and-no-override, construct a
process-lifetime plaintext factory owned by the Session (a new `cfg_`-adjacent owning member, mirroring
how the override is owned) and point the FSM at it. Engine-managed initiators (`run_connect_loop`) resolve
the same way.
**Acceptor path (half-restructure twin — `[[feedback_half_restructure_symmetric_api]]`).** `run_accept_loop`
(engine.cpp:647-688) currently maps the profile → `SslCtxConfig` and sets `lcfg.ssl_cfg`; the listener
then builds its accept factory via `make_asio_tls_transport_factory` (asio_listener.cpp:162). For
`insecure_plain_tcp`, `run_accept_loop` MUST instead select the plaintext accept-factory path and the
listener MUST mint via `make_asio_plain_transport_factory` — and the acceptor MUST skip its
post-accept `async_handshake` (engine.cpp run_accept_loop). Both halves are required; fixing only the
initiator is the exact half-restructure trap.
**Rationale.** Config-derived transport (QFJ/Fix8 precedent) removes the "forgot to swap the factory"
footgun while preserving an explicit override. **Alternative rejected.** Require the operator to install a
plaintext factory explicitly (quickfix-cpp style) — rejected per the clarify decision (more boilerplate +
footgun; an operator who sets `insecure_plain_tcp` but leaves the default TLS factory would otherwise get
a confusing handshake failure instead of a working plaintext session).

### D-5 — `TransportFactory::kind()` query for the FR-008 consistency check (DEFAULTED virtual)
**Decision.** Add a **non-pure (defaulted) virtual**
`[[nodiscard]] virtual transport_security_kind kind() const noexcept { return
transport_security_kind::tls; }` to the abstract `TransportFactory`, where `enum class
transport_security_kind : std::uint8_t { tls, plaintext }`. `asio_plain_transport_factory` overrides →
`plaintext`; `asio_tls_transport_factory` overrides → `tls` (explicit, for clarity). The **pure**-virtual
count stays **3** (well under the `[const §XIV.2]` 5/5 cap — the cap counts pure-virtuals).
**Rationale.** FR-008 must reject an **explicit** override whose kind disagrees with the profile, *before*
connect/handshake. A typed `kind()` is robust where `dynamic_cast<asio_tls_transport_factory*>` is fragile.
**Why DEFAULTED, not pure (correction to the first draft):** a *pure* `kind()` would break **~11 existing
test-double `TransportFactory` impls** under `tests/session/` (each would fail to compile until it added an
override) — the `[[feedback_nodiscard_migration_misses_raw_callsites]]` census-miss class. A defaulted
`kind()` returning `tls` (a) leaves every existing double compiling unchanged with the correct value (they
are all TLS-oriented), and (b) is a **safe default** — a third-party factory that forgets to override
defaults to `tls`, so the worst case is a fail-closed mismatch-reject against a plaintext profile, never a
silent plaintext downgrade. Note: the `transport_factory.hpp` base (unlike the `transport.hpp` `Transport`
base) carries no "zero defaulted-virtual headroom" constraint, so a defaulted virtual is permitted here.
**Alternative rejected.** Pure `kind()` (breaks 11 doubles, census-miss risk); `dynamic_cast`-based kind
detection (fragile, type-coupled).

### D-6 — `Session::open()` consistency + acceptance arm (FR-001, FR-008)
**Decision.** Extend the open() validation block (session.cpp:875-920):
1. `insecure_plain_tcp` is **accepted** (it is not `unset`); the existing `k == unset` reject is unchanged.
2. **Consistency (FR-008):** if an explicit `transport_factory_override` is present and
   `override->kind()` disagrees with the profile category (plaintext profile ⇄ tls factory, or tls
   profile ⇄ plaintext factory) → `co_return std::unexpected(error::invalid_session_config)` (slot 53),
   before any FSM spawn. A plaintext profile with no override is auto-corrected (D-4), not rejected.
**Rationale.** Fail closed at open() rather than discover the mismatch at handshake (a TLS profile + plain
factory would never authenticate; a plaintext profile + TLS factory would attempt an unanswered handshake).

### D-7 — FSM handshake-skip gated on profile (FR-005)
**Decision.** The session passes a plaintext indicator to the FSM (a new
`ReconnectFsm::set_plaintext_profile(bool)` mirroring `set_tls_profile`, or fold into a small
`security_kind_` field). FSM step 6 (reconnect_fsm.cpp:256-272) becomes: **if plaintext → skip the
`dynamic_cast`+`async_handshake` entirely** (connect → Logon). For non-plaintext profiles the existing
"null cast ⇒ count+continue/error" stays fail-closed (a TLS profile that received a non-TLS transport is
still a bug, not a silent plaintext downgrade).
**Rationale.** Spec FR-005 + clarify decision: gate on profile, never on the cast result alone.
**Session-mapping interaction.** The session.cpp:1161-1179 `SK→TK` mapping must NOT build an
`SslCtxConfig` / set a TLS `tls_profile` for plaintext (which would arm a handshake). For
`insecure_plain_tcp` the mapping leaves `tls_profile = unset` AND sets the plaintext indicator; the FSM
short-circuits before the `transport_psk_unsupported` gate.

### D-8 — acceptor handshake skip (FR-004/FR-005 twin)
**Decision.** `run_accept_loop` (engine.cpp) skips its post-`async_accept` `async_handshake` step when the
session profile is `insecure_plain_tcp`, symmetric to D-7. The minted accepted transport is a plain
transport (D-4 acceptor path); there is no `TlsTransport` to handshake.

### D-9 — loud-insecure friction: `[[deprecated]]` on the session enumerator (FR-006)
**Decision.** Mark the new session-layer enumerator
`SecurityProfile::kind::insecure_plain_tcp [[deprecated("insecure_plain_tcp disables transport security
(no TLS/encryption/peer-auth); use only over a separately-secured link (colo/VPN) — prefer mtls_pinned/
mtls_ca")]] = 4` in `include/fixpp/session/security_profile.hpp`. Internal references to the value (the
session.cpp mapping, the auto-derive, tests) wrap in `#pragma clang diagnostic ignored
"-Wdeprecated-declarations"` exactly like session.cpp:1173-1176.
**Rationale.** The friction must fire at the operator's *selection* site (where they type the enum value),
which is the enumerator. Suppressing internally keeps fixpp's own build clean while the operator sees the
warning. This is the construction-site `[[deprecated]]` diagnostic `[const §XII.5]` prescribes.
**Framing correction (advisor catch).** This is NOT "mirroring `one_way_ca`'s wiring": the **session-layer**
`SecurityProfile::kind::one_way_ca` carries **no** `[[deprecated]]` attribute today
(`include/fixpp/session/security_profile.hpp:45`) — the attribute lives only on the **tls-layer**
`tls::SecurityProfile::one_way_ca` (`include/fixpp/tls/security_profile.hpp:42`), and that reference is
pragma-suppressed internally (session.cpp:1173-1176). So an operator selecting session-layer
`kind::one_way_ca` gets **no** warning today — a **pre-existing constitution-vs-code drift** (§XII.5 line
185 states one_way_ca "Construction emits a compile-time `[[deprecated]]` diagnostic", which the session
enum does not honour). D-9 makes `insecure_plain_tcp` honour §XII.5's prescription at the session layer —
*stronger* than the current `one_way_ca`. **Parity decision:** bringing session-layer `one_way_ca` to
parity is **OUT OF 043 SCOPE** — it has blast radius (existing call sites select it, e.g. the
`NonSentinelAccepted` test loops over `kind::one_way_ca` at
`tests/session/test_session_open_rejects_unset_security_profile.cpp:222`, and would newly warn). The drift
is flagged for the v1.0 release gate (or a separate cleanup), not fixed here. **Alternative rejected.**
`[[deprecated]]` on a factory function (operators select the profile, not the factory, under auto-derive).

### D-10 — `handshake_result` absence + authorization on the plaintext path (FR-008a)
**Plaintext is the FIRST profile that produces no `handshake_result`** (one_way_ca still does a TLS
handshake and yields an `hr`). The FSM plaintext branch (D-7) therefore skips **both** step 6 (handshake)
**and step 7** (`reconnect_fsm.cpp` ~`authorize(hr.peer_id, …)`) — there is no `hr` to authorize against.
**Enumerated `hr`/peer-identity consumers on the connect handoff (must all be inert on plaintext):**
1. **FSM step 7** `cfg.compid_authorization_policy.authorize(hr.peer_id, asserted_compid)` — SKIPPED on
   the plaintext branch (no step 7).
2. **`Session::install_reconnected_transport(t, hr)`** (session.cpp:405-427) does
   `live_peer_id_ = std::move(hr.peer_id)` **unconditionally**. The plaintext branch passes a
   **default-constructed `handshake_result`**, so `live_peer_id_` becomes a *present-but-empty*
   `peer_identity`. This is **safe** because every consumer is `is_mtls`-gated (see below) — but for
   defence-in-depth the implement phase SHOULD leave `live_peer_id_ == nullopt` on the plaintext path (a
   one-line guard in `install_reconnected_transport`, or a plaintext-specific handoff), so a future
   consumer cannot misread an empty identity as "authenticated".
3. **Acceptor `Session::attach_accepted_transport(t, …)`** (session.cpp:534) — the acceptor twin. The
   plaintext acceptor path (D-8) has no handshake `peer_id`; the implement phase MUST trace this call and
   ensure it is invoked with no/empty identity and that `live_peer_id_` stays unset.
4. **Inbound authorization gate** (session.cpp:2137 `if (live_peer_id_.has_value() && is_mtls)`) — gated
   on `is_mtls`, which is **false** for plaintext (the SK→TK mapping never sets mtls for
   `insecure_plain_tcp`), so it falls into "case 3: non-mTLS → skip" exactly like `one_way_ca`.
**Decision.** Plaintext skips the mTLS-gated `compid_authorization_policy` authz (already skipped for
non-mTLS); the cert-independent `check_comp_id` inbound 49/56 match (session.cpp:2499, default on) is
unaffected. Required code work is minimal: confirm `is_mtls` stays false (it will), pass a default `hr` on
the plaintext handoff, and add the `live_peer_id_`-stays-nullopt defensive guard (#2/#3).
**Limitation L-043-x.** The plaintext profile provides **no peer authentication**; documented in B&L.

### D-11 — error reuse, no new slots (FR-013)
`error::invalid_session_config` (53) for FR-008 mismatch; `transport_factory_failed` (109) for plain
factory throw; `session_invalid_argument` (119) for `reload_credentials` on the plain factory; the
existing `transport_connect_*`/`transport_read_*`/`transport_write_*`/`transport_*_cancelled` variants
apply verbatim to the plain socket. No `transport_psk_unsupported`/`transport_handshake_*`/
`transport_read_truncated` on this path. **No new error enumerator** is introduced.

### D-12 — `Transport::Config` reuse (FR-010)
The plain transport honours `tcp_nodelay` (default ON), keepalive knobs, send/recv buffers, SO_LINGER,
SO_REUSEADDR via the same `apply_socket_options_()` logic; `tls_handshake_timeout`/`tls_close_timeout`
are simply unread on this path. No new Config field.

### D-13 — stale comment + doc reconciliation
`engine.cpp:322` ("The engine exclusively uses asio_tls_transport") and the `tls_transport.hpp:69-71`
"every v1.0 transport is TLS-capable" partition note become stale and MUST be corrected (dated note, not
silent rewrite). The `security_profile.hpp` session-stub comment ("A future no-TLS / plaintext escape
value … will be appended …") is reconciled by D-9.

## Open items deferred to /plan-tasks (not blocking)

- Exact ownership of the auto-derived initiator plaintext factory (a Session-owned member vs an engine
  cache) — D-4 picks Session-owned; tasks.md pins the member.
- Whether `set_plaintext_profile(bool)` vs a `security_kind_` enum on the FSM (D-7) — cosmetic; tasks.md
  picks one.
- alloc-NFR: the plain read/write path is caller-buffer (no transport read-buffer alloc), same as TLS —
  no new alloc gate beyond the existing transport read alloc guard.
- **SC-004 witness mechanism (advisor catch).** A `[[deprecated]]` enumerator emits a *warning* only in an
  unsuppressed TU, so "observable at build time" needs a concrete witness. Pin in tasks.md: a dedicated
  one-line TU that selects `kind::insecure_plain_tcp` compiled with `-Werror=deprecated-declarations`,
  asserted to **fail compilation** (a CMake `try_compile`/negative-compile test, or the repo's existing
  negative-compile harness), OR a documented manual witness. Not a runtime gtest.
- **`kind()` is defaulted (D-5)** so it does NOT ripple to the ~11 `tests/session/` `TransportFactory`
  test doubles — they keep compiling and report `tls`. No census required; this is the deliberate reason
  D-5 chose defaulted over pure.
