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
// 083 T047 (FR-012 / FR-016) — WHAT DOES NOT COUNT AS DELIMITER COVERAGE
//
// `tests/dictionary/collision_membership_guards_test.cpp` is NOT delimiter
// coverage, at any cardinality. Its 69 parameterized cases look like a broad
// per-context census and are not one: their discriminator (`first_tag_only_in`)
// is derived INDEPENDENTLY of the delimiter, and a sibling feature had to add
// an `exclude` parameter specifically so the injected delimiter would not be
// chosen as that discriminator. A suite that must actively avoid the delimiter
// to work cannot also be evidence the delimiter is right.
//
// This case is therefore the ONLY delimiter authority, and it carries no
// carve-out, exclusion list or per-dictionary exemption — asserted below, not
// merely intended. See research.md D-9.
// ============================================================================
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
using fixpp::dict::field_data_type;
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

// C-3.4a's checked set (contracts/group_ctx_delims.md), mirrored against the
// loader's own gate — which post-082 is `find_context_without_delim_record`
// (`src/dictionary/dictionary_internal.hpp`), NOT `dictionary.cpp:445-463` as
// this banner used to cite. That anchor is stale twice over: the line range
// moved, and `as_table_view()` no longer uses this datatype test at all
// (082 re-pointed it onto `group_first_field(t) != 0`). What this mirrors is the
// FR-023 load-time sweep, which still tests `NumInGroup` — see the divergence
// warning at the head of that function.
//
// ⚠️ The datatype test in the body below is CORRECT AND DELIBERATE, not stale
// residue: it is the classifier for the inverted 55-context pin
// (`int_typed_registered`), whose whole job is to identify the INT-typed
// population. Re-pointing it structurally would make the pin self-referential
// and unable to detect a reintroduced datatype gate.
//
// A context is checked iff, on `mt`'s deduped field run, the count tag's
// FieldRef type is NumInGroup AND at least one FieldRef has group_no_tag == no_tag. Three
// outcomes, not two — an INT-typed count tag (L-066-1/#196) and a
// NumInGroup-typed tag with no members in THIS message (dictionary.cpp:463's
// separate "plain scalar reuse" skip) are different exclusion reasons and
// must not be folded together, or the exact-55 tripwire below would silently
// absorb a second population.
enum class CheckedSetStatus : std::uint8_t { kChecked, kNotNumInGroup, kEmptyMembers };

CheckedSetStatus checked_set_status(Dictionary const& dict, GroupContextKey const& key) {
    auto const all_fields = dict.message_fields(key.msg_type);
    bool found_numingroup = false;
    for (auto const& fr : all_fields) {
        if (fr.tag == key.no_tag) {
            found_numingroup = fr.type == field_data_type::NumInGroup;
            break;
        }
    }
    if (!found_numingroup) {
        return CheckedSetStatus::kNotNumInGroup;
    }
    for (auto const& fr : all_fields) {
        if (fr.group_no_tag == key.no_tag) {
            return CheckedSetStatus::kChecked;
        }
    }
    return CheckedSetStatus::kEmptyMembers;
}

// Dictionaries here are large (FIX50SP2 / Orchestra FIX Latest have tens of
// thousands of contexts) — mirrors required_scope_census_test.cpp's arena.
constexpr std::size_t kArenaBytes = 32UZ * 1024UZ * 1024UZ;

