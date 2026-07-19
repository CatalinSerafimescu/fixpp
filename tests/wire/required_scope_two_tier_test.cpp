// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/required_scope_two_tier_test.cpp
//
// 079-required-presence-scope T013 [US3] (Contract 3 / SC-004 / SC-008 /
// FR-007): the runtime `dictionary_driven_validator` and the generated
// typed `validate_<Msg>` tier must agree on accept/reject for the same
// message. This guards the Phase-0 "no codegen change" conclusion (FR-007 /
// Assumptions) — a divergence here localizes a missed codegen leg.
//
// Scope, per Contract 3 / spec.md US3:
//   - v44 (PositionReport/AP, NoUnderlyings(711) group): END-TO-END
//     full-frame verdict comparison. The SAME conforming input (T006) and
//     malformed input (T010) are run through BOTH the runtime validator (a
//     real wire frame) and the typed tier (an equivalent PositionReportArgs)
//     — asserting identical accept/reject verdicts.
//   - v50sp2 (TradeCaptureReport/AE, NoSides(552)/Side(54)) and vlatest
//     (PositionReport/AP): standalone full-frame `validate()` is BLOCKED for
//     v50sp2 by the empty FIXT `<header/>` (L-041-2/#203 — validate() Step
//     1(a) rejects the framing tag 8 before Step 2/3 ever run). vlatest's
//     Orchestra PositionReport shape diverges substantially from v44's own
//     (confirmed by direct inspection of the generated
//     fixpp/vlatest/messages/PositionReport.validator.cpp: its top-level
//     required_checks is {715,721} vs v44's {1,581,715,721,728,730,731,734},
//     and its NoUnderlyings(711) group carries ZERO required members vs
//     v44's {732,733}) — so a hand-built "conforming vlatest frame" is not a
//     reliable corroboration of THIS message's group-scope fix specifically.
//     Both are therefore compared at the DERIVATION tier instead: runtime
//     `table_view::required_fields(msg)` vs the typed tier's OWN
//     message-level required-tag set (`fixpp::wire::writer_traits<...Args>::
//     required_checks`, read directly from the generated validator source —
//     the typed tier's actual required-set source, not the codegen IR) must
//     be exactly set-equal.
//   - FIX42 excluded entirely: no generated typed `validate_<Msg>`
//     (tools/codegen/fixpp-codegen/main.cpp:132 `if (ir.ns != "v42")` —
//     L-077-1/#196).
//
// Header-only inclusion (`FIXPP_VALIDATORS_HEADER_ONLY_<Msg>`) is used for
// every message here: `fixpp::wire::writer_traits<...>::required_checks` is
// ONLY visible via the `.inl` (header-only) body — the default
// forward-declared surface exposes just `validate_<Msg>`, not the
// specialization (data-model.md Entity 3 vs Entity 4). This TU therefore
// needs NO link dependency on `fixpp::validators::{v44,v50sp2,vlatest}`.
//
// Anchors: specs/079-required-presence-scope/contracts/census-and-agreement.md
// Contract 3; spec.md US3 (lines 62-75) / FR-007 / SC-004 / SC-008;
// quickstart.md §5; tests/wire/validator_type_check_test.cpp (T006/T010/T011
// real-frame corpus, reused here); tests/codegen/test_078_validator_mixing_us3.cpp
// (header-only-inclusion precedent).

#define FIXPP_VALIDATORS_HEADER_ONLY_PositionReport
#include <fixpp/v44/messages/PositionReport.hpp>
#undef FIXPP_VALIDATORS_HEADER_ONLY_PositionReport

#define FIXPP_VALIDATORS_HEADER_ONLY_TradeCaptureReport
#include <fixpp/v50sp2/messages/TradeCaptureReport.hpp>
#undef FIXPP_VALIDATORS_HEADER_ONLY_TradeCaptureReport

