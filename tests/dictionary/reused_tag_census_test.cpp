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

#include "required_scope_oracle.hpp"  // 082 T008: independent group-tag census
#include "reused_tag_census.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fixpp/dict/dictionary.hpp>
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
#include <vector>

namespace {

using fixpp::dict::Dictionary;
using fixpp_test_support::census_for;
using fixpp_test_support::DictCensus;
using fixpp_test_support::kRuntimeDicts;
using fixpp_test_support::Variant;

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

        auto const oracle = fixpp_test::required_scope_oracle::build_quickfix_oracle(path);
        auto const dc = census_for(dict, std::string{fname}, oracle.group_tags);
        auto const scan = delimiter_scan_for(path, dict);

        // L-063-1 — RESOLVED at the test-time census by 082 T008
        // (contracts/group-detection.md FR-018/FR-004): the finding below
        // originally documented was that `Dictionary::as_table_view()`'s
        // group-membership population — both the legacy bare-no_tag loop
        // and the 063 context-scoped loop — identified a group's count tag
        // via `fr.type == field_data_type::NumInGroup` while walking
        // message_fields(), so FIX40.xml/FIX41.xml/FIX42.xml (which declare
        // EVERY group count field with type INT, not NUMINGROUP) silently
        // registered ZERO groups. `census_for` (this file's own census
        // helper, reused_tag_census.hpp) used the SAME datatype gate, so it
        // reproduced the gap rather than witnessing it — the load-bearing
        // reason 082 T008 re-points `census_for`'s gate onto
        // `required_scope_oracle.hpp`'s independent, structural, non-
        // circular group-tag census (`DictOracle::group_tags` — a real
        // `<group name="...">` element, regardless of the referenced
        // field's declared datatype) instead. As of that re-point, THIS
        // census now registers FIX40/41/42's groups correctly (4/7/18 tags,
        // matching contracts/group-detection.md C2's registered-after
        // column) — the loop below is therefore expected to find no
        // registration gap for any of the nine runtime dicts.
        //
        // The underlying PRODUCTION gap this originally reported is NOT yet
        // fixed by this test-file-only re-point: `Dictionary::as_table_view()`
        // itself (`dictionary.cpp:398,441,446`) and codegen's
        // `emit_messages.cpp` (`f->ref.type == NumInGroup`) both still gate on
        // the same datatype test, so `wire::Validator`/`OffsetTable::group()`
        // and the generated `v42` flyweight remain INERT for FIX40/41/42's
        // repeating groups until 082's own T023 (runtime predicate
        // replacement) and T024/T025 (codegen re-point) land — no longer an
        // open, unowned limitation; it is this feature's Phase 3 work.
        {
            std::size_t const raw_group_notags = scan.site_count.size();
            std::size_t const registered_notags = dc.per_tag.size();
            if (raw_group_notags > 0 && registered_notags == 0) {
                std::cout << "  *** REGISTRATION GAP: " << raw_group_notags
                          << " distinct no_tag(s) have raw <group> XML declaration sites but "
                             "ZERO were registered by this census (unexpected post-082-T008 — "
                             "the independent oracle's group-tag set should register every "
                             "declared, message-reachable <group>). ***\n";
            }
        }

