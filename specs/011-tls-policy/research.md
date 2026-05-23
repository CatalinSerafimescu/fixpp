# Phase 0 Research — 011-tls-policy

**Date**: 2026-05-23
**Plan**: [plan.md](plan.md)
**Spec**: [spec.md](spec.md)

## Inheritance posture

**No NEEDS CLARIFICATION items remain.** Every design decision in this feature is settled by a signed-off upstream — `.specify/2g-tls.md` v0.4 (Phase-2 Gate A round 3 converged 2026-05-09) for the design space, plus the 5 `/speckit-clarify` resolutions recorded in `spec.md ## Clarifications ### Session 2026-05-23` for the contract surface choices the design doc deliberately left open for `/clarify`.

Phase 0 records the binding decisions here, anchored to the upstream — **not** re-derived. The orchestrator (Phase-4 Gate A) consults this file to confirm the design space is closed before unblocking `/speckit-tasks`.

## D-1 — TLS provider: OpenSSL, both Linux and Windows

- **Decision**: OpenSSL is the v1.0 TLS provider on both Linux and Windows. No Schannel, no platform-native fallback.
- **Rationale**: `[const §XII.1]` (locked 2026-05-06). Single TLS-implementation surface keeps the security-spine reviewable against a single set of upstream CVEs.
- **Alternatives considered**: Schannel (rejected 2026-05-06; complicates code path divergence Linux/Windows); BoringSSL (rejected — incompatible API surface relative to OpenSSL 3.x; non-standard release cadence).
- **Anchor**: `[const §XII.1]`, `[2g §2]`.

## D-2 — TLS version posture: TLS 1.3 preferred, TLS 1.2 ECDHE-AEAD as fallback

- **Decision**: `SSL_CTX_set_min_proto_version = TLS1_2_VERSION`, `SSL_CTX_set_max_proto_version = TLS1_3_VERSION`. TLS 1.3-capable counterparties always negotiate 1.3; 1.2-only counterparties remain interop-viable. TLS 1.0 / 1.1 / SSL are compile-time-refused via `static_assert` in `CipherPolicy`.
- **Rationale**: matches design-doc `[arch §4.6]` catalogue row T-007 ("TLS 1.3 support — preferred; session caching optional") and keeps v1.0 interop-viable against counterparties not yet on 1.3 — the common reality on FIX networks today. Operator-visible.
- **Alternatives considered**: TLS 1.3-only (rejected via `/clarify` Q1 Option B — maximum hardening, minimum interop; ruled out because forcing every counterparty to upgrade before fixpp ships is impractical). No-preference, OpenSSL default ordering (rejected via `/clarify` Q1 Option C — outcome is the same in practice but the spec does not enforce the preference, which makes the SSL_CTX-mapping-table contract loose).
- **Anchor**: `[const §XII.2]`, `[const §XII.3]`, `[2g §4.5]`, `spec.md FR-014`, `spec.md ## Clarifications` Q1.

## D-3 — `mtls_pinned` empty-pinset bootstrap: fail-EARLY at session-open

- **Decision**: `make_ssl_ctx_config(mtls_pinned, empty_pinset, ...)` returns `unexpected{tls_pin_empty_at_open}` (new distinct error variant). The session never opens. Distinct from `tls_cert_pin_mismatch` so operator logs separate "fixpp-config problem" from "peer-cert problem".
- **Rationale**: FIXS-RC1 §5 leaf-cert pinning is genuinely new to FIX operators — none of QuickFIX-cpp / QuickFIX-J / Fix8 implement leaf-cert pinning at all (sweep performed 2026-05-23, see `spec.md ## Clarifications` Q2). Operators migrating from those engines will be learning a new model; a distinct config-time error short-circuits the diagnostic confusion that would otherwise materialise as per-handshake `tls_pin_mismatch` logs hunting for the wrong cause.
- **Alternatives considered**: fail-LATE at every handshake via `tls_pin_mismatch` (rejected via `/clarify` Q2 Option B — natural outcome by construction, no new variant needed, but looks like a peer-cert problem in logs); WARN-only at session-open + fail-LATE at handshake (rejected via `/clarify` Q2 Option C — adds an observability hook with no hard refusal; not in keeping with the security-first posture).
- **Cost**: one new `error::tls_*` variant (`tls_pin_empty_at_open`); one config-time validation branch in `make_ssl_ctx_config`.
- **Anchor**: `[2g §4.5]` line 115, `spec.md FR-025`, `spec.md US3 AC5`, `spec.md ## Clarifications` Q2.

## D-4 — `verify_peer` multi-violation ordering: short-circuit first hit

