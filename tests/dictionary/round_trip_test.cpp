// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/round_trip_test.cpp — seam #8 — AC-D1 / AC-D2 / AC-D5

#include <gtest/gtest.h>

#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/field_ref.hpp>
#include <fixpp/dict/xml_loader.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory_resource>
#include <ranges>
#include <string>
#include <string_view>

namespace {

constexpr std::size_t k4MiB = 4UZ * 1024UZ * 1024UZ;

// Representative tag corpus: a handful of well-known FIX 4.4 header/body tags
// plus a deliberately absent tag so the NotDeclared branch is exercised.
constexpr std::array<std::uint16_t, 11> kProbeTags{
    1,    // Account
    8,    // BeginString
    9,    // BodyLength
    11,   // ClOrdID
    35,   // MsgType
    49,   // SenderCompID
    56,   // TargetCompID
    78,   // NoAllocs
    453,  // NoPartyIDs
    555,  // NoLegs
    9999, // known-absent sentinel
};

// Bytewise comparator over msg_type strings (unsigned-char domain per D-6).
auto bytewise_less = [](std::string_view a, std::string_view b) noexcept {
    return std::ranges::lexicographical_compare(
        a, b,
        [](char lhs, char rhs) noexcept {
            return static_cast<unsigned char>(lhs) <
                   static_cast<unsigned char>(rhs);
        });
};

// Load FIX44.xml once and pin in monotonic storage.  Each test fixture
// re-uses a fresh buffer so tests remain independent.
fixpp::dict::Dictionary load_fix44()
{
    static std::array<std::byte, k4MiB> s_buf{};
    static std::pmr::monotonic_buffer_resource s_mr{
        s_buf.data(), s_buf.size()};

    auto const path =
        std::filesystem::path{FIXPP_DICT_DATA_DIR} / "FIX44.xml";
    return fixpp::dict::XmlLoader{}.load(path, &s_mr);
}

}  // namespace

// ---------------------------------------------------------------------------
// AC-D5 — messages() is non-empty and bytewise-sorted
// ---------------------------------------------------------------------------

TEST(RoundTrip, MessagesIsSortedBytewise)
{
    auto d    = load_fix44();
    auto msgs = d.messages();

    ASSERT_FALSE(msgs.empty())
        << "messages() must return at least one entry for FIX44.xml";

    bool const sorted = std::ranges::is_sorted(
        msgs,
        [](fixpp::dict::MessageEntry const& a,
           fixpp::dict::MessageEntry const& b) noexcept {
            return std::ranges::lexicographical_compare(
                a.msg_type, b.msg_type,
                [](char lhs, char rhs) noexcept {
                    return static_cast<unsigned char>(lhs) <
                           static_cast<unsigned char>(rhs);
                });
        });

    EXPECT_TRUE(sorted)
        << "messages() must be sorted bytewise by msg_type (research.md D-6)";

    // Verify no adjacent pair violates strict weak ordering.
    for (std::size_t i = 0; i + 1 < msgs.size(); ++i) {
        bool const ok =
            !bytewise_less(msgs[i + 1].msg_type, msgs[i].msg_type);
        EXPECT_TRUE(ok)
            << "Inversion at index " << i << ": \""
            << msgs[i].msg_type << "\" vs \"" << msgs[i + 1].msg_type << "\"";
        if (!ok) break;
    }
}

// ---------------------------------------------------------------------------
// AC-D1 / AC-D2 — field_ref and field() are consistent for every message
// ---------------------------------------------------------------------------

TEST(RoundTrip, FieldRefMatchesFieldOptional)
{
    auto d    = load_fix44();
    auto msgs = d.messages();

    ASSERT_FALSE(msgs.empty());

    for (auto const& m : msgs) {
        for (std::uint16_t tag : kProbeTags) {
            auto fr  = d.field_ref(m.msg_type, tag);
            auto opt = d.field(m.msg_type, tag);

            // AC-D2: nullopt iff rule == NotDeclared
            bool const declared =
                (fr.rule != fixpp::dict::field_presence::NotDeclared);

            EXPECT_EQ(opt.has_value(), declared)
                << "msg_type=\"" << m.msg_type << "\" tag=" << tag
                << ": field_ref.rule=" << static_cast<int>(fr.rule)
                << " but field().has_value()=" << opt.has_value();

            if (opt.has_value()) {
                // AC-D1/D2 round-trip: the FieldRef inside the optional must
                // equal the FieldRef returned by field_ref().
                EXPECT_EQ(opt->tag,  fr.tag)
                    << "tag mismatch for msg_type=\"" << m.msg_type << "\"";
                EXPECT_EQ(opt->rule, fr.rule)
                    << "rule mismatch for msg_type=\"" << m.msg_type << "\"";
                EXPECT_EQ(opt->type, fr.type)
                    << "type mismatch for msg_type=\"" << m.msg_type << "\"";
                EXPECT_EQ(opt->group_no_tag, fr.group_no_tag)
                    << "group_no_tag mismatch for msg_type=\""
                    << m.msg_type << "\"";
                EXPECT_EQ(opt->component_index, fr.component_index)
                    << "component_index mismatch for msg_type=\""
                    << m.msg_type << "\"";
            }
        }
    }
}

