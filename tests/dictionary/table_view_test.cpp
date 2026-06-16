// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/dictionary/table_view_test.cpp
//
// T007 (RED → T008 GREEN): Dictionary::as_table_view() produces a table_view
// whose 5 dictionary-backed methods agree with the source Dictionary for
// representative msg types / tags / groups, and whose enum_valid is always
// true (C-1, 041-validation-gate-wiring/contracts/validation-gate.md).
//
// Anchors:
//   spec: 041-validation-gate-wiring/spec.md FR-003/FR-006
//   data-model: E-2 / E-3 (table_view surface + as_table_view() builder)
//   research: R-1 / R-1a (global tag→field_type map, union-across-messages)
//   contracts: C-1

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/field_ref.hpp>
#include <fixpp/dict/field_type.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <memory_resource>
#include <span>
#include <string>
#include <vector>

namespace {

// Load a small FIX44 dictionary from an inline XML string.
fixpp::dict::Dictionary load_test_dictionary(std::pmr::memory_resource* mr) {
    // Minimal FIX 4.4 dictionary with:
    //   - Logon (A): required fields 49, 56, 98, 108
    //   - NewOrderSingle (D): required 11, 21, 54, 55; optional 38 (Float), 40 (Char)
    //   - Execution Report (8): optional group NoContraBrokers(382)/375
    constexpr std::string_view kXml =
        R"(<fix type='FIX' major='4' minor='4' servicepack='0'>)"
        R"(<fields>)"
        R"(<field number='8'   name='BeginString'      type='STRING'/>)"
        R"(<field number='9'   name='BodyLength'       type='LENGTH'/>)"
        R"(<field number='10'  name='CheckSum'         type='STRING'/>)"
        R"(<field number='11'  name='ClOrdID'          type='STRING'/>)"
        R"(<field number='21'  name='HandlInst'        type='CHAR'/>)"
        R"(<field number='35'  name='MsgType'          type='STRING'/>)"
        R"(<field number='38'  name='OrderQty'         type='QTY'/>)"
        R"(<field number='40'  name='OrdType'          type='CHAR'/>)"
        R"(<field number='49'  name='SenderCompID'     type='STRING'/>)"
        R"(<field number='54'  name='Side'             type='CHAR'/>)"
        R"(<field number='55'  name='Symbol'           type='STRING'/>)"
        R"(<field number='56'  name='TargetCompID'     type='STRING'/>)"
        R"(<field number='98'  name='EncryptMethod'    type='INT'/>)"
        R"(<field number='108' name='HeartBtInt'       type='INT'/>)"
        R"(<field number='375' name='ContraBroker'     type='STRING'/>)"
        R"(<field number='382' name='NoContraBrokers'  type='NUMINGROUP'/>)"
        R"(</fields>)"
        R"(<messages>)"
        R"(<message name='Logon' msgtype='A' msgcat='admin'>)"
        R"(  <field name='BeginString'  required='N'/>)"
        R"(  <field name='BodyLength'   required='N'/>)"
        R"(  <field name='CheckSum'     required='N'/>)"
        R"(  <field name='MsgType'      required='N'/>)"
        R"(  <field name='SenderCompID' required='Y'/>)"
        R"(  <field name='TargetCompID' required='Y'/>)"
        R"(  <field name='EncryptMethod' required='Y'/>)"
        R"(  <field name='HeartBtInt'   required='Y'/>)"
        R"(</message>)"
        R"(<message name='NewOrderSingle' msgtype='D' msgcat='app'>)"
        R"(  <field name='BeginString'  required='N'/>)"
        R"(  <field name='BodyLength'   required='N'/>)"
        R"(  <field name='CheckSum'     required='N'/>)"
        R"(  <field name='MsgType'      required='N'/>)"
        R"(  <field name='ClOrdID'      required='Y'/>)"
        R"(  <field name='HandlInst'    required='Y'/>)"
        R"(  <field name='Side'         required='Y'/>)"
        R"(  <field name='Symbol'       required='Y'/>)"
        R"(  <field name='OrderQty'     required='N'/>)"
        R"(  <field name='OrdType'      required='N'/>)"
        R"(  <group name='NoContraBrokers' required='N'>)"
        R"(    <field name='ContraBroker' required='N'/>)"
        R"(  </group>)"
        R"(</message>)"
        R"(</messages>)"
        R"(</fix>)";
    return fixpp::dict::XmlLoader{}.load_from_string(kXml, mr);
}

}  // namespace

