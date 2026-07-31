// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/typed_read_split_agreement_test.cpp
//
// 083-group-delimiter-resolution T011 (FR-021e / SC-016;
// contracts/typed_read_splitter.md C-8.0c, W-10a) — W-10a **leg 1 only**,
// observed RED (quickstart.md §2a):
//
//   35=X | 100=2 | 200=1 201=A | 200=1 201=B
//
// The SAME #208 B-2 shape T010 (tests/wire/consume_group_nested_delim_test.cpp)
// reproduces on the VALIDATION path, read back through the **read** path
// instead: `Parser<Index>{tv}.parse()` -> `offsets().group(100)`. This is a
// second scanner with the same asymmetry, in a different subsystem
// (`OffsetTable::consume_group_extent`, src/wire/offset_table.cpp:438-503):
// it consumes the instance-opening delimiter with a bare `++k` (:475) and
// descends into a nested group only for members scanned AFTER the delimiter
// (:485-488), never AT the delimiter position itself. Outer group 100
// (NoOuter) is delimited by 200 — which is itself nested group 200's
// (NoInner's) own count tag — so the second instance's opening tag (the
// second `200=1`) is never reached via a nesting-aware descent, and the
// extent walk under-reports.
//
// Only leg 1 of W-10a (the four legs in contracts/typed_read_splitter.md's
// "Witnesses" section) is exercised here. Legs 2-4 (instance-count-both-
// directions, validation agreement, the depth-cap early-return) are T020 /
// T021 / T023's scope, per the brief that names this task.
//
// Anchors:
//   tasks:     specs/083-group-delimiter-resolution/tasks.md T011
//   quickstart: specs/083-group-delimiter-resolution/quickstart.md §2a
//   contract:  specs/083-group-delimiter-resolution/contracts/typed_read_splitter.md
//              (C-8.0c, W-10a leg 1)

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/wire/parser.hpp>
#include <memory_resource>
#include <string>
#include <string_view>
#include <vector>

#include "support/frame_view_factory.hpp"

namespace {

using fixpp::dict::table_view;
using fixpp::wire::access_mode;
using fixpp::wire::MessageView;
using fixpp::wire::Parser;

// ── Shared wire-frame helpers (mirrors consume_group_nested_delim_test.cpp) ──
std::string make_checksum_field(unsigned chk) {
    std::string s = "10=";
    s.push_back(static_cast<char>('0' + ((chk / 100U) % 10U)));
    s.push_back(static_cast<char>('0' + ((chk / 10U) % 10U)));
    s.push_back(static_cast<char>('0' + (chk % 10U)));
    s.push_back('\x01');
    return s;
}

std::vector<std::byte> make_frame(std::string_view body_fields) {
    std::string body{body_fields};
    std::string nine = "9=" + std::to_string(body.size()) + "\x01";
    std::string pre = "8=FIX.4.4\x01" + nine + body;
    unsigned sum = 0;
    for (unsigned char c : pre) {
        sum += c;
    }
    std::string full = pre + make_checksum_field(sum % 256U);
    std::vector<std::byte> out(full.size());
    std::memcpy(out.data(), full.data(), full.size());
    return out;
}

// ── T010's bare fixture, rebuilt here (T010's version is file-scoped to
// consume_group_nested_delim_test.cpp) — SAME bytes and SAME dictionary
// wiring, so this case and T010/T020's later leg-3 agreement check share one
// fixture. msg_type "X". Outer group NoOuter(100), delimiter NoInner(200) —
// itself the nested group NoInner's own count tag, delimiter InnerField(201).
// 201 is registered only as NoInner's member — never as a member of NoOuter
// itself (contract C-4.1's "the tags [inside the nested group] are not
// members of the outer group" framing).
table_view make_bare_nested_delim_dict() {
    table_view tv;
    for (std::uint16_t const t : {std::uint16_t{8}, std::uint16_t{9}, std::uint16_t{10},
                                  std::uint16_t{35}, std::uint16_t{100}, std::uint16_t{200},
                                  std::uint16_t{201}}) {
        tv.add_valid("X", t);
    }
    tv.set_group_first(100, 200);  // NoOuter: delimiter = NoInner's own count tag
    tv.set_group_first(200, 201);  // NoInner: delimiter = InnerField
    return tv;
}

}  // namespace