        // 082 T006 (contracts/group-detection.md C2/K1, FR-018/SC-002): the
        // oracle's OWN output pinned against LITERAL constants transcribed
        // from C2's registered-after column — deliberately NOT compared
        // against `dc`/the loaded Dictionary here (that oracle-vs-actual
        // comparison is T015/T016/T018/T042's job, landed separately). A
        // pin against the loader only proves the two independent
        // derivations agree with EACH OTHER; if the oracle were ever
        // "simplified" to track a drifted loader, an oracle-vs-actual pin
        // would still pass while both sides are wrong together. Do NOT
        // collapse this into an oracle-vs-actual check.
        {
            static std::map<std::string_view, std::size_t> const kExpectedGroupTags{
                {"FIX40.xml", 4},     {"FIX41.xml", 7},      {"FIX42.xml", 18},
                {"FIX43.xml", 34},    {"FIX44.xml", 59},     {"FIX50.xml", 67},
                {"FIX50SP1.xml", 97}, {"FIX50SP2.xml", 505}, {"FIXT11.xml", 1},
            };
            EXPECT_EQ(oracle.group_tags.size(), kExpectedGroupTags.at(fname))
                << fname << ": oracle group_tags count vs literal C2 registered-after constant";
            EXPECT_EQ(fixpp_test::required_scope_oracle::count_zero_member_groups_quickfix(path), 0U)
                << fname << ": zero-member-<group> count vs literal 0 (FR-023/K11/P1-NON)";
        }
        if (fname == "FIX40.xml") {
            std::set<std::uint16_t> const kExpected{73, 78, 124, 136};
            EXPECT_EQ(oracle.group_tags, kExpected) << "FIX40 group-tag set vs literal C2 set";
        } else if (fname == "FIX41.xml") {
            std::set<std::uint16_t> const kExpected{33, 73, 78, 124, 136, 146, 199};
            EXPECT_EQ(oracle.group_tags, kExpected) << "FIX41 group-tag set vs literal C2 set";
        } else if (fname == "FIX42.xml") {
            std::set<std::uint16_t> const kExpected{33,  73,  78,  124, 136, 146, 199, 215, 267,
                                                     268, 295, 296, 382, 384, 386, 398, 420, 428};
            EXPECT_EQ(oracle.group_tags, kExpected) << "FIX42 group-tag set vs literal C2 set";
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
                std::cout
                    << "    delimiter-scan: no raw <group> declaration site found (unexpected)\n";
                continue;
            }
            auto const site_n = scan.site_count.at(no_tag);
            if (dit->second.size() > 1) {
                any_delimiter_varies = true;
                std::ostringstream oss;
                oss << dc.name << " no_tag=" << no_tag << " (" << site_n << " raw <group> sites, "
                    << dit->second.size() << " distinct delimiters)";
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
                    if (has299)
                        has_quote_entry_variant = true;
                    else
                        has_quote_cxl_variant = true;
                }
                saw_fix44_295_collision = has_quote_entry_variant && has_quote_cxl_variant;
            }
        }
    }

    std::cout << "=== Q1: max nesting depth (parent-path length) at which any collision occurs: "
              << max_collision_depth << " ===\n";
    std::cout << "=== Q2: delimiter variance across colliding no_tags: "
              << (any_delimiter_varies ? "AT LEAST ONE VARIES" : "ALL SHARE A DELIMITER")
              << " ===\n";
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

// 082 T006 — Orchestra FIX Latest's leg of the same literal-constant pin.
// Not in kRuntimeDicts (that array is QuickFIX-XML only), so it is not
// exercised by the loop above. Same rationale as there: pinned against C2's
// literal 524, not against a loaded Dictionary/table_view.
TEST(ReusedTagCensus, OrchestraFixLatestGroupTagsMatchesLiteralConstant) {
    auto const path =
        std::filesystem::path{FIXPP_ORCHESTRA_DATA_DIR} / "OrchestraFIXLatest.xml";
    auto const oracle = fixpp_test::required_scope_oracle::build_orchestra_oracle(path);
    EXPECT_EQ(oracle.group_tags.size(), 524U)
        << "Orchestra FIX Latest: oracle group_tags count vs literal C2 registered-after constant";
    EXPECT_EQ(fixpp_test::required_scope_oracle::count_zero_member_groups_orchestra(path), 0U)
        << "Orchestra FIX Latest: zero-member-<group> count vs literal 0 (FR-023/K11/P1-NON)";
}

// ============================================================================
// 072-nested-group-hardening — FR-001 / FR-002 structural census (Part A).
//
// These two PERMANENT, CI-tripping assertions pin the two structural dictionary
// conventions the nested-group read relies on, across ALL membership contexts —
// deliberately a strictly-stronger walk than the FR-003 load guard's first-seen
// `groups_` seam (research D-A3). They are derived from a RAW per-`<group>` XML
// walk that (i) threads the immediate-enclosing-group's delimiter down the
// recursion, (ii) expands `<component>` references so cross-component nesting is
// parent-linked, and (iii) resolves each group's OWN delimiter POST-component-
// expansion (first field of the fully-expanded member list, mirroring the
// loader's `expand_field_list`) — NOT the first literal `<field>` child. This
// walk is count-field-type-agnostic (keys on the raw `<group>` element, not on
// `NumInGroup`), so it covers FIX40/41/42 (whose INT-typed count fields register
// ZERO groups under `as_table_view()`/`census_for`) non-vacuously.
//
// Each assertion carries a POSITIVE CONTROL (an injected-bad inline dict that
// MUST be flagged) as a permanent trip-proof — a never-red census proves nothing
// (Constitution Art VII §3).
// ============================================================================

