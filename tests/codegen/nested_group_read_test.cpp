// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/codegen/nested_group_read_test.cpp — 062 T015/T018 [US2]
//
// US2 AC1 / FR-002 / INV-G3 / INV-G7 / SC-001: typed reads through the
// GENERATED nested-group descent chain (MassQuote `NoQuoteSets(296) ->
// NoQuoteEntries(295) -> NoLegs(555)`).
//
// 062 nested witnesses use a HAND-BUILT `fixpp::dict::table_view` (declaring
// group membership explicitly, the same builder API `tests/wire/
// group_slice_trailing_soh_test.cpp` T008 uses) rather than
// `Dictionary::as_table_view()` over the real v44 XML. The real loader
// mis-resolves the reused NumInGroup tag 295 (it registers repeating groups
// globally first-seen-wins by NumInGroup tag, so 295 resolves to the WRONG
// variant — QuotCxlEntriesGrp, lacking QuoteEntryID/BidPx/OfferPx — instead
// of MassQuote's QuotEntryGrp); see
// research/findings/dict-group-tag-collision-2026-07-05.md. That defect is
// fixed in prereq 063, NOT here. The hand-built table_view declares the
// CORRECT membership per FIX44.xml's QuotSetGrp/QuotEntryGrp/InstrumentLeg
// component definitions, so these witnesses prove the 062 slicer/cache
// mechanism (`OffsetTable::nested_group_slices`, T005/T006/T016) over
// GENERATED flyweights (`fixpp::v44::MassQuote`); the real-XML-dictionary
// path for MassQuote is L-062-* limited pending 063.
//
// ESCALATED (out of scope for 062, see final TEST below):
// `NestedQuoteEntriesPerInstancePrices` (>=2 QuoteEntries within ONE
// QuoteSet occurrence) cannot be made to pass without a change to
// PRE-EXISTING `OffsetTable::group()` (src/wire/offset_table.cpp — present
// unmodified on `main`, confirmed via `git show main:src/wire/
// offset_table.cpp | grep seen_in_instance` and `git log main..HEAD --
// src/wire/offset_table.cpp`, which shows 062 only ADDED
// build_nested_subview()/nested_group_slices(), never touched group()).
// group()'s per-top-level-occurrence boundary walk uses a "has this tag
// already appeared since the last delimiter" heuristic
// (offset_table.cpp:~420-435, `seen_in_instance`) that has no notion of
// nesting depth: once a nested group's own descendant tags (299/132/133,
// necessarily registered as transitive members of the OUTER group 296 so
// the outer occurrence's byte-span extends far enough to cover them) repeat
// a SECOND time within one outer occurrence (because the nested group has a
// SECOND entry), the heuristic mistakes the repeat for "next outer
// instance" and truncates the outer QuoteSet's slice mid-way through its
// first nested QuoteEntry — before the second QuoteEntry's bytes. This is a
// genuine pre-existing wire-layer defect that 062 is the first feature to
// expose (US2 is the first caller to descend a nested group with >1 entry
// via this exact codepath); fixing `OffsetTable::group()` is a production
// wire-layer change outside 062's T015/T018 (test-only, no production
// source) scope. See the trailing TEST (GTEST_SKIP, not a fake pass) for
// the frame bytes / expected-vs-actual proof.
//
// Test-only: no production/codegen/dictionary source is modified by this
// file.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fixpp/core/decimal_alias.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/v44/Messages.hpp>
#include <fixpp/wire/parser.hpp>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using MV = fixpp::wire::MessageView<fixpp::wire::access_mode::Index>;
using fixpp::decimal_t;

