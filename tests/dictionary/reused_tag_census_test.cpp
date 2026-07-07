// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/reused_tag_census_test.cpp — 063 T016 [US1]
//
// Loader-faithful reused-NumInGroup-tag census over all nine runtime XMLs
// (research.md census methodology; tasks.md T016 / [pin#3-report]).
//
// MEMBERSHIP DERIVATION mirrors `Dictionary::as_table_view()`
// (src/dictionary/dictionary.cpp:355-422) EXACTLY: walks the real,
// loader-produced `Dictionary::messages()` / `Dictionary::message_fields()`
// (NOT a hand-rolled XML parser), builds each message's immediate-parent
// chain from `FieldRef.group_no_tag`, and derives the full outermost-first
// parent-no_tag path per NumInGroup tag. A COLLISION is a `no_tag` whose
// member SET (by content, not cardinality) differs across ≥2 contexts within
// one dict — benign same-membership reuse across messages is NOT a
// collision.
//
// DELIMITER-VARIANCE (T016 question 2) uses a SEPARATE, bounded, read-only
// pugixml scan of the raw XML (same library round_trip_test.cpp already
// uses for direct XML cross-checks) — NOT a re-implementation of
// xml_loader.cpp's component-expansion walk. Rationale (source-verified,
// see defect_a_group_context_test.cpp's escalation note + research.md
// D-A/T012): `Dictionary::message_fields()` returns FieldRef spans SORTED
// BY TAG (xml_loader.cpp's `append_run`, stable-sort+dedup for O(log n)
// lookup), which discards true wire-declaration order; the loader's own
// order-preserving structure (`GroupDef.node`) is retained ONLY for the
// first-seen XML `<group>` site per no_tag (the very object of Defect A),
// not per context. True per-context declaration order is therefore NOT
// recoverable from the current Dictionary/table_view accessor surface
// without an `xml_loader.cpp` change (T012's already-documented,
// out-of-scope-for-063 gap). The pugixml scan below instead answers a
// weaker, still-useful question: across every raw `<group name="...">`
// XML declaration site sharing a no_tag, is the first `<field>` child (the
// wire delimiter) the SAME field, or does it vary? Two distinct `<group>`
// declaration sites are exactly what a real membership collision requires
// (identical sites cannot produce different member sets), so this signal
// is a faithful (if coarser-than-per-context) proxy.
//
// This is informational (C-6, NOT a soundness gate — B-004-1): the read
// path (`OffsetTable::group()`) slices on the FRAME's own delimiter
// (offset_table.cpp:440), never on the dict's stored `group_first`, so a
// delimiter-variance finding here does not indicate a live parsing defect
// — see the printed report for the full reasoning.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/field_ref.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <iostream>
#include <map>
#include <memory>
#include <memory_resource>
#include <optional>
#include <pugixml.hpp>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace {

using fixpp::dict::Dictionary;
using fixpp::dict::field_data_type;
using fixpp::dict::FieldRef;

// One (msg_type, outer-to-inner parent-no_tag path) coordinate.
struct GroupContext {
    std::string msg_type;
    std::vector<std::uint16_t> path;
};

// A distinct member-set observed for a given no_tag, plus one representative
// context that produced it and how many distinct (msg_type,path) contexts
// share this exact set.
struct Variant {
    std::vector<std::uint16_t> members;  // sorted
    GroupContext example_context;
    std::size_t context_count = 0;
};

struct DictCensus {
    std::string name;
    // no_tag -> distinct member-set variants observed anywhere in the dict.
    std::map<std::uint16_t, std::vector<Variant>> per_tag;
    // Total distinct (msg_type, path, no_tag) triples registered — mirrors
    // exactly what Dictionary::as_table_view() would insert into
    // table_view::group_ctx_ (the census-derived table-size report).
    std::size_t total_context_entries = 0;
};