// vlatest's PositionReportArgs / groups live in a distinct `fixpp::vlatest`
// namespace from v44's, so no symbol collision — but the free helper
// functions the header-only body emits (e.g. `PositionReportArgs_required_
// 715`) land in the shared `fixpp::wire` namespace, overloaded on the
// (distinct) Args parameter type. Included un-namespaced, same as the v44 /
// v50sp2 blocks above.
//
// Gate B r1 F4: guarded on FIXPP_TEST_HAS_VLATEST (CMakeLists.txt, set only
// when FIXPP_CODEGEN_FIX_LATEST is ON) — with the tier OFF,
// cmake/Codegen.cmake removes _codegen/include/fixpp/vlatest/ entirely, so
// this #include must not compile unconditionally, but the v44/v50sp2 legs
// below (independent of this tier) must still build+run either way.
#ifdef FIXPP_TEST_HAS_VLATEST
#define FIXPP_VALIDATORS_HEADER_ONLY_PositionReport
#include <fixpp/vlatest/messages/PositionReport.hpp>
#undef FIXPP_VALIDATORS_HEADER_ONLY_PositionReport
#endif  // FIXPP_TEST_HAS_VLATEST

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory_resource>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fixpp/core/decimal_alias.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/orchestra_loader.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fixpp/wire/parser.hpp>
#include <fixpp/wire/validator.hpp>

#include "support/frame_view_factory.hpp"

namespace {

using fixpp::core::error;
using fixpp::decimal_t;
using fixpp::dict::Dictionary;
using fixpp::wire::access_mode;
using fixpp::wire::dictionary_driven_validator;
using fixpp::wire::MessageView;
using fixpp::wire::Parser;

// ── Shared wire-frame helpers (mirrors tests/wire/validator_type_check_test.cpp) ──
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

template <std::size_t N>
MessageView<access_mode::Index> parse_index(std::vector<std::byte> const& buf,
                                            std::array<std::byte, N>& stack_buf,
                                            std::pmr::monotonic_buffer_resource& arena_out) {
    new (&arena_out) std::pmr::monotonic_buffer_resource{stack_buf.data(), stack_buf.size(),
                                                         std::pmr::null_memory_resource()};
    auto fv = fixpp::wire::test::make_frame_view(buf);
    if (!fv.has_value()) {
        ADD_FAILURE() << "make_frame_view failed";
        return {};
    }
    Parser<access_mode::Index> parser{};
    auto mv = parser.parse(*fv, &arena_out);
    if (!mv.has_value()) {
        ADD_FAILURE() << "parser.parse failed";
        return {};
    }
    return std::move(*mv);
}

constexpr std::size_t kScratch = 2048;

Dictionary load_real_dict(char const* file, std::pmr::memory_resource* mr) {
    auto const path = std::filesystem::path{FIXPP_DICT_DATA_DIR} / file;
    return fixpp::dict::XmlLoader{}.load(path, mr);
}

Dictionary load_orchestra_dict(std::pmr::memory_resource* mr) {
    auto const path = std::filesystem::path{FIXPP_ORCHESTRA_DATA_DIR} / "OrchestraFIXLatest.xml";
    return fixpp::dict::OrchestraLoader{}.load(path, mr);
}

decimal_t make_decimal(std::string_view sv, std::pmr::memory_resource* mr) {
    auto bytes =
        std::span<const std::byte>{reinterpret_cast<const std::byte*>(sv.data()), sv.size()};
    auto r = decimal_t::parse(bytes, mr);
    EXPECT_TRUE(r.has_value()) << "make_decimal failed for: " << sv;
    return r.value_or(decimal_t{});
}

// FIX44 AP message-level required prefix (T006/T010's corpus): PosMaintRptID
// (721), PosReqResult(728), ClearingBusinessDate(715), Account(1),
// AccountType(581), SettlPrice(730), SettlPriceType(731), PriorSettlPrice
// (734) + header — NO NoUnderlyings(711) group present here.
std::string fix44_ap_required_prefix() {
    return "35=AP\x01"
           "34=1\x01" "49=SENDER\x01" "52=20240101-00:00:00\x01" "56=TARGET\x01"
           "1=ACCT1\x01" "581=1\x01" "715=20240101\x01" "721=RPT1\x01" "728=0\x01"
           "730=1.5\x01" "731=1\x01" "734=1.4\x01";
}

// Same field values as fix44_ap_required_prefix(), as a typed Args (no
// underlyings group set).
fixpp::v44::PositionReportArgs make_v44_ap_required_args(std::pmr::memory_resource* mr) {
    fixpp::v44::PositionReportArgs args{};
    args.account = "ACCT1";
    args.account_type = 1;
    args.clearing_business_date = "20240101";
    args.pos_maint_rpt_id = "RPT1";
    args.pos_req_result = 0;
    args.settl_price = make_decimal("1.5", mr);
    args.settl_price_type = 1;
    args.prior_settl_price = make_decimal("1.4", mr);
    return args;
}

std::set<std::uint16_t> to_set(std::span<std::uint16_t const> s) {
    return std::set<std::uint16_t>{s.begin(), s.end()};
}

template <typename Args>
std::set<std::uint16_t> typed_required_tags() {
    std::set<std::uint16_t> out;
    for (auto const& rc : fixpp::wire::writer_traits<Args>::required_checks) {
        out.insert(rc.tag);
    }
    return out;
}

}  // namespace

