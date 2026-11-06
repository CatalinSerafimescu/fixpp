// SPDX-License-Identifier: AGPL-3.0-or-later
// src/dictionary/version_registry.cpp
//
// dict::version_registry — construction surface + get() body.
//
// 003 shipped the shape only (AC-X1..X3): [2c §4.9] get() returns
// dict_no_dictionary_for_application_version when no Dictionary is registered.
// 007 owns the construction shape (D-20): the vector<shared_ptr<const
// Dictionary>> constructor maps session_version → application_version for
// non-FIXT.1.1 dicts; a vt11 session-layer dict goes to transport() instead
// (099-fixt-appl-version-validation, data-model E-1a).
//
// The mapping is session_to_application in <fixpp/dict/version_profile.hpp>:
//   a session_version that names an application version → that application_version
//   session_version::vt11    → no slot (session-admin layer; app-version
//                              lookup uses ApplVerID(1128) per-message)
//   session_version::Unknown → skip
#include <cstddef>
#include <expected>
#include <fixpp/core/error.hpp>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/version_profile.hpp>
#include <fixpp/dict/version_registry.hpp>
#include <memory>
#include <vector>

namespace fixpp::dict {

version_registry::version_registry(
    const std::vector<std::shared_ptr<const Dictionary>>& dicts) noexcept {
    for (const auto& d : dicts) {
        if (!d) {
            continue;
        }
        if (d->which_session_version() == session_version::vt11) {
            transport_ = d;  // last writer wins, as for every slot below
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
    if (const auto& entry = entries_[idx]) {
        return entry.get();
    }
    // 097-fix-latest-applverid-10 one-way fallback: an empty v50sp2 slot answers from
    // the vlatest slot. The fallback serves a v50sp2 lookup only; a vlatest lookup
    // answers from its own slot or not at all.
    if (v == application_version::v50sp2) {
        if (const auto& latest = entries_[static_cast<std::size_t>(application_version::vlatest)]) {
            return latest.get();
        }
    }
    return std::unexpected{core::error::dict_no_dictionary_for_application_version};
}

core::expected_t<Dictionary const*> version_registry::transport() const noexcept {
    if (transport_) {
        return transport_.get();
    }
    return std::unexpected{core::error::dict_no_dictionary_for_application_version};
}

}  // namespace fixpp::dict
