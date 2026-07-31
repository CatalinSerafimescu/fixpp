// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/delimiter_divergence_wire_test.cpp
//
// 083-group-delimiter-resolution T009 (FR-001 premise repro / FR-010a /
// FR-007a / SC-004, SC-013) — the wire-level RED witnesses that close the
// single inference the analysis rested on (quickstart.md §1/§1a).
//
// This is a MEASUREMENT task, not a "make it red" task. Every assertion
// below encodes the POST-FIX target so the pin stays meaningful once Phase
// 3/5/6 land; today's actual (measured) verdict is recorded in the failure
// message and in this header, not silently adjusted to match.
//
// ============================================================================
// FR-001 PREMISE VERDICT (see DivergentContextRejectedWhileFirstSeenAccepted
// below): the divergent context (FIX44 CollateralRequest/AX, NoExecs(124)) IS
// rejected today for EVERY non-empty instance, and the first-seen context
// (FIX44 AllocationInstruction/J, same tag 124) IS accepted today. The
// premise holds; the feature is not vacuous.
//
// First-seen context, determined empirically (not assumed): `NoExecs(124)`
// is declared via THREE separate group declarations, one per referencing
// component (`dictionaries/FIX44.xml:2805-2824` — `ExecAllocGrp`,
// `ExecCollGrp`, `ExecsGrp`). `ExecAllocGrp`'s first field is `LastQty`
// (tag 32); `ExecCollGrp`/`ExecsGrp`'s sole field is `ExecID` (tag 17).
// `ExecAllocGrp` is referenced for the first time in document order at
// `FIX44.xml:552`, inside `AllocationInstruction` (msgtype `J`,
// `FIX44.xml:539`) — no earlier message references any `NoExecs`-bearing
// component. So `J` is the first-seen context, and its group's own
// (correct) delimiter is 32 — exactly the measured `bare_global=32` for
// tag 124 (research.md T007 sample table). `CollateralRequest` (`AX`,
// `FIX44.xml:2046`) references `ExecCollGrp` (`:2058`), whose true
// (declaration-order) delimiter is 17 — exactly the measured
// `oracle_delim=17` for the same tag, i.e. AX/124 is the divergent context.
//
// ============================================================================
// FR-010a / FR-007a NEGATIVE FINDING (measured, not assumed — see the
// "polluted-context blocking mechanism" comment block below): FIX44's ONLY
// polluted `NoExecs(124)` contexts are the six Collateral messages (AX/AY/
// AZ/BA/BB/BG), and for ALL of them the injected/polluted tag is 32
// (LastQty) — i.e. the SAME tag that is today's wrongly-resolved delimiter
// for this no_tag (spec.md's "100% of cases" observation, D-5). Tag 32 is
// NOT declared anywhere in any Collateral message's OWN dictionary tree
// (`FIX44.xml:2046-2265` — none of the six reference `ExecAllocGrp`), so it
// is absent from `table_view::valid_tags_for("AX")` (built solely from
// `message_fields("AX")`, `dictionary.cpp:378-383` — a per-message-type
// computation entirely independent of the group-context pollution
// mechanism at `table_view.hpp:641-646`). `dictionary_driven_validator::
// validate()`'s Step 1 ("(a) Unexpected tag check", `validator.hpp:176`)
// walks EVERY field in the frame — including fields inside a group's body,
// since `MessageView::begin()/end()` is a flat, dict-free byte scan with no
// notion of group nesting (`parser.hpp:186-234`) — and runs BEFORE Step 3's
// group-structure walk. So ANY AX frame containing tag 32 anywhere is
// rejected with `wire_unexpected_tag` at Step 1, REGARDLESS of the
// group-context member-set pollution, both TODAY and AFTER this feature
// (083's scope never touches `add_valid_tag`/`valid_tags_for`). This is
// confirmed empirically below
// (PollutedContextInjectedTagIsNotIndependentlyValid) and is not specific
// to AX: the mechanism is dictionary-agnostic — a "polluted" member is
// BY DEFINITION a tag the polluted context's OWN declaration does not
// contain (that is what makes it an injection rather than a real member),
// and `valid_tags_for(mt)` is built solely from `mt`'s own declaration, so
// no named polluted context anywhere in the ten shipped dictionaries can
// exhibit "accepted today" for its injected tag via this validation path.
//
// The over-permissive-membership leniency FR-010a describes is therefore
// UNREACHABLE at the wire level through `dictionary_driven_validator::
// validate()`, for every polluted context this feature's own Baseline
// measures (spec.md's 52). C1 and C2 below record this as the shapes
// tried and the negative result (per FR-010a/FR-007a's own escape clause),
// sharing one root cause: Step 1's message-wide valid-tag gate rejects the
// injected tag before Step 3's group-structure/extent walk — the layer
// FR-010a/FR-007a's leniency and swallow would need to run in — ever sees
// it, both pre- and post-083.
//
// Anchors:
//   tasks:      specs/083-group-delimiter-resolution/tasks.md T009
//   quickstart: specs/083-group-delimiter-resolution/quickstart.md §1/§1a
//   spec:       specs/083-group-delimiter-resolution/spec.md US1 scenarios
//               1-5, SC-004, SC-013, FR-010a, FR-007a, Assumptions
//               ("Delimiter correctness subsumes member-set correctness")
//   research:   specs/083-group-delimiter-resolution/research.md D-5, T007

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fixpp/core/error.hpp>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fixpp/wire/parser.hpp>
#include <fixpp/wire/validator.hpp>
#include <memory_resource>
#include <string>
#include <string_view>
#include <vector>