// Mirrors Dictionary::as_table_view()'s Defect-A membership derivation
// (src/dictionary/dictionary.cpp:369-422) field-for-field.
DictCensus census_for(Dictionary const& dict, std::string name) {
    DictCensus dc;
    dc.name = std::move(name);

    // Distinct (msg_type,path,no_tag) triples seen (for total_context_entries).
    std::set<std::tuple<std::string, std::vector<std::uint16_t>, std::uint16_t>> seen_contexts;

    for (auto const& msg_entry : dict.messages()) {
        auto const mt = msg_entry.msg_type;
        auto const all_fields = dict.message_fields(mt);

        std::unordered_map<std::uint16_t, std::uint16_t> immediate_parent;
        for (auto const& fr : all_fields) {
            if (fr.type == field_data_type::NumInGroup) {
                immediate_parent[fr.tag] = fr.group_no_tag;
            }
        }

        for (auto const& fr : all_fields) {
            if (fr.type != field_data_type::NumInGroup) {
                continue;
            }
            std::uint16_t const no_tag = fr.tag;

            std::vector<std::uint16_t> members;
            for (auto const& m : all_fields) {
                if (m.group_no_tag == no_tag) {
                    members.push_back(m.tag);
                }
            }
            if (members.empty()) {
                continue;  // not a real group in this message
            }
            std::sort(members.begin(), members.end());

            std::vector<std::uint16_t> path;
            std::uint16_t cur = fr.group_no_tag;
            while (cur != 0) {
                path.push_back(cur);
                auto const pit = immediate_parent.find(cur);
                cur = (pit != immediate_parent.end()) ? pit->second : std::uint16_t{0};
            }
            std::reverse(path.begin(), path.end());

            auto const ctx_key = std::make_tuple(std::string{mt}, path, no_tag);
            if (seen_contexts.insert(ctx_key).second) {
                ++dc.total_context_entries;
            }

            auto& variants = dc.per_tag[no_tag];
            bool matched = false;
            for (auto& v : variants) {
                if (v.members == members) {
                    ++v.context_count;
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                variants.push_back(Variant{members, GroupContext{std::string{mt}, path}, 1});
            }
        }
    }
    return dc;
}

// Bounded pugixml scan (T016 question 2): collect, for every raw
// `<group name="...">` declaration site anywhere in the document, the
// no_tag (via Dictionary::field_by_name on the group's own `name`
// attribute) and its first `<field>` child's tag (the wire delimiter).
struct DelimiterScan {
    // no_tag -> set of distinct delimiter tags seen across all declaration sites.
    std::map<std::uint16_t, std::set<std::uint16_t>> delimiters_by_no_tag;
    // no_tag -> number of raw <group> declaration sites found.
    std::map<std::uint16_t, std::size_t> site_count;
};

void walk_groups(pugi::xml_node const& node, Dictionary const& dict, DelimiterScan& scan) {
    for (auto const& child : node.children()) {
        std::string_view const child_name = child.name();
        if (child_name == "group") {
            auto const group_field_name = child.attribute("name").value();
            auto const no_tag_opt = dict.field_by_name(group_field_name);
            // First <field> child = the wire delimiter for this declaration site.
            std::optional<std::uint16_t> delim_tag;
            for (auto const& gc : child.children()) {
                std::string_view const gc_name = gc.name();
                if (gc_name == "field") {
                    delim_tag = dict.field_by_name(gc.attribute("name").value());
                    break;
                }
            }
            if (no_tag_opt && delim_tag) {
                scan.site_count[*no_tag_opt]++;
                scan.delimiters_by_no_tag[*no_tag_opt].insert(*delim_tag);
            }
        }
        walk_groups(child, dict, scan);
    }
}

DelimiterScan delimiter_scan_for(std::filesystem::path const& xml_path, Dictionary const& dict) {
    DelimiterScan scan;
    pugi::xml_document doc;
    auto const result = doc.load_file(xml_path.c_str());
    if (!result) {
        return scan;  // reported as empty; the primary Dictionary load already
                       // exercised the same file successfully above.
    }
    walk_groups(doc.root(), dict, scan);
    return scan;
}

constexpr std::array<std::string_view, 9> kRuntimeDicts{
    "FIX40.xml", "FIX41.xml", "FIX42.xml",   "FIX43.xml",    "FIX44.xml",
    "FIX50.xml", "FIX50SP1.xml", "FIX50SP2.xml", "FIXT11.xml",
};

}  // namespace