// Per-dictionary tally. T012 re-point: `unregistered` is split into
// `unregistered_in_checked_set` (a real FR-023/C-3.4 completeness violation
// — asserted 0) and `int_typed_out_of_checked_set` (L-066-1/#196).
// `empty_members_out_of_checked_set` is a THIRD, distinct exclusion reason
// (dictionary.cpp:463's "plain scalar reuse" skip) — reported, not folded
// into either bucket above, so it can't silently distort the exact-count
// tripwire.
//
// 082 (#196) INVERTED the second bucket. It used to hold exactly 55 contexts
// (FIX40 6 / FIX41 10 / FIX42 38 / FIX43 1) whose count tag is INT-typed and
// which the datatype gate therefore skipped entirely. Detection is now
// structural, so those same 55 contexts REGISTER: the bucket is 0 and the
// population moved to `int_typed_registered`. Both halves are asserted below
// — 0 alone would be satisfied by a census that measured nothing.
//
// `int_typed_registered` is classified by the SAME datatype test as the
// out-of-checked-set bucket (`checked_set_status`, which reads
// `FieldRef::type`). 082 deliberately does not change `FieldRef::type`
// (research.md D-4), so the population stays identifiable and the 55 is a
// stable pin rather than a moving target.
struct DictCensus {
    std::string label;
    std::size_t contexts = 0;
    std::size_t wrong_delimiter = 0;
    std::size_t wrong_delimiter_nested = 0;  // of those, the delimiter is itself a nested group
    // SC-016's UNCONDITIONED population: every oracle context (any
    // registration/correctness status) whose oracle-expected delimiter is
    // itself a child group's count tag. Not conditioned on `wrong_delimiter`
    // (unlike `wrong_delimiter_nested` above).
    std::size_t nested_delim_total = 0;
    std::size_t nested_delim_total_registered = 0;
    std::size_t nested_delim_total_unregistered = 0;
    std::size_t polluted = 0;
    std::size_t unregistered_in_checked_set = 0;
    std::size_t int_typed_out_of_checked_set = 0;
    // 082/#196: same INT-typed-count-tag population as above, but REGISTERED.
    // The two are exhaustive over that population, so a census that stopped
    // measuring drops both to 0 and fails the exact-55 pin below.
    std::size_t int_typed_registered = 0;
    std::size_t empty_members_out_of_checked_set = 0;
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

        // Hoisted above the registration check (was previously only reached
        // for registered contexts) — `nested_delim_total` below needs the
        // oracle's expected delimiter for EVERY context, registered or not.
        // Every group_members[key] insertion in required_scope_oracle.hpp is
        // paired with a group_delims.try_emplace(key, ...) on the same key in
        // the same walker call, so this should never miss; kept as a guarded
        // EXPECT_NE rather than assumed.
        auto const dit = oracle.group_delims.find(key);
        EXPECT_NE(dit, oracle.group_delims.end())
            << dc.label << ": oracle.group_delims missing an entry group_members has (walker bug) — "
               "msg=" << key.msg_type << " no_tag=" << key.no_tag;
        if (dit == oracle.group_delims.end()) {
            continue;  // ASSERT_ is unusable in a non-void function; guard manually.
        }
        std::uint16_t const expected_delim = dit->second;

        // SC-016 UNCONDITIONED nested-delimiter check: is the oracle's
        // expected delimiter itself a group count tag, structurally
        // reachable as a direct child of THIS context? Exact per-context
        // check: does the oracle record a context keyed (msg_type, path +
        // this no_tag, expected_delim)? Computed for every context
        // regardless of wrong/correct/registered status (unlike the
        // `wrong_delimiter_nested` sub-check below, which conditions on
        // `wrong_delim`).
        std::vector<std::uint16_t> child_path = key.path;
        child_path.push_back(key.no_tag);
        GroupContextKey const child_key{key.msg_type, child_path, expected_delim};
        bool const nested_delim = oracle.group_members.contains(child_key);

        auto const ctx_members = tv.group_member_tags(key.msg_type, std::span{key.path}, key.no_tag);
        auto const bare_members = tv.group_member_tags(key.no_tag);
        bool const registered = ctx_members.data() != bare_members.data();

        if (nested_delim) {
            ++census.nested_delim_total;
            if (registered) {
                ++census.nested_delim_total_registered;
            } else {
                ++census.nested_delim_total_unregistered;
            }
        }

        // Hoisted above the registration branch by 082: the INT-typed-count-tag
        // population (L-066-1/#196) must be counted on BOTH sides, because
        // structural detection moved all 55 of them from the unregistered bucket
        // to the registered one. Classified only from `FieldRef::type`, which 082
        // does not change (research.md D-4).
        CheckedSetStatus const status = checked_set_status(dict, key);