namespace {

using fixpp::dict::Dictionary;

// Component-name -> definition node, built once per document.
using ComponentIndex = std::map<std::string_view, pugi::xml_node>;

ComponentIndex build_component_index(pugi::xml_node const& fix_root) {
    ComponentIndex idx;
    for (auto const& c : fix_root.child("components").children("component")) {
        idx.emplace(std::string_view{c.attribute("name").value()}, c);
    }
    return idx;
}

// Resolve the leading field tag of a group/component member list, expanding
// `<component>` refs (mirrors the loader's post-component-expansion delimiter).
// A leading `<group>` member contributes its own count field (the NoX tag).
// `stack` guards `<component>` cycles.
std::optional<std::uint16_t> resolve_leading_field(pugi::xml_node node, ComponentIndex const& cidx,
                                                   Dictionary const& dict,
                                                   std::set<std::string_view>& stack) {
    for (auto const& child : node.children()) {
        std::string_view const n = child.name();
        if (n == "field" || n == "group") {
            if (auto t = dict.field_by_name(child.attribute("name").value())) {
                return t;
            }
        } else if (n == "component") {
            std::string_view const cn = child.attribute("name").value();
            auto const it = cidx.find(cn);
            if (it != cidx.end() && stack.insert(cn).second) {
                auto const r = resolve_leading_field(it->second, cidx, dict, stack);
                stack.erase(cn);
                if (r) {
                    return r;
                }
            }
        }
    }
    return std::nullopt;
}

struct Fr001Result {
    std::size_t sites = 0;                // group-declaration sites observed
    std::vector<std::string> collisions;  // nested delim == parent delim
};

// Recursive descent from a wire usage container (message/header/trailer),
// threading the immediate-enclosing-group's delimiter (`parent_delim`).
void walk_fr001(pugi::xml_node node, std::optional<std::uint16_t> parent_delim,
                ComponentIndex const& cidx, Dictionary const& dict,
                std::set<std::string_view>& comp_stack, Fr001Result& r) {
    for (auto const& child : node.children()) {
        std::string_view const n = child.name();
        if (n == "group") {
            ++r.sites;
            std::set<std::string_view> seen;
            auto const own_delim = resolve_leading_field(child, cidx, dict, seen);
            if (parent_delim && own_delim && *parent_delim == *own_delim) {
                auto const own_no_tag = dict.field_by_name(child.attribute("name").value());
                std::ostringstream oss;
                oss << "nested group no_tag=" << (own_no_tag ? *own_no_tag : 0) << " delimiter "
                    << *own_delim << " == parent group delimiter";
                r.collisions.push_back(oss.str());
            }
            walk_fr001(child, own_delim, cidx, dict, comp_stack, r);  // own_delim is the new parent
        } else if (n == "component") {
            std::string_view const cn = child.attribute("name").value();
            auto const it = cidx.find(cn);
            if (it != cidx.end() && comp_stack.insert(cn).second) {
                walk_fr001(it->second, parent_delim, cidx, dict, comp_stack,
                           r);  // keep parent delim
                comp_stack.erase(cn);
            }
        } else {
            walk_fr001(child, parent_delim, cidx, dict, comp_stack, r);  // container: same context
        }
    }
}

// Run the FR-001 census over a raw XML document + its loaded Dictionary,
// walking only the wire usage contexts (header/trailer/messages) so component
// refs are expanded at their real enclosing-group site.
Fr001Result census_fr001(pugi::xml_document const& doc, Dictionary const& dict) {
    auto const fix = doc.document_element();
    auto const cidx = build_component_index(fix);
    Fr001Result r;
    std::set<std::string_view> comp_stack;
    for (char const* section : {"header", "trailer"}) {
        if (auto s = fix.child(section)) {
            walk_fr001(s, std::nullopt, cidx, dict, comp_stack, r);
        }
    }
    for (auto const& m : fix.child("messages").children("message")) {
        walk_fr001(m, std::nullopt, cidx, dict, comp_stack, r);
    }
    return r;
}

// Contents at one group level (component-expanded, NOT descending into nested
// groups): the scalar field tags declared at this level, plus the nested child
// `<group>` nodes for pairwise disjointness + recursion.
struct LevelContents {
    std::set<std::uint16_t> scalar_fields;
    std::vector<pugi::xml_node> child_groups;
};

void collect_level(pugi::xml_node node, ComponentIndex const& cidx, Dictionary const& dict,
                   std::set<std::string_view>& stack, LevelContents& out) {
    for (auto const& child : node.children()) {
        std::string_view const n = child.name();
        if (n == "field") {
            if (auto t = dict.field_by_name(child.attribute("name").value())) {
                out.scalar_fields.insert(*t);
            }
        } else if (n == "group") {
            if (auto t = dict.field_by_name(child.attribute("name").value())) {
                out.scalar_fields.insert(*t);  // the nested group's count is a scalar of THIS level
            }
            out.child_groups.push_back(child);  // but its members belong to the child level
        } else if (n == "component") {
            std::string_view const cn = child.attribute("name").value();
            auto const it = cidx.find(cn);
            if (it != cidx.end() && stack.insert(cn).second) {
                collect_level(it->second, cidx, dict, stack, out);
                stack.erase(cn);
            }
        }
    }
}

struct Fr002Result {
    std::size_t member_sets_examined = 0;
    std::vector<std::string> collisions;  // scalar tag shared parent <-> nested child
};

// `parent` is this group's ALREADY-collected level (computed once by the caller).
// Check each nested child's scalar members are disjoint from this level's, then
// recurse with the child level (so each group's `collect_level` runs exactly once).
void walk_fr002_group(LevelContents const& parent, ComponentIndex const& cidx,
                      Dictionary const& dict, Fr002Result& r) {
    ++r.member_sets_examined;
    for (auto const& cg : parent.child_groups) {
        std::set<std::string_view> cstack;
        LevelContents child;
        collect_level(cg, cidx, dict, cstack, child);
        for (auto const t : parent.scalar_fields) {
            if (child.scalar_fields.contains(t)) {
                auto const cno = dict.field_by_name(cg.attribute("name").value());
                std::ostringstream oss;
                oss << "scalar tag " << t
                    << " shared between a parent group and nested child no_tag="
                    << (cno ? *cno : 0);
                r.collisions.push_back(oss.str());
            }
        }
        walk_fr002_group(child, cidx, dict, r);  // recurse for deeper parent/child pairs
    }
}

Fr002Result census_fr002(pugi::xml_document const& doc, Dictionary const& dict) {
    auto const fix = doc.document_element();
    auto const cidx = build_component_index(fix);
    Fr002Result r;
    auto const process = [&](pugi::xml_node container) {
        std::set<std::string_view> stack;
        LevelContents top;
        collect_level(container, cidx, dict, stack, top);
        for (auto const& g : top.child_groups) {
            std::set<std::string_view> gstack;
            LevelContents glevel;
            collect_level(g, cidx, dict, gstack, glevel);
            walk_fr002_group(glevel, cidx, dict, r);
        }
    };
    for (char const* section : {"header", "trailer"}) {
        if (auto s = fix.child(section)) {
            process(s);
        }
    }
    for (auto const& m : fix.child("messages").children("message")) {
        process(m);
    }
    return r;
}

// Load a raw XML doc from a fixture path (read-only pugixml, same as
// delimiter_scan_for above).
pugi::xml_document load_raw(std::filesystem::path const& xml_path) {
    pugi::xml_document doc;
    (void)doc.load_file(xml_path.c_str());
    return doc;
}

}  // namespace

