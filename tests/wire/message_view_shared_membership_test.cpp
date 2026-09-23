// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/message_view_shared_membership_test.cpp
//
// fixpp#495 (`.specify/495-493-486-dict-reify-copy.md` §2.2, §2.3; §10 T-7, T-8):
//   T-7 — the owned-route Parser constructor accepts an lvalue
//         `shared_ptr<const table_view>`, const-qualified or not; the
//         static_asserts below pin the forms it rejects.
//   T-8 — `detail::message_view_membership_access::shared_membership()` shares on
//         the owned route (arm 1), copies on the borrowed route (arm 2), yields
//         nullptr dict-free (arm 3), and copies the OLD table when the owner
//         object was reassigned after the parse (arm 1's identity check).

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fixpp/wire/parser.hpp>
#include <memory>
#include <memory_resource>
#include <string>
#include <type_traits>
#include <vector>

#include "support/fix44_dictionary.hpp"
#include "support/frame_view_factory.hpp"
#include "support/reify_test_frame.hpp"

namespace {

using fixpp::dict::table_view;
using fixpp::wire::access_mode;
using fixpp::wire::Parser;
using fixpp::wire::detail::message_view_membership_access;
using fixpp::wire::detail::owned_route_key;

using K = owned_route_key;
using S = std::shared_ptr<const table_view>;
using P = Parser<access_mode::Index>;

// ── T-7: constructor constraints ────────────────────────────────────────────
static_assert(std::is_constructible_v<P, K, S&>, "an lvalue owner is accepted");
static_assert(std::is_constructible_v<P, K, S const&>, "a const lvalue owner is accepted");
static_assert(!std::is_constructible_v<P, K, S&&>,
              "an rvalue owner would dangle: the parser stores its address");
static_assert(!std::is_constructible_v<P, K, S const&&>,
              "a const rvalue owner would dangle: the parser stores its address");
static_assert(
    !std::is_constructible_v<P, K, std::shared_ptr<table_view>&>,
    "only the exact owner type: a non-const pointee is rejected in the immediate context");

// A dictionary other than FIX44, for the reassigned-owner arm. FIX42's
// NewOrderSingle has no NoPartyIDs(453); FIX44's does.
std::shared_ptr<const fixpp::dict::Dictionary> make_fix42_dictionary() {
    auto mr = std::make_shared<std::pmr::monotonic_buffer_resource>();
    std::string const path = std::string(FIXPP_DICT_DATA_DIR) + "/FIX42.xml";
    auto* d = new fixpp::dict::Dictionary{fixpp::dict::XmlLoader{}.load(path, mr.get())};
    return std::shared_ptr<const fixpp::dict::Dictionary>{
        d, [mr](fixpp::dict::Dictionary const* p) { delete p; }};
}

class SharedMembership : public ::testing::Test {
protected:
    SharedMembership()
        : dict44_(fixpp::test_support::make_fix44_dictionary()),
          frame_(fixpp::test_support::make_nos_frame()),
          fv_(fixpp::wire::test::make_frame_view(frame_)) {}

    std::shared_ptr<const fixpp::dict::Dictionary> dict44_;
    std::vector<std::byte> frame_;
    fixpp::core::expected_t<fixpp::wire::frame_view> fv_;
    std::pmr::monotonic_buffer_resource arena_;
};

constexpr std::uint16_t kNoPartyIDs = 453;

// ── T-8 arm 1 — owned: the view's owner is shared, no copy ──────────────────
TEST_F(SharedMembership, OwnedRouteSharesTheOwner) {
    ASSERT_TRUE(fv_.has_value());
    S sp = std::make_shared<const table_view>(dict44_->as_table_view());
    P parser{K{}, sp};
    auto mv = parser.parse(*fv_, &arena_);
    ASSERT_TRUE(mv.has_value());

    long const before = sp.use_count();
    S const s = message_view_membership_access::shared_membership(*mv);
    EXPECT_EQ(s.get(), sp.get()) << "the owned route must share the owner's table";
    EXPECT_EQ(sp.use_count(), before + 1) << "sharing is one reference, not a copy";
}

// ── T-8 arm 2 — borrowed: a self-contained copy answering identically ───────
TEST_F(SharedMembership, BorrowedRouteCopies) {
    ASSERT_TRUE(fv_.has_value());
    S sp = std::make_shared<const table_view>(dict44_->as_table_view());
    P parser{*sp};
    auto mv = parser.parse(*fv_, &arena_);
    ASSERT_TRUE(mv.has_value());

    S const s = message_view_membership_access::shared_membership(*mv);
    ASSERT_NE(s, nullptr);
    EXPECT_NE(s.get(), sp.get()) << "a borrowed view has no owner to share: it copies";
    for (auto const& e : mv->offsets().entries()) {
        EXPECT_EQ(s->field_valid_for("D", e.tag), sp->field_valid_for("D", e.tag))
            << "tag " << e.tag;
    }
}

// ── T-8 arm 3 — dict-free: nothing to share ─────────────────────────────────
TEST_F(SharedMembership, DictFreeYieldsNull) {
    ASSERT_TRUE(fv_.has_value());
    P parser{};
    auto mv = parser.parse(*fv_, &arena_);
    ASSERT_TRUE(mv.has_value());
    EXPECT_EQ(message_view_membership_access::shared_membership(*mv), nullptr);
}

// ── T-8 arm 1's identity check — owner reassigned, old table still alive ────
TEST_F(SharedMembership, ReassignedOwnerCopiesTheTableTheViewWasParsedAgainst) {
    ASSERT_TRUE(fv_.has_value());
    S sp = std::make_shared<const table_view>(dict44_->as_table_view());
    S const old = sp;  // keeps the parsed-against table alive
    P parser{K{}, sp};
    auto mv = parser.parse(*fv_, &arena_);
    ASSERT_TRUE(mv.has_value());

    auto const dict42 = make_fix42_dictionary();
    sp = std::make_shared<const table_view>(dict42->as_table_view());
    ASSERT_FALSE(sp->field_valid_for("D", kNoPartyIDs))
        << "control: the new table must not carry the discriminating membership";
    ASSERT_TRUE(old->field_valid_for("D", kNoPartyIDs)) << "control: the old table must carry it";

    S const s = message_view_membership_access::shared_membership(*mv);
    ASSERT_NE(s, nullptr);
    EXPECT_NE(s.get(), sp.get()) << "must not share the reassigned owner's NEW table";
    EXPECT_NE(s.get(), old.get()) << "the owner no longer points at the old table: copy it";
    EXPECT_TRUE(s->field_valid_for("D", kNoPartyIDs))
        << "the copy is of the table the view was parsed against";
}

}  // namespace