        if (registered) {
            if (status == CheckedSetStatus::kNotNumInGroup) {
                ++census.int_typed_registered;
            }
        } else {
            switch (status) {
                case CheckedSetStatus::kChecked:
                    // In C-3.4a's checked set but not registered — a real
                    // FR-023/C-3.4 completeness violation (not #196).
                    ++census.unregistered_in_checked_set;
                    if (detail_printed < kMaxDetailLinesPerDict) {
                        std::cout << "    [unregistered_in_checked_set] " << dc.label
                                  << " msg=" << key.msg_type << " no_tag=" << key.no_tag << "\n";
                        ++detail_printed;
                    }
                    break;
                case CheckedSetStatus::kNotNumInGroup:
                    // L-066-1/#196: the count tag is INT-typed, not
                    // NumInGroup-typed, so the pre-082 datatype gate never
                    // visited it — out of 083's scope. Post-082 this arm is
                    // unreachable on all ten dictionaries (structural detection
                    // registers the whole population); kept because reaching it
                    // is exactly the regression the pin below must attribute.
                    ++census.int_typed_out_of_checked_set;
                    break;
                case CheckedSetStatus::kEmptyMembers:
                    // dictionary.cpp:463's separate "plain scalar reuse"
                    // skip — a distinct exclusion reason, reported only.
                    ++census.empty_members_out_of_checked_set;
                    break;
            }
            continue;
        }

        std::uint16_t const actual_delim =
            tv.group_first_field(key.msg_type, std::span{key.path}, key.no_tag);

        bool const wrong_delim = actual_delim != expected_delim;
        if (wrong_delim) {
            ++census.wrong_delimiter;
            if (nested_delim) {
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
                                         (nested_delim ? "[nested]" : ""))
                                      : "")
                      << (polluted ? " POLLUTED" : "") << "\n";
            ++detail_printed;
        }
    }

    return census;
}