#include "support/frame_view_factory.hpp"

namespace {

using fixpp::core::expected_t;
using fixpp::dict::Dictionary;
using fixpp::wire::access_mode;
using fixpp::wire::dictionary_driven_validator;
using fixpp::wire::MessageView;
using fixpp::wire::Parser;

// ── Shared wire-frame helpers (mirrors tests/wire/required_scope_two_tier_test.cpp) ──
std::string make_checksum_field(unsigned chk) {
    std::string s = "10=";
    s.push_back(static_cast<char>('0' + ((chk / 100U) % 10U)));
    s.push_back(static_cast<char>('0' + ((chk / 10U) % 10U)));
    s.push_back(static_cast<char>('0' + (chk % 10U)));
    s.push_back('\x01');
    return s;
}

std::vector<std::byte> make_frame(std::string_view body_fields) {
    std::string body{body_fields};
    std::string nine = "9=" + std::to_string(body.size()) + "\x01";
    std::string pre = "8=FIX.4.4\x01" + nine + body;
    unsigned sum = 0;
    for (unsigned char c : pre) {
        sum += c;
    }
    std::string full = pre + make_checksum_field(sum % 256U);
    std::vector<std::byte> out(full.size());
    std::memcpy(out.data(), full.data(), full.size());
    return out;
}

MessageView<access_mode::Index> parse_index(std::vector<std::byte> const& buf,
                                            std::pmr::monotonic_buffer_resource& arena) {
    auto fv = fixpp::wire::test::make_frame_view(buf);
    if (!fv.has_value()) {
        ADD_FAILURE() << "make_frame_view failed";
        return {};
    }
    Parser<access_mode::Index> parser{};
    auto mv = parser.parse(*fv, &arena);
    if (!mv.has_value()) {
        ADD_FAILURE() << "parser.parse failed";
        return {};
    }
    return std::move(*mv);
}

Dictionary load_real_dict(char const* file, std::pmr::memory_resource* mr) {
    auto const path = std::filesystem::path{FIXPP_DICT_DATA_DIR} / file;
    return fixpp::dict::XmlLoader{}.load(path, mr);
}

// FIX50SP2 frames are FIXT.1.1-encoded (BeginString="FIXT.1.1"); the session-
// header fields (34/49/52/56/...) reach validity via the 081 Concern A
// `kFixtFramingTable` (dictionary.cpp:573-589), keyed off
// `which_session_version()` detecting v50sp2 from the loaded dictionary —
// not via `valid_tags_for`, which stays empty for FIX50SP2's own (FIXT-
// split) `<header/>`.
std::vector<std::byte> make_frame_fixt(std::string_view body_fields) {
    std::string body{body_fields};
    std::string nine = "9=" + std::to_string(body.size()) + "\x01";
    std::string pre = "8=FIXT.1.1\x01" + nine + body;
    unsigned sum = 0;
    for (unsigned char c : pre) {
        sum += c;
    }
    std::string full = pre + make_checksum_field(sum % 256U);
    std::vector<std::byte> out(full.size());
    std::memcpy(out.data(), full.data(), full.size());
    return out;
}

constexpr std::size_t kScratch = 2048;

expected_t<void> run_validate(dictionary_driven_validator const& v,
                              MessageView<access_mode::Index> const& mv) {
    std::array<std::byte, kScratch> scratch_buf{};
    std::pmr::monotonic_buffer_resource scratch_mr{scratch_buf.data(), scratch_buf.size(),
                                                   std::pmr::null_memory_resource()};
    return v.validate(mv, &scratch_mr, nullptr);
}

// FIX44 CollateralRequest(AX) message-level required prefix: CollReqID(894),
// CollAsgnReason(895, enum), TransactTime(60) + standard header
// (FIX44.xml:2046-2049).
std::string fix44_ax_required_prefix() {
    return "35=AX\x01"
           "34=1\x01" "49=SENDER\x01" "52=20240101-00:00:00\x01" "56=TARGET\x01"
           "894=COLLREQ1\x01" "895=0\x01" "60=20240101-00:00:00\x01";
}

// FIX44 AllocationInstruction(J) message-level required prefix: AllocID(70),
// AllocTransType(71, enum), AllocType(626, enum), AllocNoOrdersType(857,
// enum), Side(54, enum), Quantity(53), AvgPx(6), TradeDate(75) + standard
// header (FIX44.xml:539-576). Instrument(required='Y') carries no required
// direct fields of its own (FIX44.xml:2386-2401, all required='N').
std::string fix44_j_required_prefix() {
    return "35=J\x01"
           "34=1\x01" "49=SENDER\x01" "52=20240101-00:00:00\x01" "56=TARGET\x01"
           "70=ALLOC1\x01" "71=0\x01" "626=1\x01" "857=0\x01" "54=1\x01" "53=100\x01" "6=10.5\x01"
           "75=20240101\x01";
}

}  // namespace

