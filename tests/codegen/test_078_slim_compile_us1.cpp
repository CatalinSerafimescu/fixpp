// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/codegen/test_078_slim_compile_us1.cpp
//
// 078-precompiled-builder-libs T020 [US1] [TESTS-FIRST]: slim-header compile
// + link witness (US1 AC1/AC2). This TU `#include`s ONLY the slim per-message
// declaration header for one v44 message -- NOT `all.hpp`, NOT the removed
// monolithic `Builders.hpp` -- calls `build_NewOrderSingle`, and links the
// prebuilt `fixpp::builders::v44` library.
//
// Foundational (T002-T019) already landed the split + relink, so this
// witness is BORN-GREEN (the property it asserts already holds) --
// feedback_fail_placeholder_red_test: no fake red-first cycle staged here.
// The discrimination check lives in the companion nm probe wired in
// tests/codegen/CMakeLists.txt (test078_slim_compile_us1_nm_object_check /
// test_078_nm_assert.cmake): the compiled OBJECT for this TU carries ZERO
// defined `fixpp::v44::build_` symbols (not even NewOrderSingle's own body
// -- the slim header only declares it) and one UNDEFINED `build_NewOrderSingle`
// reference (resolved at link time from the archive) -- proving AC1
// ("object contains machine code only for the called builder", here strictly
// stronger: for none of them, since every body lives in the linked lib).
//
// Uses v44 (not the quickstart's vlatest) per the orchestrator brief: the
// v44 lib is built locally; the vlatest lib is CI-deferred (not built on
// this WSL2 host).
//
// Anchors: specs/078-precompiled-builder-libs/spec.md US1 AC1/AC2 (lines
// 56-59); quickstart.md Scenario 1; contracts/include-layout.md invariant 1;
// contracts/cmake-targets.md.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <fixpp/v44/messages/NewOrderSingle.hpp>  // slim decl header -- NOT all.hpp
#include <span>

TEST(SlimCompileUS1, BuildNewOrderSingleViaLinkedLib) {
    fixpp::v44::NewOrderSingleArgs args{};
    args.cl_ord_id = "SLIM-1";
    args.symbol = "SLIM";
    args.side = '1';

    std::array<std::byte, 256> out{};
    auto built = fixpp::v44::build_NewOrderSingle(std::span<std::byte>{out}, args);
    ASSERT_TRUE(built.has_value()) << "build_NewOrderSingle failed";
    EXPECT_GT(built->size(), 0u) << "build_NewOrderSingle produced an empty frame";
}
