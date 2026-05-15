// SPDX-License-Identifier: AGPL-3.0-or-later
// src/dictionary/version_registry.cpp
//
// 003-dictionary-codegen — out-of-line body of dict::version_registry::get
// ([2c §4.9]). SHAPE ONLY in v1.0 (AC-X1..X3): the ownership/construction
// model (engine-owned-by-value vs session-borrowed; EngineConfig::dictionaries)
// is deferred to 2d ([2c §10] Q10; spec §10 F3; data-model Entity 7). With no
// v1.0 construction surface the registry holds no Dictionary, so get() always
// reports dict_no_dictionary_for_application_version — the AC-X2 error,
// distinct from dict_unknown_appl_ver_id (a wire-string parse failure).
#include <fixpp/dict/version_registry.hpp>

namespace fixpp::dict {

core::expected_t<Dictionary const*>
version_registry::get(application_version /*v*/) const noexcept {
    return std::unexpected{core::error::dict_no_dictionary_for_application_version};
}

}  // namespace fixpp::dict
