# 2g — TLS cert_source + Pinset rotation

**Status:** Draft v0.5 — post-sign-off targeted amendment (2026-08-29): the reproduced `[const §XII.5]` text was **deleted**, not refreshed. Article XII §5 was amended by constitution **v0.3 (2026-06-17, feature 043)** adding a fourth `SecurityProfile`; the copy here had gone false with nothing linking it to the article. Prior: Draft v0.4 — Gate A round 3 converged via post-cap pass (Phase A). ⚠️ **This document reproduces several constitutional articles verbatim — see "Appendix Z" at the END of this file.**
**Date:** 2026-05-09
**Owner:** Opus (Phase A drafter)
**Inherits:** constitution.md v0.2, architecture.md v0.2, 2a-decimal.md v0.3, 2b-wire.md v0.2, 2c-codegen.md v1.3, 2d-threading.md v0.4, 2e-msgstore.md v0.4, 2f-async-mutex.md v1.5
**Cites:** [const §VI.5], [const §VII.4], [const §VIII.1], [const §VIII.2], [const §VIII.3], [const §VIII.5], [const §X.4], [const §X.5], [const §XI.1], [const §XI.2], [const §XI.3], [const §XI.4], [const §XII.1], [const §XII.2], [const §XII.3], [const §XII.4], [const §XII.5], [const §XII.6], [const §XII.7], [const §XII.8], [const §XIII.3], [const §XIV.1], [const §XIV.2], [const §XIV.4], [const §XV.9], [const §XV.10], [const §XV.11], [const §XVII.1], [arch §1.2], [arch §2.3], [arch §3], [arch §4.4], [arch §4.5], [arch §4.6], [arch §4.7], [arch §4.10], [arch §5.1], [arch §5.3], [arch §5.4], [arch §5.5], [arch §5.6], [arch §5.7], [arch §6], [arch §8.1], [arch §10] row 2g, [arch Appendix B] (Pinning & rotation row), [SYN §3.2 Q6a] (ASIO native cancellation slots), [SYN §3.4 Q14] (cert source — DECIDED), [SYN §3.4 Q15] (pinset rotation — DECIDED), [FIXS §2.2] Mutual and Simple TLS protocol options, [FIXS §2.3] Leaf Certificate Pinning, [FIXS §3.1] Protocol version, [FIXS §3.2] Protocol features, [FIXS §3.3] Cipher suites, [FIXS §3.4] Certificate parameters, [FIXS §4.1] Sharing secrets, [FIXS §4.2] Storing secrets, [FIXS §4.3] Renewing secrets, [FIXS §4.4] Authorization linked to authentication, [FIXS RC1 §5] Certificate pinning and rotation (project-rolled-up cite per [arch Appendix B]; the underlying spec sections are §2.3 + §4.3), [2a §4.2] (`trap_throw`), [2a §6.5] (per-doc Tier 1 ceiling table precedent), [2b §6.4] (declaration-site lifetime annotation precedent), [2b §6.6] (parse-window allocation), [2c §4.8] (`owning_message_t<>` PMR-owned-string precedent), [2d §4.4] (`EngineConfig`), [2d §4.5] (`SessionConfig`), [2d §4.7] (two-phase close), [2d §4.8] (`session_executor`), [2d §6.5] (`cancellable_dispatch`), [2d §7.4] (executor-compat surface), [2d §7.5] (TLS strand-safety boundary), [2d §7.9] (effective-clock single rule), [2e §6.4] (writer-mutex contract precedent), [2e §6.6] (per-doc Tier 1 ceiling table precedent), [2f §4.1.1] (`async_mutex::async_lock(...)` surface — referenced, not consumed), [2f §6.5] (cancellation outcome + per-doc-prefix discipline; cancellation-group precedent), [2f §6.6] (Enforcement of [const §XV.9])
**Catalogue rows owned:** **T-006**, **T-007**, **T-008**, **T-011**, **T-013** (per [arch §4.6]). **Cross-cuts owned with 2h:** **T-039** (FIXS §3.4 — certificate parameters; 2g owns parsed-cert validation, 2h owns the OpenSSL handshake-time hook), **T-040** (FIXS §4.1 — secrets distribution / loading; 2g owns the `cert_source` interface that consumes whatever channel the operator distributes, 2h does not touch). **Cross-cut into session/ Phase-4:** **T-041** (FIXS §4.4 — authorization linked to authentication; 2g supplies the parsed peer-cert identity, the session-module Phase-4 spec performs the CompID-to-TLS-identity binding).
**Convergence log:** Appendix C — addresses Codex round-1 review (8 P1 / 5 P2 / 3 P3) and Opus round-1 adversarial review (combined post-judging 13 P1 / 7 P2 / 8 P3 + 3 root causes); addresses Codex round-2 review (1 P1 / 1 P2 / 1 P3) and Opus round-2 adversarial review (combined post-judging 4 P1 / 4 P2 / 5 P3; no new root causes); addresses Codex round-3 review (2 P1 / 0 P2 / 1 P3) and Opus round-3 adversarial review (combined post-judging 2 P1 / 0 P2 / 1 P3; no new root causes; round cap hit) via user-authorized post-cap line-edit pass; see Appendix C.

---

## §1 Goals

1. **Lock the `fixpp::tls::cert_source` plugin interface** at ≤ 5 pure-virtual methods per [const §XIV.2] / [arch §6], operationalising the [SYN §3.4 Q14] decision: pluggable, file-path default in v1.0, HSM/KMS/vault impls user-side via the same surface. v0.2 collapses `load_leaf` / `load_chain` / synchronous `sign(...)` into a single awaitable `load_credentials() -> awaitable<expected_t<local_credentials>>` whose `local_credentials::signer` carries `std::variant<software_key_ref, async_signer_ref>`; the `async_signer_ref` is awaitable-shaped and offloads HSM signing OFF the session strand per `[2d §7.5]`. The pure-virtual surface is **2 methods** (`load_credentials` + `load_trust_anchors`) — well under the [const §XIV.2] cap.
2. **Lock the `fixpp::tls::file_cert_source` default impl** as the v1.0-shipped reference: PEM/DER parse over a caller-supplied `cert_source::Config` (file-path + optional password callback for encrypted PEM); single CA bundle path; constitutional refusal to load anything outside the [const §XII.3] cipher / [const §XII.2] TLS-version / [const §XII.4] banned-cryptography envelope at chain-validation time. v0.2 publishes `make_file_cert_source(Config, std::pmr::memory_resource*) -> expected_t<std::shared_ptr<cert_source>>` as the [arch §6] rule-4 factory entry point.
3. **Lock the `fixpp::tls::Pinset` rotation API** per [SYN §3.4 Q15] / [FIXS RC1 §5]: `add(cert)` and `remove(cert)` are **separate explicit operations**; old certs remain usable until `remove` returns; **no atomic-swap shortcut**. Rotation is mid-session-mutable per [arch §5.6] (the architecture explicitly carves Pinset out of the frozen-config rule). Concurrent reads during rotation are correct without taking a coroutine-suspending lock. v0.2 publishes `Pinset::find(...) -> pin_view` (a value-typed handle that carries the `shared_ptr<const pin_snapshot>` keeping the matched entry alive) instead of the v0.1 raw `pin const*`, eliminating the dangling-on-rotation hazard.
4. **Lock the `fixpp::tls::CipherPolicy`** as a **compile-time** allow-list per [const §XII.3] — the engine refuses to load anything outside the four normative lists (TLS 1.3 suites, TLS 1.2 ECDHE-AEAD suites, key-exchange groups X25519 / secp256r1 / secp384r1, signature algorithms ECDSA P-256 / P-384, RSA-PSS over RSA keys of size ≥ 2048 bits). Banned suites per [const §XII.4] / [const §XV.11] (RC4, DES, 3DES, MD5, DH_anon, NULL, export-grade, CBC-mode TLS 1.2, SHA-1, 1024-bit RSA, TLS 1.3 0-RTT) are rejected at **compile** time. v0.2 also publishes `CipherPolicy::is_allowed(std::string_view) constexpr noexcept -> bool` for the C-ABI runtime path that 2i bridges into when an opaque profile-or-cipher string crosses the language boundary.
5. **Lock `SecurityProfile`** as the type the session uses to pick trust mode at construction per [const §XII.5]. ⚠️ **The member list is deliberately not enumerated here** — it is normative in §XII.5, which has since been amended (v0.3, 2026-06-17, feature 043); the shipped type is `include/fixpp/session/security_profile.hpp` (namespace `fixpp::session`, **not** `fixpp::tls` as earlier revisions of this document said). The profile is consumed by the `SecurityProfile`-to-OpenSSL-`SSL_CTX`-config adapter that 2g publishes; the `SSL_CTX` itself is built and owned by the transport per [arch §4.5] / 2h. The `[[deprecated]]` attribute is on the *enumerator* declaration in v0.2 (not in a comment), satisfying [const §XII.5]'s compile-time-diagnostic-at-construction rule. v0.2 publishes a normative `SecurityProfile`-to-OpenSSL-mode mapping table (§4.5.1) so 2h's wiring is unambiguous.
6. **Stay zero-allocation on the handshake-read hot path** per [const §VIII.5]: Pinset *lookup* during TLS handshake is read-only and on the session strand and must allocate zero (the `shared_ptr` snapshot capture is one acquire-load + one refcount RMW; the `pin_view` is value-typed; no heap touch). Cert *load* (loading bytes from disk, parsing, building the chain) is **cold-path** and allowed PMR allocation (§6.1).
7. **Stay exception-free across the session-handling window** per [arch §5.3]: every `expected_t<T>`-returning method is `[[nodiscard]]`; every accessor returning a non-owning view (`std::span<const Certificate>`, `std::string_view`, `pin_view`, the inner `span` of `load_credentials`'s awaitable result) carries `[[clang::lifetimebound]]` at the **declaration site of the abstract base** per [arch §5.5] and the [2b §6.4] precedent (v0.2 RC#1 close — the v0.1 "annotate only the override" workaround is retired). PMR throws are routed through `[2a §4.2]` `trap_throw` and surface as `error::tls_*` variants per §6.6. Construction-time configuration errors (bad cert file, malformed PEM at engine bootstrap before any session is open) are permitted to throw per the [arch §5.3] carve-out; the `make_file_cert_source(...)` factory wraps that boundary in `expected_t<...>` for non-construction-time callers (e.g., 2i's C ABI, future hot-reload).
8. **Honour ASIO native cancellation slots** per [const §XI.2] / [SYN §3.2 Q6a] for the cold-path `cert_source::load_credentials(...)` awaitable; cancellation propagates through `[2d §6.5]`'s `cancellable_dispatch` precedent (§6.4 publishes the recipe verbatim — read `co_await asio::this_coro::executor`, recover `fixpp::core::session_executor`, read `co_await asio::this_coro::cancellation_state`, post any parse/file-work handoff via `cancellable_dispatch` before returning `tls_load_cancelled`); the awaitable completes with `expected_t::unexpected{tls_load_cancelled}` (§6.4 / §6.6).
9. **Partition T-039 / T-040 between 2g and 2h** explicitly: 2g owns the `cert_source` interface (T-040: secrets are loaded *through* this interface — operators distribute via approved channels per [FIXS §4.1] and 2g consumes whatever the user passes) and the parsed-cert validation against [FIXS §3.4] (T-039: RSA between 2048 and `cert_source::Config::max_rsa_key_bits` (default 8192), ECDSA P-256/P-384, X.509 v2/v3 with v1 rejected, expiration-at-handshake against the *effective* clock per [2d §7.9]) **after** 2h's OpenSSL handshake produces the peer cert. 2h owns the `SSL_CTX` construction and the OpenSSL `SSL_VERIFY_PEER` callback wiring; 2g supplies the validation predicate that wiring calls. v0.2 also bounds `verify_peer`'s DoS surface — RSA upper-cap, per-cert ASN.1 envelope cap, SAN list cardinality cap (§1.1) — with three corresponding `error::tls_*` variants (§6.6).
10. **Declare the T-041 cross-cut into session/ Phase-4** explicitly: 2g publishes a `peer_identity` value (parsed from the verified peer cert) over which the session-module Phase-4 spec performs the CompID-to-TLS-identity binding per [FIXS §4.4]. 2g does not own that binding; it owns the value type and the accessor.

### §1.1 Magnitude domain / scope boundary

