// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/codegen/test_102_multichar_read.cpp — 102-dict-width-multichar-type, fixpp#621
// (spec US2.1, US2.2, FR-004; research D4 "#621"; tasks T019).
//
// A MultiCharValue field (FIX's MultipleCharValue: space-separated single characters, for
// example ExecInst(18)=1 2) reads through its generated typed accessor as the string type a
// MultipleStringValue field reads as, verbatim, with no check of the MultipleCharValue
// grammar (spec FR-004). Cells, all on `fixpp::v50sp2`:
//   - `NewOrderSingle::exec_inst()` on `18=1 2`, on `18=1`, and on a value with a doubled and
//     a trailing space;
//   - a group entry's `leg_exec_inst()` (LegExecInst(1384)) on `1384=1 2`, through
//     `NewOrderMultileg::legs()`, the read class whose group entry carries `leg_exec_inst`;
//   - the owning (reified) `owning_NewOrderSingle::exec_inst()` on `18=1 2`;
//   - the shape of each accessor, `expected_t<std::string_view>`, checked at run time.
//
// Every cell compiles whatever the accessor's value type is: a value is compared through
// `as_text`, which renders a `char` or a `std::string_view` as `std::string`, and a type is
// compared with `std::is_same_v` inside `EXPECT_TRUE`, so a type that differs is a failing
// assertion, not a compile error.

#include <gtest/gtest.h>

#include <cstddef>
#include <fixpp/core/error.hpp>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fixpp/v50sp2/Messages.hpp>
#include <fixpp/v50sp2/Reify.hpp>
#include <memory_resource>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "support/app_message_read_scaffold.hpp"

namespace {

using IndexView = fixpp::wire::MessageView<fixpp::wire::access_mode::Index>;

// The value as text, whichever of `char` and `std::string_view` the accessor returns.
template <class T>
std::string as_text(T const& v) {
    if constexpr (std::is_same_v<T, char>) {
        return std::string(1, v);
    } else {
        return std::string{v};
    }
}

// The accessor's value type, from the expected the accessor returns.
template <class Expected>
using value_of = std::remove_cvref_t<Expected>::value_type;

struct V50Sp2 {
    std::pmr::monotonic_buffer_resource arena{65536};
    fixpp::dict::Dictionary dict{
        fixpp::dict::XmlLoader{}.load(std::string(FIXPP_DICT_DATA_DIR) + "/FIX50SP2.xml", &arena)};
    fixpp::dict::table_view tv{dict.as_table_view()};
};

// Loaded once per binary; every view built over `tv` must not outlive it.
V50Sp2 const& v50sp2() {
    static V50Sp2 const d;
    return d;
}

// A parsed frame and the arena and bytes the view reads from. The view is move-constructed
// in place (`emplace`), never move-assigned: a moved-into view keeps the parse's arena only
// when it is constructed from the parse's result.
struct Parsed {
    std::vector<std::byte> frame;
    std::pmr::monotonic_buffer_resource arena{16384};
    std::optional<IndexView> view;
};

IndexView const& parse_into(Parsed& p, std::string_view body) {
    p.frame = fixpp_test_support::make_frame("FIX.5.0SP2", body);
    return p.view.emplace(fixpp_test_support::parse_dict(p.frame, v50sp2().tv, &p.arena));
}

}  // namespace

TEST(MultiChar102V50sp2, ExecInstTwoValuesReadsVerbatim) {
    Parsed p;
    IndexView const& view = parse_into(p,
                                       "35=D\x01"
                                       "11=CL1\x01"
                                       "18=1 2\x01");
    ASSERT_FALSE(::testing::Test::HasFailure());
    fixpp::v50sp2::NewOrderSingle const msg{view};
    auto const r = msg.exec_inst();
    ASSERT_TRUE(r.has_value()) << "exec_inst() refused the multi-valued 18=1 2";
    EXPECT_EQ(as_text(*r), "1 2");
}

