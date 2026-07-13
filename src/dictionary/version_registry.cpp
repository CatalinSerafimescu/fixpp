// SPDX-License-Identifier: AGPL-3.0-or-later
// src/dictionary/version_registry.cpp
//
// dict::version_registry — construction surface + get() body.
//
// 003 shipped the shape only (AC-X1..X3): [2c §4.9] get() returns
// dict_no_dictionary_for_application_version when no Dictionary is registered.
// 007 owns the construction shape (D-20): the vector<shared_ptr<const
// Dictionary>> constructor maps session_version → application_version for
// non-FIXT.1.1 dicts; vt11 session-layer dicts are skipped.
//
// The mapping is:
//   session_version::v40..v50sp2 → the corresponding application_version
//   session_version::vt11        → skip (session-admin layer; app-version
//                                   lookup uses ApplVerID(1128) per-message)
//   session_version::Unknown     → skip
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <fixpp/core/error.hpp>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/version_profile.hpp>
#include <fixpp/dict/version_registry.hpp>
#include <memory>
#include <vector>

namespace fixpp::dict {

namespace {
// Map session_version to application_version for non-FIXT.1.1 unified
// sessions. Returns application_version::Unknown for vt11 (session-admin
// only; skip in registry) and Unknown (not a valid mapping).
application_version session_to_application(session_version sv) noexcept {
    switch (sv) {
        case session_version::v40:
            return application_version::v40;
        case session_version::v41:
            return application_version::v41;
        case session_version::v42:
            return application_version::v42;
        case session_version::v43:
            return application_version::v43;
        case session_version::v44:
            return application_version::v44;
        case session_version::v50:
            return application_version::v50;
        case session_version::v50sp1:
            return application_version::v50sp1;
        // 074: FIX Latest carries no distinct ApplVerID — its wire application
        // version IS v50sp2 (session_version::vlatest is the read-tier identity;
        // application_version stays v50sp2, no application_version::vlatest).
        // Stacked labels (one arm, two labels) — the standard idiom for a shared
        // body; avoids a bugprone-branch-clone on two identical adjacent arms.
        case session_version::v50sp2:
        case session_version::vlatest:
            return application_version::v50sp2;
        // NOLINTNEXTLINE(bugprone-branch-clone) - vt11 and Unknown are distinct semantic cases
        case session_version::vt11:
            return application_version::Unknown;  // skip
        case session_version::Unknown:
            return application_version::Unknown;  // skip
    }
    return application_version::Unknown;
}
}  // namespace

version_registry::version_registry(
    const std::vector<std::shared_ptr<const Dictionary>>& dicts) noexcept {
    for (const auto& d : dicts) {
        if (!d) {
            continue;
        }
        const auto av = session_to_application(d->which_session_version());
        const auto idx = static_cast<std::size_t>(av);
        if (idx == 0 || idx >= kTableSize) {
            continue;
        }  // Unknown or out-of-range
        // 074 FR-010: FIX50SP2 and FIX Latest (session_version::vlatest) both
        // map to the shared application_version::v50sp2 slot (idx 8) — the ONLY
        // slot two DISTINCT session_versions can collide on. Registering both in
        // one registry would silently last-writer-wins one dictionary, so a
        // lookup for application_version::v50sp2 would non-deterministically
        // return whichever landed last. Fail loud instead. Release-effective
        // (this ctor is noexcept — a throw would std::terminate anyway; an
        // NDEBUG-stripped assert would silently miss in release), scoped to the
        // literal FR-010 case: same slot, DIFFERENT session_versions. Same
        // session_version twice stays last-writer-wins (unrelated; not FR-010).
        if (entries_[idx] && idx == static_cast<std::size_t>(application_version::v50sp2) &&
            entries_[idx]->which_session_version() != d->which_session_version()) {
            std::fputs(
                "fixpp::dict::version_registry: FR-010 fatal — a FIX50SP2 dictionary and "
                "a FIX Latest (session_version::vlatest) dictionary both resolve to the "
                "shared application_version::v50sp2 slot; register at most one of them in "
                "a single version_registry\n",
                stderr);
            std::abort();
        }
        // Last writer wins if two dicts map to the same version.
        entries_[idx] = d;
    }
}

core::expected_t<Dictionary const*> version_registry::get(application_version v) const noexcept {
    const auto idx = static_cast<std::size_t>(v);
    if (idx == 0 || idx >= kTableSize) {
        // Unknown is not a valid lookup key.
        return std::unexpected{core::error::dict_no_dictionary_for_application_version};
    }
    const auto& entry = entries_[idx];
    if (!entry) {
        return std::unexpected{core::error::dict_no_dictionary_for_application_version};
    }
    return entry.get();
}

}  // namespace fixpp::dict
