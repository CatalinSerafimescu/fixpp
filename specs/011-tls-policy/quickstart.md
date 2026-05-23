# Quickstart — 011-tls-policy

**Date**: 2026-05-23
**Plan**: [plan.md](plan.md)
**Spec**: [spec.md](spec.md)
**Design anchor**: `.specify/2g-tls.md` v0.4 §11 (Hand-off).

This is the **operator quickstart** for the TLS-policy core. It shows the canonical wiring shapes for the three user stories (P1 rotation, P2 plug a custom credential source, P3 hardened-by-default). After this slice ships in `main`, the same content lands at `docs/src/tls-quickstart.md` (mdBook).

The wire layer — actually opening the TLS socket, building the `SSL_CTX`, and wiring the `SSL_VERIFY_PEER` callback — is **2h-transport's** responsibility. This feature publishes the **policy core** that 2h consumes; until 2h ships, the snippets below are *what 2h will pass through verbatim once it lands*.

---

## Prerequisites

- fixpp ≥ v1.0 with the 2g surface (this feature) and 2h-transport (the next feature) shipped.
- An OpenSSL 3.x runtime on Linux or Windows.
- PEM/DER cert + private key on disk (or your HSM/KMS endpoint).

---

## Scenario A — CA-trust mTLS, file-based cert source (the default starting point)

```cpp
#include <fixpp/tls/cert_source.hpp>
#include <fixpp/tls/file_cert_source.hpp>
#include <fixpp/tls/security_profile.hpp>
#include <fixpp/core/clock.hpp>
#include <memory_resource>

using namespace fixpp;

// 1. Wire the file-based cert source.
auto pmr = std::pmr::new_delete_resource();

// file_cert_source::Config per [2g §4.2] (the abstract base cert_source does
// NOT publish Config; file_cert_source::Config is impl-specific per data-model
// E-1 / E-1a — round-2 P1-1 propagation close).
tls::file_cert_source::Config cs_cfg{
    .leaf_path          = "/etc/fixpp/tls/our_leaf.pem",
    .chain_path         = "/etc/fixpp/tls/our_chain.pem",
    .private_key_path   = "/etc/fixpp/tls/our_leaf_key.pem",  // [2g §4.2] line 319 — required field for mTLS local-cert signing.
    .ca_bundle_path     = "/etc/fixpp/tls/counterparty_ca_bundle.pem",
    .password_cb        = nullptr,    // unencrypted PEM; field name per [2g §4.2] line 320 / data-model E-1a.
    .max_rsa_key_bits   = 8192,
    .max_cert_der_bytes = 16 * 1024,
    .max_san_entries    = 64,
    .max_chain_depth    = 8,
};

auto cs = tls::make_file_cert_source(cs_cfg, pmr);
if (!cs) {
    // tls_cert_load_failed: file missing / unreadable / malformed PEM / wrong password (per [2g §6.6])
    log_fatal("cert source load failed", cs.error());
    return EXIT_FAILURE;
}

// 2. Build the SSL_CTX config. No Pinset under mtls_ca → null pinset is OK.
auto clock = std::make_shared<core::system_clock_source>();

// Parameter order per [2g §4.5] lines 681-686 (NEW-P1-2 close):
// (profile, cs, clock, pinset = nullptr, mr = nullptr).
auto ssl_cfg = tls::make_ssl_ctx_config(
    tls::SecurityProfile::mtls_ca,
    cs.value(),
    clock,
    /*pinset=*/ nullptr);

if (!ssl_cfg) {
    log_fatal("SSL ctx config rejected", ssl_cfg.error());
    return EXIT_FAILURE;
}

// 3. Hand off to 2h-transport (post-this-feature). For now: store in EngineConfig.
engine_cfg.default_ssl_ctx_config = std::move(ssl_cfg.value());
```

---

## Scenario B — FIXS-RC1 leaf-pinning + per-counterparty rotation (the recommended FIXS profile)

