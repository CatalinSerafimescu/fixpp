# Feature Specification: 011 — TLS Policy Core

**Feature Branch**: `011-tls-policy`
**Created**: 2026-05-23
**Status**: Draft
**Input**: User description: TLS policy core (slug: 011-tls-policy). Locks the public TLS-policy surface for fixpp v1.0, consumed by the 2h-transport feature that ships next. Inherits signed-off design doc `.specify/2g-tls.md` v0.4 (Gate A r3 converged Phase A).

## User Scenarios & Testing *(mandatory)*

### User Story 1 — Counterparty cert rotation without disconnects (Priority: P1)

A FIX engine operator runs a long-lived session against a counterparty whose leaf certificate is renewed on a 30–90 day cadence. When the counterparty announces the new cert, the operator adds it to the engine's pinset for that counterparty *before* the old cert is removed. Any TLS handshake in flight during the rotation uses whichever set of pins was current when that handshake started; no in-flight handshake observes a partially-updated pinset, and no live session is disconnected by the rotation. After the counterparty has cut over to the new cert, the operator removes the old pin. The next handshake validates against the new set only.

**Why this priority**: This is the canonical FIXS RC1 §5 rotation use case. Without it, every cert rotation on either side of a counterparty link forces a session restart, breaking 24-hour-connectivity (S-032) and producing customer-visible outages on a routine operational event. It is also the *only* mid-session-mutable TLS surface per the architecture carve-out — every other TLS knob freezes at session open — so the contract here is load-bearing.

**Independent Test**: With a session in `mtls_pinned` mode, run a continuous handshake loop against a peer presenting the *old* cert, then `add(new)`, then `remove(old)`, then run the same loop against a peer presenting the *new* cert. Both endpoints succeed across the rotation window; an in-flight handshake captured mid-rotation completes against whichever set it started with.

**Acceptance Scenarios**:

1. **Given** an active session with pinset `{old}`, **When** the operator calls `add(new)` then `remove(old)` in that order, **Then** the active session is not disconnected and the next handshake validates against `{new}` only.
2. **Given** a handshake captured mid-rotation (pinset transitioning from `{old}` to `{old, new}` to `{new}`), **When** the handshake completes, **Then** the validation result matches whichever pinset was current when the handshake's `find()` ran (no torn read).
3. **Given** the operator calls `remove(old)` before `add(new)`, **When** a peer presents the new cert, **Then** the handshake fails with `error::tls_cert_pin_mismatch` — the operator-induced ordering error is surfaced, not silently absorbed.
4. **Given** a pinset already at `max_pins`, **When** the operator calls `add(extra)`, **Then** the call surfaces a rejection (the cap is enforced; no silent eviction).

---

### User Story 2 — Plug a custom credential source without recompiling (Priority: P2)

A security-conscious operator routes session-cert signing through an HSM / KMS / vault rather than a file on disk. The operator implements a small class that conforms to the published `cert_source` interface and hands it to the engine at session-config time. The engine never sees the private key in process memory; signing operations are offloaded off the session strand to whatever executor the operator's `async_signer_ref` is bound to. No fixpp source change, no recompile, no fork.

