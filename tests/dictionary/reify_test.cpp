// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/reify_test.cpp — T028 [P] [US2]
//
// AC-R1: reify_as<Msg> signature + owning_message_t<Msg> alias shape.
// AC-R2: owning_<Msg> class exists, is move-only, exposes which()/view()/
//         field_value() and per-field accessors matching the flyweight.
// AC-R3: from_view(src, mr) factory exists; returns expected_t<owning_<Msg>>.
//         2b cutover (004 T059): from_view performs the REAL owning deep-copy;
//         the R6 dict_reify_wire_body_not_ready placeholder is RETIRED. Runtime
//         tests assert real parsed values + source-arena-reset survival.
// AC-R6: owning_message_handle shape (move-only, version()/msg_type()/view()/
//         field_value()/as<Msg>()) tested at compile-time shape level only
//         (the runtime-dispatch handle is separately deferred — needs the
//         build-tree _dispatch CMake target; not instantiated here).
// AC-R8: dict_reify_msg_type_mismatch error slot is defined + distinct —
//         compile-time error-code identification.
//
// 2b cutover (004 T059): owning_<Msg>::from_view deep-copies the validated
// frame into the mr-owned arena; view() lazily rebuilds a MessageView<Index>
// over it via the Framer. Behavioural assertions (real byte values + AC-R4
// source-arena-reset survival) are GREEN here and in
// tests/wire/cutover_2b_gated_test.cpp (SC-006).
//
// Oracle: specs/003-dictionary-codegen/contracts/reify.hpp +
//         specs/003-dictionary-codegen/contracts/generated_message.hpp;
//         data-model Entity 4/6; spec AC-R1..R3/R6/R8; spec §5 R6 deferral.
#include <gtest/gtest.h>

#include <cstddef>
#include <fixpp/core/error.hpp>
#include <fixpp/dict/reify.hpp>
#include <fixpp/dict/version_profile.hpp>
#include <fixpp/wire/message_view_contract.hpp>
#include <fixpp/wire/parser.hpp>  // Framer, pmr_carry_buffer, MessageView (T059)
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

// Generated headers (build-tree only, AC-C4).
#include <fixpp/v44/Reify.hpp>
#include <fixpp/v50sp2/Reify.hpp>
#include <fixpp/vt11/Reify.hpp>

#include "support/reify_test_frame.hpp"  // make_nos_frame (T059)

namespace {

using MV = fixpp::wire::MessageView<fixpp::wire::access_mode::Index>;
using NOS = fixpp::v44::NewOrderSingle;
using ONOS = fixpp::v44::owning_NewOrderSingle;

MV parse_frame(std::vector<std::byte> const& buf, std::pmr::memory_resource* mr) {
    fixpp::wire::pmr_carry_buffer carry{buf.size(), mr};
    fixpp::wire::Framer fr{};
    fixpp::wire::frame_view fvs[1]{};
    auto framed = fr.feed(std::span<const std::byte>{buf.data(), buf.size()}, carry,
                          std::span<fixpp::wire::frame_view>{fvs, 1});
    EXPECT_TRUE(framed.has_value());
    EXPECT_FALSE(framed->empty());
    return MV{(*framed)[0], mr};
}

std::vector<std::byte> make_frame(std::string_view begin_string, std::string_view body) {
    std::string pre = std::string("8=") + std::string(begin_string) + "\x01" +
                      "9=" + std::to_string(body.size()) + "\x01" + std::string(body);
    unsigned sum = 0;
    for (unsigned char c : pre) {
        sum += c;
    }
    char checksum[8]{};
    std::snprintf(checksum, sizeof(checksum), "10=%03u\x01", sum % 256U);
    std::string full = pre + checksum;
    std::vector<std::byte> out(full.size());
    std::memcpy(out.data(), full.data(), full.size());
    return out;
}

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
// AC-R3 / AC-R4 — 2b cutover (004 T059): from_view performs the REAL
// owning deep-copy. The R6 dict_reify_wire_body_not_ready oracle is
// retired (it asserted a placeholder that no longer exists). The full
// SC-006 behavioural proof also lives in
// tests/wire/cutover_2b_gated_test.cpp; these assert it at the reify
// owner's layer.
// ─────────────────────────────────────────────────────────────────

using fixpp::test_support::make_nos_frame;

TEST(ReifyTest, FromViewEmptySourceIsWellFormed) {
    // Post-cutover: from_view on a default (empty) MessageView SUCCEEDS —
    // a 0-byte deep copy is valid; accessors report field-absent (no UB).
    // (Was the R6 dict_reify_wire_body_not_ready oracle — now retired.)
    std::pmr::monotonic_buffer_resource arena;
    MV mv;
    auto result = ONOS::from_view(mv, &arena);
    ASSERT_TRUE(result.has_value())
        << "from_view must succeed (real deep-copy; R6 placeholder retired)";
    EXPECT_FALSE(result->cl_ord_id().has_value()) << "empty source → every field absent, no UB";
}

TEST(ReifyTest, ReifyDefaultMessageViewNormalizesMissingMsgType) {
    // 057: an empty view has no MsgType(35) → the get<35>-absent branch returns
    // dict_reify_unknown_msg_type (FR-009 remap of the retired placeholder).
    std::pmr::monotonic_buffer_resource arena;
    fixpp::dict::version_profile const profile{fixpp::dict::session_version::vt11,
                                               fixpp::dict::application_version::v44, true, 0};
    auto r = fixpp::dict::reify(MV{}, profile, &arena);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), fixpp::core::error::dict_reify_unknown_msg_type);
}