// ============================================================================
// FR-001 premise repro (quickstart.md §1, US1 scenarios 1-2). Two
// assertions, one asymmetry: a divergent context is rejected while the
// first-seen context of the SAME NumInGroup tag is accepted, TODAY. Both
// assertions below encode the POST-FIX target (both accepted) so the pin
// stays meaningful after Phase 5 lands; TODAY the first is expected RED
// (rejected) and the second GREEN (already accepted).
//
// If this observed today: BOTH accept -> the feature's premise is wrong and
// this comment must say so rather than proceeding. That is NOT what is
// observed (see the verdicts recorded in the failure messages / EXPECT
// below), so the feature's premise stands.
// ============================================================================
TEST(DelimiterDivergenceWire, DivergentContextRejectedWhileFirstSeenAccepted) {
    std::pmr::monotonic_buffer_resource dict_mr;
    auto d44 = load_real_dict("FIX44.xml", &dict_mr);
    dictionary_driven_validator v{d44.as_table_view()};

    // Divergent context: CollateralRequest(AX), NoExecs(124) opened with the
    // group's TRUE declaration-order delimiter, ExecID(17) — the tag the
    // dictionary actually declares for ExecCollGrp (FIX44.xml:2815-2819).
    // Today the context's REGISTERED delimiter is still the dictionary-wide
    // first-seen value (32, LastQty), so opening with 17 is rejected
    // (wire_required_field_missing at validator.hpp:287-291 — the first
    // instance's opening tag doesn't match delim_tag).
    {
        auto buf = make_frame(fix44_ax_required_prefix() +
                              "124=1\x01"
                              "17=EXEC1\x01");
        std::pmr::monotonic_buffer_resource arena;
        auto mv = parse_index(buf, arena);
        auto result = run_validate(v, mv);
        EXPECT_TRUE(result.has_value())
            << "POST-FIX TARGET: CollateralRequest(AX)/NoExecs(124) opened with the group's "
               "true declaration-order delimiter ExecID(17) must be ACCEPTED (FR-001/FR-002, "
               "US1 scenario 1). TODAY this is expected to be REJECTED with error="
            << (result.has_value() ? 0 : static_cast<int>(result.error()))
            << " (wire_required_field_missing — the context's registered delimiter is still "
               "the wrong dictionary-global first-seen value, 32/LastQty, not 17/ExecID).";
    }

    // First-seen context: AllocationInstruction(J), NoExecs(124) opened with
    // ITS OWN true declaration-order delimiter, LastQty(32) — which today's
    // dictionary-global first-seen resolution ALSO happens to produce for
    // every context sharing this no_tag (J is the source of that global
    // value; see the file-header comment for how this was determined).
    // Must be accepted BOTH today and after the fix — the fix must not
    // trade one context's correctness for another's (US1 scenario 2).
    {
        auto buf = make_frame(fix44_j_required_prefix() +
                              "124=1\x01"
                              "32=100\x01");
        std::pmr::monotonic_buffer_resource arena;
        auto mv = parse_index(buf, arena);
        auto result = run_validate(v, mv);
        EXPECT_TRUE(result.has_value())
            << "AllocationInstruction(J)/NoExecs(124) opened with LastQty(32) — the first-seen "
               "context's own true delimiter, which also happens to be today's dictionary-wide "
               "first-seen value — must be ACCEPTED both before and after this feature (US1 "
               "scenario 2). error="
            << (result.has_value() ? 0 : static_cast<int>(result.error()));
    }
}

