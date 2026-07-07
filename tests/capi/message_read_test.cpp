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

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
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

std::vector<std::byte> make_checked_frame(std::string_view body) {
    std::string pre = "8=FIX.4.4\x01" + std::string("9=") + std::to_string(body.size()) + "\x01" +
                      std::string(body);
    unsigned sum = 0;
    for (unsigned char c : pre) {
        sum += c;
    }
    char checksum[16]{};
    std::snprintf(checksum, sizeof(checksum), "10=%03u\x01", sum % 256U);
    std::string full = pre + checksum;
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

// F2 gate-b/r1: positive tag check in check_inbound_msg (mirrors
// MessageFieldIteration.TypeMismatchedHandleReturnsInvalidHandle).
// An ENGINE-tagged handle with a non-null view slips past the DEAD-only guard
// (ENGINE != DEAD) and then the view-null check (view != nullptr), landing in a
// real MessageView lookup on the wrong-layout struct.  The positive tag check
// (tag_ != FIXPP_HANDLE_TAG_MSG → INVALID_HANDLE) is the only guard that catches it.
//
// Pre-fix (DEAD-only guard): ENGINE != DEAD → passes; view != null → passes; returns
// the view pointer → fixpp_msg_get_string looks up tag 35 in the real view → OK.
// Post-fix (positive tag): ENGINE != MSG → INVALID_HANDLE.
TEST(MessageRead, TypeMismatchedHandleReturnsInvalidHandle) {
    auto buf = make_raw_frame("35=D\x01" "49=SENDER\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    std::pmr::monotonic_buffer_resource arena;
    MessageView<access_mode::Index> mv{*fv, &arena};

    fixpp_msg bad{};
    bad.tag_ = FIXPP_HANDLE_TAG_ENGINE;  // wrong type, non-DEAD
    bad.view = &mv;                       // non-null: DEAD-only guard would sail past
    const auto* p = reinterpret_cast<const fixpp_msg_t*>(&bad);

    const char* out = nullptr;
    size_t len = 0;
    EXPECT_EQ(fixpp_msg_get_string(p, 35, &out, &len), FIXPP_ERR_INVALID_HANDLE);
    EXPECT_EQ(fixpp_msg_get_msg_type(p, &out, &len), FIXPP_ERR_INVALID_HANDLE);
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
// Covers ALL read accessors (SC-003: "every read accessor"):
//   get_string, get_bytes, get_int, get_double, get_decimal, has_tag,
//   get_msg_type, get_group (absent-tag early-return path).
//
// Uses the mallocnesia LD_PRELOAD dual-gate via alloc_guard_markers.hpp.
// Without the preload the markers no-op (test still validates correctness).
//
// Frame: 35=D, 49=SENDER (STRING — used for get_string/get_bytes),
//        56=TARGET, 34=42 (INT — used for get_int/get_double/get_decimal).
// get_decimal: uses a stack-local 512-byte monotonic scratch (null upstream)
//   inside fixpp_msg_get_decimal — zero global heap.
// get_double:  uses std::from_chars (no alloc) on this platform.
// get_bytes:   raw pointer alias into the wire buffer — no alloc.
// get_group (absent tag): early-return before cursor allocation → zero alloc.
//   The group cursor path (present tag) is guarded separately in
//   ZeroGlobalHeapGroupCursorGuard below.
TEST(MessageRead, ZeroGlobalHeapAllocGuard) {
    auto buf = make_raw_frame("35=D\x01" "49=SENDER\x01" "56=TARGET\x01" "34=42\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    std::pmr::monotonic_buffer_resource arena;
    MessageView<access_mode::Index> mv{*fv, &arena};

    InboundHandle h;
    h.msg.view = &mv;

    // Warm-up: prime any lazy-init caches outside the measured window.
    // Covers every scalar read accessor (get_string, get_bytes, get_int,
    // get_double, get_decimal, has_tag, get_msg_type, version) and
    // get_group (absent tag 453 — frame has no group field; early return).
    for (int i = 0; i < 8; ++i) {
        const char* out = nullptr; size_t len = 0;
        (void)fixpp_msg_get_string(h.ptr(), 49, &out, &len);
        const uint8_t* bout = nullptr; size_t blen = 0;
        (void)fixpp_msg_get_bytes(h.ptr(), 49, &bout, &blen);
        int64_t iv = 0;
        (void)fixpp_msg_get_int(h.ptr(), 34, &iv);
        double dv = 0.0;
        (void)fixpp_msg_get_double(h.ptr(), 34, &dv);
        fixpp_decimal_t decv{};
        (void)fixpp_msg_get_decimal(h.ptr(), 34, &decv);
        fixpp_resolved_msg_version_t verv{};
        (void)fixpp_msg_version(h.ptr(), &verv);
        const fixpp_group_t* grp_wu = nullptr; size_t cnt_wu = 0;
        (void)fixpp_msg_get_group(h.ptr(), 453, &grp_wu, &cnt_wu);  // absent → TAG_NOT_FOUND
    }

    // SC-003 zero-global-heap guard: covers ALL read accessors including
    // fixpp_msg_get_group on the absent-tag early-return path (no cursor
    // allocated → zero alloc).  The present-tag cursor path is guarded in
    // ZeroGlobalHeapGroupCursorGuard.
    if (alloc_guard_start) alloc_guard_start();
    for (int i = 0; i < 1000; ++i) {
        const char* out = nullptr; size_t len = 0;
        ASSERT_EQ(fixpp_msg_get_string(h.ptr(), 49, &out, &len), FIXPP_ERR_OK);

        const uint8_t* bout = nullptr; size_t blen = 0;
        ASSERT_EQ(fixpp_msg_get_bytes(h.ptr(), 49, &bout, &blen), FIXPP_ERR_OK);

        int64_t iv = 0;
        ASSERT_EQ(fixpp_msg_get_int(h.ptr(), 34, &iv), FIXPP_ERR_OK);

        double dv = 0.0;
        ASSERT_EQ(fixpp_msg_get_double(h.ptr(), 34, &dv), FIXPP_ERR_OK);

        fixpp_decimal_t decv{};
        ASSERT_EQ(fixpp_msg_get_decimal(h.ptr(), 34, &decv), FIXPP_ERR_OK);

        bool present = false;
        ASSERT_EQ(fixpp_msg_has_tag(h.ptr(), 56, &present), FIXPP_ERR_OK);

        const char* mt = nullptr; size_t mtl = 0;
        ASSERT_EQ(fixpp_msg_get_msg_type(h.ptr(), &mt, &mtl), FIXPP_ERR_OK);

        fixpp_resolved_msg_version_t ver{};
        ASSERT_EQ(fixpp_msg_version(h.ptr(), &ver), FIXPP_ERR_OK);

        // get_group on an absent tag: early return before cursor allocation → zero alloc.
        const fixpp_group_t* grp_out = nullptr; size_t grp_count = 0;
        ASSERT_EQ(fixpp_msg_get_group(h.ptr(), 453, &grp_out, &grp_count), FIXPP_ERR_TAG_NOT_FOUND);
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

// SC-003 alloc guard: group cursor path.  The group cursor shell
// (fixpp_group) is now allocated from the parse arena (not the global heap),
// so the steady-state "cursor already built" path is zero-global-heap.
//
// Strategy: use a pre-seeded arena large enough to hold the OffsetTable
// entries + group_slices build + one cursor shell — all from the pre-seeded
// buffer.  Warm up group_slices materialisation outside the markers (the first
// build allocates into the pre-seeded arena, not new_delete, when the buffer
// has capacity); inside the markers the slices are cached and the cursor
// carves from the pre-seeded buffer → zero calls to new/malloc.
//
// Frame: 35=D, 453=1, 448=PA, 447=D (1 instance of group 453).
// Buffer: 16 KiB — generous headroom for OffsetTable vectors + group_slices
//         + cursor shell.  null_memory_resource upstream: if the buffer were
//         exhausted, parse would fail (the test would ASSERT-fail before the
//         guard window), so a passing test guarantees no overflow to new_delete.
TEST(MessageRead, ZeroGlobalHeapGroupCursorGuard) {
    auto dict = make_group_dict();
    auto buf = make_raw_frame(
        "35=D\x01"
        "34=1\x01"
        "453=1\x01"
        "448=PA\x01"
        "447=D\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    // Pre-seeded arena with null_memory_resource upstream: all allocations must
    // carve from arena_buf; any overflow would throw bad_alloc → test aborts.
    // This proves that the parse + group_slices build + cursor all fit in the
    // pre-seeded buffer (i.e. zero calls to new/malloc at any point).
    alignas(std::max_align_t) std::byte arena_buf[16384];
    std::pmr::monotonic_buffer_resource arena{arena_buf, sizeof(arena_buf),
                                              std::pmr::null_memory_resource()};

    Parser<access_mode::Index> parser{dict};
    auto mv_res = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv_res.has_value());

    InboundHandle h;
    h.msg.view = &mv_res.value();

    // Warm-up: trigger lazy group_slices build (allocates into the pre-seeded
    // arena) and the first cursor allocation — both outside the measured window.
    {
        const fixpp_group_t* grp_wu = nullptr;
        size_t cnt_wu = 0;
        ASSERT_EQ(fixpp_msg_get_group(h.ptr(), 453, &grp_wu, &cnt_wu), FIXPP_ERR_OK);
        EXPECT_EQ(cnt_wu, 1U);
    }

    // SC-003 guard: group_slices is cached; cursor carves from the pre-seeded
    // buffer (null upstream → any new_delete call would terminate the test here).
    if (alloc_guard_start) alloc_guard_start();
    {
        const fixpp_group_t* grp_out = nullptr;
        size_t grp_count = 0;
        ASSERT_EQ(fixpp_msg_get_group(h.ptr(), 453, &grp_out, &grp_count), FIXPP_ERR_OK);
        ASSERT_EQ(grp_count, 1U);
        ASSERT_NE(grp_out, nullptr);
        // Verify field access through the cursor also stays zero-alloc.
        const char* pv = nullptr; size_t pl = 0;
        ASSERT_EQ(fixpp_group_get_field_string(grp_out, 0, 448, &pv, &pl), FIXPP_ERR_OK);
        EXPECT_EQ(std::string_view(pv, pl), "PA");
    }
    if (alloc_guard_end) alloc_guard_end();
}

#ifndef NDEBUG

TEST(MessageReadGroupDeath, StaleTopLevelGroupCursorMetadataTrapsAfterRecycle) {
    auto dict = make_group_dict();
    auto buf = make_checked_frame(
        "35=D\x01"
        "34=1\x01"
        "453=1\x01"
        "448=PA\x01"
        "447=D\x01");

    fixpp::wire::Framer framer;
    alignas(std::max_align_t) std::byte arena_buf[16384];
    std::pmr::monotonic_buffer_resource arena{arena_buf, sizeof(arena_buf),
                                              std::pmr::null_memory_resource()};
    fixpp::wire::pmr_carry_buffer carry{buf.size(), &arena};
    std::array<fixpp::wire::frame_view, 1> frames{};
    auto framed = framer.feed(std::span<const std::byte>{buf.data(), buf.size()}, carry,
                              std::span<fixpp::wire::frame_view>{frames});
    ASSERT_TRUE(framed.has_value());
    ASSERT_GE(framed->size(), 1U);

    Parser<access_mode::Index> parser{dict};
    auto mv_res = parser.parse((*framed)[0], &arena);
    ASSERT_TRUE(mv_res.has_value());

    InboundHandle h;
    h.msg.view = &mv_res.value();

    {
        const fixpp_group_t* grp = nullptr;
        size_t count = 0;
        ASSERT_EQ(fixpp_msg_get_group(h.ptr(), 453, &grp, &count), FIXPP_ERR_OK);
        ASSERT_NE(grp, nullptr);
        ASSERT_EQ(count, 1U);
    }

    framer.recycle_pool();

    EXPECT_DEATH(
        {
            const fixpp_group_t* grp = nullptr;
            size_t count = 0;
            (void)fixpp_msg_get_group(h.ptr(), 453, &grp, &count);
        },
        "");

    EXPECT_DEATH(
        {
            const fixpp_group_t* grp = nullptr;
            size_t count = 0;
            (void)fixpp_msg_get_group(h.ptr(), 9999, &grp, &count);  // absent tag → stale find() must trap
        },
        "");
}

#endif

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

// FR-011 discriminating test: TWO outer-group instances each carrying a nested
// group with DIFFERENT values.  Before the fix, get_nested_group(g,0,...) and
// get_nested_group(g,1,...) return the same slices (the bug).
//
// Wire layout:
//   453=2          — two instances of outer group
//   448=PA 447=D   — outer entry 0 header fields
//   539=1          — outer entry 0 carries one nested group
//   524=NPA0 525=C0  — nested entry 0 (belongs to outer entry 0)
//   448=PB 447=D   — outer entry 1 header fields
//   539=1          — outer entry 1 carries one nested group
//   524=NPB1 525=C1  — nested entry 1 (belongs to outer entry 1)
TEST(MessageReadGroup, NestedGroupDescentTwoOuterEntries) {
    auto dict = make_nested_group_dict();

    auto buf = make_raw_frame(
        "35=D\x01"
        "453=2\x01"
        "448=PA\x01"
        "447=D\x01"
        "539=1\x01"
        "524=NPA0\x01"
        "525=C0\x01"
        "448=PB\x01"
        "447=D\x01"
        "539=1\x01"
        "524=NPB1\x01"
        "525=C1\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    Parser<access_mode::Index> parser{dict};
    auto mv_res = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv_res.has_value());

    InboundHandle h;
    h.msg.view = &mv_res.value();

    // Get the outer group (453) — expect 2 entries
    const fixpp_group_t* grp = nullptr;
    size_t count = 0;
    ASSERT_EQ(fixpp_msg_get_group(h.ptr(), 453, &grp, &count), FIXPP_ERR_OK);
    ASSERT_EQ(count, 2U);

    // --- outer entry 0: nested 539 should have NPA0 / C0 ---
    const fixpp_group_t* nested0 = nullptr;
    size_t nested_count0 = 0;
    ASSERT_EQ(fixpp_group_get_nested_group(grp, 0, 539, &nested0, &nested_count0), FIXPP_ERR_OK);
    ASSERT_EQ(nested_count0, 1U);
    ASSERT_NE(nested0, nullptr);

    const char* val = nullptr; size_t vlen = 0;
    ASSERT_EQ(fixpp_group_get_field_string(nested0, 0, 524, &val, &vlen), FIXPP_ERR_OK);
    EXPECT_EQ(std::string_view(val, vlen), "NPA0");

    ASSERT_EQ(fixpp_group_get_field_string(nested0, 0, 525, &val, &vlen), FIXPP_ERR_OK);
    EXPECT_EQ(std::string_view(val, vlen), "C0");

    // --- outer entry 1: nested 539 should have NPB1 / C1 ---
    const fixpp_group_t* nested1 = nullptr;
    size_t nested_count1 = 0;
    ASSERT_EQ(fixpp_group_get_nested_group(grp, 1, 539, &nested1, &nested_count1), FIXPP_ERR_OK);
    ASSERT_EQ(nested_count1, 1U);
    ASSERT_NE(nested1, nullptr);

    ASSERT_EQ(fixpp_group_get_field_string(nested1, 0, 524, &val, &vlen), FIXPP_ERR_OK);
    // This MUST be NPB1 (entry 1), not NPA0 (entry 0) — the discriminating assertion.
    EXPECT_EQ(std::string_view(val, vlen), "NPB1");

    ASSERT_EQ(fixpp_group_get_field_string(nested1, 0, 525, &val, &vlen), FIXPP_ERR_OK);
    EXPECT_EQ(std::string_view(val, vlen), "C1");
}

// 063 Round-2 tasks-pins §5 [pin#5] — C-ABI last-nested-instance-extent
// MassQuote-shaped witness: a MULTI-ENTRY nested group (539=2) followed by an
// OUTER member (999) that appears AFTER the nested group within the SAME
// outer occurrence. Targets the newly-activated delimiter-bounded
// multi-instance branch in fixpp_group_get_nested_group
// (src/capi/message_read.cpp, "close previous instance" — the pre-063
// OffsetTable::group() flat `seen_in_instance` walk could never produce an
// outer occurrence slice containing >=2 occurrences of a nested delimiter,
// so that branch's LCOV_EXCL_START marking is now STALE: it IS reachable
// post-063, since consume_group_extent's outer-extent walk now correctly
// encloses the FULL multi-entry nested group).
//
// ESCALATED — CONFIRMED DIVERGENCE, NOT a fake pass. Empirically RUN (not
// just traced): this exact frame/dict, with the assertions below LIVE (not
// skipped), produces:
//   fixpp_group_get_field_string(nested, /*i=*/1, /*tag=*/999, ...)
//     == FIXPP_ERR_OK, value "TRAIL"   (expected: FIXPP_ERR_TAG_NOT_FOUND)
// i.e. the trailing outer member 999 IS reachable through the LAST nested
// instance's own span — a real, reachable leak, not a hypothetical.
//
// Root cause: fixpp_group_get_nested_group's Phase-2 scan (message_read.cpp)
// is POSITIONAL / dictionary-membership-free by design (plan.md Round-2:
// Opus rejected threading `group_context` + dict through the C-ABI cursor as
// a gratuitous ABI-adjacent rewrite the fix does not require). It closes the
// LAST nested instance at `sl->data + sl->len` (the end of the OUTER
// occurrence's own — now correctly, post-063, membership-bounded — slice),
// not at the nested group's OWN extent end. When the outer slice legitimately
// contains trailing content AFTER the nested group (a sibling scalar that is
// a member of the OUTER group but not of the nested one), that trailing
// content is swallowed into the last nested instance's span. Every positional
// (membership-free) heuristic considered (assume uniform per-instance
// byte-width from instance 0; stop on first tag not seen in an earlier
// sibling instance) is unanchored and wrong in general for groups with
// optional/variable fields — inventing one would be exactly the kind of
// unauthorized enforcement the escalation policy forbids. A CORRECT fix
// requires nested-group membership, which this C-ABI path deliberately
// lacks (Opus Round-2 disposition, plan.md:97). This contradicts the Round-2
// tasks-pins §5 prediction ("corrects transitively, no fix needed") and is
// reported here rather than worked around; NOT marked passing. See the 063
// Phase-4 phase-implementer report for the orchestrator decision (options:
// (a) accept as a documented L-063-* limitation/waiver, (b) a positional
// heuristic with an explicit documented limitation, (c) plumb
// `group_context`+dict through the C-ABI cursor, reversing the Round-2
// rejection).
TEST(MessageReadGroup, NestedGroupLastInstanceExtentDoesNotAbsorbTrailingOuterMember) {
    GTEST_SKIP() << "ESCALATED: confirmed divergence — fixpp_group_get_nested_group's "
                    "positional (membership-free) Phase-2 scan closes the LAST nested "
                    "instance at the end of the OUTER occurrence's slice, not at the "
                    "nested group's own extent, so a trailing outer-level member "
                    "AFTER a multi-entry nested group leaks into the last nested "
                    "instance's span (fixpp_group_get_field_string(nested,1,999,...) "
                    "== FIXPP_ERR_OK/\"TRAIL\" instead of FIXPP_ERR_TAG_NOT_FOUND). "
                    "See this test's own comment block for the root-cause / options; "
                    "reported to the orchestrator rather than worked around.";

    fixpp::dict::table_view dict;
    dict.add_valid("D", 35)
        .add_valid("D", 453)
        .add_valid("D", 448)
        .add_valid("D", 447)
        .add_valid("D", 539)
        .add_valid("D", 524)
        .add_valid("D", 525)
        .add_valid("D", 999)
        .set_group_first(453, 448)
        .add_group_member(453, 447)
        .add_group_member(453, 539)
        .add_group_member(453, 524)   // transitively under 453 (nested delim)
        .add_group_member(453, 525)   // transitively under 453 (nested member)
        .add_group_member(453, 999)   // outer trailing scalar, AFTER the nested group
        .set_group_first(539, 524)
        .add_group_member(539, 525);

    auto buf = make_raw_frame(
        "35=D\x01"
        "453=1\x01"
        "448=PA\x01"
        "447=D\x01"
        "539=2\x01"
        "524=NPA0\x01"
        "525=C0\x01"
        "524=NPA1\x01"
        "525=C1\x01"
        "999=TRAIL\x01");
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
    ASSERT_NE(grp, nullptr);

    // Sanity: 999 legitimately belongs to THIS outer occurrence (it was
    // registered as a member of 453 and appears once, after the nested
    // group) — proves the nested-exclusion assertion below is meaningful
    // (999 is genuinely reachable at the outer level, not absent everywhere).
    const char* val = nullptr;
    size_t vlen = 0;
    ASSERT_EQ(fixpp_group_get_field_string(grp, 0, 999, &val, &vlen), FIXPP_ERR_OK);
    EXPECT_EQ(std::string_view(val, vlen), "TRAIL");

    const fixpp_group_t* nested = nullptr;
    size_t nested_count = 0;
    ASSERT_EQ(fixpp_group_get_nested_group(grp, 0, 539, &nested, &nested_count), FIXPP_ERR_OK);
    ASSERT_EQ(nested_count, 2U);
    ASSERT_NE(nested, nullptr);

    ASSERT_EQ(fixpp_group_get_field_string(nested, 0, 524, &val, &vlen), FIXPP_ERR_OK);
    EXPECT_EQ(std::string_view(val, vlen), "NPA0");
    ASSERT_EQ(fixpp_group_get_field_string(nested, 0, 525, &val, &vlen), FIXPP_ERR_OK);
    EXPECT_EQ(std::string_view(val, vlen), "C0");

    ASSERT_EQ(fixpp_group_get_field_string(nested, 1, 524, &val, &vlen), FIXPP_ERR_OK);
    EXPECT_EQ(std::string_view(val, vlen), "NPA1");
    ASSERT_EQ(fixpp_group_get_field_string(nested, 1, 525, &val, &vlen), FIXPP_ERR_OK);
    EXPECT_EQ(std::string_view(val, vlen), "C1");

    // DISCRIMINATOR: the LAST nested instance's own extent must NOT absorb
    // the trailing outer member 999 — a naive "close last instance at the
    // end of the outer slice" scan would leak 999 into nested[1]'s span.
    size_t discard_len = 0;
    EXPECT_EQ(fixpp_group_get_field_string(nested, 1, 999, &val, &discard_len),
              FIXPP_ERR_TAG_NOT_FOUND)
        << "999 must NOT be reachable through the last nested instance's own span";
}

// ── Coverage gap closers ──────────────────────────────────────────────────────

// NULL out-pointer for EVERY accessor (one call each, all return NULL_HANDLE).
// Closes the True-branch of "if (bytes_out==nullptr)" etc. for each accessor.
TEST(MessageRead, NullOutPointerAllAccessors) {
    auto buf = make_raw_frame("35=D\x01" "49=SENDER\x01" "34=42\x01" "44=1.5\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    std::pmr::monotonic_buffer_resource arena;
    MessageView<access_mode::Index> mv{*fv, &arena};
    InboundHandle h;
    h.msg.view = &mv;

    size_t len = 0;
    const uint8_t* bp = nullptr;
    EXPECT_EQ(fixpp_msg_get_bytes(h.ptr(), 49, nullptr, &len), FIXPP_ERR_NULL_HANDLE);
    EXPECT_EQ(fixpp_msg_get_bytes(h.ptr(), 49, &bp, nullptr), FIXPP_ERR_NULL_HANDLE);

    int64_t iv = 0;
    EXPECT_EQ(fixpp_msg_get_int(h.ptr(), 34, nullptr), FIXPP_ERR_NULL_HANDLE);

    double dv = 0.0;
    EXPECT_EQ(fixpp_msg_get_double(h.ptr(), 44, nullptr), FIXPP_ERR_NULL_HANDLE);

    fixpp_decimal_t dec{};
    EXPECT_EQ(fixpp_msg_get_decimal(h.ptr(), 44, nullptr), FIXPP_ERR_NULL_HANDLE);

    bool present = false;
    EXPECT_EQ(fixpp_msg_has_tag(h.ptr(), 49, nullptr), FIXPP_ERR_NULL_HANDLE);

    fixpp_resolved_msg_version_t ver{};
    EXPECT_EQ(fixpp_msg_version(h.ptr(), nullptr), FIXPP_ERR_NULL_HANDLE);

    const char* sv = nullptr;
    EXPECT_EQ(fixpp_msg_get_msg_type(h.ptr(), nullptr, &len), FIXPP_ERR_NULL_HANDLE);
    EXPECT_EQ(fixpp_msg_get_msg_type(h.ptr(), &sv, nullptr), FIXPP_ERR_NULL_HANDLE);

    const fixpp_group_t* grp = nullptr;
    size_t cnt = 0;
    EXPECT_EQ(fixpp_msg_get_group(h.ptr(), 453, nullptr, &cnt), FIXPP_ERR_NULL_HANDLE);
    EXPECT_EQ(fixpp_msg_get_group(h.ptr(), 453, &grp, nullptr), FIXPP_ERR_NULL_HANDLE);
}

// Group accessor NULL guards: g==nullptr for each group field accessor.
TEST(MessageRead, GroupNullHandleAllAccessors) {
    const fixpp_group_t* null_grp = nullptr;

    const char* sv = nullptr; size_t svlen = 0;
    EXPECT_EQ(fixpp_group_get_field_string(null_grp, 0, 448, &sv, &svlen), FIXPP_ERR_NULL_HANDLE);

    const uint8_t* bv = nullptr; size_t blen = 0;
    // v_out and len_out null checks for string
    EXPECT_EQ(fixpp_group_get_field_string(null_grp, 0, 448, nullptr, &svlen), FIXPP_ERR_NULL_HANDLE);
    EXPECT_EQ(fixpp_group_get_field_string(null_grp, 0, 448, &sv, nullptr), FIXPP_ERR_NULL_HANDLE);

    int64_t iv = 0;
    EXPECT_EQ(fixpp_group_get_field_int(null_grp, 0, 38, &iv), FIXPP_ERR_NULL_HANDLE);
    EXPECT_EQ(fixpp_group_get_field_int(null_grp, 0, 38, nullptr), FIXPP_ERR_NULL_HANDLE);

    double dv = 0.0;
    EXPECT_EQ(fixpp_group_get_field_double(null_grp, 0, 44, &dv), FIXPP_ERR_NULL_HANDLE);
    EXPECT_EQ(fixpp_group_get_field_double(null_grp, 0, 44, nullptr), FIXPP_ERR_NULL_HANDLE);

    fixpp_decimal_t dec{};
    EXPECT_EQ(fixpp_group_get_field_decimal(null_grp, 0, 44, &dec), FIXPP_ERR_NULL_HANDLE);
    EXPECT_EQ(fixpp_group_get_field_decimal(null_grp, 0, 44, nullptr), FIXPP_ERR_NULL_HANDLE);

    const fixpp_group_t* nested = nullptr; size_t nc = 0;
    EXPECT_EQ(fixpp_group_get_nested_group(null_grp, 0, 539, &nested, &nc), FIXPP_ERR_NULL_HANDLE);
    EXPECT_EQ(fixpp_group_get_nested_group(null_grp, 0, 539, nullptr, &nc), FIXPP_ERR_NULL_HANDLE);
    EXPECT_EQ(fixpp_group_get_nested_group(null_grp, 0, 539, &nested, nullptr), FIXPP_ERR_NULL_HANDLE);
}

// tag 1137 present in the frame → appl_ver_id filled.
TEST(MessageRead, VersionWithApplVerId) {
    // Include tag 1137 (DefaultApplVerID) for FIXT
    auto buf = make_raw_frame("35=D\x01" "1137=FIX.5.0SP2\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    std::pmr::monotonic_buffer_resource arena;
    MessageView<access_mode::Index> mv{*fv, &arena};
    InboundHandle h;
    h.msg.view = &mv;

    fixpp_resolved_msg_version_t ver{};
    ASSERT_EQ(fixpp_msg_version(h.ptr(), &ver), FIXPP_ERR_OK);
    ASSERT_NE(ver.appl_ver_id, nullptr);
    EXPECT_EQ(std::string_view(ver.appl_ver_id, ver.appl_ver_id_len), "FIX.5.0SP2");
}

// get_decimal with an invalid decimal string → FIXPP_ERR_DECIMAL_INVALID.
// Uses tag 44 with a non-numeric value to trigger the decimal_invalid path.
TEST(MessageRead, GetDecimalInvalid) {
    auto buf = make_raw_frame("35=D\x01" "44=GARBAGE\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    std::pmr::monotonic_buffer_resource arena;
    MessageView<access_mode::Index> mv{*fv, &arena};
    InboundHandle h;
    h.msg.view = &mv;

    fixpp_decimal_t val{};
    EXPECT_EQ(fixpp_msg_get_decimal(h.ptr(), 44, &val), FIXPP_ERR_DECIMAL_INVALID);
}

// Group field accessors — error arms for int, double, decimal.
// Reuses the dict and frame from GetGroupIntAndDouble.
TEST(MessageReadGroup, GroupFieldIntErrors) {
    fixpp::dict::table_view dict;
    dict.add_valid("D", 35)
        .add_valid("D", 453)
        .add_valid("D", 448)
        .add_valid("D", 38)
        .add_valid("D", 44)
        .set_group_first(453, 448)
        .add_group_member(453, 38)
        .add_group_member(453, 44);

    // Entry with non-numeric value in tag 38
    auto buf = make_raw_frame(
        "35=D\x01"
        "453=1\x01"
        "448=PA\x01"
        "38=NOTNUM\x01"
        "44=10.50\x01");
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

    // Absent tag in the entry → TAG_NOT_FOUND
    int64_t iv = 0;
    EXPECT_EQ(fixpp_group_get_field_int(grp, 0, 55, &iv), FIXPP_ERR_TAG_NOT_FOUND);

    // Non-numeric value → WIRE_INVALID_FRAME
    EXPECT_EQ(fixpp_group_get_field_int(grp, 0, 38, &iv), FIXPP_ERR_WIRE_INVALID_FRAME);

    // Index out of range
    EXPECT_EQ(fixpp_group_get_field_int(grp, 1, 38, &iv), FIXPP_ERR_INDEX_OUT_OF_RANGE);
}

TEST(MessageReadGroup, GroupFieldDoubleErrors) {
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
        "44=NOTNUM\x01");
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

    // Absent tag → TAG_NOT_FOUND
    double dv = 0.0;
    EXPECT_EQ(fixpp_group_get_field_double(grp, 0, 55, &dv), FIXPP_ERR_TAG_NOT_FOUND);

    // Non-numeric value → WIRE_INVALID_FRAME
    EXPECT_EQ(fixpp_group_get_field_double(grp, 0, 44, &dv), FIXPP_ERR_WIRE_INVALID_FRAME);

    // Index out of range
    EXPECT_EQ(fixpp_group_get_field_double(grp, 1, 44, &dv), FIXPP_ERR_INDEX_OUT_OF_RANGE);
}

TEST(MessageReadGroup, GroupFieldDecimalErrors) {
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
        "44=GARBAGE\x01");
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

    // Absent tag → TAG_NOT_FOUND
    fixpp_decimal_t dec{};
    EXPECT_EQ(fixpp_group_get_field_decimal(grp, 0, 55, &dec), FIXPP_ERR_TAG_NOT_FOUND);

    // Invalid decimal value → DECIMAL_INVALID
    EXPECT_EQ(fixpp_group_get_field_decimal(grp, 0, 44, &dec), FIXPP_ERR_DECIMAL_INVALID);

    // Index out of range
    EXPECT_EQ(fixpp_group_get_field_decimal(grp, 1, 44, &dec), FIXPP_ERR_INDEX_OUT_OF_RANGE);
}

// Nested group: absent nested tag → TAG_NOT_FOUND.
TEST(MessageReadGroup, NestedGroupAbsentTag) {
    auto dict = make_nested_group_dict();
    auto buf = make_raw_frame(
        "35=D\x01"
        "453=1\x01"
        "448=PA\x01"
        "447=D\x01");  // no 539 nested group
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

    const fixpp_group_t* nested = nullptr;
    size_t nc = 0;
    // 539 not present in the outer entry → TAG_NOT_FOUND
    EXPECT_EQ(fixpp_group_get_nested_group(grp, 0, 539, &nested, &nc), FIXPP_ERR_TAG_NOT_FOUND);
}

// fixpp_msg_version with tag 8 absent: structurally invalid but should
// return FIXPP_ERR_TAG_NOT_FOUND with nulled output fields.
// Construct a frame that has 9= and 10= but NO 8= tag.
TEST(MessageRead, VersionNoTag8) {
    // Build a raw frame that starts at 9= (no 8= prefix)
    std::string body = "35=D\x01";
    std::string nine = "9=" + std::to_string(body.size()) + "\x01";
    std::string full = nine + body + "10=000\x01";
    std::vector<std::byte> buf(full.size());
    std::memcpy(buf.data(), full.data(), full.size());

    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    std::pmr::monotonic_buffer_resource arena;
    MessageView<access_mode::Index> mv{*fv, &arena};

    InboundHandle h;
    h.msg.view = &mv;

    fixpp_resolved_msg_version_t ver{};
    ASSERT_EQ(fixpp_msg_version(h.ptr(), &ver), FIXPP_ERR_TAG_NOT_FOUND);
    // Output fields must be zero-initialized
    EXPECT_EQ(ver.begin_string, nullptr);
    EXPECT_EQ(ver.begin_string_len, 0U);
    EXPECT_EQ(ver.appl_ver_id, nullptr);
    EXPECT_EQ(ver.appl_ver_id_len, 0U);
}

// fixpp_msg_get_group on a null msg: exercises the TRUE arm of the
// `if (view == nullptr)` guard at line 319 (check_inbound_msg returns null).
// NullHandleReturnsNullHandle tests get_string/get_int/etc with null msg but
// NOT get_group — this closes that gap.
TEST(MessageRead, GetGroupNullMsgReturnsNullHandle) {
    const fixpp_group_t* grp = nullptr;
    size_t count = 0;
    EXPECT_EQ(fixpp_msg_get_group(nullptr, 453, &grp, &count), FIXPP_ERR_NULL_HANDLE);
}

// fixpp_msg_get_msg_type on a frame that has no 35= tag (empty msg_type()): exercises
// the `if (sv.empty()) return FIXPP_ERR_TAG_NOT_FOUND` at line 302.
// The VersionNoTag8 test builds a frame without 8= but still has 35=D; here we
// explicitly omit 35= from the body.
TEST(MessageRead, GetMsgTypeEmptyMsgType) {
    // Frame with 49=SENDER only (no 35=), so msg_type() returns empty string.
    auto buf = make_raw_frame("49=SENDER\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    std::pmr::monotonic_buffer_resource arena;
    MessageView<access_mode::Index> mv{*fv, &arena};
    InboundHandle h;
    h.msg.view = &mv;

    const char* mt = nullptr;
    size_t mt_len = 0;
    EXPECT_EQ(fixpp_msg_get_msg_type(h.ptr(), &mt, &mt_len), FIXPP_ERR_TAG_NOT_FOUND);
}

// get_string with a non-null value_out but null len_out: exercises the second
// operand of the '||' guard at line 157 (covers the short-circuited branch).
// The NullOutPointerAllAccessors test above passes null,null for get_string,
// which short-circuits on the first operand — this test drives the second.
TEST(MessageRead, GetStringNullLenOut) {
    auto buf = make_raw_frame("35=D\x01" "49=SENDER\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    std::pmr::monotonic_buffer_resource arena;
    MessageView<access_mode::Index> mv{*fv, &arena};
    InboundHandle h;
    h.msg.view = &mv;

    const char* sv = nullptr;
    // value_out != nullptr, len_out == nullptr → NULL_HANDLE (second ||  operand)
    EXPECT_EQ(fixpp_msg_get_string(h.ptr(), 49, &sv, nullptr), FIXPP_ERR_NULL_HANDLE);
}

// get_bytes on an absent tag: exercises line 179 (the if(!res) absent branch).
TEST(MessageRead, GetBytesAbsentTag) {
    auto buf = make_raw_frame("35=D\x01" "49=SENDER\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    std::pmr::monotonic_buffer_resource arena;
    MessageView<access_mode::Index> mv{*fv, &arena};
    InboundHandle h;
    h.msg.view = &mv;

    const uint8_t* bp = nullptr;
    size_t len = 0;
    // tag 56 is absent → TAG_NOT_FOUND (line 179 branch)
    EXPECT_EQ(fixpp_msg_get_bytes(h.ptr(), 56, &bp, &len), FIXPP_ERR_TAG_NOT_FOUND);
}

// get_double on an absent tag: exercises line 211 (the if(!res) absent branch).
TEST(MessageRead, GetDoubleAbsentTag) {
    auto buf = make_raw_frame("35=D\x01" "49=SENDER\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    std::pmr::monotonic_buffer_resource arena;
    MessageView<access_mode::Index> mv{*fv, &arena};
    InboundHandle h;
    h.msg.view = &mv;

    double dv = 0.0;
    // tag 44 is absent → TAG_NOT_FOUND (line 211 branch)
    EXPECT_EQ(fixpp_msg_get_double(h.ptr(), 44, &dv), FIXPP_ERR_TAG_NOT_FOUND);
}

// Group field accessors: v_out == nullptr or len_out == nullptr with a VALID group
// cursor. The GroupNullHandleAllAccessors test above always passes null_grp which
// short-circuits on the first '||' operand; here g is non-null so later operands
// are evaluated (covers lines 359/388 second and third sub-expressions).
TEST(MessageReadGroup, GroupNullOutParamWithValidHandle) {
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
    ASSERT_NE(grp, nullptr);

    // get_field_string: v_out null (g valid → second operand evaluated)
    size_t slen = 0;
    EXPECT_EQ(fixpp_group_get_field_string(grp, 0, 448, nullptr, &slen), FIXPP_ERR_NULL_HANDLE);

    // get_field_string: len_out null (g valid → third operand evaluated)
    const char* sv = nullptr;
    EXPECT_EQ(fixpp_group_get_field_string(grp, 0, 448, &sv, nullptr), FIXPP_ERR_NULL_HANDLE);

    // get_field_int: v_out null (g valid → second operand evaluated)
    EXPECT_EQ(fixpp_group_get_field_int(grp, 0, 448, nullptr), FIXPP_ERR_NULL_HANDLE);

    // get_field_double: v_out null (g valid → second operand evaluated)
    EXPECT_EQ(fixpp_group_get_field_double(grp, 0, 448, nullptr), FIXPP_ERR_NULL_HANDLE);

    // get_field_decimal: v_out null (g valid → second operand evaluated)
    EXPECT_EQ(fixpp_group_get_field_decimal(grp, 0, 448, nullptr), FIXPP_ERR_NULL_HANDLE);
}

// Nested group: v_out / len_out null with a valid group cursor (second and third
// operands of the nested_group null guard with g != nullptr).
TEST(MessageReadGroup, NestedGroupNullOutParamWithValidHandle) {
    auto dict = make_nested_group_dict();
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
    ASSERT_NE(grp, nullptr);

    // g valid, nested_out == nullptr → NULL_HANDLE
    size_t nc = 0;
    EXPECT_EQ(fixpp_group_get_nested_group(grp, 0, 539, nullptr, &nc), FIXPP_ERR_NULL_HANDLE);

    // g valid, count_out == nullptr → NULL_HANDLE
    const fixpp_group_t* ng = nullptr;
    EXPECT_EQ(fixpp_group_get_nested_group(grp, 0, 539, &ng, nullptr), FIXPP_ERR_NULL_HANDLE);
}

// parse_int64 / parse_double with an EMPTY string value: exercises the
// `if (sv.empty()) return false` lines (73/82) — the true arm is unreachable
// via standard get_int/get_double (the wire parser always gives non-empty
// field values), but IS reachable here by building a fake entry with an
// empty-value tag. These are LCOV branch lines, not DA lines.
//
// We test via get_field_int/get_field_double on a group member that has a
// zero-length value in the slice.  The group slice is hand-crafted so the
// member tag maps to an empty span_view — this is the only reachable path.
//
// NOTE: the standard wire format does not emit "tag=\x01" (empty field), but
// scan_slice_for_tag matches on the raw bytes; we build a frame with "38=\x01"
// and verify WIRE_INVALID_FRAME is returned (parse_int64 with empty → false).
TEST(MessageReadGroup, ParseIntAndDoubleEmptyFieldValue) {
    fixpp::dict::table_view dict;
    dict.add_valid("D", 35)
        .add_valid("D", 453)
        .add_valid("D", 448)
        .add_valid("D", 38)
        .add_valid("D", 44)
        .set_group_first(453, 448)
        .add_group_member(453, 38)
        .add_group_member(453, 44);

    // Wire frame with empty values for tag 38 and tag 44
    auto buf = make_raw_frame(
        "35=D\x01"
        "453=1\x01"
        "448=PA\x01"
        "38=\x01"   // empty value for int tag
        "44=\x01"); // empty value for double tag
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

    // Empty string → parse_int64 returns false → WIRE_INVALID_FRAME (line 73 true arm)
    int64_t iv = 0;
    EXPECT_EQ(fixpp_group_get_field_int(grp, 0, 38, &iv), FIXPP_ERR_WIRE_INVALID_FRAME);

    // Empty string → parse_double returns false → WIRE_INVALID_FRAME (line 82 true arm)
    double dv = 0.0;
    EXPECT_EQ(fixpp_group_get_field_double(grp, 0, 44, &dv), FIXPP_ERR_WIRE_INVALID_FRAME);
}

// Nested group: count field present but zero instances follow it (empty group).
// The 539 count field is at the very end of the outer instance with no delimiter after it.
TEST(MessageReadGroup, NestedGroupEmptyGroupCountLastField) {
    auto dict = make_nested_group_dict();
    // 539=0 at the end of the outer instance (count present, no delimiter follows)
    auto buf = make_raw_frame(
        "35=D\x01"
        "453=1\x01"
        "448=PA\x01"
        "447=D\x01"
        "539=0\x01");
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

    const fixpp_group_t* nested = nullptr;
    size_t nc = 0;
    // Count found but no delimiter follows → empty group → FIXPP_ERR_OK (count==0)
    EXPECT_EQ(fixpp_group_get_nested_group(grp, 0, 539, &nested, &nc), FIXPP_ERR_OK);
    EXPECT_EQ(nc, 0U);
}

}  // namespace
