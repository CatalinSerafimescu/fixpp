// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/round_trip_test.cpp — seam #8 — AC-D1 / AC-D2 / AC-D5

#include <gtest/gtest.h>

#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/field_ref.hpp>
#include <fixpp/dict/xml_loader.hpp>

// T028: direct pugixml parse for exact exhaustiveness check.
#include <pugixml.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory_resource>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

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
// T028 — AC-D5 exhaustive-coverage seam (tasks.md:126)
//
// Direct-parses FIX44.xml via pugixml to build the *expected* set of
// (msg_type, tag) pairs by recursively expanding <component>/<group> refs,
// then walks the loaded Dictionary over messages()×[0..65535] to build the
// *actual* set, and asserts exact equality per [const §VII.3] / [const §VII.4].
//
// Secondary diagnostics (heuristic floors) are kept so a total-count=0
// regression fails loudly even if the set-equality check somehow fails first.
// ---------------------------------------------------------------------------

namespace {

// Collect all (numeric) tags reachable from a container node, recursively
// expanding <component> and <group> refs using the global component map and
// field-name→tag map. Mirrors LoaderState::expand_field_list semantics.
// NOLINTNEXTLINE(misc-no-recursion) — recursive XML walk by design
void collect_tags(pugi::xml_node const& container,
                  std::set<std::uint16_t>& out,
                  std::unordered_map<std::string, pugi::xml_node> const& comp_map,
                  std::unordered_map<std::string, std::uint16_t> const& field_tag_map)
{
    for (auto const& child : container.children()) {
        std::string_view const cn{child.name()};
        if (cn == "field") {
            std::string const fname{child.attribute("name").as_string("")};
            auto const it = field_tag_map.find(fname);
            if (it != field_tag_map.end()) {
                out.insert(it->second);
            }
        } else if (cn == "component") {
            std::string const cname{child.attribute("name").as_string("")};
            auto const it = comp_map.find(cname);
            if (it != comp_map.end()) {
                collect_tags(it->second, out, comp_map, field_tag_map);
            }
        } else if (cn == "group") {
            // Emit the NoXxx field tag.
            std::string const gname{child.attribute("name").as_string("")};
            auto const git = field_tag_map.find(gname);
            if (git != field_tag_map.end()) {
                out.insert(git->second);
            }
            // Recurse into group body.
            collect_tags(child, out, comp_map, field_tag_map);
        }
    }
}

}  // namespace

TEST(RoundTrip, ExhaustiveCoverageExactEquality)
{
    auto const xml_path =
        std::filesystem::path{FIXPP_DICT_DATA_DIR} / "FIX44.xml";

    // ---- Step 1: direct pugixml parse to build ground-truth set ----
    pugi::xml_document doc;
    auto const result = doc.load_file(xml_path.c_str());
    ASSERT_TRUE(result) << "pugixml failed to load FIX44.xml: " << result.description();

    auto const fix_root = doc.child("fix");
    ASSERT_TRUE(fix_root) << "FIX44.xml missing root <fix> element";

    // Build field-name → tag map from the global <fields> block.
    std::unordered_map<std::string, std::uint16_t> field_tag_map;
    for (auto const& f : fix_root.child("fields").children("field")) {
        std::string const name{f.attribute("name").as_string("")};
        int const tag_i = f.attribute("number").as_int(0);
        if (!name.empty() && tag_i > 0 && tag_i <= 65535) {
            field_tag_map.emplace(name, static_cast<std::uint16_t>(tag_i));
        }
    }

    // Build component-name → node map from the global <components> block.
    std::unordered_map<std::string, pugi::xml_node> comp_map;
    for (auto const& c : fix_root.child("components").children("component")) {
        std::string const name{c.attribute("name").as_string("")};
        if (!name.empty()) {
            comp_map.emplace(name, c);
        }
    }

    // Also expose header/trailer as pseudo-components so per-message expansion
    // can incorporate them (header/trailer fields are inherited by every message
    // in the XmlLoader implementation).
    auto const header_node  = fix_root.child("header");
    auto const trailer_node = fix_root.child("trailer");

    // Build expected set: for each message, collect tags from header + body + trailer.
    using MsgTagPair = std::pair<std::string, std::uint16_t>;
    std::set<MsgTagPair> expected_pairs;
    for (auto const& m : fix_root.child("messages").children("message")) {
        std::string const msg_type{m.attribute("msgtype").as_string("")};
        if (msg_type.empty()) { continue; }

        std::set<std::uint16_t> msg_tags;
        if (header_node) {
            collect_tags(header_node, msg_tags, comp_map, field_tag_map);
        }
        collect_tags(m, msg_tags, comp_map, field_tag_map);
        if (trailer_node) {
            collect_tags(trailer_node, msg_tags, comp_map, field_tag_map);
        }

        for (auto const tag : msg_tags) {
            expected_pairs.emplace(msg_type, tag);
        }
    }

    // ---- Step 2: walk the loaded Dictionary to build actual set ----
    auto d    = load_fix44();
    auto msgs = d.messages();
    ASSERT_FALSE(msgs.empty());

    std::set<MsgTagPair> actual_pairs;
    std::size_t total_pairs   = 0;
    std::size_t nos_tag_count = 0;
    std::size_t er_tag_count  = 0;
    for (auto const& entry : msgs) {
        std::size_t per_msg = 0;
        for (std::uint32_t t = 0; t < 65536u; ++t) {
            auto const fr = d.field_ref(entry.msg_type, static_cast<std::uint16_t>(t));
            if (fr.rule != fixpp::dict::field_presence::NotDeclared) {
                actual_pairs.emplace(std::string{entry.msg_type},
                                     static_cast<std::uint16_t>(t));
                ++per_msg;
                ++total_pairs;
            }
        }
        if (entry.msg_type == "D") { nos_tag_count = per_msg; }
        if (entry.msg_type == "8") { er_tag_count  = per_msg; }
    }

    // ---- Step 3: exact equality assertion ----
    EXPECT_EQ(actual_pairs.size(), expected_pairs.size())
        << "Loaded Dictionary has different (msg_type, tag) pair count than "
           "direct XML parse of FIX44.xml";

    // Report first mismatch if sizes differ.
    for (auto const& p : expected_pairs) {
        if (!actual_pairs.count(p)) {
            ADD_FAILURE() << "Expected pair missing in Dictionary: "
                          << "msg_type=\"" << p.first << "\" tag=" << p.second;
        }
    }
    for (auto const& p : actual_pairs) {
        if (!expected_pairs.count(p)) {
            ADD_FAILURE() << "Extra pair in Dictionary not in XML: "
                          << "msg_type=\"" << p.first << "\" tag=" << p.second;
        }
    }

    // ---- Secondary: heuristic floors for loud failure on catastrophic regressions ----
    EXPECT_GT(total_pairs, 200u)
        << "Distinct (msg_type, tag) coverage too low — suspect under-iteration";
    EXPECT_GT(nos_tag_count, 30u)
        << "NewOrderSingle has too few declared tags — under-iteration?";
    EXPECT_GT(er_tag_count, 50u)
        << "ExecutionReport has too few declared tags — under-iteration?";
}
