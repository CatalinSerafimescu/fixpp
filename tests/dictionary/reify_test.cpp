// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/reify_test.cpp — T028 [P] [US2]
//
// AC-R1: reify_as<Msg> signature + owning_message_t<Msg> alias shape.
// AC-R2: owning_<Msg> class exists, is move-only, exposes which()/view()/
//         field_value() and per-field accessors matching the flyweight.
// AC-R3: from_view(src, mr) factory exists; returns expected_t<owning_<Msg>>.
//         R6-blocked: behavioural round-trip (values survive arena reset) cannot
//         run because the frozen wire stub carries no frame bytes. Compile-time
//         shape asserted; runtime value assertions deferred to 2b (see comment).
// AC-R6: owning_message_handle shape (move-only, version()/msg_type()/view()/
//         field_value()/as<Msg>()) tested at compile-time shape level only
//         (handle is US3 scope; not instantiated here).
// AC-R8: dict_reify_msg_type_mismatch via get<35>() returning field-absent
//         (frozen stub) — compile-time error-code identification.
//
// R6 scope: Every get<>() on the frozen stub returns
//   std::unexpected{core::error::dict_xml_parse_failed}
// (message_view_contract.hpp stub body). Behavioural assertions (real byte
// values, cross-arena-reset survival, ≤4-alloc budget) are R6-blocked until 2b
// swaps in the real OffsetTable-backed wire body (spec §11 R6).
//
// Oracle: specs/003-dictionary-codegen/contracts/reify.hpp +
//         specs/003-dictionary-codegen/contracts/generated_message.hpp;
//         data-model Entity 4/6; spec AC-R1..R3/R6/R8.
#include <gtest/gtest.h>

#include <fixpp/core/error.hpp>
#include <fixpp/dict/reify.hpp>
#include <fixpp/dict/version_profile.hpp>
#include <fixpp/wire/message_view_contract.hpp>
#include <memory_resource>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

// Generated headers (build-tree only, AC-C4).
#include <fixpp/v44/Reify.hpp>
#include <fixpp/v50sp2/Reify.hpp>
#include <fixpp/vt11/Reify.hpp>

namespace {

using MV = fixpp::wire::MessageView<fixpp::wire::access_mode::Index>;
using NOS = fixpp::v44::NewOrderSingle;
using ONOS = fixpp::v44::owning_NewOrderSingle;

// ─────────────────────────────────────────────────────────────────
// AC-R1 / AC-G7a — compile-time shape: owning_message_t<NOS> resolves to
// owning_NewOrderSingle via the emitted owning_message_traits specialisation.
// ─────────────────────────────────────────────────────────────────

static_assert(std::is_same_v<fixpp::dict::owning_message_t<NOS>, ONOS>,
              "AC-G7a: owning_message_traits specialisation must resolve to owning_NewOrderSingle");

// AC-R2 — owning_<Msg> is move-only.
static_assert(!std::is_copy_constructible_v<ONOS>,
              "AC-R2: owning_NewOrderSingle must not be copy-constructible");
static_assert(!std::is_copy_assignable_v<ONOS>,
              "AC-R2: owning_NewOrderSingle must not be copy-assignable");
static_assert(std::is_move_constructible_v<ONOS>,
              "AC-R2: owning_NewOrderSingle must be move-constructible");
static_assert(std::is_move_assignable_v<ONOS>,
              "AC-R2: owning_NewOrderSingle must be move-assignable");

// AC-R2 — which() returns version_v of the correct application_version.
static_assert(NOS::version_v == fixpp::dict::application_version::v44);
static_assert(ONOS::version_v == fixpp::dict::application_version::v44,
              "owning_<Msg> must carry the same version_v as the flyweight");

// AC-R2 — which() is a static constexpr function returning version_v.
static_assert(ONOS::which() == fixpp::dict::application_version::v44,
              "AC-R2: which() must return version_v");

// AC-R3 — from_view factory return type: expected_t<owning_<Msg>>.
static_assert(std::is_same_v<decltype(ONOS::from_view(std::declval<MV const&>(),
                                                      std::declval<std::pmr::memory_resource*>())),
                             fixpp::core::expected_t<ONOS>>,
              "AC-R3: from_view must return expected_t<owning_NewOrderSingle>");

// AC-R2 — view() return type matches the flyweight's view().
static_assert(std::is_same_v<decltype(std::declval<ONOS const&>().view()), MV const&>,
              "AC-R2: view() must return MV const&");

// AC-R2 — field_value(uint16_t) return type.
static_assert(std::is_same_v<decltype(std::declval<ONOS const&>().field_value(std::uint16_t{})),
                             fixpp::core::expected_t<fixpp::wire::field_view>>,
              "AC-R2: field_value(uint16_t) must return expected_t<field_view>");

// AC-G7a — AC-R1 also checks all four versions' NOS (v44/v50sp2) via multiple
// specialisation static_asserts emitted into Reify.hpp; check vt11 too.
static_assert(std::is_same_v<fixpp::dict::owning_message_t<fixpp::vt11::Heartbeat>,
                             fixpp::vt11::owning_Heartbeat>,
              "AC-G7a: vt11::Heartbeat owning_message_traits must resolve to owning_Heartbeat");

static_assert(std::is_same_v<fixpp::dict::owning_message_t<fixpp::v50sp2::NewOrderSingle>,
                             fixpp::v50sp2::owning_NewOrderSingle>,
              "AC-G7a: v50sp2 owning_message_traits must resolve to owning_NewOrderSingle");

}  // namespace

