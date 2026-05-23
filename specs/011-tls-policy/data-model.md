# Data Model — 011-tls-policy

**Date**: 2026-05-23
**Plan**: [plan.md](plan.md)
**Spec**: [spec.md](spec.md)
**Design anchor**: `.specify/2g-tls.md` v0.4 §4 (Public C++ API).

This is the **entity model** for the TLS-policy core. Each entity has: name, fields, relationships, validation rules. State transitions are noted only where applicable (Pinset). The model is the **contract surface** the implementation in `src/tls/*.cpp` must satisfy and the test suite in `tests/tls/*` must witness.

---

## E-1 — `cert_source` (interface)

- **Kind**: pure-virtual interface; ≤ 5 pure-virtuals per `[const §XIV.2]` (ships at 2; wide margin for HSM/KMS user-side extension).
- **Header**: `include/fixpp/tls/cert_source.hpp`.
- **Pure-virtual methods**:
  - `virtual asio::awaitable<expected_t<local_credentials>> load_credentials() = 0;`
  - `virtual std::span<const Certificate> load_trust_anchors() const noexcept = 0;`
- **Nested config**: `cert_source::Config` (passed to `make_file_cert_source` and embedded in concrete impls). Fields:
  - `std::filesystem::path leaf_path` (file impls)
  - `std::filesystem::path chain_path` (file impls)
  - `std::filesystem::path ca_bundle_path` (file impls)
  - `std::function<expected_t<std::string>()> password_callback` (optional; for encrypted PEM)
  - `std::size_t max_rsa_key_bits = 8192` (DoS upper-cap)
  - `std::size_t max_cert_der_bytes = 16 * 1024` (DoS per-cert envelope)
  - `std::size_t max_san_entries = 64` (DoS SAN cardinality)
  - `std::size_t max_chain_depth = 8` (matches OpenSSL default)
- **Relationships**: implemented by `file_cert_source` (the v1.0 default); held by `EngineConfig::default_cert_source` (`std::shared_ptr<cert_source>`) and `SessionConfig::cert_source` (also `shared_ptr<cert_source>`). Consumed by `make_ssl_ctx_config` via the configured cert source's `load_credentials` + `load_trust_anchors`.
- **Frozen at session open** per `[arch §5.6]`. No mid-session swap.

## E-2 — `local_credentials` (value type)

- **Kind**: aggregate value-type returned by `cert_source::load_credentials`.
- **Header**: `include/fixpp/tls/cert_source.hpp` (same TU; tightly coupled).
- **Fields**:
  - `Certificate leaf` (parsed leaf cert)
  - `std::pmr::vector<Certificate> chain` (parsed chain; PMR-allocated; cold path)
  - `std::variant<software_key_ref, async_signer_ref> signer` (software for in-process keys; async for HSM/KMS offload)
- **Validation rules**:
  - `leaf.der.size() ≤ Config::max_cert_der_bytes` (rejected at parse time with `tls_cert_der_too_large` else)
  - `chain.size() ≤ Config::max_chain_depth - 1` (leaf + chain ≤ depth cap)
  - `signer` MUST be one of the two variants; default-construction is NOT permitted (no implicit empty variant).