```cpp
#include <fixpp/tls/pinset.hpp>

// 1. Obtain your counterparty's current leaf certificate (out-of-band).
//    Pinset::add consumes the Certificate for its SHA-256 + subject_dn + SAN
//    list at add() time per [2g §4.3] line 487 (Codex P1-2 close).
tls::Certificate counterparty_X_current_leaf = /* parsed Certificate value */;
auto kCounterpartyXOldSha256 = counterparty_X_current_leaf.sha256();

// 2. Build a Pinset for counterparty X (per-counterparty granularity per
//    FR-009a / Clarify Q5).
auto cp_x_pins = std::make_shared<tls::Pinset>();
if (auto r = cp_x_pins->add(counterparty_X_current_leaf); !r) {
    log_fatal("pin add failed", r.error());
    return EXIT_FAILURE;
}

// 3. Build the SSL_CTX config with mtls_pinned + the populated Pinset.
//    Parameter order per [2g §4.5] lines 681-686 (NEW-P1-2 close):
//    (profile, cs, clock, pinset = nullptr, mr = nullptr).
auto ssl_cfg = tls::make_ssl_ctx_config(
    tls::SecurityProfile::mtls_pinned,
    cs.value(),
    clock,
    cp_x_pins);   // non-null AND non-empty (else tls_pin_empty_at_open per Clarify Q2)

if (!ssl_cfg) {
    // E.g. tls_pin_empty_at_open if the pinset was empty when we got here.
    log_fatal("SSL ctx config rejected", ssl_cfg.error());
    return EXIT_FAILURE;
}

// 4. Many SessionConfigs against counterparty X share the SAME pinset
//    (per-counterparty pattern). One rotation API call → atomic visibility
//    on the next handshake of every same-counterparty session.
SessionConfig session_a{ .counterparty_id = "X", .pinset = cp_x_pins, .ssl_ctx_config = ssl_cfg.value() };
SessionConfig session_b{ .counterparty_id = "X", .pinset = cp_x_pins, .ssl_ctx_config = ssl_cfg.value() };

// 5. Rotation: counterparty X announces a new cert; we add it first, then
//    remove the old one AFTER the counterparty cuts over. In-flight handshakes
//    use whichever snapshot was current when their find() ran (no torn read).
//
//    Note: add+remove are SEPARATE calls; there is no atomic-swap shortcut.
//    add() takes a Certificate (consumed for diagnostic fields);
//    remove() takes a SHA-256 fingerprint (32 bytes).
tls::Certificate counterparty_X_new_leaf = /* parsed Certificate value */;
cp_x_pins->add(counterparty_X_new_leaf);
// ... counterparty cuts over to the new cert ...
cp_x_pins->remove(kCounterpartyXOldSha256);
```

---

## Scenario C — Plug an HSM-backed credential source (offload signing off the session strand)

```cpp
#include <fixpp/tls/cert_source.hpp>

class hsm_cert_source final : public tls::cert_source {
 public:
    explicit hsm_cert_source(my_hsm_handle hsm, asio::any_io_executor signing_executor)
      : hsm_(std::move(hsm))
      , signing_exec_(std::move(signing_executor)) {}

    asio::awaitable<expected_t<tls::local_credentials>>
    load_credentials() override {
        // Cold-path: read the cert chain from HSM-attached storage (or
        // wherever your HSM puts the cert metadata). May allocate.
        // Honour cancellation per the [2g §6.4] / FR-021 recipe verbatim
        // (the recipe body shape is published in contracts/cert_source.hpp).

        auto exec = co_await asio::this_coro::executor;
        auto cs   = co_await asio::this_coro::cancellation_state;

        // Reap pre-I/O cancellation (recipe step 3 — the load-bearing
        // "between-call-and-first-suspension" reap).
        if (cs.cancelled() != asio::cancellation_type::none) {
            co_return core::expected_t<tls::local_credentials>{
                unexpect, error::tls_load_cancelled };
        }

        // local_credentials::leaf is a value Certificate; ::chain is a
        // std::span<const Certificate> VIEW into impl-owned storage per
        // [2g §4.1] lines 251-255 (Codex P1-1 sub-finding 3 close).
        tls::local_credentials creds;
        creds.leaf  = parsed_leaf_;                                  // impl-owned Certificate value
        creds.chain = std::span<const tls::Certificate>{chain_};     // view into impl-owned vector

        // The signer is async — signing operations will run via
        // cancellable_dispatch on signing_exec_, NOT on the session strand
        // (per [2d §6.5] / [2d §7.5]). Per [2g §4.1] the async_signer_ref
        // carries `sign_fn`, an awaitable callable that the impl supplies.
        creds.signer = tls::async_signer_ref{
            .sign = [this](tls::sign_request const& req)
                        -> asio::awaitable<core::expected_t<tls::sign_response>> {
                // Hop to signing_exec_ via cancellable_dispatch per [2d §6.5].
                co_return co_await this->sign_on_hsm(req);
            },
        };

        co_return creds;
    }

    core::expected_t<std::span<const tls::Certificate>>
    load_trust_anchors() override {
        return std::span<const tls::Certificate>{trust_anchors_};   // populated at construction
    }

 private:
    my_hsm_handle                       hsm_;
    asio::any_io_executor               signing_exec_;
    tls::Certificate                    parsed_leaf_;
    std::vector<tls::Certificate>       chain_;
    std::vector<tls::Certificate>       trust_anchors_;
};

// Wire it:
auto signing_ctx = asio::thread_pool{ /* 2 threads */ };
auto cs = std::make_shared<hsm_cert_source>(hsm_open("/dev/hsm0"), signing_ctx.get_executor());

// Parameter order per [2g §4.5] lines 681-686 (NEW-P1-2 close):
// (profile, cs, clock, pinset = nullptr, mr = nullptr).
auto ssl_cfg = tls::make_ssl_ctx_config(
    tls::SecurityProfile::mtls_ca,
    cs,
    clock);

// During handshake, OpenSSL invokes the signing callback on signing_exec_ —
// not on the session strand. The session strand stays available to dispatch
// other coroutines while the HSM call is in flight.
```

