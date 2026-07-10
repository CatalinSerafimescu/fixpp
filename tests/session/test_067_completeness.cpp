// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/session/test_067_completeness.cpp
//
// 067-codegen-writer-emitter T011 [US1] [TESTS-FIRST] — exact-set
// completeness gate (contracts/generated-builder.md G4/FR-004): the
// generated MsgType->builder REGISTRY keys (MsgType WIRE STRINGS, multi-char
// AF/AC/AG/AS included — NOT `build_<Identifier>` symbol names, a different
// namespace) must equal, set-for-set, the literal 33-element OFFICIAL set
// (research.md R6). Set-equality, NOT subset-presence
// (feedback_completeness_gate_exact_set_not_subset): a missing OR an extra
// key both fail.
//
// MUST FAIL FIRST: references `fixpp::v44::builder_registry`, which does not
// exist until Phase 3b (T016/T017) generates `<fixpp/v44/Builders.hpp>` —
// this translation unit does not compile/link until then (RED).
//
// Registry shape (ESCALATION — no anchor pins an exact symbol/shape; G4 only
// requires "a registry mapping each MsgType string to its builder"; the
// completeness gate itself needs only the KEY SET). This test assumes Phase
// 3b emits, in `fixpp::v44`:
//   struct builder_registry_entry { std::string_view msg_type; };
//   inline constexpr std::array<builder_registry_entry, 33> builder_registry;
// (No function-pointer/type-erased dispatch — the 33 `<Msg>Args` types are
// heterogeneous and nothing in the spec needs runtime dispatch; YAGNI.)
// Flagged to the orchestrator for Phase 3b confirmation.
//
// Anchors: specs/067-codegen-writer-emitter/contracts/generated-builder.md
//          G4; research.md R6; tasks.md T011.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <fixpp/v44/Builders.hpp>  // GENERATED (Phase 3b) — fixpp::v44::builder_registry
#include <set>
#include <string>
#include <string_view>

namespace {

// research.md R6 — the exact 33-element OFFICIAL set. A(13) + M(17) + P(3).
constexpr std::array<std::string_view, 33> kExpectedOfficial33 = {
    "D", "E", "F", "G", "H", "8", "9", "q", "r", "AF", "AC", "t", "u",  // A (13)
    "V", "W", "X", "Y", "c", "d", "e", "f", "g", "h", "i", "b", "S", "R", "AG", "Z", "a",  // M (17)
    "J", "P", "AS",  // P (3)
};

}  // namespace

TEST(Completeness067, ExactSetEqualityOverBuilderRegistryKeys) {
    ASSERT_EQ(kExpectedOfficial33.size(), 33U) << "expected-set literal itself must be 33 elements";

    std::set<std::string> expected(kExpectedOfficial33.begin(), kExpectedOfficial33.end());
    ASSERT_EQ(expected.size(), 33U) << "expected-set literal must be 33 DISTINCT MsgTypes (no dup)";

    std::set<std::string> actual;
    for (auto const& entry : fixpp::v44::builder_registry) {
        actual.insert(std::string{entry.msg_type});
    }

    // Set-equality, not subset: report BOTH a missing key and an extra key
    // explicitly so a partial-implementation regression is diagnosable.
    std::vector<std::string> missing;
    std::set_difference(expected.begin(), expected.end(), actual.begin(), actual.end(),
                         std::back_inserter(missing));
    std::vector<std::string> extra;
    std::set_difference(actual.begin(), actual.end(), expected.begin(), expected.end(),
                         std::back_inserter(extra));

    auto join = [](std::vector<std::string> const& v) {
        std::string s;
        for (auto const& x : v) {
            s += x;
            s += ' ';
        }
        return s;
    };
    EXPECT_TRUE(missing.empty()) << "registry is MISSING MsgTypes: " << join(missing);
    EXPECT_TRUE(extra.empty()) << "registry has EXTRA (non-OFFICIAL) MsgTypes: " << join(extra);
    EXPECT_EQ(actual, expected);
}

// Non-tautological registry-cardinality pin: the registry must carry exactly
// 33 entries (no silent duplicate MsgType key collapsing set size while
// leaving array size wrong, and no accidental duplicate-key entries hiding
// behind a correct SET but wrong COUNT).
TEST(Completeness067, RegistryArrayHasExactly33Entries) {
    EXPECT_EQ(fixpp::v44::builder_registry.size(), 33U);
}

// Multi-char MsgTypes (AF/AC/AG/AS) must be present verbatim, not truncated
// to their first character (a plausible off-by-one in a naive MsgType-string
// emission that only handles single-char codes).
TEST(Completeness067, MultiCharMsgTypesPresentVerbatim) {
    std::set<std::string> actual;
    for (auto const& entry : fixpp::v44::builder_registry) {
        actual.insert(std::string{entry.msg_type});
    }
    for (std::string_view mc : {"AF", "AC", "AG", "AS"}) {
        EXPECT_TRUE(actual.contains(std::string{mc})) << "missing multi-char MsgType: " << mc;
    }
    // Discriminating: the single-char 'A' must NOT be a key (no OFFICIAL
    // v44 message has bare MsgType "A" — a truncation bug would introduce
    // this spuriously, colliding all four multi-char rows onto one key).
    EXPECT_FALSE(actual.contains("A")) << "MsgType 'A' must not appear (truncation of AF/AC/AG/AS)";
}
