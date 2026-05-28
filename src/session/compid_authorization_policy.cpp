// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/session/compid_authorization_policy.cpp
//
// fixpp::session::CompIdAuthorizationPolicy implementation.
// Anchors: FR-019..FR-025, D-8..D-10; [FIXS §4.4]; data-model.md §E-3;
// contracts/compid_authorization_policy.hpp; Clarifications Q2=A + Q3=A.
//
// PHASE 2 SHAPE: struct impl is defined here with std::string storage;
// method bodies are link-green stubs. Behavioral wiring + PMR-backed storage
// lands in Phase 3 T023/T024. [const §VII.1] TDD ordering: behavioral tests
// land Phase 3+.

#include <algorithm>
#include <map>
#include <memory>
#include <memory_resource>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "fixpp/core/error.hpp"
#include "fixpp/session/compid_authorization_policy.hpp"
// NOTE: fixpp/tls/peer_identity.hpp is included transitively via
// compid_authorization_policy.hpp. Direct include here would violate [arch §2.3]
// session→tls edge (check_layers.py). The Phase 2 authorize() stub does not
// access peer_identity fields; Phase 3 T023/T024 wires the full FR-022 extraction.

namespace fixpp::session {

// ── Private implementation ────────────────────────────────────────────────────
//
// Phase 2 storage: std::map<string, vector<string>> (std::string, no PMR).
// Phase 3 T023/T024 will migrate to PMR containers when the behavioral body
// lands alongside test cells. The mr field is stored for future use.
struct CompIdAuthorizationPolicy::impl {
    std::pmr::memory_resource* mr;
    std::map<std::string, std::vector<std::string>> bindings;

    explicit impl(std::pmr::memory_resource* r)
        : mr{r ? r : std::pmr::get_default_resource()},
          bindings{}
    {}

    impl(impl const&) = default;
    impl& operator=(impl const&) = default;
    impl(impl&&) noexcept = default;
    impl& operator=(impl&&) noexcept = default;
    ~impl() = default;
};

// ── Constructors / destructor ─────────────────────────────────────────────────

CompIdAuthorizationPolicy::CompIdAuthorizationPolicy() noexcept
    : impl_{std::make_unique<impl>(std::pmr::get_default_resource())}
{}

CompIdAuthorizationPolicy::CompIdAuthorizationPolicy(
    std::pmr::memory_resource* mr) noexcept
    : impl_{std::make_unique<impl>(mr)}
{}

CompIdAuthorizationPolicy::CompIdAuthorizationPolicy(
    CompIdAuthorizationPolicy const& other)
    : impl_{std::make_unique<impl>(*other.impl_)}
{}

CompIdAuthorizationPolicy& CompIdAuthorizationPolicy::operator=(
    CompIdAuthorizationPolicy const& other)
{
    if (this != &other) {
        impl_ = std::make_unique<impl>(*other.impl_);
    }
    return *this;
}

CompIdAuthorizationPolicy::CompIdAuthorizationPolicy(
    CompIdAuthorizationPolicy&&) noexcept = default;

CompIdAuthorizationPolicy& CompIdAuthorizationPolicy::operator=(
    CompIdAuthorizationPolicy&&) noexcept = default;

CompIdAuthorizationPolicy::~CompIdAuthorizationPolicy() = default;

// ── add_binding ───────────────────────────────────────────────────────────────
//
// Throws std::invalid_argument on empty principal / empty compid per
// [arch §5.3] construction-time throw carve-out.

void CompIdAuthorizationPolicy::add_binding(std::string_view principal,
                                             std::string_view compid)
{
    if (principal.empty()) {
        throw std::invalid_argument(
            "CompIdAuthorizationPolicy::add_binding: principal must not be empty");
    }
    if (compid.empty()) {
        throw std::invalid_argument(
            "CompIdAuthorizationPolicy::add_binding: compid must not be empty");
    }
    std::string key{principal};
    auto it = impl_->bindings.find(key);
    if (it == impl_->bindings.end()) {
        std::vector<std::string> vals;
        vals.emplace_back(compid);
        impl_->bindings.emplace(std::move(key), std::move(vals));
    } else {
        // Avoid duplicates (idempotent for the same principal+compid pair).
        std::string cid{compid};
        auto& vec = it->second;
        if (std::find(vec.begin(), vec.end(), cid) == vec.end()) {
            vec.push_back(std::move(cid));
        }
    }
}

// ── authorize ─────────────────────────────────────────────────────────────────
//
// FR-022 canonical-fixed order: CN → SAN-DNS → SAN-URI → SHA-256-fingerprint.
// Phase 2 SHAPE: default-deny stub. Behavioral body lands in Phase 3 T023/T024.

[[nodiscard]] core::expected_t<bound_principal>
CompIdAuthorizationPolicy::authorize(
    fixpp::tls::peer_identity const& /*pid*/,
    std::string_view /*asserted_compid*/) const noexcept
{
    // Phase 2 stub — behavioral body (FR-022 principal extraction + lookup) lands
    // in Phase 3 T023/T024. Default-deny until then.
    return std::unexpected{core::error::session_compid_unauthorized};
}

// ── Accessors ─────────────────────────────────────────────────────────────────

[[nodiscard]] std::size_t CompIdAuthorizationPolicy::binding_count() const noexcept {
    return impl_->bindings.size();
}

[[nodiscard]] bool CompIdAuthorizationPolicy::has_principal(
    std::string_view principal) const noexcept
{
    return impl_->bindings.count(std::string{principal}) > 0;
}

}  // namespace fixpp::session
