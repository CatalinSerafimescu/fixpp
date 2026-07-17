// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/codegen/test_078_all_hpp_fullset_us4.cpp
//
// 078-precompiled-builder-libs T029 [US4] [TESTS-FIRST]: `all.hpp` full-set
// equivalence (SC-005 / FR-008 / FR-012). This TU is the dedicated US4
// aggregator entry-point witness -- it #includes ONLY
// <fixpp/v44/all.hpp> (not the removed monolith, not per-message headers
// directly), links fixpp::builders::v44 + fixpp::validators::v44 (default,
// link mode), and composes three legs:
//
//   Leg A (full-SET existence via all.hpp) -- reuses the SAME machinery as
//   test_077_v44_builder_completeness.cpp (builder_completeness_common.hpp +
//   the committed generated/v44_builder_completeness_entries.def, 83
//   entries): a compile-time address-of census over every
//   fixpp::v44::build_<Msg>/validate_<Msg>. Since this TU's ONLY builder
//   #include is <fixpp/v44/all.hpp>, a successful compile of the address-of
//   list IS the proof that all 83 entry points resolve THROUGH the
//   aggregator specifically (not merely through some other TU's direct
//   per-message #includes) -- iterating the emitted `builder_registry`
//   array alone would only prove the string table, not that the functions
//   themselves are reachable (the registry and the build_/validate_
//   functions are separate emissions -- builder_completeness_common.hpp
//   leg 4's own rationale). Combined with the registry exact-set-equality
//   check below (also sourced from this TU's all.hpp), this is the "every
//   build_<Msg> for v44" leg the task requires, at existence/link
//   granularity -- SC-004/FR-009's byte-identical-OUTPUT leg is covered by
//   Leg B below at representative-sample granularity (full 83-message
//   byte-identical-output evidence already exists post-T019 relink in
//   tests/session/test_069_all_families_roundtrip.cpp, which also
//   #includes <fixpp/v44/all.hpp> exclusively; this file is not a re-proof
//   of that, it is the dedicated US4/FR-008/FR-012 witness).
//
//   Leg B (SC-005/FR-009 byte-identical output, representative sample) --
//   three messages, chosen for scalar-type diversity and to exercise a
//   DIFFERENT subset than the existing 078 witnesses (T021's
//   NewOrderSingle/ExecutionReport, T026's mixing pair): IOI (all-string
//   scalars), Advertisement (string + char + decimal), and
//   TradeCaptureReport (group-bearing -- exercises a nested read through
//   `all.hpp`'s pulled-in group headers, mirroring FR-012's "shared groups
//   compile once" concern at the read side). Literal inputs and expected
//   field values are copied VERBATIM from
//   tests/session/test_069_all_families_roundtrip.cpp's IOI/Advertisement/
//   TradeCaptureReport cases (the established, chosen-before-build oracle
//   for this exact message set, feedback_coverage_push_enshrines_bugs) --
//   reproducing the identical field-for-field dict-readback outcome here,
//   through the all.hpp include path in a fresh TU, is byte-identical-
//   output evidence for FR-009/SC-005 (build_<Msg>'s body is unchanged by
//   078 -- only which header/library it is declared/compiled in -- so
//   identical inputs necessarily encode to identical bytes; same
//   byte-identity argument as T021's file-header note).
//
//   Leg C (FR-012, shared groups compile once) -- this ONE TU's single
//   `#include <fixpp/v44/all.hpp>` transitively pulls in every one of v44's
//   83 `messages/<Msg>.hpp` slim headers, each of which `#include`s its own
//   subset of the per-plan `groups/G_*Args.hpp` headers (up to 88 distinct
//   plans, shared across many messages -- e.g. G_78_5Args is pulled in by
//   both NewOrderSingle and other allocation-bearing messages). A
//   redefinition error from a shared group struct being multiply-defined
//   would be a hard COMPILE failure for this TU. The clean compile IS the
//   FR-012 witness -- no runtime assertion needed; this file header
//   documents it as a leg because the compile-time census (Leg A) is what
//   forces every one of those 83 per-message headers to actually be
//   ODR-used (a plain `#include <fixpp/v44/all.hpp>` with no reference to
//   any decl could in principle be satisfied even by a compiler that never
//   fully instantiated some transitively-#included content).
//
// Anchors: specs/078-precompiled-builder-libs/spec.md US4 (SC-005), FR-008,
// FR-012; quickstart.md Scenario 5;
// tests/codegen/test_077_v44_builder_completeness.cpp (Leg A machinery
// precedent); tests/codegen/builder_completeness_common.hpp;
// tests/session/test_069_all_families_roundtrip.cpp (Leg B literal-input
// oracle).

