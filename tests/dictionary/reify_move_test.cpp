// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/reify_move_test.cpp — T029 [P] [US2] / seam #14
//
// AC-R4: move semantics of owning_<Msg>.
//
// Static assertions (compile-time shape oracle, seam #14):
//   * no reference members (is_nothrow_move_constructible_v requires this in
//     practice; more directly: we assert no-throw-move + not-default)
//   * std::is_nothrow_move_constructible_v<owning_<Msg>>
//   * move ctor is NOT = default (cannot be trivial; custom reset of caches)
//
// 2b cutover (004 T059): from_view performs the REAL owning deep-copy. The
// R6 dict_reify_wire_body_not_ready oracle is retired. Move semantics are
// verified both via the static shape oracle (below — UNCHANGED: the
// no-new-member T059 design preserves owning_<Msg>'s shape) AND via real
// behavioural assertions (parsed values survive move + source-arena death).
//
// Oracle: specs/003-dictionary-codegen/contracts/generated_message.hpp;
//         data-model Entity 4 / I-9; spec AC-R4 / seam #14.
#include <gtest/gtest.h>

#include <cstddef>
#include <fixpp/core/error.hpp>
#include <fixpp/dict/reify.hpp>
#include <fixpp/wire/message_view_contract.hpp>
#include <fixpp/wire/parser.hpp>  // Framer, pmr_carry_buffer, MessageView (T059)
#include <memory_resource>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

// Generated headers (build-tree only).
#include <fixpp/v42/Reify.hpp>
#include <fixpp/v44/Reify.hpp>

#include "support/reify_test_frame.hpp"  // make_nos_frame (T059)

namespace {

using MV = fixpp::wire::MessageView<fixpp::wire::access_mode::Index>;
using ONOS = fixpp::v44::owning_NewOrderSingle;
using OHB = fixpp::v42::owning_Heartbeat;
using fixpp::test_support::make_nos_frame;

// Reify a NOS into owning_mr; the source frame buffer + arena are built and
// DESTROYED inside, so a returned ONOS that still reads fields proves the
// owning deep-copy survived the source's death (AC-R4 / SC-006).
[[nodiscard]] inline fixpp::core::expected_t<ONOS>
reify_nos(std::pmr::memory_resource* owning_mr) {
    auto buf = make_nos_frame();
    std::pmr::monotonic_buffer_resource frame_mr;
    fixpp::wire::pmr_carry_buffer carry{buf.size(), &frame_mr};
    fixpp::wire::Framer fr{};
    fixpp::wire::frame_view fvs[1]{};
    auto framed = fr.feed(std::span<const std::byte>{buf.data(), buf.size()},
                          carry, std::span<fixpp::wire::frame_view>{fvs, 1});
    if (!framed || framed->empty()) {
        return std::unexpected{fixpp::core::error::dict_reify_wire_body_not_ready};
    }
    MV src{(*framed)[0], &frame_mr};
    return ONOS::from_view(src, owning_mr);
}

// ─────────────────────────────────────────────────────────────────
// Seam #14 compile-time static_asserts (AC-R4 / I-9)
// ─────────────────────────────────────────────────────────────────

// No reference members — a class with a reference member cannot be
// nothrow-move-constructible with a custom (resetting) move.
static_assert(!std::is_reference_v<ONOS>,
              "owning_NewOrderSingle must not itself be a reference type");

// is_nothrow_move_constructible_v — the custom move ctor is noexcept.
static_assert(std::is_nothrow_move_constructible_v<ONOS>,
              "AC-R4/seam#14: owning_<Msg> must be nothrow-move-constructible");
static_assert(std::is_nothrow_move_assignable_v<ONOS>,
              "AC-R4/seam#14: owning_<Msg> must be nothrow-move-assignable");

// Move ctor is NOT trivial (= NOT defaulted): a defaulted move on the optional
// would leave a stale cache aliasing pre-move bytes (R4 risk / I-9). We detect
// this by asserting is_trivially_move_constructible_v is FALSE.
static_assert(!std::is_trivially_move_constructible_v<ONOS>,
              "AC-R4/seam#14: move ctor must NOT be =default (must reset caches)");
static_assert(!std::is_trivially_move_assignable_v<ONOS>,
              "AC-R4/seam#14: move assign must NOT be =default (must reset caches)");

// Same for OHB (v42) — verify the emitter produced correct shapes on v42.
static_assert(std::is_nothrow_move_constructible_v<OHB>);
static_assert(!std::is_trivially_move_constructible_v<OHB>);

}  // namespace

