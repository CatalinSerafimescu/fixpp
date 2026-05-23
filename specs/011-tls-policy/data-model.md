# Data Model — 011-tls-policy

**Date**: 2026-05-23
**Plan**: [plan.md](plan.md)
**Spec**: [spec.md](spec.md)
**Design anchor**: `.specify/2g-tls.md` v0.4 §4 (Public C++ API). Entity shapes below are **re-emitted verbatim** from 2g §4.1 / §4.2 / §4.3 / §4.4 / §4.5 + §6.6 (the signed-off Phase-2 Gate A r3 surface), with the five `/clarify` Session 2026-05-23 decisions layered as additive amendments only.

This is the **entity model** for the TLS-policy core. Each entity has: name, fields, relationships, validation rules. State transitions are noted only where applicable (Pinset). The model is the **contract surface** the implementation in `src/tls/*.cpp` must satisfy and the test suite in `tests/tls/*` must witness.

---

## E-1 — `cert_source` (interface) — verbatim from `[2g §4.1]`

- **Kind**: pure-virtual interface; ≤ 5 pure-virtuals per `[const §XIV.2]` (ships at 2; wide margin for HSM/KMS user-side extension).
- **Header**: `include/fixpp/tls/cert_source.hpp`.
- **Pure-virtual methods** (re-emitted verbatim from `[2g §4.1]` lines 277-290 — Codex P1-1 close):
  - `[[nodiscard]] virtual asio::awaitable<core::expected_t<local_credentials>> load_credentials() = 0;`
  - `[[nodiscard]] virtual core::expected_t<std::span<const Certificate>> load_trust_anchors() [[clang::lifetimebound]] = 0;`
  - **CRITICAL** (Codex P1-1 sub-finding 1): `load_trust_anchors` returns `core::expected_t<std::span<const Certificate>>`, NOT bare `std::span<const Certificate>` — per `[2g §4.1]` line 288 the expected_t return surfaces FIXS RC1 strict-pinned-only error cases without a separate failure channel.
- **`cert_source::Config`** (used by `file_cert_source` impl; embedded in concrete impls — see E-1a). The abstract base does NOT publish `Config`; `file_cert_source::Config` is impl-specific (see E-1a). DoS-cap fields (`max_rsa_key_bits`, `max_cert_der_bytes`, `max_san_entries`, `max_chain_depth`) are reachable through the impl's `Config` and are consumed by `verify_peer` via the cert_source reference (`[2g §4.2]` line 320-326).
- **Relationships**: implemented by `file_cert_source` (the v1.0 default); held by `EngineConfig::default_cert_source` (`std::shared_ptr<cert_source>`) and `SessionConfig::cert_source` (also `shared_ptr<cert_source>`). Consumed by `make_ssl_ctx_config` via the configured cert source.
- **Frozen at session open** per `[arch §5.6]`. No mid-session swap.

## E-1a — `file_cert_source::Config` — verbatim from `[2g §4.2]`

- **Kind**: aggregate value-type, embedded in the `file_cert_source` impl (NOT on the abstract base).
- **Header**: `include/fixpp/tls/file_cert_source.hpp` (or appended to `cert_source.hpp`).
- **Fields** (re-emitted verbatim from `[2g §4.2]` lines 316-327 — Codex P1-1 sub-finding 2 + 3 close):
  - `std::string leaf_path` — PEM or DER (auto-detect by file extension + magic byte).
  - `std::string chain_path` — PEM bundle; intermediates only or leaf+intermediates (deduped on load).
  - `std::string private_key_path` — PEM or DER; PKCS#8 or RFC1421-style. **Restored** per Codex P1-1 sub-finding 2 — `[2g §4.2]` line 319 declares this field; dropping it silently severs `file_cert_source`'s ability to load the local private key while still claiming mTLS support.
  - `std::string ca_bundle_path` — PEM bundle for CA trust anchors. May be empty for `SecurityProfile::mtls_pinned`.
  - `std::function<std::string()> password_cb` — Optional; called once at construction-time per `[arch §5.3]` carve-out. May be empty.
  - `std::pmr::memory_resource* mr {nullptr}` — PMR resource for parsed-cert storage. nullptr → engine default; `make_file_cert_source`'s `mr` parameter takes precedence over the field.
  - `std::size_t max_chain_depth {8}` — `[2g §1.1]` cap (chain depth).
  - `std::size_t max_rsa_key_bits {8192}` — `[2g §1.1]` DoS cap; RSA upper bound (lower bound 2048 from `[FIXS §3.4]`).
  - `std::size_t max_cert_der_bytes {16 * 1024}` — `[2g §1.1]` DoS cap; per-cert ASN.1 envelope size.
  - `std::size_t max_san_entries {64}` — `[2g §1.1]` DoS cap; SAN list cardinality.

