// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/unknown_fields_test.cpp — T017 (US1, seam #9).
// unknown_fields_view yields ONLY dictionary-MISSING tags (known-but-invalid
// tags raise wire_unexpected_tag via validator rule 5, US4 — not here). No
// vector materialization: the iterator walks a borrowed span of (tag,value)
// pairs the parser recorded in document order, so a round-trip preserves the
// original on-wire byte order. Authored red; GREEN against T024/T026.
//
// gate-b/r1: the dict is now captured by Parser and threaded into
// MessageView; unknown_fields() performs the real classification against
// the seam-#1 mock table_view (unknown = not in dict for this msg_type).

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// seam #1 — mock_dict_table.hpp BEFORE parser.hpp
#include <fixpp/wire/parser.hpp>
#include <fixpp/wire/unknown_fields.hpp>

#include "support/expired_parser_parse.hpp"
#include "support/frame_view_factory.hpp"
#include "support/mock_dict_table.hpp"
#include "support/pmr_allocation_tracking_resource.hpp"

namespace {

using fixpp::wire::access_mode;
using fixpp::wire::Parser;
using fixpp::wire::unknown_fields_view;

std::vector<std::byte> make_raw_frame(std::string const& body) {
    std::string nine = "9=" + std::to_string(body.size()) + "\x01";
    std::string full = "8=FIX.4.4\x01" + nine + body + "10=000\x01";
    std::vector<std::byte> out(full.size());
    std::memcpy(out.data(), full.data(), full.size());
    return out;
}

TEST(WireUnknownFields, DocumentOrderRoundTripNoMaterialization) {
    // Three "unknown" fields recorded in document order. The view borrows the
    // kv span (no owning vector) — iteration yields them in wire order and
    // each value aliases the originating buffer byte-for-byte.
    std::string raw =
        "5000=alpha\x01"
        "5001=beta\x01"
        "5000=gamma\x01";
    std::vector<std::byte> buf(raw.size());
    std::memcpy(buf.data(), raw.data(), raw.size());

    // Value slices, in document order (tag, ptr-into-buf, len).
    std::vector<unknown_fields_view::kv> items{
        {.tag = 5000, .data = buf.data() + 5, .len = 5},   // "alpha"
        {.tag = 5001, .data = buf.data() + 16, .len = 4},  // "beta"
        {.tag = 5000, .data = buf.data() + 26, .len = 5},  // "gamma" (repeat tag, kept in order)
    };
    unknown_fields_view uf{std::span<unknown_fields_view::kv const>{items.data(), items.size()},
                           {}};

    std::vector<std::uint16_t> tags;
    std::vector<std::string> vals;
    for (auto it = uf.begin(); !(it == uf.end()); ++it) {
        tags.push_back((*it).tag);
        vals.emplace_back(reinterpret_cast<char const*>((*it).data), (*it).len);
    }
    ASSERT_EQ(tags.size(), 3U);
    EXPECT_EQ(tags[0], 5000U);
    EXPECT_EQ(tags[1], 5001U);
    EXPECT_EQ(tags[2], 5000U);
    EXPECT_EQ(vals[0], "alpha");
    EXPECT_EQ(vals[1], "beta");
    EXPECT_EQ(vals[2], "gamma");
    EXPECT_FALSE(uf.empty());
}

// [PR68-02] Contract API: unknown_fields() uses the dict passed to Parser at
// construction time — no caller-supplied argument. Tag 5000 is not registered
// for "D", so it must appear as unknown; known fields (35, 34) and framing
// tags (8, 9, 10) must NOT appear. ([2b §4.3] / [2b §4.8])
TEST(WireUnknownFields, DictBoundUnknownSplit) {
    // Build a dict that knows 35 (MsgType) and 34 (MsgSeqNum) for "D".
    fixpp::dict::table_view_builder b;
    b.add_valid("D", 35)      // MsgType
        .add_valid("D", 34);  // MsgSeqNum
    fixpp::dict::table_view const dict = std::move(b).build();

    auto buf = make_raw_frame(
        "35=D\x01"
        "34=1\x01"
        "5000=x\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    // [2b §4.3]: Parser borrows the caller-owned dict at construction;
    // unknown_fields() uses it without a caller-supplied argument.
    Parser<access_mode::Index> parser{dict};
    auto mv = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv.has_value());

    // unknown_fields() — contract API, no arg — walks the offset table and
    // classifies against the dict stored at Parser construction time.
    auto uf = mv->unknown_fields();
    // tag 5000 is not in the dict for "D" → must appear as unknown.
    EXPECT_FALSE(uf.empty()) << "tag 5000 must be classified as unknown";
    std::vector<std::uint16_t> unknown_tags;
    for (auto it = uf.begin(); !(it == uf.end()); ++it) {
        unknown_tags.push_back((*it).tag);
    }
    EXPECT_EQ(unknown_tags.size(), 1U);
    EXPECT_EQ(unknown_tags[0], 5000U) << "only tag 5000 should be unknown; framing tags 8/9/10 and "
                                         "known dict tags 35/34 must not appear";
}

TEST(WireUnknownFields, UnknownFieldsRemainUsableAfterTemporaryParserDies) {
    fixpp::dict::table_view_builder b;
    b.add_valid("D", 35).add_valid("D", 34);
    fixpp::dict::table_view const dict = std::move(b).build();

    auto buf = make_raw_frame(
        "35=D\x01"
        "34=1\x01"
        "5000=x\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    auto mv = fixpp::wire::test::parse_with_expired_parser(dict, *fv, &arena);
    ASSERT_TRUE(mv.has_value());

    auto uf = mv->unknown_fields();
    std::vector<std::uint16_t> unknown_tags;
    for (auto it = uf.begin(); !(it == uf.end()); ++it) {
        unknown_tags.push_back((*it).tag);
    }

    ASSERT_EQ(unknown_tags.size(), 1U);
    EXPECT_EQ(unknown_tags[0], 5000U);
}

// With an empty dict (no known tags for this msg_type), all non-framing
// tags are classified as unknown by unknown_fields() (no-arg contract API).
TEST(WireUnknownFields, EmptyDictAllTagsUnknown) {
    auto buf = make_raw_frame(
        "35=D\x01"
        "34=1\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());

    std::pmr::monotonic_buffer_resource arena;
    // Empty dict: all non-framing tags will be classified as unknown.
    Parser<access_mode::Index> parser{};
    auto mv = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv.has_value());

    // unknown_fields() — no-arg contract API — uses the dict-free Parser path.
    // 35 (MsgType) and 34 (MsgSeqNum) are not in the empty dict → unknown.
    // Framing tags 8, 9, 10 must NOT appear.
    auto uf = mv->unknown_fields();
    bool has_35 = false;
    bool has_34 = false;
    bool has_framing = false;
    for (auto it = uf.begin(); !(it == uf.end()); ++it) {
        auto tag = (*it).tag;
        if (tag == 35) {
            has_35 = true;
        }
        if (tag == 34) {
            has_34 = true;
        }
        if (tag == 8 || tag == 9 || tag == 10) {
            has_framing = true;
        }
    }
    EXPECT_TRUE(has_35) << "tag 35 must appear as unknown with empty dict";
    EXPECT_TRUE(has_34) << "tag 34 must appear as unknown with empty dict";
    EXPECT_FALSE(has_framing) << "framing tags 8/9/10 must never appear as unknown";
}

// ── Q-32 (093-inbound-frame-dispositions, FR-015, fixpp#540) ─────────────────
//
// unknown_fields() is noexcept and pushes into a pmr vector over the view's parse
// resource. Over a resource whose upstream is null_memory_resource, a list that does
// not fit used to throw bad_alloc out of the noexcept body: std::terminate. The parse
// resource here is a monotonic buffer over a heap block, its upstream set explicitly to
// null (through a tracking resource), so the result does not depend on the lane's
// arena_upstream(). The block is the smallest that lets the parse succeed with no
// upstream request, found by a sweep, so the unknown-field list cannot fit.
//
// The calls run under EXPECT_EXIT(..., ExitedWithCode(0), ...): a terminate is a
// recorded failure of this cell, not a crashed binary.

namespace q32 {

constexpr std::size_t kUnknownFields = 40;

std::vector<std::byte> frame_with_unknown_fields() {
    std::string body =
        "35=D\x01"
        "34=1\x01";
    for (std::size_t i = 0; i < kUnknownFields; ++i) {
        body += "5000=x\x01";
    }
    return make_raw_frame(body);
}

// A parse over a monotonic buffer of `size` bytes whose upstream is a tracking resource
// over `final_upstream`: null_memory_resource for the cell, new_delete for the sizing
// sweep. The sweep forwards because MSVC's debug STL draws container proxies from the
// resource in the view's noexcept constructor, outside the parse's own catch, so a block
// too small for them would terminate the sweep rather than fail the parse. Members are
// declared in construction order.
struct BoundedParse {
    std::vector<std::byte> block;
    fixpp::test_support::pmr_allocation_tracking_resource upstream;
    std::pmr::monotonic_buffer_resource mr;
    Parser<access_mode::Index> parser{};  // dict-free: every non-framing tag is unknown
    fixpp::core::expected_t<fixpp::wire::MessageView<access_mode::Index>> mv;

    BoundedParse(fixpp::wire::frame_view const& fv, std::size_t size,
                 std::pmr::memory_resource* final_upstream = std::pmr::null_memory_resource())
        : block(size),
          upstream{final_upstream},
          mr{block.data(), block.size(), &upstream},
          mv{parser.parse(fv, &mr)} {}

    BoundedParse(BoundedParse const&) = delete;
    BoundedParse& operator=(BoundedParse const&) = delete;
    BoundedParse(BoundedParse&&) = delete;
    BoundedParse& operator=(BoundedParse&&) = delete;
    ~BoundedParse() = default;
};

// The smallest block, in steps of 8, over which the parse makes no upstream request;
// 0 if none up to the sweep's end.
std::size_t smallest_parse_only_block(fixpp::wire::frame_view const& fv) {
    for (std::size_t size = 16; size <= 65536; size += 8) {
        BoundedParse p{fv, size, std::pmr::new_delete_resource()};
        if (p.mv.has_value() && p.upstream.allocate_calls() == 0U) return size;
    }
    return 0;
}

}  // namespace q32

// Positive control: over a block large enough for the list, the same frame yields
// every unknown field, so an empty view in the cell below is the exhaustion's.
TEST(WireUnknownFieldsQ32, ControlTheListFitsAndHoldsEveryUnknownField) {
    auto buf = q32::frame_with_unknown_fields();
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    q32::BoundedParse p{*fv, 65536};
    ASSERT_TRUE(p.mv.has_value());
    std::size_t n = 0;
    auto const uf = p.mv->unknown_fields();
    for (auto it = uf.begin(); !(it == uf.end()); ++it) ++n;
    // 35, 34 and the 5000s: a dict-free view classifies every non-framing tag unknown.
    EXPECT_EQ(n, q32::kUnknownFields + 2U);
    EXPECT_EQ(p.upstream.allocate_calls(), 0U) << "the control's list fits in its block";
}

TEST(WireUnknownFieldsQ32, ExhaustedArenaReturnsTheSameEmptyViewAndTheProcessLives) {
    auto buf = q32::frame_with_unknown_fields();
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    std::size_t const size = q32::smallest_parse_only_block(*fv);
    ASSERT_NE(size, 0U) << "no block size lets the parse succeed without an upstream request";
    q32::BoundedParse p{*fv, size};
    ASSERT_TRUE(p.mv.has_value());
    ASSERT_EQ(p.upstream.allocate_calls(), 0U) << "the parse itself fits in the block";

    // Exit codes: 0 = both calls returned the same empty view; 1 = the first call
    // returned a non-empty view (the list fitted, so nothing was exhausted);
    // 2 = the second call differs from the first.
    EXPECT_EXIT(
        {
            auto const first = p.mv->unknown_fields();
            auto const second = p.mv->unknown_fields();
            if (!first.empty()) std::exit(1);
            if (!second.empty() || !(first.begin() == second.begin())) std::exit(2);
            std::exit(0);
        },
        ::testing::ExitedWithCode(0), "");
}

}  // namespace
