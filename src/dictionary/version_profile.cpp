// SPDX-License-Identifier: AGPL-3.0-or-later
// src/dictionary/version_profile.cpp
//
// 003-dictionary-codegen (RC#1) — out-of-line body of the 003-owned free
// function dict::resolve_application_version declared in
// include/fixpp/dict/version_profile.hpp. Profile-only [FIXT §5] priority
// resolution ([2c §4.3:457-501]) so dict::reify (which holds no Dictionary)
// can run it. Pure; no wire dependency. AC-VP2/AC-VP3/AC-VP4; data-model
// Entity 10 / I-13.
#include <fixpp/dict/version_profile.hpp>
#include <string_view>

namespace fixpp::dict {

core::expected_t<application_version> resolve_application_version(
    version_profile profile, std::string_view appl_ver_id_value) noexcept {
    // Step 2 ([2c §4.3:470-474]): empty ApplVerID → profile default.
    if (appl_ver_id_value.empty()) {
        if (profile.default_appl == application_version::Unknown) {
            // RC#1 / AC-D6: NOT a sentinel fall-through to
            // dict_reify_unknown_msg_type — the misdiagnosis path is closed.
            return std::unexpected{core::error::dict_unresolved_application_version};
        }
        return profile.default_appl;
    }

    // Step 1 ([2c §4.3:486-501]): parse via the WIRE enum values, NOT the C++
    // application_version internal indices (AC-VP4 / I-13 — reusing the C++
    // index would mis-map FIX 5.0 and the SP variants). Single wire char.
    if (appl_ver_id_value.size() == 1) {
        switch (appl_ver_id_value.front()) {
            case '2':
                return application_version::v40;  // FIX 4.0
            case '3':
                return application_version::v41;  // FIX 4.1
            case '4':
                return application_version::v42;  // FIX 4.2
            case '5':
                return application_version::v43;  // FIX 4.3
            case '6':
                return application_version::v44;  // FIX 4.4
            case '7':
                return application_version::v50;  // FIX 5.0
            case '8':
                return application_version::v50sp1;  // FIX 5.0 SP1
            case '9':
                return application_version::v50sp2;  // FIX 5.0 SP2
            default:
                break;  // '0'/'1' (pre-4.0/unused) and any other → reject
        }
    }
    return std::unexpected{core::error::dict_unknown_appl_ver_id};
}

}  // namespace fixpp::dict