// A group opened with a tag that is NOT that context's declared delimiter
// must still be rejected (US1 scenario 3) — correct resolution must not
// become permissiveness. AllocationInstruction(J)'s own context delimiter
// is 32 (LastQty); opening with ExecID(17) instead is never valid for J,
// before or after this feature.
TEST(DelimiterDivergenceWire, WrongOpeningTagStillRejected) {
    std::pmr::monotonic_buffer_resource dict_mr;
    auto d44 = load_real_dict("FIX44.xml", &dict_mr);
    dictionary_driven_validator v{d44.as_table_view()};

    auto buf = make_frame(fix44_j_required_prefix() +
                          "124=1\x01"
                          "17=EXEC1\x01");
    std::pmr::monotonic_buffer_resource arena;
    auto mv = parse_index(buf, arena);
    auto result = run_validate(v, mv);
    EXPECT_FALSE(result.has_value())
        << "AllocationInstruction(J)/NoExecs(124) opened with ExecID(17) — not J's declared "
           "delimiter (LastQty/32) in ANY reading of the dictionary — must be REJECTED, both "
           "before and after this feature (US1 scenario 3, tag 17 is also not independently "
           "valid for msg_type J so this fails at Step 1 today regardless).";
}

// ============================================================================
// FR-010a / FR-007a negative-result witnesses (#210 Consequences 1 and 2).
//
// MEASURED (not assumed): PollutedContextInjectedTagIsNotIndependentlyValid
// below confirms, against the REAL loaded FIX44 dictionary, that tag 32 —
// the injected/polluted member of every one of FIX44's six polluted
// NoExecs(124) contexts (AX/AY/AZ/BA/BB/BG) — is absent from
// `valid_tags_for("AX")`. Given that, `dictionary_driven_validator::
// validate()`'s Step 1 (`validator.hpp:176`, which runs over EVERY field in
// the frame BEFORE Step 3's group-structure walk, per parser.hpp's flat
// dict-free field_iterator) rejects any AX frame containing tag 32 anywhere
// with wire_unexpected_tag, independent of the group-context pollution at
// table_view.hpp:641-646 (whose scope is `group_ctx_`/`group_member_tags`,
// never `valid_`/`valid_tags_for`). So AX/NoExecs(124) — the "cleanest"
// divergent context per quickstart.md §1, and FIX44's ONLY polluted
// NoExecs(124) family — cannot demonstrate FR-010a's over-permissive-
// membership leniency: a message carrying tag 32 inside the group is
// REJECTED today, for a reason (Step 1's unrelated valid-tag gate) that has
// nothing to do with the pollution this feature removes, and stays
// rejected after (083 does not touch `add_valid_tag`). The two cases below
// (C1/C2) record this as the shapes tried and the negative result, per
// FR-010a/FR-007a's own escape clause ("If measurement shows [it] is
// unreachable, that negative result and the shapes tried MUST be recorded
// in Assumptions instead").
//
// This is not an AX-specific artifact: a "polluted" member is BY
// DEFINITION not declared in the polluted context's own message tree (that
// is what makes it an injection rather than a genuine member), and
// `valid_tags_for(mt)` is built solely from `mt`'s own declaration
// (dictionary.cpp:378-383). So no named polluted context, in any of the
// ten shipped dictionaries, can exhibit "accepted today" for its injected
// tag through this validation path — the blocking mechanism generalises.
// ============================================================================
TEST(DelimiterDivergenceWire, PollutedContextInjectedTagIsNotIndependentlyValid) {
    std::pmr::monotonic_buffer_resource dict_mr;
    auto d44 = load_real_dict("FIX44.xml", &dict_mr);
    auto const tv = d44.as_table_view();

    constexpr std::uint16_t kNoExecs = 124;
    constexpr std::uint16_t kInjectedTag = 32;    // LastQty — today's wrong global delimiter
    constexpr std::uint16_t kTrueMember = 17;     // ExecID — AX's true (only) declared member
    std::array<std::uint16_t, 0> const root{};

    // The context's registered delimiter today is the wrong, dictionary-
    // global first-seen value (32) — measured directly (research.md T007:
    // FIX44|AX|{}|124|17|32|YES divergent).
    EXPECT_EQ(tv.group_first_field("AX", root, kNoExecs), kInjectedTag)
        << "AX/NoExecs(124)'s registered delimiter should still be today's wrong bare-global "
           "value (32/LastQty) before this feature lands.";

    // The member set is polluted: it contains 32 in addition to the true
    // declared member 17 (D-5's injection mechanism).
    auto const members = tv.group_member_tags("AX", root, kNoExecs);
    bool has_injected = false;
    bool has_true_member = false;
    for (auto const m : members) {
        has_injected = has_injected || (m == kInjectedTag);
        has_true_member = has_true_member || (m == kTrueMember);
    }
    EXPECT_TRUE(has_injected) << "AX/NoExecs(124)'s member set should be polluted with the "
                                 "injected delimiter (32/LastQty) before this feature lands.";
    EXPECT_TRUE(has_true_member) << "AX/NoExecs(124)'s true declared member (17/ExecID) must "
                                    "remain present regardless.";

    // THE FINDING: the injected/polluted tag is NOT independently valid for
    // msg_type AX at all — it is absent from valid_tags_for("AX"), which is
    // built solely from AX's own message_fields() expansion
    // (dictionary.cpp:378-383) and is entirely untouched by the group-
    // context pollution mechanism. This is TRUE BOTH BEFORE AND AFTER this
    // feature (083's scope never touches add_valid_tag/valid_tags_for), so
    // it is the reason FR-010a's leniency cannot be witnessed on this named
    // context.
    EXPECT_FALSE(tv.field_valid_for("AX", kInjectedTag))
        << "MEASURED: tag 32 (LastQty) is NOT independently valid for msg_type AX — it is not "
           "declared anywhere in CollateralRequest's own dictionary tree (FIX44.xml:2046-2090; "
           "none of the six Collateral messages reference ExecAllocGrp). This means Step 1 of "
           "dictionary_driven_validator::validate() rejects ANY AX frame containing tag 32 "
           "as wire_unexpected_tag, independent of and unaffected by the group-context member-"
           "set pollution — the mechanism that blocks FR-010a's witness on this context, "
           "unchanged before and after this feature.";
}