---

## Scenario D — Compile-time refusal of a non-allowed cipher (build-time fail-loud)

```cpp
// In a build configuration patch:
//   constexpr std::array<std::string_view, 1> rogue_suites = {{
//       "ECDHE-RSA-AES128-CBC-SHA",   // CBC + SHA-1 — both banned per [const §XII.4]
//   }};
//   // ... wire rogue_suites into CipherPolicy::tls12_suites ...

// Build will fail at compile time:
//
//   error: static assertion failed: TLS 1.2 suite list contains a banned token.
//     static_assert(!contains_any(tls12_suites, banned_tokens),
//                   "TLS 1.2 suite list contains a banned token.");
//
// The build CANNOT be produced. No shippable binary is possible with a
// non-allowed cipher — FR-011 / SC-003.
```

---

## Scenario E — Runtime cipher-string predicate (the 2i C-ABI bridge path)

```cpp
#include <fixpp/tls/cipher_policy.hpp>

// 2i's C-ABI bridge consults this when an opaque cipher string crosses the
// language boundary. Substring matching against banned_tokens catches
// CBC / SHA-1 / RSA-1024 / TLS-1.0/1.1 / etc.

static_assert( tls::CipherPolicy::is_allowed("ECDHE-ECDSA-AES128-GCM-SHA256") == true );
static_assert( tls::CipherPolicy::is_allowed("TLS_AES_128_GCM_SHA256")        == true );
static_assert( tls::CipherPolicy::is_allowed("RC4-MD5")                       == false );
static_assert( tls::CipherPolicy::is_allowed("ECDHE-RSA-AES128-CBC-SHA")      == false );
```

---

## What this feature does NOT do (cross-cuts you'll find elsewhere)

- **Build the `SSL_CTX`** — 2h-transport.
- **Run the TLS handshake** — 2h-transport.
- **Wire the `SSL_VERIFY_PEER` callback** — 2h-transport (it calls `verify_peer` from this feature).
- **Expose the C ABI** — 2i-capi (it bridges into `make_file_cert_source`, `Pinset::add/remove/find`, `make_ssl_ctx_config`, and the `FIXPP_ERR_TLS_*` codes published here).
- **Bind CompID ↔ TLS identity** — the session-FSM Phase-4 feature (it consumes `peer_identity` from `verify_peer`).
- **Trigger a runtime cert reload** — 2j-controlplane (the API contract is "construct a new `cert_source` and start a new session"; mid-session reload is post-v1).
- **Record TLS events to OTel / logs** — 2k-log-otel (it picks up the named `error::tls_*` variants and the success-path `peer_identity` for span tags).

---

## Next steps for the operator

1. Generate / fetch counterparty cert fingerprints out-of-band (`openssl x509 -in <leaf>.pem -outform DER | sha256sum`).
2. Pick a `SecurityProfile`: `mtls_pinned` for FIXS-RC1 conformance, `mtls_ca` for CA-trust deployments, `one_way_ca` only if forced by a legacy counterparty.
3. If using HSM/KMS/vault, implement `cert_source` against your provider (Scenario C); bind its signing executor to a thread pool distinct from the session executor.
4. Wire one `Pinset` per counterparty (Scenario B); share its `shared_ptr` across all `SessionConfig`s targeting that counterparty.
5. After 2h-transport ships, the same `SslCtxConfig` you built here is what 2h's `asio_tls_transport` consumes.

---

## Reference

- Design doc: [`.specify/2g-tls.md`](../../.specify/2g-tls.md) v0.4 (Gate A r3 converged 2026-05-09)
- Spec: [`spec.md`](spec.md)
- Plan: [`plan.md`](plan.md)
- Research: [`research.md`](research.md)
- Data model: [`data-model.md`](data-model.md)
- Contracts: [`contracts/`](contracts/)
- Constitution: [`.specify/constitution.md`](../../.specify/constitution.md) Article XII (Security & TLS)
- Architecture: [`.specify/architecture.md`](../../.specify/architecture.md) §4.6 (`tls/` surface), §5.6 (mid-session-mutable carve-out) (Codex P3-2 close)
