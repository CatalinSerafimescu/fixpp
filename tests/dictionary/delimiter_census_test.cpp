// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/delimiter_census_test.cpp
//
// 083-group-delimiter-resolution T006 (FR-002/FR-010/FR-012/FR-013/FR-015) —
// the all-ten regression pin. For every group context in all ten dictionaries
// (nine QuickFIX-XML + Orchestra FIX Latest), with no carve-out, exclusion
// list, or per-dictionary exemption, assert the engine's resolved delimiter
// equals the independent oracle's declaration-order first member, and that
// the member set matches (FR-015 rides the same pin, not a second
// assertion).
//
// THIS TASK PRODUCES A RED TEST. spec.md's Baseline table measures 335
// wrong-delimiter contexts, 52 polluted member sets and 30 unregistered
// contexts on today's tree (before Phase 3/US1's loader fix lands); the
// assertions below express the POST-FIX target (every bucket zero), so they
// are expected to fail today. That is the point of a Foundational RED task
// (`[const §VII.3]`/`[const §VII.4]`) — see tasks.md Phase 2.
//
// Anchors:
//   tasks:     specs/083-group-delimiter-resolution/tasks.md T006
//   contracts: specs/083-group-delimiter-resolution/contracts/group_ctx_delims.md
//              ("Lookup-miss behaviour")
//   spec:      specs/083-group-delimiter-resolution/spec.md Baseline table
//   quickstart: specs/083-group-delimiter-resolution/quickstart.md §3
//
// ============================================================================
// MISS-DISCRIMINATION (contracts/group_ctx_delims.md "Lookup-miss
// behaviour"): both `table_view::group_first_field(msg_type, path, no_tag)`
// and `group_member_tags(msg_type, path, no_tag)` fall back to the BARE
// global store on a context miss, so a miss is indistinguishable from a
// wrong answer unless discriminated explicitly. We discriminate by comparing
// the returned member-span's `.data()` pointer against the bare span's —
// omitting this inflated fixpp#210's originally reported defect count by 10
// contexts. A context is classified into exactly one of:
//   - unregistered   — registered == false (no record in the context store;
//                       includes the group_bit()-cleared case, where BOTH
//                       accessors short-circuit to an empty/nullptr span and
//                       the pointers compare equal, correctly reading as
//                       "unregistered", never as "registered with an empty
//                       set");
//   - wrong delimiter — registered == true AND the resolved delimiter !=
//                       the oracle's declaration-order first member;
//   - polluted member set — registered == true AND the member set (as a
//                       set) != the oracle's member set.
// A context can be BOTH wrong-delimiter and polluted (independent columns,
// not a partition); "unregistered" is exclusive of the other two.
// ============================================================================

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/orchestra_loader.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <iostream>
#include <memory>
#include <memory_resource>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "required_scope_oracle.hpp"  // the shared independent oracle (079 T018/T019 extraction)