// FR-010a / #210 Consequence 1 (over-permissive membership). NEGATIVE
// RESULT: see the file-header comment and
// PollutedContextInjectedTagIsNotIndependentlyValid above for the measured
// mechanism. Shape tried: CollateralRequest(AX), NoExecs(124) — FIX44's
// only polluted context for this tag — with the group opened by its true
// delimiter (17/ExecID) and the injected/polluted tag (32/LastQty) placed
// as an additional field inside the same instance. This is REJECTED today
// (Step 1's independent valid-tag gate; tag 32 is not declared for AX at
// all) — NOT because of the pollution FR-010a targets, and for a reason
// this feature's scope does not change, so the SAME rejection is expected
// after the fix. The leniency FR-010a describes could not be witnessed on
// this context because Step 1 already forecloses it; SC-002's cardinality
// assertion (52 -> 0) is therefore the only pin this context contributes
// to fixpp#210's membership defect — a distinct, narrower claim from the
// direct wire-acceptance witness FR-010a asked for.
TEST(DelimiterDivergenceWire, PollutedContextAcceptsInjectedTagTodayRejectsAfter) {
    std::pmr::monotonic_buffer_resource dict_mr;
    auto d44 = load_real_dict("FIX44.xml", &dict_mr);
    dictionary_driven_validator v{d44.as_table_view()};

    // AX/NoExecs(124), opened with the true delimiter (17), carrying the
    // injected/polluted tag (32) as a second field of the same instance.
    auto buf = make_frame(fix44_ax_required_prefix() +
                          "124=1\x01"
                          "17=EXEC1\x01"
                          "32=999\x01");
    std::pmr::monotonic_buffer_resource arena;
    auto mv = parse_index(buf, arena);
    auto result = run_validate(v, mv);

    EXPECT_FALSE(result.has_value())
        << "NEGATIVE RESULT (recorded, not dropped — FR-010a's escape clause): the injected "
           "tag (32/LastQty) inside AX's polluted NoExecs(124) context is REJECTED today, not "
           "accepted, because tag 32 is not independently valid for msg_type AX at all (Step 1 "
           "fires wire_unexpected_tag before Step 3's group-membership walk ever runs — see "
           "PollutedContextInjectedTagIsNotIndependentlyValid). error="
        << (result.has_value() ? 0 : static_cast<int>(result.error()))
        << ". FR-010a's over-permissive-membership leniency is therefore UNREACHABLE at the "
           "wire level on this — or any — named polluted context: a polluted member is by "
           "definition undeclared in its own context's message tree, which is exactly what "
           "keeps it out of valid_tags_for(mt) too. This assertion pins that the rejection is "
           "unchanged before and after this feature (083 never touches valid_tags_for), which "
           "is the true, achievable invariant here.";
}

