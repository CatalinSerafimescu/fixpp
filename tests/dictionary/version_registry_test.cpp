// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/version_registry_test.cpp
//
// 003-dictionary-codegen — T006. AC-X1..AC-X3. dict::version_registry SHAPE
// ONLY ([2c §4.9]): the get(application_version)→expected_t<Dictionary const*>
// signature + the AC-X2 dict_no_dictionary_for_application_version error.
// Ownership/construction is deferred to 2d (F3); the v1.0 registry has no
// construction surface, so a hand-built (default) registry's get() always
// reports the AC-X2 error. Oracle:
// specs/003-dictionary-codegen/contracts/version_registry.hpp; data-model
// Entity 7.

#include <gtest/gtest.h>

#include <type_traits>

#include <fixpp/core/error.hpp>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/version_profile.hpp>
#include <fixpp/dict/version_registry.hpp>

namespace {

using fixpp::core::error;
using fixpp::dict::application_version;
using fixpp::dict::Dictionary;
using fixpp::dict::version_registry;

// AC-X1 — the get() signature/return type is exactly as [2c §4.9] locks it.
static_assert(std::is_same_v<
    decltype(std::declval<version_registry const&>().get(application_version::v44)),
    fixpp::core::expected_t<Dictionary const*>>);

TEST(VersionRegistryAcX2, EmptyRegistryReportsNoDictionary) {
    // AC-X3 — hand-built (default) registry; no engine wiring (2d owns
    // construction). AC-X2 — distinct error from dict_unknown_appl_ver_id
    // (a wire-string parse failure).
    version_registry const reg{};
    for (auto v : {application_version::v42, application_version::v44,
                   application_version::v50sp2, application_version::Unknown}) {
        auto got = reg.get(v);
        ASSERT_FALSE(got.has_value());
        EXPECT_EQ(got.error(), error::dict_no_dictionary_for_application_version);
    }
}

}  // namespace
