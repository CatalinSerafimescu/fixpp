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

### D-3 — plaintext factory: implement the 3 base pure-virtuals + defaulted `kind()` override; `make_accepted` is concrete (not a base virtual)
**Decision.** New `asio_plain_transport_factory final : public TransportFactory` (declared in
`transport_factory.hpp`, body in `transport_factory.cpp`) + a `make_asio_plain_transport_factory(
Transport::Config) noexcept` free function (no `SslCtxConfig` argument). The base publishes **exactly 3
pure-virtuals** (`make`, `reload_credentials`, `cert_source_snapshot`) plus the **defaulted** `kind()`
(D-5); `make_accepted(...)` is **NOT a base virtual** — it is a *concrete* method on the concrete factory
(matching E-4). The plain factory therefore implements: the 3 pure-virtual overrides + an `override` of the
defaulted `kind()` + a CONCRETE (non-`override`) `make_accepted()`:
- `make(exec, ssl_cfg, mr)` — pure-virtual override; **ignores `ssl_cfg`**, mints an `asio_plain_transport`
  via the same `trap_throw` try/catch as the TLS factory (`transport_factory.cpp:210-221`);
  `transport_factory_failed` on throw.
- `reload_credentials(new_source)` — pure-virtual override; returns `error::session_invalid_argument`
  (slot 119): rotating certs on a credential-free transport is a programming error; reuses the existing
  slot the TLS factory already uses for `nullptr` (no new error surface, FR-013).
- `cert_source_snapshot()` — pure-virtual override; returns `nullptr` (no cert source). The FSM
  rotation-detect tolerates null (it compares snapshots; null==null ⇒ no rotation).
- `kind()` — defaulted-virtual override; returns `transport_security_kind::plaintext` (D-5).
- `make_accepted(accepted_socket, mr)` — **concrete (not a base virtual)**; adopts the accepted plain
  socket (mirror `transport_factory.cpp:224-244`, minus the SSL_CTX). Callable only through the
  concretely-typed accept factory the listener holds (E-7), NOT through a base `TransportFactory*`.
  **(FR-004 acceptor symmetry.)**
**Rationale.** The 3 base pure-virtuals are the polymorphic seam the FSM + initiator path hold. The
listener does NOT call `make_accepted` through the base — it holds a concretely-typed accept factory (E-4
/ E-7), so the listener Config + accept-factory holder DOES change (per E-7); the initiator/FSM `make`
seam is unchanged. The plain factory caches nothing TLS (no SSL_CTX), so its per-mint cost is
socket-construction only.

### D-4 — auto-derive the plaintext factory from the profile (FR-003a)
**Decision.** When `security_profile.k == insecure_plain_tcp` and there is **no**
`transport_factory_override`, the engine/session supplies a built-in `asio_plain_transport_factory` rather
than the TLS `default_transport_factory`. TLS profiles resolve to the configured factory unchanged.
**Initiator path — the FSM effective-factory wiring contract.** Today the Session ctor hands
`cfg.transport_factory_override.get()` to the FSM (session.cpp:154) and `ReconnectFsm::factory_` is
**private** (reconnect_fsm.hpp:238) with **no** repoint setter; for the plaintext/no-override case that
ctor-time pointer is **nullptr**, and `drive_reconnect_attempt()` fails closed at reconnect_fsm.cpp:113-115
(`if (factory_ == nullptr) co_return std::unexpected{transport_factory_failed}`). Validation alone does NOT
reach the mint path. The wiring contract:
1. **Session owns the resolved factory.** A new Session member `std::shared_ptr<fixpp::transport::Transport
   Factory> effective_transport_factory_` holds the factory resolved **once** at `open()` — the SAME object
   used for BOTH the FR-008 `kind()` validation (D-6) AND reconnect minting. For plaintext/no-override it is
   the built-in plaintext factory (auto-derive); otherwise `override.value_or(engine_default)`.
2. **New setter `ReconnectFsm::set_transport_factory(TransportFactory*) noexcept`** (mirroring the existing
   `set_tls_profile`/`set_reconnect_endpoint` setter idiom) repoints the private `factory_` from the
   ctor-time override to the resolved effective factory. `Session::open()` calls it (alongside
   `set_tls_profile`, session.cpp:1178) **BEFORE** any `drive_reconnect()`/`drive_reconnect_attempt()`.
   `factory_` stays **non-owning** raw — the Session's `shared_ptr` member owns the resolved factory; the
   setter passes `.get()` (same idiom as session.cpp:154). The reconnect_fsm.hpp:238 "owned by
   `transport_factory_override`" comment is updated to "owned by the Session (override or
   `effective_transport_factory_`)".
