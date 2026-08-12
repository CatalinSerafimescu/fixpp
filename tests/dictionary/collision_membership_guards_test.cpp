// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/collision_membership_guards_test.cpp — 063 T017 [US1]
//
// Per-collision discriminating regression guards (SC-002 / FR-002), driven
// DIRECTLY by the T016 census's own membership computation
// (reused_tag_census.hpp — shared with reused_tag_census_test.cpp, not a
// second hand-rolled copy that could silently drift). ONE parameterized
// `TEST_P` sweeps EVERY colliding `no_tag` the census finds across ALL NINE
// runtime dict XMLs — not a hand-picked subset of representatives. Each case
// asserts the DIFFERENCE between two real variants — a tag present in one
// context's member set and absent from the other's — via
// `Dictionary::as_table_view()`'s context-scoped
// `group_member_tags(msg_type, parent_path, no_tag)` (real Dictionary, real
// XML; no codegen dependency, matching tests/dictionary/'s existing
// no-codegen convention — see defect_a_group_context_test.cpp).
//
// 082-structural-group-detection T030 — the FIX40/41/42 carve-out below is
// DISCHARGED. It previously read:
//
//   "FIX40.xml / FIX41.xml / FIX42.xml contribute ZERO cases: the T016 census
//    finds ZERO registered group contexts for all three (their group count
//    fields are declared type INT, not NUMINGROUP) ... writing a passing
//    'membership' test against that state would ENSHRINE the gap."
//
// That REGISTRATION GAP was the L-063-1 / L-066-1 limitation, and 082 closed
// it: `as_table_view()` now detects groups structurally
// (`group_first_field(tag) != 0`) instead of by datatype, so all three
// register their real groups and their genuine reused-tag collisions are now
// guarded like every other dictionary's — FIX40 tag 73, FIX41 tag 73, FIX42
// tags {73, 78, 146, 268, 295, 296, 420}. Total 69 -> 78.
//
// FIXT.1.1 still contributes zero cases, but for an unrelated and unchanged
// reason: NoHops(627) is declared twice with IDENTICAL membership (benign
// reuse, not a collision); pinned separately below. Do not conflate the two —
// one was a defect that has been fixed, the other is a property of the
// dictionary.

#include <gtest/gtest.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <map>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "required_scope_oracle.hpp"  // 082 T008: independent group-tag census
#include "reused_tag_census.hpp"

namespace {

using fixpp::dict::Dictionary;
using fixpp_test_support::census_for;
using fixpp_test_support::kRuntimeDicts;
using fixpp_test_support::Variant;

bool contains(std::span<std::uint16_t const> members, std::uint16_t tag) {
    return std::find(members.begin(), members.end(), tag) != members.end();
}

Dictionary load(std::filesystem::path const& path, std::pmr::memory_resource* mr) {
    return fixpp::dict::XmlLoader{}.load(path, mr);
}

// First tag in `a` that is NOT in `b` (a/b both sorted), or nullopt if `a`
// is a subset of `b`.
// The `exclude` parameter (082 T030, issue #210) is REMOVED — 2026-08-12.
//
// It used to skip the GLOBAL first-seen delimiter, which under #210 was injected
// by `set_group_first_ctx` into EVERY context of a no_tag regardless of what the
// XML declared there, making it a tag that is present by construction and so
// cannot discriminate (the FIX42 tag-146 symptom: 46 RelatdSym, News(B)'s first
// child, leaking into QuoteRequest(R)'s context).
//
// Its own comment predicted the retirement: *"when #210 lands the exclusion
// becomes a no-op, since the delimiter will only be a member where it is
// declared"*. #210 is closed (083 T031/T032 — the delimiter SOURCE is now each
// context's own declaration), so the parameter is gone.
//
// ⚠️ Unlike the sibling revert in `required_scope_census_test.cpp`, dropping this
// parameter WIDENS the discriminator candidate set, so a green here is NOT proof
// on its own. The acceptance is the prediction itself — that removal is
// BEHAVIOUR-NEUTRAL — and it is checked against the derived case set, which is
// observable as the parameterised test-name list: **78 cases, per dict
// FIX40 1 / FIX41 1 / FIX42 7 / FIX43 9 / FIX44 12 / FIX50 13 / FIX50SP1 14 /
// FIX50SP2 21**, measured immediately before this change. If that list ever
// moves, the exclusion was NOT a no-op: a variant pair that used to differ only
// by the injected delimiter now yields a case (or stops yielding one) via the
// `continue` in `derive_cases_for_dict` below. Re-derive rather than re-baseline.
std::optional<std::uint16_t> first_tag_only_in(std::vector<std::uint16_t> const& a,
                                                std::vector<std::uint16_t> const& b) {
    for (auto const t : a) {
        if (!contains(b, t)) {
            return t;
        }
    }
    return std::nullopt;
}

struct CollisionCase {
    std::string_view dict_file;
    std::uint16_t no_tag;