// Build a well-formed FIX frame: "8=FIX.4.4<SOH> 9=<len><SOH> <body> 10=<chk><SOH>"
// body must already begin with "35=X<SOH>" and contain SOH-delimited fields.
std::vector<std::byte> make_frame(std::string_view body) {
    std::string pre =
        "8=FIX.4.4\x01" + std::string("9=") + std::to_string(body.size()) + "\x01" +
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

decimal_t parse_decimal(std::string_view sv, std::pmr::memory_resource* mr) {
    auto r = decimal_t::parse(
        std::span<const std::byte>{reinterpret_cast<const std::byte*>(sv.data()), sv.size()}, mr);
    EXPECT_TRUE(r.has_value()) << "parse_decimal failed for: " << sv;
    return r.value_or(decimal_t{});
}

// The group-membership predicate `Parser`'s dict-lvalue ctor installs
// (parser.hpp:462-472) — mirrors tests/wire/group_slice_trailing_soh_test.cpp
// T008's own copy, needed here only to construct the Parser via the
// dict-lvalue ctor (which builds this same predicate internally); no direct
// call is required by this file, but the pattern is documented here for
// readers cross-referencing T008.

// Hand-built CORRECT MassQuote group membership (FIX44.xml component defs:
// QuotSetGrp:3350-3358 for NoQuoteSets(296); QuotEntryGrp:3219-3247 for
// NoQuoteEntries(295); InstrumentLeg:2436+ for NoLegs(555)'s InstrumentLeg
// fields). `OffsetTable::group()`'s per-top-level-occurrence boundary walk
// (src/wire/offset_table.cpp) needs the OUTER group(296)'s member set to
// include every tag transitively nested under a QuoteSet occurrence (302
// QuoteSetID, 295 NoQuoteEntries + its descendants 299/132/133/555/602, and
// 367 QuoteSetValidUntilTime when present) so the outer occurrence's byte
// span extends far enough to cover the nested content — this mirrors
// exactly what `Dictionary::as_table_view()`'s recursive `expand_field_list`
// would produce for a non-collided dictionary (dictionary.cpp:295-339 /
// xml_loader.cpp:416-524).
fixpp::dict::table_view make_correct_massquote_dict() {
    fixpp::dict::table_view dict;
    dict.add_group_member(296, 302)   // QuoteSetID — NoQuoteSets' own delimiter
        .add_group_member(296, 295)   // NoQuoteEntries (nested group's own count field)
        .add_group_member(296, 299)   // QuoteEntryID — transitively under 296
        .add_group_member(296, 132)   // BidPx — transitively under 296
        .add_group_member(296, 133)   // OfferPx — transitively under 296
        .add_group_member(296, 555)   // NoLegs — transitively under 296
        .add_group_member(296, 602)   // LegSecurityID — transitively under 296
        .add_group_member(296, 367)   // QuoteSetValidUntilTime — direct member of 296
        .add_group_member(295, 299)   // QuoteEntryID — NoQuoteEntries' own delimiter
        .add_group_member(295, 132)   // BidPx — direct member of 295
        .add_group_member(295, 133)   // OfferPx — direct member of 295
        .add_group_member(295, 555)   // NoLegs — direct member of 295
        .add_group_member(295, 602)   // LegSecurityID — transitively under 295
        .add_group_member(555, 602);  // LegSecurityID — NoLegs' own delimiter
    return dict;
}

}  // namespace

