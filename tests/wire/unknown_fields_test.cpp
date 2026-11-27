// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/unknown_fields_test.cpp — T017 (US1, seam #9).
// unknown_fields_view yields ONLY dictionary-MISSING tags (known-but-invalid
// tags raise wire_unexpected_tag via validator rule 5, US4 — not here).
//
// W1 (`.specify/688-707-559-wire-headroom-token.md` D-688-1, ruling R-W1-3): the view is the
// filtered view of [2b §4.8]. It walks the offset table's entries, classifying each against the
// dictionary the Parser was built with (table_view::tag_defined), and stores nothing, so the
// parse resource's state cannot change what it yields. A hand-built table defines every tag it
// was told anything about (table_view::mark_defined's comment), so in the cells below 35 and 34
// are defined and the 5000s are not. The real-dictionary cells are in unknown_fields_dict_test.cpp.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory_resource>
#include <string>
#include <string_view>
#include <utility>
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

std::vector<std::byte> make_raw_frame(std::string const& body) {
    std::string nine = "9=" + std::to_string(body.size()) + "\x01";
    std::string full = "8=FIX.4.4\x01" + nine + body + "10=000\x01";
    std::vector<std::byte> out(full.size());
    std::memcpy(out.data(), full.data(), full.size());
    return out;
}

// A hand-built table that defines 35, 34 and 55 (each registered for "D").
fixpp::dict::table_view known_35_34_55() {
    fixpp::dict::table_view_builder b;
    b.add_valid("D", 35).add_valid("D", 34).add_valid("D", 55);
    return std::move(b).build();
}

// What a full iteration of unknown_fields() yields: each field's tag and value.
std::vector<std::pair<std::uint16_t, std::string>> yielded(
    fixpp::wire::MessageView<access_mode::Index> const& mv) {
    std::vector<std::pair<std::uint16_t, std::string>> out;
    for (auto const& kv : mv.unknown_fields()) {
        out.emplace_back(kv.tag, std::string{reinterpret_cast<char const*>(kv.data), kv.len});
    }
    return out;
}

// Unknown fields interleaved with known ones come out in document order, each value aliasing the
// frame's bytes.
TEST(WireUnknownFields, DocumentOrderInterleavedWithKnownFields) {
    auto buf = make_raw_frame(
        "35=D\x01"
        "5000=alpha\x01"
        "34=1\x01"
        "5001=beta\x01"
        "55=X\x01"
        "5002=gamma\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    fixpp::dict::table_view const dict = known_35_34_55();
    std::pmr::monotonic_buffer_resource arena;
    Parser<access_mode::Index> parser{dict};
    auto mv = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv.has_value());

    std::vector<std::pair<std::uint16_t, std::string>> const expected{
        {5000, "alpha"}, {5001, "beta"}, {5002, "gamma"}};
    EXPECT_EQ(yielded(*mv), expected);
    for (auto const& kv : mv->unknown_fields()) {
        auto const* const lo = buf.data();
        auto const* const hi = buf.data() + buf.size();
        EXPECT_TRUE(kv.data >= lo && kv.data + kv.len <= hi)
            << "tag " << kv.tag << " does not alias the frame";
    }
}

// Unknown tags between two group instances, inside the second one, and after the group as a
// trailing scalar: every occurrence, in document order, whether or not the group was read first.
TEST(WireUnknownFields, UnknownTagsAroundAndInsideGroupInstancesInDocumentOrder) {
    fixpp::dict::table_view_builder b;
    b.add_valid("D", 35).add_valid("D", 34).add_valid("D", 453);
    b.set_group_first(453, 448).add_group_member(453, 448).add_group_member(453, 447);
    fixpp::dict::table_view const dict = std::move(b).build();
    auto buf = make_raw_frame(
        "35=D\x01"
        "34=1\x01"
        "453=2\x01"
        "448=A\x01"
        "447=D\x01"
        "5100=between\x01"
        "448=B\x01"
        "5101=inside\x01"
        "447=D\x01"
        "5102=trailing\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    std::pmr::monotonic_buffer_resource arena;
    Parser<access_mode::Index> parser{dict};
    auto mv = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv.has_value());

    std::vector<std::pair<std::uint16_t, std::string>> const expected{
        {5100, "between"}, {5101, "inside"}, {5102, "trailing"}};
    EXPECT_EQ(yielded(*mv), expected) << "before any group read";
    (void)mv->offsets().group_slices(453);
    EXPECT_EQ(yielded(*mv), expected) << "after the group was read";
}

