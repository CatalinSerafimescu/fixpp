// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/codegen/test_082_v42_group_classes_test.cpp
//
// 082-structural-group-detection T019/T020 (tasks.md, RED pins written
// BEFORE T023 per the RED-first ordering rule) — contracts/group-detection.md
// K5 / SC-004, US1 AC4.
//
// Pure TEXT parse of the SHIPPED generated header
// (build/<preset>/_codegen/include/fixpp/v42/Messages.hpp) — no fixpp header
// is #included by this file and no runtime Dictionary/table_view is
// consulted. Mirrors vlatest_manifest_class_consistency_test.cpp's
// CLASS-SIDE EXTRACTION RULE exactly (same regex shapes, confirmed against
// the real v44 generated header during authoring):
//   - a MESSAGE class: `^class <Name> {$` (0-indent) body.
//   - a GROUP flyweight class: `^    class G_<N> {$` (4-indent), in
//     `namespace fixpp::v42::groups`.
//   - a nested-group REFERENCE (message body or another group body) uses
//     `group_view<[::fixpp::v42::groups::]G_<N>>`.
//
// T019: the regenerated v42/Messages.hpp carries exactly 18 `class G_`
// flyweights and keeps its 46 message classes. TODAY (pre-T023) v42 has 0
// `class G_` lines and 46 message-class bodies (92 raw `^class ` lines: 46
// forward-declared `owning_<Name>;` + 46 body definitions
// `<Name> {` -- confirmed by direct inspection of the pre-change generated
// tree, scratch-snapshotted at T001) -- so this pin is RED until T023+T024-
// T028 land (the predicate swap + VersionIR::group_tags plumbing +
// regeneration).
//
// T020: the MassQuote NoQuoteSets(296) -> NoQuoteEntries(295) nesting is
// expressed as a NESTED typed group (G_296's body references
// `group_view<G_295>`), not flattened onto MassQuote's own message class
// (US1 AC4). TODAY neither G_296 nor G_295 exist at all (0 group flyweights
// emitted for v42), so this is RED for the same underlying reason as T019 --
// but the assertion is written to describe the POST-regeneration nested
// shape precisely (distinguishing "nested" from "flattened"), not merely
// "some group class exists".
//
// Anchors: tasks.md T019/T020; contracts/group-detection.md K5, C4.3;
// spec.md SC-004, US1 AC4; vlatest_manifest_class_consistency_test.cpp
// (class-side extraction precedent).

#include <gtest/gtest.h>

#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef FIXPP_CODEGEN_V42_MESSAGES_HPP
#error "FIXPP_CODEGEN_V42_MESSAGES_HPP must be set by CMake target_compile_definitions"
#endif

namespace {

std::string read_file(std::string const& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open: " + path);
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::vector<std::string> split_lines(std::string const& text) {
    std::vector<std::string> lines;
    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line)) {
        lines.push_back(line);
    }
    return lines;
}

// Mirrors vlatest_manifest_class_consistency_test.cpp's kMsgClassStart /
// kGrpClassStart regexes exactly (0-indent message class, 4-indent group
// flyweight class).
std::regex const kMsgClassStart(R"(^class (\w+) \{$)");
std::regex const kGrpClassStart(R"(^    class G_(\d+) \{$)");
std::regex const kGrpRefRe(R"(group_view<(?:::fixpp::v42::groups::)?G_(\d+)>)");

struct ClassSide {
    std::set<std::string> message_names;       // 0-indent class names
    std::set<int> group_ids;                   // 4-indent G_<N> flyweight ids
    std::map<std::string, std::string> message_body;  // message class name -> body text
    std::map<int, std::string> group_body;             // G_<N> -> body text
};

ClassSide parse_v42_messages_hpp(std::string const& path) {
    std::vector<std::string> const lines = split_lines(read_file(path));
    ClassSide out;

    std::size_t const n = lines.size();
    for (std::size_t i = 0; i < n; ++i) {
        std::smatch m;
        if (std::regex_match(lines[i], m, kGrpClassStart)) {
            int const gid = std::stoi(m[1].str());
            out.group_ids.insert(gid);
            std::size_t j = i + 1;
            std::string body;
            while (j < n && lines[j] != "    };") {
                body += lines[j];
                body += '\n';
                ++j;
            }
            if (j >= n) {
                throw std::runtime_error("unterminated group class G_" + std::to_string(gid));
            }
            out.group_body[gid] = std::move(body);
            i = j;
        } else if (std::regex_match(lines[i], m, kMsgClassStart)) {
            std::string const name = m[1].str();
            out.message_names.insert(name);
            std::size_t j = i + 1;
            std::string body;
            while (j < n && lines[j] != "};") {
                body += lines[j];
                body += '\n';
                ++j;
            }
            if (j >= n) {
                throw std::runtime_error("unterminated message class " + name);
            }
            out.message_body[name] = std::move(body);
            i = j;
        }
    }
    return out;
}

}  // namespace

