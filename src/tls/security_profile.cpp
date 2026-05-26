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

// Extract DoS caps from the cert_source. dynamic_cast to file_cert_source to read
// the OPERATOR-CONFIGURED Config (NOT the defaults — per FR-019 spec text and
// contracts/security_profile.hpp:153-154 binding contract "caps.X is shorthand for
// the cert_source's Config::X field reachable through cfg.cs->config()").
// [arch §5.3]: dynamic_cast is allowed in cold-path (session-open) code.
// Unknown concrete cert_source impls fall back to CertSourceCaps{} defaults.
CertSourceCaps extract_caps(cert_source* cs) noexcept {
    if (!cs) return {};
    if (auto* fcs = dynamic_cast<file_cert_source*>(cs)) {
        auto const& c = fcs->config();
        return CertSourceCaps{
            .max_chain_depth    = c.max_chain_depth,
            .max_rsa_key_bits   = c.max_rsa_key_bits,
            .max_cert_der_bytes = c.max_cert_der_bytes,
            .max_san_entries    = c.max_san_entries,
        };
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

    // pinset_snapshot is INTENTIONALLY left null here. Per [2g §6.5.1] BINDING
    // CONTRACT (re-emitted at contracts/security_profile.hpp:139-141), the
    // snapshot is captured ONCE at handshake start by 2h's wiring — NOT at
    // config time. A single SslCtxConfig may serve many handshakes; capturing
    // here would fossilize the snapshot and break FR-009 rotation semantics.
    // Tests acting as 2h must populate cfg.pinset_snapshot themselves before
    // calling verify_peer.

    return cfg;
}

}  // namespace fixpp::tls