// T016 — the reused-tag census, printed for the orchestrator's
// re-verification. Kept informational on exact counts (research.md's
// ballpark is NOT hard-pinned — "report the truth, do not hard-fail if they
// differ") but retains a FEW hard discriminating invariants so this is not
// assertion-free coverage.
TEST(ReusedTagCensus, AllNineRuntimeDictsCensused) {
    // Generous heap-backed arena (not alloc-gated — FIX50SP2.xml is 1.4 MiB
    // of XML text and expands to a correspondingly large metadata handle;
    // sizing tightly here would risk a spurious OOM unrelated to this test's
    // actual subject, per the census-is-not-alloc-gated guidance).
    constexpr std::size_t kArenaBytes = 32UZ * 1024UZ * 1024UZ;
    auto storage = std::make_unique<std::byte[]>(kArenaBytes);
    std::pmr::monotonic_buffer_resource mr{storage.get(), kArenaBytes};

    std::size_t max_collision_depth = 0;
    bool any_delimiter_varies = false;
    std::vector<std::string> delimiter_variance_notes;

    std::cout << "\n=== 063 T016 reused-tag census (loader-faithful, all 9 runtime XMLs) ===\n";

    bool saw_fix44_295_collision = false;

    for (auto const& fname : kRuntimeDicts) {
        auto const path = std::filesystem::path{FIXPP_DICT_DATA_DIR} / std::string{fname};
        auto dict = fixpp::dict::XmlLoader{}.load(path, &mr);
        ASSERT_FALSE(dict.messages().empty()) << fname << " failed to load or has no messages";

        auto const dc = census_for(dict, std::string{fname});
        auto const scan = delimiter_scan_for(path, dict);

        // ESCALATED FINDING (source-verified, NOT part of Defect A/B, and
        // out of 063's touch-scope to fix): Dictionary::as_table_view()'s
        // group-membership population — BOTH the legacy bare-no_tag loop
        // (dictionary.cpp:340-353, byte-identical pre-063) and the new 063
        // context-scoped loop (dictionary.cpp:369-422) — identifies a
        // group's count tag via `fr.type == field_data_type::NumInGroup`
        // while walking message_fields(). The loader itself (xml_loader.cpp
        // expand_field_list's "group" arm) identifies a group STRUCTURALLY
        // (the raw `<group name="...">` XML element) regardless of what
        // TYPE the referenced count field is declared with in `<fields>`
        // — so `Dictionary::group()`/`group_fields()` (the GLOBAL,
        // non-table_view accessors) are unaffected. FIX40.xml/FIX41.xml/
        // FIX42.xml declare EVERY group count field with type INT (0
        // matches for `type='NUMINGROUP'` in all three, confirmed by
        // direct grep), so `as_table_view()`'s two loops silently register
        // ZERO groups for these three dictionaries — table_view-driven
        // group validation/parsing (`wire::Validator`, `OffsetTable::group()`
        // via `group_member_fn`) is therefore INERT for FIX40/41/42's
        // repeating groups, though the raw `Dictionary::group()`/
        // `group_fields()` structural accessors work correctly. The SAME
        // gate exists in codegen (`emit_messages.cpp` — `f->ref.type ==
        // NumInGroup`), and v42 is a codegen-target dict: confirmed
        // (`grep -c 'groups::' build/.../v42/Messages.hpp` == 0) that the
        // generated v42 flyweight has NO repeating-group accessors at all.
        // This is a PRE-EXISTING gap (unrelated to Defect A/B, present on
        // `main` before this feature) discovered as a side effect of
        // building this census faithfully against the real production
        // algorithm; fixing it would require an xml_loader.cpp/
        // dictionary.cpp/emit_messages.cpp change, out of T016's
        // test-file-only scope — reported here, not silently patched.
        {
            std::size_t const raw_group_notags = scan.site_count.size();
            std::size_t const registered_notags = dc.per_tag.size();
            if (raw_group_notags > 0 && registered_notags == 0) {
                std::cout << "  *** REGISTRATION GAP: " << raw_group_notags
                          << " distinct no_tag(s) have raw <group> XML declaration sites but "
                             "ZERO were registered by Dictionary::as_table_view() (their count "
                             "field's declared type is not NUMINGROUP — see escalation note "
                             "above). table_view-driven group parsing is INERT for this dict. "
                             "***\n";
            }
        }

        std::size_t collisions = 0;
        std::cout << "--- " << dc.name << " --- total context entries: " << dc.total_context_entries
                  << "\n";
        for (auto const& [no_tag, variants] : dc.per_tag) {
            if (variants.size() < 2) {
                continue;  // benign / non-colliding reuse
            }
            ++collisions;

            std::size_t local_max_depth = 0;
            for (auto const& v : variants) {
                local_max_depth = std::max(local_max_depth, v.example_context.path.size());
            }
            max_collision_depth = std::max(max_collision_depth, local_max_depth);

            std::cout << "  COLLISION no_tag=" << no_tag << " variant_count=" << variants.size()
                      << " max_variant_depth=" << local_max_depth << "\n";
            for (auto const& v : variants) {
                std::cout << "    variant members={";
                for (std::size_t i = 0; i < v.members.size(); ++i) {
                    std::cout << v.members[i] << (i + 1 < v.members.size() ? "," : "");
                }
                std::cout << "} example_context=(msg_type=" << v.example_context.msg_type
                          << ", path=[";
                for (std::size_t i = 0; i < v.example_context.path.size(); ++i) {
                    std::cout << v.example_context.path[i]
                              << (i + 1 < v.example_context.path.size() ? "," : "");
                }
                std::cout << "]) contexts_sharing_this_set=" << v.context_count << "\n";
            }

            auto const dit = scan.delimiters_by_no_tag.find(no_tag);
            if (dit == scan.delimiters_by_no_tag.end() || dit->second.empty()) {
                std::cout << "    delimiter-scan: no raw <group> declaration site found (unexpected)\n";
                continue;
            }
            auto const site_n = scan.site_count.at(no_tag);
            if (dit->second.size() > 1) {
                any_delimiter_varies = true;
                std::ostringstream oss;
                oss << dc.name << " no_tag=" << no_tag << " (" << site_n
                    << " raw <group> sites, " << dit->second.size() << " distinct delimiters)";
                delimiter_variance_notes.push_back(oss.str());
                std::cout << "    delimiter-scan: VARIES across " << site_n
                          << " raw <group> declaration sites (" << dit->second.size()
                          << " distinct delimiter tags)\n";
            } else {
                std::cout << "    delimiter-scan: SAME delimiter across all " << site_n
                          << " raw <group> declaration sites\n";
            }
        }
        std::cout << dc.name << ": " << collisions << " colliding no_tag(s)\n";

        if (dc.name == "FIX44.xml") {
            auto const it295 = dc.per_tag.find(295);
            if (it295 != dc.per_tag.end() && it295->second.size() >= 2) {
                bool has_quote_entry_variant = false, has_quote_cxl_variant = false;
                for (auto const& v : it295->second) {
                    bool const has299 =
                        std::find(v.members.begin(), v.members.end(), 299) != v.members.end();
                    if (has299) has_quote_entry_variant = true;
                    else has_quote_cxl_variant = true;
                }
                saw_fix44_295_collision = has_quote_entry_variant && has_quote_cxl_variant;
            }
        }
    }

    std::cout << "=== Q1: max nesting depth (parent-path length) at which any collision occurs: "
              << max_collision_depth << " ===\n";
    std::cout << "=== Q2: delimiter variance across colliding no_tags: "
              << (any_delimiter_varies ? "AT LEAST ONE VARIES" : "ALL SHARE A DELIMITER") << " ===\n";
    for (auto const& note : delimiter_variance_notes) {
        std::cout << "    varying: " << note << "\n";
    }
    std::cout << "=== NOTE: true per-context wire-declaration order is NOT recoverable from the "
                 "Dictionary/table_view accessor surface (message_fields() is tag-sorted; see "
                 "file header) — the above delimiter-scan compares raw <group> declaration "
                 "SITES, a coarser but source-grounded proxy. This is moot for read-path "
                 "correctness: OffsetTable::group() slices on the FRAME's own delimiter "
                 "(offset_table.cpp:440), never on the dict's stored group_first. ===\n";

    // Hard discriminating invariant (not assertion-free coverage): the
    // FIX44 295 MassQuote-vs-QuotCxlEntriesGrp collision this feature's own
    // T010 exemplar is built on must appear in the loader-faithful census.
    EXPECT_TRUE(saw_fix44_295_collision)
        << "FIX44 tag 295 must show ≥2 variants, one containing QuoteEntryID(299) and one not "
           "— the census must reproduce the discriminator T010 already proved by hand";
}
