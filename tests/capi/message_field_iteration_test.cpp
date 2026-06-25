// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/capi/message_field_iteration_test.cpp — CA-053 (T018/T019/T020) US3
//
// TDD: this file is written RED before T020 implements the bodies.
//
// Strategy: hand-built parsed frames (same pattern as message_read_test.cpp).
// Tests wrap a real wire::MessageView<Index> in a stack fixpp_msg and cast to
// fixpp_msg_t* — no live sessions required for most witnesses.
//
// FR-006: enumeration is in wire/document order (mutation-discriminating).
// FR-007: enumeration is a superset of the scalar getter (SC-002 group frame).
// SC-002: repeating-group fixture required.
// SC-003: zero-global-heap guard (mallocnesia LD_PRELOAD dual-gate entry in
//         CMakeLists.txt) + clone cross-strand (cross-thread) witness.
// TypeMismatched: positive tag check (tag_ != MSG → INVALID) discriminated by
//   ENGINE-tag handle with a non-null view — a DEAD-only guard misses it.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory_resource>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

// C-ABI under test
#include "fix/c_api/message.h"
#include "fix/c_api/error.h"

// Engine-internal concrete fixpp_msg definition (test-only access)
#include "capi_internal.hpp"

// Wire surface
#include <fixpp/wire/parser.hpp>
#include <fixpp/dict/table_view.hpp>

// Test support
#include "support/alloc_guard_markers.hpp"
#include "support/frame_view_factory.hpp"

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

// Wrap a stack MessageView<Index> as a fixpp_msg_t for C-ABI calls.
// Inbound-flavour: view != nullptr, accumulator == nullptr.
struct InboundHandle {
    fixpp_msg msg{};
    const fixpp_msg_t* ptr() const noexcept {
        return reinterpret_cast<const fixpp_msg_t*>(&msg);
    }
};

// Minimal dictionary: 453=NoPartyIDs, delimiter=448, member=447.
// Same as message_read_test.cpp::make_group_dict() (SC-002).
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

// ── Error paths ───────────────────────────────────────────────────────────────

TEST(MessageFieldIteration, NullHandleReturnsNullHandle) {
    size_t count = 99;
    EXPECT_EQ(fixpp_msg_field_count(nullptr, &count), FIXPP_ERR_NULL_HANDLE);

    fixpp_msg_field_t field{};
    EXPECT_EQ(fixpp_msg_field_at(nullptr, 0, &field), FIXPP_ERR_NULL_HANDLE);
}

