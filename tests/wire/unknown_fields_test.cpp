// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/unknown_fields_test.cpp — T017 (US1, seam #9).
// unknown_fields_view yields ONLY dictionary-MISSING tags (known-but-invalid
// tags raise wire_unexpected_tag via validator rule 5, US4 — not here).
//
// W1 (`.specify/688-707-559-wire-headroom-token.md` D-688-1, ruling R-W1-3): the view is the
// filtered view of [2b §4.8]. It walks the offset table's entries, classifying each against the
// dictionary the Parser was built with (table_view::tag_defined), and allocates nothing, so the
// parse resource's state cannot change what it yields. A hand-built table defines every tag it
// was told anything about (table_view::mark_defined's comment), so in the cells below 35 and 34
// are defined and the 5000s are not. The real-dictionary cells are in unknown_fields_dict_test.cpp.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory_resource>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// seam #1 — mock_dict_table.hpp BEFORE parser.hpp
#include <fixpp/wire/parser.hpp>
#include <fixpp/wire/unknown_fields.hpp>

#include "support/dict_hooks_test_access.hpp"
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

// ── D-688-2 (W1, R-W1-6): the first-unknown index ─────────────────────────────────────────
//
// The MessageView remembers where its first unknown entry is
// (`.specify/688-707-559-wire-headroom-token.md` §1.4a). These cells build the view with the
// three-argument constructor over a bundle whose classify stub counts its calls through the opaque
// dictionary it is given; the group hooks are null, which these frames do not need.

namespace first_unknown {

// The stub's dictionary: a call counter, and which tags it defines.
struct counting_dict {
    mutable std::size_t calls = 0;
    bool defines_every_tag = false;  // else it defines the tags below 5000
};

bool counting_classify(void const* dict, std::string_view /*msg_type*/,
                       std::uint16_t tag) noexcept {
    auto const* const d = static_cast<counting_dict const*>(dict);
    ++d->calls;
    return d->defines_every_tag || tag < 5000;
}

fixpp::wire::dict_hooks counting_hooks(counting_dict const& d) {
    return fixpp::wire::dict_hooks_test_access::make(&d, &counting_classify, nullptr, nullptr,
                                                     nullptr, nullptr);
}

bool is_framing(std::uint16_t tag) { return tag == 8 || tag == 9 || tag == 10; }

}  // namespace first_unknown

