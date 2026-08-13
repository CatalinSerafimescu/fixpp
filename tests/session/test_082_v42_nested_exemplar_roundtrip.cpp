// tests/session/test_082_v42_nested_exemplar_roundtrip.cpp
// 082-structural-group-detection T045 [US4] — US4 AC2 / SC-007. Closes L-061-1.
//
// The exemplar that proves the `fixpp::v42` builder tier is **USABLE**, not merely
// emitted. Before 082 the 061 exemplar suite had no FIX 4.2 grouped or nested WRITE
// exemplar at all — all five were forced to `fixpp::v44`, because v42's NumInGroup
// tags were datatype-gated out of the builder tier and so a v42 grouped write could
// not be expressed (L-061-1 / L-063-1 / issue #196). This is that missing exemplar.
//
// Two legs, mirroring the 061 pattern (`test_exemplar_roundtrip.cpp`):
//
//   LEG 1 — WRITE: `fixpp::v42::build_MassQuote` output byte-matches
//           `tests/session/golden/v42_mass_quote.fix`, a golden authored by a REAL
//           QuickFIX-cpp v1.16.0 (T044). The golden is an INDEPENDENT oracle: it was
//           not produced by fixpp, so agreement is evidence rather than tautology.
//
//   LEG 2 — READ round-trip: the same bytes parsed back through the REGENERATED v42
//           read tier, with **both** group levels enumerated and every seeded field
//           compared by value — `NoQuoteSets(296)` → `NoQuoteEntries(295)`.
//
// ⚠️ Both legs are required and neither substitutes for the other. Leg 1 alone would
// pass if the reader were broken; leg 2 alone would pass if the writer and reader
// shared a compensating bug (they share the same dictionary and the same group
// tables, so a symmetric error is exactly the plausible failure). The golden is what
// breaks that symmetry — cf.
// feedback_verification_corpus_built_from_the_read_it_checks_is_blind.
//
// ⚠️ THE `311` DETAIL IS LOAD-BEARING, not incidental. FIX 4.2 marks
// UnderlyingSymbol(311) **required** inside NoQuoteSets; FIX 4.4 does not. So this
// golden differs from the FIX 4.4 `mass_quote.fix` by exactly `311=AAPL`, and a v42
// MassQuote without it is invalid. If a future change "aligns" the two goldens by
// dropping 311, this test must fail — the seed sets it deliberately.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fixpp/v42/Messages.hpp>  // GENERATED read tier
#include <fixpp/v42/all.hpp>       // GENERATED builder tier (082 US2)

#include "support/app_message_read_scaffold.hpp"
#include "support/golden_diff.hpp"

namespace {

using fixpp::interop::diff_transcripts;
using fixpp::interop::GoldenFrame;
using fixpp::interop::parse_golden;
using fixpp::interop::shape_oracle_profile;
using fixpp_test_support::bytes_to_string;
using fixpp_test_support::make_decimal;
using fixpp_test_support::make_frame;
using fixpp_test_support::parse_dict;

namespace g = fixpp::v42::groups;

// The seed, matching T044's QuickFIX authoring exactly.
constexpr std::string_view kGoldenPath = "tests/session/golden/v42_mass_quote.fix";
constexpr std::string_view kBeginString = "FIX.4.2";
constexpr std::string_view kQuoteId = "QID-100";
constexpr std::string_view kQuoteSetId = "QS1";
constexpr std::string_view kUnderlyingSymbol = "AAPL";  // 311 — required in FIX 4.2
constexpr std::string_view kQuoteEntryId = "QE1";
constexpr std::string_view kBidPx = "10.5";
constexpr std::string_view kOfferPx = "10.75";

std::string read_file(std::string const& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) {
        ADD_FAILURE() << "cannot open golden: " << path;
        return {};
    }
    std::string out;
    char buf[4096];
    std::size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
        out.append(buf, n);
    }
    std::fclose(f);
    return out;
}

}  // namespace

