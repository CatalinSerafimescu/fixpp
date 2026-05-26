#pragma once
// include/fixpp/tls/peer_identity.hpp
// fixpp::tls::peer_identity — owning value type derived from a verified peer cert.
//
// Design anchor: .specify/2g-tls.md v0.4 §4.5 lines 706-724 (verbatim).
// Spec anchors: FR-018 (peer_identity value type), FR-022 ([[nodiscard]]).
// Data-model: E-13.
//
// peer_identity is constructed EXCLUSIVELY by verify_peer (contracts assertion 1).
// All string/SAN storage is PMR-allocated so the caller controls the allocator
// through cfg.mr at verify_peer time.

#include <array>
#include <chrono>
#include <cstddef>
#include <memory_resource>
#include <span>
#include <string_view>

namespace fixpp::tls {

struct peer_identity {
    // Owning storage — PMR-allocated; SAN vectors bounded ≤ max_san_entries.
    // Field shape mirrors 2g §4.5 lines 706-710 verbatim.
    std::pmr::string subject_dn;                             // owned, PMR-allocated.
    std::pmr::vector<std::pmr::string> san_dns_names_owned;  // owned, PMR-allocated.
    std::pmr::vector<std::pmr::string> san_uris_owned;       // owned, PMR-allocated.
    std::array<std::byte, 32> leaf_fingerprint{};            // SHA-256 of the leaf's DER.

    // Constructor wiring PMR allocator through all PMR members.
    // Construction ONLY by verify_peer (contracts assertion 1).
    explicit peer_identity(std::pmr::memory_resource* mr)
        : subject_dn{mr}, san_dns_names_owned{mr}, san_uris_owned{mr} {}

    // Default constructor uses new_delete_resource (for test stubs only).
    peer_identity() = default;

    // not_after carried for the session FSM's expiry checks (D-9 / [2d §7.9]).
    std::chrono::system_clock::time_point not_after{};

    // ── Convenience views — bounded by *this ──────────────────────────────────
    // Storage is the owning vectors above; accessors carry [[clang::lifetimebound]]
    // at the declaration site per [arch §5.5] / [2b §6.4] precedent.
    // (2g §4.5 lines 714-723 verbatim.)

    [[nodiscard]] std::string_view subject_dn_view() const noexcept [[clang::lifetimebound]] {
        return std::string_view{subject_dn.data(), subject_dn.size()};
    }

    [[nodiscard]] std::span<const std::pmr::string> san_dns_names() const noexcept
        [[clang::lifetimebound]] {
        return {san_dns_names_owned.data(), san_dns_names_owned.size()};
    }

    [[nodiscard]] std::span<const std::pmr::string> san_uris() const noexcept
        [[clang::lifetimebound]] {
        return {san_uris_owned.data(), san_uris_owned.size()};
    }
};

}  // namespace fixpp::tls