// (a) After the first call, a later call makes no classify call before the first unknown entry,
// and an iteration classifies only the entries after it.
TEST(WireUnknownFieldsFirstUnknownIndex, LaterCallMakesNoClassifyCallBeforeTheFirstUnknownEntry) {
    auto buf = make_raw_frame(
        "35=D\x01"
        "34=1\x01"
        "55=X\x01"
        "5000=a\x01"
        "5001=b\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    first_unknown::counting_dict const dict{};
    std::pmr::monotonic_buffer_resource arena;
    fixpp::wire::MessageView<access_mode::Index> const mv{*fv, &arena,
                                                          first_unknown::counting_hooks(dict)};

    // The expected counts, from the table: the non-framing entries up to and including the first
    // undefined one, and the non-framing entries after it.
    std::size_t through_first_unknown = 0;
    std::size_t after_first_unknown = 0;
    std::size_t defined_before = 0;
    bool seen_unknown = false;
    for (auto const& e : mv.offsets().entries()) {
        if (first_unknown::is_framing(e.tag)) continue;
        if (seen_unknown) {
            ++after_first_unknown;
            continue;
        }
        ++through_first_unknown;
        if (e.tag >= 5000) {
            seen_unknown = true;
        } else {
            ++defined_before;
        }
    }
    ASSERT_TRUE(seen_unknown);
    ASSERT_GE(defined_before, 2U)
        << "the frame needs two defined tags before its first unknown one";
    ASSERT_GE(after_first_unknown, 1U);

    dict.calls = 0;
    auto const first = mv.unknown_fields();
    EXPECT_FALSE(first.empty());
    EXPECT_EQ(dict.calls, through_first_unknown)
        << "the first call scans to the first unknown entry";

    dict.calls = 0;
    auto const later = mv.unknown_fields();
    EXPECT_FALSE(later.empty());
    EXPECT_FALSE(later.begin() == later.end());
    EXPECT_EQ(dict.calls, 0U) << "a later call classified entries before the first unknown one";

    dict.calls = 0;
    std::vector<std::pair<std::uint16_t, std::string>> got;
    for (auto const& kv : later) {
        got.emplace_back(kv.tag, std::string{reinterpret_cast<char const*>(kv.data), kv.len});
    }
    std::vector<std::pair<std::uint16_t, std::string>> const expected{{5000, "a"}, {5001, "b"}};
    EXPECT_EQ(got, expected);
    EXPECT_EQ(dict.calls, after_first_unknown)
        << "an iteration classifies exactly the entries after the first unknown one";
}

// (c) A frame with no unknown field: the first call classifies every non-framing entry, and a
// later call classifies nothing.
TEST(WireUnknownFieldsFirstUnknownIndex, FrameWithNoUnknownFieldIsClassifiedOnce) {
    auto buf = make_raw_frame(
        "35=D\x01"
        "34=1\x01"
        "55=X\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    first_unknown::counting_dict const dict{.calls = 0, .defines_every_tag = true};
    std::pmr::monotonic_buffer_resource arena;
    fixpp::wire::MessageView<access_mode::Index> const mv{*fv, &arena,
                                                          first_unknown::counting_hooks(dict)};

    std::size_t non_framing = 0;
    for (auto const& e : mv.offsets().entries()) {
        if (!first_unknown::is_framing(e.tag)) ++non_framing;
    }
    ASSERT_GE(non_framing, 3U);

    dict.calls = 0;
    EXPECT_TRUE(mv.unknown_fields().empty());
    EXPECT_EQ(dict.calls, non_framing) << "the first call classifies every non-framing entry";

    for (int call = 0; call < 3; ++call) {
        dict.calls = 0;
        auto const uf = mv.unknown_fields();
        EXPECT_TRUE(uf.empty()) << "call " << call;
        EXPECT_TRUE(uf.begin() == uf.end()) << "call " << call;
        EXPECT_EQ(dict.calls, 0U) << "call " << call << " classified again";
    }
}

// (b) Moves; there are no copies (the copy pins are in parser_index_test.cpp). The source is moved
// from with its index set, and with it unset. The destination owns the buffer and yields the
// source's list, also after the source is destroyed. The moved-from source, read while the
// destination lives, yields nothing; the oracle is the death-test child's exit code, so a crash
// or a non-empty view stays inside this cell.
void moved_from_arm(bool set_index_before_the_move) {
    auto buf = make_raw_frame(
        "35=D\x01"
        "34=1\x01"
        "5000=a\x01"
        "55=X\x01"
        "5001=b\x01");
    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value());
    fixpp::dict::table_view const dict = known_35_34_55();
    std::pmr::monotonic_buffer_resource arena;
    Parser<access_mode::Index> parser{dict};
    std::optional<fixpp::core::expected_t<fixpp::wire::MessageView<access_mode::Index>>> src{
        parser.parse(*fv, &arena)};
    ASSERT_TRUE(src->has_value());

    std::vector<std::pair<std::uint16_t, std::string>> const expected{{5000, "a"}, {5001, "b"}};
    if (set_index_before_the_move) {
        ASSERT_EQ(yielded(**src), expected);
    }

    fixpp::wire::MessageView<access_mode::Index> b{std::move(**src)};

    // M4 shows only over an empty moved-from table, so a standard library that kept the
    // elements reports here instead of leaving M4 untested.
    ASSERT_TRUE((**src).offsets().entries().empty())
        << "precondition: the moved-from source's table is empty";
    EXPECT_EXIT(
        {
            auto const uf = (**src).unknown_fields();
            std::_Exit(uf.empty() && uf.begin() == uf.end() ? 0 : 1);
        },
        ::testing::ExitedWithCode(0), "");

    EXPECT_EQ(yielded(b), expected) << "the destination, while the source lives";
    src.reset();
    EXPECT_EQ(yielded(b), expected) << "the destination, after the source is destroyed";
}

TEST(WireUnknownFieldsFirstUnknownIndexDeathTest, MovedFromSourceWithTheIndexSetYieldsNothing) {
    moved_from_arm(/*set_index_before_the_move=*/true);
}

TEST(WireUnknownFieldsFirstUnknownIndexDeathTest, MovedFromSourceWithTheIndexUnsetYieldsNothing) {
    moved_from_arm(/*set_index_before_the_move=*/false);
}

// (f) The store, detail::remember_first_unknown: an index that fits the uint32 slot is written, and
// one that does not (the sentinel, and indices past 2^32 that a cast would wrap) leaves the slot
// as it was. The slot starts at a value no case stores, so a store that skipped the check shows,
// and each index is read through a volatile, so the check runs at run time.
std::uint32_t store_first_unknown(std::size_t idx) {
    constexpr std::uint32_t kSeed = 37;
    std::uint32_t slot = kSeed;
    std::size_t volatile runtime_idx = idx;
    fixpp::wire::detail::remember_first_unknown(slot, runtime_idx);
    return slot;
}

TEST(WireUnknownFieldsFirstUnknownIndex, StoreWritesOnlyAnIndexThatFits) {
    using fixpp::wire::detail::kFirstUnknownNotComputed;
    constexpr std::uint32_t kSeed = 37;
    EXPECT_EQ(store_first_unknown(kFirstUnknownNotComputed), kSeed) << "the sentinel was stored";
    EXPECT_EQ(store_first_unknown(kFirstUnknownNotComputed - 1U), kFirstUnknownNotComputed - 1U);
    EXPECT_EQ(store_first_unknown(0), 0U);
    if constexpr (sizeof(std::size_t) > sizeof(std::uint32_t)) {
        auto const two_to_32 = static_cast<std::size_t>(std::uint64_t{1} << 32U);
        EXPECT_EQ(store_first_unknown(two_to_32), kSeed) << "2^32 was stored (it wraps to 0)";
        EXPECT_EQ(store_first_unknown(two_to_32 + 5U), kSeed) << "2^32 + 5 was stored (wraps to 5)";
    }
}

// ── Q-32 (093-inbound-frame-dispositions, FR-015, fixpp#540; W1 D-688-1, fixpp#688) ─────────
//
// The parse resource here is a monotonic buffer over a heap block, its upstream set explicitly to
// null (through a tracking resource), so the result does not depend on the lane's
// arena_upstream(). The block is the smallest that lets the parse succeed with no upstream
// request, found by a sweep, so a list materialised in the parse resource would not fit in it.
// unknown_fields() allocates nothing, so over that block it still yields every unknown field and
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

    // Two calls, each iterated: the second starts at the stored index (D-688-2).
    for (int call = 0; call < 2; ++call) {
        std::size_t n = 0;
        for (auto const& kv : p.mv->unknown_fields()) {
            (void)kv;
            ++n;
        }
        // 35, 34 and the 5000s: a dict-free view classifies every non-framing tag unknown.
        EXPECT_EQ(n, q32::kUnknownFields + 2U)
            << "call " << call << ": the exhausted block emptied the view";
    }
    EXPECT_EQ(p.upstream.allocate_calls(), 0U) << "unknown_fields() made an upstream request";
}

}  // namespace