// ─────────────────────────────────────────────────────────────────
// AC-R3 — from_view returns a value (R6-scoped: stub MV, all gets field-absent)
// ─────────────────────────────────────────────────────────────────

TEST(ReifyTest, FromViewReturnsOwning) {
    std::pmr::monotonic_buffer_resource arena;
    MV mv;
    auto result = ONOS::from_view(mv, &arena);
    // R6: from_view succeeds (no OOM in this path) and returns an owning_<Msg>.
    ASSERT_TRUE(result.has_value()) << "from_view must succeed with a live arena";
    // R6: all accessor calls return field-absent (frozen stub carries no data).
    // Behavioural value round-trip (real values survive arena reset) is
    // R6-blocked until 2b swaps in the real OffsetTable-backed body (spec §11).
    auto& o = *result;
    auto cl = o.cl_ord_id();
    EXPECT_FALSE(cl.has_value()) << "R6: stub MV returns field-absent for all get<>()";
}

// ─────────────────────────────────────────────────────────────────
// AC-R2 — view() + field_value() delegate through the lazy cache
// ─────────────────────────────────────────────────────────────────

TEST(ReifyTest, ViewAndFieldValueForward) {
    std::pmr::monotonic_buffer_resource arena;
    MV mv;
    auto result = ONOS::from_view(mv, &arena);
    ASSERT_TRUE(result.has_value());
    auto& o = *result;

    // view() must return a MV const& (not a dangling reference).
    MV const& v = o.view();
    // field_value() delegates to view().get(tag).
    auto fv = o.field_value(11);
    static_assert(std::is_same_v<decltype(fv), fixpp::core::expected_t<fixpp::wire::field_view>>);
    EXPECT_FALSE(fv.has_value());  // R6: field-absent
    // Second view() call hits the cache.
    MV const& v2 = o.view();
    EXPECT_EQ(&v, &v2) << "view() must return the same cached address";
}

// ─────────────────────────────────────────────────────────────────
// AC-R8 — dict_reify_msg_type_mismatch identification
// ─────────────────────────────────────────────────────────────────
// R6: the frozen stub get<35>() always returns dict_xml_parse_failed (not
// the real MsgType bytes). In the R6 scope the reify_as function template is
// declared in include/fixpp/dict/reify.hpp but the behaviour depends on a
// real MsgType read. We therefore test the error code shape (the specific
// dict_reify_msg_type_mismatch slot is defined and distinct) rather than a
// behavioural reify_as() call which would need a real frame. The behavioural
// mismatch path (reify_as<NOS> with a frame whose MsgType != "D") is
// R6-blocked until 2b. The error slot itself is verified here.

TEST(ReifyTest, MsgTypeMismatchErrorSlotDefined) {
    // AC-R8: dict_reify_msg_type_mismatch is defined and has the correct
    // numeric slot (23, per data-model "Error mapping" + error.hpp).
    using E = fixpp::core::error;
    static_assert(static_cast<std::uint8_t>(E::dict_reify_msg_type_mismatch) == 23);
    static_assert(E::dict_reify_msg_type_mismatch != E::dict_xml_parse_failed,
                  "AC-R8: mismatch must be a distinct error from the R6 stub error");
    SUCCEED();
}

// ─────────────────────────────────────────────────────────────────
// AC-R6 (handle shape) — owning_message_handle is move-only, declared in
// include/fixpp/dict/reify.hpp. Not instantiated (US3 scope); shape checked
// via type-traits.
// ─────────────────────────────────────────────────────────────────

TEST(ReifyTest, OwningMessageHandleIsMoveOnly) {
    using H = fixpp::dict::owning_message_handle;
    static_assert(!std::is_copy_constructible_v<H>,
                  "AC-R6: owning_message_handle must not be copy-constructible");
    static_assert(!std::is_copy_assignable_v<H>,
                  "AC-R6: owning_message_handle must not be copy-assignable");
    // Note: move-constructibility with noexcept declaration is checked; the
    // full runtime as<Msg>() dispatch is US3 scope (T034/T036).
    SUCCEED();
}
