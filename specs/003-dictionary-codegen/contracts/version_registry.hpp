// SPDX-License-Identifier: AGPL-3.0-or-later
// Phase 1 contract — literal extract of [2c §4.9]. SHAPE ONLY in this PR
// (AC-X1..X3). Ownership/construction (engine-owned-by-value vs session-
// borrowed; EngineConfig::dictionaries) is deferred to 2d ([2c §10] Q10;
// spec §10 F3; research.md D-14). Concrete value type, not a virtual
// interface ([const §XIV.2] N/A).
#pragma once
#include <fixpp/core/error.hpp>            // expected_t, error
#include <fixpp/dict/dictionary.hpp>       // Dictionary (002)
#include <fixpp/dict/version_profile.hpp>  // application_version (002)

namespace fixpp::dict {

class version_registry {
public:
    // Borrowed pointer to the Dictionary for `v`. Errors with
    // dict_no_dictionary_for_application_version when no Dictionary is
    // registered for an otherwise-resolvable version (distinct from
    // dict_unknown_appl_ver_id, which is a wire-string parse failure —
    // AC-X2 / data-model error mapping). Returned pointer aliases the
    // registry's storage (lifetime = registry lifetime).
    [[nodiscard]] expected_t<Dictionary const*>
        get(application_version v) const noexcept [[clang::lifetimebound]];

    // Construction shape intentionally unspecified in v1.0/v1.1 — 2d owns it.
    // The shape-only test builds a registry by hand and exercises get().
};

}  // namespace fixpp::dict