// ── T007-1: field_valid_for agrees with source Dictionary ───────────────────

TEST(TableViewTest, FieldValidForAgreesWithDictionary) {
    std::vector<std::byte> buf(2u * 1024u * 1024u);
    std::pmr::monotonic_buffer_resource mr{buf.data(), buf.size()};
    auto dict = load_test_dictionary(&mr);
    auto tv = dict.as_table_view();

    // Tags declared for Logon ("A") must be valid.
    EXPECT_TRUE(tv.field_valid_for("A", 49))   // SenderCompID
        << "SenderCompID must be valid for Logon";
    EXPECT_TRUE(tv.field_valid_for("A", 56))   // TargetCompID
        << "TargetCompID must be valid for Logon";
    EXPECT_TRUE(tv.field_valid_for("A", 98))   // EncryptMethod
        << "EncryptMethod must be valid for Logon";

    // Tags declared for NewOrderSingle ("D") must be valid.
    EXPECT_TRUE(tv.field_valid_for("D", 11))   // ClOrdID
        << "ClOrdID must be valid for NewOrderSingle";
    EXPECT_TRUE(tv.field_valid_for("D", 38))   // OrderQty (Float)
        << "OrderQty must be valid for NewOrderSingle";
    EXPECT_TRUE(tv.field_valid_for("D", 40))   // OrdType (Char)
        << "OrdType must be valid for NewOrderSingle";

    // Tags NOT declared for a msg_type must be invalid.
    EXPECT_FALSE(tv.field_valid_for("A", 11))   // ClOrdID not on Logon
        << "ClOrdID must NOT be valid for Logon";
    EXPECT_FALSE(tv.field_valid_for("D", 9999)) // unknown tag
        << "unknown tag must NOT be valid";
    EXPECT_FALSE(tv.field_valid_for("Z", 49))   // unknown msg_type
        << "known tag on unknown msg_type must NOT be valid";
}

// ── T007-2: required_fields agrees with source Dictionary ───────────────────

TEST(TableViewTest, RequiredFieldsAgreesWithDictionary) {
    std::vector<std::byte> buf(2u * 1024u * 1024u);
    std::pmr::monotonic_buffer_resource mr{buf.data(), buf.size()};
    auto dict = load_test_dictionary(&mr);
    auto tv = dict.as_table_view();

    // Logon required: 49, 56, 98, 108.
    auto logon_req = tv.required_fields("A");
    ASSERT_FALSE(logon_req.empty()) << "Logon required_fields must not be empty";
    bool has_49 = false, has_56 = false, has_98 = false, has_108 = false;
    for (auto t : logon_req) {
        if (t == 49)  has_49  = true;
        if (t == 56)  has_56  = true;
        if (t == 98)  has_98  = true;
        if (t == 108) has_108 = true;
    }
    EXPECT_TRUE(has_49)  << "SenderCompID(49) must be in Logon required_fields";
    EXPECT_TRUE(has_56)  << "TargetCompID(56) must be in Logon required_fields";
    EXPECT_TRUE(has_98)  << "EncryptMethod(98) must be in Logon required_fields";
    EXPECT_TRUE(has_108) << "HeartBtInt(108) must be in Logon required_fields";

    // NewOrderSingle required: 11, 21, 54, 55.
    auto nos_req = tv.required_fields("D");
    ASSERT_FALSE(nos_req.empty()) << "NewOrderSingle required_fields must not be empty";
    bool has_11 = false, has_21 = false, has_54 = false, has_55 = false;
    for (auto t : nos_req) {
        if (t == 11) has_11 = true;
        if (t == 21) has_21 = true;
        if (t == 54) has_54 = true;
        if (t == 55) has_55 = true;
    }
    EXPECT_TRUE(has_11) << "ClOrdID(11) must be in NewOrderSingle required_fields";
    EXPECT_TRUE(has_21) << "HandlInst(21) must be in NewOrderSingle required_fields";
    EXPECT_TRUE(has_54) << "Side(54) must be in NewOrderSingle required_fields";
    EXPECT_TRUE(has_55) << "Symbol(55) must be in NewOrderSingle required_fields";

    // OrderQty (38) is optional — must NOT appear in required_fields("D").
    bool has_38 = false;
    for (auto t : nos_req) {
        if (t == 38) has_38 = true;
    }
    EXPECT_FALSE(has_38) << "OrderQty(38) is optional; must NOT be in required_fields(D)";

    // Unknown msg_type → empty span.
    EXPECT_TRUE(tv.required_fields("Z").empty())
        << "required_fields for unknown msg_type must be empty";
}

