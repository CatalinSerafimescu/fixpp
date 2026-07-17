// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/codegen/test_078_nm_link_only_us3.cpp
//
// 078-precompiled-builder-libs T028 [US3] [TESTS-FIRST]: SC-002 `nm`
// link-only witness. This TU calls a SUBSET (exactly 3) of v44's builders --
// NewOrderSingle, ExecutionReport, News -- and links fixpp::builders::v44.
// It deliberately never `#include`s or references a 4th message
// (OrderCancelRequest) at all, nor `all.hpp` -- proving per-message `.o`
// archive granularity: the linker pulls only the 3 referenced
// `<Msg>.builder.o` members out of fixpp_builders_v44.a, not the whole
// ~18-20 MiB version-wide builder set (SC-002).
//
// Foundational (T002-T019) already landed the real emitter split + the
// per-message static-archive granularity (cmake/Codegen.cmake globs
// `*.builder.cpp` into fixpp_builders_<ver>, one .o per message), so this
// witness is BORN-GREEN (the property it asserts already holds) --
// feedback_fail_placeholder_red_test: no fake red-first cycle staged here.
// This is the real-lib, subset-of-3 counterpart to T020's single-message
// slim-compile witness (US1 AC1) and T024's builder/validator-disjointness
// witness (US2/SC-003) -- here the axis is INTRA-builder-lib granularity
// (SC-002), not cross-lib disjointness.
//
// Discrimination (wired in tests/codegen/CMakeLists.txt): a demangled
// (`nm -C`), namespace-qualified check on the FINAL LINKED BINARY asserts
// ALL THREE called `fixpp::v44::build_<Msg>` symbols ARE present AND that
// the un-called `fixpp::v44::build_OrderCancelRequest` is ABSENT -- proving
// the linker did not pull in the whole archive. See
// test_078_nm_builder_only_us2_check.cmake for why a bare mangled
// substring match is unsafe (fixpp::wire::body_builder carries an unrelated
// validate_group_grammar() member, pulled in transitively via fixpp_wire).
//
// Anchors: specs/078-precompiled-builder-libs/spec.md SC-002 (line 165);
// quickstart.md Scenario 3; tests/codegen/test_078_nm_builder_only_us2.cpp
// (nm precedent, demangled/namespace-qualified discrimination).

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <fixpp/core/decimal_alias.hpp>
#include <fixpp/v44/messages/ExecutionReport.hpp>
#include <fixpp/v44/messages/News.hpp>
#include <fixpp/v44/messages/NewOrderSingle.hpp>
#include <memory_resource>
#include <span>

TEST(NmLinkOnlyUS3, CallsExactlyThreeOfV44Builders) {
    std::pmr::monotonic_buffer_resource arena{4096};

    fixpp::v44::NewOrderSingleArgs nos_args{};
    nos_args.cl_ord_id = "US3-NOS-1";
    nos_args.symbol = "US3SYM";
    nos_args.side = '1';
    std::array<std::byte, 256> nos_out{};
    auto nos_built = fixpp::v44::build_NewOrderSingle(std::span<std::byte>{nos_out}, nos_args);
    ASSERT_TRUE(nos_built.has_value()) << "build_NewOrderSingle failed";
    EXPECT_GT(nos_built->size(), 0u);

    fixpp::v44::ExecutionReportArgs er_args{};
    er_args.cl_ord_id = "US3-ER-1";
    er_args.exec_id = "US3-EXECID";
    er_args.order_id = "US3-ORDERID";
    er_args.side = '2';
    std::array<std::byte, 256> er_out{};
    auto er_built = fixpp::v44::build_ExecutionReport(std::span<std::byte>{er_out}, er_args);
    ASSERT_TRUE(er_built.has_value()) << "build_ExecutionReport failed";
    EXPECT_GT(er_built->size(), 0u);

    std::array<fixpp::v44::groups::G_33Args, 1> lines{fixpp::v44::groups::G_33Args{}};
    lines[0].text = "US3 news body";
    fixpp::v44::NewsArgs news_args{};
    news_args.headline = "US3-HEADLINE";
    news_args.lines_of_text = lines;
    std::array<std::byte, 256> news_out{};
    auto news_built = fixpp::v44::build_News(std::span<std::byte>{news_out}, news_args);
    ASSERT_TRUE(news_built.has_value()) << "build_News failed";
    EXPECT_GT(news_built->size(), 0u);
}