TEST(V42NestedExemplar082, BuildMatchesQuickFixGoldenAndRoundTripsBothGroupLevels) {
    std::pmr::monotonic_buffer_resource arena{1U << 16};

    // ── Build the nested Args: 296 carrying one entry, which carries one 295 ──
    std::array<g::G_295_3Args, 1> entries{};
    entries[0].quote_entry_id = kQuoteEntryId;
    entries[0].bid_px = make_decimal(kBidPx, &arena);
    entries[0].offer_px = make_decimal(kOfferPx, &arena);

    std::array<g::G_296_2Args, 1> sets{};
    sets[0].quote_set_id = kQuoteSetId;
    sets[0].underlying_symbol = kUnderlyingSymbol;  // 311
    sets[0].tot_quote_entries = 1;
    sets[0].quote_entries = std::span<const g::G_295_3Args>{entries};

    fixpp::v42::MassQuoteArgs args{};
    args.quote_id = kQuoteId;
    args.quote_sets = std::span<const g::G_296_2Args>{sets};

    // Validate first: a build that emitted an invalid frame would make the golden
    // diff below the only guard, and its failure would be harder to attribute.
    auto const valid = fixpp::v42::validate_MassQuote(args);
    ASSERT_TRUE(valid.has_value())
        << "the seeded v42 MassQuote Args must validate clean (error="
        << (valid.has_value() ? 0 : static_cast<int>(valid.error())) << ')';

    std::array<std::byte, 4096> out{};
    auto const built = fixpp::v42::build_MassQuote(std::span<std::byte>{out}, args);
    ASSERT_TRUE(built.has_value()) << "fixpp::v42::build_MassQuote failed -- the v42 builder tier is "
                                     "the thing US2 delivered; a failure here is not a test bug";
    std::span<const std::byte> const body = *built;

    // ── LEG 1: byte-diff against the QuickFIX-authored golden ────────────────
    std::string const golden_text = read_file(std::string(FIXPP_REPO_ROOT) + "/" +
                                              std::string(kGoldenPath));
    ASSERT_FALSE(golden_text.empty()) << "golden is empty -- an empty golden would make the diff "
                                         "below vacuous rather than failing";
    std::vector<GoldenFrame> const expected = parse_golden(golden_text);
    ASSERT_EQ(expected.size(), 1U) << "golden must hold exactly one frame";

    std::vector<GoldenFrame> const actual{
        GoldenFrame{'>', std::vector<std::byte>{body.begin(), body.end()}}};
    auto const diff = diff_transcripts(expected, actual, shape_oracle_profile());
    EXPECT_TRUE(static_cast<bool>(diff))
        << "v42 MassQuote build diverged from the QuickFIX-authored golden: " << diff.detail;

    // ── LEG 2: read round-trip through the regenerated v42 read tier ─────────
    std::pmr::monotonic_buffer_resource read_arena{1U << 16};
    fixpp::dict::XmlLoader loader;
    fixpp::dict::Dictionary const dict =
        loader.load(std::string(FIXPP_DICT_DATA_DIR) + "/FIX42.xml", &read_arena);
    fixpp::dict::table_view const tv = dict.as_table_view();

    // Non-vacuity: the read tier can only enumerate 296/295 if they are REGISTERED,
    // which is exactly what 082 changed. Assert that before trusting the walk below —
    // an unregistered group yields an EMPTY group_view, and every "field matches"
    // check would then simply not run.
    ASSERT_NE(tv.group_first_field(296), 0) << "296 must be a registered group (US1)";
    ASSERT_NE(tv.group_first_field(295), 0) << "295 must be a registered group (US1)";

    std::vector<std::byte> const frame = make_frame(kBeginString, bytes_to_string(body));
    auto const mv = parse_dict(frame, tv, &read_arena);

    fixpp::v42::MassQuote const mq{mv};
    auto const qid = mq.quote_id();
    ASSERT_TRUE(qid.has_value());
    EXPECT_EQ(*qid, kQuoteId);

    // Level 1 — NoQuoteSets(296)
    std::size_t sets_seen = 0;
    std::size_t entries_seen = 0;
    for (auto const& qs : mq.quote_sets()) {
        ++sets_seen;
        auto const sid = qs.quote_set_id();
        ASSERT_TRUE(sid.has_value());
        EXPECT_EQ(*sid, kQuoteSetId);

        auto const usym = qs.underlying_symbol();
        ASSERT_TRUE(usym.has_value()) << "311 must round-trip -- it is REQUIRED in FIX 4.2 and is "
                                         "the single field distinguishing this golden from the "
                                         "FIX 4.4 sibling";
        EXPECT_EQ(*usym, kUnderlyingSymbol);

        auto const tot = qs.tot_quote_entries();
        ASSERT_TRUE(tot.has_value());
        EXPECT_EQ(*tot, 1);

        // Level 2 — NoQuoteEntries(295), nested inside this 296 entry
        for (auto const& qe : qs.quote_entries()) {
            ++entries_seen;
            auto const eid = qe.quote_entry_id();
            ASSERT_TRUE(eid.has_value());
            EXPECT_EQ(*eid, kQuoteEntryId);

            // decimal_t has no to_string(); compare value-to-value through
            // make_decimal, the established idiom (app_message_read_scaffold.hpp's
            // expect_decimal does exactly this).
            auto const bid = qe.bid_px(&read_arena);
            ASSERT_TRUE(bid.has_value());
            EXPECT_EQ(*bid, make_decimal(kBidPx, &read_arena));

            auto const offer = qe.offer_px(&read_arena);
            ASSERT_TRUE(offer.has_value());
            EXPECT_EQ(*offer, make_decimal(kOfferPx, &read_arena));
        }
    }

    // The counts are the anti-vacuity guard for the two loops above: a group that
    // failed to enumerate yields zero iterations and every EXPECT inside simply does
    // not run, which reads identically to "all fields matched".
    EXPECT_EQ(sets_seen, 1U) << "NoQuoteSets(296) must enumerate exactly one entry -- zero means "
                                "the outer group did not round-trip and the assertions above never "
                                "executed";
    EXPECT_EQ(entries_seen, 1U) << "NoQuoteEntries(295) must enumerate exactly one entry INSIDE the "
                                   "296 entry -- zero means the NESTED level did not round-trip, "
                                   "which is precisely the L-061-1 capability under test";
}