    std::string msg_type_present;              // context whose variant HAS discriminator_tag
    std::vector<std::uint16_t> path_present;
    std::string msg_type_absent;               // context whose variant LACKS discriminator_tag
    std::vector<std::uint16_t> path_absent;
    std::uint16_t discriminator_tag;
};

// Derives one discriminating CollisionCase per colliding no_tag (a no_tag
// with >=2 distinct member-set variants) found by the T016 census for one
// dict file. Any two variants in `dc.per_tag[no_tag]` are guaranteed to
// differ (census_for only appends a new Variant on an exact member-set
// mismatch — reused_tag_census.hpp), so variants[0] vs variants[1] always
// yields a non-empty symmetric difference.
std::vector<CollisionCase> derive_cases_for_dict(std::string_view fname) {
    std::vector<CollisionCase> cases;

    // Heap-backed arena — FIX50SP2.xml's expanded metadata is large (see the
    // T016 census's table-size report); this derivation is not alloc-gated
    // and the arena is freed once this function returns (only owned
    // std::string/std::vector fields are copied out into CollisionCase).
    constexpr std::size_t kArenaBytes = 32UZ * 1024UZ * 1024UZ;
    auto storage = std::make_unique<std::byte[]>(kArenaBytes);
    std::pmr::monotonic_buffer_resource mr{storage.get(), kArenaBytes};

    auto const path = std::filesystem::path{FIXPP_DICT_DATA_DIR} / std::string{fname};
    auto dict = load(path, &mr);
    auto const oracle = fixpp_test::required_scope_oracle::build_quickfix_oracle(path);
    auto const dc = census_for(dict, std::string{fname}, oracle.group_tags);

    for (auto const& [no_tag, variants] : dc.per_tag) {
        if (variants.size() < 2) {
            continue;  // benign / non-colliding reuse — not a collision
        }
        auto const& a = variants[0];
        auto const& b = variants[1];

        Variant const* present = nullptr;
        Variant const* absent = nullptr;
        std::uint16_t discriminator = 0;

        // 082 T030 / issue #210's global-delimiter exclusion is REMOVED (#210 is
        // closed by 083) — see first_tag_only_in's banner for why, and for the
        // 78-case list that is this removal's acceptance instrument.
        if (auto const disc = first_tag_only_in(a.members, b.members); disc.has_value()) {
            present = &a;
            absent = &b;
            discriminator = *disc;
        } else {
            auto const disc_b = first_tag_only_in(b.members, a.members);
            if (!disc_b.has_value()) {
                // Reachable only if two variants the census called DISTINCT have
                // identical member sets, which `census_for` forbids by
                // construction. Retained as a fail-safe rather than an expected
                // path: pre-#210 this fired when a pair differed only by the
                // injected delimiter. It is skipped LOUDLY via the per-dict count
                // pin, which will disagree with `expected_per_dict` if it ever
                // starts firing on a dictionary where it did not before.
                continue;
            }
            present = &b;
            absent = &a;
            discriminator = *disc_b;
        }

        cases.push_back(CollisionCase{
            fname,
            no_tag,
            present->example_context.msg_type,
            present->example_context.path,
            absent->example_context.msg_type,
            absent->example_context.path,
            discriminator,
        });
    }
    return cases;
}

// Every colliding no_tag across all nine runtime dicts, one discriminating
// case each — computed once (function-local static), consumed by
// INSTANTIATE_TEST_SUITE_P below and by the completeness pin.
std::vector<CollisionCase> const& collision_cases() {
    static std::vector<CollisionCase> const cases = [] {
        std::vector<CollisionCase> all;
        for (auto const& fname : kRuntimeDicts) {
            auto sub = derive_cases_for_dict(fname);
            all.insert(all.end(), sub.begin(), sub.end());
        }
        return all;
    }();
    return cases;
}

class CollisionMembershipGuards : public ::testing::TestWithParam<CollisionCase> {};

}  // namespace