void print_census_table(std::vector<DictCensus> const& all) {
    std::cout << "\n=== 083 T006/T012 delimiter census — all ten dictionaries ===\n";
    std::cout << "  dictionary            contexts  wrong  wrong(nested)  nested_total  polluted  "
                 "unreg_checked  int_typed_oos  int_typed_reg  empty_members_oos\n";
    DictCensus total{.label = "TOTAL"};
    for (auto const& c : all) {
        std::cout << "  " << c.label << std::string(std::max<std::size_t>(1, 22 - c.label.size()), ' ')
                  << c.contexts << "  " << c.wrong_delimiter << "  " << c.wrong_delimiter_nested << "  "
                  << c.nested_delim_total << "  " << c.polluted << "  " << c.unregistered_in_checked_set
                  << "  " << c.int_typed_out_of_checked_set << "  " << c.int_typed_registered << "  "
                  << c.empty_members_out_of_checked_set << "\n";
        total.contexts += c.contexts;
        total.wrong_delimiter += c.wrong_delimiter;
        total.wrong_delimiter_nested += c.wrong_delimiter_nested;
        total.nested_delim_total += c.nested_delim_total;
        total.nested_delim_total_registered += c.nested_delim_total_registered;
        total.nested_delim_total_unregistered += c.nested_delim_total_unregistered;
        total.polluted += c.polluted;
        total.unregistered_in_checked_set += c.unregistered_in_checked_set;
        total.int_typed_out_of_checked_set += c.int_typed_out_of_checked_set;
        total.int_typed_registered += c.int_typed_registered;
        total.empty_members_out_of_checked_set += c.empty_members_out_of_checked_set;
    }
    std::cout << "  " << total.label << std::string(std::max<std::size_t>(1, 22 - total.label.size()), ' ')
              << total.contexts << "  " << total.wrong_delimiter << "  " << total.wrong_delimiter_nested
              << "  " << total.nested_delim_total << "  " << total.polluted << "  "
              << total.unregistered_in_checked_set << "  " << total.int_typed_out_of_checked_set << "  "
              << total.int_typed_registered << "  " << total.empty_members_out_of_checked_set << "\n";
    std::cout << "  nested_delim_total registered/unregistered split: "
              << total.nested_delim_total_registered << " / " << total.nested_delim_total_unregistered
              << " (SC-016 asserts 262 over this UNCONDITIONED population — not asserted here, T012 "
                 "measurement only)\n";
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
    std::cout << "\n  spec.md target: 502 -> 505\n";

    EXPECT_EQ(registered, 505u)
        << "FIX50SP2 registered-group count did not reach the post-fix target of 505 "
           "(spec.md SC-005/FR-017/W-7) — the pre-fix measured value is 502; the three groups "
           "named in spec.md (count tags 1499, 1669, 1919) resolve no delimiter and never "
           "register.";

    // 083 T043: ACCOUNT for the 502 -> 505 delta by naming its three members,
    // rather than resting on the total. A bare count is a weak pin: three
    // groups appearing while three others silently disappeared would leave it
    // green. Asserting the named three resolve makes the delta attributable.
    for (auto const named : {std::uint16_t{1499}, std::uint16_t{1669}, std::uint16_t{1919}}) {
        EXPECT_NE(tv.group_first_field(named), 0)
            << "FR-017 / SC-005: count tag " << named
            << " is one of the three FIX50SP2 groups the pre-fix loader dropped (they resolved no "
               "delimiter and never registered). It must now resolve — this is what accounts for "
               "the 502 -> 505 delta, so the total alone is not the pin.";
    }
    // The complementary direction: nothing was lost to make room. `registered`
    // equals the oracle's distinct declared count, so the set is exact rather
    // than merely the right size.
    EXPECT_EQ(registered, distinct_no_tags.size())
        << "every group the independent oracle declares must be registered — an equal COUNT with "
           "a different SET would pass the assertion above.";
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
                  << " polluted=" << c.polluted
                  << " unregistered_in_checked_set=" << c.unregistered_in_checked_set << "\n";
        EXPECT_EQ(c.wrong_delimiter, 0u) << c.label << ": SC-008 violated — wrong-delimiter count changed";
        EXPECT_EQ(c.polluted, 0u) << c.label << ": SC-008 violated — polluted-member-set count changed";
        // T012 re-point: the `unregistered` leg now checks
        // unregistered_in_checked_set only — L-066-1/#196's INT-typed
        // exclusions land in int_typed_out_of_checked_set instead and must not
        // fail this SC-008 pin. (082 empties that bucket, so the distinction no
        // longer changes this leg's outcome; it is kept because the split is what
        // makes the exact-55 pin below attributable.)
        EXPECT_EQ(c.unregistered_in_checked_set, 0u)
            << c.label << ": SC-008 violated — unregistered-in-checked-set count changed";
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

    // 083 T047 (FR-012 / FR-016) — NO CARVE-OUT, asserted rather than intended.
    // The pin's whole value is that it covers every shipped dictionary with no
    // exclusion list and no per-dictionary exemption; a later "just skip this
    // one" would otherwise be a one-line edit that no test notices.
    ASSERT_EQ(all.size(), 10u)
        << "FR-012 / FR-016: the delimiter census must run over ALL TEN shipped dictionaries "
           "with no carve-out, exclusion list or per-dictionary exemption. Observed "
        << all.size() << ".";
    for (auto const& c : all) {
        EXPECT_GT(c.contexts, 0u)
            << c.label
            << ": censused zero contexts — a dictionary present in the list but contributing "
               "nothing is an exemption by accident, which FR-016 forbids as firmly as an "
               "explicit one.";
    }

    print_census_table(all);

    for (auto const& c : all) {
        EXPECT_EQ(c.wrong_delimiter, 0u) << c.label << ": " << c.wrong_delimiter
                                          << " context(s) with a wrong delimiter (post-fix target: 0, "
                                             "FR-001/FR-002)";
        EXPECT_EQ(c.polluted, 0u) << c.label << ": " << c.polluted
                                   << " context(s) with a polluted member set (post-fix target: 0, "
                                      "FR-010/FR-015)";
        // T012 re-point: this leg checks unregistered_in_checked_set only.
        // int_typed_out_of_checked_set (L-066-1/#196) was out of 083's scope and
        // this pin was written never to require it to be zero. 082 makes it zero
        // anyway; the requirement now lives in the dedicated pin below, which
        // asserts BOTH that emptiness and where the 55 contexts went. Left split
        // here so a future regression is attributed to the right bucket.
        EXPECT_EQ(c.unregistered_in_checked_set, 0u)
            << c.label << ": " << c.unregistered_in_checked_set
            << " unregistered-in-checked-set context(s) (post-fix target: 0, FR-006/FR-023)";
    }
}

