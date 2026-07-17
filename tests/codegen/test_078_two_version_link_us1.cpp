// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/codegen/test_078_two_version_link_us1.cpp
//
// 078-precompiled-builder-libs T022 [US1] [TESTS-FIRST]: two-version
// no-symbol-collision witness (US1 AC3). A consumer TU links BOTH
// `fixpp::builders::v44` AND `fixpp::builders::v50sp2` and calls one
// `build_<Msg>` from each -- each resolves from its own library with no
// symbol collision.
//
// BORN-GREEN by construction: v44 and v50sp2's generated `build_<Msg>`
// symbols live in disjoint namespaces (`fixpp::v44::build_NewOrderSingle` vs
// `fixpp::v50sp2::build_NewOrderSingle`), so there is no ODR/link hazard to
// begin with -- feedback_fail_placeholder_red_test: reported honestly, not
// staged as a fake red-first cycle. The witness proves this empirically (the
// build+link succeeding IS the assertion, mirroring test_078_odr_sc003_probe's
// established "link succeeds" oracle) and additionally asserts BOTH calls
// produce a non-empty, DISTINCT wire body -- proving each resolved its own
// version's body, not a stray shared/aliased definition.
//
// Uses v44 + v50sp2 (both built locally) per the orchestrator brief --
// NOT vlatest, whose lib is CI-deferred.
//
// Anchors: specs/078-precompiled-builder-libs/spec.md US1 AC3 (line 60);
// contracts/cmake-targets.md (physically disjoint per-version namespaces).

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <fixpp/v44/messages/NewOrderSingle.hpp>
#include <fixpp/v50sp2/messages/NewOrderSingle.hpp>
#include <span>

TEST(TwoVersionLinkUS1, BothVersionsResolveFromOwnLibNoCollision) {
    fixpp::v44::NewOrderSingleArgs v44_args{};
    v44_args.cl_ord_id = "V44-CLORDID";
    v44_args.symbol = "V44SYM";
    v44_args.side = '1';

    fixpp::v50sp2::NewOrderSingleArgs v50sp2_args{};
    v50sp2_args.cl_ord_id = "V50SP2-CLORDID";
    v50sp2_args.symbol = "V50SP2SYM";
    v50sp2_args.side = '2';

    std::array<std::byte, 256> v44_out{};
    std::array<std::byte, 256> v50sp2_out{};

    auto v44_built = fixpp::v44::build_NewOrderSingle(std::span<std::byte>{v44_out}, v44_args);
    auto v50sp2_built =
        fixpp::v50sp2::build_NewOrderSingle(std::span<std::byte>{v50sp2_out}, v50sp2_args);

    ASSERT_TRUE(v44_built.has_value()) << "fixpp::v44::build_NewOrderSingle failed";
    ASSERT_TRUE(v50sp2_built.has_value()) << "fixpp::v50sp2::build_NewOrderSingle failed";

    std::string const v44_body(reinterpret_cast<char const*>(v44_built->data()), v44_built->size());
    std::string const v50sp2_body(reinterpret_cast<char const*>(v50sp2_built->data()),
                                   v50sp2_built->size());

    // Each version's own inputs (distinct ClOrdID/Symbol/Side) must show up
    // in its own body only -- a collision (e.g. one version's build_
    // silently invoking the other's) would cross-contaminate these.
    EXPECT_NE(v44_body.find("V44-CLORDID"), std::string::npos);
    EXPECT_EQ(v44_body.find("V50SP2-CLORDID"), std::string::npos);
    EXPECT_NE(v50sp2_body.find("V50SP2-CLORDID"), std::string::npos);
    EXPECT_EQ(v50sp2_body.find("V44-CLORDID"), std::string::npos);
    EXPECT_NE(v44_body, v50sp2_body);
}