// ============================================================================
// W-10a leg 1 — the extent walk's reported extent for group 100, read back
// through the typed-read path rather than validated (contracts/
// typed_read_splitter.md C-8.0c.1, W-10a leg 1).
// ============================================================================
TEST(TypedReadSplitAgreement, ExtentWalkDescendsAtNestedGroupDelimiter_Leg1ExtentByConstruction) {
    // Two instances of the #208 B-2 shape: outer group 100 declares count=2,
    // each instance opened by its delimiter 200 (itself NoInner's count tag,
    // with one nested InnerField(201) member per instance).
    auto tv = make_bare_nested_delim_dict();
    auto buf = make_frame("35=X\x01"
                          "100=2\x01"
                          "200=1\x01" "201=A\x01"
                          "200=1\x01" "201=B\x01");

    auto fv = fixpp::wire::test::make_frame_view(buf);
    ASSERT_TRUE(fv.has_value()) << "make_frame_view failed";

    // Dict-AWARE parser (constructed over `tv`, not the default dict-free
    // ctor) — offsets().group() only exercises consume_group_extent's
    // membership-driven descent when opaque_dict_/group_member_fn_ are
    // threaded; the dict-free fallback degrades to rest-of-message (offset_
    // table.hpp:150-157) and would not exercise the defect at all.
    Parser<access_mode::Index> parser{tv};
    std::pmr::monotonic_buffer_resource arena;
    auto mv = parser.parse(*fv, &arena);
    ASSERT_TRUE(mv.has_value()) << "parser.parse failed";

    auto gi = mv->offsets().group(100);
    ASSERT_TRUE(gi.has_value())
        << "group(100) lookup itself failed, error=" << static_cast<int>(gi.error());

    // ── Hand-derivation of the expected post-fix entry_count(), from the
    // fixture's own entry layout — NEVER captured from the implementation.
    //
    // OffsetTable::entries_ for this frame (index : tag : value):
    //   0:8=FIX.4.4  1:9=<len>  2:35=X  3:100=2  4:200=1  5:201=A
    //   6:200=1      7:201=B    8:10=<chk>
    //
    // group(100): count_idx=3 (tag 100), first=4 (tag 200, the delimiter).
    //
    // consume_group_extent(count_idx=3, ctx=root, depth=0) — POST-FIX
    // (C-8.0c.1: probe whether `delim` is itself a group in the child
    // context BEFORE consuming it with a bare ++k; if so, descend via
    // recursion rather than skipping past it):
    //
    //   Instance 1 (outer k starts at 4, delim=200):
    //     - probe: is 200 (the delimiter) itself a group under child=[100]?
    //       yes (set_group_first(200,201) registers 200 as a group whose
    //       first member is 201) -> descend via consume_group_extent(4,
    //       [100], depth=1) instead of bare ++k.
    //       - that recursion: count_idx=4 (tag 200), first=5 (tag 201,
    //         itself now the delimiter). delim'=201; declared'=1 (the
    //         value of THIS 200 field). Consumes the single instance
    //         {201=A} (entries 5), then entries_[6].tag=200 != 201 (the
    //         nested group's own delimiter) ends the nested loop.
    //         Returns k=6 -> nested extent = entries [4,6) = {200, 201} =
    //         2 entries (the delimiter itself + its one member).
    //     - k is now 6. Outer inner-loop test: entries_[6].tag(200) ==
    //       outer delim(200) -> instance 1 ends here (no further scalar
    //       members after the nested descent). Instance 1 spans entries
    //       [4,6) = 2 entries.
    //   Instance 2 (outer k=6, inst=1 < declared=2, entries_[6].tag==200):
    //     - same probe/descent at k=6: consume_group_extent(6, [100],
    //       depth=1) — count_idx=6 (tag 200), first=7 (tag 201, "B").
    //       declared'=1, consumes entry 7, then entries_[8].tag=10
    //       (CheckSum) != 201 ends the nested loop; 10 is also not a
    //       member of group 200 in context [100] -> nested loop returns
    //       k=8. Nested extent = entries [6,8) = {200, 201} = 2 entries.
    //     - k is now 8. Outer inner-loop test: entries_[8].tag(10) !=
    //       outer delim(200) -> inner loop runs once more: t=10, NOT a
    //       member of group 100 under root ctx -> breaks (ends instance
    //       AND the group). Instance 2 spans entries [6,8) = 2 entries.
    //     - inst becomes 2 == declared(2) -> outer loop exits. Returns k=8.
    //
    //   group_end = 8, first = 4 -> entry_count() = group_end - first = 4.
    //
    // Per-instance arithmetic, generalised as the brief asks (delimiter
    // count field + nested extent + remaining members), per instance:
    //   1 (delimiter tag 200, itself NoInner's own count field)
    //   + 1 (NoInner's single declared member, InnerField(201))
    //   + 0 (no further outer-group scalar members follow the nested group
    //        in this fixture — NoOuter's only child is NoInner)
    //   = 2 entries/instance x 2 declared instances = 4.
    //
    // Expected POST-FIX entry_count() = 4 (spans BOTH instances' full
    // nested content: {200,201,200,201} = entries 4,5,6,7). The trailing
    // CheckSum(10) at entry 8 correctly stays OUTSIDE the group extent.
    constexpr std::size_t kExpectedPostFixEntryCount = 4;

    EXPECT_EQ(gi->entry_count(), kExpectedPostFixEntryCount)
        << "W-10a leg 1 (C-8.0c, FR-021e): group(100)'s reported extent must span ALL "
           "declared instances (hand-derived = 4 entries: {200,201,200,201}), not just the "
           "first instance's opening delimiter. TODAY this is expected RED — "
           "consume_group_extent's bare `++k` at the instance-opening delimiter (offset_"
           "table.cpp:475) never descends into the nested group headed by that same "
           "delimiter, so the second instance's opening tag is never reached. observed="
        << gi->entry_count();
}