// ── T007-3: group_first_field agrees with source Dictionary ─────────────────

TEST(TableViewTest, GroupFirstFieldAgreesWithDictionary) {
    std::vector<std::byte> buf(2u * 1024u * 1024u);
    std::pmr::monotonic_buffer_resource mr{buf.data(), buf.size()};
    auto dict = load_test_dictionary(&mr);
    auto tv = dict.as_table_view();

    // NoContraBrokers(382) → first field = ContraBroker(375).
    std::uint16_t const first = tv.group_first_field(382);
    EXPECT_EQ(first, std::uint16_t{375})
        << "group_first_field(NoContraBrokers=382) must be ContraBroker(375)";

    // Unknown no_tag → 0.
    EXPECT_EQ(tv.group_first_field(9999), std::uint16_t{0})
        << "group_first_field for unknown no_tag must be 0";
}

// ── T007-4: group_member_tags agrees with source Dictionary ─────────────────

TEST(TableViewTest, GroupMemberTagsAgreesWithDictionary) {
    std::vector<std::byte> buf(2u * 1024u * 1024u);
    std::pmr::monotonic_buffer_resource mr{buf.data(), buf.size()};
    auto dict = load_test_dictionary(&mr);
    auto tv = dict.as_table_view();

    // NoContraBrokers(382) has member ContraBroker(375).
    auto members = tv.group_member_tags(382);
    ASSERT_FALSE(members.empty())
        << "group_member_tags(382) must not be empty";
    bool has_375 = false;
    for (auto t : members) {
        if (t == 375) has_375 = true;
    }
    EXPECT_TRUE(has_375)
        << "ContraBroker(375) must be in group_member_tags(NoContraBrokers=382)";

    // Unknown no_tag → empty span.
    EXPECT_TRUE(tv.group_member_tags(9999).empty())
        << "group_member_tags for unknown no_tag must be empty";
}

// ── T007-5: field_type_of agrees with source Dictionary field_data_type ──────

TEST(TableViewTest, FieldTypeOfAgreesWithDictionary) {
    std::vector<std::byte> buf(2u * 1024u * 1024u);
    std::pmr::monotonic_buffer_resource mr{buf.data(), buf.size()};
    auto dict = load_test_dictionary(&mr);
    auto tv = dict.as_table_view();

    // ClOrdID(11) → STRING → field_type::String
    EXPECT_EQ(tv.field_type_of(11), fixpp::dict::field_type::String)
        << "ClOrdID(11) is STRING → field_type::String";

    // EncryptMethod(98) → INT → field_type::Int
    EXPECT_EQ(tv.field_type_of(98), fixpp::dict::field_type::Int)
        << "EncryptMethod(98) is INT → field_type::Int";

    // HeartBtInt(108) → INT → field_type::Int
    EXPECT_EQ(tv.field_type_of(108), fixpp::dict::field_type::Int)
        << "HeartBtInt(108) is INT → field_type::Int";

    // OrderQty(38) → QTY → field_type::Float (R-1a: Price/Qty/Amt → Float)
    EXPECT_EQ(tv.field_type_of(38), fixpp::dict::field_type::Float)
        << "OrderQty(38) is QTY → field_type::Float";

    // OrdType(40) → CHAR → field_type::Char
    EXPECT_EQ(tv.field_type_of(40), fixpp::dict::field_type::Char)
        << "OrdType(40) is CHAR → field_type::Char";

    // HandlInst(21) → CHAR → field_type::Char
    EXPECT_EQ(tv.field_type_of(21), fixpp::dict::field_type::Char)
        << "HandlInst(21) is CHAR → field_type::Char";

    // BodyLength(9) → LENGTH → field_type::Length
    EXPECT_EQ(tv.field_type_of(9), fixpp::dict::field_type::Length)
        << "BodyLength(9) is LENGTH → field_type::Length";

    // Unknown tag → String (safe default).
    EXPECT_EQ(tv.field_type_of(9999), fixpp::dict::field_type::String)
        << "unknown tag must default to field_type::String";
}

// ── T007-6: enum_valid always true (Phase-1 / FR-005) ───────────────────────