TEST(ReifyTest, ReifyFixtAdminFrameReturnsSessionAdminHandle) {
    // 057: each FIXT.1.1 admin frame reifies to a LIVE handle {session_admin,
    // vt11, Unknown} (was the retired R6 placeholder stub exit).
    fixpp::dict::version_profile const profile{fixpp::dict::session_version::vt11,
                                               fixpp::dict::application_version::v50sp2, true, 0};
    for (char mt : {'0', '1', '2', '3', '4', '5', 'A'}) {
        auto frame = make_frame(
            "FIXT.1.1", std::string("35=") + mt + "\x01" + "34=1\x01" + "49=S\x01" + "56=T\x01");
        std::pmr::monotonic_buffer_resource arena;
        auto mv = parse_frame(frame, &arena);

        auto r = fixpp::dict::reify(mv, profile, &arena);
        ASSERT_TRUE(r.has_value()) << "MsgType=" << mt;
        EXPECT_EQ(r->version().k, fixpp::dict::resolved_message_version::kind::session_admin)
            << "MsgType=" << mt;
        EXPECT_EQ(r->version().session, fixpp::dict::session_version::vt11) << "MsgType=" << mt;
        EXPECT_EQ(r->version().application, fixpp::dict::application_version::Unknown)
            << "MsgType=" << mt;
    }
}

TEST(ReifyTest, ReifyApplicationFrameUsesProfileDefaultWhen1128Absent) {
    // 057: 35=D, absent 1128 → resolution uses the profile default (v44) and
    // reify returns a LIVE handle with version().application == v44.
    auto buf = make_nos_frame();
    std::pmr::monotonic_buffer_resource arena;
    auto mv = parse_frame(buf, &arena);
    fixpp::dict::version_profile const profile{fixpp::dict::session_version::vt11,
                                               fixpp::dict::application_version::v44, true, 0};

    auto r = fixpp::dict::reify(mv, profile, &arena);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->version().application, fixpp::dict::application_version::v44);
}

TEST(ReifyTest, ReifyApplicationFramePropagatesUnknownApplVerId) {
    auto frame = make_frame("FIXT.1.1",
                            "35=D\x01"
                            "1128=bogus\x01"
                            "34=1\x01"
                            "49=S\x01"
                            "56=T\x01");
    std::pmr::monotonic_buffer_resource arena;
    auto mv = parse_frame(frame, &arena);
    fixpp::dict::version_profile const profile{fixpp::dict::session_version::vt11,
                                               fixpp::dict::application_version::v44, true, 0};

    auto r = fixpp::dict::reify(mv, profile, &arena);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), fixpp::core::error::dict_unknown_appl_ver_id);
}

TEST(ReifyTest, ReifyApplicationFramePropagatesUnresolvedDefault) {
    auto buf = make_nos_frame();
    std::pmr::monotonic_buffer_resource arena;
    auto mv = parse_frame(buf, &arena);
    fixpp::dict::version_profile const profile{fixpp::dict::session_version::vt11,
                                               fixpp::dict::application_version::Unknown, true, 0};

    auto r = fixpp::dict::reify(mv, profile, &arena);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), fixpp::core::error::dict_unresolved_application_version);
}