// ---------------------------------------------------------------------------
// AC-D5 driver — exhaustive walk finds the FIX44 headline message types
// ---------------------------------------------------------------------------

TEST(RoundTrip, ExhaustiveWalkVisitsEveryMessage)
{
    auto d    = load_fix44();
    auto msgs = d.messages();

    ASSERT_FALSE(msgs.empty());

    // Collect msg_types into a set for O(log n) membership checks.
    std::vector<std::string> seen;
    seen.reserve(msgs.size());
    for (auto const& m : msgs) {
        seen.emplace_back(m.msg_type);
    }

    // FIX44 headline messages: NewOrderSingle(D), ExecutionReport(8),
    // Logon(A), Heartbeat(0), Reject(3).
    constexpr std::array<std::string_view, 5> kHeadlines{
        "D", "8", "A", "0", "3",
    };

    for (auto const headline : kHeadlines) {
        bool const found =
            std::ranges::find(seen, headline) != seen.end();
        EXPECT_TRUE(found)
            << "Headline msg_type \"" << headline
            << "\" not found in messages()";
    }

    // Every entry should have a non-empty msg_type and a non-empty name.
    for (auto const& m : msgs) {
        EXPECT_FALSE(m.msg_type.empty())
            << "MessageEntry has empty msg_type";
        EXPECT_FALSE(m.name.empty())
            << "MessageEntry for msg_type=\"" << m.msg_type
            << "\" has empty name";
    }
}

// ---------------------------------------------------------------------------
// T028 — AC-D5 exhaustive-coverage guard (US3 codegen consumer)
//
// Counts the distinct `(msg_type, tag)` pairs the Dictionary admits across
// every message, scanning the full uint16 tag space. The assertions are
// **lower bounds** chosen well above realistic regression noise so that any
// silent under-iteration in `messages()` or per-MsgType FieldRef arrays
// fails the test even before a structural diff lands.
// ---------------------------------------------------------------------------

TEST(RoundTrip, ExhaustiveCoverageHasReasonableMinima)
{
    auto d    = load_fix44();
    auto msgs = d.messages();

    ASSERT_FALSE(msgs.empty());

    // Per-msg distinct-tag counts.
    std::size_t total_pairs = 0;
    std::size_t nos_tag_count = 0;
    std::size_t er_tag_count  = 0;
    for (auto const& m : msgs) {
        std::size_t per_msg = 0;
        for (std::uint32_t t = 0; t < 65536; ++t) {
            auto const fr = d.field_ref(m.msg_type, static_cast<std::uint16_t>(t));
            if (fr.rule != fixpp::dict::field_presence::NotDeclared) {
                ++per_msg;
                ++total_pairs;
            }
        }
        if (m.msg_type == "D") {
            nos_tag_count = per_msg;
        } else if (m.msg_type == "8") {
            er_tag_count = per_msg;
        }
    }

    // FIX44 declares hundreds of distinct tags across its messages; well
    // above 200 total pairs even with the dedup of header+body+trailer.
    EXPECT_GT(total_pairs, 200u)
        << "Distinct (msg_type, tag) coverage too low — suspect under-iteration";

    // NewOrderSingle (D) and ExecutionReport (8) are field-heavy headline
    // messages. Real counts are well above 30 / 50; the bars below are
    // regression-noise floors.
    EXPECT_GT(nos_tag_count, 30u)
        << "NewOrderSingle has too few declared tags — under-iteration?";
    EXPECT_GT(er_tag_count, 50u)
        << "ExecutionReport has too few declared tags — under-iteration?";
}
