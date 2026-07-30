// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/session/test_082_group_required_member_validation_test.cpp
//
// 082-structural-group-detection T021b [US1] RED pin (written BEFORE T023
// per the RED-first ordering rule) -- contracts/group-detection.md C4.2,
// FR-006 / FR-006b, SC-008a (second leg).
//
// SC-008a's SECOND leg: with `validate_inbound_messages` ON, the
// newly-reachable group-REQUIRED-MEMBER rejections
// (`fixpp::wire::dictionary_driven_validator::consume_group`'s per-instance
// required-member bitmap check, validator.hpp:308-399) are EXACTLY the set
// derivable from the dictionary's `required='Y'` group members -- enumerated
// and pinned, not merely observed.
//
// ── Derivation (NOT transcribed) ─────────────────────────────────────────
// The 14-pair set is read DIRECTLY off T005's shared oracle
// (`required_scope_oracle.hpp`'s `DictOracle::group_required`): every
// `GroupContextKey` whose `oracle.group_required[key]` is non-empty --
// 13 TOP-LEVEL contexts (`key.path == {}`) plus exactly ONE NESTED context
// (`key.path == {296}`, MassQuote's NoQuoteEntries(295) inside the required
// NoQuoteSets(296)) -- matching contracts/group-detection.md K9's own count
// ("13 are top-level omissions; the 14th ... nested inside the required
// NoQuoteSets(296)"), reconciled independently here for the SESSION-level
// validator's own nested-descent mechanism (`consume_group`'s recursive
// branch, validator.hpp:374-387) rather than the BUILDER tier's
// `gc.validate_entry` (T034/US2 -- a DIFFERENT enforcement mechanism over
// the SAME oracle-derived set). No hand-transcribed pair list exists in this
// file; the test iterates `oracle.group_required` directly and ASSERTS the
// resulting count is 14 (13 top-level + 1 nested) as a drift guard, not as
// the derivation itself.
//
// The set's *members* (msg_type/no_tag/required-tag values) are oracle-
// derived; the group's DELIMITER tag (needed only to construct a
// well-formed wire INSTANCE -- never asserted as an expected value) is
// looked up from a small `kDelimTags` table, cross-checked against the
// vendored dictionaries/FIX42.xml directly (grep line refs in the table
// itself) -- the same status as any other test's hardcoded wire bytes (cf.
// tests/support/fix44_group_frame_bodies.hpp's literal tag numbers). A
// missing table entry throws at construction time (loud failure, not a
// silent skip), so a change to the oracle's required-member set cannot
// silently narrow the case list.
//
// ── Why the nested case (14th) needs a DIFFERENT construction ────────────
// `consume_group`'s nested-descent branch (validator.hpp:374-387) PROPAGATES
// a nested failure IMMEDIATELY (`if (!nested) return nested;`) -- it never
// reaches the enclosing group's OWN required-member completeness check.
// So testing "NoQuoteEntries(295)'s OWN required member 299 (QuoteEntryID)
// is missing FROM one of its instances" requires 296 to be a COMPLETE,
// valid top-level instance (delimiter + all its own required members,
// INCLUDING a present-but-incomplete 295 sub-instance) -- omitting any of
// 296's OTHER required members here would produce the WRONG rejection
// reason (296's own check firing before descent into 295 even happens).
// Conversely, 296's OWN required-member omission (testing that 295's mere
// ABSENCE as a member of 296 is caught) is ALREADY one of the 13 top-level
// cases (omitted_tag == 295, case (i, {}, 296)) -- a DIFFERENT construction
// from the nested case, testing a DIFFERENT failure site.
//
// ── Generic frame construction (not per-message hand transcription) ──────
// Message-level required-field completeness (validate() Step 2, which runs
// BEFORE Step 3's group check and must NOT be what fails a case) is filled
// GENERICALLY from `tv.required_fields(msg_type)` + `tv.field_type_of(tag)`
// + `tv.enum_valid(tag, ...)` probing (`pick_filler_value`), not from 12
// hand-read message schemas -- see that helper's comment. A required field
// that is itself ANOTHER group's no_tag is satisfied with `tag=0` (a
// present-but-empty group is structurally valid: `consume_group` returns
// immediately for `declared_count == 0`, validator.hpp:283-285).
//
// ── Both directions ────────────────────────────────────────────────────
// Positive: all 14 cases' "omit the chosen required member" frame must
// reject (RED today). Baseline: all 14 cases' "nothing omitted" frame must
// validate CLEAN today (a discriminator -- if a baseline cell does not pass
// today, that cell's RED tells us nothing; per-case ASSERT, not EXPECT).
// Negative control: Allocation(J)'s NoAllocs(78) -- one of FIX42's 18 group
// tags whose top-level required-member set is EMPTY (not one of the 14) --
// must NOT reject on an instance missing (i.e., never declaring) a
// non-required member, both today AND (by the mechanism's own logic, since
// `check_required` gates on a non-empty `req_members`) after T023.
//
// Anchors: tasks.md T021b; contracts/group-detection.md C4.2, K9;
// spec.md FR-006/FR-006b, SC-008a; include/fixpp/wire/validator.hpp
// (`consume_group`, `validate_group_level`); tests/dictionary/
// required_scope_oracle.hpp (T005's shared oracle, reused verbatim -- no
// forked walker).

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <memory_resource>
#include <set>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <fixpp/core/error.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fixpp/wire/framer.hpp>
#include <fixpp/wire/parser.hpp>
#include <fixpp/wire/validator.hpp>