- **Decision**: `verify_peer` returns on the first violation hit in the documented evaluation order: (1) per-cert DER envelope size → (2) RSA key bounds (lower 2048, then upper `max_rsa_key_bits`) → (3) ECDSA curve membership (P-256/P-384) → (4) chain depth → (5) SAN cardinality → (6) X.509 version (v2/v3 only) → (7) expiration against `effective_clock` → (8) `CipherPolicy::is_allowed` on the negotiated suite. One `error::tls_*` variant per failed handshake; no aggregate report.
- **Rationale**: matches OpenSSL's own `X509_V_ERR_*` norm (one error per verify pass); keeps the 2i C-ABI projection flat (no list-of-codes type to bridge); the multi-violation case is rare in practice and the *first* violation is usually the actionable one. Reference-engine sweep (2026-05-23) found none of QuickFIX-cpp / QuickFIX-J / Fix8 ship analogous DoS-bound surfaces — fixpp is establishing the diagnostic shape.
- **Alternatives considered**: aggregate-report `tls_handshake_failed{ violations: span<sub_reason> }` (rejected via `/clarify` Q3 Option B — requires a sub-reason list type that crosses the C ABI; richer telemetry but heavier surface); short-circuit + diagnostic ring-buffer recorded out-of-band (rejected via `/clarify` Q3 Option C — adds a cross-cut into 2k-log-otel that this feature would have to coordinate; better as a 2k feature when 2k spec lands).
- **Anchor**: `[2g §6.6]`, `spec.md FR-020a`, `spec.md ## Clarifications` Q3.

## D-5 — Pin lookup key: SHA-256-of-leaf-DER