namespace {

using fixpp::dict::Dictionary;
using fixpp::dict::table_view;
using fixpp_test::required_scope_oracle::build_orchestra_oracle;
using fixpp_test::required_scope_oracle::build_quickfix_oracle;
using fixpp_test::required_scope_oracle::DictOracle;
using fixpp_test::required_scope_oracle::GroupContextKey;

struct DictCase {
    std::string label;
    std::string filename;
    bool is_orchestra = false;
};

std::vector<DictCase> const kAllDicts{
    {.label = "FIX40", .filename = "FIX40.xml", .is_orchestra = false},
    {.label = "FIX41", .filename = "FIX41.xml", .is_orchestra = false},
    {.label = "FIX42", .filename = "FIX42.xml", .is_orchestra = false},
    {.label = "FIX43", .filename = "FIX43.xml", .is_orchestra = false},
    {.label = "FIX44", .filename = "FIX44.xml", .is_orchestra = false},
    {.label = "FIX50", .filename = "FIX50.xml", .is_orchestra = false},
    {.label = "FIX50SP1", .filename = "FIX50SP1.xml", .is_orchestra = false},
    {.label = "FIX50SP2", .filename = "FIX50SP2.xml", .is_orchestra = false},
    {.label = "FIXT11", .filename = "FIXT11.xml", .is_orchestra = false},
    {.label = "Orchestra FIX Latest", .filename = "OrchestraFIXLatest.xml", .is_orchestra = true},
};

std::filesystem::path dict_path(DictCase const& dc) {
    if (dc.is_orchestra) {
        return std::filesystem::path{FIXPP_ORCHESTRA_DATA_DIR} / dc.filename;
    }
    return std::filesystem::path{FIXPP_DICT_DATA_DIR} / dc.filename;
}

DictOracle build_oracle(DictCase const& dc) {
    return dc.is_orchestra ? build_orchestra_oracle(dict_path(dc)) : build_quickfix_oracle(dict_path(dc));
}

Dictionary load_actual(DictCase const& dc, std::pmr::memory_resource* mr) {
    if (dc.is_orchestra) {
        return fixpp::dict::OrchestraLoader{}.load(dict_path(dc), mr);
    }
    return fixpp::dict::XmlLoader{}.load(dict_path(dc), mr);
}

std::set<std::uint16_t> to_set(std::span<std::uint16_t const> s) {
    return std::set<std::uint16_t>{s.begin(), s.end()};
}

// Dictionaries here are large (FIX50SP2 / Orchestra FIX Latest have tens of
// thousands of contexts) — mirrors required_scope_census_test.cpp's arena.
constexpr std::size_t kArenaBytes = 32UZ * 1024UZ * 1024UZ;

// Per-dictionary tally, matching spec.md's Baseline table columns exactly.
struct DictCensus {
    std::string label;
    std::size_t contexts = 0;
    std::size_t wrong_delimiter = 0;
    std::size_t wrong_delimiter_nested = 0;  // of those, the delimiter is itself a nested group
    std::size_t polluted = 0;
    std::size_t unregistered = 0;
};

// Cap the per-context failure DETAIL lines printed per dictionary — an
// unbounded diff on a 25897-context dictionary has OOM-killed this host
// before ([[feedback_large_file_expect_eq_builds_unbounded_gtest_diff_ooms_host]]).
// The per-dictionary COUNTS below are always printed in full, uncapped.
constexpr std::size_t kMaxDetailLinesPerDict = 20;

// Runs the census for one dictionary: walks every context the independent
// oracle found, discriminates registration via the member-span data-pointer
// comparison, and classifies each context into the buckets above. Detail
// lines for the first `kMaxDetailLinesPerDict` defects are printed to
// std::cout; the full counts are always accumulated and returned.
DictCensus census_one(DictCase const& dc) {
    DictCensus census{.label = dc.label};

    auto storage = std::make_unique<std::byte[]>(kArenaBytes);
    std::pmr::monotonic_buffer_resource mr{storage.get(), kArenaBytes};

    auto const oracle = build_oracle(dc);
    auto const dict = load_actual(dc, &mr);
    auto const tv = dict.as_table_view();

    std::size_t detail_printed = 0;

    for (auto const& [key, expected_members] : oracle.group_members) {
        ++census.contexts;

        auto const ctx_members = tv.group_member_tags(key.msg_type, std::span{key.path}, key.no_tag);
        auto const bare_members = tv.group_member_tags(key.no_tag);
        bool const registered = ctx_members.data() != bare_members.data();

        if (!registered) {
            ++census.unregistered;
            if (detail_printed < kMaxDetailLinesPerDict) {
                std::cout << "    [unregistered] " << dc.label << " msg=" << key.msg_type
                          << " no_tag=" << key.no_tag << "\n";
                ++detail_printed;
            }
            continue;
        }

        auto const dit = oracle.group_delims.find(key);
        EXPECT_NE(dit, oracle.group_delims.end())
            << dc.label << ": oracle.group_delims missing an entry group_members has (walker bug) — "
               "msg=" << key.msg_type << " no_tag=" << key.no_tag;
        if (dit == oracle.group_delims.end()) {
            continue;  // ASSERT_ is unusable in a non-void function; guard manually.
        }
        std::uint16_t const expected_delim = dit->second;
        std::uint16_t const actual_delim =
            tv.group_first_field(key.msg_type, std::span{key.path}, key.no_tag);

        bool const wrong_delim = actual_delim != expected_delim;
        bool nested = false;
        if (wrong_delim) {
            ++census.wrong_delimiter;
            // FR-004: is the (oracle's) expected delimiter itself a nested
            // group's count tag, structurally reachable as a direct child of
            // THIS context? Exact per-context check: does the oracle record
            // a context keyed (msg_type, path + this no_tag, expected_delim)?
            std::vector<std::uint16_t> child_path = key.path;
            child_path.push_back(key.no_tag);
            GroupContextKey const child_key{key.msg_type, child_path, expected_delim};
            nested = oracle.group_members.contains(child_key);
            if (nested) {
                ++census.wrong_delimiter_nested;
            }
        }

        auto const actual_set = to_set(ctx_members);
        bool const polluted = actual_set != expected_members;
        if (polluted) {
            ++census.polluted;
        }

        if ((wrong_delim || polluted) && detail_printed < kMaxDetailLinesPerDict) {
            std::cout << "    [defect] " << dc.label << " msg=" << key.msg_type
                      << " no_tag=" << key.no_tag
                      << (wrong_delim ? (" WRONG_DELIM(expected=" + std::to_string(expected_delim) +
                                         " actual=" + std::to_string(actual_delim) + ")" +
                                         (nested ? "[nested]" : ""))
                                      : "")
                      << (polluted ? " POLLUTED" : "") << "\n";
            ++detail_printed;
        }
    }

    return census;
}

void print_census_table(std::vector<DictCensus> const& all) {
    std::cout << "\n=== 083 T006 delimiter census — all ten dictionaries ===\n";
    std::cout << "  dictionary            contexts  wrong  wrong(nested)  polluted  unregistered\n";
    DictCensus total{.label = "TOTAL"};
    for (auto const& c : all) {
        std::cout << "  " << c.label << std::string(std::max<std::size_t>(1, 22 - c.label.size()), ' ')
                  << c.contexts << "  " << c.wrong_delimiter << "  " << c.wrong_delimiter_nested << "  "
                  << c.polluted << "  " << c.unregistered << "\n";
        total.contexts += c.contexts;
        total.wrong_delimiter += c.wrong_delimiter;
        total.wrong_delimiter_nested += c.wrong_delimiter_nested;
        total.polluted += c.polluted;
        total.unregistered += c.unregistered;
    }
    std::cout << "  " << total.label << std::string(std::max<std::size_t>(1, 22 - total.label.size()), ' ')
              << total.contexts << "  " << total.wrong_delimiter << "  " << total.wrong_delimiter_nested
              << "  " << total.polluted << "  " << total.unregistered << "\n";
    std::cout << "  spec.md Baseline for comparison: total contexts wrong=335 (232 nested) "
                 "polluted=52 unregistered=30\n";
}

}  // namespace

