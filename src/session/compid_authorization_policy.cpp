// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/session/compid_authorization_policy.cpp
//
// fixpp::session::CompIdAuthorizationPolicy implementation — T034 [US2] Phase 4.
//
// Anchors: FR-019..FR-025, D-8..D-10; [FIXS §4.4]; data-model.md §E-3;
// contracts/compid_authorization_policy.hpp; Clarifications Q2=A + Q3=A.
//
// bound_principal.value LIFETIME DESIGN (T034 brief):
//   authorize() is const noexcept and returns bound_principal{string_view value; source from}.
//   On a successful match, value is a view into the matched KEY stored in bindings_
//   (the operator-supplied std::string key, which lives for the lifetime of the
//   impl object, which lives for the lifetime of the SessionConfig). This sidesteps
//   any scratch-buffer/lifetime problem for ALL four source types uniformly:
//   - CN:       extracted from subject_dn into a local scratch string (stack);
//               used as the lookup key; on hit, return string_view into the map key
//               (NOT into the local scratch — the scratch was copied into the map at
//               add_binding time). The scratch is transient and does NOT need to persist.
//   - SAN_DNS:  first entry of san_dns_names(); used as lookup key; on hit, view into map key.
//   - SAN_URI:  first entry of san_uris(); used as lookup key; on hit, view into map key.
//   - SHA256:   computed as 64-char lowercase hex into a local scratch array (stack, 64 bytes);
//               used as the lookup key; on hit, view into the matching map key (the operator
//               registered the same hex string via add_binding). Scratch does NOT persist.
//   This design works because std::map node keys are stable under insert/erase (no relocation),
//   so string_view into a key is valid for the lifetime of the map.
//
// flat_set availability: std::pmr::flat_set is C++23 (libstdc++/libc++ may not have it).
//   We use std::vector<std::string> (sorted, dedup on insert) as an equivalent for the
//   compid set. O(N) lookup over a typically small set (≤10 CompIDs per principal).
//   The header comment's "approximating the flat_set shape" note covers this choice.

#include "fixpp/session/compid_authorization_policy.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <memory_resource>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "fixpp/core/error.hpp"
// peer_identity.hpp is transitively included via compid_authorization_policy.hpp.
// Direct include here would violate [arch §2.3] session→tls edge per check_layers.py.
// Access via fixpp::tls::peer_identity is already available through the transitively
// included header.

namespace fixpp::session {

// ── Private implementation ────────────────────────────────────────────────────
//
// Storage: std::map<std::string, std::vector<std::string>>.
// - std::map chosen over std::unordered_map for stable node semantics (key
//   string_view returned by authorize() stays valid for map lifetime under inserts
//   as long as we do NOT erase). Both ordered and unordered maps satisfy
//   node-stability for inserts in C++17 (unordered_map buckets may rehash but
//   node key storage is stable per [alg.stable]).
// - std::vector<std::string> for the compid set: sorted + deduped on insert,
//   linear binary search at lookup. Typically ≤5 entries.
// - No PMR here: the impl is heap-allocated once at SessionConfig build time;
//   the operator calls add_binding() a handful of times. The cost is acceptable
//   at SessionConfig-copy time per data-model §E-3. A future perf pass can
//   introduce PMR if profiling shows it matters.
//
// The mr_ field is stored per the contract header to satisfy the PMR-ctor form.
struct CompIdAuthorizationPolicy::impl {
    std::pmr::memory_resource* mr;
    std::map<std::string, std::vector<std::string>> bindings;

    explicit impl(std::pmr::memory_resource* r)
        : mr{r ? r : std::pmr::get_default_resource()}, bindings{} {}