TEST(TableViewTest, EnumValidAlwaysTrue) {
    std::vector<std::byte> buf(2u * 1024u * 1024u);
    std::pmr::monotonic_buffer_resource mr{buf.data(), buf.size()};
    auto dict = load_test_dictionary(&mr);
    auto tv = dict.as_table_view();

    // enum_valid must return true for any tag and any value (Phase-1 stub).
    std::array<std::byte, 1> val{std::byte{0}};
    EXPECT_TRUE(tv.enum_valid(54, std::span<const std::byte>{val.data(), val.size()}))
        << "enum_valid must always return true (Phase-1)";
    EXPECT_TRUE(tv.enum_valid(9999, std::span<const std::byte>{}))
        << "enum_valid must always return true for any tag";
}

// ── T007-7: spans remain valid (table_view owns its storage) ────────────────

TEST(TableViewTest, SpansRemainingValidAfterDictionaryDestroyed) {
    // Build the table_view, then destroy the dictionary; spans must still work.
    fixpp::dict::table_view tv;
    {
        std::vector<std::byte> buf(2u * 1024u * 1024u);
        std::pmr::monotonic_buffer_resource mr{buf.data(), buf.size()};
        auto dict = load_test_dictionary(&mr);
        tv = dict.as_table_view();
        // dict goes out of scope here (dictionary destroyed).
    }
    // Must still be usable.
    EXPECT_FALSE(tv.required_fields("A").empty())
        << "required_fields span must remain valid after Dictionary is destroyed";
    EXPECT_TRUE(tv.field_valid_for("A", 49))
        << "field_valid_for must work after Dictionary is destroyed";
}

// ── T007-8: full FIX44 dictionary as_table_view() round-trip ────────────────

TEST(TableViewTest, FullFix44DictionaryTableView) {
    std::vector<std::byte> buf(4u * 1024u * 1024u);
    std::pmr::monotonic_buffer_resource mr{buf.data(), buf.size()};
    auto const xml_path =
        std::filesystem::path{FIXPP_DICT_DATA_DIR} / "FIX44.xml";
    auto dict = fixpp::dict::XmlLoader{}.load(xml_path, &mr);
    auto tv = dict.as_table_view();

    // Logon (A) → SenderCompID(49) valid + required.
    EXPECT_TRUE(tv.field_valid_for("A", 49))
        << "SenderCompID must be valid for Logon in FIX44";
    auto logon_req = tv.required_fields("A");
    bool has_sender = false;
    for (auto t : logon_req) {
        if (t == 49) has_sender = true;
    }
    EXPECT_TRUE(has_sender)
        << "SenderCompID(49) must be required for Logon in FIX44";

    // NewOrderSingle (D) exists.
    EXPECT_TRUE(tv.field_valid_for("D", 11))
        << "ClOrdID must be valid for NewOrderSingle in FIX44";

    // NoPartyIDs(453) is a group in FIX44 — first field should be non-zero.
    auto const party_first = tv.group_first_field(453);
    EXPECT_NE(party_first, std::uint16_t{0})
        << "group_first_field(NoPartyIDs=453) must be non-zero in FIX44";

    // group_member_tags(453) must include the first-field tag.
    auto party_members = tv.group_member_tags(453);
    ASSERT_FALSE(party_members.empty())
        << "group_member_tags(NoPartyIDs=453) must not be empty in FIX44";
    bool has_first = false;
    for (auto t : party_members) {
        if (t == party_first) has_first = true;
    }
    EXPECT_TRUE(has_first)
        << "group_member_tags must contain the group_first_field tag";
}

// ── T005 Gate A P3-b: UtcTimestamp collapses to String ──────────────────────
// Pin the decision made in Gate A P3-b: UtcTimestamp (and all UTC* variants)
// map to field_type::String.  This means a malformed SendingTime(52) value
// is not catchable by the Phase-1 type-check arm — by design (the SendingTime
// guard in 038 already handles it before validation runs).
// Anchor: 041-validation-gate-wiring/plan.md Gate A P3-b.
TEST(TableViewTest, FieldTypeFromDataTypeUtcTimestampMapsToString) {
    using fixpp::dict::field_type;
    using fixpp::dict::field_data_type;

    EXPECT_EQ(field_type_from_data_type(field_data_type::UtcTimestamp), field_type::String)
        << "UtcTimestamp must map to String (Gate A P3-b pin)";
    EXPECT_EQ(field_type_from_data_type(field_data_type::UtcTimeOnly), field_type::String)
        << "UtcTimeOnly must map to String";
    EXPECT_EQ(field_type_from_data_type(field_data_type::UtcDateOnly), field_type::String)
        << "UtcDateOnly must map to String";
}