// FR-002 / INV-G3 — depth-3 non-first-outer-occurrence collision discriminator.
// Two QuoteSets, each with one QuoteEntry holding one Leg; the two Legs carry
// DIFFERENT LegSecurityID(602) values ("LEGA" under NoQuoteSets[0], "LEGB"
// under NoQuoteSets[1]). We read via NoQuoteSets[1] (the NON-first outer
// occurrence) and assert LEGB — targeting [1], not [0], is deliberate: the
// nested sub-view cache is keyed by the outer entry slice's `data` pointer
// (INV-G3). A defective `(no_tag + ordinal)` key would collide across
// repeated outer occurrences at ordinal 0 (QuoteSets[0]'s legs()[0] and
// QuoteSets[1]'s legs()[0] share the SAME ordinal key) and would silently
// serve QuoteSets[0]'s cached sub-view/value ("LEGA") when descending through
// QuoteSets[1] — so this witness targeting [1] fails under an ordinal key
// and passes only under the correct slice-data-pointer key. Asserting via
// [0] alone would NOT discriminate (an ordinal key happens to be correct at
// ordinal 0 for the very first outer occurrence).
TEST(NestedGroupRead, Depth3NonFirstOuterOccurrenceNoCollision) {
    std::string body =
        "35=i\x01"
        "296=2\x01"
        "302=SET0\x01"
        "295=1\x01"
        "299=E0\x01"
        "132=10.10\x01"
        "133=10.20\x01"
        "555=1\x01"
        "602=LEGA\x01"
        "302=SET1\x01"
        "295=1\x01"
        "299=E1\x01"
        "132=20.10\x01"
        "133=20.20\x01"
        "555=1\x01"
        "602=LEGB\x01";

    std::pmr::monotonic_buffer_resource arena{16384};
    auto dict = make_correct_massquote_dict();

    auto buf = make_frame(body);
    fixpp::wire::pmr_carry_buffer carry{buf.size(), &arena};
    fixpp::wire::Framer fr{};
    fixpp::wire::frame_view fvs[1]{};
    auto framed = fr.feed(
        std::span<const std::byte>{buf.data(), buf.size()}, carry,
        std::span<fixpp::wire::frame_view>{fvs, 1});
    ASSERT_TRUE(framed.has_value());
    ASSERT_FALSE(framed->empty());

    fixpp::wire::Parser<fixpp::wire::access_mode::Index> parser{dict};
    auto mv_exp = parser.parse((*framed)[0], &arena);
    ASSERT_TRUE(mv_exp.has_value());

    fixpp::v44::MassQuote mq{*mv_exp};
    auto sets = mq.quote_sets();
    ASSERT_EQ(sets.size(), 2U);

    // Contrast case: QuoteSets[0]'s own leg must read "LEGA".
    auto quote_set0 = sets[0];
    auto entries0 = quote_set0.quote_entries();
    ASSERT_EQ(entries0.size(), 1U);
    auto entry0_0 = entries0[0];
    auto legs0 = entry0_0.legs();
    ASSERT_EQ(legs0.size(), 1U);
    auto leg0_0 = legs0[0];
    auto sec0 = leg0_0.leg_security_id();
    ASSERT_TRUE(sec0.has_value()) << "QuoteSets[0].legs()[0].leg_security_id() must have value";
    EXPECT_EQ(*sec0, "LEGA");

    // Discriminator: QuoteSets[1] (the NON-first outer occurrence) must read
    // its OWN leg value "LEGB", not QuoteSets[0]'s cached "LEGA".
    auto quote_set1 = sets[1];
    auto entries1 = quote_set1.quote_entries();
    ASSERT_EQ(entries1.size(), 1U);
    auto entry1_0 = entries1[0];
    auto legs1 = entry1_0.legs();
    ASSERT_EQ(legs1.size(), 1U);
    auto leg1_0 = legs1[0];
    auto sec1 = leg1_0.leg_security_id();
    ASSERT_TRUE(sec1.has_value()) << "QuoteSets[1].legs()[0].leg_security_id() must have value";
    EXPECT_EQ(*sec1, "LEGB");
}

