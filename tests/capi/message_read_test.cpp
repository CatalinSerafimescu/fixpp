// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/capi/message_read_test.cpp — CA-008 (T005) + CA-010-read (T012)
//
// TDD: this file is written RED before T006/T013 implement the bodies.
//
// Strategy: hand-built parsed frames (not loopback) per advisor recommendation
// (D-11 / offset_table_test.cpp pattern). Tests wrap a real
// wire::MessageView<Index> in a stack fixpp_msg and cast to fixpp_msg_t* — the
// same approach used by recv_alloc_guard_test.cpp. The approach:
//   1. Build a wire frame string with make_raw_frame().
//   2. Parse with frame_view_factory + Parser<Index>{dict}.
//   3. Wrap the resulting MessageView in a stack fixpp_msg{}.
//   4. Pass reinterpret_cast<const fixpp_msg_t*>(&h) to the C-ABI function.
//
// SC-003 alloc guard: mallocnesia LD_PRELOAD gate via alloc_guard_markers.hpp
// (read path must be zero global-heap; warm-up outside the window).

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory_resource>
#include <string>
#include <string_view>
#include <vector>

// C-ABI under test
#include "fix/c_api/message.h"
#include "fix/c_api/error.h"

// Engine-internal concrete fixpp_msg definition (test-only access)
#include "capi_internal.hpp"

// Wire surface
#include <fixpp/wire/parser.hpp>

// Test support
#include "support/alloc_guard_markers.hpp"
#include "support/frame_view_factory.hpp"
#include "support/mock_dict_table.hpp"  // has group_member_tags interface
#include <fixpp/dict/table_view.hpp>

using fixpp::wire::MessageView;
using fixpp::wire::access_mode;
using fixpp::wire::Parser;