// FR-001: no nested repeating group's delimiter equals its immediate parent
// group's delimiter, in EVERY membership context, across all 9 runtime dicts,
// NON-VACUOUSLY (>0 group-declaration sites per dict). Positive control first.
TEST(NestedGroupDelimiterCensus, NoNestedGroupDelimiterEqualsParentDelimiter) {
    // ---- Positive control (permanent trip-proof): an injected nested==parent
    // delimiter MUST be flagged, else this census could pass vacuously. ----
    {
        constexpr std::string_view kBad =
            R"(<fix type='FIX' major='4' minor='4' servicepack='0'>)"
            R"(<fields>)"
            R"(<field number='35' name='MsgType' type='STRING'/>)"
            R"(<field number='100' name='NoOuter' type='NUMINGROUP'/>)"
            R"(<field number='150' name='SharedDelim' type='INT'/>)"
            R"(<field number='200' name='NoInner' type='NUMINGROUP'/>)"
            R"(<field number='250' name='InnerData' type='STRING'/>)"
            R"(</fields>)"
            R"(<messages><message name='Bad' msgtype='U' msgcat='app'>)"
            R"(<field name='MsgType' required='N'/>)"
            R"(<group name='NoOuter' required='N'>)"
            R"(<field name='SharedDelim' required='N'/>)"  // outer delimiter = 150
            R"(<group name='NoInner' required='N'>)"
            R"(<field name='SharedDelim' required='N'/>)"  // inner delimiter = 150 == outer
            R"(<field name='InnerData' required='N'/>)"
            R"(</group></group></message></messages></fix>)";
        std::array<std::byte, 1UZ << 20> buf{};
        std::pmr::monotonic_buffer_resource mr{buf.data(), buf.size()};
        // NOTE: the load itself would throw under the FR-003 guard; the census is
        // a pure XML+Dictionary analysis, so parse the raw doc directly and build
        // a Dictionary via load_from_string is not possible (guard throws). Build
        // the Dictionary from a guard-free CONFORMING twin (distinct delimiters)
        // so field_by_name resolves, then run the census over the BAD raw doc.
        constexpr std::string_view kNamesOnly =
            R"(<fix type='FIX' major='4' minor='4' servicepack='0'>)"
            R"(<fields>)"
            R"(<field number='35' name='MsgType' type='STRING'/>)"
            R"(<field number='100' name='NoOuter' type='NUMINGROUP'/>)"
            R"(<field number='150' name='SharedDelim' type='INT'/>)"
            R"(<field number='200' name='NoInner' type='NUMINGROUP'/>)"
            R"(<field number='250' name='InnerData' type='STRING'/>)"
            R"(</fields>)"
            R"(<messages><message name='Ok' msgtype='U' msgcat='app'>)"
            R"(<field name='MsgType' required='N'/>)"
            R"(<group name='NoOuter' required='N'>)"
            R"(<field name='SharedDelim' required='N'/>)"
            R"(<group name='NoInner' required='N'>)"
            R"(<field name='InnerData' required='N'/>)"  // distinct delimiter 250 → loads
            R"(<field name='SharedDelim' required='N'/>)"
            R"(</group></group></message></messages></fix>)";
        auto dict = fixpp::dict::XmlLoader{}.load_from_string(kNamesOnly, &mr);
        pugi::xml_document bad_doc;
        ASSERT_TRUE(bad_doc.load_string(std::string{kBad}.c_str()));
        auto const control = census_fr001(bad_doc, dict);
        EXPECT_GT(control.sites, 0U) << "positive control must observe group sites";
        EXPECT_FALSE(control.collisions.empty())
            << "FR-001 census FAILED to flag an injected nested==parent delimiter — a never-red "
               "census proves nothing";
    }

    // ---- Real census: all 9 shipped dicts clean + non-vacuous. ----
    constexpr std::size_t kArenaBytes = 32UZ * 1024UZ * 1024UZ;
    auto storage = std::make_unique<std::byte[]>(kArenaBytes);
    std::pmr::monotonic_buffer_resource mr{storage.get(), kArenaBytes};

    for (auto const& fname : kRuntimeDicts) {
        auto const path = std::filesystem::path{FIXPP_DICT_DATA_DIR} / std::string{fname};
        auto dict = fixpp::dict::XmlLoader{}.load(path, &mr);
        ASSERT_FALSE(dict.messages().empty()) << fname << " failed to load";
        auto const doc = load_raw(path);
        auto const res = census_fr001(doc, dict);
        EXPECT_GT(res.sites, 0U) << fname
                                 << ": FR-001 census observed 0 group-declaration sites (vacuous)";
        for (auto const& c : res.collisions) {
            ADD_FAILURE() << fname << ": FR-001 delimiter collision — " << c;
        }
    }
}