// ============================================================================
// SC-005/FR-017/W-7: FIX50SP2's registered-group count. spec.md's Baseline
// table: 502 measured -> 505 target (matching the code generator's emitted
// count), delta accounted for by three named groups (tags 1499, 1669, 1919)
// that resolve no delimiter today and so never register.
//
// The codegen cross-check itself (comparing against
// `tools/codegen/fixpp-codegen/ir.cpp`'s emitted count) is T043's job, not
// T006's — this case asserts against the ENGINE's own registered-group
// count instead, per the task brief ("If reaching the codegen's count from
// this test is not cleanly available, assert against the engine's
// registered-group count"). "Registered" here means the bare/global store
// (`table_view::group_first_field(no_tag)`) resolves non-zero for a no_tag —
// this is dictionary-wide (not per-context), matching what the 502/505
// figures in spec.md/contracts/group_ctx_delims.md count.
// ============================================================================
TEST(DelimiterCensus, RegisteredGroupCountMatchesCodegenFix50Sp2) {
    DictCase const dc{.label = "FIX50SP2", .filename = "FIX50SP2.xml", .is_orchestra = false};

    auto storage = std::make_unique<std::byte[]>(kArenaBytes);
    std::pmr::monotonic_buffer_resource mr{storage.get(), kArenaBytes};

    auto const oracle = build_oracle(dc);
    auto const dict = load_actual(dc, &mr);
    auto const tv = dict.as_table_view();

    std::set<std::uint16_t> distinct_no_tags;
    for (auto const& [key, members] : oracle.group_members) {
        (void)members;
        distinct_no_tags.insert(key.no_tag);
    }

    std::size_t registered = 0;
    std::vector<std::uint16_t> unresolved_no_tags;
    for (auto no_tag : distinct_no_tags) {
        if (tv.group_first_field(no_tag) != 0) {
            ++registered;
        } else {
            unresolved_no_tags.push_back(no_tag);
        }
    }

    std::cout << "\n=== 083 T006 W-7: FIX50SP2 registered-group count ===\n";
    std::cout << "  distinct group no_tags declared (oracle): " << distinct_no_tags.size() << "\n";
    std::cout << "  registered (bare group_first_field != 0): " << registered << "\n";
    std::cout << "  unresolved no_tags: ";
    for (auto t : unresolved_no_tags) {
        std::cout << t << " ";
    }
    std::cout << "\n  spec.md target: 502 measured -> 505 (codegen cross-check deferred to T043)\n";

    EXPECT_EQ(registered, 505u)
        << "FIX50SP2 registered-group count did not reach the post-fix target of 505 "
           "(spec.md SC-005/FR-017/W-7) — today's measured value is 502; the three groups "
           "named in spec.md (count tags 1499, 1669, 1919) resolve no delimiter and never "
           "register. Full codegen cross-check is T043's job.";
}

