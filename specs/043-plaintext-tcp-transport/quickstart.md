# Quickstart: Plaintext TCP transport (043)

> **⚠ Insecure.** `insecure_plain_tcp` disables transport security entirely — **no encryption, no peer
> authentication, no integrity protection**. Use it ONLY when the link is secured beneath the application
> (colocation cross-connect, VPN/IPsec tunnel) or for engine benchmarking. Prefer `mtls_pinned` / `mtls_ca`
> for any exposed link. Selecting it emits a compile-time deprecation-class diagnostic by design (FR-006).

## Initiator (the common case)

```cpp
fixpp::session::SessionConfig cfg;
cfg.dictionary       = my_dictionary;
cfg.sender_comp_id   = "CLIENT";
cfg.target_comp_id   = "BROKER";
cfg.reconnect_endpoint = fixpp::transport::Endpoint{"broker.colo.internal", 9876};

// Opt in to plaintext. No cert_source, no SslCtxConfig, no transport_factory_override needed —
// the engine auto-derives a built-in plaintext factory from the profile (FR-003a).
cfg.security_profile = { fixpp::session::SecurityProfile::kind::insecure_plain_tcp };  // <-- warns

fixpp::core::EngineConfig engine;
engine.executor = my_executor;
engine.clock    = my_clock;
// engine.default_transport_factory may remain the TLS factory; it is bypassed for this session.

fixpp::session::Session s{engine, cfg};
co_await s.open();   // connects over plain TCP, no handshake, then Logon
```

## Acceptor

```cpp
fixpp::session::SessionConfig cfg;
cfg.role               = fixpp::session::session_role::acceptor;
cfg.dictionary         = my_dictionary;
cfg.reconnect_endpoint = fixpp::transport::Endpoint{"0.0.0.0", 9876};   // repurposed as bind endpoint
cfg.security_profile   = { fixpp::session::SecurityProfile::kind::insecure_plain_tcp };  // <-- warns
// run_accept_loop builds a plaintext listener; accepted connections skip the TLS handshake.
```

## What changes vs a TLS session

| Aspect | TLS profile | `insecure_plain_tcp` |
|--------|-------------|----------------------|
| Transport | `asio_tls_transport` | `asio_plain_transport` (plain socket) |
| Factory | configured/`default_transport_factory` | auto-derived built-in plaintext factory (or explicit plaintext override) |
| Handshake | TLS handshake after connect | **none** (connect → Logon) |
| `close()` | TLS close-notify (bounded) | plain `socket.close()` |
| Peer auth | cert identity (mTLS); CompID↔identity binding (015) | **none** (binding skipped, L-043-x) |
| CompID check | `check_comp_id` (49/56 match) | `check_comp_id` **still applies** |
| `EncryptMethod(98)≠0` | rejected | **still rejected** (transport plaintext ≠ app-layer encryption) |

## Misconfiguration is rejected at open()

```cpp
// Plaintext profile + an explicit TLS factory override → open() returns invalid_session_config.
cfg.security_profile        = { ...::insecure_plain_tcp };
cfg.transport_factory_override = my_tls_factory;        // kind()==tls, mismatches → REJECTED

// A TLS profile + an explicit plaintext factory override → likewise rejected.
```

## Verifying it works (test harness)

- Stand up a plaintext acceptor + initiator on a loopback `asio::ip::tcp::acceptor`; assert a Logon/Logout
  round trip completes and that no TLS ClientHello byte is emitted (SC-001).
- Assert a default/`unset` profile is still rejected at `open()` (SC-002); both override mismatch
  directions AND a TLS profile with a plaintext engine-default factory (no override) are rejected, matched
  + no-override pairings open (SC-003).
- Confirm the deprecation diagnostic fires when selecting the enumerator (SC-005).
- Confirm a `Transport::Config` TCP knob takes effect and `close()` returns without a `tls_close_timeout`
  wait (SC-008).