// A repeated unknown tag is yielded at every occurrence, not once.
TEST(WireUnknownFields, DuplicateUnknownTagsAreEachYielded) {
    auto buf = make_raw_frame(
        "35=D\x01"
        "5000=one\x01"
        "34=1\x01"
        "5000=two\x01"
        "5000=three\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    fixpp::dict::table_view const dict = known_35_34_55();
    std::pmr::monotonic_buffer_resource arena;
    Parser<access_mode::Index> parser{dict};
    auto mv = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv.has_value());

    std::vector<std::pair<std::uint16_t, std::string>> const expected{
        {5000, "one"}, {5000, "two"}, {5000, "three"}};
    EXPECT_EQ(yielded(*mv), expected);
}

// A frame with no unknown field: empty(), and begin() == end().
TEST(WireUnknownFields, FrameWithNoUnknownFieldIsEmpty) {
    auto buf = make_raw_frame(
        "35=D\x01"
        "34=1\x01"
        "55=X\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    fixpp::dict::table_view const dict = known_35_34_55();
    std::pmr::monotonic_buffer_resource arena;
    Parser<access_mode::Index> parser{dict};
    auto mv = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv.has_value());

    auto const uf = mv->unknown_fields();
    EXPECT_TRUE(uf.empty());
    EXPECT_TRUE(uf.begin() == uf.end());
}

// The parse resource is the counter: whatever unknown_fields() and its iteration drew from it
// would show in allocate_calls(), which is read after the parse, because the parse itself
// allocates there.
TEST(WireUnknownFields, ReadingTheUnknownFieldsDrawsNothingFromTheParseResource) {
    auto buf = make_raw_frame(
        "35=D\x01"
        "34=1\x01"
        "5000=x\x01"
        "5001=y\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    fixpp::dict::table_view const dict = known_35_34_55();
    std::pmr::monotonic_buffer_resource arena;
    fixpp::test_support::pmr_allocation_tracking_resource counted{&arena};
    Parser<access_mode::Index> parser{dict};
    auto mv = parser.parse(*fv, &counted);
    ASSERT_TRUE(mv.has_value());
    std::size_t const after_parse = counted.allocate_calls();
    ASSERT_GT(after_parse, 0U) << "the counter sees the parse's own allocations";

    std::size_t n = 0;
    for (int pass = 0; pass < 2; ++pass) {
        for (auto const& kv : mv->unknown_fields()) {
            (void)kv;
            ++n;
        }
    }
    EXPECT_EQ(n, 4U);
    EXPECT_EQ(counted.allocate_calls(), after_parse)
        << "unknown_fields() or its iteration allocated from the parse resource";
}

// [PR68-02] Contract API: unknown_fields() uses the dict passed to Parser at
// construction time — no caller-supplied argument. The dict defines no tag 5000,
// so it must appear as unknown; known fields (35, 34) and framing
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
    // the dict defines no tag 5000 → must appear as unknown.
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

    // Iteration classifies against the dictionary, which outlives the Parser here, so a second
    // view and a second pass still classify after the Parser is gone.
    std::vector<std::pair<std::uint16_t, std::string>> const expected{{5000, "x"}};
    EXPECT_EQ(yielded(*mv), expected);
    EXPECT_EQ(yielded(*mv), expected);
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

// ── Q-32 (093-inbound-frame-dispositions, FR-015, fixpp#540; W1 D-688-1, fixpp#688) ─────────
//
// The parse resource here is a monotonic buffer over a heap block, its upstream set explicitly to
// null (through a tracking resource), so the result does not depend on the lane's
// arena_upstream(). The block is the smallest that lets the parse succeed with no upstream
// request, found by a sweep, so a list materialised in the parse resource would not fit in it.
// unknown_fields() stores nothing, so over that block it still yields every unknown field and
// makes no upstream request.

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
    Parser<access_mode::Index> parser;  // dict-free: every non-framing tag is unknown
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

// Positive control: over a block large enough for anything, the same frame yields every unknown
// field, the count the cell below requires over the smallest block.
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

TEST(WireUnknownFieldsQ32, ExhaustedArenaStillYieldsEveryUnknownFieldWithNoUpstreamRequest) {
    auto buf = q32::frame_with_unknown_fields();
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    std::size_t const size = q32::smallest_parse_only_block(*fv);
    ASSERT_NE(size, 0U) << "no block size lets the parse succeed without an upstream request";
    q32::BoundedParse p{*fv, size};
    ASSERT_TRUE(p.mv.has_value());
    ASSERT_EQ(p.upstream.allocate_calls(), 0U) << "the parse itself fits in the block";

    std::size_t n = 0;
    for (auto const& kv : p.mv->unknown_fields()) {
        (void)kv;
        ++n;
    }
    // 35, 34 and the 5000s: a dict-free view classifies every non-framing tag unknown.
    EXPECT_EQ(n, q32::kUnknownFields + 2U) << "the exhausted block emptied the view";
    EXPECT_EQ(p.upstream.allocate_calls(), 0U) << "unknown_fields() made an upstream request";
}

}  // namespace