TEST(MessageFieldIteration, NullOutputPointerReturnsNullHandle) {
    auto buf = make_raw_frame("35=D\x01" "49=SENDER\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    std::pmr::monotonic_buffer_resource arena;
    MessageView<access_mode::Index> mv{*fv, &arena};

    InboundHandle h;
    h.msg.view = &mv;

    EXPECT_EQ(fixpp_msg_field_count(h.ptr(), nullptr), FIXPP_ERR_NULL_HANDLE);
    EXPECT_EQ(fixpp_msg_field_at(h.ptr(), 0, nullptr), FIXPP_ERR_NULL_HANDLE);
}

TEST(MessageFieldIteration, DestroyedHandleReturnsInvalidHandle) {
    fixpp_msg dead{};
    dead.tag_ = FIXPP_HANDLE_TAG_DEAD;
    const auto* p = reinterpret_cast<const fixpp_msg_t*>(&dead);

    size_t count = 0;
    EXPECT_EQ(fixpp_msg_field_count(p, &count), FIXPP_ERR_INVALID_HANDLE);

    fixpp_msg_field_t field{};
    EXPECT_EQ(fixpp_msg_field_at(p, 0, &field), FIXPP_ERR_INVALID_HANDLE);
}

// A MSG-tagged but view-null handle (a not-yet-resolved / malformed inbound
// shell) → INVALID_HANDLE. Covers check_msg_for_field_iter's view==nullptr arm
// distinctly from the tag and NULL-handle guards.
TEST(MessageFieldIteration, MsgTagButNullViewReturnsInvalidHandle) {
    fixpp_msg shell{};  // default member-init: tag_ == FIXPP_HANDLE_TAG_MSG, view == nullptr
    const auto* p = reinterpret_cast<const fixpp_msg_t*>(&shell);

    size_t count = 0;
    EXPECT_EQ(fixpp_msg_field_count(p, &count), FIXPP_ERR_INVALID_HANDLE);

    fixpp_msg_field_t field{};
    EXPECT_EQ(fixpp_msg_field_at(p, 0, &field), FIXPP_ERR_INVALID_HANDLE);
}

// Discriminating type-mismatch witness: ENGINE-tagged handle with a non-null view.
// A DEAD-only guard would not fire here (ENGINE != DEAD, view != nullptr → proceeds).
// The positive tag check (tag_ != FIXPP_HANDLE_TAG_MSG → INVALID) is the only guard
// that catches this; mutation-revert (DEAD-only) causes the test to go RED.
TEST(MessageFieldIteration, TypeMismatchedHandleReturnsInvalidHandle) {
    // Build a real view so that view != nullptr (discriminates from view-null check).
    auto buf = make_raw_frame("35=D\x01" "49=SENDER\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    std::pmr::monotonic_buffer_resource arena;
    MessageView<access_mode::Index> mv{*fv, &arena};

    fixpp_msg bad{};
    bad.tag_ = FIXPP_HANDLE_TAG_ENGINE;  // wrong type, non-DEAD
    bad.view = &mv;                       // non-null: DEAD-only guard would sail past
    const auto* p = reinterpret_cast<const fixpp_msg_t*>(&bad);

    size_t count = 0;
    EXPECT_EQ(fixpp_msg_field_count(p, &count), FIXPP_ERR_INVALID_HANDLE);

    fixpp_msg_field_t field{};
    EXPECT_EQ(fixpp_msg_field_at(p, 0, &field), FIXPP_ERR_INVALID_HANDLE);
}

TEST(MessageFieldIteration, IndexOutOfRange) {
    auto buf = make_raw_frame("35=D\x01" "49=SENDER\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    std::pmr::monotonic_buffer_resource arena;
    MessageView<access_mode::Index> mv{*fv, &arena};

    InboundHandle h;
    h.msg.view = &mv;

    size_t count = 0;
    ASSERT_EQ(fixpp_msg_field_count(h.ptr(), &count), FIXPP_ERR_OK);
    ASSERT_GT(count, 0U);

    fixpp_msg_field_t field{};
    EXPECT_EQ(fixpp_msg_field_at(h.ptr(), count, &field), FIXPP_ERR_INDEX_OUT_OF_RANGE);
    EXPECT_EQ(fixpp_msg_field_at(h.ptr(), count + 99, &field), FIXPP_ERR_INDEX_OUT_OF_RANGE);
}

// ── FR-006: wire-order enumeration (mutation-discriminating) ──────────────────
//
// Frame body: 35=D, 49=SENDER, 56=TARGET, 34=42
// Full wire:  8=FIX.4.4, 9=..., 35=D, 49=SENDER, 56=TARGET, 34=42, 10=000
// entries() in wire order: [0]=8, [1]=9, [2]=35, [3]=49, [4]=56, [5]=34, [6]=10
//
// A sorted implementation would return: [0]=8,[1]=9,[2]=10,[3]=34,[4]=35,[5]=49,[6]=56
// so entries[2].tag==10 (not 35).  Asserting [2]==35 and [3]==49 discriminates.
TEST(MessageFieldIteration, WireOrderDiscriminating) {
    auto buf = make_raw_frame("35=D\x01" "49=SENDER\x01" "56=TARGET\x01" "34=42\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    std::pmr::monotonic_buffer_resource arena;
    MessageView<access_mode::Index> mv{*fv, &arena};

    InboundHandle h;
    h.msg.view = &mv;

    size_t count = 0;
    ASSERT_EQ(fixpp_msg_field_count(h.ptr(), &count), FIXPP_ERR_OK);
    ASSERT_EQ(count, 7U) << "8,9,35,49,56,34,10 → 7 entries";

    fixpp_msg_field_t f0{}, f2{}, f3{};
    ASSERT_EQ(fixpp_msg_field_at(h.ptr(), 0, &f0), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_msg_field_at(h.ptr(), 2, &f2), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_msg_field_at(h.ptr(), 3, &f3), FIXPP_ERR_OK);

    EXPECT_EQ(f0.tag, 8U)  << "index 0 must be tag 8 (BeginString)";
    EXPECT_EQ(f2.tag, 35U) << "index 2 must be tag 35 (MsgType) — sorted impl returns 10";
    EXPECT_EQ(f3.tag, 49U) << "index 3 must be tag 49 (SenderCompID) — sorted impl returns 34";
}

// ── Offset cross-check: field_at value must byte-equal get_string ─────────────
//
// Validates the offset math (e.offset = value-start, not tag-start).
// If offset pointed to the tag= prefix, the string "49=MYSENDER" would be
// returned instead of "MYSENDER" — this test catches the off-by-one.
TEST(MessageFieldIteration, ValueMatchesGetString) {
    auto buf = make_raw_frame("35=D\x01" "49=MYSENDER\x01" "56=MYTARGET\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    std::pmr::monotonic_buffer_resource arena;
    MessageView<access_mode::Index> mv{*fv, &arena};

    InboundHandle h;
    h.msg.view = &mv;

    // Find the index of tag 49 via iteration.
    size_t count = 0;
    ASSERT_EQ(fixpp_msg_field_count(h.ptr(), &count), FIXPP_ERR_OK);

    size_t idx49 = SIZE_MAX;
    for (size_t i = 0; i < count; ++i) {
        fixpp_msg_field_t f{};
        ASSERT_EQ(fixpp_msg_field_at(h.ptr(), i, &f), FIXPP_ERR_OK);
        if (f.tag == 49) {
            idx49 = i;
            break;
        }
    }
    ASSERT_NE(idx49, SIZE_MAX) << "tag 49 must appear in entries";

    fixpp_msg_field_t field{};
    ASSERT_EQ(fixpp_msg_field_at(h.ptr(), idx49, &field), FIXPP_ERR_OK);
    EXPECT_EQ(field.tag, 49U);
    ASSERT_NE(field.value, nullptr);
    EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(field.value), field.len), "MYSENDER");

    // Cross-check: byte-identical with fixpp_msg_get_string(49).
    const char* gs_out = nullptr;
    size_t gs_len = 0;
    ASSERT_EQ(fixpp_msg_get_string(h.ptr(), 49, &gs_out, &gs_len), FIXPP_ERR_OK);
    ASSERT_NE(gs_out, nullptr);
    EXPECT_EQ(field.len, gs_len);
    EXPECT_EQ(0, std::memcmp(field.value, gs_out, field.len))
        << "field_at value must be byte-identical to get_string";
}

// ── Value aliasing: field.value must lie within the wire buffer ────────────────
TEST(MessageFieldIteration, ValueAliasesWireBuffer) {
    auto buf = make_raw_frame("35=D\x01" "49=SENDER\x01" "56=TARGET\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    std::pmr::monotonic_buffer_resource arena;
    MessageView<access_mode::Index> mv{*fv, &arena};

    InboundHandle h;
    h.msg.view = &mv;

    size_t count = 0;
    ASSERT_EQ(fixpp_msg_field_count(h.ptr(), &count), FIXPP_ERR_OK);

    const auto* buf_start = reinterpret_cast<const uint8_t*>(buf.data());
    const auto* buf_end   = buf_start + buf.size();
    for (size_t i = 0; i < count; ++i) {
        fixpp_msg_field_t f{};
        ASSERT_EQ(fixpp_msg_field_at(h.ptr(), i, &f), FIXPP_ERR_OK);
        ASSERT_NE(f.value, nullptr) << "field " << i << " value must not be null";
        EXPECT_GE(f.value, buf_start) << "field " << i << " must alias wire buffer";
        EXPECT_LT(f.value, buf_end)   << "field " << i << " must alias wire buffer";
    }
}

// ── FR-007 + SC-002: superset on repeating-group message ─────────────────────
//
// Frame body: 35=D, 49=SENDER, 453=2, 448=PA, 447=D, 448=PB, 447=E, 34=42
// Full entries: 8, 9, 35, 49, 453, 448, 447, 448, 447, 34, 10 → 11 entries.
// Tag 448 appears twice: indices 5 and 7 (wire order).
// get_string(448) → "PA" (first occurrence only).
// field_count returns 2 entries for tag 448 → superset proven (FR-007).
TEST(MessageFieldIteration, SupersetOnRepeatingGroup) {
    auto dict = make_group_dict();
    auto buf = make_raw_frame(
        "35=D\x01" "49=SENDER\x01"
        "453=2\x01" "448=PA\x01" "447=D\x01"
                    "448=PB\x01" "447=E\x01"
        "34=42\x01"
    );
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    std::pmr::monotonic_buffer_resource arena;

    Parser<access_mode::Index> parser{dict};
    auto mv_res = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv_res.has_value());

    InboundHandle h;
    h.msg.view = &mv_res.value();

    size_t count = 0;
    ASSERT_EQ(fixpp_msg_field_count(h.ptr(), &count), FIXPP_ERR_OK);
    // 8, 9, 35, 49, 453, 448, 447, 448, 447, 34, 10 → 11 entries
    ASSERT_EQ(count, 11U) << "expected 11 entries for repeating-group frame";

    // Collect tag-448 occurrences.
    int tag448_count = 0;
    std::string first448_val, second448_val;
    for (size_t i = 0; i < count; ++i) {
        fixpp_msg_field_t f{};
        ASSERT_EQ(fixpp_msg_field_at(h.ptr(), i, &f), FIXPP_ERR_OK);
        if (f.tag == 448) {
            ++tag448_count;
            std::string val(reinterpret_cast<const char*>(f.value), f.len);
            if (tag448_count == 1) first448_val = val;
            else if (tag448_count == 2) second448_val = val;
        }
    }
    EXPECT_EQ(tag448_count, 2) << "tag 448 must appear twice (superset)";
    EXPECT_EQ(first448_val, "PA")  << "first 448 entry must be PA (wire order)";
    EXPECT_EQ(second448_val, "PB") << "second 448 entry must be PB (wire order)";

    // FR-007: scalar getter is a strict subset — only returns the first occurrence.
    const char* gs_out = nullptr;
    size_t gs_len = 0;
    ASSERT_EQ(fixpp_msg_get_string(h.ptr(), 448, &gs_out, &gs_len), FIXPP_ERR_OK);
    EXPECT_EQ(std::string_view(gs_out, gs_len), "PA")
        << "get_string returns first occurrence only; enumeration is a superset";
}

// ── SC-003 zero-global-heap: field_count + field_at are pure span lookups ─────
//
// The binding gate is the mallocnesia LD_PRELOAD dual-gate entry in CMakeLists.txt
// (capi_message_field_iteration_mallocnesia).  The in-process marker test here is a
// complementary discriminator for runs without the preload.
TEST(MessageFieldIteration, ZeroGlobalHeapAllocGuard) {
    auto buf = make_raw_frame("35=D\x01" "49=SENDER\x01" "56=TARGET\x01" "34=42\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    std::pmr::monotonic_buffer_resource arena;
    MessageView<access_mode::Index> mv{*fv, &arena};

    InboundHandle h;
    h.msg.view = &mv;

    // Warm-up: prime any lazy-init paths outside the guard window.
    {
        size_t c = 0;
        (void)fixpp_msg_field_count(h.ptr(), &c);
        for (size_t i = 0; i < c; ++i) {
            fixpp_msg_field_t f{};
            (void)fixpp_msg_field_at(h.ptr(), i, &f);
        }
    }

    if (alloc_guard_start) alloc_guard_start();
    for (int iter = 0; iter < 1000; ++iter) {
        size_t c = 0;
        ASSERT_EQ(fixpp_msg_field_count(h.ptr(), &c), FIXPP_ERR_OK);
        // 8, 9, 35, 49, 56, 34, 10 → 7 entries
        ASSERT_EQ(c, 7U);
        for (size_t i = 0; i < c; ++i) {
            fixpp_msg_field_t f{};
            ASSERT_EQ(fixpp_msg_field_at(h.ptr(), i, &f), FIXPP_ERR_OK);
            ASSERT_NE(f.value, nullptr);
        }
    }
    if (alloc_guard_end) alloc_guard_end();
}

// ── SC-003 clone cross-strand: field iteration on a clone is THREAD_SAFE ──────
//
// Discriminating witness (gate-b/r1 F5): the clone is created inside a nested
// scope that holds the source buf/arena/MessageView/InboundHandle.  That scope
// is exited (destroying ALL source objects) BEFORE the clone is read on a
// separate thread.  If the clone secretly aliases the source buffer or arena,
// the iteration will produce a use-after-free under ASan or wrong data here.
//
// The FR-006/007 contract states a clone is self-contained (owns its deep-copied
// frame bytes via owned_frame_ + its own owned_view_) and is valid after the
// source dispatch window closes.  This topology proves that contract: the worker
// thread reads the clone only after every source object is gone.
TEST(MessageFieldIteration, CloneFieldIterationCrossThread) {
    fixpp_msg_t* clone = nullptr;

    // ── Nested scope: holds the source objects.  Clone is created here, then the
    //    scope exits, destroying buf / arena / mv / h — all source backing.
    {
        auto buf = make_raw_frame("35=D\x01" "49=SENDER\x01" "56=TARGET\x01");
        auto fv = fixpp::wire::test::make_frame_view(buf);
        ASSERT_TRUE(fv.has_value());
        std::pmr::monotonic_buffer_resource arena;
        MessageView<access_mode::Index> mv{*fv, &arena};

        InboundHandle h;
        h.msg.view = &mv;

        // Clone while source is live (clone deep-copies the frame bytes).
        ASSERT_EQ(fixpp_msg_clone(h.ptr(), &clone), FIXPP_ERR_OK);
        ASSERT_NE(clone, nullptr);
    }
    // ── Source scope has ended: buf / arena / mv / h are destroyed.
    //    If clone->owned_frame_ aliases buf, the next access is a use-after-free
    //    caught by ASan.  If clone->owned_view_ aliases mv, field offsets are
    //    dangling and values will be wrong.

    // Iterate the clone's fields from a separate thread.
    size_t thread_count = 0;
    uint16_t thread_tag0 = 0;
    std::string_view thread_tag0_val;
    std::thread worker([&]() {
        EXPECT_EQ(fixpp_msg_field_count(clone, &thread_count), FIXPP_ERR_OK);
        if (thread_count > 0) {
            fixpp_msg_field_t f{};
            EXPECT_EQ(fixpp_msg_field_at(clone, 0, &f), FIXPP_ERR_OK);
            thread_tag0 = f.tag;
            // Capture the value; it must alias the clone's OWN owned_frame_, not buf.
            thread_tag0_val = std::string_view(reinterpret_cast<const char*>(f.value), f.len);
        }
    });
    worker.join();

    // Frame: 8=FIX.4.4, 9=..., 35=D, 49=SENDER, 56=TARGET, 10=000 → 6 entries.
    EXPECT_EQ(thread_count, 6U);
    EXPECT_EQ(thread_tag0, 8U);               // BeginString is first in wire order
    EXPECT_EQ(thread_tag0_val, "FIX.4.4");    // value of tag 8

    fixpp_msg_destroy(clone);
}

}  // namespace