TEST_P(CollisionMembershipGuards, ContextResolvesToTheCorrectVariant) {
    auto const& c = GetParam();

    constexpr std::size_t kArenaBytes = 32UZ * 1024UZ * 1024UZ;
    auto storage = std::make_unique<std::byte[]>(kArenaBytes);
    std::pmr::monotonic_buffer_resource mr{storage.get(), kArenaBytes};

    auto const path = std::filesystem::path{FIXPP_DICT_DATA_DIR} / std::string{c.dict_file};
    auto dict = load(path, &mr);
    auto tv = dict.as_table_view();

    auto const present_members = tv.group_member_tags(c.msg_type_present, c.path_present, c.no_tag);
    auto const absent_members = tv.group_member_tags(c.msg_type_absent, c.path_absent, c.no_tag);

    // Sanity: both contexts must resolve to a REAL, non-empty member set —
    // otherwise "absent" would trivially hold for a miss, not a genuine
    // context-scoped resolution.
    ASSERT_FALSE(present_members.empty())
        << c.dict_file << " no_tag=" << c.no_tag << " msg_type=" << c.msg_type_present
        << ": expected a non-empty member set";
    ASSERT_FALSE(absent_members.empty())
        << c.dict_file << " no_tag=" << c.no_tag << " msg_type=" << c.msg_type_absent
        << ": expected a non-empty member set";

    // The discriminating assertion: the SAME no_tag resolves to DIFFERENT
    // member sets depending on context — the actual difference, not merely
    // non-empty (per feedback_witness_asserts_named_postcondition_not_proxy
    // shape (b)).
    EXPECT_TRUE(contains(present_members, c.discriminator_tag))
        << c.dict_file << " no_tag=" << c.no_tag << " in msg_type=" << c.msg_type_present
        << " context must include discriminator_tag=" << c.discriminator_tag;
    EXPECT_FALSE(contains(absent_members, c.discriminator_tag))
        << c.dict_file << " no_tag=" << c.no_tag << " in msg_type=" << c.msg_type_absent
        << " context must NOT include discriminator_tag=" << c.discriminator_tag
        << " (that tag belongs to the OTHER declared variant of this reused NumInGroup tag — "
           "resolving it here would reproduce Defect A)";
}

INSTANTIATE_TEST_SUITE_P(PerCensusedCollision, CollisionMembershipGuards,
                        ::testing::ValuesIn(collision_cases()),
                        [](::testing::TestParamInfo<CollisionCase> const& info) {
                            std::string name{info.param.dict_file};
                            name.erase(std::remove(name.begin(), name.end(), '.'), name.end());
                            name += "_tag" + std::to_string(info.param.no_tag);
                            return name;
                        });