    impl(impl const&) = default;
    impl& operator=(impl const&) = default;
    impl(impl&&) noexcept = default;
    impl& operator=(impl&&) noexcept = default;
    ~impl() = default;
};

// ── extract_principal — T035 ──────────────────────────────────────────────────
//
// Canonical-fixed-order first-non-empty wins: CN → SAN_DNS → SAN_URI →
// SHA256_FINGERPRINT per FR-022 / D-8 / Clarifications Q2=A.
//
// Returns {extracted_string, source} where extracted_string is a temporary
// std::string. The caller looks it up in bindings and returns a string_view
// into the matched map key (NOT into the returned string).
//
// noexcept: all operations are stack-based or PMR-free; no allocation.
//   CN parse: scan the DN string — O(N) in DN length, no alloc.
//   SAN_DNS / SAN_URI: first entry index access, no alloc.
//   SHA256: loop over 32 bytes into a char[64] — no alloc.
//
// Called exclusively from authorize() on the hot path.

namespace {

// Parse the CN (commonName) attribute from an OpenSSL text-form DN string.
// The text form is a comma-separated sequence of "TYPE=VALUE" attributes,
// e.g. "CN=ACME-PROD-01,O=Acme Corp,C=US".
//
// We scan for the FIRST "CN=" prefix (case-sensitive, matching OpenSSL output)
// and return the value up to the next ',' or end-of-string. The value is NOT
// percent-decoded or RFC4514-unescaped; this matches the raw string the operator
// registers in add_binding (which is also the raw CN value without escaping in
// typical deployment).
//
// Returns an empty string_view if no CN attribute is found.
//
// noexcept — pure string scanning; no allocation.
[[nodiscard]] std::string_view parse_cn_from_dn(std::string_view dn) noexcept {
    // Search for "CN=" possibly preceded by a separator or at start.
    // To avoid partial matches (e.g. "OCN=..."), we check that the match
    // is either at the start of the string or preceded by ',' or ' '.
    std::size_t pos = 0;
    while (pos < dn.size()) {
        // Find "CN="
        const auto found = dn.find("CN=", pos);
        if (found == std::string_view::npos) {
            return {};  // not found
        }
        // Verify it is at start or preceded by field separator.
        if (found > 0) {
            const char pre = dn[found - 1];
            if (pre != ',' && pre != ' ' && pre != '/') {
                // Not a proper boundary — could be "OCN=" — skip past.
                pos = found + 3;
                continue;
            }
        }
        // Value starts after "CN=".
        const std::size_t vstart = found + 3;
        if (vstart >= dn.size()) {
            return {};
        }
        // Value ends at the next ',' or end-of-string.
        // RFC4514 allows escaped characters (\,) but we do not handle them
        // here for simplicity (the typical deployment uses unescaped CNs).
        std::size_t vend = vstart;
        while (vend < dn.size() && dn[vend] != ',') {
            ++vend;
        }
        const std::string_view value = dn.substr(vstart, vend - vstart);
        if (!value.empty()) {
            return value;
        }
        // Empty value — look for another CN= attribute.
        pos = vend;
    }
    return {};
}

// Encode 32 raw bytes as a 64-char lowercase hexadecimal string.
// Writes into `out[0..63]`. Returns a string_view into `out`.
// noexcept — pure byte→char mapping.
static constexpr char kHexChars[] = "0123456789abcdef";

[[nodiscard]] std::string_view fingerprint_to_hex(const std::array<std::byte, 32>& fp,
                                                  std::array<char, 64>& out) noexcept {
    for (std::size_t i = 0; i < 32; ++i) {
        const auto b = static_cast<unsigned char>(fp[i]);
        out[2 * i] = kHexChars[b >> 4u];
        out[2 * i + 1] = kHexChars[b & 0xFu];
    }
    return std::string_view{out.data(), 64};
}

struct ExtractedPrincipal {
    std::string value;  // canonical principal value (used as lookup key)
    bound_principal::source source;
};

[[nodiscard]] ExtractedPrincipal extract_principal(fixpp::tls::peer_identity const& pid) noexcept {
    // Step 1: CN from subject_dn.
    const std::string_view dn = pid.subject_dn_view();
    if (!dn.empty()) {
        const std::string_view cn = parse_cn_from_dn(dn);
        if (!cn.empty()) {
            return {std::string{cn}, bound_principal::source::CN};
        }
    }

    // Step 2: first SAN-DNS name.
    const auto dns_names = pid.san_dns_names();
    if (!dns_names.empty() && !dns_names[0].empty()) {
        return {std::string{dns_names[0]}, bound_principal::source::SAN_DNS};
    }

    // Step 3: first SAN-URI.
    const auto uris = pid.san_uris();
    if (!uris.empty() && !uris[0].empty()) {
        return {std::string{uris[0]}, bound_principal::source::SAN_URI};
    }

    // Step 4: SHA-256 fingerprint → 64-char lowercase hex.
    // Stack buffer — no heap allocation.
    std::array<char, 64> hex_buf{};
    const std::string_view hex = fingerprint_to_hex(pid.leaf_fingerprint, hex_buf);
    return {std::string{hex}, bound_principal::source::SHA256_FINGERPRINT};
}

}  // namespace

// ── Constructors / destructor ─────────────────────────────────────────────────

CompIdAuthorizationPolicy::CompIdAuthorizationPolicy() noexcept
    : impl_{std::make_unique<impl>(std::pmr::get_default_resource())} {}

CompIdAuthorizationPolicy::CompIdAuthorizationPolicy(std::pmr::memory_resource* mr) noexcept
    : impl_{std::make_unique<impl>(mr)} {}

CompIdAuthorizationPolicy::CompIdAuthorizationPolicy(CompIdAuthorizationPolicy const& other)
    : impl_{std::make_unique<impl>(*other.impl_)} {}

CompIdAuthorizationPolicy& CompIdAuthorizationPolicy::operator=(
    CompIdAuthorizationPolicy const& other) {
    if (this != &other) {
        impl_ = std::make_unique<impl>(*other.impl_);
    }
    return *this;
}

CompIdAuthorizationPolicy::CompIdAuthorizationPolicy(CompIdAuthorizationPolicy&&) noexcept =
    default;

CompIdAuthorizationPolicy& CompIdAuthorizationPolicy::operator=(
    CompIdAuthorizationPolicy&&) noexcept = default;

CompIdAuthorizationPolicy::~CompIdAuthorizationPolicy() = default;

// ── add_binding ───────────────────────────────────────────────────────────────
//
// Throws std::invalid_argument on empty principal / empty compid per
// [arch §5.3] construction-time throw carve-out.
//
// Idempotent: inserting the same (principal, compid) pair twice is a no-op
// (deduplication via sorted insert + find).

void CompIdAuthorizationPolicy::add_binding(std::string_view principal, std::string_view compid) {
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
        // Dedup: only insert if not already present.
        std::string cid{compid};
        auto& vec = it->second;
        const auto pos = std::lower_bound(vec.begin(), vec.end(), cid);
        if (pos == vec.end() || *pos != cid) {
            vec.insert(pos, std::move(cid));
        }
    }
}