#include "dictionary/required_scope_oracle.hpp"  // T005's shared oracle -- no forked walker
#include "support/app_message_read_scaffold.hpp"  // fixpp_test_support::make_frame

namespace fixpp::session::test082 {
namespace {

using fixpp::wire::access_mode;
using fixpp::wire::Framer;
using fixpp::wire::frame_view;
using fixpp::wire::MessageView;
using fixpp::wire::Parser;
using fixpp::wire::pmr_carry_buffer;
using fixpp_test::required_scope_oracle::build_quickfix_oracle;
using fixpp_test::required_scope_oracle::DictOracle;
using fixpp_test::required_scope_oracle::GroupContextKey;

// ── Delimiter-tag lookup (input-construction data, NOT an asserted value) ──
// Verified directly against dictionaries/FIX42.xml (the group's FIRST
// resolvable field/group child, matching xml_loader.cpp's
// `first_field_tag` resolution): each `<group name=X>` element's own first
// child, PER OCCURRENCE (a no_tag can have a DIFFERENT delimiter in
// different messages -- e.g. NoMDEntries(268) opens with 269 in
// MarketDataSnapshotFullRefresh(W) but 279 in
// MarketDataIncrementalRefresh(X); confirmed by direct inspection).
std::map<GroupContextKey, std::uint16_t> const kDelimTags{
    {{"B", {}, 33}, 58},    // News: LinesOfText -> Text(58)
    {{"C", {}, 33}, 58},    // Email: LinesOfText -> Text(58)
    {{"E", {}, 73}, 11},    // NewOrderList: NoOrders -> ClOrdID(11)
    {{"N", {}, 73}, 11},    // ListStatus: NoOrders -> ClOrdID(11)
    {{"R", {}, 146}, 55},   // QuoteRequest: NoRelatedSym -> Symbol(55)
    {{"V", {}, 146}, 55},   // MarketDataRequest: NoRelatedSym -> Symbol(55)
    {{"V", {}, 267}, 269},  // MarketDataRequest: NoMDEntryTypes -> MDEntryType(269)
    {{"W", {}, 268}, 269},  // MarketDataSnapshotFullRefresh: NoMDEntries -> MDEntryType(269)
    {{"X", {}, 268}, 279},  // MarketDataIncrementalRefresh: NoMDEntries -> MDUpdateAction(279)
    {{"Z", {}, 295}, 55},   // QuoteCancel: NoQuoteEntries -> Symbol(55)
    {{"i", {}, 296}, 302},  // MassQuote: NoQuoteSets -> QuoteSetID(302)
    {{"l", {}, 420}, 12},   // BidResponse: NoBidComponents -> Commission(12)
    {{"m", {}, 428}, 55},   // ListStrikePrice: NoStrikes -> Symbol(55)
    {{"i", {296}, 295}, 299},  // MassQuote: NoQuoteSets->NoQuoteEntries -> QuoteEntryID(299)
    {{"J", {}, 78}, 79},    // Allocation: NoAllocs -> AllocAccount(79) (NEGATIVE CONTROL)
};

std::vector<std::uint16_t> const kHeaderTags{8, 9, 10, 34, 35, 49, 52, 56};

// Generic filler-value chooser (advisor-directed design): probes a small
// per-field_type candidate list against `tv.enum_valid`, so this test does
// NOT need to hand-read 12 message schemas' required-field enum domains.
// check_field_type's structural rules (validator.hpp:469-566) are honored
// by construction: Int/Length -> digit strings, Char -> single byte,
// Boolean -> Y/N, Float -> a decimal-parseable token, String/Data -> no
// structural constraint beyond enum.
std::string pick_filler_value(fixpp::dict::table_view const& tv, std::uint16_t tag) {
    using fixpp::dict::field_type;
    std::vector<std::string> candidates;
    switch (tv.field_type_of(tag)) {
        case field_type::Int:
        case field_type::Length:
            candidates = {"1", "2", "3", "0", "5", "10", "100"};
            break;
        case field_type::Float:
            candidates = {"1", "2", "0", "1.5", "100"};
            break;
        case field_type::Char:
            candidates = {"1", "2", "3", "4", "0", "A", "B", "N", "Y", "F", "D", "G", "C", "S"};
            break;
        case field_type::Boolean:
            candidates = {"Y", "N"};
            break;
        case field_type::String:
        case field_type::Data:
        default:
            candidates = {"TEST", "X", "A", "AAAA", "N", "Y", "1"};
            break;
    }
    for (auto const& c : candidates) {
        std::span<std::byte const> const bytes{reinterpret_cast<std::byte const*>(c.data()), c.size()};
        if (tv.enum_valid(tag, bytes)) {
            return c;
        }
    }
    throw std::runtime_error("pick_filler_value: no candidate accepted for tag " + std::to_string(tag));
}

std::string field(std::uint16_t tag, std::string const& value) {
    return std::to_string(tag) + "=" + value + "\x01";
}

std::string header_prefix(std::string const& msg_type, std::uint32_t seq) {
    std::string b = field(35, msg_type);
    b += field(34, std::to_string(seq));
    b += field(49, "SND");
    b += field(52, "20240101-00:00:00.000");
    b += field(56, "TGT");
    return b;
}

// Every OTHER message-level required field (tv.required_fields(msg_type)
// minus the header tags minus `exclude`), filled generically. A required
// field that is itself a group's no_tag is satisfied with count=0 (a
// present-but-empty group -- structurally valid, consume_group returns
// immediately without needing a delimiter).
std::string other_required_fields(fixpp::dict::table_view const& tv, DictOracle const& oracle,
                                  std::string const& msg_type, std::set<std::uint16_t> const& exclude) {
    std::string out;
    for (auto tag : tv.required_fields(msg_type)) {
        if (exclude.contains(tag)) continue;
        if (std::find(kHeaderTags.begin(), kHeaderTags.end(), tag) != kHeaderTags.end()) continue;
        if (oracle.group_tags.contains(tag)) {
            out += field(tag, "0");
            continue;
        }
        out += field(tag, pick_filler_value(tv, tag));
    }
    return out;
}

// Build ONE group instance's body: `delim_tag` first (unless it IS
// `omitted_tag`, in which case it is skipped entirely -- the instance then
// opens with the next required member, or is left empty), then every OTHER
// required member (ascending tag order via std::set, excluding both the
// omitted tag and the delimiter) with a generic filler value. Passing
// `omitted_tag == 0` (no real tag) omits nothing -- the "complete" baseline.
std::string build_group_instance(fixpp::dict::table_view const& tv, std::uint16_t delim_tag,
                                 std::set<std::uint16_t> const& required_members,
                                 std::uint16_t omitted_tag) {
    std::string out;
    if (delim_tag != omitted_tag) {
        out += field(delim_tag, pick_filler_value(tv, delim_tag));
    }
    for (auto tag : required_members) {
        if (tag == omitted_tag || tag == delim_tag) continue;
        out += field(tag, pick_filler_value(tv, tag));
    }
    return out;
}

// TOP-LEVEL case frame: header + [no_tag=1, group instance] + other
// required fields. `omitted_tag == 0` builds the "complete" baseline.
std::vector<std::byte> build_top_level_frame(fixpp::dict::table_view const& tv, DictOracle const& oracle,
                                             GroupContextKey const& key, std::uint16_t omitted_tag,
                                             std::uint32_t seq) {
    auto const delim_it = kDelimTags.find(key);
    if (delim_it == kDelimTags.end()) {
        throw std::runtime_error("build_top_level_frame: no kDelimTags entry for msg_type=" +
                                 key.msg_type + " no_tag=" + std::to_string(key.no_tag));
    }
    auto const& required = oracle.group_required.at(key);

    std::string body = header_prefix(key.msg_type, seq);
    body += field(key.no_tag, "1");
    body += build_group_instance(tv, delim_it->second, required, omitted_tag);
    body += other_required_fields(tv, oracle, key.msg_type, {key.no_tag});
    return fixpp_test_support::make_frame("FIX.4.2", body);
}

// NESTED case frame (MassQuote(i): NoQuoteSets(296) -> NoQuoteEntries(295)):
// 296 is a COMPLETE top-level instance (delimiter + all its own required
// members, including 295 present as a member); 295's OWN sub-instance is
// deliberately incomplete when `omit_299` is true (its delimiter/sole
// required member QuoteEntryID(299) is left out), which the nested-descent
// branch of `consume_group` catches and propagates immediately -- see the
// file header comment for why 296's OTHER required members (304/311) are
// deliberately NOT reachable/needed here.
std::vector<std::byte> build_nested_frame(fixpp::dict::table_view const& tv, DictOracle const& oracle,
                                          bool omit_299, std::uint32_t seq) {
    GroupContextKey const key296{"i", {}, 296};
    auto const delim_it = kDelimTags.find(key296);
    if (delim_it == kDelimTags.end()) {
        throw std::runtime_error("build_nested_frame: no kDelimTags entry for (i, {}, 296)");
    }
    auto const& req296 = oracle.group_required.at(key296);  // {295, 302, 304, 311}
    if (!req296.contains(295)) {
        throw std::runtime_error("build_nested_frame: MassQuote NoQuoteSets(296) required-member "
                                 "set no longer includes 295 -- oracle drift, re-derive");
    }

    std::string body = header_prefix("i", seq);
    body += field(296, "1");
    body += field(delim_it->second, pick_filler_value(tv, delim_it->second));  // 302, delimiter, opens 296's instance
    body += field(295, "1");  // 295 present as a member of 296's instance, ONE sub-instance
    if (!omit_299) {
        body += field(299, pick_filler_value(tv, 299));  // 295's own delimiter/required member, present
    }
    // 296's own OTHER required members (304, 311) are deliberately NOT
    // emitted here -- see the file header comment: the nested descent into
    // 295 short-circuits validate() before they would ever be scanned.
    body += other_required_fields(tv, oracle, "i", {296, 295, 299});
    return fixpp_test_support::make_frame("FIX.4.2", body);
}

// Real production entry point (mirrors test_066_arena_fit_test.cpp's
// AdminGroupMessageFitsAdminArena construction exactly): Framer::feed then
// Parser<Index>{tv}.parse -- no Session, no FSM, but the SAME wire-decode +
// MessageView path Session::parse_and_dispatch_ uses.
fixpp::core::expected_t<void> run_validate(fixpp::wire::dictionary_driven_validator const& validator,
                                           fixpp::dict::table_view const& tv,
                                           std::vector<std::byte> const& raw, std::uint16_t* ref_tag_out) {
    constexpr std::size_t kArena = 16384;
    std::array<std::byte, kArena> storage{};
    std::pmr::monotonic_buffer_resource pa_mr{storage.data(), storage.size()};
    std::array<std::byte, 512> carry_store{};
    std::pmr::monotonic_buffer_resource carry_mr{carry_store.data(), carry_store.size()};
    pmr_carry_buffer carry{carry_store.size(), &carry_mr};
    Framer framer;
    std::array<frame_view, 1> out{};
    auto feed_r = framer.feed(std::span<std::byte const>{raw}, carry, std::span{out});
    if (!feed_r.has_value() || feed_r->empty()) {
        throw std::runtime_error("run_validate: Framer::feed failed to produce a frame");
    }
    Parser<access_mode::Index> parser{tv};
    auto mv_r = parser.parse(out[0], &pa_mr);
    if (!mv_r.has_value()) {
        throw std::runtime_error("run_validate: Parser::parse failed -- construction bug, "
                                 "not the assertion under test");
    }
    return validator.validate(*mv_r, &pa_mr, ref_tag_out);
}

}  // namespace

// T021b [US1]: exact-set-equality witness -- every oracle-derived
// group-required-member context (top-level + the one nested descent)
// rejects on omission (RED today); every context's COMPLETE frame is clean
// (baseline discriminator); a group OUTSIDE the set never rejects via this
// mechanism (negative control).
TEST(GroupRequiredMemberValidation, ExactlyTheOracleDerivedFourteenPairsRejectOnOmission) {
    constexpr std::size_t kBufSize = 4u * 1024u * 1024u;
    auto storage = std::make_unique<std::array<std::byte, kBufSize>>();
    std::pmr::monotonic_buffer_resource mr{storage->data(), storage->size()};
    std::string const path = std::string(FIXPP_DICT_DATA_DIR) + "/FIX42.xml";
    auto const dict = fixpp::dict::XmlLoader{}.load(path, &mr);
    auto const tv = dict.as_table_view();
    fixpp::wire::dictionary_driven_validator const validator{tv};

    auto const oracle = build_quickfix_oracle(path);

    // ── Derive the case set FROM THE ORACLE (not transcribed) ──────────────
    std::vector<GroupContextKey> top_level_cases;
    std::size_t nested_count = 0;
    for (auto const& [key, required] : oracle.group_required) {
        if (required.empty()) continue;
        if (key.path.empty()) {
            top_level_cases.push_back(key);
        } else {
            ++nested_count;
            EXPECT_EQ(key.msg_type, "i");
            EXPECT_EQ(key.path, (std::vector<std::uint16_t>{296}));
            EXPECT_EQ(key.no_tag, 295);
        }
    }
    ASSERT_EQ(top_level_cases.size(), 13u)
        << "oracle-derived top-level required-member context count drifted from the pinned 13 "
           "(contracts/group-detection.md K9) -- re-derive, don't silently update this pin";
    ASSERT_EQ(nested_count, 1u)
        << "oracle-derived NESTED required-member context count drifted from the pinned 1 "
           "(MassQuote NoQuoteSets(296)->NoQuoteEntries(295)) -- re-derive";

    std::uint32_t seq = 2;
    std::size_t cases_checked = 0;

    for (auto const& key : top_level_cases) {
        SCOPED_TRACE(::testing::Message() << "msg_type=" << key.msg_type << " no_tag=" << key.no_tag);
        auto const& required = oracle.group_required.at(key);
        // Deterministic choice: prefer a NON-delimiter required tag (so this
        // case exercises the seen_mask completeness check, validator.hpp:390-
        // 399); fall back to the delimiter itself only when it is the SOLE
        // required member (exercises the "first instance must open with the
        // delimiter" check instead, validator.hpp:287-291 -- BOTH set
        // ref_tag == the omitted tag, so the assertion below is uniform).
        auto const delim_it = kDelimTags.find(key);
        ASSERT_NE(delim_it, kDelimTags.end());
        std::uint16_t omitted_tag = 0;
        for (auto tag : required) {
            if (tag != delim_it->second) {
                omitted_tag = tag;
                break;
            }
        }
        if (omitted_tag == 0) {
            omitted_tag = delim_it->second;
        }

        // Baseline: complete frame (nothing omitted) validates CLEAN today --
        // discriminator per the advisor guidance: if this fails, the case's
        // RED below tells us nothing.
        auto const complete_frame = build_top_level_frame(tv, oracle, key, /*omitted_tag=*/0, seq++);
        std::uint16_t baseline_ref = 0;
        auto const baseline_r = run_validate(validator, tv, complete_frame, &baseline_ref);
        ASSERT_TRUE(baseline_r.has_value())
            << "baseline (complete) frame for msg_type=" << key.msg_type << " no_tag=" << key.no_tag
            << " did not validate clean (ref_tag=" << baseline_ref
            << ", error=" << static_cast<int>(baseline_r.error())
            << ") -- construction bug in this test, not the 082 feature under test";

        // THE PIN: omitting `omitted_tag` must reject with
        // wire_required_field_missing at ref_tag==omitted_tag. RED today --
        // pre-T023 no group structure check ever fires for FIX42 tags at
        // all, so this frame validates OK today (same as the baseline).
        auto const omitted_frame = build_top_level_frame(tv, oracle, key, omitted_tag, seq++);
        std::uint16_t ref_tag = 0;
        auto const r = run_validate(validator, tv, omitted_frame, &ref_tag);
        EXPECT_FALSE(r.has_value())
            << "msg_type=" << key.msg_type << " no_tag=" << key.no_tag << " omitted_tag="
            << omitted_tag << ": omitting a required group member must be REJECTED -- RED "
               "pre-T023 (group structure check unreachable for FIX42 tags)";
        if (!r.has_value()) {
            EXPECT_EQ(r.error(), fixpp::core::error::wire_required_field_missing)
                << "msg_type=" << key.msg_type << " no_tag=" << key.no_tag;
            EXPECT_EQ(ref_tag, omitted_tag)
                << "msg_type=" << key.msg_type << " no_tag=" << key.no_tag
                << ": wrong ref_tag -- rejected for the WRONG reason (not the omitted required "
                   "member) would be a false-positive pin";
        }
        ++cases_checked;
    }
    EXPECT_EQ(cases_checked, 13u);

    // ── The 14th case: NESTED descent (MassQuote NoQuoteSets(296)->
    // NoQuoteEntries(295), omitting 295's own required member 299) ─────────
    {
        SCOPED_TRACE("nested: msg_type=i path=[296] no_tag=295 omit=299");
        auto const complete_frame = build_nested_frame(tv, oracle, /*omit_299=*/false, seq++);
        std::uint16_t baseline_ref = 0;
        auto const baseline_r = run_validate(validator, tv, complete_frame, &baseline_ref);
        ASSERT_TRUE(baseline_r.has_value())
            << "nested baseline (complete) frame did not validate clean (ref_tag=" << baseline_ref
            << ", error=" << static_cast<int>(baseline_r.error())
            << ") -- construction bug in this test, not the 082 feature under test";

        auto const omitted_frame = build_nested_frame(tv, oracle, /*omit_299=*/true, seq++);
        std::uint16_t ref_tag = 0;
        auto const r = run_validate(validator, tv, omitted_frame, &ref_tag);
        EXPECT_FALSE(r.has_value())
            << "MassQuote: a NoQuoteEntries(295) instance missing its own required QuoteEntryID"
               "(299) must be REJECTED via nested descent -- RED pre-T023";
        if (!r.has_value()) {
            EXPECT_EQ(r.error(), fixpp::core::error::wire_required_field_missing);
            EXPECT_EQ(ref_tag, 299u) << "wrong ref_tag -- rejected for the WRONG reason";
        }
    }

    EXPECT_EQ(cases_checked + 1u, 14u)
        << "total case count (13 top-level + 1 nested) drifted from the pinned 14";

    // ── Negative control (both-directions completeness): Allocation(J)'s
    // NoAllocs(78) has an EMPTY top-level required-member set (NOT one of
    // the 14) -- an instance carrying only its delimiter (no other members
    // at all) must NOT reject via this mechanism, proving the 14-pair
    // enumeration is EXACT, not merely "at least these reject". Holds both
    // today (mechanism entirely unreachable) and post-T023 (`check_required`
    // gates on a non-empty `req_members`, validator.hpp:327). ──────────────
    {
        SCOPED_TRACE("negative control: msg_type=J no_tag=78 (empty required-member set)");
        GroupContextKey const key78{"J", {}, 78};
        ASSERT_FALSE(oracle.group_required.contains(key78))
            << "Allocation(J) NoAllocs(78) unexpectedly has a non-empty required-member set in "
               "the oracle -- this negative control's premise no longer holds, pick a different "
               "tag from FIX42's 18 minus the 13 top-level pairs";
        auto const delim_it = kDelimTags.find(key78);
        ASSERT_NE(delim_it, kDelimTags.end());

        std::string body = header_prefix("J", seq++);
        body += field(70, pick_filler_value(tv, 70));  // AllocID
        body += field(71, pick_filler_value(tv, 71));  // AllocTransType
        body += field(78, "1");
        body += field(delim_it->second, pick_filler_value(tv, delim_it->second));  // 79, delimiter only
        body += other_required_fields(tv, oracle, "J", {70, 71, 78});
        auto const frame = fixpp_test_support::make_frame("FIX.4.2", body);

        std::uint16_t ref_tag = 0;
        auto const r = run_validate(validator, tv, frame, &ref_tag);
        EXPECT_TRUE(r.has_value())
            << "Allocation(J) NoAllocs(78), whose required-member set is EMPTY, unexpectedly "
               "rejected (ref_tag=" << ref_tag << ", error=" << (r.has_value() ? -1 : static_cast<int>(r.error()))
            << ") -- the 14-pair set is not exact";
    }
}

}  // namespace fixpp::session::test082
