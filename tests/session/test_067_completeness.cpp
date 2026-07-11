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
// 069-v44-all-families T014 [US2] — GENERALIZED (contracts/coverage-and-
// completeness.md C2; data-model.md Entity "Intended emitted set +
// completeness pin"): the emitter now widens to 83 builders under the
// default `all` coverage mode, so the hardcoded 33-element pin REDs. This
// pin now asserts exact-set equality against the ACTIVE MODE's intended
// set: `official` -> kExpectedOfficial33 (33); `all` -> an independently
// FIX44.xml-censused 83-element set. Mode selection is keyed on a compile
// define, FIXPP_CODEGEN_V44_FAMILIES_OFFICIAL, which is NOT wired by any
// CMake target yet (that is 069 T015/US3, a separate task) — absent, this
// TU defaults to `all` (matching the emitter's own current default; C1).
// When T015 lands the CACHE STRING + `official`-mode CMake target, it MUST
// define FIXPP_CODEGEN_V44_FAMILIES_OFFICIAL on that target so this pin
// tracks the build's actual selected mode instead of silently mismatching.
//
// Anchors: specs/067-codegen-writer-emitter/contracts/generated-builder.md
//          G4; research.md R6; tasks.md T011.
//          specs/069-v44-all-families/contracts/coverage-and-completeness.md
//          C1/C2; data-model.md "Intended emitted set + completeness pin";
//          tasks.md T014.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fixpp/v44/Builders.hpp>  // GENERATED (Phase 3b) — fixpp::v44::builder_registry
#include <pugixml.hpp>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#ifndef FIXPP_DICT_DATA_DIR
#error "FIXPP_DICT_DATA_DIR must be set by CMake target_compile_definitions"
#endif

namespace {

// research.md R6 — the exact 33-element OFFICIAL set. A(13) + M(17) + P(3).
// [[maybe_unused]]: only referenced under the `official`-mode #if branch
// below; unused (with no warning) when this TU defaults to `all` mode.
[[maybe_unused]] constexpr std::array<std::string_view, 33> kExpectedOfficial33 = {
    "D", "E", "F", "G", "H", "8", "9", "q", "r", "AF", "AC", "t", "u",  // A (13)
    "V", "W", "X", "Y", "c", "d", "e", "f", "g", "h", "i", "b", "S", "R", "AG", "Z", "a",  // M (17)
    "J", "P", "AS",  // P (3)
};

// data-model.md "N-002/N-003 exclusion set" — the members PRESENT in the
// vendored FIX44.xml (BW/BX/BY are FIX 5.0-only, absent here).
constexpr std::array<std::string_view, 2> kN002N003PresentInFix44 = {"BE", "BF"};

// 069 T014 (contracts/coverage-and-completeness.md C2 "Non-circular expected
// set"): an INDEPENDENT raw FIX44.xml census of `<message msgcat='app'>`
// MsgTypes, minus the present N-002/N-003 exclusions. Deliberately does NOT
// touch VersionIR/build_ir/MessageIR.is_application — the emitter's own IR —
// so a mis-parsed/defaulted msgcat cannot drop from both sides and pass
// vacuously. Mirrors the N3-census raw-pugixml precedent in
// tests/codegen/test_067_emit_builders_unit.cpp (lines 36/221).
std::set<std::string> census_all_mode_expected_set() {
    auto const path = std::filesystem::path{FIXPP_DICT_DATA_DIR} / "FIX44.xml";

    pugi::xml_document doc;
    pugi::xml_parse_result const result = doc.load_file(path.c_str());
    if (!result) {
        ADD_FAILURE() << "failed to parse " << path << ": " << result.description();
        return {};
    }

    std::set<std::string> app_messages;
    for (auto const& msg : doc.child("fix").child("messages").children("message")) {
        std::string_view const msgcat = msg.attribute("msgcat").as_string("");
        if (msgcat == "app") {
            app_messages.insert(std::string{msg.attribute("msgtype").as_string("")});
        }
    }

    for (std::string_view excluded : kN002N003PresentInFix44) {
        app_messages.erase(std::string{excluded});
    }
    return app_messages;
}

std::set<std::string> const& all_mode_expected_set() {
    static std::set<std::string> const expected = [] {
        auto s = census_all_mode_expected_set();
        // Self-asserting cardinality (data-model.md / C2): the vendored
        // FIX44.xml MUST census to exactly 83 app-msgcat messages minus
        // {BE, BF}. A census-code bug (wrong node path, wrong attribute)
        // must not silently produce a different-but-plausible count.
        EXPECT_EQ(s.size(), 83U)
            << "independent FIX44.xml app-message census (minus BE/BF) must be 83; "
               "got "
            << s.size() << " — census logic itself may be broken (non-circularity check)";
        return s;
    }();
    return expected;
}

// 069 T014 mode-selection: keyed on a compile define that no CMake target
// currently sets (T015/US3 wires it for an `official`-mode build). Absent,
// this TU defaults to `all`, matching the emitter's own current default.
#if defined(FIXPP_CODEGEN_V44_FAMILIES_OFFICIAL)
std::set<std::string> intended_set() {
    return std::set<std::string>(kExpectedOfficial33.begin(), kExpectedOfficial33.end());
}
constexpr std::size_t kExpectedCount = 33U;
#else
std::set<std::string> intended_set() { return all_mode_expected_set(); }
constexpr std::size_t kExpectedCount = 83U;
#endif

}  // namespace

TEST(Completeness067, ExactSetEqualityOverBuilderRegistryKeys) {
    std::set<std::string> const expected = intended_set();
    ASSERT_EQ(expected.size(), kExpectedCount)
        << "intended-set itself must be " << kExpectedCount << " elements";

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
    EXPECT_TRUE(extra.empty()) << "registry has EXTRA (non-intended) MsgTypes: " << join(extra);
    EXPECT_EQ(actual, expected);
}

// Non-tautological registry-cardinality pin: the registry must carry exactly
// the mode's intended count of entries (no silent duplicate MsgType key
// collapsing set size while leaving array size wrong, and no accidental
// duplicate-key entries hiding behind a correct SET but wrong COUNT).
TEST(Completeness067, RegistryArrayHasExactlyIntendedCountEntries) {
    EXPECT_EQ(fixpp::v44::builder_registry.size(), kExpectedCount);
}

// Multi-char MsgTypes (AF/AC/AG/AS) must be present verbatim, not truncated
// to their first character (a plausible off-by-one in a naive MsgType-string
// emission that only handles single-char codes). These 4 are all OFFICIAL,
// so present under BOTH modes — mode-independent.
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