// T019 [US1]: exactly 18 `class G_` group flyweights, 46 message classes.
// K5 / SC-004. RED until T023-T028 land (today: 0 group flyweights, 46
// message classes already -- the message-class count is NOT expected to
// move, only the group-flyweight count).
TEST(V42GroupClasses, EighteenGroupFlyweightsFortySixMessageClasses) {
    ClassSide const cs = parse_v42_messages_hpp(FIXPP_CODEGEN_V42_MESSAGES_HPP);

    EXPECT_EQ(cs.message_names.size(), 46u)
        << "v42/Messages.hpp message-class count drifted from the pinned 46 -- "
           "this pin is NOT expected to move across the 082 predicate swap "
           "(FR-016 byte-identity elsewhere; only the group-flyweight count "
           "should change here)";

    EXPECT_EQ(cs.group_ids.size(), 18u)
        << "v42/Messages.hpp `class G_` flyweight count -- expected 18 "
           "(contracts/group-detection.md K5 / C2's FIX42 struct-set), got "
           << cs.group_ids.size()
           << ". RED until T023 (predicate swap) + T024/T025 (VersionIR::"
              "group_tags plumbing) + T026 (regeneration) land.";
}

// T020 [US1]: MassQuote's NoQuoteSets(296) -> NoQuoteEntries(295) nesting is
// a NESTED typed group (G_296's body references group_view<G_295>), not
// flattened onto MassQuote's own message class. US1 AC4.
//
// Distinguishes "nested" from "flattened": a flattening regression would
// instead show group_view<G_295> referenced DIRECTLY from MassQuote's own
// message-class body (never from inside G_296), OR show G_295 entirely
// absent while G_296 exists with 295's members inlined into it. This test
// asserts the NESTED shape specifically -- the mere existence of G_296 or
// G_295 is not sufficient (that is T019's job).
TEST(V42GroupClasses, MassQuoteNoQuoteSetsNestsNoQuoteEntriesNotFlattened) {
    ClassSide const cs = parse_v42_messages_hpp(FIXPP_CODEGEN_V42_MESSAGES_HPP);

    ASSERT_TRUE(cs.message_names.contains("MassQuote"))
        << "v42/Messages.hpp has no MassQuote message class -- fixture/regen regression, "
           "independent of the 082 predicate swap";

    // Both flyweights must exist (T019's own assertion covers the aggregate
    // count; this ASSERT_TRUE pair pins the TWO SPECIFIC ids this test
    // depends on, so a failure here reports precisely which is missing
    // rather than falling through to a confusing downstream crash).
    ASSERT_TRUE(cs.group_ids.contains(296))
        << "v42/Messages.hpp has no G_296 (NoQuoteSets) flyweight -- RED until T023-T028 land";
    ASSERT_TRUE(cs.group_ids.contains(295))
        << "v42/Messages.hpp has no G_295 (NoQuoteEntries) flyweight -- RED until T023-T028 land";

    // NESTED shape: G_296's own body must reference group_view<G_295>.
    // There may be multiple group_view<G_N> refs inside G_296 (296 carries other
    // nested/typed members too, per v44's precedent) -- scan all matches for a
    // 295 hit. Same flag-loop shape as `mq_directly_refs_295` below, so the two
    // halves of this test read identically.
    //
    // (/simplify: this was `regex_search(...) && [lambda]()`. The lambda's
    // sregex_iterator already yields false when there is no match, so the
    // regex_search conjunct could never change the result and its `std::smatch`
    // existed only to feed it.)
    std::string const& g296_body = cs.group_body.at(296);
    bool g296_refs_295 = false;
    for (auto it = std::sregex_iterator(g296_body.begin(), g296_body.end(), kGrpRefRe);
         it != std::sregex_iterator(); ++it) {
        if (std::stoi((*it)[1].str()) == 295) {
            g296_refs_295 = true;
            break;
        }
    }
    EXPECT_TRUE(g296_refs_295)
        << "G_296's body does not reference group_view<G_295> -- NoQuoteEntries(295) is not "
           "expressed as a NESTED group under NoQuoteSets(296)";

    // NOT flattened: MassQuote's own message-class body must NOT reference
    // group_view<G_295> directly (that would mean 295 was hoisted to
    // top-level instead of staying nested under 296).
    std::string const& mq_body = cs.message_body.at("MassQuote");
    bool mq_directly_refs_295 = false;
    for (auto it = std::sregex_iterator(mq_body.begin(), mq_body.end(), kGrpRefRe);
         it != std::sregex_iterator(); ++it) {
        if (std::stoi((*it)[1].str()) == 295) {
            mq_directly_refs_295 = true;
            break;
        }
    }
    EXPECT_FALSE(mq_directly_refs_295)
        << "MassQuote's own message-class body references group_view<G_295> directly -- "
           "NoQuoteEntries(295) was FLATTENED to top-level instead of staying nested under "
           "NoQuoteSets(296) (US1 AC4 violation)";
}
