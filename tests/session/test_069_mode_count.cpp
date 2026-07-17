// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/session/test_069_mode_count.cpp
//
// 069-v44-all-families T016 [US3] -- mode-count / build-graph assertion
// (SC-004): fixpp::v44::builder_registry.size() must equal 83 under the
// default `all` coverage mode and 33 under `official`. Mode selection is
// keyed on the SAME compile define as test_067_completeness.cpp's C2 pin
// (FIXPP_CODEGEN_V44_FAMILIES_OFFICIAL), wired by cmake/Codegen.cmake (T015)
// onto this TU's host target whenever FIXPP_CODEGEN_V44_FAMILIES=official.
//
// Distinct from test_067_completeness.cpp's exact-SET completeness pin
// (contracts/coverage-and-completeness.md C2 -- independently-censused
// FIX44.xml set equality): this is the lighter-weight per-mode cardinality
// pin from the US3 Independent-Test criterion (spec.md/tasks.md) -- a plain
// registry-size check, no census.
//
// Joins the existing test_067_completeness binary (Article VII §8 -- no new
// executable, no gtest_discover_tests). That host is mode-agnostic in BOTH
// coverage modes, unlike test_067_builder_roundtrip/test_069_us1_smoke
// (which reference specific non-OFFICIAL typed builder symbols, e.g.
// build_TradeCaptureReport, and therefore cannot compile under an
// `official`-mode configure) -- this TU must build under either mode.
//
// Anchors: specs/069-v44-all-families/tasks.md T016;
//          specs/069-v44-all-families/contracts/coverage-and-completeness.md
//          C1/C2; cmake/Codegen.cmake (FIXPP_CODEGEN_V44_FAMILIES).

#include <gtest/gtest.h>

#include <cstddef>
#include <fixpp/v44/all.hpp>  // GENERATED -- fixpp::v44::builder_registry

namespace {

#if defined(FIXPP_CODEGEN_V44_FAMILIES_OFFICIAL)
constexpr std::size_t kExpectedModeCount = 33U;
#else
constexpr std::size_t kExpectedModeCount = 83U;
#endif

}  // namespace

TEST(ModeCount069, BuilderRegistrySizeMatchesActiveCoverageMode) {
    EXPECT_EQ(fixpp::v44::builder_registry.size(), kExpectedModeCount);
}