// 063 Phase 4 (US2 Defect B) — depth-3 AC3: multiple entries at MORE THAN ONE
// level simultaneously (no cross-level truncation). QuoteSets[0] has ONE
// QuoteEntry holding TWO Legs (multi-entry at the DEEPEST level, 555);
// QuoteSets[1] has TWO QuoteEntries, each holding ONE Leg (multi-entry at the
// MIDDLE level, 295). Every level's entry count and every entry's own fields
// are asserted, so a walk that truncates ANY one nested extent (at either
// level, in either QuoteSet) fails a concrete assertion below — this is the
// exact recursion-bug exposer the advisor flagged (spec US2 AC3 / Edge Cases
// "depth >=3/4"): the pre-063 flat walk could truncate at the first
// repeated tag it saw, regardless of which nesting level it belonged to.
TEST(NestedGroupRead, Depth3MultiEntryAtMultipleLevelsNoCrossLevelTruncation) {
    std::string body =
        "35=i\x01"
        "296=2\x01"
        // QuoteSets[0]: 1 QuoteEntry, that entry holds 2 Legs.
        "302=SET0\x01"
        "295=1\x01"
        "299=E00\x01"
        "132=10.10\x01"
        "133=10.20\x01"
        "555=2\x01"
        "602=LEG000\x01"
        "602=LEG001\x01"
        // QuoteSets[1]: 2 QuoteEntries, each holding 1 Leg.
        "302=SET1\x01"
        "295=2\x01"
        "299=E10\x01"
        "132=20.10\x01"
        "133=20.20\x01"
        "555=1\x01"
        "602=LEG100\x01"
        "299=E11\x01"
        "132=21.10\x01"
        "133=21.20\x01"
        "555=1\x01"
        "602=LEG110\x01";

    std::pmr::monotonic_buffer_resource arena{16384};
    auto dict = make_correct_massquote_dict();

    auto buf = make_frame(body);
    fixpp::wire::pmr_carry_buffer carry{buf.size(), &arena};
    fixpp::wire::Framer fr{};
    fixpp::wire::frame_view fvs[1]{};
    auto framed = fr.feed(
        std::span<const std::byte>{buf.data(), buf.size()}, carry,
        std::span<fixpp::wire::frame_view>{fvs, 1});
    ASSERT_TRUE(framed.has_value());
    ASSERT_FALSE(framed->empty());

    fixpp::wire::Parser<fixpp::wire::access_mode::Index> parser{dict};
    auto mv_exp = parser.parse((*framed)[0], &arena);
    ASSERT_TRUE(mv_exp.has_value());

    fixpp::v44::MassQuote mq{*mv_exp};
    auto sets = mq.quote_sets();
    ASSERT_EQ(sets.size(), 2U) << "level 1 (296): both QuoteSets present";

    // QuoteSets[0]: 1 QuoteEntry x 2 Legs.
    auto set0 = sets[0];
    auto set0_id = set0.quote_set_id();
    ASSERT_TRUE(set0_id.has_value());
    EXPECT_EQ(*set0_id, "SET0");

    auto set0_entries = set0.quote_entries();
    ASSERT_EQ(set0_entries.size(), 1U) << "level 2 (295) under QuoteSets[0]: exactly 1 entry";
    auto set0_entry0 = set0_entries[0];
    auto s0e0_id = set0_entry0.quote_entry_id();
    ASSERT_TRUE(s0e0_id.has_value());
    EXPECT_EQ(*s0e0_id, "E00");
    auto s0e0_bid = set0_entry0.bid_px(&arena);
    ASSERT_TRUE(s0e0_bid.has_value());
    EXPECT_EQ(*s0e0_bid, parse_decimal("10.10", &arena));
    auto s0e0_offer = set0_entry0.offer_px(&arena);
    ASSERT_TRUE(s0e0_offer.has_value());
    EXPECT_EQ(*s0e0_offer, parse_decimal("10.20", &arena));

    auto set0_legs = set0_entry0.legs();
    ASSERT_EQ(set0_legs.size(), 2U)
        << "level 3 (555) under QuoteSets[0].quote_entries()[0]: BOTH legs present — "
           "no truncation at the deepest level";
    auto s0_leg_entry0 = set0_legs[0];
    auto s0_leg0 = s0_leg_entry0.leg_security_id();
    ASSERT_TRUE(s0_leg0.has_value());
    EXPECT_EQ(*s0_leg0, "LEG000");
    auto s0_leg_entry1 = set0_legs[1];
    auto s0_leg1 = s0_leg_entry1.leg_security_id();
    ASSERT_TRUE(s0_leg1.has_value());
    EXPECT_EQ(*s0_leg1, "LEG001");

    // QuoteSets[1]: 2 QuoteEntries x 1 Leg each.
    auto set1 = sets[1];
    auto set1_id = set1.quote_set_id();
    ASSERT_TRUE(set1_id.has_value());
    EXPECT_EQ(*set1_id, "SET1");

    auto set1_entries = set1.quote_entries();
    ASSERT_EQ(set1_entries.size(), 2U)
        << "level 2 (295) under QuoteSets[1]: BOTH entries present — no truncation "
           "at the middle level, and no bleed from QuoteSets[0]'s own content";

    auto set1_entry0 = set1_entries[0];
    auto s1e0_id = set1_entry0.quote_entry_id();
    ASSERT_TRUE(s1e0_id.has_value());
    EXPECT_EQ(*s1e0_id, "E10");
    auto s1e0_bid = set1_entry0.bid_px(&arena);
    ASSERT_TRUE(s1e0_bid.has_value());
    EXPECT_EQ(*s1e0_bid, parse_decimal("20.10", &arena));
    auto s1e0_legs = set1_entry0.legs();
    ASSERT_EQ(s1e0_legs.size(), 1U);
    auto s1e0_leg_entry0 = s1e0_legs[0];
    auto s1e0_leg0 = s1e0_leg_entry0.leg_security_id();
    ASSERT_TRUE(s1e0_leg0.has_value());
    EXPECT_EQ(*s1e0_leg0, "LEG100");

    auto set1_entry1 = set1_entries[1];
    auto s1e1_id = set1_entry1.quote_entry_id();
    ASSERT_TRUE(s1e1_id.has_value());
    EXPECT_EQ(*s1e1_id, "E11");
    auto s1e1_bid = set1_entry1.bid_px(&arena);
    ASSERT_TRUE(s1e1_bid.has_value());
    EXPECT_EQ(*s1e1_bid, parse_decimal("21.10", &arena));
    auto s1e1_legs = set1_entry1.legs();
    ASSERT_EQ(s1e1_legs.size(), 1U)
        << "QuoteSets[1].quote_entries()[1] (the LAST entry at the middle level) must "
           "keep its own distinct Leg, not the sibling entry[0]'s";
    auto s1e1_leg_entry0 = s1e1_legs[0];
    auto s1e1_leg0 = s1e1_leg_entry0.leg_security_id();
    ASSERT_TRUE(s1e1_leg0.has_value());
    EXPECT_EQ(*s1e1_leg0, "LEG110");
}

