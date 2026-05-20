// SPDX-License-Identifier: AGPL-3.0-or-later
// include/fixpp/dict/version_registry.hpp
//
// 003-OWNED shape + 2d-OWNED construction surface. The [2c §4.9] shape
// (get() only, AC-X1..X3) shipped with 003. The construction shape —
// "2d owns it" ([2c §10] Q10 / spec §10 F3 / research.md D-14) — is
// realized here by 007 (D-20). Concrete value type, not a virtual
// interface ([const §XIV.2] N/A). Oracle:
// specs/003-dictionary-codegen/contracts/version_registry.hpp; data-model
// Entity 7.
#pragma once
#include <memory>
#include <vector>

#include <fixpp/core/error.hpp>            // expected_t, error
#include <fixpp/dict/dictionary.hpp>       // Dictionary (002-shipped)
#include <fixpp/dict/version_profile.hpp>  // application_version enum (002);
                                           // version_profile struct is 003-owned
                                           // (RC#1; spec §8 audit).

namespace fixpp::dict {

class version_registry {
public:
    // Default-constructed registry holds NO dictionaries. get() always
    // returns dict_no_dictionary_for_application_version (AC-X2; seam 20).
    version_registry() noexcept = default;

    // 2d-OWNED construction surface (D-20): build a registry from a vector
    // of shared_ptr<const Dictionary> — the same element type as
    // EngineConfig::dictionaries. Non-FIXT.1.1 dicts are mapped by their
    // which_session_version() → the corresponding application_version.
    // FIXT.1.1 session-layer dicts (session_version::vt11) are NOT
    // registered in the application-version registry (they are session-admin;
    // per-message ApplVerID(1128) selects the application dict). The
    // borrowed pointers returned by get() alias the shared_ptr keepalives
    // held in this registry's entries_ vector (lifetime = registry lifetime).
    explicit version_registry(
        const std::vector<std::shared_ptr<const Dictionary>>& dicts) noexcept;

    version_registry(version_registry&&) noexcept = default;
    version_registry& operator=(version_registry&&) noexcept = default;

    // Non-copyable: shared_ptr keepalives could be copied, but the borrowed
    // pointer contract (get() returns Dictionary const* aliasing storage
    // with lifetime = registry lifetime) is cleaner without copy semantics.
    version_registry(const version_registry&)            = delete;
    version_registry& operator=(const version_registry&) = delete;

    ~version_registry() = default;

    // Borrowed pointer to the Dictionary for `v`. Errors with
    // dict_no_dictionary_for_application_version when no Dictionary is
    // registered for an otherwise-resolvable version (distinct from
    // dict_unknown_appl_ver_id, a wire-string parse failure — AC-X2 /
    // data-model error mapping). Returned pointer aliases the registry's
    // storage (lifetime = registry lifetime).
    [[nodiscard]] core::expected_t<Dictionary const*> get(application_version v) const noexcept
        [[clang::lifetimebound]];

private:
    // Keepalive + lookup table. Indexed by application_version (0-based:
    // Unknown=0 is never stored; v40=1..v50sp2=8). Null shared_ptr means
    // not registered for that version.
    // Small fixed-size array (9 entries) — application_version::v50sp2 == 8.
    static constexpr std::size_t kTableSize = 9;
    std::shared_ptr<const Dictionary> entries_[kTableSize]{};
};

}  // namespace fixpp::dict
