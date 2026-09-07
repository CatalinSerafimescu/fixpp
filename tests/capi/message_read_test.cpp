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
#include <expected>
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
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/xml_loader.hpp>

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
// outer occurrence.
//
// FIXED by 065-cabi-nested-group-membership: fixpp_group_get_nested_group no
// longer runs a positional (membership-free) scan. It now delegates to
// OffsetTable::nested_group_slices (062) + consume_group_extent (063), bound
// by the parent cursor's own group_ctx, so the last nested instance's extent
// is membership-bounded and a trailing outer member is never absorbed into
// it. See src/capi/message_read.cpp::fixpp_group_get_nested_group and
// specs/065-cabi-nested-group-membership/ for the fix.
TEST(MessageReadGroup, NestedGroupLastInstanceExtentDoesNotAbsorbTrailingOuterMember) {
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

// Gate B PR#176 r1, fix queue item 1(ii): C-ABI top-level colliding-group
// read. Reproduces the real FIX44.xml reused-tag collision on top-level tag
// 296 (NoQuoteSets) minimally: MassQuoteAcknowledgement's QuotSetAckGrp
// (declared FIRST → wins the legacy bare/global-first-seen store; members
// {302, 304}, no 367) vs MassQuote's QuotSetGrp (declared SECOND; members
// {302, 367, 304}). Before the root-context ctor seed (parser.hpp dict-aware
// MessageView ctors), `fixpp_msg_get_group()` (`offsets.group_slices()`
// directly, no typed group<>() call anywhere on this path) ran under the
// empty `{msg_type=""}` context, missed the context-scoped store, fell back
// to bare/Ack membership, and truncated the QuoteSet extent before 367 —
// so a real, valid MassQuote's 367 field was unreachable via the public
// C-ABI. Mutation-proven: reverting the ctor seed makes
// fixpp_group_get_field_string(grp, 0, 367, ...) return FIXPP_ERR_TAG_NOT_FOUND
// instead of FIXPP_ERR_OK.
TEST(MessageReadGroup, TopLevelCollidingGroup296CAbiReadsFullMassQuoteExtent) {
    static constexpr std::string_view kMassQuote296CollisionXml = R"xml(
<fix major="4" minor="4">
  <header>
    <field number="8"  name="BeginString"  required="Y"/>
    <field number="9"  name="BodyLength"   required="Y"/>
    <field number="35" name="MsgType"      required="Y"/>
    <field number="10" name="CheckSum"     required="Y"/>
  </header>
  <trailer>
    <field number="10" name="CheckSum" required="Y"/>
  </trailer>
  <messages>
    <message name="MassQuoteAcknowledgement" msgtype="b" msgcat="app">
      <group number="296" name="NoQuoteSets" required="N">
        <field number="302" name="QuoteSetID" required="N"/>
        <field number="304" name="TotNoQuoteEntries" required="N"/>
      </group>
    </message>
    <message name="MassQuote" msgtype="i" msgcat="app">
      <field number="117" name="QuoteID" required="Y"/>
      <group number="296" name="NoQuoteSets" required="Y">
        <field number="302" name="QuoteSetID" required="Y"/>
        <field number="367" name="QuoteSetValidUntilTime" required="N"/>
        <field number="304" name="TotNoQuoteEntries" required="Y"/>
      </group>
    </message>
  </messages>
  <fields>
    <field number="8"   name="BeginString"  type="STRING"/>
    <field number="9"   name="BodyLength"   type="INT"/>
    <field number="35"  name="MsgType"      type="STRING"/>
    <field number="10"  name="CheckSum"     type="STRING"/>
    <field number="117" name="QuoteID"      type="STRING"/>
    <field number="296" name="NoQuoteSets"  type="NUMINGROUP"/>
    <field number="302" name="QuoteSetID"   type="STRING"/>
    <field number="367" name="QuoteSetValidUntilTime" type="UTCTIMESTAMP"/>
    <field number="304" name="TotNoQuoteEntries" type="INT"/>
  </fields>
</fix>
)xml";

    std::pmr::monotonic_buffer_resource arena;
    auto dict = fixpp::dict::XmlLoader{}.load_from_string(kMassQuote296CollisionXml, &arena);
    auto tv = dict.as_table_view();

    auto buf = make_raw_frame(
        "35=i\x01"
        "117=Q1\x01"
        "296=1\x01"
        "302=SET0\x01"
        "367=20260101-00:00:00\x01"
        "304=1\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    Parser<access_mode::Index> parser{tv};
    auto mv_res = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv_res.has_value());

    InboundHandle h;
    h.msg.view = &mv_res.value();

    const fixpp_group_t* grp = nullptr;
    size_t count = 0;
    ASSERT_EQ(fixpp_msg_get_group(h.ptr(), 296, &grp, &count), FIXPP_ERR_OK);
    ASSERT_EQ(count, 1U);
    ASSERT_NE(grp, nullptr);

    const char* v = nullptr;
    size_t vlen = 0;
    ASSERT_EQ(fixpp_group_get_field_string(grp, 0, 367, &v, &vlen), FIXPP_ERR_OK)
        << "fixpp_msg_get_group(296) on a real MassQuote must resolve the FULL QuotSetGrp "
           "extent (incl. 367) via the C-ABI, not the colliding QuotSetAckGrp truncation";
    EXPECT_EQ(std::string_view(v, vlen), "20260101-00:00:00");
}