3. **One setter covers both initiator paths — NOT a half-restructure.** Engine-managed initiators
   (`run_connect_loop`, engine.cpp:990) drive the connect entirely through `session->drive_reconnect()`
   (engine.cpp:1009) → `drive_reconnect_attempt()` → `factory_->make(...)`; they do **no** independent
   factory resolution. So the same `set_transport_factory` in `open()` covers the per-session initiator
   AND the engine-managed initiator. The acceptor mints via the listener (E-7), separately covered — there
   is no third site to repoint.
**Test note.** A 043 test MUST assert that a plaintext/no-override session **AND** a TLS/no-override session
with a plaintext-incompatible (here: TLS) engine-default actually connect through the **checked effective
factory** — i.e. the resolved factory reaches the FSM mint path, not a late nullptr/stale-pointer failure
in `drive_reconnect_attempt()` (and the mismatched-engine-default case fails clean at `open()` per D-6,
never at the FSM cast).
**Acceptor path (half-restructure twin — `[[feedback_half_restructure_symmetric_api]]`).** The acceptor is
the bundle's weakest seam: the handshake does NOT live in the listener — it lives in `run_accept_loop`.
The plaintext acceptor is a **coordinated THREE-site change** (Gate A round 1 found the bundle had named
only two), plus one **listener Config contract** change. All four MUST change in lockstep:
1. **ssl_cfg profile-map arm (engine.cpp:664-681).** The `else` arm today maps **every** unrecognised
   profile kind — *including `insecure_plain_tcp`* — to `mtls_ca` (engine.cpp:674-675). Left unchanged,
   a plaintext acceptor would silently build a **TLS** listener and reject every plain connection. The
   fix MUST add an explicit `insecure_plain_tcp` arm that does **not** build an `ssl_cfg` / does not
   fall through to `mtls_ca`, and selects the plaintext listener path instead.