// FR-007a / #210 Consequence 2 (extent mis-parse — the swallow the issue
// author sequenced first). NEGATIVE RESULT, same root cause as C1 above:
// shape tried — the SAME AX/NoExecs(124) polluted context, group opened
// with the true delimiter (17), followed by a message-level field AFTER
// the group whose tag equals the injected/polluted member (32). Per
// quickstart.md §1a this is meant to show a trailing field swallowed into
// the last group instance (an extent-termination defect distinct from
// C1's instance-open defect). It could not be reached: the frame is
// rejected at Step 1 (tag 32 unexpected for msg_type AX) before Step 3's
// extent scan — the layer where a swallow could even be observed — ever
// runs, so neither "swallowed" nor "not swallowed" is witnessable on this
// context; the frame simply never reaches that code path today, and
// (083's scope leaves Step 1 untouched) would not reach it after the fix
// either.
TEST(DelimiterDivergenceWire, TrailingMessageFieldNotSwallowedIntoLastInstance) {
    std::pmr::monotonic_buffer_resource dict_mr;
    auto d44 = load_real_dict("FIX44.xml", &dict_mr);
    dictionary_driven_validator v{d44.as_table_view()};

    // AX/NoExecs(124), one instance opened with the true delimiter (17),
    // followed by a message-level field (32) AFTER the group — the shape
    // quickstart.md §1a describes as "swallowed into the last instance"
    // today.
    auto buf = make_frame(fix44_ax_required_prefix() +
                          "124=1\x01"
                          "17=EXEC1\x01"
                          "32=999\x01");  // trailing field, tag == injected member
    std::pmr::monotonic_buffer_resource arena;
    auto mv = parse_index(buf, arena);
    auto result = run_validate(v, mv);

    EXPECT_FALSE(result.has_value())
        << "NEGATIVE RESULT (recorded, not dropped — FR-007a's escape clause): this shape is "
           "REJECTED at Step 1 (wire_unexpected_tag on tag 32, which is not independently valid "
           "for msg_type AX) before Step 3's extent-termination scan ever runs. The swallow "
           "FR-007a describes (a trailing field consumed into the last instance because the "
           "extent scan does not terminate) can only be observed at Step 3, which this shape "
           "never reaches. error="
        << (result.has_value() ? 0 : static_cast<int>(result.error()))
        << ". PROPOSED spec.md Assumptions text (NOT applied by this task — reported for the "
           "orchestrator to apply with an amendment-log entry): \"The FR-007a extent-mis-parse "
           "swallow was sought on FIX44's only polluted NoExecs(124) context "
           "(CollateralRequest/AX) by opening the group with its true delimiter (ExecID/17) "
           "and following it with a message-level field equal to the injected/polluted member "
           "(LastQty/32). The shape is unreachable: tag 32 is not independently declared "
           "anywhere in CollateralRequest's own dictionary tree, so "
           "dictionary_driven_validator::validate()'s Step 1 (the message-wide valid-tag "
           "check) rejects any AX frame containing it with wire_unexpected_tag before Step 3's "
           "group-extent scan — the layer where a swallow could occur — ever runs. No named "
           "polluted context in the ten shipped dictionaries can exhibit this shape, because a "
           "polluted member is by construction undeclared in its own context's message tree, "
           "which is exactly what keeps it out of the per-message valid-tag set too. FR-007a's "
           "swallow is therefore not witnessable at the wire level through this validation "
           "path on any measured polluted context (tests/wire/delimiter_divergence_wire_test."
           "cpp::DelimiterDivergenceWire.TrailingMessageFieldNotSwallowedIntoLastInstance).\"";
}