// ============================================================================
// T012: the `int_typed_out_of_checked_set` bucket (L-066-1/#196 — count
// tags declared INT rather than NUMINGROUP, which dictionary.cpp:446 skips
// entirely) is intentionally out of 083's scope and MUST NOT gate SC-015
// above. But an un-asserted "just report it" bucket is a gate that observes
// and never fails — the natural hiding place for a future silent drop, since
// anything falling out of the checked set lands here unnoticed. Assert the
// EXACT count instead: a tripwire that fails loudly (rather than silently
// widening) the day #196 lands and these contexts start registering, so
// someone updates this pin deliberately instead of it drifting unnoticed.
//
// If a re-measurement of this pin ever disagrees with the constants below,
// update the constants only after confirming why via a real cause (e.g.
// #196 landing) — never adjust them merely to match whatever the code
// currently produces (that would pin nothing, per
// [[feedback_coverage_push_enshrines_bugs]]-style "assert whatever we
// observed").
//
// ── 082 (#196) DISCHARGED THIS TRIPWIRE, WHICH IS WHY IT INVERTS ────────────
// The confirmed cause the comment above names has now happened: detection is
// structural (`group_first_field(t) != 0`), so an INT-typed count tag is no
// longer skipped and all 55 contexts REGISTER. The out-of-checked-set bucket
// is therefore 0 — and 0 on its own would be a **vacuous** pin, satisfied
// equally by a census that stopped measuring, which is the exact failure mode
// this test was planted to prevent
// ([[feedback_verification_grep_must_be_proven_nonzero_on_the_unfixed_tree]]).
//
// So the 55 is not deleted, it MOVES: the same per-dictionary breakdown
// (6/10/38/1) is now asserted against `int_typed_registered`, classified by
// the same `FieldRef::type` test as before (082 does not change `FieldRef::type`
// — research.md D-4). The two buckets are exhaustive over that population, so
// a dead census fails the second pair rather than passing the first. The pin
// keeps its original job in the new direction: if someone reintroduced a
// datatype gate, all 55 would fall back out and BOTH halves would fail.
// ============================================================================
TEST(DelimiterCensus, IntTypedCountTagContextsAreExactlyFiftyFiveAndNowRegistered) {
    std::vector<DictCensus> all;
    all.reserve(kAllDicts.size());
    for (auto const& dc : kAllDicts) {
        all.push_back(census_one(dc));
    }

    std::size_t total_out = 0;
    std::size_t total_registered = 0;
    for (auto const& c : all) {
        total_out += c.int_typed_out_of_checked_set;
        total_registered += c.int_typed_registered;
        std::size_t expected = 0;
        if (c.label == "FIX40") {
            expected = 6;
        } else if (c.label == "FIX41") {
            expected = 10;
        } else if (c.label == "FIX42") {
            expected = 38;
        } else if (c.label == "FIX43") {
            expected = 1;
        }
        EXPECT_EQ(c.int_typed_out_of_checked_set, 0u)
            << c.label << ": " << c.int_typed_out_of_checked_set
            << " INT-typed count-tag context(s) still OUT of the checked set — 082/#196 makes"
               " detection structural, so this bucket must be empty on every dictionary";
        EXPECT_EQ(c.int_typed_registered, expected)
            << c.label << ": int_typed_registered (L-066-1/#196) drifted from its pinned "
                           "per-dictionary count — this is the SAME population the pre-082 pin held "
                           "as out-of-checked-set, now required to be registered. Update these "
                           "constants deliberately only if the cause is confirmed, never to match an "
                           "unexplained observation; a drop toward 0 means the datatype gate is back";
    }
    EXPECT_EQ(total_out, 0u) << "int_typed_out_of_checked_set must total 0 post-082 — see the "
                                "per-dictionary breakdown above";
    // Non-vacuity for the zeros above: the population still has to be FOUND.
    EXPECT_EQ(total_registered, 55u)
        << "int_typed_registered total drifted from its pinned 55 (L-066-1/#196). 55 is the count "
           "the pre-082 tripwire pinned in the out-of-checked-set bucket; structural detection moves "
           "it here rather than dissolving it, so this is the assertion that keeps the zeros above "
           "from being vacuous";
}