// SC-002 completeness pin: the parameterization above must cover EVERY
// colliding no_tag the T016 census finds — an EXACT count, not a lower
// bound, so a future change that silently drops collisions from
// parameterization (or the census itself finding fewer) goes RED here
// rather than merely shrinking the reported test count.
TEST(CollisionMembershipGuards, CoversEveryCensusedCollisionExactly) {
    auto const& cases = collision_cases();

    // 082 T030: FIX40/41/42 now contribute cases. Before 082 they registered
    // ZERO groups (their count tags are XML type='INT', never 'NUMINGROUP'),
    // so the census found no collisions in them at all. The structural
    // predicate makes their real groups visible, and with them their genuine
    // reused-tag collisions: FIX40 tag 73, FIX41 tag 73, FIX42 tags
    // {73, 78, 146, 268, 295, 296, 420}. 69 -> 78.
    std::map<std::string_view, std::size_t> expected_per_dict{
        {"FIX40.xml", 1},  {"FIX41.xml", 1},     {"FIX42.xml", 7},
        {"FIX43.xml", 9},  {"FIX44.xml", 12},    {"FIX50.xml", 13},
        {"FIX50SP1.xml", 14}, {"FIX50SP2.xml", 21},
    };
    std::size_t const expected_total = 1 + 1 + 7 + 9 + 12 + 13 + 14 + 21;

    EXPECT_EQ(cases.size(), expected_total)
        << "expected exactly " << expected_total
        << " discriminating cases (one per colliding no_tag, matching the T016 census's "
           "per-dict counts) — a lower count means collisions were silently dropped from "
           "parameterization (SC-002/FR-002: 'each' collision, not a subset)";

    std::map<std::string_view, std::size_t> actual_per_dict;
    for (auto const& c : cases) {
        ++actual_per_dict[c.dict_file];
    }
    for (auto const& [dict_file, expected_n] : expected_per_dict) {
        EXPECT_EQ(actual_per_dict[dict_file], expected_n)
            << dict_file << ": collision-case count mismatch against the T016 census";
    }
    // 082 T030: only FIXT.1.1 still contributes zero, and for a DIFFERENT
    // reason than FIX40/41/42 used to — benign same-membership reuse, not a
    // registration gap. FIX40/41/42 moved into `expected_per_dict` above.
    //
    // Uses `operator[]` (the tally), NOT `count()`. `std::map::count` returns
    // only 0 or 1, so the previous form could report presence but never
    // magnitude — it would have passed unchanged whether a dictionary
    // contributed 1 case or 70. Asserting the tally is what makes a wrong
    // count fail.
    for (std::string_view const dict_file : {"FIXT11.xml"}) {
        EXPECT_EQ(actual_per_dict[dict_file], 0u)
            << dict_file
            << " must contribute zero collision cases (benign same-membership reuse for FIXT.1.1)";
    }
}

// FIXT.1.1: NoHops(627) is declared twice in FIXT11.xml (session-layer
// header, appears in more than one message context) but with IDENTICAL
// membership both times — benign reuse, NOT a collision (T016 census: 0
// colliding no_tags for FIXT11.xml). This regression pins that the census's
// "benign reuse ≠ collision" classification holds for the session-layer
// dict too, and that context-scoped resolution still works correctly when
// there is nothing to discriminate.
TEST(CollisionMembershipGuards, FixtNoHopsBenignReuseNotACollision) {
    constexpr std::size_t kArenaBytes = 4UZ * 1024UZ * 1024UZ;
    auto storage = std::make_unique<std::byte[]>(kArenaBytes);
    std::pmr::monotonic_buffer_resource mr{storage.get(), kArenaBytes};

    auto const path = std::filesystem::path{FIXPP_DICT_DATA_DIR} / "FIXT11.xml";
    auto dict = load(path, &mr);
    auto tv = dict.as_table_view();

    // Logon ("A") and Logout ("5") both carry NoHops(627) in FIXT.1.1's
    // header (see dictionaries/FIXT11.xml's two `<group name='NoHops'>`
    // sites); both resolve to the SAME member set (HopCompID/HopSendingTime
    // /HopRefID), confirming this reuse is benign.
    auto const logon_members = tv.group_member_tags("A", {}, 627);
    auto const logout_members = tv.group_member_tags("5", {}, 627);

    ASSERT_FALSE(logon_members.empty()) << "NoHops(627) must resolve for Logon (\"A\")";
    ASSERT_FALSE(logout_members.empty()) << "NoHops(627) must resolve for Logout (\"5\")";

    std::vector<std::uint16_t> a{logon_members.begin(), logon_members.end()};
    std::vector<std::uint16_t> b{logout_members.begin(), logout_members.end()};
    std::ranges::sort(a);
    std::ranges::sort(b);
    EXPECT_EQ(a, b) << "NoHops(627) is benign same-membership reuse across FIXT.1.1 messages — "
                       "must NOT differ (that would make it a real collision, contradicting the "
                       "T016 census's 0-collision finding for FIXT11.xml)";
}