// ============================================================================
// SC-008: FIX40, FIX41 and FIXT11 have ZERO affected contexts before AND
// after this feature — the fix must not introduce churn where there was no
// defect. FIX42 is deliberately NOT in this set: it changes (8 wrong
// delimiter / 4 polluted, per spec.md's Baseline table) — do not add it.
// ============================================================================
TEST(DelimiterCensus, NoChangeDictionariesUnchanged) {
    std::vector<DictCase> const no_change_dicts{
        {.label = "FIX40", .filename = "FIX40.xml", .is_orchestra = false},
        {.label = "FIX41", .filename = "FIX41.xml", .is_orchestra = false},
        {.label = "FIXT11", .filename = "FIXT11.xml", .is_orchestra = false},
    };

    std::cout << "\n=== 083 T006 SC-008: no-change dictionaries (FIX40/FIX41/FIXT11 only) ===\n";
    for (auto const& dc : no_change_dicts) {
        auto const c = census_one(dc);
        std::cout << "  " << c.label << ": contexts=" << c.contexts << " wrong=" << c.wrong_delimiter
                  << " polluted=" << c.polluted << " unregistered=" << c.unregistered << "\n";
        EXPECT_EQ(c.wrong_delimiter, 0u) << c.label << ": SC-008 violated — wrong-delimiter count changed";
        EXPECT_EQ(c.polluted, 0u) << c.label << ": SC-008 violated — polluted-member-set count changed";
        EXPECT_EQ(c.unregistered, 0u) << c.label << ": SC-008 violated — unregistered-context count changed";
    }
}

// ============================================================================
// SC-015: the all-ten sweep. Asserts the POST-FIX target (every bucket
// zero) for every one of the ten dictionaries, with NO carve-out, exclusion
// list or per-dictionary exemption (FR-012/FR-016). This is expected to be
// RED today — spec.md's Baseline table measures 335 wrong-delimiter, 52
// polluted, 30 unregistered contexts on the current tree. The complete
// per-dictionary table and totals are always printed (uncapped counts;
// per-context DETAIL lines capped at kMaxDetailLinesPerDict) so the RED
// observation carries the reconciliation numbers T012 needs — a RED
// observation with no numbers is useless.
// ============================================================================
TEST(DelimiterCensus, RedCountsReconcileWithSpecBaseline) {
    std::vector<DictCensus> all;
    all.reserve(kAllDicts.size());
    for (auto const& dc : kAllDicts) {
        all.push_back(census_one(dc));
    }

    print_census_table(all);

    for (auto const& c : all) {
        EXPECT_EQ(c.wrong_delimiter, 0u) << c.label << ": " << c.wrong_delimiter
                                          << " context(s) with a wrong delimiter (post-fix target: 0, "
                                             "FR-001/FR-002)";
        EXPECT_EQ(c.polluted, 0u) << c.label << ": " << c.polluted
                                   << " context(s) with a polluted member set (post-fix target: 0, "
                                      "FR-010/FR-015)";
        EXPECT_EQ(c.unregistered, 0u) << c.label << ": " << c.unregistered
                                       << " unregistered context(s) (post-fix target: 0, FR-006/FR-023)";
    }
}
