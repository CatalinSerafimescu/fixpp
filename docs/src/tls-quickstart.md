# TLS quickstart (`fixpp::tls`)

> Operator-facing companion to [`specs/011-tls-policy/quickstart.md`](./specs/011-tls-policy/quickstart.md). The full Scenarios A/B/C/D/E walk-through lives in the spec quickstart; this page is the high-altitude orientation + the operator decision tree.

## What `fixpp::tls` ships in v1.0

| Type                       | Purpose                                                                                                              | Header                                |
| -------------------------- | -------------------------------------------------------------------------------------------------------------------- | ------------------------------------- |
| `cert_source`              | Pluggable credential-source interface (2 pure-virtuals — `load_credentials` awaitable + `load_trust_anchors`).         | `fixpp/tls/cert_source.hpp`           |
| `file_cert_source`         | v1.0 reference impl: PEM/DER from disk, optional encrypted-PEM passphrase, factory `make_file_cert_source(...)`.       | `fixpp/tls/file_cert_source.hpp`      |
| `Pinset`                   | FIXS-RC1 §5 add-then-remove rotation container; mid-session-mutable; lock-free reader path on the handshake hot path. | `fixpp/tls/pinset.hpp`                |
| `CipherPolicy`             | Compile-time allow-list per `[const §XII.3]`. Banned suites refused via `static_assert`.                              | `fixpp/tls/cipher_policy.hpp`         |
| `SecurityProfile`          | Enum + `SslCtxConfig` value + `make_ssl_ctx_config(...)` factory + `verify_peer(...)` predicate.                       | `fixpp/tls/security_profile.hpp`      |
| `Certificate`              | Value-typed view over a parsed peer/leaf cert.                                                                       | `fixpp/tls/certificate.hpp`           |
| `peer_identity`            | Owning value type the session-FSM module consumes for the T-041 CompID-to-TLS-identity binding.                       | `fixpp/tls/peer_identity.hpp`         |
| `error::tls_*` (16 slots)  | Error envelope per `[2g §6.6]` + the `tls_pin_empty_at_open` `/clarify` Q2 amendment.                                  | `fixpp/core/error.hpp` (slots 78..93) |

## Operator decision tree

```text
Are you running mutual TLS with a CA-issued counterparty cert?
├── Yes, with CA trust validation → Scenario A (mtls_ca + file_cert_source)
├── Yes, with FIXS-RC1 strict leaf pinning → Scenario B (mtls_pinned + per-counterparty Pinset)
└── Server-only auth (legacy) → SecurityProfile::one_way_ca is [[deprecated]] in v1.0 —
    migrate to mtls_pinned or mtls_ca per [FIXS RC1].

Need to keep private keys in an HSM / KMS / vault?
└── Implement cert_source against your provider (Scenario C); bind the signer to an
    executor distinct from the session executor — fixpp's cancellable_dispatch hop
    routes signing off the session strand automatically.

Need to enforce cipher discipline?
├── Build-time: a CipherPolicy variant that lists a banned suite refuses to compile
│              (Scenario D). The `static_assert` chain in cipher_policy.hpp is the gate.
└── Runtime (2i C-ABI bridge / config file): call CipherPolicy::is_allowed(string_view)
   (Scenario E). Returns false for substring matches against banned_tokens
   (RC4, DES, 3DES, MD5, DH_anon, NULL, EXPORT, TLS_RSA, CBC, SHA1, TLS_AES_128_CCM, 0RTT).
```

## Pinset rotation — operator workflow (Scenario B excerpt)

```cpp
// One shared_ptr<Pinset> per counterparty (recommended per /clarify Q5).
auto cp_X = std::make_shared<Pinset>();
cp_X->add(counterparty_X_current_leaf_cert);   // pass a Certificate

SessionConfig session_a;
session_a.counterparty_id = "X";
session_a.pinset = cp_X;
SessionConfig session_b;
session_b.counterparty_id = "X";
session_b.pinset = cp_X;                       // shared — rotation applies atomically.

// When the counterparty rotates: add new, wait for cutover, remove old.
cp_X->add(counterparty_X_new_leaf_cert);
// ... counterparty cuts over ...
cp_X->remove(sha256_of_their_old_leaf);
```

Both sessions transition atomically; an in-flight handshake captured mid-rotation completes against whichever set it started with. The `Pinset` writer holds `std::shared_mutex` (synchronous, NOT coroutine-suspending — per `[2g §6.5.2]` consolidated rationale); the reader path is lock-free via `std::atomic<std::shared_ptr<const pin_snapshot>>`.

## Validation predicate — `verify_peer`

`verify_peer(SslCtxConfig const&, std::span<const Certificate> peer_chain) noexcept -> expected_t<peer_identity>` short-circuits on the **first** violation in the canonical 10-step order per FR-020a:

1. Per-cert DER envelope > 16 KiB cap → `tls_cert_der_too_large`.
2. RSA key < 2048 bits → `tls_handshake_failed` (sub-reason `"rsa_under_min"`).
3. RSA key > `max_rsa_key_bits` (default 8192) → `tls_rsa_key_too_large`.
4. ECDSA non-P-256/P-384 → `tls_handshake_failed` (`"ecdsa_curve"`).
5. Chain depth > `max_chain_depth` (default 8) → `tls_handshake_failed` (`"chain_too_deep"`).
6. SAN cardinality > `max_san_entries` (default 64) → `tls_san_entries_exceeded`.
7. X.509 v1 → `tls_handshake_failed` (`"x509_v1"`).
8. Expiration vs `cfg.clock->now()` → `tls_handshake_failed` (`"expired"` / `"not_yet_valid"`).
9. Pinning (under `mtls_pinned` only): leaf SHA-256 not in the captured `cfg.pinset_snapshot` → `tls_pin_mismatch`.
10. Cipher: `CipherPolicy::is_allowed(negotiated)` → `tls_cipher_not_allowed`.

The pinning step scans the **captured** snapshot per the `[2g §6.5.1]` BINDING CONTRACT — `verify_peer` NEVER calls `cfg.pinset->find/contains/snapshot` mid-verification.

## What this feature does **not** do

- OpenSSL `SSL_CTX` construction + `async_handshake` coroutine + `SSL_VERIFY_PEER` callback wiring — owned by **2h-transport** (catalogue rows T-039/T-040 await its arrival).
- C-ABI bridge (`fixpp_cert_source_t`, `fixpp_pinset_t`, `fixpp_security_profile_t`, `FIXPP_ERR_TLS_*` coalescing) — owned by **2i**.
- Control-plane reload via gRPC `RotatePinset` / `ReloadCertSource` — owned by **2j**.
- TLS-event log / OTel span schema — owned by **2k**; `fixpp::tls` records call sites only.
- CompID-to-TLS-identity binding (T-041) — owned by **session/ Phase-4** consuming `peer_identity`.

## Reference

- Spec quickstart (full Scenarios A/B/C/D/E with compilable code): [`specs/011-tls-policy/quickstart.md`](./specs/011-tls-policy/quickstart.md)
- Design doc: [`../../.specify/architecture.md`](../../.specify/architecture.md) §4.6 + [`../../.specify/2g-tls.md`](../../.specify/2g-tls.md)
- Constitution: [`../../.specify/constitution.md`](../../.specify/constitution.md) Article XII (Security & TLS), Article XIV (pluggable interfaces), Article XV (banned patterns).