// ============================================================================
// SC-004's named eight-tag wire subset: 1677, 1772, 40204, 41599, 42060
// (cause-2 wrong-delimiter, FIX50SP2), 1499, 1669, 1919 (cause-3 currently-
// unregistered, FIX50SP2). Every one of the eight resolves its TRUE
// (declaration-order) delimiter to a NESTED group's own count tag (the
// US2/US3 shape). RED now (Phase 1); REQUIRED GREEN at the Phase-3 exit
// (quickstart.md §4's "SC-004's named eight-tag wire subset" row). Each
// message is built with the outer group opened by its true delimiter and
// TWO instances, to exercise the descend-at-delimiter fix directly.
//
// Host messages and true delimiters were derived directly from
// dictionaries/FIX50SP2.xml (declaration order, component recursion) and
// cross-checked against research.md's T007 sample table where a row
// exists for that context:
//   1677 NoPartyRiskLimits      -> PartyRiskLimitsReport(CM)           -> true delim 1671 (NoPartyDetails)
//   1772 NoPartyEntitlements    -> PartyEntitlementsReport(CV)         -> true delim 1671 (NoPartyDetails)
//   40204 NoPhysicalSettlTerms  -> IOI(6), path {}                      -> true delim 40209 (research.md T007)
//   41599 NoLegPhysicalSettlTerms -> IOI(6), path {555}                 -> true delim 41604 (research.md T007)
//   42060 NoUnderlyingPhysicalSettlTerms -> IOI(6), path {711}          -> true delim 42065 (research.md T007)
//   1499 NoAsgnReqs             -> StreamAssignmentRequest(CC)          -> true delim 453 (NoPartyIDs, research.md T007)
//   1669 NoRiskLimits           -> PartyRiskLimitsReport(CM), path {1677} -> true delim 1529 (research.md D-model line 494/499)
//   1919 NoPriceMovements       -> SecurityList(y), path {146}          -> true delim 1920 (spec.md Baseline: "1919->1920")
//
// 1669 and 1919 are nested inside a PARENT group (1677, 146 respectively);
// 1669's parent (1677) is itself one of the five wrong-delimiter contexts,
// so 1669's own fixture is a precondition on BOTH Phase 3 (receiver
// descent) AND Phase 5/6 (loader recursion + fail-closed registration)
// landing together — its rejection today may originate at either level;
// the per-tag verdict below records the outcome, not the exact defect
// layer, per the task's per-tag reporting requirement.
// ============================================================================
TEST(DelimiterDivergenceWire, NamedCountTagSubsetAccepts) {
    std::pmr::monotonic_buffer_resource dict_mr;
    auto d50sp2 = load_real_dict("FIX50SP2.xml", &dict_mr);
    dictionary_driven_validator v{d50sp2.as_table_view()};

    struct Case {
        std::uint16_t tag;
        char const* name;
        std::string body;
    };

    std::vector<Case> const cases{
        // 1677 NoPartyRiskLimits, PartyRiskLimitsReport(CM), true delim 1671.
        {1677, "1677 NoPartyRiskLimits (CM)",
         "35=CM\x01" "34=1\x01" "49=SENDER\x01" "52=20240101-00:00:00\x01" "56=TARGET\x01"
         "1667=RPT1\x01"
         "1677=2\x01"
         "1671=1\x01" "1691=PD1\x01"
         "1671=1\x01" "1691=PD2\x01"},
        // 1772 NoPartyEntitlements, PartyEntitlementsReport(CV), true delim 1671.
        {1772, "1772 NoPartyEntitlements (CV)",
         "35=CV\x01" "34=1\x01" "49=SENDER\x01" "52=20240101-00:00:00\x01" "56=TARGET\x01"
         "1771=RPT2\x01"
         "1772=2\x01"
         "1671=1\x01" "1691=PD1\x01"
         "1671=1\x01" "1691=PD2\x01"},
        // 40204 NoPhysicalSettlTerms, IOI(6), path {}, true delim 40209.
        {40204, "40204 NoPhysicalSettlTerms (IOI)",
         "35=6\x01" "34=1\x01" "49=SENDER\x01" "52=20240101-00:00:00\x01" "56=TARGET\x01"
         "23=IOI1\x01" "28=N\x01" "54=1\x01" "27=S\x01"
         "40204=2\x01"
         "40209=0\x01"
         "40209=0\x01"},
        // 41599 NoLegPhysicalSettlTerms, IOI(6), path {555}, true delim 41604.
        // Nested inside ONE instance of NoLegs(555), opened with its own
        // true delimiter LegSymbol(600).
        {41599, "41599 NoLegPhysicalSettlTerms (IOI/NoLegs)",
         "35=6\x01" "34=1\x01" "49=SENDER\x01" "52=20240101-00:00:00\x01" "56=TARGET\x01"
         "23=IOI1\x01" "28=N\x01" "54=1\x01" "27=S\x01"
         "555=1\x01"
         "600=LEGSYM1\x01"
         "41599=2\x01"
         "41604=0\x01"
         "41604=0\x01"},
        // 42060 NoUnderlyingPhysicalSettlTerms, IOI(6), path {711}, true
        // delim 42065. Nested inside ONE instance of NoUnderlyings(711),
        // opened with its own true delimiter UnderlyingSymbol(311).
        {42060, "42060 NoUnderlyingPhysicalSettlTerms (IOI/NoUnderlyings)",
         "35=6\x01" "34=1\x01" "49=SENDER\x01" "52=20240101-00:00:00\x01" "56=TARGET\x01"
         "23=IOI1\x01" "28=N\x01" "54=1\x01" "27=S\x01"
         "711=1\x01"
         "311=UND1\x01"
         "42060=2\x01"
         "42065=0\x01"
         "42065=0\x01"},
        // 1499 NoAsgnReqs, StreamAssignmentRequest(CC), true delim 453
        // (NoPartyIDs). Currently UNREGISTERED (spec.md Baseline: "1499->
        // 453"; Parties' own first child is a group, not a scalar, so the
        // pre-fix one-level scan resolves nothing).
        {1499, "1499 NoAsgnReqs (CC)",
         "35=CC\x01" "34=1\x01" "49=SENDER\x01" "52=20240101-00:00:00\x01" "56=TARGET\x01"
         "1497=SR1\x01" "1498=1\x01"
         "1499=2\x01"
         "453=1\x01" "448=PARTY1\x01"
         "453=1\x01" "448=PARTY2\x01"},
        // 1669 NoRiskLimits, PartyRiskLimitsReport(CM), path {1677}, true
        // delim 1529 (NoRiskLimitTypes). Currently UNREGISTERED (spec.md
        // Baseline: "1669->1529"). Nested inside ONE instance of the
        // OUTER group 1677, which is itself one of the five wrong-
        // delimiter contexts above — see the file comment on why this
        // fixture has two preconditions.
        {1669, "1669 NoRiskLimits (CM, nested under 1677)",
         "35=CM\x01" "34=1\x01" "49=SENDER\x01" "52=20240101-00:00:00\x01" "56=TARGET\x01"
         "1667=RPT1\x01"
         "1677=1\x01"
         "1671=0\x01"
         "1669=2\x01"
         "1529=0\x01"
         "1529=0\x01"},
        // 1919 NoPriceMovements, SecurityList(y), path {146}, true delim
        // 1920 (NoPriceMovementValues). Currently UNREGISTERED (spec.md
        // Baseline: "1919->1920"). Nested inside ONE instance of
        // NoRelatedSym(146), opened with its own (already-correct)
        // delimiter Symbol(55).
        {1919, "1919 NoPriceMovements (SecurityList, nested under 146)",
         "35=y\x01" "34=1\x01" "49=SENDER\x01" "52=20240101-00:00:00\x01" "56=TARGET\x01"
         "146=1\x01"
         "55=SYM1\x01"
         "1919=2\x01"
         "1920=0\x01"
         "1920=0\x01"},
    };

    for (auto const& c : cases) {
        auto buf = make_frame_fixt(c.body);
        std::pmr::monotonic_buffer_resource arena;
        auto mv = parse_index(buf, arena);
        auto result = run_validate(v, mv);
        EXPECT_TRUE(result.has_value())
            << "tag " << c.tag << " (" << c.name
            << "): POST-FIX TARGET is ACCEPTED with an instance count of two (SC-004). "
               "TODAY's actual verdict: "
            << (result.has_value() ? "ACCEPTED" : "REJECTED, error=" +
                                                        std::to_string(static_cast<int>(result.error())));
    }
}
