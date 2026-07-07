// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/codegen/group_entry_generation_trap_test.cpp — 062 T019b [US3]
//
// INV-G6: "every entry read (scalar field_view mint + nested sub-frame
// build) uses the parent generation_token threaded through ctx_.gen; no read
// is performed under a default {} token" ([2b §6.4] trap stays armed).
//
// This is NOT catchable by the T021 sanitizer matrix: a default-constructed
// token carries pool_id==0, the designated "untracked, never traps" sentinel
// (tests/wire/view.hpp — WireLifetimeTrap.UntrackedPoolNeverTraps), so a bug
// that silently drops the real token and reads under `{}` produces no
// sanitizer signal at all — it just silently stops tracking lifetime safety.
// This witness proves the OPPOSITE property end-to-end over a GENERATED
// group-entry flyweight: a real (non-zero pool_id) token IS threaded all the
// way into both a one-level entry's scalar read and a nested descent's
// entry read, by showing both TRAP (abort) once that token is invalidated —
// which could only happen if the real, live token was captured in the first
// place.
//
// Mechanism/precedent: tests/wire/lifetime_trap_test.cpp
// (WireLifetimeTrapDeath.ParserViewTrapsAfterArenaRecycle) — a real Framer
// (assigns a non-zero pool_id_) is kept alive across the parse, so
// framer.recycle_pool() bumps that SAME pool's live generation; any View
// still holding the pre-recycle token traps on its next check_alive()
// (called transitively via View::bytes(), e.g. field_view::as_string()).
//
// Frame/dict: reuses the single-entry-per-occurrence nested-group layout
// from tests/codegen/nested_group_read_test.cpp's
// NonLastNestedGroupTrailingFieldNotSwallowed (MassQuote NoQuoteSets(296) ->
// NoQuoteEntries(295), one occurrence each) so ONE frame exercises both a
// one-level outer-entry scalar read (QuoteSetValidUntilTime, 367) and a
// nested-descent entry scalar read (BidPx, 132) — matching the task's "mint
// an entry (and a single-entry nested sub-view)" wording.
//
// Multi-entry nested reads are out of scope for 062 (Defect B, deferred to
// 063) — this witness stays within the single-entry-per-occurrence bound
// like every other 062 nested witness.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fixpp/core/decimal_alias.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/v44/Messages.hpp>
#include <fixpp/wire/parser.hpp>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

// Build a well-formed FIX frame: "8=FIX.4.4<SOH> 9=<len><SOH> <body> 10=<chk><SOH>"
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

// Same hand-built MassQuote group membership as
// tests/codegen/nested_group_read_test.cpp's make_correct_massquote_dict(),
// trimmed to what this witness needs (no Legs(555) descent required).
fixpp::dict::table_view make_massquote_dict() {
    fixpp::dict::table_view dict;
    dict.add_group_member(296, 302)   // QuoteSetID
        .add_group_member(296, 295)   // NoQuoteEntries (nested group's own count field)
        .add_group_member(296, 299)   // QuoteEntryID — transitively under 296
        .add_group_member(296, 132)   // BidPx — transitively under 296
        .add_group_member(296, 133)   // OfferPx — transitively under 296
        .add_group_member(296, 367)   // QuoteSetValidUntilTime — direct member of 296
        .add_group_member(295, 299)   // QuoteEntryID — NoQuoteEntries' own delimiter
        .add_group_member(295, 132)   // BidPx — direct member of 295
        .add_group_member(295, 133);  // OfferPx — direct member of 295
    return dict;
}

}  // namespace

#ifndef NDEBUG

