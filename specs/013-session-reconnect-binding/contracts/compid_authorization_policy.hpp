// SPDX-License-Identifier: AGPL-3.0-or-later
//
// fixpp — 013-session-reconnect-binding
// Contract: CompIdAuthorizationPolicy — operator-supplied allow-list of
// {principal → {compid_set}} bindings for CompID↔TLS-identity authorization.
// Anchors: FR-019..FR-025, D-8..D-10; [FIXS §4.4]; Clarifications Q2=A + Q3=A.

#pragma once

#include <cstdint>
#include <memory_resource>
#include <string_view>

#include "fixpp/core/error.hpp"
#include "fixpp/core/expected.hpp"
#include "fixpp/tls/peer_identity.hpp"

namespace fixpp::session {

// bound_principal — result of a successful authorize() call. The value field
// is the principal value extracted per FR-022 canonical-fixed order (D-8); the
// `from` field records which peer_identity slot bound the session (for the
// peer_identity_bound event's principal_source field per D-10 / FR-022).
struct bound_principal {
    std::string_view value;  // [[clang::lifetimebound]] into peer_identity material
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
class CompIdAuthorizationPolicy {
public:
    // Default-constructed = empty allow-list = default-deny.
    CompIdAuthorizationPolicy() noexcept;

    // PMR-allocated form for operator-supplied long-lived arenas.
    explicit CompIdAuthorizationPolicy(std::pmr::memory_resource* mr) noexcept;

    // COPY-CONSTRUCTIBLE per 010 W-5 SessionConfig static_assert
    // (`include/fixpp/session/session_config.hpp:176-180`); the underlying
    // std::pmr::unordered_map<std::pmr::string, std::pmr::flat_set<std::pmr::string>>
    // storage already supports copy. Copy is value-semantics deep-copy of the
    // bindings using the destination's PMR memory_resource. SessionConfig is
    // held BY VALUE by Session per 010 FR-001 by-value membership; every
    // SessionConfig field MUST be copy-constructible.
    //
    // Copy cost is acceptable at SessionConfig copy time (engine bootstrap /
    // Session::open path, NOT the hot path). The std::shared_ptr<store_factory>
    // pattern (010 FR-001a) is NOT applied here — the value-semantics copy is
    // operator-visible and intentional (operator may copy a SessionConfig to
    // open a parallel session against the same binding policy).
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
    [[nodiscard]] expected_t<bound_principal>
    authorize(fixpp::tls::peer_identity const& pid,
              std::string_view asserted_compid) const noexcept;

    // Accessor — number of bindings (operator audit + completeness audit).
    [[nodiscard]] std::size_t binding_count() const noexcept;

    // Accessor — true if at least one binding exists for the given principal
    // value. Operator audit helper; NOT for hot-path use.
    [[nodiscard]] bool has_principal(std::string_view principal) const noexcept;

private:
    // Storage shape defined in src/session/compid_authorization_policy.cpp;
    // approximate: std::pmr::unordered_map<std::pmr::string,
    //                                       std::pmr::flat_set<std::pmr::string>>
    struct impl;
    std::unique_ptr<impl> impl_;
};

}  // namespace fixpp::session