// INV-G7 — dict-aware nested-slicer discriminator. One QuoteSet, one
// QuoteEntry, and a QuoteSet-level scalar field QuoteSetValidUntilTime(367)
// placed AFTER the nested NoQuoteEntries(295) group in WIRE order (367 is
// declared BEFORE QuotEntryGrp in the FIX44 QuotSetGrp component
// (dictionaries/FIX44.xml:3350-3358), so this wire layout deliberately
// diverges from dictionary declaration order — the point being that
// correctness must not depend on wire position, only on dictionary
// membership).
//
// Three assertions:
//  1. The nested group's own fields (BidPx/OfferPx) read exact — the
//     trailing outer field does not corrupt them.
//  2. The OUTER's own scalar read (`quote_set_valid_until_time()`) reads the
//     trailing field's exact value.
//  3. THE DISCRIMINATOR: `quote_entries()[0].field_value(367)` MUST be
//     absent. Under a dict-free fallback (`group_end = entries_.size()`)
//     applied to the sub-OffsetTable built over the QuoteSet's OWN slice,
//     the single QuoteEntry occurrence's span would extend past
//     OfferPx(133) through to QuoteSetValidUntilTime(367) — swallowing 367
//     into the QuoteEntry's own span, so `field_value(367)` would
//     incorrectly report PRESENT. Under the mandatory dict-aware sub-view
//     (367 is declared a member of 296 but NOT of 295 in the hand-built
//     dict above — matching the real FIX44 declaration, where 367 sits
//     directly under QuotSetGrp, not under QuotEntryGrp), the QuoteEntry's
//     span correctly excludes 367.
TEST(NestedGroupRead, NonLastNestedGroupTrailingFieldNotSwallowed) {
    std::string body =
        "35=i\x01"
        "296=1\x01"
        "302=SET0\x01"
        "295=1\x01"
        "299=E0\x01"
        "132=10.10\x01"
        "133=10.20\x01"
        "367=20260101-00:00:00\x01";

    std::pmr::monotonic_buffer_resource arena{16384};
    auto dict = make_correct_massquote_dict();

    auto buf = make_frame(body);
    fixpp::wire::pmr_carry_buffer carry{buf.size(), &arena};
    fixpp::wire::Framer fr{};
    fixpp::wire::frame_view fvs[1]{};
    auto framed = fr.feed(
        std::span<const std::byte>{buf.data(), buf.size()}, carry,
        std::span<fixpp::wire::frame_view>{fvs, 1});
    ASSERT_TRUE(framed.has_value());
    ASSERT_FALSE(framed->empty());

    fixpp::wire::Parser<fixpp::wire::access_mode::Index> parser{dict};
    auto mv_exp = parser.parse((*framed)[0], &arena);
    ASSERT_TRUE(mv_exp.has_value());

    fixpp::v44::MassQuote mq{*mv_exp};
    auto sets = mq.quote_sets();
    ASSERT_EQ(sets.size(), 1U);

    auto quote_set0 = sets[0];
    auto entries = quote_set0.quote_entries();
    ASSERT_EQ(entries.size(), 1U);
    auto entry0 = entries[0];

    // (1) nested group fields read exact.
    auto bid = entry0.bid_px(&arena);
    ASSERT_TRUE(bid.has_value()) << "entries[0].bid_px() must have value";
    EXPECT_EQ(*bid, parse_decimal("10.10", &arena));
    auto offer = entry0.offer_px(&arena);
    ASSERT_TRUE(offer.has_value()) << "entries[0].offer_px() must have value";
    EXPECT_EQ(*offer, parse_decimal("10.20", &arena));

    // (2) the outer trailing field reads exact.
    auto valid_until = quote_set0.quote_set_valid_until_time();
    ASSERT_TRUE(valid_until.has_value()) << "sets[0].quote_set_valid_until_time() must have value";
    EXPECT_EQ(*valid_until, "20260101-00:00:00");

    // (3) discriminator: the trailing outer field must NOT be reachable
    // through the nested QuoteEntry's own span.
    auto swallowed = entry0.field_value(367);
    ASSERT_FALSE(swallowed.has_value())
        << "QuoteEntry's own span must NOT swallow the outer QuoteSet's "
           "trailing field(367) — dict-aware nested-slicer regression";
    EXPECT_EQ(swallowed.error(), fixpp::core::error::wire_required_field_missing);
}