// ══════════════════════════════════════════════════════════════════════════
// v44 — end-to-end full-frame two-tier agreement (PositionReport/AP,
// NoUnderlyings(711): direct required members {732,733}).
// ══════════════════════════════════════════════════════════════════════════

// Conforming (US1/T006): omits the optional NoUnderlyings group entirely.
// Both tiers must ACCEPT.
TEST(RequiredScopeTwoTier, V44PositionReport_OmitsOptionalGroup_BothTiersAccept) {
    // Runtime tier: real wire frame through dictionary_driven_validator.
    std::pmr::monotonic_buffer_resource dict_mr;
    auto d44 = load_real_dict("FIX44.xml", &dict_mr);
    dictionary_driven_validator v{d44.as_table_view()};

    auto buf = make_frame(fix44_ap_required_prefix());
    std::array<std::byte, 4096> stack{};
    std::pmr::monotonic_buffer_resource arena;
    auto mv = parse_index(buf, stack, arena);

    std::array<std::byte, kScratch> scratch_buf{};
    std::pmr::monotonic_buffer_resource scratch_mr{scratch_buf.data(), scratch_buf.size(),
                                                   std::pmr::null_memory_resource()};
    auto runtime_result = v.validate(mv, &scratch_mr, nullptr);

    // Typed tier: equivalent Args, `underlyings` left unset (nullopt).
    std::pmr::monotonic_buffer_resource args_mr{4096};
    auto args = make_v44_ap_required_args(&args_mr);
    auto typed_result = fixpp::v44::validate_PositionReport(args);

    EXPECT_TRUE(runtime_result.has_value())
        << "runtime tier: conforming AP omitting NoUnderlyings must accept";
    EXPECT_TRUE(typed_result.has_value())
        << "typed tier: conforming Args omitting underlyings must accept";
    EXPECT_EQ(runtime_result.has_value(), typed_result.has_value())
        << "two-tier verdict disagreement on conforming AP (Contract 3)";
}