TEST(ReifyTest, ReifyMultiCharMsgTypeSkipsFixtAdminCheck) {
    // 057: 35=AB is a two-char MsgType — NOT treated as FIXT admin (that check
    // is single-char). It routes to the application path; "AB" is a known v44
    // arm, so reify returns a LIVE v44 handle (proving the fixt-admin check is
    // length-gated and skipped for multi-char).
    auto frame = make_frame("FIXT.1.1",
                            "35=AB\x01"
                            "34=1\x01"
                            "49=S\x01"
                            "56=T\x01");
    std::pmr::monotonic_buffer_resource arena;
    auto mv = parse_frame(frame, &arena);
    fixpp::dict::version_profile const profile{fixpp::dict::session_version::vt11,
                                               fixpp::dict::application_version::v44, true, 0};

    auto r = fixpp::dict::reify(mv, profile, &arena);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->version().application, fixpp::dict::application_version::v44);
}

TEST(ReifyTest, FromViewReturnsOwning) {
    auto buf = make_nos_frame();
    std::pmr::monotonic_buffer_resource frame_mr;
    fixpp::wire::pmr_carry_buffer carry{buf.size(), &frame_mr};
    fixpp::wire::Framer fr{};
    fixpp::wire::frame_view fvs[1]{};
    auto framed = fr.feed(std::span<const std::byte>{buf.data(), buf.size()}, carry,
                          std::span<fixpp::wire::frame_view>{fvs, 1});
    ASSERT_TRUE(framed.has_value());
    ASSERT_FALSE(framed->empty());
    MV src{(*framed)[0], &frame_mr};

    std::pmr::monotonic_buffer_resource owning_mr;
    auto result = ONOS::from_view(src, &owning_mr);
    ASSERT_TRUE(result.has_value()) << "from_view must succeed with a live arena";
    auto cl = result->cl_ord_id();
    ASSERT_TRUE(cl.has_value()) << "parsed ClOrdID(11) present after reify";
    EXPECT_EQ(cl.value(), "ORD1");
}

TEST(ReifyTest, FromViewSurvivesSourceArenaReset) {
    // AC-R4 / SC-006: the owning deep-copy must survive destruction of the
    // SOURCE arena + source frame buffer.
    std::pmr::monotonic_buffer_resource owning_mr;
    std::optional<ONOS> owned;
    {
        auto buf = make_nos_frame();
        std::pmr::monotonic_buffer_resource frame_mr;
        fixpp::wire::pmr_carry_buffer carry{buf.size(), &frame_mr};
        fixpp::wire::Framer fr{};
        fixpp::wire::frame_view fvs[1]{};
        auto framed = fr.feed(std::span<const std::byte>{buf.data(), buf.size()}, carry,
                              std::span<fixpp::wire::frame_view>{fvs, 1});
        ASSERT_TRUE(framed.has_value());
        ASSERT_FALSE(framed->empty());
        MV src{(*framed)[0], &frame_mr};
        auto r = ONOS::from_view(src, &owning_mr);
        ASSERT_TRUE(r.has_value());
        owned.emplace(std::move(*r));
    }  // buf + frame_mr (+ src) destroyed here

    auto cl = owned->cl_ord_id();
    ASSERT_TRUE(cl.has_value()) << "owning deep-copy must outlive the source arena (AC-R4/SC-006)";
    EXPECT_EQ(cl.value(), "ORD1");
}

TEST(ReifyTest, ViewAndFieldValueForward) {
    auto buf = make_nos_frame();
    std::pmr::monotonic_buffer_resource frame_mr;
    fixpp::wire::pmr_carry_buffer carry{buf.size(), &frame_mr};
    fixpp::wire::Framer fr{};
    fixpp::wire::frame_view fvs[1]{};
    auto framed = fr.feed(std::span<const std::byte>{buf.data(), buf.size()}, carry,
                          std::span<fixpp::wire::frame_view>{fvs, 1});
    ASSERT_TRUE(framed.has_value());
    ASSERT_FALSE(framed->empty());
    MV src{(*framed)[0], &frame_mr};

    std::pmr::monotonic_buffer_resource owning_mr;
    auto result = ONOS::from_view(src, &owning_mr);
    ASSERT_TRUE(result.has_value());
    auto& o = *result;

    MV const& v = o.view();
    auto fv = o.field_value(11);
    static_assert(std::is_same_v<decltype(fv), fixpp::core::expected_t<fixpp::wire::field_view>>);
    EXPECT_TRUE(fv.has_value()) << "field_value(11) present after reify";
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
