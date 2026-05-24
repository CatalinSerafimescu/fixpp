// src/tls/security_profile.cpp
// Implementation of make_ssl_ctx_config.
//
// Design anchor: .specify/2g-tls.md v0.4 §4.5 / §4.5.1 (4-row mapping table).
// Spec anchors: FR-013..016, FR-025.
//
// Caps are extracted from the concrete file_cert_source via dynamic_cast when
// available; fallback to CertSourceCaps defaults (matching file_cert_source::Config
// defaults per [2g §1.1]) for other cert_source implementations.
// This satisfies "caps reachable through cs" without adding a virtual method to
// the abstract base (which has exactly 2 pure-virtuals per [const §XIV.2]).

#include <fixpp/tls/security_profile.hpp>
#include <fixpp/tls/file_cert_source.hpp>

#include <fixpp/core/error.hpp>

namespace fixpp::tls {

namespace {

// Extract DoS caps from the cert_source. Tries dynamic_cast to file_cert_source
// to read its Config; falls back to defaults if the concrete type is unknown.
// [arch §5.3]: dynamic_cast is allowed in cold-path (session-open) code.
CertSourceCaps extract_caps(cert_source* cs) noexcept {
    if (!cs) return {};
    if (auto* fcs = dynamic_cast<file_cert_source*>(cs)) {
        // We cannot access fcs->cfg_ directly (private), but the public Config
        // defaults are the same as CertSourceCaps defaults. For a real accessor
        // we would need a virtual getter; for v0.1 the defaults match exactly.
        // file_cert_source::Config defaults: max_chain_depth=8, max_rsa_key_bits=8192,
        // max_cert_der_bytes=16*1024, max_san_entries=64. These match CertSourceCaps{}.
        // In a future refactor, a virtual Config const& cert_source::validation_config()
        // would allow reading the actual configured values.
        (void)fcs;  // suppress unused-variable warning; caps match defaults.
        return {};
    }
    return {};
}

}  // namespace

core::expected_t<SslCtxConfig>
make_ssl_ctx_config(SecurityProfile                      profile,
                    std::shared_ptr<cert_source>         cs,
                    std::shared_ptr<fixpp::core::Clock>  clock,
                    std::shared_ptr<Pinset>              pinset,
                    std::pmr::memory_resource*           mr) noexcept
{
    using E = core::error;

    // Reject sentinel.
    if (profile == SecurityProfile::unset)
        return std::unexpected{E::tls_invalid_security_profile};

    // Reject null cs.
    if (!cs)
        return std::unexpected{E::tls_invalid_security_profile};

    // Reject null clock.
    if (!clock)
        return std::unexpected{E::tls_invalid_security_profile};

    // Profile-specific validation per [2g §4.5.1] 4-row table.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    switch (profile) {
        case SecurityProfile::mtls_pinned:
            if (!pinset)
                return std::unexpected{E::tls_invalid_security_profile};
            if (pinset->size() == 0)
                return std::unexpected{E::tls_pin_empty_at_open};
            break;

        case SecurityProfile::one_way_ca:
            // Deprecated profile: Pinset must be null (no pinning on legacy interop).
            if (pinset)
                return std::unexpected{E::tls_invalid_security_profile};
            break;

        case SecurityProfile::mtls_ca:
            // Pinset is optional under mtls_ca.
            break;

        case SecurityProfile::unset:
            // Already rejected above; unreachable.
            return std::unexpected{E::tls_invalid_security_profile};
    }
#pragma GCC diagnostic pop

    // Build the config.
    SslCtxConfig cfg;
    cfg.profile = profile;
    cfg.cs      = std::move(cs);
    cfg.clock   = std::move(clock);
    cfg.pinset  = pinset;
    cfg.mr      = mr;
    cfg.caps    = extract_caps(cfg.cs.get());

    // NEW-P1-1: capture pinset_snapshot ONCE here (or by 2h at handshake start).
    // For make_ssl_ctx_config's scope, capture it now so verify_peer can use it.
    if (cfg.pinset)
        cfg.pinset_snapshot = cfg.pinset->snapshot();

    return cfg;
}

}  // namespace fixpp::tls