// FR-002: no scalar member tag shared between a parent group and its nested
// child group, across all dicts it can cover, NON-VACUOUSLY (>0 member-sets
// examined). The structural walk recovers per-group member sets for all 9 dicts
// including FIX40/41/42 (it keys on the raw `<group>` element, not NumInGroup),
// so no dict is left as an unpinned residual here (FR-013d fallback unneeded).
TEST(NestedGroupScalarMemberCensus, NoSharedParentChildScalarMember) {
    // ---- Positive control: injected shared parent/child scalar MUST be flagged. ----
    {
        constexpr std::string_view kBad =
            R"(<fix type='FIX' major='4' minor='4' servicepack='0'>)"
            R"(<fields>)"
            R"(<field number='35' name='MsgType' type='STRING'/>)"
            R"(<field number='100' name='NoOuter' type='NUMINGROUP'/>)"
            R"(<field number='150' name='OuterDelim' type='INT'/>)"
            R"(<field number='300' name='SharedScalar' type='STRING'/>)"
            R"(<field number='200' name='NoInner' type='NUMINGROUP'/>)"
            R"(<field number='250' name='InnerDelim' type='STRING'/>)"
            R"(</fields>)"
            R"(<messages><message name='Bad' msgtype='U' msgcat='app'>)"
            R"(<field name='MsgType' required='N'/>)"
            R"(<group name='NoOuter' required='N'>)"
            R"(<field name='OuterDelim' required='N'/>)"    // outer delim 150 (disjoint)
            R"(<field name='SharedScalar' required='N'/>)"  // 300 in parent
            R"(<group name='NoInner' required='N'>)"
            R"(<field name='InnerDelim' required='N'/>)"    // inner delim 250 (disjoint)
            R"(<field name='SharedScalar' required='N'/>)"  // 300 in child — SHARED
            R"(</group></group></message></messages></fix>)";
        std::array<std::byte, 1UZ << 20> buf{};
        std::pmr::monotonic_buffer_resource mr{buf.data(), buf.size()};
        // Delimiters are disjoint (150 vs 250), so this dict LOADS (FR-004
        // asymmetry) — build the Dictionary from it directly.
        auto dict = fixpp::dict::XmlLoader{}.load_from_string(kBad, &mr);
        pugi::xml_document doc;
        ASSERT_TRUE(doc.load_string(std::string{kBad}.c_str()));
        auto const control = census_fr002(doc, dict);
        EXPECT_GT(control.member_sets_examined, 0U);
        EXPECT_FALSE(control.collisions.empty())
            << "FR-002 census FAILED to flag an injected shared parent/child scalar member";
    }

    // ---- Real census: all 9 shipped dicts clean + non-vacuous. ----
    constexpr std::size_t kArenaBytes = 32UZ * 1024UZ * 1024UZ;
    auto storage = std::make_unique<std::byte[]>(kArenaBytes);
    std::pmr::monotonic_buffer_resource mr{storage.get(), kArenaBytes};

    for (auto const& fname : kRuntimeDicts) {
        auto const path = std::filesystem::path{FIXPP_DICT_DATA_DIR} / std::string{fname};
        auto dict = fixpp::dict::XmlLoader{}.load(path, &mr);
        ASSERT_FALSE(dict.messages().empty()) << fname << " failed to load";
        auto const doc = load_raw(path);
        auto const res = census_fr002(doc, dict);
        EXPECT_GT(res.member_sets_examined, 0U)
            << fname << ": FR-002 census examined 0 member-sets (vacuous)";
        for (auto const& c : res.collisions) {
            ADD_FAILURE() << fname << ": FR-002 scalar-member collision — " << c;
        }
    }
}