## E-2 — `local_credentials` (value type) — verbatim from `[2g §4.1]`

- **Kind**: aggregate value-type returned by `cert_source::load_credentials`.
- **Header**: `include/fixpp/tls/cert_source.hpp`.
- **Fields** (re-emitted verbatim from `[2g §4.1]` lines 251-255 — Codex P1-1 sub-finding 3 close):
  - `Certificate leaf [[clang::lifetimebound]]` — VALUE-typed leaf certificate; the value's view fields alias the producing cert_source's storage.
  - `std::span<const Certificate> chain [[clang::lifetimebound]]` — **VIEW** into cert_source-owned storage, root last. **Not** a `std::pmr::vector<Certificate>` owning vector (Codex P1-1 sub-finding 3 close — owning vector contradicts the `[2g §4.6]` ownership rule that the chain's Certificate views are *this-bounded by the cert_source instance).
  - `std::variant<software_key_ref, async_signer_ref> signer` — software handle OR awaitable HSM oracle.
- **Validation rules**:
  - `leaf.raw_der().size() ≤ Config::max_cert_der_bytes` (parser refuses with `tls_cert_load_failed` else).
  - `chain.size() + 1 ≤ Config::max_chain_depth` (leaf + chain ≤ depth cap).
  - `signer` MUST be one of the two variants; default-construction is NOT permitted.
- **Lifetime**: the leaf's and chain's views alias cert_source-owned storage; the `signer` is owning (software handle's RAII or awaitable closure).

## E-3 — `software_key_ref` (value type) — verbatim from `[2g §4.1]`

- **Kind**: engine-internal handle to a parsed private key the implementation owns.
- **Fields** (re-emitted verbatim from `[2g §4.1]` lines 218-221):
  - `detail::private_key_handle handle` — `[[clang::lifetimebound]]` on accessor; *this-bounded.
  - `int ossl_pkey_id` — OpenSSL `EVP_PKEY_id` (e.g., `EVP_PKEY_RSA`, `EVP_PKEY_EC`).
- **Validation**: lifetime is bounded by the holding `cert_source` instance.

## E-4 — `async_signer_ref` (value type) — verbatim from `[2g §4.1]`

- **Kind**: HSM-style awaitable signing oracle.
- **Fields** (re-emitted verbatim from `[2g §4.1]` lines 239-244):
  - `sign_fn sign` — `std::function<asio::awaitable<core::expected_t<sign_response>>(sign_request const&)>`. Never invoked synchronously on the session strand without a `cancellable_dispatch` hop.
- **Associated types** (re-emitted verbatim from `[2g §4.1]` lines 230-237):
  - `sign_request { std::span<const std::byte> tbs [[clang::lifetimebound]]; int sig_alg; }`
  - `sign_response { std::pmr::vector<std::byte> signature; }` — owning, PMR-allocated by the impl.
- **Validation**: the implementation contract requires signing work runs OFF the session strand (via `cancellable_dispatch` to a non-session executor per `[2d §6.5]` / `[2d §7.5]`).

## E-5 — `Pinset` (mutable container) — verbatim from `[2g §4.3]`

- **Kind**: stateful container with mid-session-mutable rotation API per `[arch §5.6]` carve-out.
- **Header**: `include/fixpp/tls/pinset.hpp`.
- **Granularity recommendation** (`/clarify` Q5 / FR-009a — NEW-P3-2 close): one `Pinset` instance per counterparty is the **recommended** operator pattern; the same `std::shared_ptr<Pinset>` is shared across all `SessionConfig`s targeting that counterparty so rotation applies atomically to every same-counterparty session on the next handshake. The engine does NOT enforce granularity; per-session is permitted; engine-wide sharing across DIFFERENT counterparties is discouraged in operator-facing docs because it conflates trust authorities.
- **`Config`** (re-emitted verbatim from `[2g §4.3]` lines 437-460):
  - `std::pmr::memory_resource* mr {nullptr}` — Lifetime contract per `[2g §4.6]` (round-3 P1-1 close): **`mr` MUST outlive every `shared_ptr<const pin_snapshot>` the `Pinset` ever publishes** — i.e., the union of `Pinset` instance lifetime AND every reader-held snapshot lifetime, NOT merely the `Pinset` instance itself. The default path (null → engine-resolved) is satisfied by construction; user-supplied `mr` is a documented caller contract. `[2g §9 seam #18]` exercises this contract under ASan + TSan.
  - `std::size_t max_pins {16}` — `[2g §1.1]` cap.
- **API** (re-emitted verbatim from `[2g §4.3]` lines 486-518 — Codex P1-2 + NEW-P1-4 close):
  - `[[nodiscard]] core::expected_t<void> add(Certificate const& cert);` — **Input is a Certificate** (NOT a bare `pin_fingerprint`); the Certificate is consumed for its SHA-256 + subject_dn + SAN list at add() time and its diagnostic fields are PMR-copied into the snapshot. Refuses with `tls_pinset_capacity_exhausted` at-cap, `tls_pin_already_present` on duplicate.
  - `[[nodiscard]] core::expected_t<void> remove(std::array<std::byte, 32> const& sha256);` — Keys on the fingerprint (remove does NOT need diagnostic context). Refuses with `tls_pin_not_found` if absent.
  - `[[nodiscard]] pin_view find(std::array<std::byte, 32> const& sha256) const noexcept;` — Hot-path lookup; one acquire-load on the snapshot atomic + linear scan ≤ 16 entries; returns value-typed `pin_view` carrying the snapshot `shared_ptr`. ≤ 130 ns p99 on the lock-free floor per `[2g §6.3]`.
  - `[[nodiscard]] bool contains(std::array<std::byte, 32> const& sha256) const noexcept;` — Convenience; same hot-path cost as `find`.
  - `[[nodiscard]] std::shared_ptr<const pin_snapshot> snapshot() const noexcept;` — **The published reachability for handshake-time pinset access** per `[2g §6.5.1]` BINDING CONTRACT — 2h's handshake wiring calls this ONCE at handshake start and scans the captured snapshot for the entire handshake. `verify_peer` consumes the captured snapshot via `SslCtxConfig::pinset_snapshot` (NEW-P1-1 close); `verify_peer` NEVER calls `cfg.pinset->find/contains/snapshot` itself.
- **Validation rules**:
  - At-cap: `published_snapshot->size() == max_pins` rejects `add` with `tls_pinset_capacity_exhausted`.
  - Duplicate: pin already present (by SHA-256) rejects `add` with `tls_pin_already_present`.
  - Absent-on-remove: rejects with `tls_pin_not_found`.
  - Concurrent `find` + `remove`: the `find`'s captured snapshot is unaffected by a subsequent publication; the `pin_view`'s `shared_ptr<const pin_snapshot>` keeps the matched entry alive until the view goes out of scope.
- **State transitions**:
  - Initial: empty snapshot (`size() == 0`).
  - On `add(cert1)` from empty: snapshot becomes `{pin{sha256(cert1.der), cert1.subject_dn, cert1.san_dns, now()}}`.
  - On `add(cert2)` from `{pin1}`: snapshot becomes `{pin1, pin2}` — both usable simultaneously (canonical rotation window per `[FIXS RC1 §5]`).
  - On `remove(sha256_of_cert1)` from `{pin1, pin2}`: snapshot becomes `{pin2}`; any outstanding `pin_view` of `pin1` from earlier `find` remains valid for the view's lifetime.
- **Frozen?** No — explicit `[arch §5.6]` carve-out; rotation is the ONLY mid-session-mutable TLS surface.
- **Mutex choice**: `std::shared_mutex` writer + lock-free reader (atomic `shared_ptr<const pin_snapshot>` load/store). Consolidated rationale lives ONLY at `[2g §6.5.2]` (NEW-P2-6 close — the bundle MUST NOT re-derive the rationale at multiple sites; cite `[2g §6.5.2]` and stop).

## E-6 — `pin` (immutable record) — verbatim from `[2g §4.3]`

- **Kind**: owned diagnostic record + 32-byte fingerprint; one entry per pinned counterparty cert.
- **Header**: `include/fixpp/tls/pinset.hpp`.
- **Fields** (re-emitted verbatim from `[2g §4.3]` lines 411-416 — NEW-P1-4 close):
  - `std::array<std::byte, 32> sha256` — SHA-256 fingerprint of the pinned leaf cert's DER bytes.
  - `std::pmr::string subject_dn` — PMR-copied at `add()` time from the caller-supplied Certificate.
  - `std::pmr::vector<std::pmr::string> san_dns` — PMR-copied at `add()` time.
  - `core::time_point added_at` — Wall-clock UTC at `add()` time, sourced from `effective_clock` per `[2d §7.9]`.

## E-7 — `pin_snapshot` — verbatim from `[2g §4.3]`

- **Kind**: immutable container readers acquire via `snapshot()` or `pin_view::snapshot_`.
- **Header**: `include/fixpp/tls/pinset.hpp`.
- **Definition** (re-emitted verbatim from `[2g §4.3]` line 421 — NEW-P1-4 close):
  - `using pin_snapshot = std::pmr::vector<pin>;`
  - **Always PMR-allocated**; lifetime is the holding `shared_ptr`'s. The PMR backing is `Pinset::Config::mr` (or engine default if null); `mr` MUST outlive every snapshot per the round-3 P1-1 contract.

## E-8 — `pin_view` (handle) — verbatim from `[2g §4.3]`

- **Kind**: value-typed handle returned by `Pinset::find`. Lifetime-extends the snapshot containing the matched pin.
- **Header**: `include/fixpp/tls/pinset.hpp`.
- **Fields** (re-emitted verbatim from `[2g §4.3]` lines 427-433):
  - `std::shared_ptr<const pin_snapshot> snapshot` — pins the matched entry's lifetime (and the entire snapshot).
  - `pin const* value [[clang::lifetimebound]] = nullptr` — bounded by `*this` (i.e., by `snapshot`); attribute on the same line as the field.
- **Accessors**:
  - `bool found() const noexcept` — true if a match was found.
  - `explicit operator bool() const noexcept` — same.
- **Invariant**: a `pin_view` constructed from `find(fp)` that returns `found() == true` remains valid for the view's lifetime regardless of any subsequent `Pinset::remove(fp)` calls.

## E-9 — `CipherPolicy` (compile-time policy + runtime predicate) — verbatim from `[2g §4.4]`

- **Kind**: compile-time `static_assert`-enforced allow-list + `constexpr` runtime-string predicate.
- **Header**: `include/fixpp/tls/cipher_policy.hpp`.
- **Static members** (re-emitted verbatim from `[2g §4.4]` lines 553-604 — round-2 P1-4 close restores the byte-faithful transcription after round 1 silently drifted three of the five arrays):
  - `tls13_suites` (3 entries) — `TLS_AES_128_GCM_SHA256`, `TLS_AES_256_GCM_SHA384`, `TLS_CHACHA20_POLY1305_SHA256`.
  - `tls12_suites` (6 entries) — ECDHE-(RSA|ECDSA) × (AES128-GCM | AES256-GCM | CHACHA20-POLY1305).
  - `kx_groups` (3 entries) — `X25519`, `secp256r1`, `secp384r1`.
  - `sig_algs` (4 entries) — `ECDSA+SHA256`, `ECDSA+SHA384`, `RSA-PSS+SHA256`, `RSA-PSS+SHA384`.
  - `banned_tokens` (12 entries per `[2g §4.4]` lines 599-604 — NEW-P3-1 close): `RC4`, `DES`, `3DES`, `MD5`, `DH_anon`, `NULL`, `EXPORT`, `TLS_RSA`, `CBC`, `SHA1`, `TLS_AES_128_CCM`, `0RTT`. (The `0RTT` token is the NEW-P3-1 close per `[2g §4.4]` line 603 — `[const §XII.3]`'s no-0-RTT enforcement IS this banned_tokens entry. Note `SHA1` has no dash per the design-doc verbatim spelling; the TLS-version tokens / RSA-1024 are constitutional pins enforced via the `[const §XII.3]` allow-list construction, NOT additional banned_tokens entries.)
- **Compile-time validation**:
  - `static_assert(!any_banned(tls13_suites, banned_tokens), "TLS 1.3 suite list contains a banned token (per [const §XII.3] / [const §XV.11]).");`
  - Same for `tls12_suites`, `kx_groups`, `sig_algs`.
- **Runtime predicate**:
  - `[[nodiscard]] static constexpr bool is_allowed(std::string_view tok) noexcept;` — Returns true iff `tok` is on `tls13_suites ∪ tls12_suites ∪ kx_groups ∪ sig_algs` AND NOT a substring-match against `banned_tokens`. Exposed for the 2i C-ABI bridge.
- **Validation rules**:
  - Banned token in any allow-list → compile fails (`static_assert` diagnostic identifies the violation).
  - Runtime string outside the allow-list (or matching banned) → `is_allowed` returns `false`; the 2i C bridge translates to `tls_cipher_not_allowed`.

## E-10 — `SecurityProfile` (enum) — verbatim from `[2g §4.5]`

- **Kind**: scoped enum with 4 enumerators including the `unset = 0` sentinel.
- **Header**: `include/fixpp/tls/security_profile.hpp`.
- **Values** (re-emitted verbatim from `[2g §4.5]` lines 650-656 — Codex P1-4 close):
  - `unset = 0` — sentinel; rejected at `make_ssl_ctx_config` and at `Session::open` per `[const §XII.5]`'s no-implicit-default rule.
  - `mtls_ca = 1` — CA-trust on peer cert.
  - `mtls_pinned = 2` — leaf-cert pinning; required for FIXS-conformant deployments.
  - `one_way_ca [[deprecated("one_way_ca is legacy interop; prefer mtls_pinned or mtls_ca")]] = 3` — `[[deprecated]]` on the enumerator declaration itself (not in a comment).
- **Validation rule**: `make_ssl_ctx_config(SecurityProfile::unset, ...)` rejects with `tls_invalid_security_profile`.

## E-11 — `SslCtxConfig` (value type — the contract 2h consumes) — verbatim from `[2g §4.5]`

- **Kind**: value-type description of the `SSL_CTX` configuration 2h-transport must apply.
- **Header**: `include/fixpp/tls/security_profile.hpp`.
- **Fields** (re-emitted verbatim from `[2g §4.5]` lines 670-677 — NEW-P1-1 close: `pinset_snapshot` field added):
  - `SecurityProfile profile`
  - `std::shared_ptr<cert_source> cs`
  - `std::shared_ptr<Pinset> pinset` — null permitted under `mtls_ca` + `one_way_ca`. Kept for **diagnostic reachability only**; verification consumes `pinset_snapshot`.
  - `std::shared_ptr<const pin_snapshot> pinset_snapshot` — **NEW-P1-1 close**: 2h's handshake wiring captures `Pinset::snapshot()` ONCE at handshake start per `[2g §6.5.1]` BINDING CONTRACT and assigns the result here; `verify_peer` scans this snapshot directly. NEVER call `cfg.pinset->find/contains/snapshot` from inside `verify_peer`. null permitted under `mtls_ca` + `one_way_ca` (no pinning).
  - `std::shared_ptr<fixpp::core::Clock> clock` — session-scoped `effective_clock` per `[2d §7.9]`; `verify_peer` uses `clock->now()`.
  - `CipherPolicy ciphers {}` — value-typed; constexpr-only members.
  - `std::pmr::memory_resource* mr {nullptr}` — for `peer_identity`'s owning SAN-string allocation; null → engine default.
- **TLS-version posture** (`/clarify` Q1 / FR-014): the 2h adapter, when configuring `SSL_CTX` from this descriptor, applies `SSL_CTX_set_min_proto_version = TLS1_2_VERSION` and `SSL_CTX_set_max_proto_version = TLS1_3_VERSION` for every profile (except `unset` which is rejected). The version posture is part of THIS feature's contract per FR-014, not a 2h-private choice.
- **Factory** (re-emitted verbatim from `[2g §4.5]` lines 681-686 — NEW-P1-2 close):
  - `[[nodiscard]] core::expected_t<SslCtxConfig> make_ssl_ctx_config(SecurityProfile profile, std::shared_ptr<cert_source> cs, std::shared_ptr<fixpp::core::Clock> clock, std::shared_ptr<Pinset> pinset = nullptr, std::pmr::memory_resource* mr = nullptr);`
  - **Parameter order** is `(profile, cs, clock, pinset = nullptr, mr = nullptr)` per `[2g §4.5]` lines 681-686 (NEW-P1-2 close — NOT the bundle's earlier `(profile, source, pinset, clock, validation_caps)` shape).
  - **NO `validation_caps` parameter** — the cert_source's DoS-cap fields (`max_rsa_key_bits`, `max_cert_der_bytes`, `max_san_entries`, `max_chain_depth`) are reachable via `cs->config()` (or via concrete-impl accessor); `verify_peer` consumes them through `cs`, NOT through a duplicated SslCtxConfig field per `[2g §4.2]` line 320-326.
- **Factory validation**:
  - `SecurityProfile::unset` → `tls_invalid_security_profile`.
  - null `cs` → `tls_invalid_security_profile`.
  - null `clock` → `tls_invalid_security_profile`.
  - `mtls_pinned` + null `pinset` → `tls_invalid_security_profile`.
  - `mtls_pinned` + non-null EMPTY `pinset` → `tls_pin_empty_at_open` (`/clarify` Q2; new variant).
  - `one_way_ca` + non-null `pinset` → `tls_invalid_security_profile` (deprecated profile + pinning is contradictory per `[2g §4.5.1]` row 3).

## E-12 — `Certificate` (value-typed view) — verbatim from `[2g §4.5]`

- **Kind**: value-typed view over a parsed peer cert.
- **Header**: `include/fixpp/tls/certificate.hpp`.
- **Fields**:
  - `std::span<const std::byte> raw_der_` (back-pointer; the DER bytes are owned by the `cert_source` impl)
  - `std::string_view subject_dn_` (back-pointer into raw_der_)
  - `std::string_view issuer_dn_` (back-pointer)
  - `std::span<const std::string_view> san_dns_names_` (back-pointers; cap-bounded ≤ `max_san_entries`)
  - `std::span<const std::string_view> san_uris_` (back-pointers; cap-bounded)
  - `std::array<std::byte, 32> sha256_` (32 bytes; SHA-256-of-raw_der_) — the Pinset key per `/clarify` Q4
  - `int x509_version_` (1, 2, or 3; v1 rejected at validate)
  - `std::chrono::system_clock::time_point not_before_` — X.509-envelope ABSOLUTE wall-clock time; parsed from DER, NOT captured against `cfg.clock` (NEW-P2-4 close — the comparison in `verify_peer` evaluates these absolute times against `cfg.clock->now()`, which is well-defined for any session-scoped clock override).
  - `std::chrono::system_clock::time_point not_after_` — same.
  - `signature_algorithm alg_` enum
  - `std::size_t rsa_key_bits_` (only meaningful when RSA-PSS; else 0)
  - `ecdsa_curve curve_` (only meaningful when ECDSA; else `unspecified`)
- **Accessors** — every non-owning view accessor carries `[[clang::lifetimebound]]` at the declaration site per D-11 / `[arch §5.5]` / `[2b §6.4]`.
- **Validation rules** (enforced at parse; see also `verify_peer` E-14):
  - `raw_der_.size() ≤ Config::max_cert_der_bytes`
  - `san_dns_names_.size() + san_uris_.size() ≤ Config::max_san_entries`
  - `x509_version_ ∈ {2, 3}` (v1 rejected at `verify_peer`)

## E-13 — `peer_identity` (value type; T-041 cross-cut) — verbatim from `[2g §4.5]`

- **Kind**: owning value type derived from a verified peer cert; the value the session-FSM Phase-4 module consumes for the CompID-to-TLS-identity binding.
- **Header**: `include/fixpp/tls/peer_identity.hpp`.
- **Fields** (re-emitted verbatim from `[2g §4.5]` lines 706-724):
  - `std::pmr::string subject_dn` — owned, PMR-allocated.
  - `std::pmr::vector<std::pmr::string> san_dns_names_owned` — owned, PMR-allocated; bounded by `max_san_entries`.
  - `std::pmr::vector<std::pmr::string> san_uris_owned` — owned, PMR-allocated; bounded by `max_san_entries`.
  - `std::array<std::byte, 32> leaf_fingerprint` — owned (32 bytes); SHA-256 of the leaf's DER (the Pinset key).
  - `std::chrono::system_clock::time_point not_after` — carried for the session FSM's effective-clock-aware expiry checks per D-9.
- **Accessors** (re-emitted verbatim from `[2g §4.5]` lines 716-723) — every view returns `[[clang::lifetimebound]]` bound to `*this`:
  - `std::string_view subject_dn_view() const noexcept [[clang::lifetimebound]]`
  - `std::span<const std::pmr::string> san_dns_names() const noexcept [[clang::lifetimebound]]`
  - `std::span<const std::pmr::string> san_uris() const noexcept [[clang::lifetimebound]]`
- **Construction**: only via `verify_peer` (E-14) which builds the owning value from the view-typed `Certificate` on accept; the SAN strings + subject DN are PMR-copied into the owning storage at `verify_peer` time. The PMR resource is `cfg.mr` (or engine default).

## E-14 — `verify_peer` (validation predicate) — verbatim from `[2g §4.5]`

- **Kind**: free function (not a class method); the validation predicate 2h-transport's `SSL_VERIFY_PEER` callback invokes.
- **Header**: `include/fixpp/tls/security_profile.hpp` (or a dedicated `verify_peer.hpp`; final placement at /implement time).
- **Signature** (re-emitted verbatim from `[2g §4.5]` lines 700-701 — Codex P1-3 + NEW-P1-1 close):
  - `[[nodiscard]] core::expected_t<peer_identity> verify_peer(SslCtxConfig const& cfg, std::span<const Certificate> peer_chain) noexcept;`
  - **ONE `peer_chain` parameter** (NOT a `leaf + chain` split); the leaf is `peer_chain[0]`. The OpenSSL `SSL_VERIFY_PEER` callback delivers ONE chain.
- **Behaviour** — short-circuits on the first violation hit per FR-020a (`/clarify` Q3 — evaluation order). Per `[2g §6.6]`, most rejection causes route through the `tls_handshake_failed` GROUPING variant with a diagnostic-field sub-reason; dedicated variants exist for DoS caps (`tls_cert_der_too_large`, `tls_rsa_key_too_large`, `tls_san_entries_exceeded`) and for pinning (`tls_pin_mismatch`):
  1. Per-cert DER envelope: `peer_chain[0].raw_der().size() ≤ cs.config().max_cert_der_bytes`. Violation → `tls_cert_der_too_large`.
  2. RSA key lower bound: if leaf is RSA-PSS, `rsa_key_bits ≥ 2048`. Violation → `tls_handshake_failed` (sub-reason `"rsa_under_min"`).
  3. RSA key upper bound: if RSA-PSS, `rsa_key_bits ≤ cs.config().max_rsa_key_bits`. Violation → `tls_rsa_key_too_large`.
  4. ECDSA curve: if ECDSA, `curve ∈ {p256, p384}`. Violation → `tls_handshake_failed` (sub-reason `"ecdsa_curve"`).
  5. Chain depth: `peer_chain.size() ≤ cs.config().max_chain_depth`. Violation → `tls_handshake_failed` (sub-reason `"chain_too_deep"`).
  6. SAN cardinality: `san_dns_names.size() + san_uris.size() ≤ cs.config().max_san_entries`. Violation → `tls_san_entries_exceeded`.
  7. X.509 version: `x509_version ∈ {2, 3}`. Violation → `tls_handshake_failed` (sub-reason `"x509_v1"`).
  8. Expiration: `peer_chain[0].not_before() ≤ cfg.clock->now() ≤ peer_chain[0].not_after()`. Violation → `tls_handshake_failed` (sub-reason `"expired"` or `"not_yet_valid"`).
  9. Pinning (only under `SecurityProfile::mtls_pinned`): scan `cfg.pinset_snapshot` linearly for `peer_chain[0].sha256()`. **NEW-P1-1 close**: this MUST use the captured `cfg.pinset_snapshot` per `[2g §6.5.1]` BINDING CONTRACT — never call `cfg.pinset->find/contains/snapshot`. Violation → `tls_pin_mismatch`.
  10. Cipher (if 2h delegates): `CipherPolicy::is_allowed(negotiated)`. Violation → `tls_cipher_not_allowed`.
- **On accept**: returns `peer_identity` built from `peer_chain[0]` (PMR-copies the subject DN + SANs into owning storage; PMR resource is `cfg.mr` or engine default).

## E-15 — `error::tls_*` variants — verbatim from `[2g §6.6]`

- **Kind**: enum-variant extensions to the existing `fixpp::core::error` enum.
- **Header**: appended to `include/fixpp/core/error.hpp` at the next free slots (slot allocation respects prior 005/008/009/010 pinning per `[[project_2e_design_doc_only_seqnum_handoff]]`; 011 takes the next contiguous range after slot 77).
- **Variants** (re-emitted verbatim from `[2g §6.6]` lines 980-1003 — 15 variants per the signed-off block + 1 additive amendment from `/clarify` Q2 = **16 total**; Codex P2-1 + NEW-P1-4 close):

  **Configuration / cold-path-load (`FIXPP_ERR_TLS_CONFIG`):**
  - `tls_cert_load_failed` — `[2g §4.2]` — `file_cert_source` could not read a cert file at construction; OR `make_file_cert_source` factory returning the same condition through `expected_t<...>`; OR local cert exceeds DoS caps at load.
  - `tls_cert_parse_failed` — `[2g §4.2]` / `[2g §4.5]` — PEM/DER parse failed (malformed envelope, unexpected ASN.1 shape, unsupported encoding).
  - `tls_cipher_not_allowed` — `[2g §4.4]` / `[2g §4.5]` / `[2g §6.1]` — runtime configuration attempted to enable a cipher not on the four `CipherPolicy` allow-lists. The compile-time `static_assert` chain catches at build; this variant covers the C-ABI / config-file path through `CipherPolicy::is_allowed`.
  - `tls_invalid_security_profile` — `[2g §4.5]` — `SecurityProfile::unset` reached `make_ssl_ctx_config`; OR `mtls_pinned` with null Pinset; OR `one_way_ca` with non-null Pinset; OR any profile with null clock.
  - `tls_sign_callback_unavailable` — `[2g §4.1]` — impl returned `local_credentials` whose `signer` variant carried a `software_key_ref` with a null handle, AND the awaitable signer path was also empty. Preserved for the C-ABI path where the discriminator may carry an unset value.

  **Pinset state (`FIXPP_ERR_TLS_PINSET`):**
  - `tls_pin_not_found` — `[2g §4.3]` — `Pinset::remove(fp)` with a fingerprint not in the set.
  - `tls_pin_already_present` — `[2g §4.3]` — `Pinset::add(p)` with a pin already in the set (by SHA-256).
  - `tls_pinset_capacity_exhausted` — `[2g §4.3]` / `[2g §1.1]` — `Pinset::add` with `size() == max_pins`.

  **Runtime allocation (`FIXPP_ERR_TLS_RUNTIME`):**
  - `tls_pinset_alloc_failed` — `[2g §4.3]` — PMR allocation throw on snapshot clone routed through `[2a §4.2]` `trap_throw`.

  **Handshake / verify_peer (`FIXPP_ERR_TLS_HANDSHAKE` — C-ABI coalescing group):**
  - **`tls_handshake_failed`** — `[2g §6.6]` line 995. **The GROUPING variant**; the C-ABI coalescing scheme depends on this. The diagnostic field carries the specific sub-reason (`"expired"`, `"not_yet_valid"`, `"rsa_under_min"`, `"sigalg_disallowed"`, `"ecdsa_curve"`, `"chain_too_deep"`, `"x509_v1"`, etc.). `verify_peer` returns this variant on any non-DoS-cap, non-pinning rejection.
  - `tls_rsa_key_too_large` — DoS bound; surfaces through `FIXPP_ERR_TLS_HANDSHAKE` group with sub-reason `"rsa_over_max"`.
  - `tls_cert_der_too_large` — DoS bound.
  - `tls_san_entries_exceeded` — DoS bound.
  - `tls_pin_mismatch` — peer cert SHA-256 not in the captured handshake-time Pinset snapshot under `mtls_pinned`.

  **Cancellation (reuses `FIXPP_ERR_CANCELLED`):**
  - `tls_load_cancelled` — `[2g §4.1]` / `[2g §6.4]` — `cert_source::load_credentials`'s awaitable was cancelled (or `async_signer_ref::sign` was).

  **Additive amendment from `/clarify` Q2 (projects to `FIXPP_ERR_TLS_CONFIG`):**
  - `tls_pin_empty_at_open` — `make_ssl_ctx_config(mtls_pinned, ..., empty_pinset, ...)` returns this distinct variant. Surfaces at session-open (config-time), NOT per-handshake; distinct from `tls_pin_mismatch` so operator logs separate "fixpp-config problem" from "peer-cert problem".

- **C-ABI coalescing** (re-emitted verbatim from `[2g §6.6]` lines 1006-1013 — Codex P2-1 close): five projection groups owned by 2i:
  - configuration → `FIXPP_ERR_TLS_CONFIG`
  - handshake / verification (incl. the `tls_handshake_failed` grouping variant + DoS-cap + pinning variants) → `FIXPP_ERR_TLS_HANDSHAKE`
  - pinset state → `FIXPP_ERR_TLS_PINSET`
  - runtime allocation → `FIXPP_ERR_TLS_RUNTIME`
  - cancellation → existing `FIXPP_ERR_CANCELLED`

## Relationships overview

```
        EngineConfig ─────────────────┐
              │                       │
              │ default_cert_source   │ clock
              ▼                       ▼
        cert_source ◄───── SessionConfig ─────► Clock
              │                       │
              │ load_credentials      │ cert_source (override)
              ▼                       │ pinset (per-counterparty shared_ptr per FR-009a)
        local_credentials             │
       { leaf, chain (view), signer } │
                                      │ verify_peer-time:
                                      │   2h captures Pinset::snapshot() ONCE
                                      │   into SslCtxConfig::pinset_snapshot
                                      │   per [2g §6.5.1] BINDING CONTRACT
                                      ▼
                              make_ssl_ctx_config(profile, cs, clock, pinset?, mr?)
                                      │ returns
                                      ▼
                              SslCtxConfig ──────► consumed by 2h-transport
                                  { profile, cs, pinset, pinset_snapshot,
                                    clock, ciphers, mr }
                                      │
                                      │ verify_peer(cfg, peer_chain)
                                      ▼
                              peer_identity ──────► consumed by session/ Phase-4
                                                    (T-041 CompID binding)

        Pinset ◄── shared_ptr ── SessionConfig (one Pinset per counterparty)
           │
           │ add(Certificate) / remove(sha256) (synchronous, shared_mutex writer)
           │ find (lock-free reader; returns pin_view)
           │ snapshot (lock-free; captured ONCE at handshake start by 2h)
           ▼
       pin_snapshot = std::pmr::vector<pin>  (PMR-allocated)
           │
           │ kept alive by
           ▼
       pin_view (operator scope) / SslCtxConfig::pinset_snapshot (handshake scope)
```