// ESCALATED — NOT a fake pass. US2 AC1 / FR-002 / SC-001 originally called
// for this case to assert per-instance DISTINCT BidPx/OfferPx across TWO
// QuoteEntries nested within a SINGLE QuoteSet occurrence. Empirically
// (verified via a standalone OffsetTable-level probe reproducing the exact
// production callpath `MessageView::group<296,G_296>()` ->
// `OffsetTable::group_slices(296)`, parser.hpp:248-273), this frame layout
// cannot be made to parse correctly through the CURRENT, unmodified
// `OffsetTable::group()` (src/wire/offset_table.cpp, present on `main`
// unchanged — confirmed via `git show main:src/wire/offset_table.cpp | grep
// seen_in_instance` and `git log main..HEAD -- src/wire/offset_table.cpp`,
// which shows 062 only ADDED build_nested_subview()/nested_group_slices(),
// never touched group()).
//
// Root cause: group()'s per-top-level-occurrence boundary walk
// (offset_table.cpp:~412-438) uses a "has this tag already appeared since
// the last delimiter" heuristic (`seen_in_instance`) with no notion of
// nesting depth. For the outer QuoteSet's byte-span to extend far enough to
// cover a nested QuoteEntry's fields at all, 299/132/133 MUST be registered
// as transitive members of group 296 (confirmed empirically: omitting them
// truncates the outer slice immediately after "295=2", inner group entirely
// empty). But once registered, a SECOND occurrence of 299 (E1's QuoteEntryID
// — legitimate, since QuoteSet[0] here has 2 QuoteEntries) is
// indistinguishable, at the flat top-level scan, from "this tag repeated
// without the outer delimiter (302) reappearing" — group() takes it as a
// signal to stop the current instance, one nested entry early.
//
// Frame (this test's `body`):
//   35=i, 296=1, 302=SET0, 295=2,
//   299=E0, 132=10.10, 133=10.20,
//   299=E1, 132=20.10, 133=20.20
//
// Expected: quote_sets()[0].quote_entries().size() == 2, with entries[0]
// reading BidPx=10.10/OfferPx=10.20 and entries[1] reading
// BidPx=20.10/OfferPx=20.20.
//
// Actual (probed against the exact production callpath, hand-built dict
// with the transitive membership required for the boundary walk to extend
// past the nested group's own count field at all): group_slices(296)
// returns ONE outer slice truncated to
// "302=SET0\x01295=2\x01299=E0\x01132=10.10\x01133=10.20" (entry_count()==5)
// — ending right after E0's OfferPx, BEFORE E1's QuoteEntryID(299). The
// nested sub-view build for 295 over this truncated slice therefore finds
// only ONE QuoteEntry (E0); `quote_entries().size() == 1`, not 2.
//
// This is a genuine PRE-EXISTING wire-layer limitation that 062 US2 is the
// first feature to expose (the first caller to descend a nested group with
// >1 entry through this exact codepath); fixing `OffsetTable::group()` is a
// production wire-layer change outside T015/T018's test-only scope (062's
// brief: "do NOT modify production/codegen/dictionary source"). Escalated
// to the orchestrator rather than worked around; NOT marked passing.
TEST(NestedGroupRead, NestedQuoteEntriesPerInstancePrices) {
    GTEST_SKIP() << "ESCALATED: pre-existing OffsetTable::group() "
                    "seen_in_instance heuristic (src/wire/offset_table.cpp) "
                    "truncates a QuoteSet occurrence's byte span at the "
                    "SECOND nested QuoteEntry (repeated QuoteEntryID(299) "
                    "tag looks like a new top-level instance at the flat "
                    "scan level). Confirmed present unmodified on `main`; "
                    "out of scope for 062 (test-only, no production source "
                    "changes permitted). See the file-header comment and "
                    "this test's own comment block for frame bytes / "
                    "expected-vs-actual / root-cause location.";

    std::string body =
        "35=i\x01"
        "296=1\x01"
        "302=SET0\x01"
        "295=2\x01"
        "299=E0\x01"
        "132=10.10\x01"
        "133=10.20\x01"
        "299=E1\x01"
        "132=20.10\x01"
        "133=20.20\x01";

    std::pmr::monotonic_buffer_resource arena{16384};
    auto dict = make_correct_massquote_dict();

    auto buf = make_frame(body);
    fixpp::wire::pmr_carry_buffer carry{buf.size(), &arena};
    fixpp::wire::Framer fr{};
    fixpp::wire::frame_view fvs[1]{};
    auto framed = fr.feed(
        std::span<const std::byte>{buf.data(), buf.size()}, carry,
        std::span<fixpp::wire::frame_view>{fvs, 1});
    ASSERT_TRUE(framed.has_value());
    ASSERT_FALSE(framed->empty());

    fixpp::wire::Parser<fixpp::wire::access_mode::Index> parser{dict};
    auto mv_exp = parser.parse((*framed)[0], &arena);
    ASSERT_TRUE(mv_exp.has_value());

    fixpp::v44::MassQuote mq{*mv_exp};
    auto sets = mq.quote_sets();
    ASSERT_EQ(sets.size(), 1U);

    auto quote_set0 = sets[0];
    auto entries = quote_set0.quote_entries();
    ASSERT_EQ(entries.size(), 2U);

    auto e0 = entries[0];
    auto bid0 = e0.bid_px(&arena);
    ASSERT_TRUE(bid0.has_value()) << "entries[0].bid_px() must have value";
    EXPECT_EQ(*bid0, parse_decimal("10.10", &arena));
    auto offer0 = e0.offer_px(&arena);
    ASSERT_TRUE(offer0.has_value()) << "entries[0].offer_px() must have value";
    EXPECT_EQ(*offer0, parse_decimal("10.20", &arena));

    auto e1 = entries[1];
    auto bid1 = e1.bid_px(&arena);
    ASSERT_TRUE(bid1.has_value()) << "entries[1].bid_px() must have value";
    EXPECT_EQ(*bid1, parse_decimal("20.10", &arena));
    auto offer1 = e1.offer_px(&arena);
    ASSERT_TRUE(offer1.has_value()) << "entries[1].offer_px() must have value";
    EXPECT_EQ(*offer1, parse_decimal("20.20", &arena));
}