**Why this priority**: This is the entire reason the cert-source surface is a plugin interface rather than a baked-in file loader. Without it, regulated FIX deployments (anywhere PKCS#11 / FIPS HSMs are required) cannot use fixpp at all. It is also the user-visible surface that proves the [const §XIV.2] ≤5-pure-virtual cap is honoured.

**Independent Test**: Provide a stub `cert_source` implementation whose `load_credentials()` returns an `async_signer_ref` pointing at an isolated executor. Open a TLS session; observe that signing-callback work runs on the operator's executor (not the session strand), the session opens successfully, and the file-default impl is never instantiated.

**Acceptance Scenarios**:

1. **Given** an operator-supplied `cert_source` whose `load_credentials()` returns a `local_credentials` carrying an `async_signer_ref`, **When** the session opens, **Then** the handshake completes and any handshake-time signing work runs on the executor the `async_signer_ref` is bound to (off the session strand).
2. **Given** an operator-supplied `cert_source` whose `load_credentials()` returns `unexpected{tls_load_failed}`, **When** the session is opened, **Then** session open fails with that exact error variant — no fallback to the file default.
3. **Given** an operator-supplied `cert_source` whose `load_credentials()` is cancelled mid-flight, **When** cancellation propagates, **Then** the awaitable completes with `unexpected{tls_load_cancelled}` (not by throwing).
4. **Given** the default `file_cert_source` constructed with a non-existent PEM path, **When** the factory `make_file_cert_source(...)` is called, **Then** it returns `unexpected{tls_load_failed}` rather than throwing across the session-handling boundary.

---

### User Story 3 — Hardened-by-default trust mode (Priority: P3)

A FIX engine is configured against a new counterparty. The operator picks a `SecurityProfile` at session-config time (`mtls_ca`, `mtls_pinned`, or the deprecated `one_way_ca`). The engine refuses, at session construction or at handshake, every cipher / TLS version / cert parameter outside the FIXS-RC1-aligned envelope: no TLS ≤1.1, no SSL, no CBC-mode TLS 1.2, no SHA-1, no RSA < 2048, no DH_anon / NULL / export, no TLS 1.3 0-RTT. Banned suites are rejected at compile time (the binary cannot be built); runtime-string overrides (from the C ABI) are rejected by `CipherPolicy::is_allowed`. Peer certs outside the bounds (RSA > operator cap, DER too large, SAN list too long, X.509 v1, expired, chain too deep) are rejected at `verify_peer` with a distinct named error variant — never a "TLS failed" catch-all.

**Why this priority**: This is the constitutional spine ([const §XII], [FIXS §3]) made enforceable. Without it, the engine could be misconfigured into a non-compliant cipher / version posture and still appear to work, which is the worst possible failure mode for a security-critical library. The compile-time half of the contract is what makes the runtime half cheap (the hot path never has to re-check what the type system already proved).

**Independent Test**: (a) Attempt to build the engine with a non-allowed cipher constant wired into `CipherPolicy`; verify the build fails with a diagnostic identifying the violation. (b) Open a session against a peer presenting an RSA-1024 cert; verify handshake refuses with `error::tls_rsa_key_too_small`. (c) Open a session against a peer presenting a cert with > `max_san_entries` SAN entries; verify refusal with `error::tls_san_entries_exceeded`.

**Acceptance Scenarios**:

1. **Given** a build configuration that lists a non-allowed cipher suite in `CipherPolicy`, **When** the build is attempted, **Then** the build fails at compile time with a diagnostic naming the violation (no shippable binary).
2. **Given** the C-ABI runtime path passes the string of a non-allowed cipher to `CipherPolicy::is_allowed`, **When** the predicate is evaluated, **Then** it returns `false` — no fall-through to OpenSSL.
3. **Given** a session opened in `mtls_ca` mode against a peer presenting an RSA-1024 leaf cert, **When** `verify_peer` runs, **Then** the handshake is refused with `error::tls_rsa_key_too_small`.
4. **Given** a session opened against a peer presenting an X.509 v1 leaf cert, **When** `verify_peer` runs, **Then** the handshake is refused (X.509 v1 is rejected per [FIXS §3.4]).
5. **Given** an `mtls_pinned` session whose pinset is empty at session open, **When** the session attempts to open, **Then** session open is refused (fail-closed; the pinset is the trust authority in this mode and an empty trust authority is a configuration error).

---

### Edge Cases

- **Concurrent rotation + handshake**: `add(new)` lands during an in-flight handshake. The handshake completes against whichever pinset was current when its `find()` ran; the new cert is observable only on the *next* handshake. There is no atomic-swap shortcut and no torn read.
- **`remove()` while a `pin_view` from earlier `find()` is held**: the cert remains usable through that handle for the handle's lifetime; the underlying pin object is kept alive by the `shared_ptr` snapshot the `pin_view` captured.
- **PEM password incorrect**: `make_file_cert_source(Config, …)` returns `unexpected{tls_load_failed}`; no exception crosses the session-handling boundary.
- **Cert file unreadable** (permissions / missing): same — `unexpected{tls_load_failed}`.
- **Peer presents an RSA key above the operator's `max_rsa_key_bits` cap**: refused at `verify_peer` with `error::tls_rsa_key_too_large`. (DoS vector — `BN_mod_exp` cost is super-linear in modulus size.)
- **Peer presents a per-cert DER above `max_cert_der_bytes` (default 16 KiB)**: refused *before* parsing with `error::tls_cert_der_too_large`. (DoS vector — 10 MiB cert wastes parse time.)
- **Peer presents a SAN list above `max_san_entries` (default 64)**: refused with `error::tls_san_entries_exceeded`. (DoS vector — bounds `peer_identity` allocation.)
- **Peer presents a chain deeper than `max_chain_depth` (default 8)**: refused.
- **Peer cert expired against the *effective* clock**: refused (clock plugin from 007-threading-clock is the time source, not wall-clock).
- **Async signer cancellation mid-handshake**: `load_credentials` completes with `unexpected{tls_load_cancelled}`; the session's open path consumes the error variant.
- **PMR allocator throws during cold-path `load_credentials`**: routed through `[2a §4.2]` `trap_throw`; surfaces as `error::tls_load_failed`.
- **`add()` to a pinset already at `max_pins = 16`**: refused (no silent eviction; operator decides which old pin to remove first).
- **Operator attempts mid-session `SecurityProfile` swap**: not exposed — there is no such API. The supported pattern is close + reopen the session.
- **Operator attempts mid-session `cert_source` swap**: not exposed — same.
- **Operator wires the C ABI cipher-name runtime override to a banned cipher string**: `CipherPolicy::is_allowed` returns `false`; the 2i bridge surfaces `error::tls_cipher_not_allowed` to the C caller.

## Requirements *(mandatory)*

All requirements below are testable. Items tagged "(security)" trace to [const §XII] and [FIXS RC1].

### Functional Requirements

**Cert-source plugin surface**

- **FR-001**: The cert-source plugin interface MUST expose at most 2 pure-virtual methods (`load_credentials`, `load_trust_anchors`) — well below the [const §XIV.2] cap of ≤ 5.
- **FR-002**: `load_credentials` MUST be awaitable and MUST return `expected_t<local_credentials>` (no exceptions cross the session-handling boundary per [arch §5.3]).
- **FR-003**: `local_credentials::signer` MUST carry `std::variant<software_key_ref, async_signer_ref>` so HSM/KMS/vault impls can offload signing off the session strand per [2d §7.5]. (security)
- **FR-004**: A file-based default impl (`file_cert_source`) MUST ship as the v1.0 reference — PEM/DER over a caller-supplied `cert_source::Config`, with an optional password callback for encrypted PEM.
- **FR-005**: A factory `make_file_cert_source(Config, std::pmr::memory_resource*) -> expected_t<std::shared_ptr<cert_source>>` MUST be the [arch §6] rule-4 entry point for non-construction-time callers. Construction-time bootstrap errors (engine init before any session is open) MAY throw per the [arch §5.3] carve-out; the factory wraps that boundary.

**Pinset rotation**

- **FR-006**: `Pinset::add(cert)` and `Pinset::remove(cert)` MUST be separate explicit operations. There MUST be NO atomic-swap or "set" shortcut. Old certs MUST remain usable until `remove` returns. (security, [FIXS RC1 §5])
- **FR-007**: Pinset reads on the TLS handshake hot path MUST be lock-free and MUST allocate zero per [const §VIII.5]. (security)
- **FR-008**: `Pinset::find(...)` MUST return a value-typed `pin_view` that carries a `shared_ptr<const pin_snapshot>` keeping the matched entry alive for the view's lifetime — no dangling on concurrent rotation.
- **FR-009**: Pinset rotation MUST be mid-session-mutable per the [arch §5.6] carve-out. A rotation landing during an in-flight handshake MUST NOT affect that handshake; the new pinset is picked up at the *next* handshake.
- **FR-010**: `Pinset::Config::max_pins` MUST default to 16; `add()` over the cap MUST be refused (no silent eviction).

**Cipher / version policy**

- **FR-011**: `CipherPolicy` MUST be a compile-time allow-list. Anything outside the four normative lists ([const §XII.3] TLS 1.3 suites + TLS 1.2 ECDHE-AEAD suites + X25519/secp256r1/secp384r1 KX groups + ECDSA P-256/P-384 / RSA-PSS ≥2048 signature algorithms) MUST fail to compile. Banned items per [const §XII.4] / [const §XV.11] (RC4, DES, 3DES, MD5, DH_anon, NULL, export-grade, CBC-mode TLS 1.2, SHA-1, 1024-bit RSA, TLS 1.3 0-RTT, TLS ≤1.1, SSL) MUST fail to compile. (security)
- **FR-012**: `CipherPolicy::is_allowed(std::string_view) constexpr noexcept -> bool` MUST be exposed for the runtime-string path that the C ABI (2i) bridges into. (security)

**Security profile**

- **FR-013**: `SecurityProfile` MUST publish exactly three enumerators: `mtls_ca`, `mtls_pinned`, `one_way_ca`. The `[[deprecated]]` attribute MUST be applied to the `one_way_ca` enumerator declaration itself (not in a comment) per [const §XII.5]. (security)
- **FR-014**: The mapping from each `SecurityProfile` value to the OpenSSL `SSL_CTX` configuration the 2h-transport feature applies MUST be published as a normative table; the table is the contract 2h consumes.
- **FR-015**: `SecurityProfile` MUST be frozen at session open per [arch §5.6]. No mid-session swap API MUST be exposed.
- **FR-016**: The `cert_source` MUST be frozen at session open per [arch §5.6]. The `Pinset` is the ONLY mid-session-mutable TLS surface.

**Value-type accessors + lifetime**

- **FR-017**: `Certificate` MUST be value-typed. Every accessor returning a non-owning view (`std::span<const Certificate>`, `std::string_view`, `pin_view`, the inner span of `load_credentials`'s awaitable result) MUST carry `[[clang::lifetimebound]]` at the **declaration site of the abstract base** per [arch §5.5] and the [2b §6.4] precedent.
- **FR-018**: `peer_identity` MUST be exposed as a value type derived from a verified peer cert. The session-FSM Phase-4 feature consumes it to perform the T-041 CompID-to-TLS-identity binding; this feature owns the value, not the binding.

**DoS-bounded `verify_peer`**

- **FR-019**: `verify_peer` MUST enforce, at entry: RSA key size ≤ `cert_source::Config::max_rsa_key_bits` (default 8192) with refusal variant `error::tls_rsa_key_too_large`; per-cert DER ≤ `Config::max_cert_der_bytes` (default 16 KiB) with `error::tls_cert_der_too_large`; SAN list (DNS + URI combined) ≤ `Config::max_san_entries` (default 64) with `error::tls_san_entries_exceeded`. (security)
- **FR-020**: `verify_peer` MUST also enforce: chain depth ≤ `Config::max_chain_depth` (default 8); RSA key size ≥ 2048 bits per [FIXS §3.4]; ECDSA keys MUST be P-256 or P-384; X.509 v1 certs MUST be rejected; cert expiration MUST be evaluated against the *effective* clock per [2d §7.9] (not wall-clock). (security)

**Cancellation, error envelope, allocation**

- **FR-021**: `cert_source::load_credentials` MUST honour ASIO native cancellation slots per [const §XI.2] / [SYN §3.2 Q6a] via the [2d §6.5] `cancellable_dispatch` precedent. Cancellation MUST complete with `expected_t::unexpected{tls_load_cancelled}` (no thrown exception).
- **FR-022**: Every `expected_t<T>`-returning method on the public surface MUST be `[[nodiscard]]`.
- **FR-023**: PMR allocation MUST be permitted on the cold-path `load_credentials` (file I/O + parse + chain build). The zero-allocation invariant applies to the hot-path `Pinset::find` only.
- **FR-024**: PMR-thrown exceptions MUST be routed through `[2a §4.2]` `trap_throw` and surface as `error::tls_*` variants. No PMR throw MUST escape the public surface as an exception.
- **FR-025**: The `error::tls_*` variant family MUST cover at minimum: `tls_load_failed`, `tls_load_cancelled`, `tls_cert_invalid`, `tls_cert_expired`, `tls_cert_pin_mismatch`, `tls_cert_chain_too_deep`, `tls_cert_der_too_large`, `tls_rsa_key_too_large`, `tls_rsa_key_too_small`, `tls_san_entries_exceeded`, `tls_profile_mismatch`, `tls_cipher_not_allowed`. Each MUST coalesce to a per-doc-prefix `FIXPP_ERR_TLS_*` C-ABI variant (the C-ABI surface itself is delegated to 2i).

**Scope boundary (negative requirements)**

- **FR-026**: This feature MUST NOT own: the OpenSSL `SSL_CTX` construction; the TLS handshake coroutine; the `SSL_VERIFY_PEER` callback wiring (all 2h-transport); the C ABI surface itself (2i); HSM/TPM/KMS/vault concrete impls (user-side per [const §XII.8]); the session-FSM consumption of `SecurityProfile` at `Session::open` (session Phase-4 module); the CompID-to-TLS-identity binding T-041 (session Phase-4 module); the control-plane reload trigger (2j); the TLS-event log / OTel schema (2k).
- **FR-027**: This feature MUST NOT support: PSK auth (T-012, post-v1); CRL / OCSP infrastructure (post-v1); mid-handshake pinset rotation; mid-session `SecurityProfile` swap; mid-session `cert_source` swap; `dlopen`-based plugin loading.

### Key Entities

- **`cert_source`** — abstract pure-virtual plugin interface (≤ 2 methods). Users implement to integrate HSM / KMS / vault / file / in-memory cert sources without touching engine code.
- **`local_credentials`** — value type returned by `cert_source::load_credentials`. Carries the leaf cert, its chain, and a `signer` variant (`software_key_ref` for in-process keys, `async_signer_ref` for offloaded signing).
- **`Pinset`** — mutable container of pinned counterparty certificates. Owns the FIXS RC1 §5 add-then-remove rotation semantics. Mid-session-mutable.
- **`pin_view`** — value-typed handle returned by `Pinset::find`. Keeps the matched pin alive across concurrent rotation.
- **`CipherPolicy`** — compile-time allow-list of cipher suites, TLS versions, key-exchange groups, and signature algorithms. Banned items are `static_assert`-rejected at build time. Exposes `is_allowed(string_view) constexpr` for runtime-string predicates.
- **`SecurityProfile`** — enum selecting trust mode at session construction: `mtls_ca`, `mtls_pinned`, `one_way_ca [[deprecated]]`. Frozen at session open.
- **`Certificate`** — value-typed view over a parsed peer cert (subject DN, issuer DN, SAN list, SHA-256 fingerprint, raw-DER back-pointer). `[[clang::lifetimebound]]`-annotated accessors.
- **`peer_identity`** — value type derived from a verified peer cert; consumed by the session FSM for the T-041 CompID-to-TLS-identity binding.
- **`error::tls_*` variants** — the named error envelope every TLS-policy failure mode surfaces through. Distinct variants for each rejection cause; no "TLS failed" catch-all.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: An operator rotates a counterparty's leaf certificate on the counterparty's renewal cadence (30–90 days) with **zero customer-visible session disconnects** across the rotation window.
- **SC-002**: **100%** of TLS handshakes against peers presenting a cert outside the FIXS-RC1 envelope (banned cipher, banned TLS version, RSA < 2048, RSA > operator cap, chain depth > 8, SAN list > 64, X.509 v1, expired against effective clock, per-cert DER > 16 KiB) are refused before any FIX-level data flows.
- **SC-003**: A build that wires a non-allowed cipher suite or TLS version into `CipherPolicy` **fails at compile time** with a diagnostic identifying the violation. No such build can be produced and shipped.
- **SC-004**: The cert-source plugin surface is small enough (no more than the [const §XIV.2] cap of 5 pure-virtual methods) that a counterparty-side HSM integration is implementable as a single user-provided class **without modifying or forking fixpp**.
- **SC-005**: Adopting session-cert pinning in a previously-CA-trust-only deployment requires only a configuration-time switch (selecting `mtls_pinned`) plus populating the pinset. **No fixpp source change. No recompile of fixpp.**
- **SC-006**: Every distinct TLS-policy failure mode surfaces as a **distinct, named error variant** the operator can recognise in logs / metrics. No "TLS failed" catch-all exists in the public surface.
- **SC-007**: A `Pinset::find` call on the handshake hot path completes within the design-doc latency ceiling (≤ 130 ns p99 on the lock-free libstdc++ ≥ 16 / libc++ floor; ≤ 200 ns p99 on the libstdc++ ≤ 15 internal-mutex fallback per [2g §6.3]) such that handshake-add overhead from the lookup is imperceptible at session-open time.
- **SC-008**: An operator-supplied `cert_source` whose signing path is bound to a non-session executor can be observed (in a trace or test harness) running its signing work **off the session strand**, with no `[2d §7.5]` strand-safety violation.

## Assumptions

This spec codifies the user-visible contract that the signed-off design doc `.specify/2g-tls.md` v0.4 (Gate A round 3 converged via Phase A) settles. The spec does **not** re-derive design decisions; it states the contract that downstream `/plan` will turn into tasks.

- **Design-doc inheritance**: 2g-tls.md v0.4 is the binding upstream. All §-anchored cites in this spec resolve there or in the inherited docs (constitution v0.2, architecture v0.2, 2a/2b/2c/2d/2e/2f signed-off).
- **TLS provider**: OpenSSL on Linux and Windows. No Schannel, no platform-native fallback, no other TLS backend in v1.0. (Locked by [const §XII.1].)
- **TLS version posture**: The compile-time allow-list admits both TLS 1.3 and TLS 1.2 (ECDHE-AEAD-only); negotiation order between them is owned by the 2h-transport `SSL_CTX` config, not by this feature. This spec locks *which* versions are admissible; 2h locks the *preference* in `SSL_CTX_set_min_proto_version` / `SSL_CTX_set_max_proto_version`. (Candidate /clarify probe.)
- **`mtls_pinned` bootstrap**: A session opened in `mtls_pinned` mode with an empty pinset fail-closes at session open with `error::tls_pin_empty_at_open` (per US3 acceptance scenario 5). An empty trust authority is a configuration error, not a deferred-populate signal. (Candidate /clarify probe — fail-closed is the default; the alternative is empty-pinset-accepts-any, which is unsafe by construction.)
- **`verify_peer` multi-violation ordering**: When a peer cert violates multiple DoS bounds simultaneously (e.g., RSA too large AND chain too deep AND SAN too long), `verify_peer` MAY short-circuit on the first violation hit. The surfaced error variant is the *first* violation hit, not an aggregated set. Detection-order is documented in the design doc §6.6. (Candidate /clarify probe — short-circuit is the default for cost; aggregate is the alternative for richer observability.)
- **HSM / TPM / KMS / vault impls are user-side**: this feature locks the `cert_source` interface; concrete impls are supplied by operators per [const §XII.8] and [const §XIV.4]'s no-dlopen rule.
- **C ABI ownership**: the C ABI for `cert_source`, `Pinset`, `SecurityProfile`, `CipherPolicy::is_allowed`, and the `FIXPP_ERR_TLS_*` coalescing groups is owned by **2i-capi**. This feature publishes the C++ source-of-truth and the per-doc-prefix coalescing rule that 2i consumes.
- **Control-plane reload trigger**: owned by **2j-controlplane**. This feature does not own runtime hot-reload of cert sources; rotation through the `Pinset` API is the only mid-session-mutable knob.
- **TLS-event log / OTel record schema**: owned by **2k-log-otel**. This feature publishes the named `error::tls_*` variants 2k will record; the record format itself is 2k's.
- **2h-transport boundary**: the OpenSSL `SSL_CTX` construction, the TLS handshake coroutine, and the `SSL_VERIFY_PEER` callback wiring all live in 2h-transport (the next Phase-4 feature). This feature publishes the `SecurityProfile`-to-`SSL_CTX`-config mapping table (FR-014) and the `verify_peer` validation predicate (FR-019/FR-020); 2h consumes both.
- **Session-FSM boundary**: the session FSM's consumption of `SecurityProfile` at `Session::open`, and the CompID-to-TLS-identity binding T-041, both live in the session Phase-4 module. This feature publishes the `peer_identity` value (FR-018); session consumes it.
- **Interop testing deferred to pre-v1.0**: per `[[project_release_interop_quickfix_fix8]]`, fixpp ↔ QuickFIX-cpp / Fix8 interop against TLS-active sessions is a release-gate, not a per-feature gate.
- **Non-goals (carried verbatim from design-doc §2)**: no Schannel; no TLS 1.0 / 1.1 / SSL; no application-layer encryption (`EncryptMethod(98) ≠ 0` rejection is wire-validator + session FSM, not 2g); no TLS 1.3 0-RTT / early data; no dynamic cipher reconfiguration; no PSK in v1.0; no CRL / OCSP infrastructure in v1.0; no mid-handshake pinset rotation; no `dlopen` plugin loading; no mid-session `SecurityProfile` or `cert_source` swap.
- **/clarify is mandatory** before /plan per [const §XVI.3] (security feature). Probes flagged above ("Candidate /clarify probe") are the priority targets for the /clarify pass.