The first six caps bound v1.0 default-impl resource budgets. The last three (RC#2 / N-P1-4 close) are **DoS bounds** enforced at `verify_peer(...)` entry and surface a refusal `error::tls_*` variant when exceeded; they bound the policy core's input domain, not the constitutional invariants.

- **Pinset cardinality.** A typical FIXS pinset holds 1..4 leaf certs per counterparty (the active cert + 1..2 rotation candidates per [FIXS §4.3] / [FIXS RC1 §5]). v1.0 caps at **`Pinset::Config::max_pins = 16`** — high enough to absorb operational drift (multiple counterparties sharing a pinset, mid-rotation overlap), low enough that a linear scan during handshake is cheaper than a hash lookup. Concrete cost at the §6.3 latency ceiling: 16 × SHA-256 fingerprint compare ≤ 16 × 5 ns ≈ 80 ns.
- **Cert chain depth.** Bounded by `cert_source::Config::max_chain_depth = 8` (matches OpenSSL default). Self-signed leaf with no intermediate is depth = 1; production chains rarely exceed 4.
- **Cert object size.** Parsed `Certificate` is ≈ 256 B (subject DN view + issuer DN view + SAN list view + 32-byte fingerprint + raw DER span back-pointer); the raw DER bytes are owned by the `cert_source` implementation, not by `Certificate`. Pinset memory at the cap = 16 × 256 B = 4 KiB plus the raw-DER bytes the underlying source holds.
- **`cert_source::load_credentials` latency.** Cold-path file I/O + parse + chain build; expected ≈ 1–10 ms on commodity NVMe for typical chain sizes; bench-time ceiling **≤ 50 ms (soft)** per call (no [const §VIII.5] applicability).
- **`Pinset::find` latency on the handshake hot path.** Read-only lookup under the v1.0 16-pin cap, split as `snapshot_acquire` + `linear_scan_16` (≤ 100 ns); §6.3 row 1 specifies the platform-conditional ceiling (≤ 130 ns p99 on the lock-free libstdc++ ≥ 16 / libc++ floor; ≤ 200 ns p99 on the libstdc++ ≤ 15 internal-mutex fallback per v0.3 / N-P2-2). The v0.2 rebudget per RC#1 / Codex P2-3 escalation admits the `atomic<shared_ptr>` load + refcount RMW cost the v0.1 ≤ 100 ns ceiling failed to absorb; the v0.3 split admits the libstdc++ ≤ 15 mutex-fallback cost the v0.2 lock-free `static_assert` falsely rejected.
- **Rotation rate.** Operationally ≤ 1 rotation per cert-renewal cycle (typically 30–90 days). The mutex contention surface around `add` / `remove` is therefore vanishingly low; the algorithm is built for *correctness* under concurrent reads, not for high write throughput.
- **DoS cap — RSA key-size upper bound.** `cert_source::Config::max_rsa_key_bits = 8192` (rejects above; the lower bound is the [FIXS §3.4] / T-039 minimum of 2048 bits enforced separately). At 16384 bits, `BN_mod_exp` is O(N²)-ish; a 16 KiB modulus consumes microseconds-class verification time and admits a DoS-by-handshake-amplification vector (cf. Cloudflare 2014, OpenSSL CVE-2022-0778). `verify_peer` rejects with `error::tls_rsa_key_too_large`. Operators needing higher key sizes raise the cap explicitly in `Config`.
- **DoS cap — per-cert ASN.1 envelope size.** `cert_source::Config::max_cert_der_bytes = 16 * 1024` (16 KiB) — a wide v1.0 ceiling that absorbs every production cert size (typical leaf ≈ 1–2 KiB, intermediate ≈ 1–3 KiB, RSA-4096 ≈ 4 KiB). A peer presenting a 10 MiB certificate is rejected before parsing with `error::tls_cert_der_too_large`.
- **DoS cap — SAN list cardinality.** `cert_source::Config::max_san_entries = 64` — covers the wildest production SAN deployments (CDN-style `*.<region>.<svc>` rolls rarely exceed 32). A peer presenting a cert with > 64 SAN entries (DNS + URI combined) is rejected with `error::tls_san_entries_exceeded`. The cap also bounds `peer_identity`'s owning-vector allocation under RC#1's lifetime fix (§4.5).

### §1.2 Scope boundary — what 2g owns vs what it doesn't

2g **owns**:

- The `fixpp::tls::cert_source` pure-virtual interface (≤ 5 methods, §4.1).
- The `fixpp::tls::file_cert_source` default impl (§4.2).
- The `fixpp::tls::Pinset` value-typed class with `add` / `remove` / `find` / `contains` API (§4.3) and its add-then-remove invariants (§6.5).
- The `fixpp::tls::CipherPolicy` compile-time allow-list (§4.4) and its `static_assert`-based banned-cipher refusal (§6.1).
- The `SecurityProfile` enum and the `SecurityProfile`-to-`SSL_CTX`-config adapter shape (§4.5).
- The `fixpp::tls::Certificate` value-typed view + `fixpp::tls::peer_identity` (the parsed-cert subject + SAN value the session FSM consumes for T-041).
- The `error::tls_*` variants per §6.7 and their per-doc-prefix `FIXPP_ERR_TLS_*` C-ABI coalescing groups (delegated to 2i).

2g **does not own**:

- The OpenSSL `SSL_CTX` construction, the TLS handshake coroutine itself, or the `SSL_VERIFY_PEER` callback wiring — these are 2h (`asio_tls_transport`).
- The session FSM that consumes `SecurityProfile` at `Session::open` — owned by the Phase-4 session-module spec.
- The CompID-to-TLS-identity binding (T-041) — owned by the Phase-4 session-module spec.
- The HSM / TPM / KMS / vault-fetched concrete `cert_source` implementations — user-side per [const §XII.8] (the *interface* lives here; the *impls* are post-v1 best-effort or user-side).
- The C ABI surface for `cert_source` / `Pinset` / `SecurityProfile` — owned by **2i** (§5).
- The control-plane trigger that reloads certs at runtime — owned by **2j** (§7.7).
- TLS-event log/OTel record schema — owned by **2k** (§7.8).
- PSK authentication (T-012) — out of v1.0 priority surface per [arch Appendix B] and [const §XII.6]'s Pre-shared-key carve-out (P2; deferred).

---

## §2 Non-goals

- **No Schannel / no Windows-native TLS backend.** OpenSSL on both Linux and Windows per [const §XII.1].
- **No TLS 1.0 / 1.1 / SSL.** Compile-time-rejected per [const §XII.2] / [const §XV.11].
- **No application-layer encryption.** `EncryptMethod(98) ≠ 0` is rejected per [const §XII.7] / [const §XV.10] (the rejection itself is owned by the wire validator and the session FSM, not by 2g; 2g records the constitutional pin).
- **No 0-RTT / early-data on TLS 1.3.** Banned per [const §XII.3].
- **No dynamic cipher reconfiguration.** The allow-list is compile-time only per [const §XII.3]; runtime cipher overrides are not exposed.
- **No PSK in v1.0.** Pre-shared-key authentication is post-v1 per [arch Appendix B] (T-012 P2; tracked in `coverage-index.md`).
- **No certificate-revocation-list (CRL) / OCSP infrastructure in v1.0.** Operational reality (FIXS deployments use pinning, not revocation per [FIXS §2.3]); add post-v1 if a counterparty requires it.
- **No mid-handshake pinset rotation.** A rotation that lands during an in-flight handshake does not affect that handshake; the new pinset is picked up at the *next* handshake. (§6.5 / §7.1.)
- **No `dlopen`-based plugin loading** for `cert_source` per [const §XIV.4]. Compile-time selection only.
- **No mid-session swap of `SecurityProfile`.** Frozen at session open per [arch §5.6].
- **No mid-session swap of `cert_source`.** Frozen at session open per [arch §5.6]; the `Pinset` is the only mid-session-mutable TLS surface (architectural carve-out).

---

## §3 Inherited surface

This section quotes — verbatim, short excerpts — every binding section that constrains 2g. Each excerpt names the doc it inherits from and why it binds.

### §3.1 From [arch §4.6] — the tls/ surface inventory (the spine)

> **Public surface:**
>
> - `fixpp::tls::cert_source` — interface; `load_leaf`, `load_chain`, `sign_callback` for HSM flows. ≤5 pure-virtual `[SYN §3.4 Q14]`.
> - `fixpp::tls::file_cert_source` — default file-path impl.
> - `fixpp::tls::Pinset` — `add(cert)` / `remove(cert)` API; FIXS RC1 §5 add-then-remove rotation `[SYN §3.4 Q15]`.
> - `fixpp::tls::CipherPolicy` — compile-time allow-list per `[const §XII.3]`.
>
> **Catalogue rows:** T-006, T-007, T-008, T-011, T-013.

This is the spine of the present doc; §4 expands every bullet. The rows are claimed in Appendix A.

### §3.2 From [arch §5.6] — mid-session-mutable carve-out

> - **`SessionConfig` is value-typed and frozen at session open.** No mid-session reconfiguration of: dictionary, security profile, message store, executor, lock policy, dialect overlay. The supported pattern for any of these is close-and-reopen the session. Mutating ops on session-adjacent state that *do* admit mid-session change (e.g., pinset rotation per `[const §XII]`) go through their own APIs and are explicitly thread-aware. **Mid-session dialect-overlay swap is rejected categorically per `[2c §7.2]`** — there is no `Session::swap_dialect_overlay(...)` API in v1.0.

This is the architectural anchor for §4.3 and §6.5: `Pinset` is the single TLS surface that is **not** frozen at session open. The contract demands that the rotation API be thread-aware on its own (no piggy-backing on the session strand for serialisation). (v0.4 / round-3 P3-1 close: the §3.2 block now matches `architecture.md` line 407 byte-for-byte — earlier drafts bolded `pinset rotation` and elided the trailing dialect-overlay-categorical-rejection sentence; the byte-faithful Appendix D.1 "Before" block was already correct, this fixes the §3.2 inherited-surface-recap to honour the §3 preamble's "verbatim" promise.)

### §3.3 From [const §XII] — the security spine (full article relevant)

§XII.3 cipher allow-list (quoted verbatim because the exact lists are normative for §4.4):

> **Allowed cipher suites are an explicit compile-time allow-list. The engine refuses to load anything outside it (FIXS RC1 alignment).**
>
> - **TLS 1.3:** `TLS_AES_128_GCM_SHA256`, `TLS_AES_256_GCM_SHA384`, `TLS_CHACHA20_POLY1305_SHA256` (RFC 8446 §9.1 mandatory + recommended set).
> - **TLS 1.2:** ECDHE-(RSA\|ECDSA) with AES-128-GCM, AES-256-GCM, or ChaCha20-Poly1305; SHA-256 or SHA-384 PRF only.
> - **Key exchange groups:** X25519, secp256r1, secp384r1.
> - **Signature algorithms:** ECDSA (P-256, P-384), RSA-PSS (key size ≥ 2048 bits).
>
> Anything not on these four lists — including TLS 1.3 0-RTT data, static RSA key exchange, CBC-mode suites, SHA-1 signatures, and 1024-bit RSA — is rejected at compile time.

§XII.5 SecurityProfile rule — **NOT reproduced here. Read `[const §XII.5]` in `.specify/constitution.md`.**

> **Deleted 2026-08-29, deliberately not refreshed.** This block previously reproduced §XII.5 verbatim *"because the enum signature in §4.5 is normative"*, listing `mtls_ca` / `mtls_pinned` / `one_way_ca`. Article XII §5 was **amended** — constitution **v0.3, 2026-06-17**, Gate A folded into feature **043** — to add a fourth profile with **no TLS at all**. The copy stayed as written and so became false, and because it was labelled *normative* it read as more authoritative than an ordinary claim.
>
> The enumerated set lives in the constitution and in the shipped header; **this document deliberately
> holds no copy of it.** A copy is what went stale here the last time, silently, because nothing links
> a reproduction back to the article it reproduces.
>
> Refreshing the copy would re-arm the same trap for the next amendment, so it is **deleted** rather than updated: the enumerated set lives in the constitution, and the shipped type lives in `include/fixpp/session/security_profile.hpp`, whose header comment names the amendment that changed it. Read those two; do not re-copy them here.

§XII.8 cert_source rule (quoted verbatim):

> **Pluggable `cert_source` interface** with one default impl (file-based PEM/DER) in v1.0; HSM/TPM/cloud-KMS impls are user-side or future bundles (Article XIV).

§XII.6 pinset-rotation v1.0 commitment (quoted verbatim):

> **Certificate pinset rotation API** is a v1.0 feature (multiple valid peer certs per counterparty, FIXS §5).

### §3.4 From [const §XIV.2] — the ≤5 pure-virtual cap

> **Interface surfaces are small.** Each pluggable interface defines **≤5 pure-virtual methods**. Bigger surfaces are permitted only with an explicit design-doc justification (one paragraph naming the necessary methods and why each is irreducible). The justification is reviewed at Gate A.

§4.1 of this doc declares **exactly 2** pure-virtual methods on `cert_source` (`load_credentials` + `load_trust_anchors`) — well under the cap. No justification needed; the cap is honoured directly. v0.1's 4-pure-virtual split (`load_leaf` + `load_chain` + sync `sign` + `load_trust_anchors`) was collapsed in v0.2 / RC#2 close — see §4.1.

### §3.5 From [const §XV.9] — banned `std::mutex` in coroutine context

> **`std::mutex` in coroutine context.** Use `fixpp::sync::async_mutex` (Article XI §3).

This bears on §4.3 / §6.5. The consolidated rationale for using `std::shared_mutex` (writer-vs-writer surface only) over `fixpp::sync::async_mutex` lives in §6.5; §3.5 records only the inheritance pin. Note that `[const §XV.9]` and `[const §XI.3]`'s grep gate per `[2f §6.6]` bind on `std::mutex` (not `std::shared_mutex`); 2g's `Pinset` rotation surface is fully synchronous (`add` / `remove` return `expected_t<void>` directly), and the reader-side path (`find`, `snapshot`) is lock-free against an `std::atomic<std::shared_ptr<...>>`-published immutable snapshot — see §6.5 for the full rationale chain.

### §3.6 From [arch §6] — plugin pattern

> Each pluggable interface gets:
> 1. A pure-virtual class in the relevant module's public header.
> 2. **≤5 pure-virtual methods** `[const §XIV.2]`.
> 3. One default implementation shipped in v1.0.
> 4. A clear factory entry point that takes a `std::pmr::memory_resource*` plus interface-specific config.
> 5. Compile-time selection in v1.0; no `dlopen`.

`cert_source` is a v1.0 pluggable interface (per the table at [arch §6]). §4.1 / §4.2 satisfy all five rules.

### §3.7 From [2d §4.7] — two-phase close + cancellation contract

> - **Phase 1 — graceful close.** `Session::close()` opens a *child* `asio::cancellation_state` (the "graceful state") composed below the root, attempts a FIX `Logout` exchange under that child state…
> - **Phase 2 — teardown.** … the root cancellation slot fires `asio::cancellation_type::total`, propagating to in-flight transport `async_read` / `async_write`, the heartbeat `Clock::sleep_until`, the awaitable-mutex acquire (executor-compat surface owed by 2f), the application-callback dispatch (via `cancellable_dispatch` — §6.5), and the parser → `fromApp` chain.

`cert_source::load_credentials` is the only awaitable surface 2g exposes; it consumes [2d §6.5]'s `cancellable_dispatch` precedent for cancellation surfacing. The `async_signer_ref` (HSM signing path) is the second awaitable shape — it suspends OFF the session strand by posting via `cancellable_dispatch`, satisfying `[2d §7.5]`'s strand-safety boundary. Mid-handshake cancellation is the cold path; §6.4 spells the contract verbatim.

### §3.8 From [2d §7.5] — strand-safety boundary for TLS

> The TLS handshake coroutine (`async_handshake`) runs on the session strand. Cert rotation triggered via `Pinset::add(cert)` / `Pinset::remove(cert)` per `[arch §4.6]` may run on any thread — the pinset object is independently thread-safe (owned by 2g). When a rotation lands during a session, the session's next handshake (e.g., on reconnect) picks up the new pinset; mid-handshake rotation does not affect an in-flight handshake.

This is the locked contract surface 2g must deliver: thread-safe `Pinset` rotation independent of the session strand, mid-handshake rotation does not affect in-flight handshakes. Operationalised in §4.3 + §6.2 + §6.5.

### §3.9 From [2f §4.1.1] / [2f §6.5] — async_mutex surface (referenced, NOT consumed by Pinset)

> 2f's `async_mutex::async_lock(...)` returns `asio::awaitable<expected_t<async_lock_guard>>`; on `cancellation_type::total` it removes the waiter from the LIFO list and completes the awaitable with `expected_t::unexpected{error::sync_lock_aborted}`.

Recorded for completeness. The full consolidated rationale for the `std::shared_mutex` choice over `fixpp::sync::async_mutex` lives in §6.5 (v0.2 N-P1-3 close — three v0.1 rationales collapsed into one). The `async_mutex` dependency is *not* consumed by 2g; the cancellation-group precedent at `[2f §6.5]` is consumed by §6.6 for the `tls_load_cancelled` variant.

### §3.10 From [SYN §3.4 Q14] — cert source decision

> Per user: define a `cert_source` interface (load leaf, load chain, sign callback for HSM-backed flows). Ship a file-path implementation as the default. HSM, environment-injected, vault-fetched are all user-side or future-bundled impls.

§4.1 obeys: **2 pure-virtual methods** covering credential bundle (leaf + chain + signer-as-`variant<software_key_ref, async_signer_ref>` — `async_signer_ref` carries the HSM sign-callback case awaitably) plus trust-anchor loading. The `[SYN §3.4 Q14]` "load leaf, load chain, sign callback" decomposition is preserved at the *value* level (`local_credentials::leaf` / `local_credentials::chain` / `local_credentials::signer`); the *interface* level is collapsed because the v0.1 split surface (a) needed a third pure-virtual `sign(...)` whose synchronous shape blocked the session strand on HSM impls, and (b) gave software-key impls no path to expose a private-key handle to OpenSSL. v0.2's `local_credentials` carries the credential bundle as one value, the signer as a variant, and the engine never sees a private-key handle directly: software impls expose `software_key_ref` (engine-internal handle that 2h's OpenSSL wiring binds via `EVP_DigestSign*` against impl-owned key storage); HSM impls expose `async_signer_ref` (awaitable signing oracle that suspends off-strand). RC#2 close.

### §3.11 From [SYN §3.4 Q15] — pinset rotation decision

> Per user: follow FIXS. New cert added to the pinset before old cert is retired. API mirrors this — `pinset.add(cert)` succeeds while old certs are still active; `pinset.remove(cert)` is a separate explicit operation. No atomic-swap shortcut.

§4.3 + §6.5 obey: explicit `add` and `remove`, no `swap`, no `set`-the-list bulk operation. The §9 ordering test enforces.

This document refines the inherited surface; it does **not** diverge.

---

## §4 Public C++ API

### §4.1 `fixpp::tls::cert_source` — abstract interface (2 pure-virtual; ≤ 5 per [const §XIV.2] / [SYN §3.4 Q14])

v0.2 / RC#2 close: the v0.1 split surface (`load_leaf` + `load_chain` + synchronous `sign(...)` + `load_trust_anchors` = 4 pure-virtual) is collapsed to **2 pure-virtual** (`load_credentials` + `load_trust_anchors`). The credential bundle is now one awaitable value (`local_credentials`); the signer is a `std::variant<software_key_ref, async_signer_ref>` so HSM impls suspend off-strand and software impls expose an engine-internal key handle for OpenSSL's `EVP_DigestSign*` direct path. The pure-virtual count drops well under the [const §XIV.2] cap, leaving headroom for a future hot-reload hook.

```cpp
// include/fixpp/tls/cert_source.hpp
#include <asio/awaitable.hpp>
#include <asio/cancellation_type.hpp>
#include <memory>
#include <memory_resource>
#include <span>
#include <string_view>
#include <variant>
#include <fixpp/core/expected.hpp>
#include <fixpp/tls/certificate.hpp>

namespace fixpp::tls {

// software_key_ref — engine-internal handle to a parsed private key the
// implementation owns. The engine never sees raw key bytes; 2h's OpenSSL
// wiring binds the impl-owned EVP_PKEY* directly through this handle for
// the EVP_DigestSign*/EVP_DigestVerify* fast path. Lifetime is bounded by
// the holding cert_source instance.
struct software_key_ref {
    detail::private_key_handle handle;       // [[clang::lifetimebound]] on accessor; *this-bounded.
    int                        ossl_pkey_id; // OpenSSL EVP_PKEY_id (e.g., EVP_PKEY_RSA, EVP_PKEY_EC).
};

// async_signer_ref — HSM-style awaitable signing oracle. The signer is
// invoked on the session strand by 2h's wiring; the implementation MUST
// suspend off-strand for any blocking work (network round-trip to HSM /
// vault / KMS) by posting via [2d §6.5] cancellable_dispatch on a
// non-session executor; the signing call resumes on the session strand.
// The contract that signing-thread-affinity stays off-strand is the
// implementation's responsibility — the engine simply co_awaits.
struct sign_request {
    std::span<const std::byte> tbs;          // [[clang::lifetimebound]] on accessor; bytes-to-be-signed (TLS handshake digest).
    int                        sig_alg;      // OpenSSL NID for the signature algorithm.
};

struct sign_response {
    std::pmr::vector<std::byte> signature;   // owning; PMR-allocated by the impl from the per-handshake arena.
};

struct async_signer_ref {
    using sign_fn = std::function<                                    //
        asio::awaitable<core::expected_t<sign_response>>(             //
            sign_request const& req)>;                                //
    sign_fn sign;                            // never invoked synchronously on the session strand without a cancellable_dispatch hop.
};

// local_credentials — one value carrying everything OpenSSL's SSL_CTX
// configuration needs at handshake time. Returned by load_credentials();
// lifetime is bounded by the cert_source instance that produced it (the
// chain's Certificate views, the leaf's view, and software_key_ref's
// handle alias *this-owned storage).
struct local_credentials {
    Certificate                                     leaf [[clang::lifetimebound]];
    std::span<const Certificate>                    chain [[clang::lifetimebound]];   // root last.
    std::variant<software_key_ref, async_signer_ref> signer;                          // software handle OR awaitable HSM oracle.
};

// Abstract interface. EXACTLY 2 pure-virtual methods (≤ 5 per [const §XIV.2];
// well under the cap, no Gate-A justification needed).
class cert_source {
public:
    virtual ~cert_source() = default;

    // (1) Load the local credentials bundle (leaf cert + chain + signer).
    //     Awaitable so that fetch-from-vault / HSM-blocking / file-on-
    //     network-fs impls can suspend without blocking the session strand.
    //     Cancellation follows ASIO's native slot model per [const §XI.2] /
    //     [SYN §3.2 Q6a]; on cancellation_type::total the awaitable
    //     completes with expected_t::unexpected{error::tls_load_cancelled}
    //     (§6.4 publishes the cancellable_dispatch recipe verbatim — this
    //     is the second pluggable awaitable after [2d §4.1.1] Clock::sleep_until
    //     and inherits the same recipe-publication obligation).
    //     The returned local_credentials' view fields are *this-bounded;
    //     the [[clang::lifetimebound]] annotations on local_credentials::
    //     leaf / chain / signer's software_key_ref::handle are
    //     declaration-site facts at the abstract base per [arch §5.5] /
    //     [2b §6.4] precedent.
    [[nodiscard]] virtual asio::awaitable<core::expected_t<local_credentials>>
        load_credentials() = 0;

    // (2) Trust anchors (CA certs) used for SecurityProfile::mtls_ca and
    //     SecurityProfile::one_way_ca. The default file_cert_source returns
    //     a chain loaded from Config::ca_bundle_path. Implementations that
    //     do not provide CA trust (FIXS RC1 strict pinned-only deployments
    //     under SecurityProfile::mtls_pinned) MAY return an empty span and
    //     the SecurityProfile adapter (§4.5) verifies that mtls_pinned is
    //     in effect before accepting empty trust anchors.
    //     Lifetimebound at the abstract base per [arch §5.5] / [2b §6.4].
    [[nodiscard]] virtual core::expected_t<std::span<const Certificate>>
        load_trust_anchors() [[clang::lifetimebound]] = 0;
};

}  // namespace fixpp::tls
```

**Pure-virtual count:** 2. Under the [const §XIV.2] cap of 5 with three slots of headroom (a future hot-reload-poll hook + two vendor-extension slots remain available to post-v1 plugins). No Gate-A justification paragraph needed.

**Concept-vs-virtual:** chosen virtual because (a) `cert_source` is held by `std::shared_ptr<cert_source>` in `EngineConfig::default_cert_source` and `SessionConfig::cert_source` per [2d §4.4] / [2d §4.5] (already typed shared_ptr in 2d's surface; binding at the C++ ABI is irreducible without a virtual surface); (b) the awaitable return type of `load_credentials` is hard to express as a concept without leaking the implementation's promise type; (c) the C ABI (delegated to 2i) needs an opaque-handle shape (`fixpp_cert_source_t`) that requires a virtual surface to forward.

**Annotations at the declaration site (RC#1 close — v0.1's "annotate only the override" workaround is retired):**

- Every `expected_t<T>`-returning method carries `[[nodiscard]]`. (Per the brief's hard rule.)
- `load_trust_anchors` returns `std::span<const Certificate>` — view-shaped value aliasing impl-owned storage. `[[clang::lifetimebound]]` is at the **abstract-base declaration site**, exactly as the [2b §6.4] precedent (every constructor/member returning views carries the annotation at the binding declaration site, not on overrides only). Calls through `cert_source&` / `shared_ptr<cert_source>` see the annotation; Clang's `-Wdangling` chain holds.
- `local_credentials::leaf`, `local_credentials::chain`, and `software_key_ref::handle` carry `[[clang::lifetimebound]]` at the struct-member declaration site; the views are bounded by the source `cert_source` instance that produced the value (transitively — the `local_credentials` value itself is owning by-value, but its inner view fields alias the engine-side `cert_source`'s storage).
- The `sign_request::tbs` span is annotated `[[clang::lifetimebound]]` at the struct-member declaration site (the bytes are caller-owned by the OpenSSL handshake state); the `sign_response::signature` is owning (`std::pmr::vector<std::byte>`) per RC#2 — the v0.1 "caller copies before further use" temporal contract was unenforceable by `[[clang::lifetimebound]]` and is replaced by ownership.

**No `[[clang::lifetimebound]]` on `load_credentials`'s `asio::awaitable<...>`** — the awaitable itself is not a view; the inner `expected_t<local_credentials>` carries the view annotations on its struct-member declarations. Clang's lifetime-bound chain reaches into the awaitable's promise type through the struct-member annotations.

### §4.2 `fixpp::tls::file_cert_source` — default file-path impl

```cpp
// include/fixpp/tls/file_cert_source.hpp
namespace fixpp::tls {

class file_cert_source final : public cert_source {
public:
    struct Config {
        std::string                    leaf_path;       // PEM or DER (auto-detect by file extension + magic byte).
        std::string                    chain_path;      // PEM bundle; intermediates only or leaf+intermediates (deduped on load).
        std::string                    private_key_path;// PEM or DER; PKCS#8 or RFC1421-style.
        std::string                    ca_bundle_path;  // PEM bundle for CA trust anchors. May be empty for SecurityProfile::mtls_pinned.
        std::function<std::string()>   password_cb;     // Optional; called once at construction-time per [arch §5.3] carve-out only. May be empty.
        std::pmr::memory_resource*     mr {nullptr};    // PMR resource for parsed-cert storage. nullptr → engine default; the make_file_cert_source factory below makes mr a parameter so 2i / hot-reload callers do not depend on a Config field.
        std::size_t                    max_chain_depth{8};        // §1.1 cap (chain depth).
        std::size_t                    max_rsa_key_bits{8192};    // §1.1 DoS cap — RSA upper bound (lower bound 2048 from [FIXS §3.4]).
        std::size_t                    max_cert_der_bytes{16 * 1024}; // §1.1 DoS cap — per-cert ASN.1 envelope size.
        std::size_t                    max_san_entries{64};       // §1.1 DoS cap — SAN list cardinality.
    };

    // [arch §6] rule-4 factory entry point. Returns expected_t<...> for
    // non-construction-time callers (2i C ABI, future hot-reload). The
    // factory accepts the PMR resource as an explicit parameter (rule 4
    // requires it) and routes Config::mr through the parameter; either
    // the field or the parameter may be null but not both (otherwise
    // expected_t::unexpected{error::tls_cert_load_failed}). Parse failures
    // surface as expected_t::unexpected{error::tls_cert_parse_failed} or
    // tls_cert_load_failed; no exception is thrown by the factory.
    [[nodiscard]] static core::expected_t<std::shared_ptr<cert_source>>
        make_file_cert_source(Config cfg, std::pmr::memory_resource* mr) noexcept;

    // Direct-construction path for in-process C++ callers that prefer
    // throw-on-failure (per [arch §5.3]'s construction-time carve-out).
    // Construction loads and parses every file once. Once construction
    // returns, every cert_source method is exception-free per [arch §5.3]
    // hot-path discipline.
    explicit file_cert_source(Config cfg);

    ~file_cert_source() override;

    // Overrides — view-returning methods carry [[clang::lifetimebound]]
    // mirroring the abstract-base declaration site annotations (RC#1).
    [[nodiscard]] asio::awaitable<core::expected_t<local_credentials>>
        load_credentials() override;

    [[nodiscard]] core::expected_t<std::span<const Certificate>>
        load_trust_anchors() [[clang::lifetimebound]] override;

private:
    Config                                  cfg_;
    std::pmr::memory_resource*              mr_;             // Resolved at construction (Config::mr ?: engine default).
    std::pmr::vector<Certificate>           chain_;          // PMR-allocated; lifetime = *this.
    std::pmr::vector<Certificate>           trust_anchors_;  // PMR-allocated; lifetime = *this.
    Certificate                             leaf_;           // Aliases chain_.front() or carries its own storage.
    detail::private_key_handle              key_;            // OpenSSL EVP_PKEY*; impl-owned RAII; software_key_ref aliases this.
    // Detail: file-watch / inotify reload is post-v1; v1.0 is load-once at
    // construction. A session-close-and-reopen picks up new disk state.
};

}  // namespace fixpp::tls
```

Notes:

- **Construction-time exceptions allowed.** Per [arch §5.3]'s carve-out for construction-time configuration errors (mirrors the precedent in [2d §4.4] / [2e §4.4]). Once construction returns, every method is `expected_t<...>`. The `make_file_cert_source` factory wraps the throwing constructor in `expected_t<...>` for non-construction-time callers per [arch §6] rule 4.
- **Load-once.** v1.0 does not file-watch. A user that needs hot-reload runs `Session::close` + `Session::open` with a fresh `file_cert_source`; the session-module Phase-4 spec exposes the close/open helper. Post-v1 may add an inotify-driven `cert_source` impl as a separate plugin.
- **PMR.** All parsed-cert storage is in `mr_` (the engine default if `Config::mr` was null at construction; the `make_file_cert_source` factory parameter takes precedence over the `Config::mr` field). The vectors are PMR-aware; the OpenSSL `X509*` objects are wrapped in RAII handles and freed by the destructor.
- **Signer (software-key path).** `file_cert_source::load_credentials` returns `local_credentials` whose `signer` is `software_key_ref{handle = key_, ossl_pkey_id = EVP_PKEY_id(key_.get())}`. 2h's OpenSSL wiring binds `key_` directly through `SSL_CTX_use_PrivateKey(...)` for the `EVP_DigestSign*` fast path. HSM impls return `signer = async_signer_ref{...}` instead; 2h dispatches to the awaitable signing path off the session strand per `[2d §6.5]` / `[2d §7.5]`.
- **DoS caps.** `Config::max_rsa_key_bits` / `max_cert_der_bytes` / `max_san_entries` (§1.1) bound input domain at *both* ends of the credential lifecycle: at file load (refuses local certs that exceed the caps with `tls_cert_load_failed`) and at peer verification through `verify_peer` (§4.5; refuses peer certs with `tls_rsa_key_too_large` / `tls_cert_der_too_large` / `tls_san_entries_exceeded`).

### §4.3 `fixpp::tls::Pinset` — add-then-remove rotation

```cpp
// include/fixpp/tls/pinset.hpp
//
// Add-then-remove rotation per [SYN §3.4 Q15] / [FIXS RC1 §5].
// No atomic-swap shortcut; no bulk-set; no overwrite-on-add.
//
// Thread-safety: independently thread-safe per [2d §7.5]. add(...) and
// remove(...) may run on any thread; find(...) and snapshot() are the
// handshake-hot-path reads.
//
// Mutex choice rationale: see §6.5 (consolidated). Summary: the type is
// std::shared_mutex (not std::mutex); the use is fully synchronous; the
// reader path is lock-free.

#include <atomic>
#include <memory>
#include <memory_resource>
#include <shared_mutex>
#include <span>
#include <string_view>
#include <fixpp/core/expected.hpp>
#include <fixpp/tls/certificate.hpp>

namespace fixpp::tls {

// pin — owned diagnostic record + 32-byte fingerprint. v0.2 / RC#1 close:
// the v0.1 caller-owned-bytes-aliasing Certificate field is dropped; the
// snapshot only stores owned diagnostic fields (PMR-allocated copies of
// subject/SAN strings) so a long-lived shared_ptr<const pin_snapshot>
// can outlive the bytes the caller passed to add().
struct pin {
    std::array<std::byte, 32> sha256;            // SHA-256 fingerprint of the pinned leaf cert's DER bytes.
    std::pmr::string          subject_dn;        // PMR-copied at add() time from the caller-supplied Certificate.
    std::pmr::vector<std::pmr::string> san_dns;  // PMR-copied at add() time; bounded by Pinset::Config::max_pins indirectly + arena.
    core::time_point          added_at;          // Wall-clock UTC at add() time, sourced from effective_clock per [2d §7.9].
};

// pin_snapshot — the immutable container readers acquire via snapshot()
// or pin_view::snapshot. Always allocated PMR; lifetime is the holding
// shared_ptr's.
using pin_snapshot = std::pmr::vector<pin>;

// pin_view — value-typed lookup result for find(). Carries the snapshot
// shared_ptr so the matched entry stays alive past concurrent add() /
// remove() calls (RC#1 close — the v0.1 raw `pin const*` could dangle
// the moment a concurrent rotation swapped the snapshot).
struct pin_view {
    std::shared_ptr<const pin_snapshot> snapshot;                                   // pins the matched entry's lifetime (and the entire snapshot).
    pin const*                          value [[clang::lifetimebound]] = nullptr;   // bounded by *this (i.e., by `snapshot`); attribute is on the same line as the field per N-P3-1 close.

    [[nodiscard]] bool found() const noexcept { return value != nullptr; }
    explicit operator bool() const noexcept { return found(); }
};

class Pinset {
public:
    struct Config {
        // Lifetime contract (v0.4 / round-3 P1-1 close): `mr` MUST outlive
        // every `shared_ptr<const pin_snapshot>` the `Pinset` ever publishes
        // — i.e., the union of `Pinset` instance lifetime AND every reader-held
        // snapshot lifetime, NOT merely the `Pinset` instance itself. The §6.5
        // binding contract permits a reader to hold
        // a `shared_ptr<const pin_snapshot>` past `~Pinset()`; each `pin`
        // in the snapshot carries `pmr::string` / `pmr::vector<pmr::string>`
        // members whose allocators capture this resource at add() time and
        // call deallocate() on it at snapshot teardown. The default path
        // (null → engine-resolved `mr_`) is satisfied by construction —
        // the engine's PMR resource per `[2d §4.4]` outlives every session
        // and every Pinset that any session reaches via `[arch §5.6]`'s
        // mid-session-mutable carve-out. Callers passing a user-owned
        // `std::pmr::memory_resource*` (e.g., a user `monotonic_buffer_resource`
        // for tests or a dedicated arena) MUST keep it alive for the full
        // union of (a) every `shared_ptr<Pinset>` holder lifetime AND (b)
        // every `pin_view` / `shared_ptr<const pin_snapshot>` reader
        // lifetime — i.e., until the last reader-held snapshot drains.
        // The §9 seam #18 (post-`~Pinset()` snapshot lifetime) exercises
        // this contract under ASan/TSan.
        std::pmr::memory_resource* mr {nullptr};
        std::size_t                max_pins {16};   // §1.1 cap.
    };

    // [arch §6] rule-4 factory entry point. Returns expected_t<...> for
    // non-construction-time callers (2i, hot-reload). Direct-construction
    // is also supported.
    [[nodiscard]] static core::expected_t<std::shared_ptr<Pinset>>
        make_pinset(Config cfg, std::pmr::memory_resource* mr) noexcept;

    explicit Pinset(Config cfg = {});
    ~Pinset();

    Pinset(Pinset const&) = delete;
    Pinset& operator=(Pinset const&) = delete;
    Pinset(Pinset&&) noexcept;
    Pinset& operator=(Pinset&&) noexcept;

    // (1) Add a pin. Succeeds and returns expected_t<void>{} unless:
    //     - max_pins exceeded → expected_t::unexpected{error::tls_pinset_capacity_exhausted};
    //     - the pin (by SHA-256) is already present → expected_t::unexpected{error::tls_pin_already_present}.
    //     The new pin is observable to the next find() that begins after add()
    //     returns (release-acquire ordering on the snapshot pointer; §6.2).
    //     OLD pins are NOT removed — that is the explicit remove() call.
    //     add(...) deep-copies the diagnostic strings into the snapshot's
    //     PMR arena per RC#1; the input Certificate is consumed for its
    //     SHA-256 + subject_dn + san list and its own storage is not
    //     retained.
    [[nodiscard]] core::expected_t<void>
        add(Certificate const& cert);

    // (2) Remove a pin (by SHA-256). Returns expected_t<void>{} on success;
    //     returns expected_t::unexpected{error::tls_pin_not_found} if the
    //     fingerprint is not present. Other pins remain usable.
    //     The removed pin is NOT observable to any find() that begins after
    //     remove() returns (release-acquire ordering; §6.2).
    [[nodiscard]] core::expected_t<void>
        remove(std::array<std::byte, 32> const& sha256);

    // (3) Lookup-by-fingerprint on the handshake-hot path. v0.2 / RC#1
    //     returns a value-typed pin_view that carries the snapshot
    //     shared_ptr; the matched entry's lifetime is pinned for as long
    //     as the caller holds the pin_view. Bounded latency (§6.3 row 1
    //     split: snapshot_acquire ≤ 30 ns + linear_scan_16 ≤ 100 ns;
    //     ≤ 130 ns combined p99 at max_pins = 16).
    [[nodiscard]] pin_view
        find(std::array<std::byte, 32> const& sha256) const noexcept;

    // (4) Explicit-snapshot accessor — the published reachability for
    //     handshake-time pinset access. The TLS handshake (in 2h)
    //     captures snapshot() ONCE before per-peer-cert lookup, then
    //     scans the snapshot directly for the entire handshake (per the
    //     §6.5 binding contract — "TLS handshake-time pinset access MUST
    //     use Pinset::snapshot() captured once at handshake start, and
    //     scan the captured snapshot, not call find() repeatedly").
    [[nodiscard]] std::shared_ptr<const pin_snapshot>
        snapshot() const noexcept;

    // Diagnostic / test-only.
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] bool        contains(std::array<std::byte, 32> const& sha256) const noexcept;

private:
    Config                                                  cfg_;
    std::pmr::memory_resource*                              mr_;          // Resolved at construction.
    mutable std::shared_mutex                               writer_;      // serialises add() / remove() against each other; readers do NOT take it. See §6.5 for [const §XV.9] rationale.
    std::atomic<std::shared_ptr<const pin_snapshot>>        snapshot_;    // immutable snapshot; replaced (release) on every add/remove.
};

}  // namespace fixpp::tls
```

**Why no swap.** Per [SYN §3.4 Q15] the API mirrors FIXS RC1 §5 explicitly: `add` and `remove` are separate operations. There is **no** `Pinset::set(span<pin>)` and **no** `Pinset::swap(Pinset&)` exposed publicly. The §9 ordering tests (deterministic + stress) enforce that an add-then-remove sequence over the same fingerprint is observable as two distinct release edges to a concurrent reader.

**Why `pin_view` is a value, not a raw pointer.** v0.1's `find(...) -> pin const*` returned a pointer into the snapshot the local `shared_ptr` held; the local died on return, so the pointer dangled the moment a concurrent `add` / `remove` swapped the snapshot. `[[clang::lifetimebound]]` only annotates the static lifetime, not the runtime hazard. v0.2's `pin_view` carries the snapshot's `shared_ptr` BY VALUE — the matched entry stays alive for as long as the caller holds the view, regardless of concurrent rotation. RC#1 close.

**Mid-session-mutable carve-out per [arch §5.6].** `Pinset` is the only TLS surface that is not frozen at session open. The session reaches the live `Pinset` through `SessionConfig::pinset` (the published reachability shape — RC#3 close; v0.1's "`file_cert_source` typically exposes a `pinset()` accessor" claim is retired). The Appendix D §D.1 / §D.2 drop-ins materialise this for the orchestrator at sign-off (the abstract `cert_source` does NOT publish `pinset()`; downstream 2h reads `SessionConfig::pinset` only).

### §4.4 `fixpp::tls::CipherPolicy` — compile-time cipher allow-list per [const §XII.3]

```cpp
// include/fixpp/tls/cipher_policy.hpp
//
// Compile-time allow-list per [const §XII.3]. The four normative lists are
// encoded as constexpr arrays; static_asserts at this header refuse any
// build that adds a banned cipher. Runtime overrides are NOT exposed.
//
// The OpenSSL SSL_CTX configuration adapter (§4.5) reads these constants and
// configures SSL_CTX_set_ciphersuites / SSL_CTX_set_cipher_list /
// SSL_CTX_set1_curves_list / SSL_CTX_set1_sigalgs_list accordingly.

namespace fixpp::tls {

struct CipherPolicy {

    // ── TLS 1.3 suites (RFC 8446 §9.1 mandatory + recommended) ──
    static constexpr std::array<std::string_view, 3> tls13_suites {{
        "TLS_AES_128_GCM_SHA256",
        "TLS_AES_256_GCM_SHA384",
        "TLS_CHACHA20_POLY1305_SHA256",
    }};

    // ── TLS 1.2 suites (ECDHE + AEAD only; SHA-256 / SHA-384 PRF) ──
    static constexpr std::array<std::string_view, 6> tls12_suites {{
        "ECDHE-RSA-AES128-GCM-SHA256",
        "ECDHE-RSA-AES256-GCM-SHA384",
        "ECDHE-ECDSA-AES128-GCM-SHA256",
        "ECDHE-ECDSA-AES256-GCM-SHA384",
        "ECDHE-RSA-CHACHA20-POLY1305",
        "ECDHE-ECDSA-CHACHA20-POLY1305",
    }};

    // ── Key exchange groups ──
    static constexpr std::array<std::string_view, 3> kx_groups {{
        "X25519",
        "secp256r1",
        "secp384r1",
    }};

    // ── Signature algorithms ──
    // RSA-PSS is a signature padding scheme; the key-size constraint
    // ("RSA keys ≥ 2048 bits") is enforced separately at verify_peer
    // (§4.5) per [FIXS §3.4]. The sig_algs entries name the OpenSSL
    // sig_alg tokens; the key-size check is independent.
    static constexpr std::array<std::string_view, 4> sig_algs {{
        "ECDSA+SHA256",   // P-256
        "ECDSA+SHA384",   // P-384
        "RSA-PSS+SHA256", // PSS over RSA keys ≥ 2048 bits (key size enforced at verify_peer).
        "RSA-PSS+SHA384",
    }};

    // Compile-time refusal of the banned set. Any attempt to add a string
    // containing a banned token to any list above fires a static_assert.
    // Note: banned_tokens covers two refusal axes — (1) tokens [const §XII.4]
    // / [const §XV.11] explicitly bans (RC4, DES, 3DES, MD5, DH_anon, NULL,
    // EXPORT, TLS_RSA static-key-exchange, CBC, SHA1, 0RTT); (2) tokens
    // simply not on the [const §XII.3] allow-list (TLS_AES_128_CCM is
    // RFC 8446 §B.4 OPTIONAL and not on the constitutional allow-list,
    // so adding it to any allow-list array would silently slip past
    // [const §XII.3]; the banned_tokens entry is a belt-and-braces
    // intersection guard, NOT a constitutional ban).
    static constexpr std::array<std::string_view, 12> banned_tokens {{
        "RC4", "DES", "3DES", "MD5", "DH_anon", "NULL",
        "EXPORT", "TLS_RSA", "CBC", "SHA1",
        "TLS_AES_128_CCM",        // refused (not on [const §XII.3] allow-list — belt-and-braces intersection guard).
        "0RTT",                   // banned per [const §XII.3].
    }};

    // (See §6.1 for the consteval value-based refusal chain that enforces
    // the banned-vs-allow-list intersection.)

    // is_allowed — runtime accessor for the C-ABI / config-file boundary
    // (2i). Returns true iff the suite-or-group-or-sigalg string appears
    // in any of the four allow-list arrays AND does not contain any
    // banned_tokens substring. Marked constexpr so compile-time and
    // runtime callers share the implementation.
    [[nodiscard]] static constexpr bool
        is_allowed(std::string_view suite_or_group_or_sigalg) noexcept;
};

}  // namespace fixpp::tls
```

The consteval refusal chain in §6.1 verifies `tls13_suites` ∩ `banned_tokens` = ∅ and similarly for the other three lists. The `SSL_CTX` adapter (§4.5) builds the cipher string by joining the allow-list arrays; banned ciphers cannot appear because they are not in any allow-list array. The `is_allowed(...)` accessor (v0.2 / N-P2-3 close) is the published runtime entry point that 2i's C-ABI string-validation path consumes — `tls_cipher_not_allowed` (§6.6) is now an *owned* runtime variant (the C-ABI calls `is_allowed`, refuses on `false`, and surfaces the variant); the v0.1 phantom-runtime-path defect is closed.

### §4.5 `SecurityProfile` — enum + adapter

```cpp
// include/fixpp/tls/security_profile.hpp
#include <memory>
#include <memory_resource>
#include <span>
#include <string_view>
#include <fixpp/core/clock.hpp>
#include <fixpp/core/expected.hpp>
#include <fixpp/tls/certificate.hpp>
#include <fixpp/tls/cipher_policy.hpp>

namespace fixpp::tls { class cert_source; class Pinset; }

namespace fixpp::tls {

// Enum signature is normative per [const §XII.5] (quoted in §3.3 above).
// Default-constructs to the `unset` sentinel; SessionConfig per [2d §4.5]
// rejects the sentinel at Session::open with error::invalid_session_config
// (the [const §XII.5] no-implicit-default rule; the rejection is implemented
// in the session-module Phase-4 spec, but the sentinel value is owned here).
//
// v0.2 / Codex P2-2 close: [[deprecated]] is on the enumerator, not in a
// comment. [const §XII.5]'s "compile-time [[deprecated]] diagnostic at
// construction" is now actually emitted by the compiler when one_way_ca
// crosses into user code.  ⚠️ SUPERSEDED 2026-06-17 by feature 043 (constitution v0.3 amended [const §XII.5], adding a FOURTH enumerator). Block LEFT AS DESIGNED — it records what 2g specified. Shipped declaration is authoritative: include/fixpp/session/security_profile.hpp (namespace fixpp::session), whose header comment names the amendment. Do not re-copy it here — a copy is what went stale.
enum class SecurityProfile : std::uint8_t {
    unset       = 0,   // sentinel — not a valid choice; rejected at Session::open.
    mtls_ca     = 1,
    mtls_pinned = 2,
    one_way_ca  [[deprecated("one_way_ca is legacy interop; prefer mtls_pinned or mtls_ca")]]
                = 3,
};

// Adapter shape: builds an OpenSSL SSL_CTX configuration block from a
// SecurityProfile + cert_source + Pinset + Clock. The actual SSL_CTX is
// constructed and owned by 2h's asio_tls_transport per [arch §4.5]; 2g
// supplies the configuration descriptor.
//
// v0.2 / RC#2 close: the Clock field is now part of the published API.
// verify_peer consumes cfg.clock->now() to evaluate notBefore ≤ now ≤
// notAfter per T-039; the clock is the SESSION-scoped effective_clock
// per [2d §7.9] (not EngineConfig::clock directly — the v0.1 §4.5 prose
// conflated the two). 2h resolves effective_clock at session open via
// the [2d §7.9] rule and stores it in SslCtxConfig::clock before
// invoking verify_peer.
struct SslCtxConfig {
    SecurityProfile                                 profile;
    std::shared_ptr<cert_source>                    cs;
    std::shared_ptr<Pinset>                         pinset;       // null permitted only under SecurityProfile::mtls_ca + one_way_ca.
    std::shared_ptr<fixpp::core::Clock>             clock;        // [2d §7.9] effective_clock; verify_peer uses clock->now().
    CipherPolicy                                    ciphers {};   // value-typed; constexpr-only members.
    std::pmr::memory_resource*                      mr {nullptr}; // for peer_identity's owning SAN-string allocation; null → engine default.
};

// Build the SslCtxConfig, validating the SecurityProfile-vs-pinset/CA/clock
// combination. Reject with tls_invalid_security_profile if cfg.clock is null.
[[nodiscard]] core::expected_t<SslCtxConfig>
    make_ssl_ctx_config(SecurityProfile                       profile,
                        std::shared_ptr<cert_source>          cs,
                        std::shared_ptr<fixpp::core::Clock>   clock,
                        std::shared_ptr<Pinset>               pinset = nullptr,
                        std::pmr::memory_resource*            mr     = nullptr);

// Verification predicate plugged into 2h's SSL_VERIFY_PEER callback. Returns
// expected_t<peer_identity>{} on accept; expected_t::unexpected{tls_*} on
// reject. The OpenSSL X509_STORE_CTX is wrapped by the caller; this function
// is the policy core.
//
// peer_identity is OWNING (RC#1 close — N-P2-2): the SAN strings are PMR-
// copied into peer_identity's std::pmr::vector<std::pmr::string> members
// at verify_peer time; the views in san_dns_names / san_uris alias the
// owning vector and lifetimebound is on *this. Downstream session-module
// Phase-4 spec captures peer_identity by value across the T-041 binding
// without dangling. The PMR resource for the copies is cfg.mr (or engine
// default).
[[nodiscard]] core::expected_t<peer_identity>
    verify_peer(SslCtxConfig const& cfg, std::span<const Certificate> peer_chain);

// peer_identity — the parsed subject + SANs the session-module Phase-4 spec
// uses for the T-041 CompID-to-TLS-identity binding. Owning value; views
// alias the owning vectors with [[clang::lifetimebound]] on *this.
struct peer_identity {
    std::pmr::string                                 subject_dn;          // owned, PMR-allocated.
    std::pmr::vector<std::pmr::string>               san_dns_names_owned; // owned, PMR-allocated; bounded by max_san_entries.
    std::pmr::vector<std::pmr::string>               san_uris_owned;      // owned, PMR-allocated; bounded by max_san_entries.
    std::array<std::byte, 32>                        leaf_fingerprint;    // owned (32 bytes).

    // Convenience views — bounded by *this. The owning vectors above are
    // the storage; these accessors carry [[clang::lifetimebound]] at the
    // declaration site mirroring the [2c §4.8] owning_message_t<>
    // accessor pattern.
    [[nodiscard]] std::string_view
        subject_dn_view() const noexcept [[clang::lifetimebound]];

    [[nodiscard]] std::span<const std::pmr::string>
        san_dns_names() const noexcept [[clang::lifetimebound]];

    [[nodiscard]] std::span<const std::pmr::string>
        san_uris() const noexcept [[clang::lifetimebound]];
};

}  // namespace fixpp::tls
```

`verify_peer` enforces the T-039 cert-parameter validation per [FIXS §3.4]: RSA key size between 2048 (lower bound) and `cert_source::Config::max_rsa_key_bits` (upper bound, default 8192 — N-P1-4 DoS close), ECDSA P-256/P-384 only, X.509 v2/v3 with v1 rejected, `notBefore ≤ now ≤ notAfter` checked against **`cfg.clock->now()`** which is the session-scoped `effective_clock` per `[2d §7.9]` (RC#2 close — the v0.1 prose conflated `EngineConfig::clock` with `effective_clock`; v0.2 routes through `effective_clock` by API construction). DoS bounds enforced at entry: per-cert ASN.1 envelope ≤ `Config::max_cert_der_bytes` (default 16 KiB) → `tls_cert_der_too_large`; SAN list cardinality ≤ `Config::max_san_entries` (default 64) → `tls_san_entries_exceeded`; RSA key size ≤ `Config::max_rsa_key_bits` → `tls_rsa_key_too_large`. Each refusal surfaces through `tls_handshake_failed` group with the diagnostic field carrying the specific sub-reason (mirrors the `tls_handshake_failed` sub-reason pattern at §6.6).

#### §4.5.1 `SecurityProfile`-to-OpenSSL-mode normative mapping (Codex P2-5 close)

The table is normative for downstream 2h; 2h MUST configure the OpenSSL `SSL_CTX` exactly as specified. Each row is `SecurityProfile` → `SSL_VERIFY_*` flags, local-cert presence requirement, CA-anchor requirement, pinset requirement, pin-check policy. The mapping discharges `[const §XII.5]` + `[FIXS §2.2]` / `[FIXS §2.3]`.

| Profile | OpenSSL `verify_mode` | Local cert required | CA anchors | Pinset required | Pin check policy |
|---|---|---|---|---|---|
| `mtls_pinned` | `SSL_VERIFY_PEER \| SSL_VERIFY_FAIL_IF_NO_PEER_CERT` (acceptor) / equivalent client-side cert presentation (initiator) | Yes (both ends) | Optional (peer chain may be self-signed under FIXS RC1 strict pinning) | **Yes** | **Mandatory** — peer cert SHA-256 MUST appear in the live `Pinset`'s captured snapshot or `verify_peer` returns `tls_pin_mismatch`. |
| `mtls_ca` | `SSL_VERIFY_PEER \| SSL_VERIFY_FAIL_IF_NO_PEER_CERT` (acceptor) / equivalent client-side (initiator) | Yes (both ends) | **Required** | Optional | If `Pinset` is provided AND non-empty, the peer cert SHA-256 MUST appear in the snapshot OR be a CA-trusted leaf signed by an in-anchor CA; if `Pinset` is null or empty, CA-trust validation is the sole mechanism. |
| `one_way_ca` (deprecated) | `SSL_VERIFY_PEER` for the acceptor's view of the server cert; initiator-side cert presentation is implementation-policy (FIXS legacy interop) | Initiator: no; acceptor: server cert required | **Required** | **Forbidden** (null `Pinset` only) — `make_ssl_ctx_config` rejects a non-null `Pinset` under `one_way_ca` with `tls_invalid_security_profile` | n/a (no pinning) |
| `unset` | n/a — `make_ssl_ctx_config` rejects with `tls_invalid_security_profile` per `[const §XII.5]`'s no-implicit-default rule | n/a | n/a | n/a | n/a |

The §9 seam #9 (`SecurityProfile`-to-cipher-list mapping) is extended in v0.2 to verify each row of this table — the resulting `SslCtxConfig` configuration matches the OpenSSL flags exactly per row, the CA-required / pinset-required combinations enforce, and the `unset` / `mtls_pinned`-with-null-pinset / `one_way_ca`-with-non-null-pinset rejections all surface `tls_invalid_security_profile`.

### §4.6 Construction / lifetime / ownership rules

- **`cert_source` is engine-owned.** `EngineConfig::default_cert_source` per [2d §4.4] is `std::shared_ptr<cert_source>`; per-session override via `SessionConfig::cert_source` per [2d §4.5] is also `std::shared_ptr<cert_source>`. The `shared_ptr` shape lets the same `cert_source` instance back many sessions and lets a control-plane caller hold a reference for reload (§7.7).
- **`Pinset` is session-adjacent, mid-session-mutable.** Per [arch §5.6]'s carve-out, `Pinset` is reached through `SessionConfig::pinset` (RC#3 close — v0.1's "`file_cert_source::pinset()` accessor" claim is retired; the abstract `cert_source` does not and will not publish `pinset()`, and 2h does NOT downcast `cert_source*` to a concrete impl). The session FSM reads the live snapshot at handshake time via `Pinset::snapshot()` captured ONCE at handshake start (§6.5 binding contract); mid-session rotation does not affect in-flight handshakes per [2d §7.5]. The Appendix D §D.2 drop-in adds `pinset` to `[2d §4.5]` `SessionConfig` for orchestrator application at sign-off.
- **`Pinset::Config::mr` lifetime contract (v0.4 / round-3 P1-1 close).** The PMR resource backing `Pinset` MUST outlive every snapshot it produces, NOT merely the `Pinset` instance itself. The §6.5 invariant 4 + §6.5.1 binding contract permit a reader to hold a `shared_ptr<const pin_snapshot>` past `~Pinset()`; each `pin` in the snapshot carries `pmr::string` / `pmr::vector<pmr::string>` members whose allocators capture `cfg_.mr` at `add()` time and call `deallocate()` on it at snapshot teardown. The v1.0 default-impl path (null `Config::mr` → engine-resolved `mr_`) is satisfied by construction — the engine's PMR resource per `[2d §4.4]` outlives every session, every `Pinset` reached via `[arch §5.6]`'s carve-out, AND every `shared_ptr<const pin_snapshot>` any session reader can hold. Callers that pass a user-owned `std::pmr::memory_resource*` (e.g., a user-owned `monotonic_buffer_resource` for tests or a dedicated arena) own the contract: keep it alive for the full union of (a) every `shared_ptr<Pinset>` holder lifetime AND (b) every reader-held snapshot lifetime. Why this option (engine-anchored MR contract — Opus round-3 P1-1 counter-proposal (a)): zero per-snapshot cost (no extra `shared_ptr<memory_resource>` field per snapshot, no §6.3 row 1 `≤ 30 ns` regression on the lock-free hot path, no extra refcount RMW); zero API surface change (no new parameter on `make_pinset` or `Pinset::snapshot()`); matches the engine-default reachability already established for `cert_source` per [2d §4.4] / [2d §4.5]; and the user-supplied case is a documented contract aligned with the existing constitutional pattern of caller-owned executor / clock / store resources (e.g., the `[2d §4.5]` `executor_override` lifetime contract requires the caller's executor to outlive the session it backs). Counter-proposals (b) [snapshot owns a `shared_ptr<memory_resource>` guard] and (c) [snapshots cannot outlive `~Pinset()`] are sound but each carries a structural cost — (b) adds one indirection per snapshot allocation and would require touching §4.3's published `using pin_snapshot = std::pmr::vector<pin>;` shape (likely a wrapping struct); (c) breaks the §6.5.1 "capture-once-for-handshake" contract in any teardown-during-handshake scenario by demanding `~Pinset()` either assert / terminate on outstanding snapshots or be re-ordered after the last reader. Option (a) preserves §6.5.1 / §6.5 invariant 4 verbatim and pushes the lifetime requirement onto the resource owner — which is also where the constitutional ownership rule already sits.
- **`Certificate` is a non-owning view.** Lifetime is bounded by the `cert_source` instance; `[[clang::lifetimebound]]` is applied at every accessor returning `Certificate` or `std::span<const Certificate>` at the **abstract-base declaration site** (RC#1 close — the v0.1 "implementation-only annotation" workaround is retired; the `[2b §6.4]` precedent binds at the abstract base by [arch §5.5]).
- **`peer_identity` is OWNING** (RC#1 / N-P2-2 close): the SAN strings + subject DN are PMR-copied into per-instance `std::pmr::string` / `std::pmr::vector<std::pmr::string>` members at `verify_peer` time. The view accessors (`subject_dn_view`, `san_dns_names`, `san_uris`) carry `[[clang::lifetimebound]]` bound to `*this`; the v0.1 fragile cross-doc lifetime contract ("bounded by 2h's `SSL*` object") is replaced by self-ownership. Phase-4 session-module captures `peer_identity` by value across the T-041 binding without dangling.
- **`SslCtxConfig` is value-typed**; the `shared_ptr` members keep the `cert_source` / `Pinset` / `Clock` alive; the `CipherPolicy` is a value type whose members are all `static constexpr` plus the `is_allowed(...)` constexpr accessor.
- **`SecurityProfile`'s `unset` sentinel is the default-construction value** per [const §XII.5] (no implicit default). Session construction (Phase-4 spec) and `make_ssl_ctx_config(...)` both reject the sentinel.

Every `expected_t<T>`-returning method declared in §§4.1–4.5 carries `[[nodiscard]]`. Every accessor returning a non-owning view (`std::span<const Certificate>`, `std::string_view`, `pin_view`'s inner pointer, `peer_identity::subject_dn_view()` / `san_dns_names()` / `san_uris()`, `sign_request::tbs`, `software_key_ref::handle`'s accessor, `local_credentials::leaf` / `chain`) carries `[[clang::lifetimebound]]` at the declaration site of the abstract base or struct member as required.

---

## §5 Public C ABI

**2g is not the C-ABI doc.** Per [arch §4.10] the C ABI surface for `tls/` is delegated to **2i**; 2g defines the C++ shapes 2i will expose.

**Shapes 2i will need:**

- `fixpp_cert_source_t` opaque handle wrapping `std::shared_ptr<fixpp::tls::cert_source>` (consistent with 2i's other opaque-handle shapes — `fixpp_engine_t`, `fixpp_session_t`, `fixpp_store_t`).
- `fixpp_pinset_t` opaque handle wrapping `std::shared_ptr<fixpp::tls::Pinset>`.
- `fixpp_security_profile_t` C-side enum mirror (`FIXPP_SP_MTLS_CA = 1`, `FIXPP_SP_MTLS_PINNED = 2`, `FIXPP_SP_ONE_WAY_CA = 3`; no `unset` value crossing the C boundary — the C ABI rejects construction with a sentinel-equivalent error code instead).
- `fixpp_pin_add(fixpp_pinset_t*, const uint8_t sha256[32], ...)` and `fixpp_pin_remove(fixpp_pinset_t*, const uint8_t sha256[32])` reentrancy = `thread-safe` per [const §X.5] (since the underlying `Pinset` is independently thread-safe per [2d §7.5]).
- `fixpp_cert_source_load_credentials(...)` mapped through 2i's awaitable-to-C-callback bridge (the same shape 2i uses for `fixpp_session_close`'s cancellation surfacing). The `local_credentials` value crosses the C boundary as a struct-of-handles: `fixpp_certificate_t leaf`, `fixpp_certificate_array_t chain`, plus a discriminator `fixpp_signer_kind` (= `FIXPP_SIGNER_SOFTWARE` or `FIXPP_SIGNER_ASYNC`) and a payload union over `fixpp_software_key_t` and a C-callable awaitable signing-callback shape.
- `FIXPP_ERR_TLS_*` C-ABI coalescing groups per §6.6.
- `fixpp_cipher_policy_is_allowed(const char* token, size_t len) -> bool` mapping to `CipherPolicy::is_allowed(...)` for the runtime config-string validation path.

**No 2g shape is fundamentally incompatible with the C ABI delegation.** The `cert_source` virtual surface, the `Pinset` snapshot pointer, and the `SecurityProfile` enum all map cleanly to opaque-handle / enum / callback shapes 2i already uses. The `sign_request` / `sign_response` shapes in §4.1 are PoD-friendly (raw byte spans for input; PMR-owning `vector<byte>` for output that 2i copies out into a caller-owned buffer); 2i can mirror them as `(const uint8_t* tbs, size_t tbs_len, int sig_alg, void* user) -> int` callback signatures with a separate `fixpp_signer_completion(uint8_t* out, size_t* out_len)` finalisation step.

---

## §6 Behavioral contract

### §6.1 Allocation / exceptions / threading

**Hot path.** For 2g, the "hot path" is **the TLS handshake**, specifically:

- `Pinset::snapshot(...)` — captured ONCE at handshake start; one acquire-load on `std::atomic<std::shared_ptr<...>>` + one refcount RMW (≈ 30 ns p99 on the lock-free toolchain floor, ≈ 100 ns p99 on the libstdc++ ≤ 15 internal-mutex fallback — see §6.3 row 1 platform-conditional split per v0.3 / N-P2-2). The `pin_snapshot` is then scanned in-place per peer-cert lookup without further `shared_ptr` work.
- `Pinset::find(...) -> pin_view` — handshake-time lookup convenience; one snapshot acquire + linear scan over ≤ 16 pins; the returned `pin_view` carries the snapshot's `shared_ptr` so the matched entry stays alive for the caller's window (RC#1 close — the v0.1 raw `pin const*` could dangle on concurrent rotation). Both paths are **zero allocation** (no heap touch beyond the one refcount RMW). [const §VIII.5] applies in the [const §VIII.5]-extended sense: the handshake can fire between message dispatch (e.g., on reconnect) and so reuses the parse→`fromApp` zero-allocation discipline, mirroring [2d §6.2]'s extension to the heartbeat path.
- `verify_peer(...)` — handshake-time policy core; the parse + validate work runs against caller-owned bytes and a PMR arena (`SslCtxConfig::mr` or engine default) for `peer_identity`'s owning SAN-string copies; **zero global heap** when the arena is provided.

**Cold path.** Cert *load* (file I/O + PEM/DER parse + chain build) is cold-path:

- `cert_source::load_credentials` is the only awaitable entry point on the abstract base; allowed PMR allocation; allowed (rare) global heap when PMR is null and HALO does not fire. The `async_signer_ref::sign(...)` awaitable is the second awaitable shape — it is invoked at handshake time by 2h's wiring but suspends OFF the session strand via `cancellable_dispatch` per `[2d §6.5]` / `[2d §7.5]`, so its cost is not budgeted on the strand's hot path.
- `file_cert_source` construction loads everything once at engine bootstrap (allowed exceptions per [arch §5.3] construction-time carve-out); the `make_file_cert_source` factory wraps that path in `expected_t<...>` for non-construction-time callers.

**Compile-time enforcement** (per [const §XII.3] / [const §XV.11]).

v0.2 / Codex P1-7 close: the v0.1 sketch was not valid C++ — `s` was a runtime `for`-loop variable used as a non-type template argument, which never compiles, and `std::string_view` is not a structural NTTP. v0.2 makes the consteval functions **value-based** (no template parameters over `string_view` values; loops are plain `consteval` `for` over the `span<const string_view>` arguments). The same body is shared between the compile-time `static_assert` chain AND the public `CipherPolicy::is_allowed(string_view)` accessor (§4.4), so compile-time and runtime callers exercise one implementation.

```cpp
// include/fixpp/tls/cipher_policy.hpp — refusal chain (v0.2 value-based).
namespace fixpp::tls::detail {
    [[nodiscard]] consteval bool
        contains_any_substring(std::string_view s,
                               std::span<const std::string_view> needles) noexcept
    {
        for (std::string_view n : needles)
            if (!n.empty() && s.find(n) != std::string_view::npos) return true;
        return false;
    }

    [[nodiscard]] consteval bool
        any_banned(std::span<const std::string_view> list,
                   std::span<const std::string_view> banned) noexcept
    {
        for (std::string_view s : list)
            if (contains_any_substring(s, banned)) return true;
        return false;
    }
}

[[nodiscard]] constexpr bool
    CipherPolicy::is_allowed(std::string_view tok) noexcept
{
    auto in = [tok](auto const& list) noexcept {
        for (std::string_view s : list) if (s == tok) return true;
        return false;
    };
    auto banned = std::span<const std::string_view>{
        CipherPolicy::banned_tokens.data(),
        CipherPolicy::banned_tokens.size() };
    if (detail::contains_any_substring(tok, banned)) return false;
    return in(CipherPolicy::tls13_suites)
        || in(CipherPolicy::tls12_suites)
        || in(CipherPolicy::kx_groups)
        || in(CipherPolicy::sig_algs);
}

static_assert(!fixpp::tls::detail::any_banned(
                  std::span<const std::string_view>{CipherPolicy::tls13_suites.data(),
                                                    CipherPolicy::tls13_suites.size()},
                  std::span<const std::string_view>{CipherPolicy::banned_tokens.data(),
                                                    CipherPolicy::banned_tokens.size()}),
              "TLS 1.3 suite list contains a banned token (per [const §XII.3] / [const §XV.11]).");
static_assert(!fixpp::tls::detail::any_banned(
                  std::span<const std::string_view>{CipherPolicy::tls12_suites.data(),
                                                    CipherPolicy::tls12_suites.size()},
                  std::span<const std::string_view>{CipherPolicy::banned_tokens.data(),
                                                    CipherPolicy::banned_tokens.size()}),
              "TLS 1.2 suite list contains a banned token.");
static_assert(!fixpp::tls::detail::any_banned(
                  std::span<const std::string_view>{CipherPolicy::kx_groups.data(),
                                                    CipherPolicy::kx_groups.size()},
                  std::span<const std::string_view>{CipherPolicy::banned_tokens.data(),
                                                    CipherPolicy::banned_tokens.size()}),
              "Key-exchange group list contains a banned token.");
static_assert(!fixpp::tls::detail::any_banned(
                  std::span<const std::string_view>{CipherPolicy::sig_algs.data(),
                                                    CipherPolicy::sig_algs.size()},
                  std::span<const std::string_view>{CipherPolicy::banned_tokens.data(),
                                                    CipherPolicy::banned_tokens.size()}),
              "Signature-algorithm list contains a banned token.");
```

The §9 seam #8 (cipher allow-list rejection via `try_compile`) verifies that adding a CBC suite to any allow-list array fires the expected `static_assert`; the implementation now actually compiles, so the constitutional `[const §XII.3]` / `[const §XV.11]` guarantee is operational, not vacuous (Codex P1-7 close).

**Exception-free across handshake.** `verify_peer`, `Pinset::find`, `Pinset::snapshot`, `Pinset::add`, `Pinset::remove`, `cert_source::load_credentials`, `cert_source::load_trust_anchors`, `async_signer_ref::sign` all return `expected_t<...>` (or `awaitable<expected_t<...>>`) and never throw across the session-strand-bound handshake window per [arch §5.3]. PMR allocation throws on `Pinset::add` (snapshot clone allocates) and on `verify_peer` (peer_identity SAN-string copies) are routed through `[2a §4.2]` `trap_throw` and surface as `error::tls_pinset_alloc_failed` / `error::tls_handshake_failed` respectively.

### §6.2 Memory ordering on Pinset rotation

The rotation algorithm publishes an immutable snapshot under a writer mutex; readers acquire-load the snapshot pointer.

| Site | Operation | Ordering | Pairing partner | Rationale |
|---|---|---|---|---|
| `Pinset::add` (writer) | `writer_.lock()` | `acquire` (mutex) | `writer_.unlock()` (release) | Serialises writers against each other; readers are unaffected. |
| `Pinset::add` (writer) | clone current snapshot, append new `pin`, build new immutable `vector<pin>` | (single-threaded under writer lock) | — | The new vector is fully constructed before the snapshot pointer flips. |
| `Pinset::add` (writer) | `snapshot_.store(new_snapshot, memory_order_release)` | release | `snapshot_.load(memory_order_acquire)` (reader) | Release publishes every prior write to the new vector's storage. |
| `Pinset::remove` (writer) | clone, erase by SHA-256, build new vector, `snapshot_.store(release)` | release | reader's acquire-load | Same publication primitive; the removed pin is no longer reachable through the new snapshot. |
| `Pinset::find` (reader) | `auto snap = snapshot_.load(memory_order_acquire); /* linear scan; pin_view{snap, &snap->[i]} */` | acquire | writer's release-store | Acquire pairs with release; the returned `pin_view` carries `snap` BY VALUE (one refcount RMW), so the matched `pin const*` stays valid past concurrent rotation. |
| `Pinset::snapshot()` (reader) | `return snapshot_.load(memory_order_acquire);` | acquire | writer's release-store | Same pairing; caller's `shared_ptr` keeps the snapshot alive past subsequent `add`/`remove`. The handshake-time path (per §6.5 binding contract) calls this ONCE at handshake start. |

The publication primitive is `std::atomic<std::shared_ptr<const pin_snapshot>>`. v0.2 / Codex P2-3-escalated-to-P1 close (refined v0.3 / N-P2-2): the `find` latency budget (§6.3 row 1) is split — `snapshot_acquire` covers one acquire-load + one refcount RMW, and `linear_scan_16` ≤ 100 ns covers 16 × SHA-256 fingerprint compare (≤ 100 ns; the v0.1 80 ns estimate underbudgeted memory-fetch tail latency). v0.3 / N-P2-2 close: the v0.2 `static_assert(std::atomic<std::shared_ptr<...>>::is_always_lock_free)` is **softened to a toolchain-floor note** rather than a hard `static_assert`, because libstdc++ ≤ 15 takes an internal mutex in `<bits/shared_ptr_atomic.h>` and would fail the assert on the project's Tier 1 Linux/Clang baseline today. Instead, the §6.3 ceiling is **platform-conditional**: ≤ 30 ns p99 on platforms where the implementation is genuinely lock-free (libstdc++ ≥ 16 with `__atomic_shared_ptr`; libc++ on x86_64 with `cmpxchg16b`); ≤ 100 ns p99 on platforms where the implementation falls back to an internal mutex (libstdc++ ≤ 15 on glibc). The §9 seam #4 records the platform-bench tuple. Tier 2/3 platforms outside `[const §II]` scope are not benched. The seqlock fallback is dropped from the v0.3 design (the v0.1 admission of a fallback contradicted the publication primitive choice).

**Snapshot PMR-resource lifetime (v0.4 / round-3 P1-1 close).** The `shared_ptr<const pin_snapshot>` published here keeps the snapshot's `vector<pin>` storage alive for as long as a reader holds the `shared_ptr`, but each `pin` in the snapshot carries `pmr::string` / `pmr::vector<pmr::string>` members whose allocators were bound at `add()` time to `Pinset::cfg_.mr`. Snapshot teardown therefore calls `deallocate()` on `cfg_.mr` for each `pin`'s owning members. The `Pinset::Config::mr` field's lifetime contract (§4.3 / §4.6 ownership rules) requires the resource to outlive every snapshot the `Pinset` produces — not just the `Pinset` itself — so a reader-held outliving snapshot's eventual destruction is well-defined. The default `mr` path (null → engine-default) satisfies this by construction; user-supplied `mr` is a documented caller contract.

**Add-then-remove invariant** (the [SYN §3.4 Q15] / [FIXS RC1 §5] mandate). Two distinct release edges are observable to a concurrent reader; an `add` that is followed by a `remove` of the same pin produces two separate snapshot publications, never a single CAS. The §9 ordering test enforces.

### §6.3 Latency Tier 1 ceilings

Per [arch §5.6] / [const §VIII.1] / [const §VIII.2] / [const §VIII.3] / [2a §6.5] / [2d §6.3] / [2e §6.6] / [2f §6.3] precedent (per-doc Tier 1 ceiling tables).

| Operation | Tier 1 ceiling | Rationale |
|---|---|---|
| `Pinset::snapshot()` — handshake hot-path "capture once" — `snapshot_acquire` | **≤ 30 ns p99** on the lock-free toolchain floor (libstdc++ ≥ 16 with `__atomic_shared_ptr`; libc++ on x86_64 with `cmpxchg16b`); **≤ 100 ns p99** on the libstdc++ ≤ 15 internal-mutex fallback path (v0.3 / N-P2-2 close) | One acquire-load on `std::atomic<std::shared_ptr<const pin_snapshot>>` (≈ 5–10 ns lock-free; mutex-take on libstdc++ ≤ 15 contributes ~50–80 ns of cache-line traffic) + one refcount RMW on the inner `shared_ptr` control block (≈ 5–10 ns + cache-line traffic). The §9 seam #4 records the platform-bench tuple so CI knows which ceiling applies on the runner. |
| `Pinset::find(sha256)` — convenience path = `snapshot_acquire` + `linear_scan_16` | **≤ 130 ns p99** at `max_pins = 16` on the lock-free floor; **≤ 200 ns p99** on the libstdc++ ≤ 15 fallback path (v0.3 / N-P2-2 close) | Composes the row above with 16 × SHA-256 fingerprint compare (32-byte memcmp; ≤ 6 ns each on warm cache; ≤ 100 ns total). The combined ceiling is the sum of the two split rows under the same toolchain-floor / fallback split. |
| `Pinset::add(pin)` / `Pinset::remove(sha256)` — writer | **≤ 5 µs p99** at `max_pins = 16` | Writer lock (shared_mutex acquire) + clone + append/erase + atomic shared_ptr publish + writer unlock. Bench-time soft ceiling; rotation rate is operationally < 1/30-days so a regression here is not user-observable. |
| `verify_peer(...)` — handshake-hot path (excluding underlying X.509 verify) | **≤ 50 µs p99** | DoS-cap entry checks (RSA bits ≤ 8192; cert DER ≤ 16 KiB; SAN cardinality ≤ 64) + subject DN extraction + SAN parse + key-strength check + expiration check against `cfg.clock->now()` (`effective_clock` per `[2d §7.9]`) + PMR-arena copy of subject DN + SAN strings into `peer_identity`'s owning vectors. The OpenSSL `X509_verify_cert` call is the dominant cost and is owned by 2h; this ceiling covers the 2g policy core only. |
| `verify_peer(...)` — DoS-bound entry checks | **≤ 1 µs p99** | Three integer comparisons + one ASN.1 length read. Sub-microsecond by construction; the cap is not a regression risk axis. |
| `cert_source::load_credentials` — cold path | **≤ 50 ms (soft)** per call | Awaitable (one file read for chain + parse + `local_credentials` build); bench-time ceiling; allowed disk I/O. The `software_key_ref` construction is microseconds-class (just packaging an existing `EVP_PKEY*`); the awaitable shape exists for HSM impls that need to fetch over a network. |
| `async_signer_ref::sign(...)` — handshake-hot path (off-strand) | **≤ 1 ms p99** for software-key impls (when an impl chooses async over the software-key fast path); HSM-backed impls bench separately | Software ECDSA/RSA-PSS sign at ≈ 200 µs–1 ms; HSM round-trip is impl-specific. The 2h fast-path uses `software_key_ref` + `EVP_DigestSign*` directly (microseconds-class) for the in-process software-key case; this row covers the awaitable signing path. |

CI flags > 5 % regression on the hot-path rows; > 2 × on the cold-path rows. Per [const §VIII.1] / [const §VIII.2] / [const §VIII.3] (perf-sensitive modules need benchmarks + regression budgets) the `Pinset::snapshot` and `Pinset::find` rows are the binding gates; v0.2 admits (rather than imagines) the `atomic<shared_ptr>` cost.

### §6.4 Cancellation contract — `load_credentials` recipe

`cert_source::load_credentials` is the primary awaitable surface 2g exposes; `async_signer_ref::sign` is the secondary one. Both consume `[2d §6.5]`'s `cancellable_dispatch` recipe verbatim. v0.2 / Codex P1-5 close: the recipe is **published** here (the v0.1 prose named the precedent without retyping the method body).

**Recipe — every implementation of `load_credentials` (and `async_signer_ref::sign`) MUST follow this body shape (mirrors `[2d §4.1.1]` Clock implementer's recipe — the second pluggable awaitable inherits the same obligation):**

```cpp
// Inside an implementation of cert_source::load_credentials():
asio::awaitable<core::expected_t<local_credentials>>
file_cert_source::load_credentials() {
    // 1. Read the awaiter's bound executor and recover the project-owned
    //    fixpp::core::session_executor wrapper per [2d §4.8]. For non-session
    //    contexts (e.g., engine bootstrap), the wrapper is the engine's
    //    default-cert-source executor; for in-session contexts, it is the
    //    session_executor for the session that initiated the load.
    auto exec = co_await asio::this_coro::executor;

    // 2. Read the awaiter's cancellation_state per [SYN §3.2 Q6a] /
    //    [const §XI.2].
    auto cs = co_await asio::this_coro::cancellation_state;

    // 3. Reap pre-I/O cancellation: if the slot already shows total, complete
    //    immediately with tls_load_cancelled.
    if (cs.cancelled() != asio::cancellation_type::none)
        co_return core::expected_t<local_credentials>{
            unexpect, error::tls_load_cancelled };

    // 4. For any disk/network/HSM work, post the handoff via [2d §6.5]
    //    cancellable_dispatch on a non-session executor (or, for already-
    //    cached state, just return the prepared local_credentials directly).
    //    cancellable_dispatch returns awaitable<expected_t<void>>;
    //    expected_t::unexpected{dispatch_aborted} from the dispatch maps to
    //    tls_load_cancelled at this boundary.
    auto dispatched = co_await fixpp::core::cancellable_dispatch(
        exec, cs.slot(),
        [this]() { /* parse cached bytes; build local_credentials */ });

    if (!dispatched)
        co_return core::expected_t<local_credentials>{
            unexpect, error::tls_load_cancelled };

    co_return core::expected_t<local_credentials>{ /* the built value */ };
}
```

The recipe is enforced at §9 seam #11 (cancellation contract on `cert_source::load_credentials`) under both `per_session_strand` and `direct_executor` modes per `[2d §4.8]`.

- The awaitable consumes the [2d §6.5] `cancellable_dispatch` precedent: `asio::cancellation_type::total` causes the awaitable to complete with `expected_t::unexpected{error::tls_load_cancelled}`.
- The awaitable resumes on the awaiter's bound executor (per `[2d §7.4]` — the `session_executor` wrapper for in-session callers).
- Cancellation that arrives **between** `co_await cs->load_credentials()` issuing the call and the awaitable taking its first suspension is reaped at recipe step 3 without I/O work; the awaitable returns `tls_load_cancelled` immediately.
- Cancellation mid-disk-I/O depends on the impl; `file_cert_source` uses `asio::posix::stream_descriptor` (Linux) / `asio::windows::random_access_handle` (Windows) — both honour ASIO cancellation slots — and surfaces the cancellation as `tls_load_cancelled`.

Synchronous methods (`Pinset::find` / `snapshot` / `add` / `remove`, `load_trust_anchors`, `verify_peer`, `make_ssl_ctx_config`, `make_file_cert_source`, `make_pinset`) do not have a cancellation contract beyond the constitutional return-via-`expected_t` rule; they do not suspend, so there is nothing to cancel.

### §6.5 FIXS rotation invariants — add-then-remove; no atomic-swap; old cert remains usable until explicit `remove()`

The four invariants (operationalised in §4.3 / §6.2; tested in §9):

1. **`add(p)` does not remove anything.** The pre-`add` snapshot's pins all remain in the post-`add` snapshot; `add`'s only effect is to append `p`.
2. **`remove(fp)` does not add anything.** The post-`remove` snapshot is the pre-`remove` snapshot minus the entry whose SHA-256 = `fp` (or unchanged + `tls_pin_not_found` if absent).
3. **No atomic-swap.** There is no public API that mutates the pinset in a single atomic step covering both an add and a remove. `add` then `remove` is two snapshot publications; reordering by the writer (writer-side serialisation under `writer_`) is not permitted.
4. **Old cert remains usable until `remove(fp)` returns.** A handshake that began before `remove(fp)` returns may have already loaded the pre-`remove` snapshot (the snapshot is `shared_ptr`-pinned by the reader); subsequent handshakes pick up the post-`remove` snapshot.

#### §6.5.1 Handshake-time pinset access — binding contract (v0.2 N-P2-1 close)

**TLS handshake-time pinset access MUST use `Pinset::snapshot()` captured ONCE at handshake start, not repeated `find(...)` calls per peer-cert lookup.** The handshake's pinset-relevant work then operates against the captured `shared_ptr<const pin_snapshot>` directly (linear scan or hash on the captured snapshot). This contract pins the pre-rotation snapshot across the entire handshake's many internal steps (chain validation, expiry check, SAN match, etc.) and makes the §2 non-goal "rotation that lands during an in-flight handshake does not affect that handshake" testable through the public API.

Implementer note: `find(...) -> pin_view` is provided as a convenience for callers outside the handshake hot path (diagnostics, tests, control-plane queries) — it captures-and-scans in one call and returns a `pin_view` that holds the snapshot's `shared_ptr` for the caller's window. Both paths are correct under concurrent rotation; the contract is which one 2h's TLS handshake wiring uses.

#### §6.5.2 Why `std::shared_mutex` and not `fixpp::sync::async_mutex` — consolidated rationale (v0.2 N-P1-3 close)

v0.2 collapses the v0.1 three-rationales-in-three-sites (§3.5 / §3.9 / §6.5) into ONE consolidated rationale here. §3.5 and §3.9 carry only inheritance pins pointing at this subsection. The consolidated reasoning, in the only order that holds:

1. **The TYPE used is `std::shared_mutex`, NOT `std::mutex`.** `[const §XV.9]` line 215 binds `std::mutex in coroutine context`; `[const §XI.3]` line 146 binds plain `std::mutex` in any header that includes `asio::awaitable<...>`. The `Pinset` header declares `mutable std::shared_mutex writer_;` (§4.3). Neither rule binds against `std::shared_mutex`. The `[2f §6.6]` enforcement-of-`[const §XV.9]` grep gate scans for `<mutex>` (or `std::mutex` declaration) — it does not fire on `<shared_mutex>` / `std::shared_mutex`.
2. **The USE is fully synchronous.** `Pinset::add(...)` / `Pinset::remove(...)` return `expected_t<void>` directly — they are NOT awaitables. There is no coroutine context across the writer lock by API construction. Whether or not the `Pinset` header includes `<asio/awaitable.hpp>` is irrelevant to this rationale (and v0.1's "the header does not include awaitable" defence was a circular argument — the doc could include it for an unrelated reason and the rationale would still hold; the binding fact is the use shape, not the include closure).
3. **The READER path is lock-free.** `Pinset::find` / `Pinset::snapshot` do an `acquire`-load on `std::atomic<std::shared_ptr<const pin_snapshot>>` and never touch `writer_`. Readers do not contend with writers; the choice of writer mutex only affects writer-vs-writer contention, which is operationally vanishing (one rotation per cert-renewal cycle ≈ 30–90 days; §1.1).

Together: the `std::shared_mutex` choice satisfies `[const §XV.9]` (the rule binds `std::mutex`, not `std::shared_mutex`), satisfies `[2f §6.6]`'s grep gate by construction (no `<mutex>` include, no `std::mutex` use), and is the simplest correct primitive for the writer-only synchronisation actually needed. Using `fixpp::sync::async_mutex` would introduce an awaitable surface where the operation is synchronous, contradicting the simplicity-first design rule and adding cost to every writer call without measurable benefit.

This rationale is recorded explicitly here (and only here, per N-P1-3) to satisfy the brief's hard rule "do not silently overlap with `[const §XV.9]`; if you need synchronisation on Pinset, use `fixpp::sync::async_mutex` or document a rationale for a non-coroutine path if the rotation surface is fully synchronous."

### §6.6 Errors introduced by this design

Per the per-doc-prefix discipline established by `[2b §6.4]` (`FIXPP_ERR_WIRE_*`), `[2c §6.4]` (`FIXPP_ERR_DICT_*`), `[2d §6.7]` (`FIXPP_ERR_THREAD_*`), `[2e §6.4]` (`FIXPP_ERR_STORE_*`), `[2f §6.5]` (`FIXPP_ERR_SYNC_*`): 2g adopts the prefix **`FIXPP_ERR_TLS_*`** for its C-ABI mapping target, owned by 2i. (v0.2 / N-P1-2 close: the v0.1 `2f §6.7` × 3 + `2e §3.1` cites pointed at non-existent / unrelated sections; v0.2 cites the actual per-doc errors sections. Historical references to those broken cites are intentionally written as plain backticked strings — not bracketed cite tokens — to keep automated cite-checkers from re-flagging them.)

Numeric range allocation. Per the per-doc-prefix convention each doc owns a non-overlapping numeric block in the engine `error` enum's variant ordering (the precise integer ranges are 2i's call; the doc-level constraint is that 2g's variants do not collide with 2b/2c/2d/2e/2f's). 2g claims a contiguous block of **15 variants** under the `tls_*` prefix; 2i is asked to assign the block adjacent to 2f's `sync_*` block.

| `fixpp::core::error` variant | Source section | Remediation class |
|---|---|---|
| `tls_cert_load_failed` | §4.2 — `file_cert_source` could not read a cert file at construction (also: a `make_file_cert_source` factory call returning the same condition through `expected_t<...>`). Also fired by §4.2 when a *local* cert exceeds `Config::max_rsa_key_bits` / `max_cert_der_bytes` / `max_san_entries` at load time. | Configuration error — fix the path, file contents, or DoS-cap. |
| `tls_cert_parse_failed` | §4.2 / §4.5 — PEM/DER parse failed (malformed envelope, unexpected ASN.1 shape, unsupported encoding). | Configuration error — re-export the cert in a supported encoding. |
| `tls_pin_not_found` | §4.3 — `Pinset::remove(fp)` called with a fingerprint not in the set. | Operator error — operator may treat as benign (idempotent remove) or as a config-drift signal. |
| `tls_pin_already_present` | §4.3 — `Pinset::add(p)` called with a pin already in the set (by SHA-256). | Operator error — typically benign (idempotent add); operator may treat as a config-drift signal. |
| `tls_pinset_capacity_exhausted` | §4.3 / §1.1 — `Pinset::add` with `size() == max_pins`. | Configuration error — remove an old pin first or raise `Pinset::Config::max_pins`. |
| `tls_pinset_alloc_failed` | §4.3 — PMR allocation throw on snapshot clone routed through `[2a §4.2]` `trap_throw`. | Configuration / arena bug; raise arena cap. |
| `tls_cipher_not_allowed` | §4.4 / §4.5 / §6.1 — runtime configuration attempted to enable a cipher not on the four `CipherPolicy` allow-lists. The compile-time `static_assert` chain catches it at build; this variant covers the C-ABI / config-file path where a string crosses into the engine and is validated through `CipherPolicy::is_allowed(...)` (§4.4). | Configuration error — replace with an allow-list cipher. |
| `tls_handshake_failed` | §4.5 — `verify_peer` rejected the peer cert (chain unverifiable, `notAfter` past `cfg.clock->now()`, `notBefore` future, RSA key strength below T-039 minimum, signature algorithm not in `sig_algs`). The diagnostic field carries the specific sub-reason ("`expired`", "`not_yet_valid`", "`rsa_under_min`", "`sigalg_disallowed`", etc.). | Configuration / counterparty error — operator inspects the rejected cert. |
| `tls_rsa_key_too_large` | §4.5 / §1.1 — peer presented an RSA key with bits > `Config::max_rsa_key_bits` (default 8192). DoS bound (N-P1-4). Surfaces through `tls_handshake_failed` group with sub-reason `"rsa_over_max"`. | Counterparty / attack — operator may raise the cap explicitly if a counterparty actually uses keys this large. |
| `tls_cert_der_too_large` | §4.5 / §1.1 — peer cert ASN.1 envelope exceeds `Config::max_cert_der_bytes` (default 16 KiB). DoS bound (N-P1-4). | Counterparty / attack — refused before parse. |
| `tls_san_entries_exceeded` | §4.5 / §1.1 — peer cert SAN list cardinality exceeds `Config::max_san_entries` (default 64). DoS bound (N-P1-4). | Counterparty / attack — refused before SAN extraction. |
| `tls_pin_mismatch` | §4.5 — under `SecurityProfile::mtls_pinned`, the verified peer cert's SHA-256 is not in the captured handshake-time `Pinset` snapshot. | Counterparty rotation drift, or attack — operator decides between rotation-add and refusal. |
| `tls_sign_callback_unavailable` | §4.1 — an impl returned `local_credentials` whose `signer` variant carried a software_key_ref with a null handle, AND the OpenSSL wiring fell back to the awaitable signer path which was also empty. (Vanishing with v0.2's variant-typed signer; preserved for the C-ABI path where the discriminator may carry an unset value.) | Configuration error — switch to a `cert_source` impl that supplies the key, or wire an HSM. |
| `tls_invalid_security_profile` | §4.5 — `SecurityProfile::unset` reached `make_ssl_ctx_config` (caller did not pick a profile per [const §XII.5]); also `mtls_pinned` with a null `Pinset`; also `one_way_ca` with a non-null `Pinset`; also any profile with a null `clock`. | Configuration error. |
| `tls_load_cancelled` | §4.1 / §6.4 — `cert_source::load_credentials`'s awaitable was cancelled (or `async_signer_ref::sign` was). Joins `[2d §6.5] cancellable_dispatch → dispatch_aborted`, `[2d §4.1.1] Clock::sleep_until → clock_sleeps_cancelled`, `[2e §6.7] store_cancelled` (the per-doc errors section where the variant is defined; the writer-mutex contract precedent itself is `[2e §6.4]`), `[2f §6.5] sync_lock_aborted` in the cancellation group. | Cancellation; not an error in most contexts — caller decides. |

(15 variants.)

C-ABI mapping (delegated to **2i**) per the per-doc-prefix discipline:

- configuration errors (`tls_cert_load_failed`, `tls_cert_parse_failed`, `tls_cipher_not_allowed`, `tls_invalid_security_profile`, `tls_sign_callback_unavailable`) → **`FIXPP_ERR_TLS_CONFIG`**;
- handshake / verification (`tls_handshake_failed`, `tls_pin_mismatch`, `tls_rsa_key_too_large`, `tls_cert_der_too_large`, `tls_san_entries_exceeded`) → **`FIXPP_ERR_TLS_HANDSHAKE`**;
- pinset state (`tls_pin_not_found`, `tls_pin_already_present`, `tls_pinset_capacity_exhausted`) → **`FIXPP_ERR_TLS_PINSET`**;
- runtime allocation (`tls_pinset_alloc_failed`) → **`FIXPP_ERR_TLS_RUNTIME`**;
- cancellation (`tls_load_cancelled`) reuses the existing **`FIXPP_ERR_CANCELLED`** per `[const §XI.2]`.

Final coalescing is 2i's call.

---

## §7 Integration with adjacent modules

### §7.1 Transport (2h) — TLS-aware sub-interface; T-039 / T-040 partition

2g publishes the policy core (`cert_source` interface, `Pinset`, `CipherPolicy`, `SecurityProfile`, `verify_peer` predicate, `peer_identity` value); 2h owns the OpenSSL `SSL_CTX` construction + `async_handshake` coroutine + `SSL_VERIFY_PEER` callback wiring. The hand-off shape is `SslCtxConfig` (§4.5) — a value-typed descriptor 2h consumes at session open.

**T-039 partition.** Certificate parameters per [FIXS §3.4] (RSA ≥ 2048, ECDSA P-256/P-384, X.509 v2/v3, expiration validation at handshake): the **policy** lives in 2g's `verify_peer` (§4.5). The **wiring** (the OpenSSL `SSL_CTX_set_verify` callback that calls `verify_peer`) lives in 2h. Both halves are needed for the row to be discharged; per the brief's "T-039 cross-cuts to consider with 2h-transport" instruction, this doc declares the partition explicitly here. Appendix A row T-039 records the partition.

**T-040 partition.** Secrets distribution per [FIXS §4.1] (operators distribute via approved channels — HTTPS, GnuPG, PKCS#12, postal, in-person — and the engine consumes whatever the operator distributes via the `cert_source` interface): the **interface** lives in 2g (`cert_source::load_credentials` returns `local_credentials` whose `signer` is `software_key_ref` for in-process keys or `async_signer_ref` for HSM-backed flows). 2h does not touch T-040 — once the credentials are loaded by 2g, 2h consumes them through 2g's `cert_source` only. The row is fully owned by 2g; the cross-cut into 2h is informational only (2h depends on 2g for the loaded chain + signer). Appendix A row T-040 records this.

### §7.2 Session (Phase-4 spec) — CompID-to-TLS-identity binding (T-041) + `SecurityProfile::unset` open-time rejection

Per [FIXS §4.4], the authenticated TLS identity (the verified peer cert's subject DN + SANs) must map to the authorized FIX `SenderCompID` / `TargetCompID`. 2g supplies the **value** (`peer_identity` per §4.5) and the verification predicate; the **binding** (the policy table that maps `peer_identity` → expected CompID, the rejection on mismatch with `error::session_identity_mismatch`) is owned by the session-module Phase-4 spec.

Appendix A row T-041 records the cross-cut. The partition is: 2g owns the parsed-cert value type and the verifier; the Phase-4 session-module spec owns the binding policy.

**`SecurityProfile::unset` open-time rejection hand-off (v0.3 / Codex round-2 P3-1 close).** Per `[2d §4.5]` (line 538-540), `SessionConfig::security_profile` default-constructs to `SecurityProfile::unset`, and `Session::open` rejects the sentinel with the *outer* `error::invalid_session_config` per `[const §XII.5]`'s no-implicit-default rule; 2d records only the rejection invariant and explicitly defers the sentinel value to 2g (`[2d §4.5]` line 608: "The exact sentinel value (e.g., `unset`) is owned by 2g; 2d records only the rejection invariant"). 2g now closes the partition: `Session::open` MUST reject `SecurityProfile::unset` **before** invoking 2h's TLS construction (i.e., before any `make_ssl_ctx_config(...)` call). The hand-off is two-layer — the *outer* error at `Session::open` is `error::invalid_session_config` per `[2d §4.5]` / `[2d §6.7]`; the *inner* error at the `make_ssl_ctx_config(...)` boundary is `error::tls_invalid_security_profile` per §6.6 (covers any caller that bypasses `Session::open` and reaches `make_ssl_ctx_config` directly, e.g., 2i's C-ABI path, future hot-reload). The Phase-4 session-module spec implements the outer rejection at session-open time; 2g owns the inner rejection at the TLS construction boundary; the two errors do not double-fire on the standard `Session::open` path.

### §7.2.1 Engine-side trust-anchor null check on `mtls_pinned`

Recorded for completeness: under `SecurityProfile::mtls_pinned` per §4.5.1, an empty / null trust-anchor span returned by `cert_source::load_trust_anchors()` is **permitted** (FIXS RC1 strict pinning may use self-signed leafs without CA validation); under `SecurityProfile::mtls_ca` and `one_way_ca`, an empty / null trust-anchor span is **rejected** at `make_ssl_ctx_config(...)` with `tls_invalid_security_profile`. The §4.5.1 normative table is the binding source.

### §7.3 core/Clock (2d) — for cert-expiry checks

`verify_peer` (§4.5) consults `cfg.clock->now()` to evaluate `notBefore ≤ now ≤ notAfter` per T-039. v0.2 / RC#2 close: `cfg.clock` is the SESSION-scoped `effective_clock` per `[2d §7.9]` (resolved as `SessionConfig::clock_override ?: EngineConfig::clock`); 2h passes `effective_clock` into `make_ssl_ctx_config` at session open. The v0.1 prose conflated `EngineConfig::clock` with `effective_clock`; v0.2 routes through `effective_clock` by API construction so handshake validation is deterministic per session even when a session overrides the clock (e.g., test fixtures). The clock interface comes from `[2d §4.1]`; 2g consumes it through the `[2d §7.9]` resolution rule.

### §7.4 sync/async_mutex (2f) — NOT consumed by Pinset rotation

Per §6.5.2 (the consolidated rationale; v0.3 / N-P1-2 close — the v0.2 carry-over of the retired "header doesn't include awaitable" defence is removed here): `Pinset` rotation uses `std::shared_mutex` for the writer-vs-writer surface — the **type is `std::shared_mutex`** (not `std::mutex`), the **use is fully synchronous** (`add` / `remove` return `expected_t<void>` directly), the **reader path is lock-free**; `[const §XV.9]` and `[2f §6.6]`'s grep gate bind `std::mutex`, not `std::shared_mutex`. 2g records the dependency on 2f's published surface for completeness — any future 2g surface that *does* require a coroutine-suspending lock would consume `fixpp::sync::async_mutex` per `[2f §4.1.1]` — but v1.0 has no such surface. **2f is not a hard hand-off gate for 2g implementation.**

### §7.5 store/MessageStore (2e) — N/A

`tls/` does not touch the store. The `cert_source` does not persist anything (load-once at construction); `Pinset` rotation does not write to durable storage (the `EngineConfig`-level operator workflow re-builds `file_cert_source` from disk on engine restart, which loads the post-rotation chain).

### §7.6 capi (2i) — C ABI shape delegation

Per §5: 2g defines the C++ shapes; 2i defines the C symbols. The opaque-handle shapes (`fixpp_cert_source_t`, `fixpp_pinset_t`), the enum mirror (`fixpp_security_profile_t`), and the `FIXPP_ERR_TLS_*` coalescing groups (§6.7) are 2g's hand-off to 2i.

### §7.7 service (2j) — control-plane interface for runtime cert/pinset reload

The control-plane gRPC schema (`service/proto/fixpp_control.proto` per [arch §8.1]) carries an `RotatePinset` RPC and a `ReloadCertSource` RPC consumed by 2j's `ControlPlane` interface. The **handler** lives in 2j; the **action** is `Pinset::add` / `Pinset::remove` / `cert_source` swap (the latter via session close-and-reopen per [arch §5.6] — the cert_source itself is frozen at session open, only the pinset is mid-session-mutable). Appendix A claims neither row from 2j; this is a forward-compat hook only.

### §7.8 log + otel (2k) — TLS-event log records / OTel spans

Every TLS event of operational interest (handshake start, handshake success, handshake failure with reason, pinset add, pinset remove, pinset rotation observed at the next handshake) emits a structured-log record through the `Logger` interface per [arch §5.7] / [arch §4.7]. 2k owns the schema; 2g owns the call sites.

OTel span shape: one `fixpp.session.tls_handshake` span per handshake; one `fixpp.tls.pinset.rotation` span per `add` or `remove`. Span attributes include the cert SHA-256 (handshake), the cert subject (handshake; via `peer_identity`), and the pin SHA-256 (rotation). 2k's span schema is owned by 2k; 2g records the call sites.

---

## §8 PMR — recap

Storage classes for 2g-owned data:

| Storage | Lifetime | Holds | Reset by |
|---|---|---|---|
| `file_cert_source::cfg_.mr` (or engine default) | `cert_source` instance lifetime | parsed `Certificate` storage; raw DER byte storage; OpenSSL `X509*` RAII handles; `std::pmr::vector<Certificate>` chain + trust anchors | `~file_cert_source` |
| `Pinset::cfg_.mr` (or engine default) | **MUST outlive every `shared_ptr<const pin_snapshot>` the `Pinset` ever publishes** — i.e., the union of `Pinset` instance lifetime AND every reader-held snapshot lifetime, NOT merely `Pinset` lifetime (v0.4 / round-3 P1-1 close — engine-default `mr` satisfies this by construction per `[2d §4.4]`; user-supplied `mr` is a documented caller contract; see §4.3 `Config` comment + §4.6 ownership-rules bullet + §6.2 publication paragraph) | the immutable snapshot vectors AND the `pmr::string` / `pmr::vector<pmr::string>` allocations inside each `pin`'s owning members; each `add` / `remove` allocates a new vector and discards the old one (the old is freed when no reader holds it; the resource MUST still be valid at that drain point) | `Pinset` destruction PLUS reader-held snapshots draining (whichever is later) |

**Lifetime classes for non-arena objects** (v0.3 / N-P1-1 close — bullets 2 and 4 rewritten to mirror RC#1 / RC#3 closures in §4.5 / §4.6 / Appendix D; the v0.2 carry-over of the v0.1 reachability / cross-doc-lifetime claims is removed; v0.4 / round-3 P1-1 close — bullet 5 (`Pinset` snapshot) carries the explicit MR-lifetime requirement so the §4.3 `Config::mr` contract and the §6.2 publication-primitive contract close into one consistent statement):

- **`cert_source` instance** — engine lifetime (held via `std::shared_ptr` in `EngineConfig::default_cert_source` and `SessionConfig::cert_source`). Engine teardown clears the shared_ptr after every session is drained.
- **`Pinset` instance** — reached through `SessionConfig::pinset` per `[arch §5.6]` mid-session-mutable carve-out (Appendix D §D.1 / §D.2; RC#3 close). The abstract `cert_source` does NOT publish a `pinset()` accessor, and 2h does NOT downcast `cert_source*` to a concrete impl — the v0.1 "owned by the `cert_source` impl that exposes a `pinset()` accessor" reachability shape is retired. Lifetime is controlled by the holding `shared_ptr` (engine, session, or control-plane caller may hold).
- **`Certificate` view** — bounded by the `cert_source` instance that minted it.
- **`peer_identity` value** — OWNING per RC#1 / N-P2-2 close (§4.5 / §4.6). The SAN strings + subject DN are PMR-copied at `verify_peer` time into `peer_identity`'s `pmr::string` / `pmr::vector<pmr::string>` members; the view accessors (`subject_dn_view`, `san_dns_names`, `san_uris`) carry `[[clang::lifetimebound]]` bound to `*this`. 2h's `SSL*` lifetime is independent — `peer_identity` survives `SSL_free`, and the v0.1 fragile cross-doc lifetime contract ("bounded by 2h's `SSL*` object for the lifetime of the verified handshake") is retired. Phase-4 session-module captures `peer_identity` by value across the T-041 binding without dangling.
- **Pinset snapshot (`shared_ptr<const std::pmr::vector<pin>>`)** — the snapshot the reader holds is alive for the duration of the `shared_ptr`'s holding; replaced (release) on every `add` / `remove`; the old snapshot is freed when no reader holds it (eventual; bounded by the rotation rate, which is operationally < 1/30-days). The PMR resource backing each `pin`'s owning members (`Pinset::cfg_.mr`) MUST itself remain valid past the snapshot's destruction — the resource owner's contract per §4.3 `Config::mr` (v0.4 / round-3 P1-1 close); the engine-default path satisfies this by construction, user-supplied `mr` does so by caller contract.

Per [const §VIII.5]: zero `new`/`delete` between parse and `fromApp`; this extends to the handshake-time `Pinset::find` per §6.1. Cert *load* allocations are PMR-arena (cold path) or rare global heap (when PMR is null); the `tools/check_alloc.py` allocation guard (§9) verifies under `mallocnesia` on Linux/Clang Tier 1 that the handshake-hot path does not touch the global heap.

---

## §9 Test seams

Per `[arch §10]` requirement (4) and `[const §VII.4]`. v0.2 ships **17 seams** (v0.1 had 13; v0.2 adds 4 to cover RC#1/RC#2 fixes and split the rotation-ordering seam into deterministic + stress); v0.4 / round-3 P1-1 close adds **seam #18** (post-`~Pinset()` snapshot lifetime under the §4.3 `Config::mr` contract) for a total of **18 seams**; each seam is referenced by **name** (per the `[2d §9]` cross-referencing precedent — ordinals are not stable across review rounds; names are).

1. **`Pinset` add-then-remove ordering — deterministic test (FIXS RC1 §5 enforcement, hook-driven).** Codex P2-4 close: a deterministic test hook on the `Pinset::add` path pauses execution after the add's release-store and before any `remove`; the test thread B then acquire-loads the published snapshot via `Pinset::snapshot()` and asserts presence of `p1`; the harness then releases the pause, runs `remove(p1.sha256)`, and B asserts absence. The deterministic version cannot pass under a single-CAS publication (a bad bulk-swap implementation), and it cannot fail under a correct add-then-remove (no scheduler-flakiness). Lives in `tests/tls/test_pinset_add_then_remove_deterministic.cpp`.

2. **`Pinset` add-then-remove ordering — stress test (TSan).** Two threads: thread A repeatedly issues `add(p1)` then `remove(p1.sha256)`; thread B reads `find(p1.sha256)` in a tight loop and records the pattern of `pin_view::found()` observations. The test enforces under TSan that no use-after-free or data race surfaces during concurrent rotation; it does not by itself force the deterministic-ordering invariant (that is seam #1's job). Run under TSan + ASan. Lives in `tests/tls/test_pinset_add_then_remove_stress.cpp`.

3. **Conformance corpus seam — the FIXS rotation scenario.** Drive a recorded FIXS rotation scenario (counterparty rotates leaf cert; engine-side operator pre-emptively `add`s the new pin, observes the new cert on next handshake, then `remove`s the old pin) end-to-end against a `MockTransport` that replays canned handshake bytes for both old and new certs. Verify: (a) handshake succeeds against new cert after `add`; (b) handshake against old cert continues to succeed until `remove`; (c) handshake against old cert fails with `tls_pin_mismatch` after `remove`. Lives in `tests/conformance/test_fixs_rotation.cpp`.

4. **Latency regression — `Pinset::snapshot_acquire` (capture once).** Google Benchmark on `Pinset::snapshot()` (one acquire-load + refcount RMW). v0.3 / N-P2-2 close: the bench records the platform tuple (`compiler / stdlib / stdlib-version / cpu-supports-cmpxchg16b`) and CI selects the applicable ceiling — ≤ 30 ns p99 on the lock-free floor (libstdc++ ≥ 16 / libc++ on x86_64 with `cmpxchg16b`) or ≤ 100 ns p99 on the libstdc++ ≤ 15 internal-mutex fallback path. CI fails on > 5 % regression against whichever ceiling matches the runner. Lives in `bench/tls/bench_pinset_snapshot_acquire.cpp`.

5. **Latency regression — `Pinset::find` (snapshot_acquire + linear_scan_16).** Google Benchmark on `Pinset::find(sha256)` at `max_pins = 16`. v0.3 / N-P2-2 close: ≤ 130 ns p99 on the lock-free floor or ≤ 200 ns p99 on the libstdc++ ≤ 15 fallback path (CI selects per platform tuple, mirroring seam #4). Variant at `max_pins = 1` ≤ 50 ns / ≤ 120 ns. CI fails on > 5 % regression. Lives in `bench/tls/bench_pinset_find.cpp`.

6. **Cert-parsing fuzzer.** libFuzzer-driven random PEM/DER inputs feeding `file_cert_source` construction; ASan + UBSan invariants; verify no crash, no UAF, no UB on adversarial inputs. Required by `[const §IX.4]`-extended (`tls/` is a parser-touching surface). Lives in `tests/fuzz/fuzz_file_cert_source.cpp`.

7. **Allocation guard on the handshake-hot path.** `tools/check_alloc.py` + `mallocnesia` (Linux/Clang Tier 1 per the `[2a §9]` / `[2b §9]` / `[2d §9]` precedent). 10⁴-handshake test; zero global-heap `new`/`delete`/`malloc` on the `Pinset::snapshot` + `Pinset::find` + `verify_peer` chain when `SslCtxConfig::mr` is non-null; PMR-arena allocations are expected. Lives in `tests/perf/test_tls_handshake_alloc_guard.cpp`.

8. **HSM-style `async_signer_ref` mock.** Implement a `mock_hsm_cert_source` whose `load_credentials()` returns `local_credentials` with `signer = async_signer_ref{...}`; verify (a) the OpenSSL handshake routes signing through the awaitable `async_signer_ref::sign(...)` path (not the software-key fast path); (b) the awaitable suspends OFF the session strand by using `cancellable_dispatch` per the §6.4 recipe (asserted via a strand-id comparator the test injects); (c) the engine never sees a raw private-key handle (a `static_assert` on `decltype(cert_source::load_credentials)` confirms `local_credentials::signer` is `variant<software_key_ref, async_signer_ref>` only). Lives in `tests/tls/test_hsm_async_signer_mock.cpp`.

9. **Pinset add/remove ordering (single-thread).** Verify `add(p1) → add(p2) → remove(p1.sha256)` produces a snapshot containing only `p2`; verify `add(p1) → remove(p1.sha256) → add(p1)` succeeds and observes p1 in the final snapshot; verify `remove(unknown)` returns `tls_pin_not_found` and leaves the snapshot unchanged; verify `add(p1) → add(p1)` returns `tls_pin_already_present` on the second call; verify `pin_view::found()` is true exactly when the SHA-256 was last `add`-ed-without-remove. Lives in `tests/tls/test_pinset_single_thread_ordering.cpp`.

10. **Cipher allow-list rejection — value-based consteval `try_compile`.** Codex P1-7 close: write a CMake `try_compile` negative test that attempts to declare a `CipherPolicy` variant that includes `"ECDHE-RSA-AES128-CBC-SHA256"` (CBC is banned per [const §XII.3]); verify the build fails with the expected `static_assert` message. Companion positive `try_compile` test confirms that the published `tls13_suites` / `tls12_suites` / `kx_groups` / `sig_algs` arrays compile clean. The v0.2 consteval is value-based (P1-7 fix), so this test is the on-ramp that proves the implementation actually compiles. Lives in `tests/tls/test_cipher_allow_list_static_assert.cpp` (with companion `tests/tls/CMakeLists.txt` declaring both the negative- and positive-compile targets).

11. **`SecurityProfile`-to-OpenSSL-mode mapping (Codex P2-5 close).** ⚠️ *(2026-08-29: the value list below is the v0.2 set; feature 043 added a fourth — enumerate from the shipped header, not from this line.)* For each `SecurityProfile` value (`mtls_ca`, `mtls_pinned`, `one_way_ca`), invoke `make_ssl_ctx_config(...)` and verify (a) the resulting `SslCtxConfig::ciphers` contains exactly the `[const §XII.3]` lists; (b) the row in §4.5.1's normative table is honoured (OpenSSL `verify_mode` flags, local-cert-required, CA-anchors-required, pinset-required, pin-check-policy); (c) `SecurityProfile::unset` rejects with `tls_invalid_security_profile`; (d) `mtls_pinned` with null `Pinset` rejects; (e) `one_way_ca` with non-null `Pinset` rejects; (f) any profile with null `clock` rejects. Lives in `tests/tls/test_security_profile_mapping.cpp`.

12. **Error-variant exercise.** Drive every `error::tls_*` variant from §6.6 (15 variants) through a unit test that synthesises the failing input (malformed PEM, fingerprint not in pinset, fingerprint already present, capacity exhausted, RSA-key-too-large, cert-DER-too-large, SAN-entries-exceeded, etc.) and verifies the returned `expected_t::unexpected{...}` carries the expected variant. Lives in `tests/tls/test_tls_error_variants.cpp`.

13. **Cancellation contract on `cert_source::load_credentials`.** Issue `co_await cs->load_credentials()` inside a coroutine bound to a strand; fire the awaiter's cancellation slot before the awaitable completes; verify the awaitable returns `expected_t::unexpected{tls_load_cancelled}`. The §6.4 recipe is enforced — the test injects a probe at recipe step 4 (`cancellable_dispatch`) and asserts the dispatch is observed and reaped. Run under both `per_session_strand` and `direct_executor` modes per `[2d §4.8]`. Lives in `tests/tls/test_load_credentials_cancellation.cpp`.

14. **T-039 cert-parameter validation.** For each rejection criterion (RSA < 2048, RSA > 8192, ECDSA non-P-256/P-384, X.509 v1, expired cert relative to `cfg.clock`, not-yet-valid cert, SHA-1 signature, banned signature algorithm, cert-DER > 16 KiB, SAN entries > 64), feed a corresponding test cert and verify `verify_peer` returns `expected_t::unexpected{tls_handshake_failed}` (or the specific DoS variant `tls_rsa_key_too_large` / `tls_cert_der_too_large` / `tls_san_entries_exceeded`) with the appropriate sub-reason in the diagnostic field. Lives in `tests/tls/test_verify_peer_t039.cpp`.

15. **Mid-session rotation does not affect in-flight handshake — `snapshot()`-captured contract.** Open a session that begins a TLS handshake against `MockTransport` paused at the middle of `async_handshake`; verify (under instrumentation) that the handshake captured `Pinset::snapshot()` ONCE before the pause (per §6.5.1 binding contract). On a separate thread, `Pinset::add(p_new)` and `Pinset::remove(p_old)`; resume the handshake; verify the in-flight handshake observes the **pre-rotation** captured snapshot (the captured `shared_ptr` keeps the matched entry alive); verify the **next** handshake (e.g., on reconnect) observes the post-rotation snapshot. Enforces the `[2d §7.5]` strand-safety boundary contract AND the §6.5.1 `snapshot()`-captured-once contract. Lives in `tests/tls/test_pinset_rotation_does_not_affect_in_flight.cpp`.

16. **`pin_view` lifetime under concurrent rotation (RC#1 close).** Thread A holds a `pin_view` from `find(p1.sha256)`; thread B issues `add(p_other)` then `remove(p1.sha256)` repeatedly; verify under ASan + TSan that thread A's `pin_view::value` dereference remains valid for as long as A holds the view (the snapshot's `shared_ptr` keeps the matched entry alive). The v0.1 raw-pointer surface would fail this test; the v0.2 `pin_view` passes by construction. Lives in `tests/tls/test_pin_view_lifetime_under_rotation.cpp`.

17. **`make_file_cert_source` factory parity (RC#2 close).** Verify (a) `make_file_cert_source(cfg, mr)` returns `expected_t<shared_ptr<cert_source>>` with no exceptions thrown; (b) parse-failure surfaces as `expected_t::unexpected{tls_cert_parse_failed}`; (c) load-failure surfaces as `expected_t::unexpected{tls_cert_load_failed}`; (d) success returns a shared_ptr whose `load_credentials()` and `load_trust_anchors()` mirror the throwing-constructor `file_cert_source` directly. Also: a positive-compile test that 2i's C-ABI bridge stub calls `make_file_cert_source` (not the throwing constructor) successfully through the factory's `expected_t<...>` shape. Lives in `tests/tls/test_make_file_cert_source_factory.cpp`.

18. **Post-`~Pinset()` snapshot lifetime — PMR resource outlives both (v0.4 / round-3 P1-1 close).** Construct a `Pinset` against a user-supplied `std::pmr::memory_resource*` (a tracking `monotonic_buffer_resource` test fixture) whose lifetime is **scope-outer** to the `Pinset` instance. `add(...)` two pins, `snapshot()` once, then drop the last `shared_ptr<Pinset>` to invoke `~Pinset()`. With the `shared_ptr<const pin_snapshot>` still held by the test, exercise read-only access on the snapshot's `pin::subject_dn` / `san_dns` members (verifying the `pmr::string` / `pmr::vector<pmr::string>` storage is still valid). Then drop the last reader reference and verify the snapshot's `~vector<pin>` runs cleanly through the still-live MR (the §4.3 `Config::mr` contract + §6.2 publication-primitive contract: `mr` outlives every snapshot, NOT just the `Pinset`). Run under ASan + TSan; a regression to UAF (e.g., the alternative shape where `cfg_.mr` is bounded by `Pinset` and snapshots dangle the resource at destruction) MUST surface as an ASan use-after-free. Companion negative test: a deliberately-misconfigured fixture where the user-supplied MR's scope is INSIDE the `Pinset` scope (violating the contract) MUST surface ASan UAF — the test asserts the failure mode, documenting that the contract is operator-side and the engine-default path is contract-satisfied by construction. Lives in `tests/tls/test_pinset_snapshot_outlives_pinset.cpp`.

---

## §10 Open questions

| # | Question | Disposition | Owner |
|---|---|---|---|
| 1 | **HSM concrete vendor selection (PKCS#11, Cloud HSM, Azure Key Vault, AWS KMS, …).** v1.0 ships only the file-based default; a user that needs HSM implements `cert_source` against the vendor SDK directly. | DEFER to user-side or post-v1 bundled impls. | user-side (v1.0); post-v1 best-effort if a bundled `hsm_cert_source` is requested |
| 2 | **mTLS client-cert lifecycle on reconnect — does the engine re-load the chain on every reconnect, or once at session open?** Default: once at session open (the `cert_source` is frozen at session open per [arch §5.6]). Operators who need hot rotation use the close-and-reopen pattern. | DECIDED — once at session open; the `Pinset` covers the operationally common case (peer cert rotation, not local cert rotation). | 2g (closed) |
| 3 | **Cert-expiry alert mechanism — does the engine emit a metric / log / OTel event before `notAfter`?** Operationally desirable but not constitutional. | DEFER to **2k** observability — the engine periodically (e.g., daily on each session's `effective_clock`-driven schedule) emits a `fixpp.tls.cert.days_to_expiry` gauge. | 2k |
| 4 | **`file_cert_source` hot-reload via inotify / `ReadDirectoryChangesW`.** v1.0 is load-once; mid-session reload is via close-and-reopen. | DEFER to post-v1 (a separate `inotify_cert_source` plugin). | post-v1 follow-up |
| 5 | **mTLS client-cert lifecycle for outbound connections (initiator-side) vs acceptor-side.** v1.0 treats both symmetrically through `cert_source`; the `SecurityProfile` enum does not distinguish. The Phase-4 session-module spec may need to refine this if a venue requires asymmetric trust modes. | DEFER to Phase-4 session-module spec for any asymmetric-mode refinement; 2g's surface is symmetric. | Phase-4 session-module spec (if needed) |
| 6 | **`SslCtxConfig` — is the `mr` field consumed by 2h, or by 2g?** 2h owns the actual `SSL_CTX` construction; whether 2h respects `SslCtxConfig::mr` for any escape-of-OpenSSL-arena allocation is 2h's call. | DEFER to **2h**. | 2h |
| 7 | **OCSP stapling / CRL distribution — should 2g call into the operator's revocation infrastructure?** v1.0: no. FIXS deployments use pinning per [FIXS §2.3], not revocation. | DEFER to post-v1 if a venue requires it. | post-v1 follow-up |
| 8 | **Cross-doc amendment to architecture.md §5.6 + `[2d §4.5]` `SessionConfig` + `library/spec/coverage-index.md` to publish `SessionConfig::pinset` as the single live-`Pinset` reachability path (RC#3 close).** [arch §5.6] mentions "pinset rotation" as the carve-out but does not name the field; `[2d §4.5]` `SessionConfig` does not carry a `pinset` field; v0.1 split the path across three incompatible shapes (function parameter, concrete `file_cert_source::pinset()` accessor, deferred amendment). v0.2 picks **`SessionConfig::pinset`** as the only published reachability — `cert_source` stays credentials-only, no downcast required, every concrete impl shares the same path. | **DECIDED** — `SessionConfig::pinset` is the published shape; Appendix D §D.1 / §D.2 / §D.3 drop-ins materialise the cross-doc amendments for the orchestrator at sign-off. (RC#3 close.) | 2g (drop-ins written); orchestrator applies |

---

## §11 Hand-off

**Docs unblocked by 2g sign-off (downstream):**

- **2h (transport)** — needs the `cert_source` contract (§4.1), the `SslCtxConfig` shape (§4.5), the `verify_peer` predicate (§4.5), the T-039 / T-040 partition (§7.1). Without 2g, 2h cannot lock the `asio_tls_transport`'s `SSL_CTX` configuration shape.
- **Session-module Phase-4 spec** — needs the `SecurityProfile` enum (§4.5), the `peer_identity` value type (§4.5) for the T-041 CompID-to-TLS-identity binding, the `SessionConfig::cert_source` field's contract (already in [2d §4.5]; 2g operationalises the type).
- **2j (control plane)** — needs the `Pinset::add` / `Pinset::remove` thread-safety contract (§4.3 / §6.5) for the runtime-rotation gRPC handlers.
- **2k (log + otel)** — needs the TLS event call sites (§7.8) to define the structured-log / OTel span schemas.
- **2i (C ABI)** — needs the C++ shape inventory (§5) for `fixpp_cert_source_t`, `fixpp_pinset_t`, `fixpp_security_profile_t`, `FIXPP_ERR_TLS_*` coalescing groups.

**Cross-doc amendments declared at sign-off (orchestrator applies, per [2c App D] / [2d App D] / [2e App D] / [2f App D] precedent — the rewrite agent does NOT edit sibling docs in this draft):**

- **Appendix D §D.1** — `[arch §5.6]` "pinset rotation" carve-out: appends one sentence naming `SessionConfig::pinset` as the published reachability shape (RC#3 close).
- **Appendix D §D.2** — `[2d §4.5]` `SessionConfig` field-list: appends `std::shared_ptr<fixpp::tls::Pinset> pinset;` with the `[arch §5.6]` mid-session-mutable comment and the null-permitted-under-mtls_ca/one_way_ca constraint (RC#3 close).
- **Appendix D §D.3** — `library/spec/coverage-index.md` §"FIXS RC1": three in-place Gap-note-column updates on the live five-column table's §3.4 / §4.1 / §4.4 rows (changing `MISSING → row added (T-XXX)` to `covered by [2g §X.Y]`). v0.4 / round-3 P1-2 close — the v0.2 / v0.3 placeholder three-column drop-in is rewritten against the live five-column schema (`Section | Title | Normative? | Catalogue IDs | Gap note`); the §4.2 row is preserved byte-faithfully (already `covered by T-040`).

(2g does NOT edit `library/spec/feature-catalogue.md`, `library/spec/coverage-index.md`, `architecture.md`, or any signed-off sibling doc directly per the brief's hard rule 6/7; the drop-ins are recorded in Appendix D verbatim for the orchestrator.)

---

## Appendix A — Catalogue row coverage

Per `[const §VI.5]` exact-coverage rule and the per-doc precedent in `[2b Appendix A]`, `[2c Appendix A]`, `[2d Appendix A]`, `[2e Appendix A]`, `[2f Appendix A]`.

### A.1 Owned (sole)

| Row | Family | Catalogue text (verbatim from `library/spec/feature-catalogue.md`) | What 2g covers | 2g §/§§ |
|---|---|---|---|---|
| **T-006** | OFFICIAL — transport | "FIXS: TLS 1.2 support — ECDHE + AES-GCM cipher suites, forward secrecy" — `[FIXS §3.1]` Protocol version | The TLS 1.2 cipher allow-list (`CipherPolicy::tls12_suites`) and the `SecurityProfile`-to-`SSL_CTX` adapter that configures OpenSSL with TLS 1.2 + the allow-list. The actual `SSL_CTX_set_min_proto_version(SSL_CTX*, TLS1_2_VERSION)` call is owned by 2h; 2g supplies the policy. | §4.4, §4.5, §6.1 |
| **T-007** | OFFICIAL — transport | "FIXS: TLS 1.3 support — preferred; session caching optional" — `[FIXS §3.1]` Protocol version | The TLS 1.3 cipher allow-list (`CipherPolicy::tls13_suites`) and the adapter that configures TLS 1.3 as preferred. Session caching is post-v1 per [const §XII.3] / [FIXS §3.2] (we disable session caching to align with FIXS §3.2's "session caching disabled" stance for high-assurance deployments — the configuration is in the adapter at §4.5). | §4.4, §4.5, §6.1 |
| **T-008** | OFFICIAL — transport | "FIXS: Mutual TLS — leaf certificate pinning on both ends" — `[FIXS §2.2]` Mutual and Simple TLS protocol options | The `SecurityProfile::mtls_pinned` mode (§4.5), the `Pinset` value-typed class (§4.3), the leaf-cert SHA-256 fingerprint matching (§4.3 / §4.5 `verify_peer`). | §4.3, §4.5 |
| **T-011** | OFFICIAL — transport | "FIXS: Certificate pinset API — multiple valid peer certs per counterparty for rotation/DR" — `[FIXS §2.3]` Leaf Certificate Pinning | The `Pinset` API with add-then-remove rotation per [SYN §3.4 Q15] / [FIXS RC1 §5] (§4.3, §6.5); the §1.1 cap (`max_pins = 16`); the lock-free reader / serialised-writer publication primitive (§6.2). | §4.3, §6.2, §6.5 |
| **T-013** | OFFICIAL — transport | "FIXS: Cipher suite enforcement — disable RC4, DES/3DES, anonymous key exchange, MD5" — `[FIXS §3.3]` Cipher suites | The `CipherPolicy` compile-time allow-list (§4.4) with the banned-token `static_assert` chain (§6.1). The per-[const §XII.4] / [const §XV.11] banned set is enforced at build time; the `tls_cipher_not_allowed` runtime variant covers the C-ABI / config-file path (§6.7). | §4.4, §6.1, §6.7 |

### A.2 Cross-cuts (partitioned with 2h or session-module)

| Row | Family | Catalogue text (verbatim) | Partition declared in this doc | Side owned by 2g | Side owned elsewhere |
|---|---|---|---|---|---|
| **T-039** | OFFICIAL — transport | "FIXS: Certificate parameters — RSA 2048-bit min, ECDSA 256-bit, X.509 v2/v3, expiration validation at handshake" — `[FIXS §3.4]` Certificate parameters | §7.1 | The `verify_peer` predicate (§4.5) that enforces RSA between 2048 and `Config::max_rsa_key_bits` (default 8192; DoS upper bound), ECDSA P-256/P-384, X.509 v2/v3 (v1 rejected), expiration against `cfg.clock->now()` (= `effective_clock` per `[2d §7.9]`), plus DoS bounds (cert DER ≤ 16 KiB, SAN cardinality ≤ 64). | The OpenSSL `SSL_CTX_set_verify` callback wiring that calls `verify_peer` — owned by **2h**. |
| **T-040** | OFFICIAL — transport | "FIXS: Secrets management — distribute private keys/PSKs/pinned-certs via approved channels (HTTPS, GnuPG, PKCS#12, postal, in-person); store securely; support rotation" — `[FIXS §4.1]` Sharing secrets | §7.1 | The `cert_source` interface (§4.1) that consumes whatever secret-distribution channel the operator uses; the `file_cert_source` default (§4.2) for the file-on-disk channel; the `Pinset` rotation API (§4.3) for the rotation half. | (none — 2h does not touch T-040; the row is fully owned by 2g). |
| **T-041** | OFFICIAL — transport | "FIXS: Authorization linked to authentication — authenticated TLS identity must map to authorized FIX CompID; per-counterparty TLS tunnel" — `[FIXS §4.4]` Authorization linked to authentication | §7.2 | The `peer_identity` value type (§4.5) — parsed subject DN + SANs from the verified peer cert; the value the session FSM consumes for the binding. | The CompID-to-TLS-identity binding policy and the `error::session_identity_mismatch` rejection — owned by the **session-module Phase-4 spec**. |

---

## Appendix B — Normative References

Per `[const §VI.5]` exact-coverage rule. Format `[DocAbbrev §X.Y.Z] Section title` per `[const §VI.2]`, drawn from `library/spec/coverage-index.md`.

### B.1 Coverage-index normative references (consumed by 2g)

| Spec area | Normative reference | 2g impact |
|---|---|---|
| TLS profile — protocol version | `[FIXS §3.1] Protocol version` | §4.4 (TLS 1.2 / 1.3 allow-lists), §4.5 (adapter configures min version); T-006, T-007 |
| TLS profile — protocol features (compression, renegotiation, session caching) | `[FIXS §3.2] Protocol features` | §4.5 (adapter disables compression, renegotiation, session caching per FIXS §3.2 stance) |
| TLS profile — cipher suites | `[FIXS §3.3] Cipher suites` | §4.4 (`CipherPolicy`); §6.1 (`static_assert` chain); T-013 |
| Certificate parameters | `[FIXS §3.4] Certificate parameters` | §4.5 (`verify_peer` T-039 enforcement); T-039 (cross-cut with 2h per §7.1) |
| Mutual TLS | `[FIXS §2.2] Mutual and Simple TLS protocol options` | §4.5 (`SecurityProfile::mtls_*`); T-008 |
| Leaf certificate pinning | `[FIXS §2.3] Leaf Certificate Pinning` | §4.3 (`Pinset`); §4.5 (`mtls_pinned`); T-008, T-011 |
| Sharing secrets | `[FIXS §4.1] Sharing secrets` | §4.1 (`cert_source` interface as the consumption surface for whatever secret-distribution channel the operator uses); T-040 |
| Storing secrets | `[FIXS §4.2] Storing secrets` | §4.2 (`file_cert_source` is the default storage backend; storage-security is the OS's responsibility per §2 non-goal); T-040 |
| Renewing secrets | `[FIXS §4.3] Renewing secrets` | §4.3 (`Pinset::add` / `Pinset::remove`); §6.5 (rotation invariants); T-011 |
| Authorization linked to authentication | `[FIXS §4.4] Authorization linked to authentication` | §4.5 (`peer_identity`); §7.2 (cross-cut with session-module Phase-4); T-041 |
| Pinning & rotation (project-rolled-up cite) | `[FIXS RC1 §5] Certificate pinning and rotation` (per [arch Appendix B]; the underlying spec sections are §2.3 + §4.3) | §4.3 (`Pinset` add-then-remove); §6.5 (no atomic-swap); T-011 |

### B.2 Constitutional clauses cited inline at point of use (exact, per `[const §VI.5]`)

`[const §VI.5]` (exact-citation rule);
`[const §VII.4]` (no untested code);
`[const §VIII.1]` (perf-sensitive modules need benchmarks);
`[const §VIII.2]` (perf regression budgets);
`[const §VIII.3]` (perf bench frameworks);
`[const §VIII.5]` (zero allocation between parse and `fromApp`, extended to handshake-hot path);
`[const §X.4]` (out-of-range C-ABI code mapping);
`[const §X.5]` (C-ABI thread-safety annotations);
`[const §XI.1]` (`asio::awaitable<T>` composition — `cert_source::load_credentials`);
`[const §XI.2]` (ASIO native cancellation slots);
`[const §XI.3]` (awaitable mutex required in coroutine context — not consumed by 2g rotation per §6.5.2);
`[const §XI.4]` (per-session strand default);
`[const §XII.1]` (OpenSSL on both Linux and Windows; Schannel dropped);
`[const §XII.2]` (TLS 1.2 / 1.3 only; banned at compile time);
`[const §XII.3]` (compile-time cipher allow-list);
`[const §XII.4]` (banned cryptography);
`[const §XII.5]` (`SecurityProfile` no-implicit-default rule + `[[deprecated]]` diagnostic on `one_way_ca`);
`[const §XII.6]` (pinset rotation v1.0 commitment);
`[const §XII.7]` (`EncryptMethod(98) ≠ 0` rejection — not implemented by 2g; recorded for completeness);
`[const §XII.8]` (pluggable `cert_source` interface);
`[const §XIII.3]` (strand-stored trace context);
`[const §XIV.1]` (v1.0 pluggable interfaces — cert_source);
`[const §XIV.2]` (≤5 pure-virtual on plugin interfaces — 2g uses 2 of 5);
`[const §XIV.4]` (no `dlopen` plugin loading);
`[const §XV.9]` (banned `std::mutex` in coroutine context — §6.5.2 rationale records non-applicability);
`[const §XV.10]` (banned application-layer encryption — recorded by 2g; rejection wiring is owned by the wire validator and the session FSM, not by 2g);
`[const §XV.11]` (banned cipher set — enforced at compile time per §6.1);
`[const §XVII.1]` (Codex Gate A required for design docs).

### B.3 Architectural sections cited (exact, per `[const §VI.5]`)

`[arch §1.2]` (non-goals — no dynamic plugin loading);
`[arch §2.3]` (allowed include edges — `tls/` may include from `core/` only);
`[arch §3]` (public namespaces — `fixpp::tls`);
`[arch §4.4]` (session module surface — recipient of `SecurityProfile`, `peer_identity`);
`[arch §4.5]` (transport module surface — recipient of `SslCtxConfig`);
`[arch §4.6]` (tls module surface — the spine of this doc);
`[arch §4.7]` (log + otel surface — recipient of TLS event call sites);
`[arch §4.10]` (capi surface delegation);
`[arch §5.1]` (executor model — handshake on session strand);
`[arch §5.3]` (error model — `expected_t<T>` hot path, no exceptions, construction-time carve-out);
`[arch §5.4]` (trace context storage axis);
`[arch §5.5]` (lifetime model — `[[clang::lifetimebound]]` on every view-returning constructor and accessor; the binding rule for RC#1);
`[arch §5.6]` (frozen-config rule + `Pinset` mid-session-mutable carve-out — Appendix D §D.1 amends);
`[arch §5.7]` (logging hook);
`[arch §6]` (plugin pattern — five rules including the rule-4 factory entry point);
`[arch §8.1]` (control-plane gRPC schema location);
`[arch §10]` row 2g (handoff: "Interface, file impl, FIXS rotation API — §6 plugin pattern; §4.6");
`[arch Appendix B]` (Pinning & rotation row mapping `[FIXS §5]` → §4.6 `Pinset` add-then-remove → T-011).

### B.4 SYNTHESIS Q-IDs cited (exact)

`[SYN §3.2 Q6a]` (ASIO native cancellation slots — drives §6.4 cancellation contract);
`[SYN §3.4 Q14]` (cert source — DECIDED — drives §4.1 `load_credentials`);
`[SYN §3.4 Q15]` (pinset rotation — DECIDED — add-then-remove, no atomic-swap shortcut — drives §4.3 / §6.5).

### B.5 Sibling-doc citations (exact, per `[const §VI.5]`)

`[2a §4.2]` (`trap_throw` pattern); `[2a §6.5]` (per-doc Tier 1 ceiling table precedent); `[2b §6.4]` (declaration-site lifetime annotation precedent — RC#1 binding source); `[2b §6.6]` (parse-window allocation discipline, extended); `[2c §4.8]` (`owning_message_t<>` PMR-owned-string accessor pattern — RC#1 / N-P2-2 precedent for owning `peer_identity`); `[2d §4.1]` (`Clock` interface); `[2d §4.1.1]` (Clock implementer's recipe — precedent for §6.4 cancellation recipe); `[2d §4.4]` (`EngineConfig`); `[2d §4.5]` (`SessionConfig` — Appendix D §D.2 amends); `[2d §4.7]` (two-phase close); `[2d §4.8]` (`session_executor`); `[2d §6.5]` (`cancellable_dispatch` cancellation primitive); `[2d §6.7]` (`error::invalid_session_config` outer wrapper for `SecurityProfile::unset` open-time rejection per §7.2 hand-off); `[2d §7.4]` (executor-compat surface); `[2d §7.5]` (TLS strand-safety boundary); `[2d §7.9]` (effective-clock single rule — drives RC#2 clock plumbing); `[2e §6.4]` (writer-mutex contract precedent — replaces v0.1's broken `2e §3.1` cite); `[2e §6.6]` (per-doc Tier 1 ceiling table precedent); `[2e §6.7]` (per-doc errors section — source of `store_cancelled` cancellation variant joined by 2g's `tls_load_cancelled` per §6.6; v0.3 / Opus round-2 N-P3-3 close — adds the row §B.5 was missing); `[2f §4.1.1]` (`async_mutex::async_lock(...)` surface — referenced, not consumed); `[2f §6.5]` (cancellation outcome at the 2f boundary + per-doc-prefix discipline + cancellation-group precedent — replaces v0.1's broken `2f §6.7` cite × 3); `[2f §6.6]` (Enforcement of `[const §XV.9]` — drives §6.5.2 rationale).

v0.2 / N-P1-2 close (refined v0.3): every `[2X §Y.Z]` reference above is verified against the cited doc's actual section headings. The v0.1 broken cites — the old `2f §6.7` × 3 (correct is `[2f §6.5]` for errors and `[2f §6.6]` for enforcement), the old `2e §3.1` (correct is `[2e §6.4]` for the writer-mutex contract), the old `2d §6.7` mis-routings (correct sections are `[2d §6.5]` for `cancellable_dispatch` and `[2d §7.9]` for effective-clock; `[2d §6.7]` itself does exist as the per-doc-prefix discipline / errors section in 2d v0.4 and is preserved where actually applicable, including the new §7.2 outer-rejection citation above) — were corrected in v0.2 and re-audited in v0.3. v0.3 additionally splits `[2e §6.4]` (writer-mutex contract precedent) from `[2e §6.7]` (per-doc errors section / `store_cancelled` source) per Opus round-2 N-P2-1 / N-P3-3: the §6.6 `tls_load_cancelled` row now cites `[2e §6.7]` for the variant definition while §B.5 keeps `[2e §6.4]` for the writer-mutex contract precedent and adds `[2e §6.7]` for the cancellation-variant precedent.

Engineering-judgment decisions whose primary driver is engineering judgment rather than a specific spec section — the precise field list of `cert_source::Config`, the `Pinset` `max_pins = 16` cap, the `std::shared_mutex` choice for `Pinset` writers per §6.5.2, the Tier 1 latency ceilings in §6.3, the `peer_identity` owning-value-type shape, the DoS cap defaults (8192 / 16 KiB / 64) — cite `[const §X.y]` / `[arch §X.y]` / `[SYN §3.x Q#]` / `[2X §X.y]` inline at point of use; they are not spec normatives and are intentionally listed as design-constraint references rather than coverage-index normatives in §B.1.

---

## Appendix C — Convergence log

### Round 1 (Phase A): v0.1 → v0.2 (2026-05-09)

**Phase A round 1 designation.** This is the first/only convergence pass for Phase A so far. No reset has been used; the round-cap budget remains 3 rounds with up to 1 full-rewrite reset per the `/gate-a` 3-phase A/B/C convention.

**Reviews input:**
- Codex Gate A (Phase A round 1; tally P1=8, P2=5, P3=3): `research/reviews/codex_2g_tls_review.md`
- Opus adversarial review (Phase A round 1; **post-judging combined tally 13 P1 / 7 P2 / 8 P3**; **3 root causes**; closing recommendation: **"v0.2 can ship after a single convergence pass"**): `research/reviews/opus_2g_tls_adversarial_review.md`

**Closing recommendation followed:** "v0.2 can ship after a single convergence pass."

**Root causes addressed (Opus, source of truth):**

- **RC#1 — Ownership/lifetime model on `Pinset` and `cert_source` accessors is asserted in prose without surfacing in the .hpp blocks.** Clusters Codex P1-1 (`Pinset::find` returns dangling pointer), Codex P1-2 (view-returning `cert_source` lifetimebound missing), Codex P2-1 (`pin` stores caller-owned cert), Codex P2-3-escalated-to-P1 (`Pinset::find` ≤ 100 ns budget assumes properties `atomic<shared_ptr>` does not give), Opus N-P2-1 (mid-handshake non-goal not testable from public API), Opus N-P2-2 (`peer_identity` SAN spans bound to anonymous storage). **Single fix.** §4.1 — `[[clang::lifetimebound]]` annotations moved to the abstract-base declaration sites (the v0.1 "annotate only the override" workaround is retired per `[arch §5.5]` / `[2b §6.4]`). §4.3 — `Pinset::find(...) -> pin_view` (value-typed handle carrying `shared_ptr<const pin_snapshot>`) replaces the v0.1 raw `pin const*`; `pin` stores owned diagnostic fields only (PMR-copied subject_dn + san_dns, no caller-bytes-aliasing `Certificate`); `add(Certificate)` deep-copies into the snapshot arena. §4.5 — `peer_identity` is OWNING (PMR-allocated `pmr::string` + `pmr::vector<pmr::string>` members; view accessors carry `[[clang::lifetimebound]]` bound to `*this`, mirroring the `[2c §4.8]` `owning_message_t<>` pattern). §6.2 / §6.3 — `Pinset::find` rebudgeted as `snapshot_acquire ≤ 30 ns` + `linear_scan_16 ≤ 100 ns` ≤ 130 ns p99 combined; the v0.1 ≤ 100 ns ceiling that contradicted `atomic<shared_ptr>` cost is dropped; the seqlock fallback is dropped from the design (Tier 2/3 platforms are out of `[const §II]` scope). §6.5.1 — TLS handshake-time pinset access binding contract: handshake captures `Pinset::snapshot()` ONCE at handshake start (not repeated `find()` calls).

- **RC#2 — Cert-credential surface is incomplete (no key-handle representation, no cancellation handoff, no factory, sign callback doesn't address HSM strand-impact, no DoS bounds, no clock parameter).** Clusters Codex P1-3 (cert-source can't supply software key, can't safely host HSM signing), Codex P1-4 (`verify_peer` has no clock parameter), Codex P1-5 (`load_chain` cancellation cites `[2d §6.5]` but doesn't use the shape), Codex P1-8 (no `make_file_cert_source` factory entry), Opus N-P1-4 (DoS surface — RSA upper-cap, cert ASN.1 cap, SAN cardinality cap), Opus N-P2-3 (CipherPolicy compile-time vs runtime split unowned). **Single fix.** §4.1 — `cert_source` reshaped: `load_leaf` + `load_chain` + synchronous `sign(...)` + `load_trust_anchors` (4 pure-virtual) collapses to `load_credentials() -> awaitable<expected_t<local_credentials>>` + `load_trust_anchors()` (**2 pure-virtual**, well under [const §XIV.2]'s 5-cap). `local_credentials::signer` is `variant<software_key_ref, async_signer_ref>`; HSM impls return `async_signer_ref` (awaitable, suspends OFF the session strand via `cancellable_dispatch`); software impls return `software_key_ref` (engine-internal handle for OpenSSL's `EVP_DigestSign*` fast path). §6.4 — `cancellable_dispatch` recipe published verbatim (read `co_await asio::this_coro::executor`, recover `fixpp::core::session_executor`, read cancellation_state, post via `cancellable_dispatch` before returning `tls_load_cancelled`); 2g is now the second pluggable awaitable after `[2d §4.1.1]` Clock::sleep_until and inherits the same recipe-publication obligation. §4.5 — `SslCtxConfig::clock` (`shared_ptr<Clock>`) added; `verify_peer` consumes `cfg.clock->now()`; `cfg.clock` is the SESSION-scoped `effective_clock` per `[2d §7.9]` (the v0.1 prose conflated `EngineConfig::clock` with `effective_clock`). §4.2 — `make_file_cert_source(Config, std::pmr::memory_resource*) -> expected_t<shared_ptr<cert_source>>` added per `[arch §6]` rule 4; the throwing constructor is preserved for in-process C++ direct-construction callers per `[arch §5.3]`. §1.1 — three DoS caps added (`max_rsa_key_bits = 8192`, `max_cert_der_bytes = 16 KiB`, `max_san_entries = 64`); §6.6 — three new error variants (`tls_rsa_key_too_large`, `tls_cert_der_too_large`, `tls_san_entries_exceeded`). §4.4 — `CipherPolicy::is_allowed(string_view) constexpr noexcept -> bool` published; the runtime variant `tls_cipher_not_allowed` now has an owned runtime path through 2i.

- **RC#3 — `Pinset` reachability path is split across three incompatible shapes; no Appendix D drop-in materialises the path the doc relies on.** Clusters Codex P1-6 (Pinset ownership split: function param vs concrete `file_cert_source::pinset()` accessor vs deferred amendment), Opus N-P1-1 (Appendix D drop-in declared in §10 Q8 but never written), Codex P2-5 (SecurityProfile-to-FIXS profile mapping not normative). **Single fix.** Pick **`SessionConfig::pinset`** as the only published reachability. §4.3 / §4.6 — drop the "`file_cert_source` typically exposes a `pinset()` accessor" claim; the abstract `cert_source` does not publish `pinset()`; downstream 2h reads `SessionConfig::pinset` only (no downcast). §10 Q8 — closed as DECIDED. §11 — repointed to Appendix D. **NEW Appendix D** — three drop-ins in `[2d App D]` / `[2f App D]` exact-text format: D.1 amends `[arch §5.6]` "pinset rotation" carve-out; D.2 appends `pinset` to `[2d §4.5]` `SessionConfig`; D.3 adds three coverage-index rows. §4.5.1 — normative `SecurityProfile`-to-OpenSSL-mode mapping table (Codex P2-5 close).

**Per-finding resolution table:**

| Finding | Severity (after Opus judging) | Resolution | Section(s) edited |
|---|---|---|---|
| Codex P1-1 — `Pinset::find` returns dangling pointer | P1 (confirmed) | RC#1: §4.3 publishes `pin_view` value-typed handle carrying `shared_ptr<const pin_snapshot>`; the matched entry stays alive for as long as the caller holds the view. | §4.3, §6.2, §6.3 |
| Codex P1-2 — view-returning cert-source declarations omit lifetime annotations | P1 (confirmed) | RC#1: §4.1 moves `[[clang::lifetimebound]]` to abstract-base declaration sites per `[arch §5.5]` / `[2b §6.4]`. The v0.1 "implementation-only" workaround is retired. | §4.1, §4.6 |
| Codex P1-3 — cert-source credential surface cannot supply software private key, cannot safely host HSM signing | P1 (confirmed) | RC#2: §4.1 reshapes to `load_credentials()` returning `local_credentials` with `signer = variant<software_key_ref, async_signer_ref>`. Pure-virtual count drops 4 → 2. | §4.1, §4.2, §3.10, §6.4 |
| Codex P1-4 — `verify_peer` needs the engine/effective clock but the API does not carry one | P1 (confirmed) | RC#2: §4.5 adds `SslCtxConfig::clock` (`shared_ptr<Clock>`); `verify_peer` consumes `cfg.clock->now()`; resolved as `effective_clock` per `[2d §7.9]`. | §4.5, §7.3 |
| Codex P1-5 — `load_chain` cancellation cites 2d but does not use the 2d shape | P1 (confirmed) | RC#2: §6.4 publishes the `cancellable_dispatch` recipe verbatim (4-step body) for `load_credentials`. | §6.4, §9 seam #13 |
| Codex P1-6 — Pinset ownership split across two incompatible access paths | P1 (confirmed) | RC#3: pick `SessionConfig::pinset`; drop concrete-accessor claim; close §10 Q8 as DECIDED; write Appendix D §D.1 / §D.2 / §D.3 drop-ins. | §4.3, §4.6, §10 Q8, §11, Appendix D |
| Codex P1-7 — `CipherPolicy`'s compile-time refusal sketch is not valid C++ | P1 (confirmed) | §6.1 rewritten with **value-based** consteval functions (no `string_view` NTTP); same body shared with `CipherPolicy::is_allowed(...)`. CMake `try_compile` negative test (§9 seam #10) confirms compile failure on banned addition; companion positive test confirms the published arrays compile clean. | §4.4, §6.1, §9 seam #10 |
| Codex P1-8 — `cert_source` plugin omits required factory entry point | P1 (confirmed) | RC#2: §4.2 adds `make_file_cert_source(Config, std::pmr::memory_resource*) -> expected_t<shared_ptr<cert_source>>` per `[arch §6]` rule 4; new §9 seam #17. | §4.2, §9 seam #17 |
| Codex P2-1 — `Pinset` stores caller-owned certificate views in long-lived snapshots | P2 (confirmed) | RC#1: §4.3 `pin` redefined to store only owned diagnostic fields (`pmr::string subject_dn`, `pmr::vector<pmr::string> san_dns`, `core::time_point added_at`); `add()` deep-copies. | §4.3 |
| Codex P2-2 — one-way profile not actually deprecated at the enum declaration | P2 (confirmed) | §4.5 — `[[deprecated("...")]]` attribute moved onto the `one_way_ca` enumerator, not the comment. `[const §XII.5]` compile-time diagnostic now actually fires. | §4.5 |
| Codex P2-3 — `Pinset::find` latency budget assumes properties `atomic<shared_ptr>` does not guarantee | **P1 (escalated by Opus)** | RC#1: §6.3 split-budget — `snapshot_acquire ≤ 30 ns` + `linear_scan_16 ≤ 100 ns` ≤ 130 ns p99 combined; the seqlock fallback is dropped from the v0.2 design; Tier 2/3 platforms rejected at compile time. New §9 seam #4 (snapshot_acquire) + #5 (find combined). | §1.1, §6.2, §6.3, §9 seams #4 + #5 |
| Codex P2-4 — add-then-remove concurrency seam is scheduler-flaky | P2 (confirmed) | §9 seam #1 split: deterministic seam #1 (hook-driven, cannot pass under single-CAS publication, cannot fail under correct add-then-remove) + stress seam #2 (TSan UAF / race detector). | §9 seams #1 + #2 |
| Codex P2-5 — SecurityProfile-to-FIXS profile mapping under-specified | P2 (confirmed) | RC#3: §4.5.1 — normative table mapping each profile to OpenSSL `verify_mode`, local-cert-required, CA-anchors-required, pinset-required, pin-check-policy. §9 seam #11 extended to verify each row. | §4.5.1, §9 seam #11 |
| Codex P3-1 — header and Appendix B use vague/non-canonical citations | P3 (confirmed) | Header `Cites:` line replaced with exact `[const §X.Y]` / `[arch §X.Y]` / `[FIXS §X.Y]` references per `[const §VI.5]`. Appendix B split into B.1 normative + B.2 / B.3 / B.4 / B.5 design-constraint references. The `[FIXS RC1 §5]` project-rolled-up cite is preserved per `[arch Appendix B]` with the underlying `§2.3 + §4.3` annotation. | header, Appendix B |
| Codex P3-2 — catalogue paths `library/.specify/feature-catalogue.md` are stale relative to the repo | P3 (confirmed editorial) | 2g's `library/spec/...` wording is preserved (already correct); the orchestration-prompt path is the stale axis and is the orchestrator's call (not 2g's edit scope). Recorded for the orchestrator. | (no doc edit) |
| Codex P3-3 — T-039 wording wobbles between X.509 v3-only and v2/v3 | P3 (confirmed) | §1 Goal #9, §4.5 prose, and Appendix A row T-039 all pinned to "**X.509 v2/v3** (v1 rejected)" — three sites unified. | §1, §4.5, Appendix A |
| Opus N-P1-1 — §10 Q8's declared-but-unwritten Appendix D drop-in is missing entirely | P1 (NEW, RC#3 cluster) | RC#3: NEW Appendix D with three drop-ins in `[2d App D]` / `[2f App D]` exact-text format — D.1 (`[arch §5.6]` carve-out), D.2 (`[2d §4.5]` `SessionConfig::pinset` field), D.3 (`coverage-index.md` rows). | §10 Q8, §11, Appendix D |
| Opus N-P1-2 — three sibling-doc citations to `2f §6.7` reference a section that does not exist; one cite to `2e §3.1` mislabels the section (broken cites are intentionally written as plain backticked strings here, not bracketed cite tokens, so automated cite-checkers do not re-flag the historical references) | P1 (NEW) | Header `Cites:` line + every `[2X §Y]` reference in §6.6 / §3.5 / §3.9 / §B.5 audited and corrected: `2f §6.7` (× 3) → `[2f §6.5]` (errors + cancellation-group precedent) and `[2f §6.6]` (enforcement of `[const §XV.9]`); `2e §3.1` (writer-mutex precedent) → `[2e §6.4]`. The error-variant table in §6.6 (`tls_load_cancelled` row) was the exact site Opus flagged at N-P3-5; v0.2 fixes both citations together. | header, §3.5, §3.9, §6.6, §B.5, §B.2 |
| Opus N-P1-3 — §3.5 / §3.9 / §6.5 give three different rationales for choosing `std::shared_mutex` over `async_mutex`; the rationale chain is internally inconsistent and the trigger interpretation is wrong | P1 (NEW) | Three v0.1 rationales collapsed into one consolidated subsection §6.5.2 with a single ordered three-fact chain (type is `shared_mutex` not `mutex`; use is synchronous; reader path is lock-free). §3.5 and §3.9 carry only inheritance pins pointing at §6.5.2. The "header doesn't include awaitable" circular argument is dropped. | §3.5, §3.9, §6.5.2 |
| Opus N-P1-4 — DoS surface for `verify_peer` is unspecified — RSA upper-cap, cert ASN.1 cap, SAN cardinality cap | P1 (NEW, RC#2 cluster) | RC#2: §1.1 adds three DoS caps (8192 bits / 16 KiB / 64 SAN entries); §4.2 `Config` carries the caps; §4.5 `verify_peer` enforces at entry; §6.6 adds three error variants (`tls_rsa_key_too_large`, `tls_cert_der_too_large`, `tls_san_entries_exceeded`); §9 seam #14 covers each. | §1.1, §4.2, §4.5, §6.6, §9 seam #14 |
| Opus N-P2-1 — §2 non-goal "No mid-handshake pinset rotation" is asserted but not testable from the published surface; seam #13 is right but the contract bypasses the public API | P2 (NEW, RC#1 cluster) | RC#1: §6.5.1 publishes the binding contract — TLS handshake-time pinset access MUST use `Pinset::snapshot()` captured ONCE at handshake start. §9 seam #15 (rotation does not affect in-flight) updated to verify the captured-once contract through instrumentation. | §6.5.1, §9 seam #15 |
| Opus N-P2-2 — `peer_identity::san_dns_names` / `san_uris` are spans into anonymous storage; the lifetime contract is unstated | P2 (NEW, RC#1 cluster) | RC#1: §4.5 makes `peer_identity` OWNING — `pmr::string subject_dn` + `pmr::vector<pmr::string> san_dns_names_owned` + `pmr::vector<pmr::string> san_uris_owned`; view accessors `subject_dn_view()` / `san_dns_names()` / `san_uris()` carry `[[clang::lifetimebound]]` bound to `*this`; the v0.1 fragile cross-doc lifetime contract ("bounded by 2h's `SSL*` object") is replaced by self-ownership. Mirrors the `[2c §4.8]` `owning_message_t<>` precedent. | §4.5, §4.6 |
| Opus N-P2-3 — Compile-time vs runtime cipher-policy claim is internally split — `CipherPolicy` is value-typed but only `static constexpr` members; `tls_cipher_not_allowed` runtime variant exists for an undefined runtime path | P2 (NEW, RC#2 cluster) | RC#2: §4.4 adds `[[nodiscard]] static constexpr bool CipherPolicy::is_allowed(std::string_view) noexcept`; §6.1 publishes the value-based consteval body shared between compile-time `static_assert` and runtime `is_allowed`. The runtime path is now owned. | §4.4, §6.1, §6.6 |
| Opus N-P3-1 — §4.4 `banned_tokens` list mixes compile-time-banned and "not-on-the-allow-list" tokens — `TLS_AES_128_CCM` is the latter, not the former | P3 (NEW) | §4.4 inline comment block clarifies the two refusal axes: (1) tokens `[const §XII.4]` / `[const §XV.11]` explicitly bans, (2) tokens not on the allow-list (TLS_AES_128_CCM is the latter). The `banned_tokens` entry for CCM is documented as a belt-and-braces intersection guard, not a constitutional ban. | §4.4 |
| Opus N-P3-2 — §4.1 `sign_response::signature` lifetime contract is in a comment, not at the declaration site | P3 (NEW) | §4.1 — `sign_response::signature` is now OWNING (`std::pmr::vector<std::byte>`) per RC#2's variant-typed signer; the v0.1 temporal "caller copies before further use" contract is replaced by ownership and the spatial lifetime is automatic. `sign_request::tbs` carries `[[clang::lifetimebound]]` at the struct member declaration site. | §4.1 |
| Opus N-P3-3 — §4.2 `password_cb` is `std::function<std::string()>` — no PMR-aware shape, may heap-allocate | P3 (NEW) | §4.2 `Config::password_cb` retained as `std::function<std::string()>` but the trailing comment annotates it as construction-time-only per `[arch §5.3]` carve-out; v1.0 does not promote it to a hot-path-shape. The `[2d §4.5]` `trace_context_provider` precedent applies to hot-path frozen-config callables, not to construction-time PEM-decryption callbacks; the decision is consistent with `[arch §5.3]` and recorded for posterity. | §4.2 |
| Opus N-P3-4 — §1 Goal 4 "RSA-PSS ≥ 2048" reads as if RSA-PSS has a key size | P3 (NEW) | §1 Goal #4 wording corrected — "RSA-PSS over RSA keys of size ≥ 2048 bits" (the parenthetical attaches to the key size, not the scheme); §4.4 `sig_algs` comment block clarifies RSA-PSS as a padding scheme over RSA keys. | §1, §4.4 |
| Opus N-P3-5 — §6.6 error variant `tls_load_cancelled` claims to "join" cancellation groups across `2d §6.7`, `2e §6.7`, `2f §6.7` — the latter cite is broken (per N-P1-2; backticked-not-bracketed here so the historical broken cites do not re-trigger the cite-checker) | P3 (NEW) | Subsumed by N-P1-2 fix: §6.6 `tls_load_cancelled` row in v0.2 cited `[2d §6.5] cancellable_dispatch → dispatch_aborted`, `[2d §4.1.1] Clock::sleep_until → clock_sleeps_cancelled`, `[2e §6.4] store_cancelled` (writer-mutex contract precedent), `[2f §6.5] sync_lock_aborted` — but the `[2e §6.4]` target was wrong (the writer-mutex contract section, not the variant-source section), corrected in v0.3 to `[2e §6.7]` per round-2 N-P2-1. | §6.6 |

**Codex findings disagreed with — none.** Every Codex finding (8 P1 / 5 P2 / 3 P3) was judged by Opus as either confirmed at the rated severity or escalated (Codex P2-3 → P1 by Opus). No Codex finding was judged "Disagree" by Opus. No Codex counter-proposal was rejected.

**Net-effect summary:** v0.2 lands the v1.0 spine intact (pluggable `cert_source` ≤ 5 pure-virtual; `Pinset` add-then-remove; compile-time `CipherPolicy`; `SecurityProfile` enum with `unset` sentinel; T-039/T-040 partition with 2h; `[arch §5.6]` mid-session-mutable carve-out) and converges every finding through three root-cause-driven structural changes plus line-edits. Net effect: **+4 test seams** (v0.1 = 13 seams → v0.2 = 17 seams; new: #2 stress test split out of #1; #4 `snapshot_acquire` benchmark; #16 `pin_view` lifetime under rotation; #17 `make_file_cert_source` factory parity); **+3 error variants** (v0.1 = 12 variants → v0.2 = 15 variants; new: `tls_rsa_key_too_large`, `tls_cert_der_too_large`, `tls_san_entries_exceeded`); **−2 pure-virtual on `cert_source`** (v0.1 = 4 → v0.2 = 2; `load_leaf` + `load_chain` + `sign(...)` collapsed into `load_credentials`); **+1 normative §4.5.1 SecurityProfile-to-OpenSSL-mode table**; **+1 NEW Appendix D** with three drop-ins (D.1 amends `[arch §5.6]`, D.2 amends `[2d §4.5]` `SessionConfig`, D.3 amends `library/spec/coverage-index.md`); §10 Q8 closed; one §6.5.2 consolidated rationale subsection (collapses §3.5 / §3.9 / §6.5 into one). Touched sections: status block, header `Cites:` line, §1, §1.1, §1.2, §3.5, §3.7, §3.9, §3.10, §4.1, §4.2, §4.3, §4.4, §4.5 (+ new §4.5.1), §4.6, §5, §6.1, §6.2, §6.3, §6.4, §6.5 (+ new §6.5.1, §6.5.2), §6.6, §7.1, §7.3, §7.4, §9, §10 Q8, §11, Appendix A row T-039, Appendix B (full split), Appendix C (this entry), NEW Appendix D.

### v0.2 → v0.3 (round 2)

**Phase A round 2 designation.** This is the second convergence pass for Phase A. No reset has been used; the round-cap budget remains 3 rounds with up to 1 full-rewrite reset per the `/gate-a` 3-phase A/B/C convention. The round-2 reviews flag no new root cause; the v0.1 → v0.2 rewrite addressed all three round-1 RCs structurally — round 2 is a single convergence (line-edit-class) pass.

**Reviews input:**
- Codex Gate A (Phase A round 2; tally P1=1, P2=1, P3=1): `research/reviews/codex_2g_2_tls_review.md`
- Opus adversarial review (Phase A round 2; **post-judging combined tally 4 P1 / 4 P2 / 5 P3**; **no new root causes**; closing recommendation: **"v0.3 can ship after a single convergence pass"**): `research/reviews/opus_2g_2_tls_adversarial_review.md`

**Closing recommendation followed:** "v0.3 can ship after a single convergence pass."

**Per-finding resolution table:**

| Finding | Severity (after Opus judging) | Resolution | Section(s) edited |
|---|---|---|---|
| Codex round-2 P1-1 — Appendix D.1 is not a byte-faithful drop-in for `architecture.md` §5.6 (the v0.2 "Before" block quotes only the middle sentence; missing the leading **`SessionConfig` is value-typed…** prefix and the trailing **dialect-overlay-categorical-rejection** sentence) | P1 (confirmed) | Appendix D.1 expanded — the "Before" block is now the full bullet at `architecture.md` line 407 verbatim (the same line preserved here byte-faithfully); the "After" block applies on top with the new sentence inserted **after** the "thread-aware" sentence and **before** the "Mid-session dialect-overlay swap is rejected categorically" sentence so the dialect-overlay-categorical-rejection clause is preserved at the bullet's end. The "Tension" preamble is updated to reference the byte-faithfulness fix. | Appendix D.1 |
| Opus round-2 N-P1-1 — §8 PMR-recap bullets 2 (Pinset reachability via `cert_source::pinset()` accessor) and 4 (`peer_identity` view fields bounded by 2h's `SSL*`) carry v0.1 prose retired by RC#1 / RC#3 | P1 (NEW) | §8 lifetime-classes bullets 2 and 4 rewritten to mirror RC#1 / RC#3 closures: bullet 2 — `Pinset` reached through `SessionConfig::pinset` per `[arch §5.6]` carve-out (Appendix D §D.1 / §D.2); abstract `cert_source` does NOT publish `pinset()`. Bullet 4 — `peer_identity` is OWNING (PMR-copied SAN strings + subject DN); view accessors `[[clang::lifetimebound]]`-bound to `*this`; 2h's `SSL*` lifetime is independent — the v0.1 fragile cross-doc lifetime contract is retired. | §8 |
| Opus round-2 N-P1-2 — §7.4 carries the v0.1 "header doesn't include `<asio/awaitable.hpp>`" rationale that §6.5.2 explicitly retired as a circular argument | P1 (NEW) | §7.4 rewritten as a redirect to §6.5.2's three-fact chain (type is `std::shared_mutex` not `std::mutex`; use is fully synchronous; reader path is lock-free); the retired include-closure defence is removed. Pattern matches the §3.5 / §3.9 redirects. | §7.4 |
| Codex round-2 P2-1 — historical `[2f §6.7]` × 3 bracket references in Appendix C still trip an automated cite-checker | P2 (confirmed) | All three Appendix C historical `[2f §6.7]` strings de-bracketed to plain backticked `2f §6.7` form (also `[2e §3.1]` in §6.6 prose and §B.5); historical broken cites no longer match the bracket-cite gate. | §6.6 prose, Appendix C (N-P1-2 row + N-P3-5 row), §B.5 |
| Opus round-2 N-P2-1 — §6.6 `tls_load_cancelled` row cites `[2e §6.4] store_cancelled` but `store_cancelled` actually lives in `[2e §6.7]` | P2 (NEW) | §6.6 row updated to cite `[2e §6.7] store_cancelled` (the per-doc errors section where the variant is defined) while noting that the writer-mutex contract precedent itself is `[2e §6.4]`; §B.5 keeps `[2e §6.4]` for the writer-mutex precedent and adds `[2e §6.7]` for the cancellation-variant precedent (mirrors the split). | §6.6 row, §B.5 |
| Opus round-2 N-P2-2 — §6.3 `Pinset::snapshot_acquire ≤ 30 ns p99` budget under-quotes `atomic<shared_ptr>` cost on glibc + libstdc++ where the implementation takes an internal mutex (libstdc++ ≤ 15); the `is_always_lock_free` `static_assert` would reject Tier 1 by accident | P2 (NEW) | §1.1, §6.1, §6.2, §6.3, §9 seam #4 / #5 all updated. The hard `static_assert(is_always_lock_free)` is **softened to a toolchain-floor note**; §6.3 row 1 is **platform-conditional**: ≤ 30 ns p99 on the lock-free floor (libstdc++ ≥ 16 with `__atomic_shared_ptr`; libc++ on x86_64 with `cmpxchg16b`); ≤ 100 ns p99 on the libstdc++ ≤ 15 internal-mutex fallback path. §9 seam #4 / #5 record the platform tuple so CI selects the applicable ceiling. | §1.1, §6.1, §6.2, §6.3, §9 seams #4 + #5 |
| Codex round-2 P3-1 — §7 omits the `Session::open` `SecurityProfile::unset` rejection hand-off; outer `error::invalid_session_config` per `[2d §4.5]` vs inner `tls_invalid_security_profile` per 2g §6.6 left interpretable | P3 (confirmed) | §7.2 expanded with a new "**`SecurityProfile::unset` open-time rejection hand-off**" paragraph: `Session::open` MUST reject `SecurityProfile::unset` BEFORE invoking 2h's TLS construction; outer error at `Session::open` is `error::invalid_session_config` per `[2d §4.5]` / `[2d §6.7]`; inner error at the `make_ssl_ctx_config(...)` boundary is `error::tls_invalid_security_profile` per §6.6 (covers the C-ABI / hot-reload paths that bypass `Session::open`). The §7.2 §B.5 cite list is extended with `[2d §6.7]` for the outer wrapper. New §7.2.1 records the trust-anchor null check on `mtls_pinned`. | §7.2 (+ new §7.2.1), §B.5 |
| Opus round-2 N-P3-1 — `pin_view::value` `[[clang::lifetimebound]]` placement is on a continuation line after a trailing comment that itself describes the attribute; editorially awkward | P3 (NEW) | `pin_view::value` declaration reformatted onto one line: `pin const* value [[clang::lifetimebound]] = nullptr;` with the explanatory comment trailing. (`= nullptr` initialiser added so the default-constructed `pin_view` is well-defined.) | §4.3 |
| Opus round-2 N-P3-2 — `CipherPolicy::is_allowed`'s inline lambda body in §6.1 omits `noexcept` while the surrounding function is `noexcept` | P3 (NEW) | §6.1 lambda annotated: `auto in = [tok](auto const& list) noexcept { ... };`. Behaviour unchanged; declaration / definition no longer drift. | §6.1 |
| Opus round-2 N-P3-3 — §B.5 sibling-doc citations footnote does not enumerate `[2e §6.7]` even though 2g normatively depends on it for `store_cancelled` | P3 (NEW) | §B.5 enumerates `[2e §6.7]` (per-doc errors section / source of `store_cancelled`); keeps `[2e §6.4]` for the writer-mutex contract precedent. | §B.5 |

**Round-1 reviews carry-over verdict (round-2 confirmation):** Opus round-2 confirmed all three round-1 RCs are **structurally closed in v0.2** — RC#1 (`pin_view` value-typed handle + lifetime-bound at the abstract base + owning `peer_identity` + owned `pin` diagnostics), RC#2 (2-pure-virtual `load_credentials` returning `local_credentials` with `variant<software_key_ref, async_signer_ref>` + `SslCtxConfig::clock` + `make_file_cert_source` factory + DoS caps + the §6.4 cancellation recipe + `CipherPolicy::is_allowed`), RC#3 (`SessionConfig::pinset` picked + Appendix D §D.1/§D.2/§D.3). Round 2 is line-edit residue only — sections the v0.2 rewriter touched (§4.x / §6.5.2 / §6.4 / §6.6 / Appendix D) but neighbouring sections (§7.4 / §8) drifted. v0.3 closes the residue.

**Codex findings disagreed with — none.** Every Codex round-2 finding (1 P1 / 1 P2 / 1 P3) was confirmed by Opus at the same severity. No Codex counter-proposal was rejected.

**Net-effect summary:** v0.3 is a single convergence pass over v0.2's three round-1 root-cause closures — no new RC, no new feature. **Net effect:** **±0 test seams** (still 17; seams #4 and #5 are re-tuned for the platform-conditional ceiling but no new seam is added); **±0 error variants** (still 15); **±0 pure-virtual on `cert_source`** (still 2); **+1 §7.2.1** (engine-side trust-anchor null check on `mtls_pinned`, recorded for completeness); **±0 Appendix D drop-ins** (still three: D.1 / D.2 / D.3 — D.1 is rewritten byte-faithfully against `architecture.md` line 407, D.2 / D.3 unchanged); §6.3 row 1 / row 2 ceilings re-stated as platform-conditional (libstdc++ ≥ 16 floor vs libstdc++ ≤ 15 fallback). Touched sections: status block, header `Convergence log` line, §1.1 (`Pinset::find` latency bullet), §6.1 (hot-path snapshot prose + lambda `noexcept`), §6.2 (publication-primitive softening), §6.3 (platform-conditional row 1 / row 2), §4.3 (`pin_view::value` reflow), §6.6 (`tls_load_cancelled` row cite swap), §7.2 (+ new §7.2.1; `Session::open unset` hand-off), §7.4 (retired-rationale rewrite), §8 (lifetime-class bullets 2 and 4 rewritten), §9 (seams #4 + #5 platform-tuple re-tune), §B.5 (add `[2d §6.7]` + `[2e §6.7]`; de-bracket two historical broken cites), Appendix C (this entry + de-bracket two Appendix C historical broken-cite strings), Appendix D.1 (byte-faithful "Before" block).

### v0.3 → v0.4 (round 3 / user-authorized post-cap pass)

**Phase A round 3 designation.** This is the third / final convergence pass for Phase A. The round cap was hit at round 3 with line-edit-class residuals (combined post-judging 2 P1 / 0 P2 / 1 P3, no new root causes); the user authorized a single final convergence pass per the post-cap line-edit precedent. Same convergence path 2c, 2d, and 2e took at their round caps (2c v1.2 → v1.3, 2d v0.3 → v0.4, 2e v0.3 → v0.4).

**Reviews input:**
- Codex Gate A (Phase A round 3; tally P1=2, P2=0, P3=1): `research/reviews/codex_2g_3_tls_review.md`
- Opus adversarial review (Phase A round 3 — cap round; **post-judging combined tally 2 P1 / 0 P2 / 1 P3**; **no new root causes**; closing recommendation: **"Round cap hit; user-authorized post-cap line-edit pass recommended"**): `research/reviews/opus_2g_3_tls_adversarial_review.md`

**Closing recommendation followed:** "Round cap hit; user-authorized post-cap line-edit pass recommended."

**Per-finding resolution table:**

| Finding | Severity (after Opus judging) | Resolution | Section(s) edited |
|---|---|---|---|
| Codex round-3 P1-1 / Opus round-3 P1-1 — Pinset PMR snapshot can outlive `cfg_.mr` (UAF on snapshot teardown after `~Pinset()` when reader holds the snapshot past `Pinset` destruction) | P1 (confirmed) | **Option (a) engine-anchored MR contract** picked (Opus round-3 P1-1 counter-proposal (a); cheapest fix shape; preserves §6.5.1 capture-once-for-handshake contract; preserves §6.5 invariant 4; zero per-snapshot cost; zero API surface change; matches the engine-default reachability already established for `cert_source` per [2d §4.4] / [2d §4.5]). §4.3 `Pinset::Config::mr` field-comment block documents the contract: "MUST outlive every `shared_ptr<const pin_snapshot>` the `Pinset` ever publishes" — engine-default satisfies by construction (per [2d §4.4]), user-supplied is a documented caller contract aligned with existing constitutional caller-owned-resource patterns. §4.6 ownership-rules adds an explicit bullet stating the contract and explaining why option (a) over (b) [snapshot owns a `shared_ptr<memory_resource>` guard — adds one indirection per snapshot allocation, requires touching the `using pin_snapshot = std::pmr::vector<pin>;` shape] and (c) [snapshots cannot outlive `~Pinset()` — breaks §6.5.1 capture-once contract under teardown-during-handshake]. §6.2 publication-primitive paragraph adds a "Snapshot PMR-resource lifetime" subparagraph noting that the `shared_ptr<const pin_snapshot>` keeps storage alive but the resource is the resource owner's contract. §8 PMR-recap table row 2 (Lifetime column) rewritten — `cfg_.mr` lifetime is "MUST outlive every `shared_ptr<const pin_snapshot>` the `Pinset` ever publishes — i.e., the union of `Pinset` instance lifetime AND every reader-held snapshot lifetime"; bullet 5 (Pinset snapshot lifetime class) carries the explicit MR-lifetime requirement so §4.3 / §6.2 / §8 close into one consistent statement. §9 seam #18 added: post-`~Pinset()` snapshot lifetime under user-supplied scope-outer `monotonic_buffer_resource`; ASan/TSan exercises read-only access on the snapshot AFTER `~Pinset()`, then drains the reader; companion negative test exercises the contract-violation path (user MR scope INSIDE `Pinset` scope) and asserts ASan UAF surfaces — documenting that the operator owns the contract and the engine-default path is contract-satisfied by construction. | §4.3 (`Pinset::Config::mr` comment block), §4.6 (new ownership-rules bullet — `Pinset::Config::mr` lifetime contract + Why this option), §6.2 (new "Snapshot PMR-resource lifetime" subparagraph), §8 (table row 2 + bullet 5), §9 seam #18 (new) |
| Codex round-3 P1-2 / Opus round-3 P1-2 — Appendix D.3 is not a byte-faithful `coverage-index.md` amendment (the v0.3 "Before" block was a three-column placeholder; the live source is a five-column table with concrete rows for `§3.4 → T-039`, `§4.1 → T-040`, `§4.2 → T-040`, `§4.4 → T-041` already pre-filled with `MISSING → row added` gap notes at lines 132–166) | P1 (confirmed) | **Option (ii) rewrite against the live five-column schema** picked (Opus round-3 P1-2 counter-proposal (ii); the existing rows already encode the FIXS-section → catalogue-ID mapping at lines 155 / 159 / 162; the amendment is now an in-place update of the Gap note column on those three rows from `MISSING → row added (T-XXX)` to `covered by [2g §X.Y]`). The "Before" block is rewritten as the verbatim FIXS RC1 section header + introduction paragraph + column-header row + every existing row from §1 through §4.4 (lines 132–162 of `coverage-index.md`, byte-faithful). The "After" block is the same block with three in-place row updates on §3.4 / §4.1 / §4.4 — the §4.2 row is preserved byte-faithfully (already `covered by T-040`). The 2h cross-cut spec-section reference for §3.4's wiring half is intentionally NOT named in the Gap note column (the catalogue-ID-level cross-cut is preserved by the existing `T-039` Catalogue IDs entry and is fully traced via 2g §A.2). The drop-in is a three-line in-place edit; the orchestrator's apply step is mechanical. §11 Hand-off bullet 3 rewritten to describe the new D.3 shape. Appendix D header reflects the v0.4 byte-faithfulness refresh. | Appendix D (header), Appendix D.3 (full rewrite — Tension paragraph + Before block + After block + closing paragraph), §11 (Hand-off bullet 3) |
| Codex round-3 P3-1 / Opus round-3 P3-1 — §3.2's "verbatim" excerpt of `[arch §5.6]` is not byte-faithful (bolds `pinset rotation`; truncates the trailing `Mid-session dialect-overlay swap is rejected categorically per [2c §7.2]` sentence); D.1's "Before" block is byte-faithful but §3.2's recap is not, creating a local quotation-label discrepancy against the §3 preamble's "verbatim" promise | P3 (confirmed) | §3.2 quoted block replaced with the byte-faithful text (drop the bolding on `pinset rotation`; restore the trailing dialect-overlay-categorical-rejection sentence; preserve the leading bullet hyphen) — exactly the same text D.1's "Before" block carries. The §3.2 recap now matches `architecture.md` line 407 byte-for-byte and honours the §3 preamble's "verbatim" promise. A trailing parenthetical note documents the v0.4 / round-3 P3-1 close. | §3.2 |

**Codex findings disagreed with — none.** Every Codex round-3 finding (2 P1 / 0 P2 / 1 P3) was confirmed by Opus at the same severity. No Codex counter-proposal was rejected.

**Round-cap precedent.** The shape of this round-3 → round-3-converged pass matches the 2c v1.2 → v1.3 / 2d v0.3 → v0.4 / 2e v0.3 → v0.4 / 2f v1.4 → v1.5 round-3 line-edit precedent: round cap hit with line-edit-class residuals against existing structural closures (no new root cause), user-authorized single post-cap pass converges the residuals without re-opening the design, the v1.0 spine carries forward intact (≤ 5 pure-virtual `cert_source` at 2 actual; `Pinset` add-then-remove with no atomic-swap; compile-time `CipherPolicy`; `SecurityProfile` enum with `unset` sentinel + `[[deprecated]]` enumerator; T-039/T-040 partition with 2h; T-041 cross-cut into Phase-4; the §4.5.1 normative table; §6.4 cancellation recipe; §6.5.2 consolidated `std::shared_mutex` rationale; `pin_view` value-typed handle with `[[clang::lifetimebound]]` at the abstract base; owning `peer_identity`; the now-three signed-off Appendix D drop-ins).

**Net-effect summary:** v0.4 is a single user-authorized post-cap line-edit pass over v0.3's three structural closures — no new RC, no new feature, no new structural change. **Net effect:** **+1 test seam** (17 → 18; seam #18 — post-`~Pinset()` snapshot lifetime under the §4.3 `Config::mr` contract); **±0 error variants** (still 15); **±0 pure-virtual on `cert_source`** (still 2); **±0 Appendix D drop-ins** (still three: D.1 / D.2 / D.3 — D.3 is rewritten byte-faithfully against `library/spec/coverage-index.md` lines 132–162; D.1 / D.2 unchanged); **+1 §4.6 ownership-rules bullet** (Pinset MR-lifetime contract + Why option (a)); **+1 §6.2 subparagraph** (Snapshot PMR-resource lifetime); §3.2 byte-faithful refresh; §8 row-2 + bullet-5 lifetime statements aligned. Touched sections: status block, header `Convergence log` line, §3.2 (byte-faithful refresh), §4.3 (`Pinset::Config::mr` contract comment), §4.6 (new ownership-rules bullet for Pinset MR lifetime), §6.2 (new "Snapshot PMR-resource lifetime" subparagraph), §8 (table row 2 lifetime column + bullet 5 closing clause), §9 (preamble seam count + new seam #18), §11 (Hand-off bullet 3 rewritten), Appendix C (this entry), Appendix D (header + full §D.3 rewrite). Same convergence path 2c, 2d, and 2e took at their round caps. **v0.4 post-verification surgical edit (2026-05-09):** user-requested independent verify of v0.4 — Codex pass (`codex_2g_4_tls_review.md`) found one P2 wording-mismatch (§4.3 field-comment paraphrased the lifetime contract while §8 row 2 + this Appendix C entry used the exact phrase "MUST outlive every `shared_ptr<const pin_snapshot>` the `Pinset` ever publishes"); §4.3 first sentence reworded verbatim to align all three sites (semantic contract unchanged). Opus independent fork verification (`opus_2g_4_tls_fork_verification.md`): 0 P1 / 0 P2 / 0 P3 — clean. Same surgical-residual-close shape 2f v1.5 used at its second fork verification.

---

## Appendix D — Drop-in amendments for sibling-doc text touched by this rewrite (NEW v0.2 / RC#3; D.1 byte-faithfulness refreshed v0.3 / round-2 Codex P1-1 / Opus P1-1; D.3 byte-faithfulness refreshed v0.4 / round-3 Codex P1-2 / Opus P1-2)

Per convergence rule 6 + the `[2c App D]` / `[2d App D]` / `[2e App D]` / `[2f App D]` sibling-doc-edit precedent, sibling-doc text touched by this rewrite is surfaced as drop-in amendment language for the orchestrator to apply at sign-off. The 2g rewrite agent does not edit `architecture.md`, `2d-threading.md`, or `library/spec/coverage-index.md` directly. Per `[const §VI.5]`, every reference uses the exact `[DocAbbrev §X.Y.Z] Title` form; review-internal IDs (e.g., "RC#3", "Codex P1-6", "Opus N-P1-1") are not carried into the sibling text.

### D.1 `[arch §5.6] Configuration shape` — name `SessionConfig::pinset` as the published reachability shape (RC#3 close; v0.3 byte-faithfulness fix per Codex round-2 P1-1 / Opus round-2 P1-1)

**Tension:** `[arch §5.6]` mentions "pinset rotation per `[const §XII]`" as the carve-out from the frozen-config rule but does not name the API/field the orchestrator and downstream 2h consume to reach the live `Pinset`. v0.1 of this doc split the reachability across three incompatible shapes (function parameter to `make_ssl_ctx_config`, concrete `file_cert_source::pinset()` accessor, deferred amendment) — RC#3. v0.2 picks `SessionConfig::pinset` and writes the amendment. v0.3 expands the "Before" block to the full bullet at `architecture.md` line 407 byte-faithfully so a strict-byte-match patch tool succeeds and the trailing dialect-overlay-categorical-rejection sentence is preserved across the orchestrator's apply step.

**Before** (current `architecture.md` v0.2 text, `[arch §5.6]` first bullet at line 407 — quoted verbatim from the source; whitespace, bolding, and ordering preserved):

> - **`SessionConfig` is value-typed and frozen at session open.** No mid-session reconfiguration of: dictionary, security profile, message store, executor, lock policy, dialect overlay. The supported pattern for any of these is close-and-reopen the session. Mutating ops on session-adjacent state that *do* admit mid-session change (e.g., pinset rotation per `[const §XII]`) go through their own APIs and are explicitly thread-aware. **Mid-session dialect-overlay swap is rejected categorically per `[2c §7.2]`** — there is no `Session::swap_dialect_overlay(...)` API in v1.0.

**After** (drop-in replacement — the new sentence is appended after the "thread-aware" sentence and BEFORE the "Mid-session dialect-overlay swap" sentence so the dialect-overlay-categorical-rejection clause is preserved at the bullet's end):

> - **`SessionConfig` is value-typed and frozen at session open.** No mid-session reconfiguration of: dictionary, security profile, message store, executor, lock policy, dialect overlay. The supported pattern for any of these is close-and-reopen the session. Mutating ops on session-adjacent state that *do* admit mid-session change (e.g., pinset rotation per `[const §XII]`) go through their own APIs and are explicitly thread-aware. The published reachability shape for the live `fixpp::tls::Pinset` is `[2d §4.5]` `SessionConfig::pinset` (a `std::shared_ptr<fixpp::tls::Pinset>` field; null permitted under `SecurityProfile::mtls_ca` / `one_way_ca` per `[2g §4.5.1]`); the abstract `fixpp::tls::cert_source` does NOT publish a `pinset()` accessor (kept credentials-only at 2 pure-virtual methods per `[const §XIV.2]`); downstream `tls/transport` (2h) reads `SessionConfig::pinset` directly without downcasting `cert_source*`. **Mid-session dialect-overlay swap is rejected categorically per `[2c §7.2]`** — there is no `Session::swap_dialect_overlay(...)` API in v1.0.

The orchestrator applies this edit at 2g sign-off; the amendment is recorded in `[architecture.md App C]` (or the equivalent sibling-doc convergence entry) as a cross-doc edit driven by 2g RC#3.

### D.2 `[2d §4.5] fixpp::session::SessionConfig — session-level frozen-at-open knobs` — append `pinset` field (RC#3 close)

**Tension:** `[2d §4.5]` v0.4 publishes `SessionConfig` with `cert_source` (line 535) and `security_profile` (line 437) but no `pinset` field. v0.1 of this doc relied on the field implicitly without queueing the amendment; RC#3 / N-P1-1 close requires the explicit field declaration.

**Before** (current `2d-threading.md` v0.4 text, `[2d §4.5]` plugin-overrides block — the existing two-line block as amended at 2e v0.4 sign-off; the new line is appended after `cert_source`):

```cpp
    // ── Plugin overrides (each null → inherit from EngineConfig) ────────
    std::unique_ptr<MessageStoreFactory>           store_factory;   // unique ownership per [arch §5.6] / [2e §4.4]
    std::shared_ptr<fixpp::tls::cert_source>       cert_source;
```

**After** (drop-in replacement — appends one new field after `cert_source`; the `store_factory` and `cert_source` lines are byte-faithful to the post-2e-sign-off baseline):

```cpp
    // ── Plugin overrides (each null → inherit from EngineConfig) ────────
    std::unique_ptr<MessageStoreFactory>           store_factory;   // unique ownership per [arch §5.6] / [2e §4.4]
    std::shared_ptr<fixpp::tls::cert_source>       cert_source;
    std::shared_ptr<fixpp::tls::Pinset>            pinset;          // mid-session-mutable per [arch §5.6] / [2g §4.3]; null permitted under SecurityProfile::mtls_ca / one_way_ca per [2g §4.5.1].
```

The diff is a single-line append at the end of the plugin-overrides block; column alignment matches the existing two-space gutter; the orchestrator's apply step is mechanical.

The orchestrator applies this edit at 2g sign-off; the amendment is recorded in `[2d-threading.md App C]` as a cross-doc edit driven by 2g RC#3 / Codex P1-6 / Opus N-P1-1.

### D.3 `library/spec/coverage-index.md` §"FIXS RC1" — update the Gap note column on the §3.4 / §4.1 / §4.4 rows from `MISSING → row added` to `covered by [2g §X.Y]` (RC#3 close; v0.4 / round-3 byte-faithfulness fix per Codex round-3 P1-2 / Opus round-3 P1-2)

**Tension:** `library/spec/coverage-index.md` carries the FIXS-row-to-doc-section mapping that Gate B linters and the `[const §VI.5]` exact-cite gate consume. v0.1 of this doc declared the rows in §11 prose without queueing the coverage-index drop-in. v0.2 / RC#3 wrote the drop-in but against a placeholder three-column schema (`FIXS section | Catalogue row(s) | Spec section(s)`). The live source's "FIXS RC1" table is **five-column** (`Section | Title | Normative? | Catalogue IDs | Gap note`) and already carries concrete rows for `§3.4 → T-039`, `§4.1 → T-040`, `§4.2 → T-040`, `§4.4 → T-041` with `MISSING → row added (T-XXX)` gap notes pre-filled (lines 155 / 159 / 162). v0.4 / round-3 P1-2 close: the drop-in is rewritten against the live five-column schema; the amendment is now an in-place update of the Gap note column on the three `MISSING → row added` rows so they read `covered by [2g §X.Y]` (the §3.4 / §4.1 / §4.4 rows). The §4.2 row (`covered by T-040`) is already in its post-amendment state and is preserved byte-faithfully (no edit). 2h's cross-cut section reference for §3.4 is left as the catalogue-ID-level cross-cut already encoded in §1's catalogue-ID column; the spec-section-level link to 2h's section number cannot be pre-named here and is the orchestrator's call at 2h sign-off (it does not block 2g sign-off because the catalogue-ID-level traceability is intact at the `T-039 → 2g §4.5 + 2h` cross-cut declared in 2g §A.2).

**Before** (current `library/spec/coverage-index.md` v0.2 text, the "FIXS RC1" section starting at the section header through the §4.4 row — quoted verbatim from lines 132–162 of the source, including the Appendix A row for closing context; whitespace, ordering, and column shape preserved):

```markdown
## FIXS RC1 (FIXS)

Section structure sourced from fixtrading.org/standards/fixs-online/ (v1.1 RC1, confirmed 2026-05-07).

| Section | Title | Normative? | Catalogue IDs | Gap note |
|---|---|---|---|---|
| §1 | Introduction | N | — | informative |
| §1.1 | Scope | N | T-002 | informative |
| §1.2 | An overview of TLS | N | — | informative background |
| §1.3 | Network topologies and perspectives | N | T-008, T-009, T-010 | informative; normative rules in §2 |
| §1.4 | When and where to use FIXS | N | T-002 | informative |
| §1.5 | References | N | — | bibliography |
| §2 | Authentication Methods | Y | T-008, T-009, T-010, T-011, T-012 | — |
| §2.1 | Recommended authentication and key exchange methods | Y | T-008, T-009 | — |
| §2.2 | Mutual and Simple TLS protocol options | Y | T-008, T-009, T-010 | — |
| §2.3 | Leaf Certificate Pinning | Y | T-008, T-011 | — |
| §2.4 | Certificate Validation with CA Pinning | Y | T-009 | — |
| §2.5 | Pre-shared keys (PSKs) | Y | T-012 | — |
| §2.6 | FIX authentication (FIXA) | Y | S-022 | FIXA details not yet public; S-022 covers Logon credentials |
| §3 | Protocol Parameters | Y | T-006, T-007, T-013 | — |
| §3.1 | Protocol version (TLS 1.2 / 1.3 only; prohibit TLS 1.0/1.1/SSL) | Y | T-006, T-007 | — |
| §3.2 | Protocol features (compression disabled, renegotiation disabled, session caching) | Y | T-006, T-007 | — |
| §3.3 | Cipher suites (AES-GCM, CHACHA20, ECDHE; prohibit RC4, DES, anon, MD5) | Y | T-013 | — |
| §3.4 | Certificate parameters (RSA 2048-bit min, ECDSA 256-bit, X.509, expiration) | Y | T-039 | MISSING → row added (T-039) |
| §3.5 | PSK properties (32-char min, out-of-band exchange, multiple simultaneous PSKs) | Y | T-012 | — |
| §3.6 | Application specific TLS (ALPN / SNI hooks) | Y | — | out-of-scope → dropped(post-1.0: ALPN/SNI application TLS) |
| §4 | Policies and Management | Y | T-011, T-012 | — |
| §4.1 | Sharing secrets (approved channels: HTTPS, GnuPG, PKCS#12, postal, in-person) | Y | T-040 | MISSING → row added (T-040) |
| §4.2 | Storing secrets (private keys, PSKs, pinned certs) | Y | T-040 | covered by T-040 |
| §4.3 | Renewing secrets (rotation support; multiple simultaneous during rotation) | Y | T-011 | — |
| §4.4 | Authorization linked to authentication (auth'd TLS identity ↔ FIX CompID) | Y | T-041 | MISSING → row added (T-041) |
```

**After** (drop-in replacement — three in-place row updates on the §3.4 / §4.1 / §4.4 rows; the Gap note column changes from `MISSING → row added (T-XXX)` to `covered by [2g §X.Y]`. Every other row, the section header, the introductory paragraph, and the column-header row are preserved byte-faithfully):

```markdown
## FIXS RC1 (FIXS)

Section structure sourced from fixtrading.org/standards/fixs-online/ (v1.1 RC1, confirmed 2026-05-07).

| Section | Title | Normative? | Catalogue IDs | Gap note |
|---|---|---|---|---|
| §1 | Introduction | N | — | informative |
| §1.1 | Scope | N | T-002 | informative |
| §1.2 | An overview of TLS | N | — | informative background |
| §1.3 | Network topologies and perspectives | N | T-008, T-009, T-010 | informative; normative rules in §2 |
| §1.4 | When and where to use FIXS | N | T-002 | informative |
| §1.5 | References | N | — | bibliography |
| §2 | Authentication Methods | Y | T-008, T-009, T-010, T-011, T-012 | — |
| §2.1 | Recommended authentication and key exchange methods | Y | T-008, T-009 | — |
| §2.2 | Mutual and Simple TLS protocol options | Y | T-008, T-009, T-010 | — |
| §2.3 | Leaf Certificate Pinning | Y | T-008, T-011 | — |
| §2.4 | Certificate Validation with CA Pinning | Y | T-009 | — |
| §2.5 | Pre-shared keys (PSKs) | Y | T-012 | — |
| §2.6 | FIX authentication (FIXA) | Y | S-022 | FIXA details not yet public; S-022 covers Logon credentials |
| §3 | Protocol Parameters | Y | T-006, T-007, T-013 | — |
| §3.1 | Protocol version (TLS 1.2 / 1.3 only; prohibit TLS 1.0/1.1/SSL) | Y | T-006, T-007 | — |
| §3.2 | Protocol features (compression disabled, renegotiation disabled, session caching) | Y | T-006, T-007 | — |
| §3.3 | Cipher suites (AES-GCM, CHACHA20, ECDHE; prohibit RC4, DES, anon, MD5) | Y | T-013 | — |
| §3.4 | Certificate parameters (RSA 2048-bit min, ECDSA 256-bit, X.509, expiration) | Y | T-039 | covered by `[2g §4.5]` verify_peer (cross-cut with 2h per `[2g §7.1]` / `[2g §A.2]`) |
| §3.5 | PSK properties (32-char min, out-of-band exchange, multiple simultaneous PSKs) | Y | T-012 | — |
| §3.6 | Application specific TLS (ALPN / SNI hooks) | Y | — | out-of-scope → dropped(post-1.0: ALPN/SNI application TLS) |
| §4 | Policies and Management | Y | T-011, T-012 | — |
| §4.1 | Sharing secrets (approved channels: HTTPS, GnuPG, PKCS#12, postal, in-person) | Y | T-040 | covered by `[2g §4.1]` cert_source::load_credentials + `[2g §4.2]` file_cert_source |
| §4.2 | Storing secrets (private keys, PSKs, pinned certs) | Y | T-040 | covered by T-040 |
| §4.3 | Renewing secrets (rotation support; multiple simultaneous during rotation) | Y | T-011 | — |
| §4.4 | Authorization linked to authentication (auth'd TLS identity ↔ FIX CompID) | Y | T-041 | covered by `[2g §4.5]` peer_identity (cross-cut with session-module Phase-4 per `[2g §7.2]` / `[2g §A.2]`) |
```

The diff is a three-row in-place edit on the §3.4 / §4.1 / §4.4 rows (Gap note column changes); the §4.2 row is unchanged (already `covered by T-040`); the section header, the introductory paragraph, the column-header row, and every other table row are preserved byte-for-byte from the live source. The orchestrator's apply step is a mechanical three-line replacement.

Per `[arch Appendix B]` precedent, the spec-section-level link to 2h's section number (for the T-039 cross-cut wiring half) is intentionally NOT named in the Gap note column; the catalogue-ID-level cross-cut is preserved by the existing `T-039` entry in the Catalogue IDs column and is fully traced to 2g + 2h via 2g §A.2 (Owned cross-cuts) — the Gap note column points at 2g's policy-core half only, and the orchestrator may add the 2h reference at 2h sign-off without touching this drop-in.

The orchestrator applies this edit at 2g sign-off; the amendment is recorded in `[library/spec/coverage-index.md]`'s versioning header (or the equivalent change-log location) as a cross-doc edit driven by 2g RC#3 / §11.

## Appendix Z — post-sign-off amendment, 2026-08-29

*Appended at the END of the file on purpose. This document is cited by line number from elsewhere in
the tree; an insertion higher up silently rots every one of those citations. Every edit made by this
amendment above is an **in-place, same-line-count** replacement for exactly that reason.*

### What was deleted, and why deletion rather than a refresh

`§3` reproduced **`[const §XII.5]` verbatim**, declaring the quote normative *"because the enum
signature in §4.5 is normative"*. **Article XII §5 was amended by constitution v0.3 (2026-06-17,
folded into feature 043)**, which appended a fourth profile — `insecure_plain_tcp`, no TLS at all,
opt-in. The reproduction became false without anyone editing this file.

The block is **deleted, not refreshed**. Refreshing it would restore a copy with no link back to the
article, which is precisely the mechanism that failed — it would re-arm the trap for the next
amendment. `[const §XII.5]` is one hop away and cannot go stale relative to itself.

### The hazard is not confined to §XII.5

This document reproduces **`[const §XII.3]`, `[const §XII.5]`, `[const §XII.6]`, `[const §XII.8]`**
verbatim, and additionally quotes `architecture.md` and `spec/coverage-index.md`. **Six copies of
governing text.** One of them demonstrably rotted.

> ⚠️ **The other three articles are UNVERIFIED, not verified-clean.** One article was checked, not
> four. Saying "the rest look fine" would be a coverage claim nobody measured — the failure this
> repository keeps re-encountering. Check them against `.specify/constitution.md` before citing.

### Also corrected in place

The namespace: this document said **`fixpp::tls::SecurityProfile`** at three sites. The shipped
declaration is `namespace fixpp::session` (`include/fixpp/session/security_profile.hpp`).

### What was deliberately left alone

The **§4.5 enum block** and the **§11 verification item** are *design records* — they state what 2g
specified at sign-off, which is their whole value. They carry supersession markers and are **not**
rewritten; editing them would destroy the account of what was believed when. The header
`security_profile.hpp` names its own amendment in a comment, which is the convention that made this
finding reachable at all.

### Cross-reference — Appendix D drop-in blocks (added 2026-08-29)

This document's `Before`/`After` drop-in blocks describe **sibling** documents and are not re-checked
by anything. Classify them rather than trusting them; no state is recorded here, because a recorded
state rots:

```bash
python3 tools/check_dropin_blocks.py --self-test
python3 tools/check_dropin_blocks.py --suspect
```

`APPLIED` + prose calling the stale half *"current"*, and `NEITHER` (target in a **third** state), are
the two combinations that are defects on their own. Both are **leads** — the matcher is
substring-based; confirm against the target and the shipped header.