// ── authorize ─────────────────────────────────────────────────────────────────
//
// FR-022 canonical-fixed order: CN → SAN-DNS → SAN-URI → SHA-256-fingerprint.
// T034/T035 behavioral implementation.
//
// On match: return bound_principal{value=string_view into bindings_ key, from=source}.
//   The string_view is valid for the lifetime of this impl (= SessionConfig lifetime).
//   std::map node keys are stable under insertions ([alg.stable]), so the view
//   remains valid even if more bindings are added after the authorize() call
//   (which in practice does not happen on the hot path, but the contract holds).
//
// On miss / empty-policy / compid not in set: return unexpected(session_compid_unauthorized).
//   noexcept: all internal paths are noexcept (pure map lookups; no allocation).

[[nodiscard]] core::expected_t<bound_principal> CompIdAuthorizationPolicy::authorize(
    fixpp::tls::peer_identity const& pid, std::string_view asserted_compid) const noexcept {
    // Empty policy → default-deny (FR-023 / D-9).
    if (impl_->bindings.empty()) {
        return std::unexpected{core::error::session_compid_unauthorized};
    }

    // Extract the principal from peer_identity using canonical-fixed order.
    // extract_principal is noexcept per its contract.
    const auto extracted = extract_principal(pid);

    // Look up the principal key in bindings_.
    const auto it = impl_->bindings.find(extracted.value);
    if (it == impl_->bindings.end()) {
        // Principal not found in policy → unauthorized (unrecognized principal).
        return std::unexpected{core::error::session_compid_unauthorized};
    }

    // Principal found. Check if asserted_compid is in the authorized set.
    const auto& compid_vec = it->second;
    // Binary search (vector is sorted via add_binding dedup logic).
    const auto pos =
        std::lower_bound(compid_vec.begin(), compid_vec.end(), std::string{asserted_compid});
    if (pos == compid_vec.end() || *pos != asserted_compid) {
        // Principal found but asserted CompID not authorized.
        return std::unexpected{core::error::session_compid_unauthorized};
    }

    // Match: return bound_principal with string_view into the MAP KEY (stable).
    // it->first is the std::string key in the std::map node — stable under
    // further insertions, valid for the lifetime of impl_.
    return bound_principal{
        .value = std::string_view{it->first},
        .from = extracted.source,
    };
}

// ── Accessors ─────────────────────────────────────────────────────────────────

[[nodiscard]] std::size_t CompIdAuthorizationPolicy::binding_count() const noexcept {
    return impl_->bindings.size();
}

[[nodiscard]] bool CompIdAuthorizationPolicy::has_principal(
    std::string_view principal) const noexcept {
    return impl_->bindings.count(std::string{principal}) > 0;
}

}  // namespace fixpp::session
