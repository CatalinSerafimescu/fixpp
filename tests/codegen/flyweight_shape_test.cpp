// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/codegen/flyweight_shape_test.cpp — T018 [US1] / seam #18
//
// AC-G7: a typed flyweight is EXACTLY one reference, no other state — the
// emitted static_assert is re-asserted here for ABI-drift detection across
// all four versions. R6 drift guard: the frozen [2b §4.3] surface
// (get<35>(), get<1128>(), get(uint16_t)) must stay well-formed against the
// vendored contract (N-P3-1 / N-P2-2).
//
// AC-G7a (per-message owning_message_traits specialisation) is emitted into
// <vXX>/Reify.hpp by US2/T032; its compile-time pin is exercised by the US2
// reify suites once Reify.hpp is generated — not here.
#include <gtest/gtest.h>

#include <fixpp/v42/Messages.hpp>
#include <fixpp/v44/Messages.hpp>
#include <fixpp/v50sp2/Messages.hpp>
#include <fixpp/vt11/Messages.hpp>
#include <fixpp/wire/group_view.hpp>
#include <fixpp/wire/message_view_contract.hpp>
#include <fixpp/wire/parser.hpp>

#include <cstddef>
#include <span>
#include <string>
#include <type_traits>

#include "support/frame_view_factory.hpp"

namespace {
using MV = fixpp::wire::MessageView<fixpp::wire::access_mode::Index>;
}

static_assert(sizeof(fixpp::v42::Heartbeat) == sizeof(MV const*));
static_assert(sizeof(fixpp::v44::NewOrderSingle) == sizeof(MV const*));
static_assert(sizeof(fixpp::v50sp2::NewOrderSingle) == sizeof(MV const*));
static_assert(sizeof(fixpp::vt11::Logon) == sizeof(MV const*));

TEST(CodegenFlyweightShape, SizeofIsOnePointer) {
    SUCCEED();  // static_asserts above are the AC-G7 pin (seam #18)
}

// 063 T005 (Round-2 tasks-pin #2): entry_context / representative generated
// `G_<no_tag>` size+alignment regression — guards the by-value object
// enlarged by the `group_context` field addition (group_view.hpp). Pinned
// to the current computed size on a 64-bit target (pointers=8,
// string_view=16, array<uint16_t,16>=32 — all standard-layout, identical
// across Tier-1/2/3 64-bit ABIs); a deliberate future growth must update
// this pin explicitly, not drift silently. `G_295` (v44 QuoteEntries,
// SC-001b's MassQuote scenario) is a representative nested-group entry —
// its sole member is an `entry_context`, so its size/align match exactly.
// fixpp#426: `dict_hooks` (5 pointers) replaced `opaque_dict` +
// `group_member_fn` (2 pointers), a deliberate +24 bytes.
// 100-wire-group-read-integrity (research D2 "Size census"): `dict_hooks` gains the
// registration callback, one more pointer, a deliberate +8 bytes.
static_assert(sizeof(fixpp::wire::entry_context) == 152);
static_assert(alignof(fixpp::wire::entry_context) == 8);
static_assert(sizeof(fixpp::v44::groups::G_295) == sizeof(fixpp::wire::entry_context));
static_assert(alignof(fixpp::v44::groups::G_295) == alignof(fixpp::wire::entry_context));

TEST(CodegenFlyweightShape, EntryContextSizeAlignRegression) {
    SUCCEED();  // static_asserts above are the 063 T005 pin
}

TEST(CodegenFlyweightShape, R6FrozenContractWellFormed) {
    MV mv;
    auto a = mv.template get<35>();    // MsgType
    auto b = mv.template get<1128>();  // ApplVerID
    auto c = mv.get(35);
    EXPECT_FALSE(a.has_value());
    EXPECT_FALSE(b.has_value());
    EXPECT_FALSE(c.has_value());
}

// 100-wire-group-read-integrity (T071 fix, B-100-7): an Iter view and a field
// iterator hold a POINTER to the `dict_hooks` bundle they read through, never a
// copy of it, so `Parser<Iter>::parse_iter()` stores one pointer per view rather
// than the whole bundle. Each pin below fails if a copy comes back:
//   - the iterator's size is its scan state plus ONE pointer (the same 64-bit
//     layout rules as the entry_context pin above: span=16, size_t=8, no STL
//     container), so a by-value bundle moves it by sizeof(dict_hooks) - 8;
//   - no constructor takes a bundle by value, so a caller passing one fails to
//     compile instead of handing the holder a temporary to alias;
//   - two views parsed by one parser expose the SAME bundle object, which two
//     copies could not.
namespace {
using IterMV = fixpp::wire::MessageView<fixpp::wire::access_mode::Iter>;
using FieldIt = IterMV::field_iterator;
}  // namespace
static_assert(sizeof(FieldIt) == 80);
static_assert(std::is_constructible_v<FieldIt, std::span<std::byte const>, std::size_t,
                                      fixpp::wire::dict_hooks const*>);
static_assert(!std::is_constructible_v<FieldIt, std::span<std::byte const>, std::size_t,
                                       fixpp::wire::dict_hooks>);
static_assert(std::is_constructible_v<IterMV, fixpp::wire::frame_view const&,
                                      fixpp::wire::dict_hooks const*>);
static_assert(!std::is_constructible_v<IterMV, fixpp::wire::frame_view const&,
                                       fixpp::wire::dict_hooks>);

TEST(CodegenFlyweightShape, IterViewHoldsTheParsersBundleNotACopy) {
    fixpp::dict::table_view const tv{};
    fixpp::wire::Parser<fixpp::wire::access_mode::Iter> parser{tv};
    std::string const raw =
        "8=FIX.4.4\x01"
        "9=5\x01"
        "35=0\x01"
        "10=000\x01";
    auto const fv_or = fixpp::wire::test::make_frame_view(std::as_bytes(std::span{raw}));
    ASSERT_TRUE(fv_or.has_value());
    auto const& fv = *fv_or;
    auto const a = parser.parse_iter(fv);
    auto const b = parser.parse_iter(fv);
    ASSERT_TRUE(a.has_value());
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ(a->hooks().opaque_dict(), &tv) << "the view reads through the parser's dictionary";
    EXPECT_EQ(&a->hooks(), &b->hooks())
        << "two views of one parser must alias one bundle; distinct addresses mean each view "
           "holds its own copy";
    // A frame-only view has no parser to alias: it reads through no dictionary.
    IterMV const bare{fv};
    EXPECT_EQ(bare.hooks().opaque_dict(), nullptr);
}