// Malformed (US2/T010): a present NoUnderlyings group whose second instance
// omits the intra-group required UnderlyingSettlPriceType(733). Both tiers
// must REJECT with wire_required_field_missing.
TEST(RequiredScopeTwoTier, V44PositionReport_GroupInstanceMissingRequired_BothTiersReject) {
    // Runtime tier: real wire frame, instance 2 omits 733.
    std::pmr::monotonic_buffer_resource dict_mr;
    auto d44 = load_real_dict("FIX44.xml", &dict_mr);
    dictionary_driven_validator v{d44.as_table_view()};

    auto buf = make_frame(
        fix44_ap_required_prefix() +
        "711=2\x01"
        "311=SYMA\x01" "732=1.1\x01" "733=1\x01"
        "311=SYMB\x01" "732=2.2\x01");
    std::array<std::byte, 4096> stack{};
    std::pmr::monotonic_buffer_resource arena;
    auto mv = parse_index(buf, stack, arena);

    std::array<std::byte, kScratch> scratch_buf{};
    std::pmr::monotonic_buffer_resource scratch_mr{scratch_buf.data(), scratch_buf.size(),
                                                   std::pmr::null_memory_resource()};
    std::uint16_t ref_tag = 0;
    auto runtime_result = v.validate(mv, &scratch_mr, &ref_tag);

    // Typed tier: underlyings span of 2 entries, second missing
    // underlying_settl_price_type (733).
    std::pmr::monotonic_buffer_resource args_mr{4096};
    auto args = make_v44_ap_required_args(&args_mr);
    fixpp::v44::groups::G_711_2Args entry1{};
    entry1.underlying_settl_price = make_decimal("1.1", &args_mr);
    entry1.underlying_settl_price_type = 1;
    fixpp::v44::groups::G_711_2Args entry2{};
    entry2.underlying_settl_price = make_decimal("2.2", &args_mr);
    // entry2.underlying_settl_price_type left unset — the malformed member.
    std::array<fixpp::v44::groups::G_711_2Args, 2> entries{entry1, entry2};
    args.underlyings = std::span<const fixpp::v44::groups::G_711_2Args>{entries};
    auto typed_result = fixpp::v44::validate_PositionReport(args);

    ASSERT_FALSE(runtime_result.has_value())
        << "runtime tier: NoUnderlyings instance omitting required 733 must reject";
    EXPECT_EQ(runtime_result.error(), error::wire_required_field_missing);
    EXPECT_EQ(ref_tag, 733);
    ASSERT_FALSE(typed_result.has_value())
        << "typed tier: underlyings[1] omitting underlying_settl_price_type must reject";
    EXPECT_EQ(typed_result.error(), error::wire_required_field_missing);
    EXPECT_EQ(runtime_result.has_value(), typed_result.has_value())
        << "two-tier verdict disagreement on malformed AP (Contract 3)";
}

// No-false-reject corroboration (US2/T011 mirror): every group instance
// carries all its required members. Both tiers must ACCEPT.
TEST(RequiredScopeTwoTier, V44PositionReport_AllGroupInstancesComplete_BothTiersAccept) {
    std::pmr::monotonic_buffer_resource dict_mr;
    auto d44 = load_real_dict("FIX44.xml", &dict_mr);
    dictionary_driven_validator v{d44.as_table_view()};

    auto buf = make_frame(
        fix44_ap_required_prefix() +
        "711=2\x01"
        "311=SYMA\x01" "732=1.1\x01" "733=1\x01"
        "311=SYMB\x01" "732=2.2\x01" "733=2\x01");
    std::array<std::byte, 4096> stack{};
    std::pmr::monotonic_buffer_resource arena;
    auto mv = parse_index(buf, stack, arena);

    std::array<std::byte, kScratch> scratch_buf{};
    std::pmr::monotonic_buffer_resource scratch_mr{scratch_buf.data(), scratch_buf.size(),
                                                   std::pmr::null_memory_resource()};
    auto runtime_result = v.validate(mv, &scratch_mr, nullptr);

    std::pmr::monotonic_buffer_resource args_mr{4096};
    auto args = make_v44_ap_required_args(&args_mr);
    fixpp::v44::groups::G_711_2Args entry1{};
    entry1.underlying_settl_price = make_decimal("1.1", &args_mr);
    entry1.underlying_settl_price_type = 1;
    fixpp::v44::groups::G_711_2Args entry2{};
    entry2.underlying_settl_price = make_decimal("2.2", &args_mr);
    entry2.underlying_settl_price_type = 2;
    std::array<fixpp::v44::groups::G_711_2Args, 2> entries{entry1, entry2};
    args.underlyings = std::span<const fixpp::v44::groups::G_711_2Args>{entries};
    auto typed_result = fixpp::v44::validate_PositionReport(args);

    EXPECT_TRUE(runtime_result.has_value())
        << "runtime tier: fully-populated NoUnderlyings instances must accept";
    EXPECT_TRUE(typed_result.has_value())
        << "typed tier: fully-populated underlyings entries must accept";
    EXPECT_EQ(runtime_result.has_value(), typed_result.has_value())
        << "two-tier verdict disagreement on fully-populated AP (Contract 3)";
}

