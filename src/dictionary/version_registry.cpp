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
        case session_version::v50sp2:
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
