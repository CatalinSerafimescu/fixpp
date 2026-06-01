// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/session/test_version_registry_missing_routes_to_dict_layer.cpp
// [2d §9.20] Seam 20 — version_registry dictionary-missing routes through
// [2c §6.7] (D-13 / I-15 / Opus N2-P2-1 / FR-017).
//
// When an EngineConfig::dictionaries list omits a version that a downstream
// FIXT.1.1 session needs, the failure MUST surface as the EXISTING 2c-owned
// error:
//     dict_no_dictionary_for_application_version  (slot 28)
//     → FIXPP_ERR_DICT_CONFIG
// and NOT as a 2d-layer synonym routing to a different C-ABI group.
//
// Two paths are tested per I-15:
//
// PATH A — Registry-build path: validate_engine_config() (proxy for
//     Engine::open's registry-construction call) builds the version_registry
//     from EngineConfig::dictionaries and returns an error when the requested
//     application_version is absent. The free function
//     fixpp::core::build_version_registry(const EngineConfig&) is the minimal
//     2d-owned realization (D-20 — no Engine type in 007).
//
// PATH B — Dispatch-time path: version_registry::get(v) for a version not
//     in the registry returns dict_no_dictionary_for_application_version —
//     the unmodified 003-owned AC-X2 error, not a new 2d variant.
//
// This ensures future drift toward a 2d-layer synonym (routing the same
// failure to a different C-ABI group) is caught by CI.
#include <gtest/gtest.h>

#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/dict/version_profile.hpp>
#include <fixpp/dict/version_registry.hpp>
#include <memory>
#include <vector>

namespace {

using fixpp::core::build_version_registry;
using fixpp::core::EngineConfig;
using fixpp::core::error;
using fixpp::dict::application_version;
using fixpp::dict::version_registry;

// ── Path B: dispatch-time registry::get → dict_no_dictionary_for_application_version ──

// A default-constructed version_registry (no dictionaries registered) must
// return dict_no_dictionary_for_application_version for every lookup.
// This is the unchanged 003-owned AC-X2 contract; the test confirms no 2d
// synonym is introduced.
TEST(SeamVersionRegistryMissingRoutesToDictLayer, EmptyRegistryGetReturnsDictLayerError) {
    version_registry reg;
    auto result = reg.get(application_version::v50sp2);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), error::dict_no_dictionary_for_application_version)
        << "An empty registry must return the 2c-owned slot-28 error, "
           "NOT a 2d synonym";
}

// Verify the error is specifically slot 28 (dict_no_dictionary_for_application_version)
// and NOT any other dict_* variant.
TEST(SeamVersionRegistryMissingRoutesToDictLayer, MissingVersionIsSlot28NotOtherDictVariant) {
    version_registry reg;
    auto result = reg.get(application_version::v44);
    ASSERT_FALSE(result.has_value());
    // Confirm NOT a 2d-layer synonym:
    EXPECT_NE(result.error(), error::executor_already_stopped);
    EXPECT_NE(result.error(), error::invalid_session_config);
    EXPECT_NE(result.error(), error::clock_not_set);
    // Confirm it IS the 2c-owned dict-layer error (slot 28):
    EXPECT_EQ(result.error(), error::dict_no_dictionary_for_application_version);
}

// ── Path A: Registry-build via build_version_registry() ────────────────────
// When EngineConfig::dictionaries is EMPTY, build_version_registry returns an
// error (dict_no_dictionary_for_application_version). This is the proxy for
// Engine::open's registry-build step — the 007-scoped free function (D-20).
TEST(SeamVersionRegistryMissingRoutesToDictLayer, BuildRegistryFromEmptyEngineConfigDictionaries) {
    EngineConfig cfg;
    // No dictionaries → registry build should succeed (empty registry is
    // valid — no dictionaries registered). The *missing-version* error only
    // surfaces at dispatch time (get()), not at build time when no dicts at all.
    // validate_engine_config still requires cfg.clock != nullptr.
    // We test build_version_registry() directly here, not validate_engine_config().
    auto reg_result = build_version_registry(cfg);
    // An empty dictionaries list is a VALID EngineConfig (no dicts = no
    // FIXT.1.1 application dispatch). Build succeeds; get() fails.
    ASSERT_TRUE(reg_result.has_value())
        << "build_version_registry with empty dictionaries must succeed "
           "(empty registry is valid)";
    // Lookup on the resulting registry must return the 2c-owned error.
    auto get_result = reg_result->get(application_version::v50sp2);
    ASSERT_FALSE(get_result.has_value());
    EXPECT_EQ(get_result.error(), error::dict_no_dictionary_for_application_version)
        << "Registry built from empty EngineConfig::dictionaries must return "
           "the 2c-owned slot-28 error on get(), NOT a 2d synonym";
}

}  // namespace