// ══════════════════════════════════════════════════════════════════════════
// v50sp2 — derivation-tier agreement (TradeCaptureReport/AE): standalone
// full-frame validate() is BLOCKED by the empty FIXT <header/> (L-041-2 /
// #203). Compare the message-level required-tag SET directly.
// ══════════════════════════════════════════════════════════════════════════

TEST(RequiredScopeTwoTier, V50sp2TradeCaptureReport_DerivationTierAgrees) {
    std::pmr::monotonic_buffer_resource mr;
    auto d502 = load_real_dict("FIX50SP2.xml", &mr);
    auto const tv = d502.as_table_view();

    auto const runtime_set = to_set(tv.required_fields("AE"));
    auto const typed_set = typed_required_tags<fixpp::v50sp2::TradeCaptureReportArgs>();

    EXPECT_FALSE(runtime_set.contains(54))
        << "runtime tier: Side(54) lives inside optional NoSides(552) — must not be "
           "message-level required";
    EXPECT_FALSE(typed_set.contains(54))
        << "typed tier: Side(54) lives inside optional NoSides(552) — must not be "
           "message-level required";
    // Both sides are exactly EMPTY (not merely 54-excluding): AE's own
    // top-level fields are all required='N' (dictionaries/FIX50SP2.xml:2063
    // -2229; its sole message-level-required entity is the `Instrument`
    // component, whose own direct fields are ALL required='N' too — see
    // FIX50SP2.xml:4722-4735), and `<group name='NoSides' required='N'>`
    // itself (FIX50SP2.xml:9047) is not required despite the enclosing
    // `TrdCapRptSideGrp` component usage being required='Y' (so 552 is not
    // promoted the way vlatest's NoPartyIDs(453) is below). Asserted
    // directly so an accidental mis-load returning `{}` for an unrelated
    // reason cannot silently pass this as "agreement".
    EXPECT_TRUE(runtime_set.empty()) << "unexpected runtime AE required set: " << runtime_set.size();
    EXPECT_TRUE(typed_set.empty()) << "unexpected typed AE required set: " << typed_set.size();
    EXPECT_EQ(runtime_set, typed_set)
        << "two-tier message-level required-set disagreement on TradeCaptureReport/AE "
           "(Contract 3 derivation tier)";
}

// ══════════════════════════════════════════════════════════════════════════
// vlatest — derivation-tier agreement (PositionReport/AP): the Orchestra
// PositionReport shape diverges from v44's (own required_checks {715,721};
// NoUnderlyings(711) carries zero required members in this schema) — no
// full-frame corroboration attempted here, per Contract 3 / SC-004.
//
// Gate B r1 F4: guarded on FIXPP_TEST_HAS_VLATEST — see the vlatest #include
// guard above.
// ══════════════════════════════════════════════════════════════════════════