- **Lifetime**: owns its bytes (the PMR `vector<Certificate>` is the owner of the chain DER spans; the leaf's DER is held by an `owning_message_t<>`-shaped PMR string per `[2c §4.8]` precedent).

## E-3 — `software_key_ref` (value type)

- **Kind**: opaque handle to an in-process private key.
- **Fields**:
  - `std::shared_ptr<void> key_handle` (type-erased; the file_cert_source default fills this with an `EVP_PKEY*` wrapped in a `shared_ptr` with a custom OpenSSL deleter)
  - `signature_algorithm alg` (enum: `ecdsa_p256`, `ecdsa_p384`, `rsa_pss_sha256`, `rsa_pss_sha384`, `rsa_pss_sha512`)
- **Validation**: `alg` MUST be one of the allow-listed algorithms per `[const §XII.3]` signature-algorithms list.

## E-4 — `async_signer_ref` (value type)

- **Kind**: awaitable signer for HSM / KMS / vault offload.
- **Fields**:
  - `std::shared_ptr<async_signer> impl` (user-implemented or library-provided; awaitable signing entry point)
  - `asio::any_io_executor executor` (binds the impl's signing work to an executor distinct from the session strand per `[2d §7.5]`)
  - `signature_algorithm alg` (same enum as E-3)
- **Validation**: `executor` MUST NOT be null; the implementation contract requires signing work runs on this executor, not on the session strand.

## E-5 — `Pinset` (mutable container)

- **Kind**: stateful container with mid-session-mutable rotation API per `[arch §5.6]` carve-out.
- **Header**: `include/fixpp/tls/pinset.hpp`.
- **Owned fields**:
  - `mutable std::shared_mutex writer_mu_` (serialises `add` + `remove`; rotation is synchronous)
  - `std::atomic<std::shared_ptr<const pin_snapshot>> published_` (the snapshot readers acquire; lock-free read per `[2g §6.2]`)
  - `Config cfg_` (`max_pins = 16` by default, operator-raisable; key type `pin_fingerprint = std::array<std::byte, 32>` — SHA-256-of-leaf-DER per `/clarify` Q4)
- **API**:
  - `expected_t<void> add(pin_fingerprint fp) noexcept;` — refuses if at-cap with `pin_cap_exceeded`; otherwise creates new snapshot containing existing + fp and publishes.
  - `expected_t<void> remove(pin_fingerprint fp) noexcept;` — refuses if not-present; otherwise creates new snapshot omitting fp and publishes; old fp remains usable through any outstanding `pin_view` until that view goes out of scope.
  - `pin_view find(pin_fingerprint fp) const noexcept [[clang::lifetimebound]];` — hot path; lock-free `published_.load(memory_order_acquire)` + linear scan ≤ 16 entries; returns value-typed `pin_view` carrying the snapshot `shared_ptr` so the matched entry survives concurrent rotation.
  - `bool contains(pin_fingerprint fp) const noexcept;` — convenience; same hot-path cost as `find`.
  - `std::shared_ptr<const pin_snapshot> snapshot() const noexcept;` — bare snapshot accessor for tests and the deterministic add-then-remove witness per `[2g §9 seam #1]`.
- **Validation rules**:
  - At-cap: `published_->size() == max_pins` rejects `add` with `pin_cap_exceeded`.
  - Absent-on-remove: rejects with `pin_not_present`.
  - Concurrent `find` + `remove`: the `find`'s captured snapshot is unaffected by a subsequent publication; the `pin_view`'s `shared_ptr<const pin_snapshot>` keeps the matched entry alive until the view goes out of scope.
- **State transitions**:
  - Initial state: empty snapshot (`size() == 0`).
  - On `add(fp1)` from empty: snapshot becomes `{fp1}`.
  - On `add(fp2)` from `{fp1}`: snapshot becomes `{fp1, fp2}` — both usable simultaneously (the canonical rotation window per FIXS-RC1 §5).
  - On `remove(fp1)` from `{fp1, fp2}`: snapshot becomes `{fp2}`; any outstanding `pin_view{fp1}` from earlier `find` remains valid for the view's lifetime.
- **Frozen?** No — explicit `[arch §5.6]` carve-out; rotation is the ONLY mid-session-mutable TLS surface.

## E-6 — `pin_snapshot` (immutable value)

- **Kind**: immutable captured state of a Pinset at a publication point.
- **Header**: `include/fixpp/tls/pinset.hpp` (same TU).
- **Fields**:
  - `std::array<pin_fingerprint, /* up to Config::max_pins */> entries_`
  - `std::size_t count_` (active entry count; `≤ max_pins`)
- **Lifetime**: owned by `std::shared_ptr<const pin_snapshot>`; kept alive while any `pin_view` holding it exists.

## E-7 — `pin_view` (handle)

- **Kind**: value-typed handle returned by `Pinset::find`. Lifetime-extends the snapshot containing the matched pin.
- **Header**: `include/fixpp/tls/pinset.hpp` (same TU).
- **Fields**:
  - `std::shared_ptr<const pin_snapshot> snapshot_`
  - `const pin_fingerprint* matched_` (pointer into `snapshot_->entries_`; the `shared_ptr` guarantees lifetime)
- **Accessors**:
  - `bool valid() const noexcept;` — true if a match was found
  - `pin_fingerprint fingerprint() const noexcept [[clang::lifetimebound]];` — UB if `!valid()`
- **Invariant**: a `pin_view` constructed from `find(fp)` that returns `valid() == true` remains valid for the view's lifetime regardless of any subsequent `Pinset::remove(fp)` calls.

## E-8 — `CipherPolicy` (compile-time policy + runtime predicate)

- **Kind**: compile-time `static_assert`-enforced allow-list + `constexpr` runtime-string predicate.
- **Header**: `include/fixpp/tls/cipher_policy.hpp`.
- **Static members**:
  - `static constexpr std::array<std::string_view, 3> tls13_suites = { "TLS_AES_128_GCM_SHA256", "TLS_AES_256_GCM_SHA384", "TLS_CHACHA20_POLY1305_SHA256" };`
  - `static constexpr std::array<std::string_view, 6> tls12_suites = { /* ECDHE-(RSA|ECDSA) × (AES128-GCM | AES256-GCM | CHACHA20-POLY1305) */ };`
  - `static constexpr std::array<std::string_view, 3> kx_groups = { "X25519", "P-256", "P-384" };`
  - `static constexpr std::array<std::string_view, 5> sig_algs = { "ecdsa_secp256r1_sha256", "ecdsa_secp384r1_sha384", "rsa_pss_rsae_sha256", "rsa_pss_rsae_sha384", "rsa_pss_rsae_sha512" };`
  - `static constexpr std::array<std::string_view, 13> banned_tokens = { "RC4", "DES", "3DES", "MD5", "DH_anon", "NULL", "EXPORT", "CBC", "SHA-1", "RSA-1024", "TLS-1.0", "TLS-1.1", "SSL" };`
- **Compile-time validation**:
  - `static_assert(!contains_any(tls13_suites, banned_tokens), "TLS 1.3 suite list contains a banned token");`
  - `static_assert(!contains_any(tls12_suites, banned_tokens), "TLS 1.2 suite list contains a banned token");`
- **Runtime predicate**:
  - `static constexpr bool is_allowed(std::string_view suite_or_token) noexcept;` — returns `true` iff `suite_or_token` is on `tls13_suites ∪ tls12_suites` AND NOT on `banned_tokens`. Exposed for the 2i C-ABI bridge.
- **Validation rules**:
  - Banned token in any allow-list → compile fails (`static_assert` diagnostic identifies the violation).
  - Runtime string outside the allow-list → `is_allowed` returns `false`; the 2i C bridge translates to `tls_cipher_not_allowed`.

## E-9 — `SecurityProfile` (enum)

- **Kind**: scoped enum.
- **Header**: `include/fixpp/tls/security_profile.hpp`.
- **Values**:
  - `mtls_ca = 1` — CA-trust on peer cert; recommended starting profile.
  - `mtls_pinned = 2` — leaf-cert pinning; required for FIXS-conformant deployments.
  - `one_way_ca [[deprecated("one_way_ca is legacy interop; prefer mtls_pinned or mtls_ca")]]` — server-cert-only TLS; legacy interop.
- **Validation rule**: `Session` construction requires an explicit `SecurityProfile` choice — there is no implicit default per `[const §XII.5]`. `make_ssl_ctx_config(SecurityProfile::unset, ...)` rejects with `tls_invalid_security_profile` (the `unset` row in `[2g §4.5]` table line 118).

## E-10 — `SslCtxConfig` (value type — the contract 2h consumes)

- **Kind**: value-type description of the `SSL_CTX` configuration 2h-transport must apply.
- **Header**: `include/fixpp/tls/security_profile.hpp` (same TU).
- **Fields**:
  - `SecurityProfile profile`
  - `int min_proto_version = TLS1_2_VERSION` (`/clarify` Q1: lock TLS 1.2 as minimum)
  - `int max_proto_version = TLS1_3_VERSION` (`/clarify` Q1: lock TLS 1.3 as maximum / preferred)
  - `std::span<const std::string_view> cipher_list` (the TLS 1.2 allow-list; TLS 1.3 suites are passed via the separate `ciphersuites` field)
  - `std::span<const std::string_view> tls13_ciphersuites` (the TLS 1.3 allow-list)
  - `std::shared_ptr<cert_source> source`
  - `std::shared_ptr<Pinset> pinset` (null permitted only under `mtls_ca` + `one_way_ca`; `mtls_pinned` MUST have non-null non-empty Pinset)
  - `std::shared_ptr<Clock> clock` (the session-scoped `effective_clock` per `[2d §7.9]`)
  - `cert_source::Config validation_caps` (the DoS bounds carried into `verify_peer`)
  - `int ssl_verify_flags` (e.g., `SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT` for the acceptor side)
- **Factory**: `expected_t<SslCtxConfig> make_ssl_ctx_config(SecurityProfile, std::shared_ptr<cert_source>, std::shared_ptr<Pinset>, std::shared_ptr<Clock>, cert_source::Config validation_caps) noexcept;`
- **Factory validation**:
  - `mtls_pinned` + null Pinset → `tls_invalid_security_profile` (E-9 contract).
  - `mtls_pinned` + non-null but empty Pinset → `tls_pin_empty_at_open` (`/clarify` Q2; new variant).
  - `one_way_ca` + non-null Pinset → `tls_invalid_security_profile` (deprecated profile + pin is contradictory).
  - `unset` → `tls_invalid_security_profile`.
  - null `clock` → `tls_invalid_security_profile`.

## E-11 — `Certificate` (value-typed view)

- **Kind**: value-typed view over a parsed peer cert.
- **Header**: `include/fixpp/tls/certificate.hpp`.
- **Fields**:
  - `std::span<const std::byte> raw_der_` (back-pointer; the DER bytes are owned by the `cert_source` impl, NOT by `Certificate`)
  - `std::string_view subject_dn_` (back-pointer into raw_der_)
  - `std::string_view issuer_dn_` (back-pointer)
  - `std::span<const std::string_view> san_dns_names_` (back-pointers; cap-bounded ≤ `max_san_entries`)
  - `std::span<const std::string_view> san_uris_` (back-pointers; cap-bounded)
  - `pin_fingerprint sha256_` (32 bytes; SHA-256-of-raw_der_)
  - `int x509_version_` (1, 2, or 3; v1 rejected at validate)
  - `system_clock_time_point not_before_`
  - `system_clock_time_point not_after_`
  - `signature_algorithm alg_` (same enum as E-3 / E-4)
  - `std::size_t rsa_key_bits_` (only meaningful when `alg_` ∈ {`rsa_pss_*`}; else 0)
  - `ecdsa_curve curve_` (only meaningful when `alg_` ∈ {`ecdsa_*`}; else `unspecified`)
- **Accessors** — every non-owning view accessor carries `[[clang::lifetimebound]]` at the abstract-base declaration site per D-11 / `[arch §5.5]`.
- **Validation rules** (enforced at parse; see also `verify_peer` E-13):
  - `raw_der_.size() ≤ Config::max_cert_der_bytes`
  - `san_dns_names_.size() + san_uris_.size() ≤ Config::max_san_entries`
  - `x509_version_ ∈ {2, 3}`

## E-12 — `peer_identity` (value type; T-041 cross-cut)

- **Kind**: owning value type derived from a verified peer cert; the value the session-FSM Phase-4 module consumes for the CompID-to-TLS-identity binding.
- **Header**: `include/fixpp/tls/peer_identity.hpp`.
- **Fields**:
  - `std::pmr::string subject_dn_owned_` (owned; PMR-allocated)
  - `std::pmr::vector<std::pmr::string> san_dns_names_owned_` (owned; cap-bounded ≤ `max_san_entries` per D-10)
  - `std::pmr::vector<std::pmr::string> san_uris_owned_` (owned; cap-bounded)
  - `pin_fingerprint sha256_` (32 bytes)
  - `system_clock_time_point not_after_` (carried for the session FSM's effective-clock-aware expiry checks)
- **Construction**: only via `verify_peer` (E-13) which builds the owning value from the view-typed `Certificate` on accept.

## E-13 — `verify_peer` (validation predicate)

- **Kind**: free function (not a class method); the validation predicate 2h-transport's `SSL_VERIFY_PEER` callback invokes.
- **Header**: `include/fixpp/tls/security_profile.hpp` (or a dedicated `verify_peer.hpp`; final placement at /implement time).
- **Signature**: `expected_t<peer_identity> verify_peer(SslCtxConfig const& cfg, Certificate const& leaf, std::span<const Certificate> chain) noexcept;`
- **Behaviour** — evaluates the documented short-circuit order per FR-020a (`/clarify` Q3):
  1. Per-cert DER envelope: `leaf.raw_der_.size() ≤ cfg.validation_caps.max_cert_der_bytes`. Violation → `tls_cert_der_too_large`.
  2. RSA key bounds (lower 2048): if `leaf.alg_` is RSA-PSS, `leaf.rsa_key_bits_ ≥ 2048`. Violation → `tls_rsa_key_too_small`.
  3. RSA key bounds (upper `max_rsa_key_bits`): if RSA-PSS, `leaf.rsa_key_bits_ ≤ cfg.validation_caps.max_rsa_key_bits`. Violation → `tls_rsa_key_too_large`.
  4. ECDSA curve: if `leaf.alg_` is ECDSA, `leaf.curve_ ∈ {p256, p384}`. Violation → `tls_cert_invalid` (sub-reason: curve).
  5. Chain depth: `chain.size() + 1 ≤ cfg.validation_caps.max_chain_depth`. Violation → `tls_cert_chain_too_deep`.
  6. SAN cardinality: `leaf.san_dns_names_.size() + leaf.san_uris_.size() ≤ cfg.validation_caps.max_san_entries`. Violation → `tls_san_entries_exceeded`.
  7. X.509 version: `leaf.x509_version_ ∈ {2, 3}`. Violation → `tls_cert_invalid` (sub-reason: x509_v1).
  8. Expiration: `leaf.not_before_ ≤ cfg.clock->now() ≤ leaf.not_after_`. Violation → `tls_cert_expired`.
  9. Cipher: the negotiated cipher (passed via a side channel from `SSL_get_cipher_name` in 2h) MUST satisfy `CipherPolicy::is_allowed`. Violation → `tls_cipher_not_allowed`. (This row may be moved to 2h depending on the OpenSSL callback shape; if 2h applies it, `verify_peer` skips this step.)
  10. Pinning (only under `mtls_pinned`): `cfg.pinset->contains(leaf.sha256_)`. Violation → `tls_cert_pin_mismatch`.
- **On accept**: returns `peer_identity` built from `leaf` (PMR-copies the subject DN + SANs into owning storage). 

## E-14 — `error::tls_*` variants (extension of `fixpp::core::error`)

- **Kind**: enum-variant extensions to the existing `fixpp::core::error` enum.
- **Header**: appended to `include/fixpp/core/error.hpp` at the next free slots (slot allocation respects prior 005/008/009/010 pinning; 011 takes the next contiguous range after slot 77).
- **Variants** (FR-025 minimum; final slot numbers determined at `/speckit-implement`):
  - `tls_load_failed`, `tls_load_cancelled`, `tls_cert_invalid`, `tls_cert_expired`, `tls_cert_pin_mismatch`, `tls_pin_empty_at_open` (`/clarify` Q2), `tls_cert_chain_too_deep`, `tls_cert_der_too_large`, `tls_rsa_key_too_large`, `tls_rsa_key_too_small`, `tls_san_entries_exceeded`, `tls_profile_mismatch` / `tls_invalid_security_profile`, `tls_cipher_not_allowed`, `pin_cap_exceeded`, `pin_not_present`.
- **C-ABI coalescing**: each variant projects to a `FIXPP_ERR_TLS_*` C-ABI code via the per-doc-prefix coalescing rule (the actual C ABI is owned by 2i; this feature publishes the C++ source-of-truth and the per-doc-prefix grouping name).

## Relationships overview

```
        EngineConfig ─────────────────┐
              │                       │
              │ default_cert_source   │ clock
              ▼                       ▼
        cert_source ◄───── SessionConfig ─────► Clock
              │                       │
              │ load_credentials      │ cert_source (override)
              ▼                       │ pinset (per-counterparty shared_ptr)
        local_credentials             │
       { leaf, chain, signer }        │
                                      ▼
                              make_ssl_ctx_config(profile, source, pinset, clock, caps)
                                      │ returns
                                      ▼
                              SslCtxConfig ──────► consumed by 2h-transport
                                      │           (builds SSL_CTX, wires SSL_VERIFY_PEER)
                                      │
                                      │ verify_peer(cfg, leaf, chain)
                                      ▼
                              peer_identity ──────► consumed by session/ Phase-4
                                                    (T-041 CompID binding)

        Pinset ◄── shared_ptr ── SessionConfig (one Pinset per counterparty)
           │
           │ add / remove (synchronous, shared_mutex writer)
           │ find (lock-free reader; returns pin_view)
           ▼
       pin_snapshot (immutable)
           │
           │ kept alive by
           ▼
       pin_view (operator scope)
```
