// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/required_scope_test.cpp
//
// fixpp#201 — the MESSAGE-level required-field set must be TOP-LEVEL scoped:
// a field that is `required='Y'` only INSIDE an (optional) repeating group must
// NOT appear in `Dictionary::required_fields(msg_type)`. QuickFIX composes
// message-level required-ness top-level-only and checks group members per
// instance; before this fix the loader leaked group-member requireds upward,
// false-rejecting conforming messages that omit the optional group.
//
// RED before the loader fix (xml_loader.cpp / orchestra_loader.cpp
// `expand_field_list` in_group suppression); GREEN after. The reproduction
// probe is research/G19-fix-fpml-iso20022/fable-assessments/repro/
// repro_required_scope.cpp — these pins encode the same numbers as unit tests.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <memory_resource>
#include <span>

namespace {

fixpp::dict::Dictionary load_dict(char const* file, std::pmr::memory_resource* mr) {
    auto const path = std::filesystem::path{FIXPP_DICT_DATA_DIR} / file;
    return fixpp::dict::XmlLoader{}.load(path, mr);
}

bool contains(std::span<std::uint16_t const> s, std::uint16_t t) {
    return std::find(s.begin(), s.end(), t) != s.end();
}

// FIX44 PositionReport(AP): UnderlyingSettlPrice(732) / UnderlyingSettlPriceType
// (733) are `required='Y'` inside PosUndInstrmtGrp (NoUnderlyings, optional).
// They must NOT be message-level required.
TEST(RequiredScope, Fix44PositionReportExcludesOptionalGroupRequireds) {
    std::pmr::monotonic_buffer_resource mr;  // heap upstream (avoids MiB-scale stack array)
    auto d44 = load_dict("FIX44.xml", &mr);

    auto const ap = d44.required_fields("AP");
    EXPECT_FALSE(contains(ap, 732)) << "732 lives only inside optional NoUnderlyings — leaked";
    EXPECT_FALSE(contains(ap, 733)) << "733 lives only inside optional NoUnderlyings — leaked";

    // Guard against over-removal: genuine TOP-LEVEL requireds must remain.
    EXPECT_TRUE(contains(ap, 581)) << "AccountType(581) is a real top-level required";
    EXPECT_TRUE(contains(ap, 715)) << "ClearingBusinessDate(715) is a real top-level required";
}

// CONTROL (QuickFIX AND-rule parity): Symbol(55) is `required='N'` inside the
// Instrument component on NewOrderSingle(D), so it is NOT message-level
// required. This is correct TODAY and must STAY unchanged (no over-correction).
TEST(RequiredScope, Fix44NewOrderSingleControlSymbolNotRequired) {
    std::pmr::monotonic_buffer_resource mr;  // heap upstream (avoids MiB-scale stack array)
    auto d44 = load_dict("FIX44.xml", &mr);

    auto const d = d44.required_fields("D");
    EXPECT_FALSE(contains(d, 55)) << "control: Symbol(55) must not become required (over-correct)";
    // A genuine top-level required stays.
    EXPECT_TRUE(contains(d, 11)) << "ClOrdID(11) is a real top-level required on D";
}

// FIX50SP2 TradeCaptureReport(AE): Side(54) is `required='Y'` inside the
// optional NoSides group — must NOT be message-level required.
TEST(RequiredScope, Fix50sp2TradeCaptureReportExcludesOptionalGroupRequired) {
    std::pmr::monotonic_buffer_resource mr;  // heap upstream (avoids MiB-scale stack array)
    auto d502 = load_dict("FIX50SP2.xml", &mr);

    auto const ae = d502.required_fields("AE");
    EXPECT_FALSE(contains(ae, 54)) << "Side(54) lives inside optional NoSides — leaked";
}

// The per-instance validator check reads the CONTEXT-scoped required-member
// store built by as_table_view(). Pin it directly on a real dictionary (the
// synthetic validator pins exercise only the bare fallback): NoUnderlyings(711)
// on FIX44 AP must carry 732/733 as DIRECT required members.
TEST(RequiredScope, Fix44AsTableViewContextRequiredMembers) {
    std::pmr::monotonic_buffer_resource mr;
    auto d44 = load_dict("FIX44.xml", &mr);
    auto const tv = d44.as_table_view();

    auto const req = tv.group_required_members("AP", std::span<std::uint16_t const>{}, 711);
    EXPECT_TRUE(contains(req, 732)) << "ctx store: 732 required per NoUnderlyings instance";
    EXPECT_TRUE(contains(req, 733)) << "ctx store: 733 required per NoUnderlyings instance";
    // Sanity: the group's DELIMITER (UnderlyingSymbol 311, required='N') is NOT
    // a required member, so a per-instance check does not over-require it.
    EXPECT_FALSE(contains(req, 311)) << "delimiter is optional — must not be a required member";
}

// 079 T008 boundary pin (discovered during /implement Phase 3/4 real-frame
// test construction — see tests/wire/validator_type_check_test.cpp's
// T010/T011 FIX42 escalation comment for the full analysis): FIX42.xml
// declares group-count fields (e.g. NoAllocs/78 on Allocation(J)) with
// `type='INT'`, not `type='NUMINGROUP'`. The context-scoped per-group store
// (dictionary.cpp's group-population loop) keys on
// `field_data_type::NumInGroup`, so it NEVER populates a context entry for
// ANY FIX42 group — `group_first_field(msg_type, path, no_tag)` returns 0
// even for a real, directly-declared, non-nested FIX42 group, though the
// BARE global `Dictionary::group_first_field(no_tag)` (a type-independent
// `<group>`-element scan) resolves correctly. This is the documented,
// already-tracked L-066-1 limitation ("FIX 4.0/4.1/4.2 sessions become
// strict-but-GROUP-BLIND under dict validation"), deferred to issue #196 —
// pinned here so a future fix to #196 flips this assertion (intentionally,
// not silently).
TEST(RequiredScope, Fix42GroupCountFieldIsIntTypedContextStoreBlindL0661) {
    std::pmr::monotonic_buffer_resource mr;
    auto d42 = load_dict("FIX42.xml", &mr);
    auto const tv = d42.as_table_view();

    // NoAllocs(78) on Allocation(J): FieldRef.type is Int (not NumInGroup).
    auto const j_fields = d42.message_fields("J");
    auto const it = std::find_if(j_fields.begin(), j_fields.end(),
                                 [](auto const& fr) { return fr.tag == 78; });
    ASSERT_NE(it, j_fields.end()) << "NoAllocs(78) must appear in J's field expansion";
    EXPECT_EQ(it->type, fixpp::dict::field_data_type::Int)
        << "FIX42 NoAllocs(78) is INT-typed, not NUMINGROUP — the L-066-1 root cause";

    // Bare global accessor (type-independent <group>-element scan) DOES resolve.
    EXPECT_EQ(d42.group_first_field(78), 79)
        << "bare global group_first_field resolves via <group> element, not field type";

    // Context-scoped store (keyed on NumInGroup type detection) does NOT.
    EXPECT_EQ(tv.group_first_field("J", std::span<std::uint16_t const>{}, 78), 0U)
        << "L-066-1: context store is group-blind for FIX42 (INT-typed count field) — "
           "if this ever resolves to 79, issue #196 has landed and this pin should be "
           "updated/removed";
}

}  // namespace