#ifdef FIXPP_TEST_HAS_VLATEST
TEST(RequiredScopeTwoTier, VlatestPositionReport_DerivationTierAgrees) {
    std::pmr::monotonic_buffer_resource mr;
    auto dvlatest = load_orchestra_dict(&mr);
    auto const tv = dvlatest.as_table_view();

    auto const runtime_set = to_set(tv.required_fields("AP"));
    auto const typed_set = typed_required_tags<fixpp::vlatest::PositionReportArgs>();

    // NoUnderlyings(711) direct required members per the FIX44 legacy
    // schema (732/733), and the group's own count tag (711), must not
    // appear at message level on either tier -- the group-scope claim
    // Contract 3 actually makes.
    EXPECT_FALSE(runtime_set.contains(711));
    EXPECT_FALSE(runtime_set.contains(732));
    EXPECT_FALSE(runtime_set.contains(733));
    EXPECT_FALSE(typed_set.contains(711));
    EXPECT_FALSE(typed_set.contains(732));
    EXPECT_FALSE(typed_set.contains(733));

    // Body-scalar agreement: `table_view::required_fields()` also carries
    // (a) StandardHeader/StandardTrailer tags {8,9,10,34,35,49,52,56}
    // (FR-009's own structurally-always-required carve-out -- the Orchestra
    // dict, unlike the QuickFIX FIX50SPx dicts, has a POPULATED header, so
    // these appear here where they didn't for FIX50SP2 above) and (b) tag
    // 453 -- NOT a group MEMBER leak, but the NoPartyIDs(453) group's own
    // count tag, promoted because Parties/NoPartyIDs is itself a REQUIRED
    // group on this message
    // (fixpp::wire::writer_traits<PositionReportArgs>::group_checks[0] ==
    // {true, ...} for party_i_ds). `required_checks` (the typed tier's
    // scalar-required surface, what this test reads) structurally does not
    // carry a required-GROUP's own tag at all -- that dimension is tracked
    // via the untagged `group_checks[i].required` bool instead (codegen
    // emits `party_i_ds` as a plain, non-optional `std::span` for a
    // REQUIRED group vs `std::optional<std::span>` for an optional one, so
    // a default-constructed/never-set span has size()==0 and IS rejected --
    // verified directly below, not merely asserted), a distinct,
    // pre-existing (067/077) representational split orthogonal to 079's
    // group-MEMBER-leak concern (FR-001). Excluding both categories, the
    // remaining body-scalar required sets must agree exactly.
    std::set<std::uint16_t> const kHeaderTrailer{8, 9, 10, 34, 35, 49, 52, 56};
    std::set<std::uint16_t> const kRequiredGroupOwnTag{453};
    std::set<std::uint16_t> filtered_runtime_set;
    for (auto tag : runtime_set) {
        if (!kHeaderTrailer.contains(tag) && !kRequiredGroupOwnTag.contains(tag)) {
            filtered_runtime_set.insert(tag);
        }
    }
    EXPECT_EQ(filtered_runtime_set, typed_set)
        << "two-tier body-scalar required-set disagreement on vlatest PositionReport/AP "
           "(Contract 3 derivation tier)";

    // Behavioral confirmation of the 453 exclusion above: the typed tier
    // DOES enforce the required NoPartyIDs(453) group -- it is absent from
    // `required_checks` (the scalar-tag set compared above) only because
    // that dimension is modeled via `group_checks[0].required==true`
    // instead, NOT because the typed tier under-enforces it relative to
    // the runtime tier. An Args with every OTHER top-level required scalar
    // set but `party_i_ds` left at its default (empty span) must reject.
    fixpp::vlatest::PositionReportArgs args_missing_parties{};
    args_missing_parties.clearing_business_date = "20240101";
    args_missing_parties.pos_maint_rpt_id = "RPT1";
    auto typed_missing_parties_result =
        fixpp::vlatest::validate_PositionReport(args_missing_parties);
    ASSERT_FALSE(typed_missing_parties_result.has_value())
        << "typed tier must reject a missing required NoPartyIDs(453) group -- confirming "
           "the 453 exclusion above is a representational split, not an under-enforcement";
    EXPECT_EQ(typed_missing_parties_result.error(), error::wire_required_field_missing);
}
#endif  // FIXPP_TEST_HAS_VLATEST