TEST(GroupEntryGenerationTrapDeath, GenerationTokenTrapOnStaleEntryRead) {
    std::string body =
        "35=i\x01"
        "296=1\x01"
        "302=SET0\x01"
        "295=1\x01"
        "299=E0\x01"
        "132=10.10\x01"
        "133=10.20\x01"
        "367=20260101-00:00:00\x01";

    auto dict = make_massquote_dict();
    auto buf = make_frame(body);

    // Keep the Framer alive across the parse (it owns the live pool_id/gen
    // slot); recycle_pool() below bumps THIS pool's generation.
    fixpp::wire::Framer framer;
    // Stack-backed, NULL-upstream arena (mirrors group_entry_alloc_gate_test):
    // all parse / carry / build_nested_subview storage is drawn from this
    // std::array so NO global operator new fires. This keeps the death-test
    // children — forked at each EXPECT_DEATH with the pre-trap nested sub-view
    // already built — free of any heap block LeakSanitizer would report when
    // the child dies mid-statement (it never reaches this arena's destructor).
    // Overrun → null upstream → hard bad_alloc (visible), not a silent heap hit.
    std::array<std::byte, 16384> arena_storage{};
    std::pmr::monotonic_buffer_resource arena{arena_storage.data(), arena_storage.size(),
                                              std::pmr::null_memory_resource()};
    fixpp::wire::pmr_carry_buffer carry{buf.size(), &arena};
    std::array<fixpp::wire::frame_view, 1> fvs{};
    auto framed = framer.feed(
        std::span<const std::byte>{buf.data(), buf.size()}, carry,
        std::span<fixpp::wire::frame_view>{fvs});
    ASSERT_TRUE(framed.has_value());
    ASSERT_GE(framed->size(), 1U);

    fixpp::wire::Parser<fixpp::wire::access_mode::Index> parser{dict};
    auto mv_exp = parser.parse((*framed)[0], &arena);
    ASSERT_TRUE(mv_exp.has_value());

    // Construct directly from *mv_exp — do NOT copy the MessageView into a
    // local. A copy re-roots the OffsetTable's resource() to the default heap
    // resource, so build_nested_subview would draw the nested sub-view from
    // global operator new; a death-test child forked at the EXPECT_DEATHs
    // below would then inherit that heap block and die before this arena's
    // destructor runs, which LeakSanitizer reports. Building over *mv_exp keeps
    // resource() == &arena (the stack-backed, NULL-upstream arena above), so
    // no global heap block exists for a forked child to leak. (Matches
    // nested_group_read_test / group_entry_alloc_gate_test, which never copy.)
    fixpp::v44::MassQuote mq{*mv_exp};
    auto sets = mq.quote_sets();
    ASSERT_EQ(sets.size(), 1U);
    auto quote_set0 = sets[0];  // one-level entry, minted while the token is live

    // Descend into the nested group WHILE the token is still live, minting
    // the nested entry (and warming/caching the nested sub-view build)
    // before the arena's generation is bumped.
    auto entries = quote_set0.quote_entries();
    ASSERT_EQ(entries.size(), 1U);
    auto nested_entry = entries[0];  // nested entry, minted while the token is live

    // Contrast case: BEFORE recycling, both reads succeed — proves this
    // exercises the genuinely-live-token path, not an already-broken one.
    auto valid_until_alive = quote_set0.quote_set_valid_until_time();
    ASSERT_TRUE(valid_until_alive.has_value()) << "must read fine before the pool recycle";
    EXPECT_EQ(*valid_until_alive, "20260101-00:00:00");
    auto bid_alive = nested_entry.bid_px(&arena);
    ASSERT_TRUE(bid_alive.has_value()) << "must read fine before the pool recycle";

    // Simulate a per-message arena recycle: bump the pool's live generation.
    // Every entry_context minted above still carries the PREVIOUS token.
    framer.recycle_pool();

    // Discriminating witness for issue #169 guard A: builds the group_view via
    // top-level OffsetTable::group_slices() but does NOT call size()/operator[]
    // on the returned held view, so it dies ONLY if the OffsetTable-layer
    // liveness guard fires before the cache-hit/materialize path returns.
    EXPECT_DEATH((void)mq.quote_sets(), "")
        << "top-level group_slices() metadata read must trap after pool recycle";

    // After recycling: the one-level entry's own scalar read must trap. A
    // bug that silently reads under a default {} token (pool_id==0, which
    // NEVER traps) would make this EXPECT_DEATH fail to observe a death.
    EXPECT_DEATH(quote_set0.quote_set_valid_until_time(), "")
        << "one-level entry scalar read must trap after pool recycle (INV-G6)";

    // The nested entry's own scalar read must ALSO trap — proving the
    // nested descent's entry_context threads the SAME live token, not a
    // default one.
    EXPECT_DEATH(nested_entry.bid_px(&arena), "")
        << "nested entry scalar read must trap after pool recycle (INV-G6)";

    // A WARM re-descent (exact nested_cache_ hit on (slice_data, no_tag),
    // OffsetTable::nested_group_slices ~:579-581) must ALSO trap after pool
    // recycle. quote_set0.quote_entries() was already called above (line
    // ~151), so this second call is a cache hit that historically returned
    // the cached sub-table's group_slices() with NO liveness check against
    // `gen` — silently serving a stale-but-plausible count/slice instead of
    // fault-closing. Only the COLD build path (build_nested_subview) checked
    // the token; the warm-hit early-return skipped it entirely.
    EXPECT_DEATH((void)quote_set0.quote_entries().size(), "")
        << "warm nested-cache-hit descent must trap after pool recycle (INV-G6)";
}

#else

TEST(GroupEntryGenerationTrap, ReleaseStripsTokenNoTrapMachinery) {
    // In release, View::check_alive() is a constexpr no-op (the token is
    // stripped entirely) — nothing to abort on. Mirrors
    // tests/wire/lifetime_trap_test.cpp's release-side counterpart.
    SUCCEED();
}

#endif