// 065 T008 — FR-011(a)/SC-005/contract C7(a). DIRECT `as_table_view()`
// extent-arithmetic witness, following the inline-XML precedent above
// (TopLevelCollidingGroup296CAbiReadsFullMassQuoteExtent). Reproduces the
// real issue-#179 shape on the REAL FIX44 tag numbers: ExecutionReport(35=8)
// NoLegs(555) -> NoLegSecurityAltID(604) [multi-entry nested] -> trailing
// LegQty(687), declared AFTER the nested group in NoLegs' own field list
// (mirrors dictionaries/FIX44.xml: InstrmtLegExecGrp component's NoLegs
// group nests InstrumentLeg's LegSecAltIDGrp component (NoLegSecurityAltID)
// then LegQty as a direct NoLegs member).
//
// C-ABI == C++ typed equivalence (SC-005) is expressed via a HAND-ROLLED
// entry_context-based flyweight pair (G555Entry/G604Entry) rather than the
// codegen-generated fixpp::v44::ExecutionReport/groups::G_555/G_604 —
// `capi_message_read_test` has no `_codegen/include` path (no CMakeLists
// change permitted; see quickstart.md §6(a)). The hand-rolled classes call
// the IDENTICAL production primitives codegen emits
// (fixpp::wire::get()/OffsetTable::nested_group_slices() via
// MessageView::group<Tag,GroupT>()/group_view<GroupT>), so they exercise the
// SAME typed-read plumbing — precedent: tests/wire/
// repeating_group_equivalence_test.cpp::TestLeg (hand-rolled entry_context
// flyweight used with `mv->template group<555, TestLeg>()`, no codegen).
//
// Per the quickstart §6(a) writability caveat, the trailing tag 687 is NOT a
// member of the nested G_604 flyweight, so equivalence is expressed as: (a)
// genuine nested member values + nested count agree between C-ABI and typed,
// and (b) 687 is ABSENT from the typed nested[last] entry via the
// `field_value()` escape hatch — NOT by asking a typed accessor for 687
// directly.
TEST(MessageReadGroup, NestedTrailingMemberExcluded_Fix44LegsAsTableView) {
    static constexpr std::string_view kExecReportNoLegsXml = R"xml(
<fix major="4" minor="4">
  <header>
    <field number="8"  name="BeginString"  required="Y"/>
    <field number="9"  name="BodyLength"   required="Y"/>
    <field number="35" name="MsgType"      required="Y"/>
    <field number="10" name="CheckSum"     required="Y"/>
  </header>
  <trailer>
    <field number="10" name="CheckSum" required="Y"/>
  </trailer>
  <messages>
    <message name="ExecutionReport" msgtype="8" msgcat="app">
      <group number="555" name="NoLegs" required="N">
        <field number="600" name="LegSymbol" required="N"/>
        <group number="604" name="NoLegSecurityAltID" required="N">
          <field number="605" name="LegSecurityAltID" required="N"/>
          <field number="606" name="LegSecurityAltIDSource" required="N"/>
        </group>
        <field number="687" name="LegQty" required="N"/>
      </group>
    </message>
  </messages>
  <fields>
    <field number="8"   name="BeginString"  type="STRING"/>
    <field number="9"   name="BodyLength"   type="INT"/>
    <field number="35"  name="MsgType"      type="STRING"/>
    <field number="10"  name="CheckSum"     type="STRING"/>
    <field number="555" name="NoLegs"       type="NUMINGROUP"/>
    <field number="600" name="LegSymbol"    type="STRING"/>
    <field number="604" name="NoLegSecurityAltID" type="NUMINGROUP"/>
    <field number="605" name="LegSecurityAltID" type="STRING"/>
    <field number="606" name="LegSecurityAltIDSource" type="STRING"/>
    <field number="687" name="LegQty"       type="QTY"/>
  </fields>
</fix>
)xml";

    std::pmr::monotonic_buffer_resource arena;
    auto dict = fixpp::dict::XmlLoader{}.load_from_string(kExecReportNoLegsXml, &arena);
    auto tv = dict.as_table_view();

    auto buf = make_raw_frame(
        "35=8\x01"
        "555=1\x01"
        "600=LEG0\x01"
        "604=2\x01"
        "605=ALT0\x01"
        "606=S0\x01"
        "605=ALT1\x01"
        "606=S1\x01"
        "687=100\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    Parser<access_mode::Index> parser{tv};
    auto mv_res = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv_res.has_value());

    InboundHandle h;
    h.msg.view = &mv_res.value();

    // ── C-ABI descent ──────────────────────────────────────────────────
    const fixpp_group_t* outer = nullptr;
    size_t outer_count = 0;
    ASSERT_EQ(fixpp_msg_get_group(h.ptr(), 555, &outer, &outer_count), FIXPP_ERR_OK);
    ASSERT_EQ(outer_count, 1U);
    ASSERT_NE(outer, nullptr);

    // Sanity: the outer entry's own delimiter field (600) reads correctly.
    const char* v = nullptr;
    size_t vlen = 0;
    ASSERT_EQ(fixpp_group_get_field_string(outer, 0, 600, &v, &vlen), FIXPP_ERR_OK);
    EXPECT_EQ(std::string_view(v, vlen), "LEG0");

    const fixpp_group_t* nested = nullptr;
    size_t nested_count = 0;
    ASSERT_EQ(fixpp_group_get_nested_group(outer, 0, 604, &nested, &nested_count), FIXPP_ERR_OK);
    ASSERT_EQ(nested_count, 2U);
    ASSERT_NE(nested, nullptr);

    ASSERT_EQ(fixpp_group_get_field_string(nested, 0, 605, &v, &vlen), FIXPP_ERR_OK);
    EXPECT_EQ(std::string_view(v, vlen), "ALT0");
    ASSERT_EQ(fixpp_group_get_field_string(nested, 0, 606, &v, &vlen), FIXPP_ERR_OK);
    EXPECT_EQ(std::string_view(v, vlen), "S0");

    ASSERT_EQ(fixpp_group_get_field_string(nested, 1, 605, &v, &vlen), FIXPP_ERR_OK);
    EXPECT_EQ(std::string_view(v, vlen), "ALT1");
    ASSERT_EQ(fixpp_group_get_field_string(nested, 1, 606, &v, &vlen), FIXPP_ERR_OK);
    EXPECT_EQ(std::string_view(v, vlen), "S1");

    // DISCRIMINATOR: the LAST nested instance's own extent must NOT absorb
    // the trailing outer member 687 (LegQty).
    EXPECT_EQ(fixpp_group_get_field_string(nested, 1, 687, &v, &vlen), FIXPP_ERR_TAG_NOT_FOUND)
        << "687 (LegQty) must NOT be reachable through the last nested "
           "NoLegSecurityAltID instance's own span";

    // 687 legitimately belongs to the OUTER NoLegs occurrence.
    ASSERT_EQ(fixpp_group_get_field_string(outer, 0, 687, &v, &vlen), FIXPP_ERR_OK);
    EXPECT_EQ(std::string_view(v, vlen), "100");

    // ── C++ typed equivalence (SC-005 / FR-011(a)) ─────────────────────
    struct G604Entry {
        explicit G604Entry(fixpp::wire::entry_context ctx) noexcept : ctx_(ctx) {}
        [[nodiscard]] fixpp::core::expected_t<std::string_view> leg_security_alt_id()
            const noexcept {
            auto f = fixpp::wire::get(ctx_.span, 605, ctx_.gen);
            if (!f) {
                return std::unexpected{f.error()};
            }
            return f->as_string();
        }
        [[nodiscard]] fixpp::core::expected_t<std::string_view> leg_security_alt_id_source()
            const noexcept {
            auto f = fixpp::wire::get(ctx_.span, 606, ctx_.gen);
            if (!f) {
                return std::unexpected{f.error()};
            }
            return f->as_string();
        }
        [[nodiscard]] fixpp::core::expected_t<fixpp::wire::field_view> field_value(
            std::uint16_t tag) const noexcept {
            return fixpp::wire::get(ctx_.span, tag, ctx_.gen);
        }
        fixpp::wire::entry_context ctx_{};
    };
    struct G555Entry {
        explicit G555Entry(fixpp::wire::entry_context ctx) noexcept : ctx_(ctx) {}
        [[nodiscard]] fixpp::wire::group_view<G604Entry> leg_security_alt_id_group()
            const noexcept {
            if (ctx_.parent_cache_owner == nullptr) {
                return {};
            }
            auto const r = ctx_.parent_cache_owner->nested_group_slices(
                ctx_.outer_occurrence_id, ctx_.span.size(), 604, ctx_.opaque_dict,
                ctx_.group_member_fn, ctx_.gen, ctx_.group_ctx);
            return fixpp::wire::group_view<G604Entry>{r.slices, ctx_, r.alloc_failed};
        }
        [[nodiscard]] fixpp::core::expected_t<fixpp::wire::field_view> field_value(
            std::uint16_t tag) const noexcept {
            return fixpp::wire::get(ctx_.span, tag, ctx_.gen);
        }
        fixpp::wire::entry_context ctx_{};
    };

    auto legs = mv_res.value().template group<555, G555Entry>();
    ASSERT_EQ(legs.size(), 1U) << "typed outer NoLegs count must agree with C-ABI outer_count";
    auto leg0 = legs[0];

    auto nested_typed = leg0.leg_security_alt_id_group();
    ASSERT_EQ(nested_typed.size(), 2U) << "typed nested count must agree with C-ABI nested_count";

    auto n0 = nested_typed[0];
    auto id0 = n0.leg_security_alt_id();
    ASSERT_TRUE(id0.has_value());
    EXPECT_EQ(*id0, "ALT0");
    auto src0 = n0.leg_security_alt_id_source();
    ASSERT_TRUE(src0.has_value());
    EXPECT_EQ(*src0, "S0");

    auto n1 = nested_typed[1];
    auto id1 = n1.leg_security_alt_id();
    ASSERT_TRUE(id1.has_value());
    EXPECT_EQ(*id1, "ALT1");
    auto src1 = n1.leg_security_alt_id_source();
    ASSERT_TRUE(src1.has_value());
    EXPECT_EQ(*src1, "S1");

    // Typed discriminator: the LAST nested entry's own field_value(687)
    // escape hatch must be ABSENT — matching the C-ABI TAG_NOT_FOUND above.
    // G604Entry has NO accessor for 687 (not its member); asking via the
    // escape hatch is the only way to express this equivalence (§6(a)
    // writability caveat).
    auto swallowed = n1.field_value(687);
    EXPECT_FALSE(swallowed.has_value())
        << "typed nested[last].field_value(687) must be absent, matching the C-ABI "
           "FIXPP_ERR_TAG_NOT_FOUND result above";

    // The outer typed entry's own field_value(687) IS present (matches the
    // C-ABI outer OK/"100" result above).
    auto outer_687 = leg0.field_value(687);
    ASSERT_TRUE(outer_687.has_value());
    EXPECT_EQ(outer_687->as_string(), "100");
}

