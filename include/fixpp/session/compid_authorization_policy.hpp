// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/session/compid_authorization_policy.hpp
//
// fixpp::session::CompIdAuthorizationPolicy — operator-supplied allow-list of
// {principal → {compid_set}} bindings for CompID↔TLS-identity authorization.
// Anchors: FR-019..FR-025, D-8..D-10; [FIXS §4.4]; data-model.md §E-3;
// contracts/compid_authorization_policy.hpp; Clarifications Q2=A + Q3=A.
//
// Copy-constructible per 010 W-5 — satisfies the
//   static_assert(std::is_copy_constructible_v<SessionConfig>)
// at session_config.hpp:176. Storage is pimpl so the include cost is minimal.
// Body lives in src/session/compid_authorization_policy.cpp.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <memory_resource>
#include <string_view>

#include "fixpp/core/error.hpp"  // error enum + expected_t<T>
#include "fixpp/session/logon_credentials.hpp"  // logon_credentials — T021/033 US2
#include "fixpp/tls/peer_identity.hpp"

namespace fixpp::session {

// bound_principal — result of a successful authorize() call. The value field
// is the principal value extracted per FR-022 canonical-fixed order (D-8); the
// `from` field records which peer_identity slot bound the session (for the
// peer_identity_bound event's principal_source field per D-10 / FR-022).
struct bound_principal {
    std::string_view value;  // lifetimebound: view into peer_identity material (session lifetime)
    enum class source : std::uint8_t {
        CN = 0,
        SAN_DNS = 1,
        SAN_URI = 2,
        SHA256_FINGERPRINT = 3,
    };
    source from;
};

// CompIdAuthorizationPolicy — allow-list only in v1.0 per FR-023 / D-9.
// Empty policy = default-deny (rejects ALL Logons). Operator MUST enumerate
// every {principal → {compid_set}} binding before opening any session.
// Constructed at SessionConfig-build time; consulted per Logon (both initiator
// AND acceptor halves per FR-024).
//
// COPY-CONSTRUCTIBLE per 010 W-5 SessionConfig static_assert. The pimpl
// value-semantics deep-copy uses the destination's PMR memory_resource.
// Copy cost is acceptable at SessionConfig copy time (engine bootstrap /
// Session::open path, NOT the hot path). [data-model §E-3]
class CompIdAuthorizationPolicy {
public:
    // Default-constructed = empty allow-list = default-deny.
    CompIdAuthorizationPolicy() noexcept;

    // PMR-allocated form for operator-supplied long-lived arenas.
    explicit CompIdAuthorizationPolicy(std::pmr::memory_resource* mr) noexcept;

    // COPY-CONSTRUCTIBLE per 010 W-5 (deep-copy of bindings into default PMR).
    CompIdAuthorizationPolicy(CompIdAuthorizationPolicy const&);
    CompIdAuthorizationPolicy& operator=(CompIdAuthorizationPolicy const&);

    CompIdAuthorizationPolicy(CompIdAuthorizationPolicy&&) noexcept;
    CompIdAuthorizationPolicy& operator=(CompIdAuthorizationPolicy&&) noexcept;

    ~CompIdAuthorizationPolicy();

    // Add an allow-list binding. Throws std::invalid_argument on empty
    // principal / empty compid per [arch §5.3] construction-time carve-out.
    // SessionConfig-build time only; not called on the hot path.
    void add_binding(std::string_view principal, std::string_view compid);

    // Authorize: extract principal from peer_identity per FR-022 canonical-fixed
    // order (CN → SAN-DNS → SAN-URI → SHA-256-fingerprint, first-non-empty wins);
    // look up in bindings_; return bound_principal on success OR
    // error::session_compid_unauthorized (slot 117) on miss / empty-policy /
    // unmatched principal→compid pair. noexcept for runtime use.
    [[nodiscard]] core::expected_t<bound_principal> authorize(
        fixpp::tls::peer_identity const& pid, std::string_view asserted_compid) const noexcept;

    // Accessor — number of bindings (operator audit + completeness audit).
    [[nodiscard]] std::size_t binding_count() const noexcept;

    // Accessor — true if at least one binding exists for the given principal
    // value. Operator audit helper; NOT for hot-path use.
    [[nodiscard]] bool has_principal(std::string_view principal) const noexcept;

    // ── FR-008a / 033 US2 — credential authorization seam ────────────────────
    //
    // authorize_logon: called on the acceptor inbound-Logon establishment path
    // independently of mTLS (fired whether or not the TLS-gated `authorize` ran).
    // Default implementation: ACCEPT (returns true). No credential validation in
    // v1.0 (FR-008a deferred); this seam is the attach point for the future
    // config-gated validation knob.
    //
    // asserted_compid: peer's SenderCompID(49) from the inbound Logon.
    // creds: parsed Username(553)/Password(554), or both absent when not sent.
    //
    // [033 research R6; contracts C7; data-model E5; FR-008/FR-008a]
    [[nodiscard]] bool authorize_logon(std::string_view asserted_compid,
                                       logon_credentials const& creds) const noexcept;
    // noexcept: the default-accept path is noexcept. A user-installed validator
    // MUST be noexcept at the call site; if it throws, std::terminate fires
    // (matching [const §X.5] noexcept convention for runtime use).

    // set_logon_validator: install the future FR-008a validation knob.
    // The callable is invoked by authorize_logon() instead of the default-accept.
    // Signature: bool(std::string_view asserted_compid, logon_credentials const& creds).
    // Returns true → accept; false → reject (causes Disconnected in the session arm).
    // Setting nullptr restores default-accept behaviour.
    // NOT noexcept — the validator may be set at config-build time, not on the hot path.
    // [033 T021; FR-008a future validation knob; research R6]
    using logon_validator_fn = std::function<bool(std::string_view, logon_credentials const&)>;
    void set_logon_validator(logon_validator_fn validator);

private:
    // Storage: std::pmr::unordered_map<std::pmr::string,
    //                                  std::pmr::vector<std::pmr::string>>
    // (approximating the flat_set shape; defined in compid_authorization_policy.cpp).
    // pimpl keeps this header free of PMR container complexity.
    struct impl;
    std::unique_ptr<impl> impl_;
};

}  // namespace fixpp::session