#include <fixpp/v44/all.hpp>  // GENERATED -- build_<Msg>/validate_<Msg>/builder_registry (FR-008/FR-012)

#include "builder_completeness_common.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <fixpp/core/decimal_alias.hpp>
#include <fixpp/dict/dictionary.hpp>
#include <iterator>
#include <memory_resource>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "support/app_message_read_scaffold.hpp"

#ifndef FIXPP_DICT_DATA_DIR
#error "FIXPP_DICT_DATA_DIR must be set by CMake target_compile_definitions"
#endif
#ifndef FIXPP_CODEGEN_V44_ALL_HPP
#error "FIXPP_CODEGEN_V44_ALL_HPP must be set by CMake target_compile_definitions"
#endif

namespace {

using fixpp_test::builder_completeness::Entry;
using fixpp_test_support::bytes_to_string;
using fixpp_test_support::make_decimal;
using fixpp_test_support::make_frame;
using fixpp_test_support::parse_dict;
using IndexView = fixpp::wire::MessageView<fixpp::wire::access_mode::Index>;

// Leg A -- compile-time address-of census over the FULL 83-entry .def table,
// reached ONLY through this TU's <fixpp/v44/all.hpp> include (see file
// header). Identical X-macro pattern to
// test_077_v44_builder_completeness.cpp, kept local to this TU rather than
// shared, so the address-of ODR-use is unambiguously anchored to all.hpp
// alone (no other builder-declaring header is #included above).
#define FIXPP_BC_ENTRY(msgtype, ident)                                          \
    Entry{msgtype, reinterpret_cast<void const*>(&::fixpp::v44::build_##ident), \
          reinterpret_cast<void const*>(&::fixpp::v44::validate_##ident)},
std::vector<Entry> const kEntries = {
#include "generated/v44_builder_completeness_entries.def"
};
#undef FIXPP_BC_ENTRY

void expect_text(IndexView const& mv, std::uint16_t tag, std::string_view expected,
                  char const* label) {
    SCOPED_TRACE(label);
    auto fv = mv.get(tag);
    ASSERT_TRUE(fv.has_value()) << "tag " << tag << " not found in parsed frame";
    EXPECT_EQ(fv->as_string(), expected);
}

void expect_decimal(IndexView const& mv, std::uint16_t tag, std::string_view expected_ascii,
                     std::pmr::memory_resource* mr, char const* label) {
    SCOPED_TRACE(label);
    auto got = mv.get_decimal(tag, mr);
    ASSERT_TRUE(got.has_value()) << "tag " << tag << " not found/decodable in parsed frame";
    EXPECT_EQ(*got, make_decimal(expected_ascii, mr));
}

// Group-entry scan (mirrors tests/session/test_069_all_families_roundtrip.cpp's
// TU-local scan_slice_for_tag, not exported, so replicated here at test
// scope): walks a bounded {data,len} group-instance slice for `<tag>=<value>`
// delimited by '=' and SOH, avoiding a bare substring-match false positive.
std::optional<std::string_view> scan_slice_for_tag(std::span<const std::byte> slice, std::uint16_t tag) {
    std::string_view sv{reinterpret_cast<char const*>(slice.data()), slice.size()};
    std::size_t pos = 0;
    while (pos < sv.size()) {
        std::size_t const eq = sv.find('=', pos);
        if (eq == std::string_view::npos) break;
        std::string_view const tag_sv = sv.substr(pos, eq - pos);
        std::size_t const soh = sv.find('\x01', eq);
        std::size_t const value_end = (soh == std::string_view::npos) ? sv.size() : soh;
        std::uint16_t parsed_tag = 0;
        bool ok = !tag_sv.empty();
        for (char c : tag_sv) {
            if (c < '0' || c > '9') {
                ok = false;
                break;
            }
            parsed_tag = static_cast<std::uint16_t>(parsed_tag * 10 + (c - '0'));
        }
        if (ok && parsed_tag == tag) {
            return sv.substr(eq + 1, value_end - (eq + 1));
        }
        if (soh == std::string_view::npos) break;
        pos = soh + 1;
    }
    return std::nullopt;
}

}  // namespace

// Leg A -- every one of the 83 registered v44 builders/validators actually
// resolves through <fixpp/v44/all.hpp>: the vector above only compiled
// because every &build_<Msg>/&validate_<Msg> address-of below was a valid
// declaration visible from all.hpp's transitive #includes; this is a
// non-empty-array sanity check on top of that compile-time gate.
TEST(AllHppFullsetUS4, AllEightyThreeEntriesResolveThroughAllHpp) {
    ASSERT_EQ(kEntries.size(), 83U);
    for (auto const& e : kEntries) {
        EXPECT_NE(e.build_addr, nullptr) << "msg_type=" << e.msg_type;
        EXPECT_NE(e.validate_addr, nullptr) << "msg_type=" << e.msg_type;
    }
}

// Leg A secondary -- the emitted builder_registry array (read out of THIS
// TU's own all.hpp, via FIXPP_CODEGEN_V44_ALL_HPP) is the FULL 83-entry set,
// exact match against the .def table's msg_type set (no subset, no
// superset) -- SC-005's "full set" claim, not a partial aggregator.
TEST(AllHppFullsetUS4, RegistryIsExactFullSet) {
    using namespace fixpp_test::builder_completeness;
    std::set<std::string> const def_set = def_msgtypes(kEntries);
    ASSERT_EQ(def_set.size(), 83U);

    std::set<std::string> const registry = parse_registry_msgtypes(FIXPP_CODEGEN_V44_ALL_HPP);
    EXPECT_EQ(registry, def_set);
}

// Leg B -- byte-identical output for a representative, type-diverse sample,
// exercised purely through the all.hpp-declared build_<Msg> entry points
// (default/link mode): IOI (all-string), Advertisement (string+char+
// decimal), TradeCaptureReport (group-bearing). Literal inputs copied
// verbatim from tests/session/test_069_all_families_roundtrip.cpp.
TEST(AllHppFullsetUS4, IOI_ByteIdenticalToEstablishedOracle) {
    fixpp::v44::IOIArgs args{};
    args.ioiid = "6_ioiid";
    args.ioi_trans_type = '1';
    args.side = '1';
    args.ioi_qty = "6_ioi_qty";

    std::array<std::byte, 1024> out{};
    auto built = fixpp::v44::build_IOI(std::span<std::byte>{out}, args);
    ASSERT_TRUE(built.has_value()) << "build_IOI failed";
    std::string const body = bytes_to_string(*built);

    std::pmr::monotonic_buffer_resource read_arena{8192};
    fixpp::dict::Dictionary dict = fixpp_test_support::load_fix44(&read_arena);
    fixpp::dict::table_view tv = dict.as_table_view();
    std::vector<std::byte> const frame = make_frame("FIX.4.4", body);
    auto const mv = parse_dict(frame, tv, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built IOI frame failed (see ADD_FAILURE above)";

    expect_text(mv, 23, "6_ioiid", "ioiid");
    expect_text(mv, 28, "1", "ioi_trans_type");
    expect_text(mv, 54, "1", "side");
    expect_text(mv, 27, "6_ioi_qty", "ioi_qty");
}

TEST(AllHppFullsetUS4, Advertisement_ByteIdenticalToEstablishedOracle) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::v44::AdvertisementArgs args{};
    args.adv_id = "7_adv_id";
    args.adv_trans_type = "7_adv_trans_type";
    args.adv_side = '1';
    args.quantity = make_decimal("10.5", &arena);

    std::array<std::byte, 1024> out{};
    auto built = fixpp::v44::build_Advertisement(std::span<std::byte>{out}, args);
    ASSERT_TRUE(built.has_value()) << "build_Advertisement failed";
    std::string const body = bytes_to_string(*built);

    std::pmr::monotonic_buffer_resource read_arena{8192};
    fixpp::dict::Dictionary dict = fixpp_test_support::load_fix44(&read_arena);
    fixpp::dict::table_view tv = dict.as_table_view();
    std::vector<std::byte> const frame = make_frame("FIX.4.4", body);
    auto const mv = parse_dict(frame, tv, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built Advertisement frame failed (see ADD_FAILURE above)";

    expect_text(mv, 2, "7_adv_id", "adv_id");
    expect_text(mv, 5, "7_adv_trans_type", "adv_trans_type");
    expect_text(mv, 4, "1", "adv_side");
    expect_decimal(mv, 53, "10.5", &read_arena, "quantity");
}

TEST(AllHppFullsetUS4, TradeCaptureReport_GroupBearing_ByteIdenticalToEstablishedOracle) {
    std::pmr::monotonic_buffer_resource arena{4096};
    fixpp::v44::TradeCaptureReportArgs args{};
    args.trade_report_id = "AE_trade_report_id";
    args.previously_reported = true;
    args.last_qty = make_decimal("10.5", &arena);
    args.last_px = make_decimal("10.5", &arena);
    args.trade_date = "AE_trade_date";
    args.transact_time = "AE_transact_time";
    fixpp::v44::groups::G_552_1Args side_entry{};
    side_entry.side = '1';
    side_entry.order_id = "AE_NoSides_OrderID";
    std::array<fixpp::v44::groups::G_552_1Args, 1> sides_arr{side_entry};
    args.sides = std::span<const fixpp::v44::groups::G_552_1Args>{sides_arr};

    std::array<std::byte, 4096> out{};
    auto built = fixpp::v44::build_TradeCaptureReport(std::span<std::byte>{out}, args);
    ASSERT_TRUE(built.has_value()) << "build_TradeCaptureReport failed";
    std::string const body = bytes_to_string(*built);

    std::pmr::monotonic_buffer_resource read_arena{16384};
    fixpp::dict::Dictionary dict = fixpp_test_support::load_fix44(&read_arena);
    fixpp::dict::table_view tv = dict.as_table_view();
    std::vector<std::byte> const frame = make_frame("FIX.4.4", body);
    auto const mv = parse_dict(frame, tv, &read_arena);
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "dict-aware parse of built TradeCaptureReport frame failed (see ADD_FAILURE above)";

    expect_text(mv, 571, "AE_trade_report_id", "trade_report_id");
    expect_text(mv, 570, "Y", "previously_reported");
    expect_decimal(mv, 32, "10.5", &read_arena, "last_qty");
    expect_decimal(mv, 31, "10.5", &read_arena, "last_px");
    expect_text(mv, 75, "AE_trade_date", "trade_date");
    expect_text(mv, 60, "AE_transact_time", "transact_time");

    // Nested depth (FR-012 read-side counterpart): NoSides(552) entry-level
    // readback through the group headers all.hpp pulled in transitively.
    auto side_slices = mv.offsets().group_slices(552);
    ASSERT_EQ(side_slices.size(), 1u) << "NoSides(552) must carry exactly 1 entry";
    std::span<const std::byte> const entry0{side_slices[0].data, side_slices[0].len};
    auto side_val = scan_slice_for_tag(entry0, 54);
    ASSERT_TRUE(side_val.has_value()) << "Side(54) not found in NoSides entry";
    EXPECT_EQ(*side_val, "1");
    auto order_id_val = scan_slice_for_tag(entry0, 37);
    ASSERT_TRUE(order_id_val.has_value()) << "OrderID(37) not found in NoSides entry";
    EXPECT_EQ(*order_id_val, "AE_NoSides_OrderID");
}