namespace {

// Build a structurally-valid FIX.4.4 frame around `body`.
// OffsetTable scans bytes; it does not verify the checksum.
std::vector<std::byte> make_raw_frame(std::string const& body) {
    std::string nine = "9=" + std::to_string(body.size()) + "\x01";
    std::string full = "8=FIX.4.4\x01" + nine + body + "10=000\x01";
    std::vector<std::byte> out(full.size());
    std::memcpy(out.data(), full.data(), full.size());
    return out;
}

// Wrap a stack MessageView<Index> as a fixpp_msg_t for the C-ABI calls.
// The fixpp_msg is inbound-flavour: view != nullptr, accumulator == nullptr,
// token is default-constructed (expired) — reads are allowed, writes → INVALID_HANDLE.
struct InboundHandle {
    fixpp_msg msg{};
    const fixpp_msg_t* ptr() const noexcept {
        return reinterpret_cast<const fixpp_msg_t*>(&msg);
    }
};

// ── US1 (T005) ─────────────────────────────────────────────────────────────

TEST(MessageRead, NullHandleReturnsNullHandle) {
    // NULL msg pointer
    const char* out = nullptr;
    size_t len = 0;
    EXPECT_EQ(fixpp_msg_get_string(nullptr, 35, &out, &len), FIXPP_ERR_NULL_HANDLE);
    EXPECT_EQ(fixpp_msg_get_msg_type(nullptr, &out, &len), FIXPP_ERR_NULL_HANDLE);

    int64_t iv = 0;
    EXPECT_EQ(fixpp_msg_get_int(nullptr, 34, &iv), FIXPP_ERR_NULL_HANDLE);

    double dv = 0.0;
    EXPECT_EQ(fixpp_msg_get_double(nullptr, 44, &dv), FIXPP_ERR_NULL_HANDLE);

    fixpp_decimal_t dec{};
    EXPECT_EQ(fixpp_msg_get_decimal(nullptr, 44, &dec), FIXPP_ERR_NULL_HANDLE);

    bool present = false;
    EXPECT_EQ(fixpp_msg_has_tag(nullptr, 35, &present), FIXPP_ERR_NULL_HANDLE);

    fixpp_resolved_msg_version_t ver{};
    EXPECT_EQ(fixpp_msg_version(nullptr, &ver), FIXPP_ERR_NULL_HANDLE);

    const uint8_t* bp = nullptr;
    EXPECT_EQ(fixpp_msg_get_bytes(nullptr, 35, &bp, &len), FIXPP_ERR_NULL_HANDLE);
}

TEST(MessageRead, NullOutPointerReturnsNullHandle) {
    auto buf = make_raw_frame("35=D\x01" "49=SENDER\x01" "56=TARGET\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    std::pmr::monotonic_buffer_resource arena;
    auto mv_res = [&]() {
        MessageView<access_mode::Index> mv{*fv, &arena};
        return mv;
    }();

    InboundHandle h;
    h.msg.view = &mv_res;

    EXPECT_EQ(fixpp_msg_get_string(h.ptr(), 49, nullptr, nullptr), FIXPP_ERR_NULL_HANDLE);
}

TEST(MessageRead, DestroyedHandleReturnsInvalidHandle) {
    // A handle with tag_ == FIXPP_HANDLE_TAG_DEAD
    fixpp_msg dead{};
    dead.tag_ = FIXPP_HANDLE_TAG_DEAD;
    auto* p = reinterpret_cast<const fixpp_msg_t*>(&dead);

    const char* out = nullptr;
    size_t len = 0;
    EXPECT_EQ(fixpp_msg_get_string(p, 35, &out, &len), FIXPP_ERR_INVALID_HANDLE);
}

TEST(MessageRead, OutboundFlavorGetStringReturnsInvalidHandle) {
    // Inbound-flavour with view==nullptr means it looks like a bad handle
    fixpp_msg outbound_ish{};
    outbound_ish.flavour = FixppMsgFlavour::outbound;
    outbound_ish.view = nullptr;
    auto* p = reinterpret_cast<const fixpp_msg_t*>(&outbound_ish);

    const char* out = nullptr;
    size_t len = 0;
    // view==nullptr → INVALID_HANDLE (no view to read from)
    EXPECT_EQ(fixpp_msg_get_string(p, 35, &out, &len), FIXPP_ERR_INVALID_HANDLE);
}

TEST(MessageRead, GetMsgType) {
    auto buf = make_raw_frame("35=D\x01" "49=SENDER\x01" "56=TARGET\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    std::pmr::monotonic_buffer_resource arena;
    MessageView<access_mode::Index> mv{*fv, &arena};

    InboundHandle h;
    h.msg.view = &mv;

    const char* out = nullptr;
    size_t len = 0;
    ASSERT_EQ(fixpp_msg_get_msg_type(h.ptr(), &out, &len), FIXPP_ERR_OK);
    ASSERT_NE(out, nullptr);
    EXPECT_EQ(std::string_view(out, len), "D");
}

TEST(MessageRead, GetStringPresent) {
    auto buf = make_raw_frame("35=D\x01" "49=MYSENDER\x01" "56=TARGET\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    std::pmr::monotonic_buffer_resource arena;
    MessageView<access_mode::Index> mv{*fv, &arena};

    InboundHandle h;
    h.msg.view = &mv;

    const char* out = nullptr;
    size_t len = 0;
    ASSERT_EQ(fixpp_msg_get_string(h.ptr(), 49, &out, &len), FIXPP_ERR_OK);
    ASSERT_NE(out, nullptr);
    EXPECT_EQ(std::string_view(out, len), "MYSENDER");

    // Alias check: the returned pointer must lie within the wire buffer
    const auto* buf_start = reinterpret_cast<const char*>(buf.data());
    const auto* buf_end   = buf_start + buf.size();
    EXPECT_GE(out, buf_start) << "string must alias wire buffer (not a copy)";
    EXPECT_LT(out, buf_end)   << "string must alias wire buffer (not a copy)";
}

TEST(MessageRead, GetBytesPresent) {
    auto buf = make_raw_frame("35=D\x01" "49=MYSENDER\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    std::pmr::monotonic_buffer_resource arena;
    MessageView<access_mode::Index> mv{*fv, &arena};

    InboundHandle h;
    h.msg.view = &mv;

    const uint8_t* bp = nullptr;
    size_t len = 0;
    ASSERT_EQ(fixpp_msg_get_bytes(h.ptr(), 49, &bp, &len), FIXPP_ERR_OK);
    ASSERT_NE(bp, nullptr);
    EXPECT_EQ(len, 8U);  // "MYSENDER"
    EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(bp), len), "MYSENDER");

    // Alias check
    const auto* buf_start = reinterpret_cast<const uint8_t*>(buf.data());
    const auto* buf_end   = buf_start + buf.size();
    EXPECT_GE(bp, buf_start);
    EXPECT_LT(bp, buf_end);
}

TEST(MessageRead, GetStringAbsentTagNotFound) {
    auto buf = make_raw_frame("35=D\x01" "49=SENDER\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    std::pmr::monotonic_buffer_resource arena;
    MessageView<access_mode::Index> mv{*fv, &arena};

    InboundHandle h;
    h.msg.view = &mv;

    const char* out = nullptr;
    size_t len = 0;
    EXPECT_EQ(fixpp_msg_get_string(h.ptr(), 56, &out, &len), FIXPP_ERR_TAG_NOT_FOUND);
}

TEST(MessageRead, GetIntPresent) {
    // tag 34 = MsgSeqNum (integer)
    auto buf = make_raw_frame("35=D\x01" "34=42\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    std::pmr::monotonic_buffer_resource arena;
    MessageView<access_mode::Index> mv{*fv, &arena};

    InboundHandle h;
    h.msg.view = &mv;

    int64_t val = 0;
    ASSERT_EQ(fixpp_msg_get_int(h.ptr(), 34, &val), FIXPP_ERR_OK);
    EXPECT_EQ(val, 42);
}

TEST(MessageRead, GetIntNegative) {
    auto buf = make_raw_frame("35=D\x01" "38=-7\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    std::pmr::monotonic_buffer_resource arena;
    MessageView<access_mode::Index> mv{*fv, &arena};

    InboundHandle h;
    h.msg.view = &mv;

    int64_t val = 0;
    ASSERT_EQ(fixpp_msg_get_int(h.ptr(), 38, &val), FIXPP_ERR_OK);
    EXPECT_EQ(val, -7);
}

TEST(MessageRead, GetIntAbsent) {
    auto buf = make_raw_frame("35=D\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    std::pmr::monotonic_buffer_resource arena;
    MessageView<access_mode::Index> mv{*fv, &arena};

    InboundHandle h;
    h.msg.view = &mv;

    int64_t val = 0;
    EXPECT_EQ(fixpp_msg_get_int(h.ptr(), 34, &val), FIXPP_ERR_TAG_NOT_FOUND);
}

TEST(MessageRead, GetIntNonNumericWireInvalidFrame) {
    // tag 34 contains non-numeric bytes
    auto buf = make_raw_frame("35=D\x01" "34=NOTNUM\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    std::pmr::monotonic_buffer_resource arena;
    MessageView<access_mode::Index> mv{*fv, &arena};

    InboundHandle h;
    h.msg.view = &mv;

    int64_t val = 0;
    EXPECT_EQ(fixpp_msg_get_int(h.ptr(), 34, &val), FIXPP_ERR_WIRE_INVALID_FRAME);
}

TEST(MessageRead, GetDoublePresent) {
    // tag 44 = Price (float/double)
    auto buf = make_raw_frame("35=D\x01" "44=12.50\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    std::pmr::monotonic_buffer_resource arena;
    MessageView<access_mode::Index> mv{*fv, &arena};

    InboundHandle h;
    h.msg.view = &mv;

    double val = 0.0;
    ASSERT_EQ(fixpp_msg_get_double(h.ptr(), 44, &val), FIXPP_ERR_OK);
    EXPECT_NEAR(val, 12.50, 1e-9);
}

TEST(MessageRead, GetDoubleNonNumericWireInvalidFrame) {
    auto buf = make_raw_frame("35=D\x01" "44=NOTNUM\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    std::pmr::monotonic_buffer_resource arena;
    MessageView<access_mode::Index> mv{*fv, &arena};

    InboundHandle h;
    h.msg.view = &mv;

    double val = 0.0;
    EXPECT_EQ(fixpp_msg_get_double(h.ptr(), 44, &val), FIXPP_ERR_WIRE_INVALID_FRAME);
}

TEST(MessageRead, GetDecimalPresent) {
    // tag 44 = Price; use a simple decimal value
    auto buf = make_raw_frame("35=D\x01" "44=1.23\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    std::pmr::monotonic_buffer_resource arena;
    MessageView<access_mode::Index> mv{*fv, &arena};

    InboundHandle h;
    h.msg.view = &mv;

    fixpp_decimal_t val{};
    ASSERT_EQ(fixpp_msg_get_decimal(h.ptr(), 44, &val), FIXPP_ERR_OK);
    // Verify by formatting back: 1.23 = mantissa 123, exponent -2
    // Use fixpp_decimal_format to check
    char fbuf[64];
    size_t written = 0;
    ASSERT_EQ(fixpp_decimal_format(val, fbuf, sizeof(fbuf), &written), FIXPP_ERR_OK);
    EXPECT_EQ(std::string_view(fbuf, written), "1.23");
}

TEST(MessageRead, GetDecimalAbsent) {
    auto buf = make_raw_frame("35=D\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    std::pmr::monotonic_buffer_resource arena;
    MessageView<access_mode::Index> mv{*fv, &arena};

    InboundHandle h;
    h.msg.view = &mv;

    fixpp_decimal_t val{};
    EXPECT_EQ(fixpp_msg_get_decimal(h.ptr(), 44, &val), FIXPP_ERR_TAG_NOT_FOUND);
}

TEST(MessageRead, HasTagPresentAndAbsent) {
    auto buf = make_raw_frame("35=D\x01" "49=SENDER\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    std::pmr::monotonic_buffer_resource arena;
    MessageView<access_mode::Index> mv{*fv, &arena};

    InboundHandle h;
    h.msg.view = &mv;

    bool present = false;
    ASSERT_EQ(fixpp_msg_has_tag(h.ptr(), 49, &present), FIXPP_ERR_OK);
    EXPECT_TRUE(present);

    present = true;
    ASSERT_EQ(fixpp_msg_has_tag(h.ptr(), 56, &present), FIXPP_ERR_OK);
    EXPECT_FALSE(present);
}

TEST(MessageRead, Version) {
    // tag 8 = BeginString; parse a frame with it in the wire buffer
    auto buf = make_raw_frame("35=D\x01" "49=S\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    std::pmr::monotonic_buffer_resource arena;
    MessageView<access_mode::Index> mv{*fv, &arena};

    InboundHandle h;
    h.msg.view = &mv;

    fixpp_resolved_msg_version_t ver{};
    ASSERT_EQ(fixpp_msg_version(h.ptr(), &ver), FIXPP_ERR_OK);
    // begin_string should be "FIX.4.4" (from the frame header)
    ASSERT_NE(ver.begin_string, nullptr);
    EXPECT_EQ(std::string_view(ver.begin_string, ver.begin_string_len), "FIX.4.4");
    // No appl_ver_id (tag 1137) in this frame
    EXPECT_EQ(ver.appl_ver_id, nullptr);
    EXPECT_EQ(ver.appl_ver_id_len, 0U);
}

// SC-003 alloc guard: the read path must not allocate on the global heap.
// Uses the mallocnesia LD_PRELOAD dual-gate via alloc_guard_markers.hpp.
// Without the preload the markers no-op (test still validates correctness).
TEST(MessageRead, ZeroGlobalHeapAllocGuard) {
    auto buf = make_raw_frame("35=D\x01" "49=SENDER\x01" "56=TARGET\x01" "34=42\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    std::pmr::monotonic_buffer_resource arena;
    MessageView<access_mode::Index> mv{*fv, &arena};

    InboundHandle h;
    h.msg.view = &mv;

    // Warm-up: prime any lazy-init caches outside the measured window
    for (int i = 0; i < 8; ++i) {
        const char* out = nullptr; size_t len = 0;
        (void)fixpp_msg_get_string(h.ptr(), 49, &out, &len);
        int64_t iv = 0;
        (void)fixpp_msg_get_int(h.ptr(), 34, &iv);
    }

    if (alloc_guard_start) alloc_guard_start();
    for (int i = 0; i < 1000; ++i) {
        const char* out = nullptr; size_t len = 0;
        ASSERT_EQ(fixpp_msg_get_string(h.ptr(), 49, &out, &len), FIXPP_ERR_OK);

        int64_t iv = 0;
        ASSERT_EQ(fixpp_msg_get_int(h.ptr(), 34, &iv), FIXPP_ERR_OK);

        bool present = false;
        ASSERT_EQ(fixpp_msg_has_tag(h.ptr(), 56, &present), FIXPP_ERR_OK);

        const char* mt = nullptr; size_t mtl = 0;
        ASSERT_EQ(fixpp_msg_get_msg_type(h.ptr(), &mt, &mtl), FIXPP_ERR_OK);
    }
    if (alloc_guard_end) alloc_guard_end();
}

// ── US3 (T012): repeating-group read tests ────────────────────────────────

// A minimal dictionary with one group: 453=NoPartyIDs, delimiter=448 (PartyID),
// member 447 (PartyIDSource).
fixpp::dict::table_view make_group_dict() {
    fixpp::dict::table_view dict;
    dict.add_valid("D", 35)
        .add_valid("D", 34)
        .add_valid("D", 49)
        .add_valid("D", 453)
        .add_valid("D", 448)
        .add_valid("D", 447)
        .set_group_first(453, 448)
        .add_group_member(453, 447);
    return dict;
}

// Nested group: within each 453 instance, nest a sub-group under tag 539
// (NoNestedPartyIDs), delimiter 524 (NestedPartyID), member 525.
fixpp::dict::table_view make_nested_group_dict() {
    fixpp::dict::table_view dict;
    dict.add_valid("D", 35)
        .add_valid("D", 34)
        .add_valid("D", 453)
        .add_valid("D", 448)
        .add_valid("D", 447)
        .add_valid("D", 539)
        .add_valid("D", 524)
        .add_valid("D", 525)
        .set_group_first(453, 448)
        .add_group_member(453, 447)
        .add_group_member(453, 539)
        .add_group_member(453, 524)
        .add_group_member(453, 525)
        .set_group_first(539, 524)
        .add_group_member(539, 525);
    return dict;
}

TEST(MessageReadGroup, GetGroupCount) {
    auto dict = make_group_dict();
    auto buf = make_raw_frame(
        "35=D\x01"
        "34=1\x01"
        "453=2\x01"
        "448=PA\x01"
        "447=D\x01"
        "448=PB\x01"
        "447=D\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    Parser<access_mode::Index> parser{dict};
    auto mv_res = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv_res.has_value());

    InboundHandle h;
    h.msg.view = &mv_res.value();

    const fixpp_group_t* grp = nullptr;
    size_t count = 0;
    ASSERT_EQ(fixpp_msg_get_group(h.ptr(), 453, &grp, &count), FIXPP_ERR_OK);
    EXPECT_EQ(count, 2U);
    EXPECT_NE(grp, nullptr);
}

TEST(MessageReadGroup, GetGroupEntryFirstAndLast) {
    auto dict = make_group_dict();
    auto buf = make_raw_frame(
        "35=D\x01"
        "34=1\x01"
        "453=2\x01"
        "448=PA\x01"
        "447=D\x01"
        "448=PB\x01"
        "447=E\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    Parser<access_mode::Index> parser{dict};
    auto mv_res = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv_res.has_value());

    InboundHandle h;
    h.msg.view = &mv_res.value();

    const fixpp_group_t* grp = nullptr;
    size_t count = 0;
    ASSERT_EQ(fixpp_msg_get_group(h.ptr(), 453, &grp, &count), FIXPP_ERR_OK);
    ASSERT_EQ(count, 2U);

    // entry [0]: tag 448 == "PA", tag 447 == "D"
    const char* val = nullptr; size_t vlen = 0;
    ASSERT_EQ(fixpp_group_get_field_string(grp, 0, 448, &val, &vlen), FIXPP_ERR_OK);
    EXPECT_EQ(std::string_view(val, vlen), "PA");

    ASSERT_EQ(fixpp_group_get_field_string(grp, 0, 447, &val, &vlen), FIXPP_ERR_OK);
    EXPECT_EQ(std::string_view(val, vlen), "D");

    // entry [1] (last): tag 448 == "PB", tag 447 == "E"
    ASSERT_EQ(fixpp_group_get_field_string(grp, 1, 448, &val, &vlen), FIXPP_ERR_OK);
    EXPECT_EQ(std::string_view(val, vlen), "PB");

    ASSERT_EQ(fixpp_group_get_field_string(grp, 1, 447, &val, &vlen), FIXPP_ERR_OK);
    EXPECT_EQ(std::string_view(val, vlen), "E");
}

TEST(MessageReadGroup, IndexOutOfRangeReturnsError) {
    auto dict = make_group_dict();
    auto buf = make_raw_frame(
        "35=D\x01"
        "453=1\x01"
        "448=PA\x01"
        "447=D\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    Parser<access_mode::Index> parser{dict};
    auto mv_res = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv_res.has_value());

    InboundHandle h;
    h.msg.view = &mv_res.value();

    const fixpp_group_t* grp = nullptr;
    size_t count = 0;
    ASSERT_EQ(fixpp_msg_get_group(h.ptr(), 453, &grp, &count), FIXPP_ERR_OK);
    ASSERT_EQ(count, 1U);

    const char* val = nullptr; size_t vlen = 0;
    // i == count → INDEX_OUT_OF_RANGE
    EXPECT_EQ(fixpp_group_get_field_string(grp, 1, 448, &val, &vlen),
              FIXPP_ERR_INDEX_OUT_OF_RANGE);
    // i > count → INDEX_OUT_OF_RANGE
    EXPECT_EQ(fixpp_group_get_field_string(grp, 99, 448, &val, &vlen),
              FIXPP_ERR_INDEX_OUT_OF_RANGE);
}

TEST(MessageReadGroup, AbsentFieldInEntryReturnsTagNotFound) {
    auto dict = make_group_dict();
    auto buf = make_raw_frame(
        "35=D\x01"
        "453=1\x01"
        "448=PA\x01"
        "447=D\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    Parser<access_mode::Index> parser{dict};
    auto mv_res = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv_res.has_value());

    InboundHandle h;
    h.msg.view = &mv_res.value();

    const fixpp_group_t* grp = nullptr;
    size_t count = 0;
    ASSERT_EQ(fixpp_msg_get_group(h.ptr(), 453, &grp, &count), FIXPP_ERR_OK);
    ASSERT_EQ(count, 1U);

    // tag 55 (Symbol) is absent from the group entry
    const char* val = nullptr; size_t vlen = 0;
    EXPECT_EQ(fixpp_group_get_field_string(grp, 0, 55, &val, &vlen), FIXPP_ERR_TAG_NOT_FOUND);
}

TEST(MessageReadGroup, AbsentGroupReturnsTagNotFound) {
    // No 453 in the frame
    auto buf = make_raw_frame("35=D\x01" "49=S\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    auto dict = make_group_dict();
    std::pmr::monotonic_buffer_resource arena;
    Parser<access_mode::Index> parser{dict};
    auto mv_res = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv_res.has_value());

    InboundHandle h;
    h.msg.view = &mv_res.value();

    const fixpp_group_t* grp = nullptr;
    size_t count = 0;
    EXPECT_EQ(fixpp_msg_get_group(h.ptr(), 453, &grp, &count), FIXPP_ERR_TAG_NOT_FOUND);
}

TEST(MessageReadGroup, NonGroupTagReturnsTypeMismatch) {
    // tag 49 (SenderCompID) is a scalar, not a group tag
    auto dict = make_group_dict();
    auto buf = make_raw_frame("35=D\x01" "49=SENDER\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    Parser<access_mode::Index> parser{dict};
    auto mv_res = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv_res.has_value());

    InboundHandle h;
    h.msg.view = &mv_res.value();

    const fixpp_group_t* grp = nullptr;
    size_t count = 0;
    // 49 is present as a scalar — classify_fn says it is valid for "D",
    // but group_slices returns empty because 49 is not a group delimiter.
    // The implementation must return TYPE_MISMATCH when the tag is known
    // valid but is not a group (present in offsets but no group slices
    // because it is not a NoXxx field).
    EXPECT_EQ(fixpp_msg_get_group(h.ptr(), 49, &grp, &count), FIXPP_ERR_TYPE_MISMATCH);
}

TEST(MessageReadGroup, GetGroupIntAndDouble) {
    // Build a group with a numeric tag
    fixpp::dict::table_view dict;
    dict.add_valid("D", 35)
        .add_valid("D", 453)
        .add_valid("D", 448)
        .add_valid("D", 15)   // Currency (string), used as numeric for test
        .add_valid("D", 44)   // Price (double)
        .add_valid("D", 38)   // OrderQty (int)
        .set_group_first(453, 448)
        .add_group_member(453, 15)
        .add_group_member(453, 44)
        .add_group_member(453, 38);

    auto buf = make_raw_frame(
        "35=D\x01"
        "453=1\x01"
        "448=PA\x01"
        "15=USD\x01"
        "44=10.50\x01"
        "38=100\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    Parser<access_mode::Index> parser{dict};
    auto mv_res = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv_res.has_value());

    InboundHandle h;
    h.msg.view = &mv_res.value();

    const fixpp_group_t* grp = nullptr;
    size_t count = 0;
    ASSERT_EQ(fixpp_msg_get_group(h.ptr(), 453, &grp, &count), FIXPP_ERR_OK);
    ASSERT_EQ(count, 1U);

    int64_t qty = 0;
    ASSERT_EQ(fixpp_group_get_field_int(grp, 0, 38, &qty), FIXPP_ERR_OK);
    EXPECT_EQ(qty, 100);

    double price = 0.0;
    ASSERT_EQ(fixpp_group_get_field_double(grp, 0, 44, &price), FIXPP_ERR_OK);
    EXPECT_NEAR(price, 10.50, 1e-9);
}

TEST(MessageReadGroup, GetGroupDecimal) {
    fixpp::dict::table_view dict;
    dict.add_valid("D", 35)
        .add_valid("D", 453)
        .add_valid("D", 448)
        .add_valid("D", 44)
        .set_group_first(453, 448)
        .add_group_member(453, 44);

    auto buf = make_raw_frame(
        "35=D\x01"
        "453=1\x01"
        "448=PA\x01"
        "44=3.14\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    Parser<access_mode::Index> parser{dict};
    auto mv_res = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv_res.has_value());

    InboundHandle h;
    h.msg.view = &mv_res.value();

    const fixpp_group_t* grp = nullptr;
    size_t count = 0;
    ASSERT_EQ(fixpp_msg_get_group(h.ptr(), 453, &grp, &count), FIXPP_ERR_OK);
    ASSERT_EQ(count, 1U);

    fixpp_decimal_t dec{};
    ASSERT_EQ(fixpp_group_get_field_decimal(grp, 0, 44, &dec), FIXPP_ERR_OK);
    char fbuf[64];
    size_t written = 0;
    ASSERT_EQ(fixpp_decimal_format(dec, fbuf, sizeof(fbuf), &written), FIXPP_ERR_OK);
    EXPECT_EQ(std::string_view(fbuf, written), "3.14");
}

TEST(MessageReadGroup, NestedGroupDescent) {
    auto dict = make_nested_group_dict();

    // One top-level 453 instance with one nested 539 group instance
    auto buf = make_raw_frame(
        "35=D\x01"
        "453=1\x01"
        "448=PA\x01"
        "447=D\x01"
        "539=1\x01"
        "524=NPA\x01"
        "525=C\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    Parser<access_mode::Index> parser{dict};
    auto mv_res = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv_res.has_value());

    InboundHandle h;
    h.msg.view = &mv_res.value();

    const fixpp_group_t* grp = nullptr;
    size_t count = 0;
    ASSERT_EQ(fixpp_msg_get_group(h.ptr(), 453, &grp, &count), FIXPP_ERR_OK);
    ASSERT_EQ(count, 1U);

    // Descend: entry [0] → nested group 539
    const fixpp_group_t* nested = nullptr;
    size_t nested_count = 0;
    ASSERT_EQ(fixpp_group_get_nested_group(grp, 0, 539, &nested, &nested_count), FIXPP_ERR_OK);
    EXPECT_EQ(nested_count, 1U);
    EXPECT_NE(nested, nullptr);

    // Read from nested entry [0]
    const char* val = nullptr; size_t vlen = 0;
    ASSERT_EQ(fixpp_group_get_field_string(nested, 0, 524, &val, &vlen), FIXPP_ERR_OK);
    EXPECT_EQ(std::string_view(val, vlen), "NPA");

    ASSERT_EQ(fixpp_group_get_field_string(nested, 0, 525, &val, &vlen), FIXPP_ERR_OK);
    EXPECT_EQ(std::string_view(val, vlen), "C");
}

}  // namespace