// 220 — REWRITTEN from 065 T011's FR-008 dict-free degradation pin, and
// rewritten DELIBERATELY, which is exactly what the old cell asked for. Its
// closing sentence read:
//
//   "Asserting FIXPP_ERR_OK here (not TAG_NOT_FOUND) pins that documented
//    degradation as a regression guard: if a future change made the dict-free
//    path ALSO exclude the trailing member, this test would need to change
//    deliberately, not silently."
//
// This is that change, and it goes further than the old cell anticipated: the
// dict-free path does not exclude the trailing member, it declines to identify
// a group at all. `OffsetTable::group()` is now a dictionary-only operation
// (fixpp#220), so with no membership predicate there is nothing for the C-ABI
// cursor to descend into and `fixpp_msg_get_group` reports
// FIXPP_ERR_TYPE_MISMATCH — the E-2 / CA-010-read result for "this tag is not
// a group here", the same answer a scalar tag gets (see
// ScalarAsGroupCapi.SymbolTagQueriedAsGroupReturnsTypeMismatch).
//
// FR-008's bar was "no crash, no UB, no regression versus today's positional
// behaviour". The first two still hold — this cell is still an ASan/UBSan
// witness for a dict-free descent attempt. The third is deliberately NOT met
// and is SUPERSEDED: 065 took pre-065 positional behaviour as the floor
// because it was fixing the dict-AWARE path and would not touch the other.
// #220 establishes that the positional result was itself the defect — the
// trailing outer member absorbed into the last nested instance is a wrong
// value returned under FIXPP_ERR_OK, and a caller cannot tell it from a real
// one. Trading a silently wrong value for a defined refusal is not a
// regression against that floor; it removes the floor.
//
// ⚠️ THIS IS A VISIBLE C-ABI BEHAVIOUR CHANGE for a dict-free caller:
// fixpp_msg_get_group(453) went from FIXPP_ERR_OK + a cursor whose last nested
// instance absorbed trailing outer members, to FIXPP_ERR_TYPE_MISMATCH. No
// exported symbol, header, or enum changes (the 1.5.0 freeze holds); the
// behaviour is recorded as B-220-1 / L-220-1 in
// spec/behaviors-and-limitations.md. A caller that needs groups must parse
// with a dictionary — Parser<access_mode::Index>{dict}.
//
// Frame is unchanged from the old cell (outer 453 -> nested 539 x2 -> trailing
// outer member 999), so the two dispositions are directly comparable.
TEST(MessageReadGroup, DictFreeGroupReadReportsTypeMismatch) {
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
    // Default ctor — dict-free: opaque_dict_ == nullptr, group_member_fn_ ==
    // nullptr (confirmed constructible; mirrors
    // tests/wire/message_view_membership_copy_test.cpp:195).
    Parser<access_mode::Index> parser{};
    auto mv_res = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv_res.has_value());
    InboundHandle h;
    h.msg.view = &mv_res.value();

    // ANTI-VACUITY: the parse itself must have succeeded and seen the frame.
    // Without this, a failed parse would produce the same TYPE_MISMATCH below
    // for an entirely different reason, and this cell would pass while proving
    // nothing about group identification.
    const char* probe = nullptr;
    size_t probe_len = 0;
    ASSERT_EQ(fixpp_msg_get_string(h.ptr(), 448, &probe, &probe_len), FIXPP_ERR_OK)
        << "the dict-free parse must still read ordinary scalars — only GROUP identification "
           "is withdrawn, and this cell is worthless if the message did not parse";
    EXPECT_EQ(std::string_view(probe, probe_len), "PA");

    // The change proper: no membership oracle => 453 is not identifiable as a
    // group, so the cursor is never handed out.
    const fixpp_group_t* grp = nullptr;
    size_t count = 0;
    EXPECT_EQ(fixpp_msg_get_group(h.ptr(), 453, &grp, &count), FIXPP_ERR_TYPE_MISMATCH)
        << "dict-free: fixpp#220 makes group identification a dictionary-only operation, so a "
           "group read reports TYPE_MISMATCH rather than handing back a positionally-derived "
           "cursor whose last nested instance absorbs trailing outer members";

    // The nested tag behaves identically — there is no path to a nested cursor
    // that does not go through the outer one.
    const fixpp_group_t* nested = nullptr;
    size_t nested_count = 0;
    EXPECT_EQ(fixpp_msg_get_group(h.ptr(), 539, &nested, &nested_count), FIXPP_ERR_TYPE_MISMATCH);
}

}  // namespace