- **Decision**: `Pinset::add(fp)` / `remove(fp)` / `find(fp)` all key on the SHA-256 fingerprint of the leaf certificate's DER encoding, stored as `std::array<std::byte, 32>`. Pin rotation involves the operator computing SHA-256(new_leaf_DER) and calling `add(new_fp)` then `remove(old_fp)`.
- **Rationale**: matches FIXS-RC1 §5 (the binding standard for this feature — `[FIXS §2.3]` Leaf Certificate Pinning + `[FIXS RC1 §5]` Certificate pinning and rotation) and the design-doc `[2g §4.5]` line 115 statement "peer cert SHA-256 MUST appear in the live Pinset's captured snapshot". Cheap to compute (one SHA-256 over ≤ 16 KiB) and cheap to compare (32 bytes). Stable under "rotate per renewal" operator pattern; intentionally changes on cert re-issue (the entire point of rotation).
- **Alternatives considered**: SHA-256 of Subject Public Key Info / SPKI (rejected via `/clarify` Q4 Option B — survives cert re-issuance with same keypair, but most FIXS operators rotate the keypair on every renewal anyway, so the SPKI advantage doesn't materialise; also non-standard for FIXS deployments); Subject Distinguished Name match (rejected via `/clarify` Q4 Option C — weakest; lets any CA-compromised peer forge a same-DN cert; security-disqualified).
- **Anchor**: `[FIXS §2.3]`, `[FIXS RC1 §5]`, `[2g §4.5]` line 115, `spec.md FR-006`, `spec.md FR-008`, `spec.md ## Clarifications` Q4.

## D-6 — Pinset granularity: per-counterparty recommended

- **Decision**: one `Pinset` instance per counterparty is the **recommended** operator pattern; the same `std::shared_ptr<Pinset>` is shared across all `SessionConfig`s targeting that counterparty so rotation through the API applies atomically to every same-counterparty session on the next handshake. The engine does NOT enforce granularity (Pinset is held by `shared_ptr`, so the operator chooses); per-session granularity is permitted (stronger isolation, more API ceremony); engine-wide sharing across DIFFERENT counterparties is documented as discouraged because it conflates trust authorities (a pin meant for `X` would be live for `Y`).
- **Rationale**: FIXS-RC1 §5 rotation semantics are per-counterparty by definition. The design-doc `[2g §1.1]` sizes `max_pins = 16` explicitly to absorb "multiple counterparties sharing a pinset" — i.e., the design admits sharing — but the recommended model anchors per-counterparty for clean rotation atomicity.
- **Alternatives considered**: per-session as the recommendation (rejected via `/clarify` Q5 Option B — strongest isolation but more API ceremony; doesn't match the FIXS RC1 §5 per-counterparty rotation model); silent on granularity (rejected via `/clarify` Q5 Option C — leaves operators without a canonical pattern; design-doc and tests would then have no anchor for cross-session rotation behaviour).
- **Anchor**: `[2g §1.1]`, `[2g §4.3]`, `[FIXS RC1 §5]`, `spec.md FR-009a`, `spec.md US1 Independent Test`, `spec.md ## Clarifications` Q5.

## D-7 — `cert_source` pure-virtual surface: 2 methods (well under the 5-cap)

- **Decision**: `cert_source` exposes 2 pure-virtual methods:
  - `virtual asio::awaitable<expected_t<local_credentials>> load_credentials() = 0;`
  - `virtual std::span<const Certificate> load_trust_anchors() const = 0;`
  Both methods are described in the contract shape oracle `contracts/cert_source.hpp`. v0.2 of the design doc collapsed the v0.1 three-method surface (`load_leaf` + `load_chain` + synchronous `sign(...)`) into the single awaitable `load_credentials` whose return carries the leaf + chain + signer variant.
- **Rationale**: `[const §XIV.2]` caps pluggable interfaces at ≤ 5 pure-virtuals; ships at 2 with a wide margin so HSM/KMS/vault/in-memory user-side impls remain implementable as a single class. The awaitable shape (vs sync) is required by `[const §XI.2]` + `[2d §7.5]` so HSM signing can offload off the session strand.
- **Alternatives considered**: separate sync `load_leaf` + `load_chain` + `sign_callback` (rejected at design-doc round 1 — couples I/O to the API caller; can't offload HSM signing); single awaitable returning just the leaf + chain with a separate `signer()` accessor (rejected — splits the credential into two awaits, complicates cancellation).
- **Anchor**: `[2g §4.1]`, `[const §XIV.2]`, `[2d §7.5]`, `spec.md FR-001`, `spec.md FR-002`, `spec.md FR-003`.

## D-8 — Pinset writer mutex: `std::shared_mutex` (NOT `std::mutex`, NOT `fixpp::sync::async_mutex`)

- **Decision**: `Pinset::add` / `remove` serialise via `std::shared_mutex`; reads via `atomic<shared_ptr<const pin_snapshot>>::load(std::memory_order_acquire)`. The mutex is held only during publication of the new snapshot; readers never block.
- **Rationale**: rotation is a fully **synchronous** operation called from the operator's control-plane thread (NOT a coroutine). `[const §XV.9]` bans `std::mutex` in **coroutine context** — not in synchronous context — so the constitutional ban does not apply here. `fixpp::sync::async_mutex` would be wrong because the writer needs synchronous serialisation, not awaitable suspension. `[2g §6.5]` is the binding design-doc rationale and is Gate-A-signed-off.
- **Alternatives considered**: `std::mutex` (rejected — `shared_mutex` is more honest about the reader/writer asymmetry, but `std::mutex` would also work since readers don't take the lock; `shared_mutex` is a marginal future-proofing choice); `fixpp::sync::async_mutex` (rejected — wrong tool; introduces a coroutine suspension that has no purpose here).
- **Anchor**: `[2g §6.5]`, `[const §XI.3]` (rationale-respected), `[const §XV.9]` (rationale-respected — not violated; ban is coroutine-context only).

## D-9 — `verify_peer` clock source: session-scoped `effective_clock`

- **Decision**: cert expiration is evaluated against `cfg.clock->now()` where `cfg.clock` is the session-scoped `effective_clock` per `[2d §7.9]` (resolved as `SessionConfig::clock_override ?: EngineConfig::clock`), NOT wall-clock and NOT `EngineConfig::clock` directly. The 2h-transport feature passes the resolved `effective_clock` into `make_ssl_ctx_config` at session open; 2g consumes it via the `SslCtxConfig::clock` field.
- **Rationale**: handshake validation is deterministic per-session even when a session overrides the clock (e.g., test fixtures using `fixpp::core::mock_clock`). `[2g §6]` (v0.2 / RC#2 close) routes through `effective_clock` by API construction.
- **Anchor**: `[2d §7.9]`, `[2g §6]` paragraph at line 1042, `spec.md FR-020`.

## D-10 — DoS bound defaults (v1.0)

| Bound | Default | Operator-raisable? | Error variant on violation |
|---|---|---|---|
| `max_rsa_key_bits` | 8192 | Yes (`cert_source::Config::max_rsa_key_bits`) | `tls_rsa_key_too_large` (upper); `tls_rsa_key_too_small` (lower, fixed at 2048 per `[FIXS §3.4]`) |
| `max_cert_der_bytes` | 16 KiB | Yes | `tls_cert_der_too_large` |
| `max_san_entries` | 64 | Yes | `tls_san_entries_exceeded` |
| `max_chain_depth` | 8 | Yes (matches OpenSSL default) | `tls_cert_chain_too_deep` |
| `Pinset::Config::max_pins` | 16 | Yes | `add()` returns rejection (operator pin-overflow) |

- **Rationale**: all five defaults are `[2g §1.1]` values, sized against real-world FIX deployment data and DoS-cost arithmetic. The RSA upper-cap absorbs the `BN_mod_exp` super-linear cost (Cloudflare 2014 / OpenSSL CVE-2022-0778); the DER cap rejects 10-MiB-cert DoS before parse; the SAN cap bounds `peer_identity` allocation; the chain depth matches OpenSSL's own default; the 16-pin cap admits operational drift across a small set of counterparties on one Pinset.
- **Anchor**: `[2g §1.1]`, `spec.md FR-019`, `spec.md FR-020`.

## D-11 — Lifetime annotation site: abstract base declaration

- **Decision**: `[[clang::lifetimebound]]` is applied at the **declaration site of the abstract base** for every accessor returning a non-owning view. The override at the concrete impl does NOT re-declare the attribute (it inherits via the override mechanism + the `[2b §6.4]` precedent that v0.2 RC#1 close codified).
- **Rationale**: `[arch §5.5]` mandates declaration-site lifetime annotation; `[2b §6.4]` precedent ratified by 2g v0.2 RC#1 close ("the v0.1 'annotate only the override' workaround is retired"). Applies to `Certificate::subject_dn()`, `Certificate::san_dns_names()`, `pin_view::fingerprint()`, the inner span of `load_credentials`'s awaitable result, etc.
- **Anchor**: `[arch §5.5]`, `[2b §6.4]`, `[2g §1]` goal 7, `spec.md FR-017`.

## D-12 — Allocation envelope

- **Decision**: hot-path Pinset reads are zero-allocation per `[const §VIII.5]` (verified via `tests/perf/test_tls_alloc_guard.cpp` + mallocnesia LD_PRELOAD dual-gate per `[[feedback_tracking_pmr_resource_false_pass]]`). Cold-path `file_cert_source::load_credentials` is permitted to allocate PMR (file I/O + parse + chain build). PMR throws route through `[2a §4.2]` `trap_throw` and surface as `error::tls_load_failed`.
- **Rationale**: matches `[const §VIII.5]` zero-alloc steady-state envelope and `[arch §5.3]` exception-free session-handling window. Mallocnesia is mandatory because `counting_resource` alone misses non-PMR `std::vector` escapes per the 008 burn recorded in `[[feedback_tracking_pmr_resource_false_pass]]`.
- **Anchor**: `[const §VIII.5]`, `[arch §5.3]`, `[2a §4.2]`, `spec.md FR-007`, `spec.md FR-023`, `spec.md FR-024`.

## D-13 — Cancellation contract on `cert_source::load_credentials`

- **Decision**: cancellation slot is read inside the coroutine before any work dispatch; the recipe from `[2d §6.5]` `cancellable_dispatch` is lifted verbatim. Awaitable completes with `expected_t::unexpected{tls_load_cancelled}`; no thrown exception escapes the public surface.
- **Rationale**: `[const §XI.2]` mandates ASIO native cancellation slots; the `[2d §6.5]` recipe is the established project pattern (used by 005 / 008 / 010). The test seam `tests/tls/test_load_credentials_cancellation.cpp` enforces the recipe via the `[2g §9 seam #13]` cancellation contract test.
- **Anchor**: `[const §XI.2]`, `[SYN §3.2 Q6a]`, `[2d §6.5]`, `[2g §9 seam #13]`, `spec.md FR-021`.

## Outstanding / Deferred

None for this slice. Cross-cuts T-039 / T-040 (with 2h-transport) and T-041 (into session/) are owned partially here (the validation predicate; the cert_source-consumes-secrets interface; the `peer_identity` value), and the remaining slices ship in 2h-transport and the session/ Phase-4 module respectively. Both cross-cuts are documented in `spec.md FR-026` and the design doc, with the boundary explicit.

The Codecov-vs-lcov-DA/BRDA gate (`[[feedback_codecov_patch_vs_lcov_da_brda_gate]]`) is procedurally anticipated as a possible YELLOW carve-out at `/speckit-verify` time for `file_cert_source.cpp` I/O fault paths + Windows-only arms that cannot be reached without filesystem-fault hook injection (precedent: PR #73, #74, #77, #82). Recorded as a foreseeable verify-time waiver candidate; not a Gate-A blocker.

## Suggested next step

`/gate-a 011-tls-policy` (Phase-4 feature Gate A; reviews this `specs/011-tls-policy/` bundle — spec.md + plan.md + research.md + data-model.md + contracts/ + quickstart.md — against `[const §XVII.1]`).