TEST(MultiChar102V50sp2, ExecInstOneValueReadsAsAString) {
    Parsed p;
    IndexView const& view = parse_into(p,
                                       "35=D\x01"
                                       "11=CL1\x01"
                                       "18=1\x01");
    ASSERT_FALSE(::testing::Test::HasFailure());
    fixpp::v50sp2::NewOrderSingle const msg{view};
    auto const r = msg.exec_inst();
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(as_text(*r), "1");
    EXPECT_TRUE((std::is_same_v<value_of<decltype(r)>, std::string_view>))
        << "exec_inst() must return the MultipleStringValue fields' string type";
}

// Spec FR-004: no MultipleCharValue grammar check; spacing is returned as it came.
TEST(MultiChar102V50sp2, ExecInstIrregularSpacingReadsVerbatim) {
    Parsed p;
    IndexView const& view = parse_into(p,
                                       "35=D\x01"
                                       "11=CL1\x01"
                                       "18=1  2 \x01");
    ASSERT_FALSE(::testing::Test::HasFailure());
    fixpp::v50sp2::NewOrderSingle const msg{view};
    auto const r = msg.exec_inst();
    ASSERT_TRUE(r.has_value()) << "exec_inst() refused 18=1  2 (doubled and trailing space)";
    EXPECT_EQ(as_text(*r), "1  2 ");
}

TEST(MultiChar102V50sp2, GroupEntryLegExecInstReadsVerbatim) {
    Parsed p;
    IndexView const& view = parse_into(p,
                                       "35=AB\x01"
                                       "11=CL1\x01"
                                       "555=1\x01"
                                       "600=SYM\x01"
                                       "1384=1 2\x01");
    ASSERT_FALSE(::testing::Test::HasFailure());
    fixpp::v50sp2::NewOrderMultileg const msg{view};
    auto const legs = msg.legs();
    ASSERT_EQ(legs.size(), 1U) << "the NoLegs(555) instance did not resolve";
    auto const leg = legs[0];  // the accessor's view is bound to a named entry
    auto const r = leg.leg_exec_inst();
    ASSERT_TRUE(r.has_value()) << "leg_exec_inst() refused the multi-valued 1384=1 2";
    EXPECT_EQ(as_text(*r), "1 2");
    EXPECT_TRUE((std::is_same_v<value_of<decltype(r)>, std::string_view>));
}

TEST(MultiChar102V50sp2, OwningExecInstReadsVerbatim) {
    Parsed p;
    IndexView const& view = parse_into(p,
                                       "35=D\x01"
                                       "11=CL1\x01"
                                       "18=1 2\x01");
    ASSERT_FALSE(::testing::Test::HasFailure());
    std::pmr::monotonic_buffer_resource own_arena{16384};
    auto owned = fixpp::v50sp2::owning_NewOrderSingle::from_view(view, &own_arena);
    ASSERT_TRUE(owned.has_value()) << "from_view failed";
    auto const r = owned->exec_inst();
    ASSERT_TRUE(r.has_value()) << "owning exec_inst() refused the multi-valued 18=1 2";
    EXPECT_EQ(as_text(*r), "1 2");
}

TEST(MultiChar102V50sp2, AccessorShapesAreStringViews) {
    using view_t = decltype(std::declval<fixpp::v50sp2::NewOrderSingle const&>().exec_inst());
    using owning_t =
        decltype(std::declval<fixpp::v50sp2::owning_NewOrderSingle const&>().exec_inst());
    using entry_t = decltype(std::declval<fixpp::v50sp2::groups::G_555 const&>().leg_exec_inst());
    EXPECT_TRUE(
        (std::is_same_v<std::remove_cvref_t<view_t>, fixpp::core::expected_t<std::string_view>>));
    EXPECT_TRUE(
        (std::is_same_v<std::remove_cvref_t<owning_t>, fixpp::core::expected_t<std::string_view>>));
    EXPECT_TRUE(
        (std::is_same_v<std::remove_cvref_t<entry_t>, fixpp::core::expected_t<std::string_view>>));
}