2. **Listener Config + accept-factory selection (asio_listener.hpp:78-105 / asio_listener.cpp:161-168).**
   Today `asio_listener::Config` carries only `ssl_cfg` and `async_accept()` hard-wires the TLS
   accept-factory via `make_asio_tls_transport_factory(cfg_.accepted_transport_config, cfg_.ssl_cfg)`
   (asio_listener.cpp:162) into a concrete `shared_ptr<asio_tls_transport_factory> accept_factory_`
   (asio_listener.hpp:148). The Config MUST gain a `transport_security_kind transport_kind{tls}` selector
   (reusing D-5's enum); `async_accept()` selects `make_asio_plain_transport_factory` when
   `transport_kind == plaintext`. The accept factory MUST be held **concretely-typed**, NOT through a base
   `TransportFactory*` — because `make_accepted()` is a concrete (non-virtual) method (D-3/E-4), callable
   only on the concrete type (verified: the listener holds `shared_ptr<asio_tls_transport_factory>` at
   asio_listener.hpp:148 **because** it calls `accept_factory_->make_accepted(...)` at
   asio_listener.cpp:169; a base `TransportFactory*` would not compile). The listener therefore holds the
   accept factory as a concretely-typed / kind-tagged variant (a TLS member and a plain member, or a
   `variant<shared_ptr<asio_tls_transport_factory>, shared_ptr<asio_plain_transport_factory>>` selected on
   `transport_kind`). `run_accept_loop` sets `lcfg.transport_kind = plaintext` for `insecure_plain_tcp`
   and leaves `lcfg.ssl_cfg` default.
3. **Post-accept handshake skip (engine.cpp:783-797).** The `dynamic_cast<TlsTransport*>` (engine.cpp:783)
   on an accepted plain transport returns null → today's `transport->close(); continue;` would reject
   **every** plain connection in a loop. `run_accept_loop` MUST skip the `async_handshake` block
   (engine.cpp:789-797) entirely on the plaintext profile (symmetric to D-7 / D-8) and proceed straight
   to the bounded first-frame read with a default-constructed `hr{}` (D-10).
Fixing only the initiator — or only sites #2/#3 while leaving site #1's `mtls_ca` fall-through — is the
exact half-restructure trap. **TLS-validation event hooks (P2-A):** a plaintext accepted transport runs
**no** handshake, so the `set_listener_events(...)` wiring (asio_listener.cpp:180; the plain transport
keeps the setter, contract asio_plain_transport.hpp:77) is **inert** — plaintext accepted transports emit
**no** `session_event_tls_validation_failed` / TLS-validation events. This is documented (the acceptor
twin of D-10's initiator inert-consumer analysis) and recorded as **L-043-x** at B&L Polish.
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

### D-6 — `Session::open()` effective-factory resolution + consistency arm (FR-001, FR-003a, FR-008)
**Decision.** Extend the open() validation block (session.cpp:875-920). The key correction (Gate A round 1)
is that the consistency check MUST key on the **resolved/effective** factory, not on the *override* alone —
otherwise a TLS profile with NO session override + a *plaintext engine-default factory*
(`EngineConfig::default_transport_factory`, resolved at transport_factory.hpp:211-220 via
`override.value_or(engine_default)`) escapes the open() check and fails late at the FSM
`dynamic_cast<TlsTransport*>` (reconnect_fsm.cpp:259-262) → a confusing retry-to-cap, not a clean
fail-closed-at-open. Define an explicit **effective-factory resolution step** before FSM wiring:
1. `insecure_plain_tcp` is **accepted** (it is not `unset`); the existing `k == unset` reject is unchanged.
2. **Effective factory:** if `insecure_plain_tcp` and **no** `transport_factory_override` ⇒ the built-in
   plaintext factory (D-4 auto-derive). Otherwise the effective factory is
   `transport_factory_override.value_or(engine_default)`.
3. **Consistency (FR-008):** require the **effective** factory's `kind()` to match the profile category —
   `kind()==tls` for a TLS profile (mtls_ca/mtls_pinned/one_way_ca), `kind()==plaintext` for
   `insecure_plain_tcp`. On mismatch → `co_return std::unexpected(error::invalid_session_config)` (slot
   53), before any FSM spawn. This catches **both** an explicit mismatched override **and** a wrong
   engine-default factory (e.g. a plaintext default installed for a TLS session). A plaintext profile with
   no override is auto-corrected to the plaintext factory (step 2), never rejected.
4. **Wire the resolved factory into the FSM (D-4 initiator).** Store the resolved factory in the Session's
   `effective_transport_factory_` member and call `reconnect_fsm_.set_transport_factory(
   effective_transport_factory_.get())` BEFORE any `drive_reconnect()` (alongside `set_tls_profile` at
   session.cpp:1178). The SAME validated object that passed step 3's `kind()` check is the one the FSM mints
   through — no second resolution, no late nullptr. Without this step a validated factory would never reach
   the mint path (the ctor-time `factory_` is the override-only pointer, nullptr for plaintext/no-override).
**Rationale.** Fail closed at open() rather than discover the mismatch at handshake (a TLS profile + plain
factory would never authenticate; a plaintext profile + TLS factory would attempt an unanswered handshake)
— and crucially at open() rather than at the FSM cast, so the wrong-engine-default case fails clean too.

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
**Decision.** `run_accept_loop` (engine.cpp) skips its post-`async_accept` `async_handshake` step
(engine.cpp:789-797) when the session profile is `insecure_plain_tcp`, symmetric to D-7. This is **site #3**
of D-4's coordinated three-site acceptor change (the other two — the engine.cpp:664-681 profile-map
`insecure_plain_tcp` arm and the asio_listener Config `transport_kind` selector — are specified in D-4).
The minted accepted transport is a plain transport (D-4 acceptor path); the `dynamic_cast<TlsTransport*>`
(engine.cpp:783) legitimately returns null, so the cast+handshake block MUST be bypassed (not
fall through to the `close(); continue;` reject), and the path proceeds with a default `hr{}` (D-10).

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
   `live_peer_id_ = std::move(hr.peer_id)` **unconditionally** (session.cpp:410). On `insecure_plain_tcp`
   the implementation **MUST** leave `live_peer_id_ == nullopt` — it MUST NOT construct or pass a fake
   `handshake_result` whose empty `peer_id` would land in `live_peer_id_` as a *present-but-empty*
   `peer_identity`. (Today every reader is `is_mtls`-gated, so a present-but-empty identity is not a live
   hole — Gate A round 1 verified this; but **fail-closed-by-construction** requires the slot stay
   `nullopt`, so a future reader that drops the `is_mtls` guard cannot misread empty-identity as
   "authenticated".) This is a **MUST**, not a defensive recommendation: reconcile with data-model.md E-7
   ("`live_peer_id_` never set") — the docs now agree.
3. **Acceptor `Session::attach_accepted_transport(t, …)`** (session.cpp:534, assigns `live_peer_id_` at
   session.cpp:538) — the acceptor twin. The plaintext acceptor path (D-8) has no handshake `peer_id`; the
   implementation **MUST** invoke this handoff with no/empty identity and **MUST** leave `live_peer_id_ ==
   nullopt` for `insecure_plain_tcp` (same MUST as #2).
4. **Inbound authorization gate** (session.cpp:2137 `if (live_peer_id_.has_value() && is_mtls)`) — gated
   on `is_mtls`, which is **false** for plaintext (the SK→TK mapping never sets mtls for
   `insecure_plain_tcp`), so it falls into "case 3: non-mTLS → skip" exactly like `one_way_ca`.
**Other two `handshake_result` fields (P2-B — completeness).** Besides `peer_id`, `handshake_result`
carries `captured_pinset` and `negotiated_cipher` (tls_transport.hpp:51-56). Both are also **inert/absent
on plaintext** and Gate A round 1 verified both safe: `captured_pinset` is read only inside the FSM mTLS
authz arm (the `is_mtls` branch plaintext skips) and is "null IFF non-pinned"; `negotiated_cipher` is read
only in the `is_mtls` success-arm event emit (session.cpp ~3760-3826, `is_mtls`-gated). The plaintext path
passes a default `hr{}` and reaches none of these reads. A future maintainer adding a non-`is_mtls` read of
either field is hereby on notice — enumerated so D-10 covers **all three** `hr` fields, not just `peer_id`.
**Decision.** Plaintext skips the mTLS-gated `compid_authorization_policy` authz (already skipped for
non-mTLS); the cert-independent `check_comp_id` inbound 49/56 match (session.cpp:2499, default on) is
unaffected. Required code work: confirm `is_mtls` stays false (it will), pass a default `hr` on the
plaintext handoff, and enforce the `live_peer_id_`-stays-`nullopt` MUST (#2/#3). A 043 test MUST assert
that on a plaintext session `compid_authorization_policy` is **not** called and **no** peer-identity state
exists (`live_peer_id_ == nullopt`) — see SC-004.
**Limitation L-043-x.** The plaintext profile provides **no peer authentication**, and (acceptor twin)
plaintext accepted transports receive **no** TLS-validation event hooks (D-4/E-7); documented in B&L.

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

### D-13 — stale comment + assert generalisation + doc reconciliation
`engine.cpp:322` ("The engine exclusively uses asio_tls_transport") and the `tls_transport.hpp:69-71`
"every v1.0 transport is TLS-capable" partition note become stale and MUST be corrected (dated note, not
silent rewrite). The `security_profile.hpp` session-stub comment ("A future no-TLS / plaintext escape
value … will be appended …") is reconciled by D-9.
**Assert body coverage hole (P3-A — Gate A round 1).** Beyond the *comment*, the `assert_transport_on_
session_strand` body (engine.cpp:317-332) `dynamic_cast`s to `asio_tls_transport*` only and **skips the
check on a null downcast** (engine.cpp:325-326). An `asio_plain_transport` null-downcasts → the R8
strand-confinement invariant (socket executor == session strand) goes **unchecked** for every plaintext
accepted/reconnected transport. The 043 implement phase MUST close this: either add an
`asio_plain_transport*` arm to the assert (sampling its `socket_executor()`) or promote `socket_executor()`
to a base `Transport` accessor so the assert works for both. Debug-only (`#ifndef NDEBUG`); production is
unaffected, but it is a real loss of a deliberately-placed invariant on exactly the new transport type.

## Open items deferred to /plan-tasks (not blocking)

- Exact ownership of the auto-derived initiator plaintext factory (a Session-owned member vs an engine
  cache) — D-4 picks Session-owned; tasks.md pins the member.
- Whether `set_plaintext_profile(bool)` vs a `security_kind_` enum on the FSM (D-7) — cosmetic; tasks.md
  picks one.
- alloc-NFR: the plain read/write path is caller-buffer (no transport read-buffer alloc), same as TLS —
  no new alloc gate beyond the existing transport read alloc guard.
- **SC-005 witness mechanism (advisor catch).** A `[[deprecated]]` enumerator emits a *warning* only in an
  unsuppressed TU, so "observable at build time" needs a concrete witness. Pin in tasks.md: a dedicated
  one-line TU that selects `kind::insecure_plain_tcp` compiled with `-Werror=deprecated-declarations`,
  asserted to **fail compilation** (a CMake `try_compile`/negative-compile test, or the repo's existing
  negative-compile harness), OR a documented manual witness. Not a runtime gtest.
- **`kind()` is defaulted (D-5)** so it does NOT ripple to the ~11 `tests/session/` `TransportFactory`
  test doubles — they keep compiling and report `tls`. No census required; this is the deliberate reason
  D-5 chose defaulted over pure.