// ─────────────────────────────────────────────────────────────────
// AC-R4 runtime — from_view returns the R6 placeholder error; move semantics
// are verified via the static shape oracle above. Behavioural post-move value
// assertions live in the #if FIXPP_R6_WIRE_BODY_READY guarded block below.
// ─────────────────────────────────────────────────────────────────

TEST(ReifyMoveTest, FromViewSucceeds) {
    // Post-cutover (T059): from_view performs the real owning deep-copy and
    // succeeds. (Was the retired R6 dict_reify_wire_body_not_ready oracle.)
    std::pmr::monotonic_buffer_resource owning_mr;
    auto r = reify_nos(&owning_mr);
    ASSERT_TRUE(r.has_value())
        << "from_view must succeed post-cutover (real owning deep-copy)";
    auto cl = r->cl_ord_id();
    ASSERT_TRUE(cl.has_value());
    EXPECT_EQ(cl.value(), "ORD1");
}

// ─── AC-R4 behavioural: move preserves values + resets caches ────────────────

TEST(ReifyMoveTest, MoveCtorPreservesValues) {
    std::pmr::monotonic_buffer_resource owning_mr;
    auto r = reify_nos(&owning_mr);
    ASSERT_TRUE(r.has_value());
    ONOS moved = std::move(*r);
    auto cl = moved.cl_ord_id();
    ASSERT_TRUE(cl.has_value()) << "moved-to reads parsed values (AC-R4)";
    EXPECT_EQ(cl.value(), "ORD1");
    EXPECT_TRUE(moved.field_value(11).has_value());
}

TEST(ReifyMoveTest, MoveAssignPreservesAndResets) {
    std::pmr::monotonic_buffer_resource mr1;
    std::pmr::monotonic_buffer_resource mr2;
    auto r1 = reify_nos(&mr1);
    auto r2 = reify_nos(&mr2);
    ASSERT_TRUE(r1.has_value());
    ASSERT_TRUE(r2.has_value());
    (void)r1->view();  // prime r1's cache (must be reset on move-assign)
    *r2 = std::move(*r1);
    auto cl = r2->cl_ord_id();
    ASSERT_TRUE(cl.has_value()) << "move-assign target reads parsed values";
    EXPECT_EQ(cl.value(), "ORD1");
    // r1 is moved-from: bytes_ moved out + view_cache_ reset (I-9/AC-R4) →
    // view() rebuilds over empty bytes_ → every field absent (no UB).
    EXPECT_FALSE(r1->cl_ord_id().has_value())
        << "moved-from source must be empty (cache reset, bytes moved out)";
}

TEST(ReifyMoveTest, MovedToCanCallAccessors) {
    std::pmr::monotonic_buffer_resource owning_mr;
    auto r = reify_nos(&owning_mr);
    ASSERT_TRUE(r.has_value());
    ONOS moved = std::move(*r);
    EXPECT_TRUE(moved.cl_ord_id().has_value());     // tag 11 — in frame
    EXPECT_TRUE(moved.field_value(55).has_value());  // tag 55 Symbol — in frame
    EXPECT_FALSE(moved.field_value(54).has_value())  // tag 54 Side — NOT in frame
        << "absent field reports absent (no UB)";
}

TEST(ReifyMoveTest, WhichReturnsVersionVAfterMove) {
    std::pmr::monotonic_buffer_resource owning_mr;
    auto r = reify_nos(&owning_mr);
    ASSERT_TRUE(r.has_value());
    ONOS moved = std::move(*r);
    EXPECT_EQ(moved.which(), fixpp::dict::application_version::v44);
}
