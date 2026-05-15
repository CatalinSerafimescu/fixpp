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

#include <fixpp/wire/message_view_contract.hpp>

#include <fixpp/v42/Messages.hpp>
#include <fixpp/v44/Messages.hpp>
#include <fixpp/v50sp2/Messages.hpp>
#include <fixpp/vt11/Messages.hpp>

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

TEST(CodegenFlyweightShape, R6FrozenContractWellFormed) {
    MV mv;
    auto a = mv.template get<35>();    // MsgType
    auto b = mv.template get<1128>();  // ApplVerID
    auto c = mv.get(35);
    EXPECT_FALSE(a.has_value());
    EXPECT_FALSE(b.has_value());
    EXPECT_FALSE(c.has_value());
}
