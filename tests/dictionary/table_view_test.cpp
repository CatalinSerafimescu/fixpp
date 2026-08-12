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
    EXPECT_TRUE(tv.field_valid_for("A", 49))  // SenderCompID
        << "SenderCompID must be valid for Logon";
    EXPECT_TRUE(tv.field_valid_for("A", 56))  // TargetCompID
        << "TargetCompID must be valid for Logon";
    EXPECT_TRUE(tv.field_valid_for("A", 98))  // EncryptMethod
        << "EncryptMethod must be valid for Logon";

    // Tags declared for NewOrderSingle ("D") must be valid.
    EXPECT_TRUE(tv.field_valid_for("D", 11))  // ClOrdID
        << "ClOrdID must be valid for NewOrderSingle";
    EXPECT_TRUE(tv.field_valid_for("D", 38))  // OrderQty (Float)
        << "OrderQty must be valid for NewOrderSingle";
    EXPECT_TRUE(tv.field_valid_for("D", 40))  // OrdType (Char)
        << "OrdType must be valid for NewOrderSingle";

    // Tags NOT declared for a msg_type must be invalid.
    EXPECT_FALSE(tv.field_valid_for("A", 11))  // ClOrdID not on Logon
        << "ClOrdID must NOT be valid for Logon";
    EXPECT_FALSE(tv.field_valid_for("D", 9999))  // unknown tag
        << "unknown tag must NOT be valid";
    EXPECT_FALSE(tv.field_valid_for("Z", 49))  // unknown msg_type
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
        if (t == 49) has_49 = true;
        if (t == 56) has_56 = true;
        if (t == 98) has_98 = true;
        if (t == 108) has_108 = true;
    }
    EXPECT_TRUE(has_49) << "SenderCompID(49) must be in Logon required_fields";
    EXPECT_TRUE(has_56) << "TargetCompID(56) must be in Logon required_fields";
    EXPECT_TRUE(has_98) << "EncryptMethod(98) must be in Logon required_fields";
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

// ── Gate B r1 (PR #194 FIX 1.a): valid_tags_for direct unit witness ─────────
// valid_tags_for is the validator's per-message hoist (validator.hpp Step 1):
// a known msg_type must yield a view whose contains() agrees with
// field_valid_for, and an unknown msg_type must yield a view whose
// contains() is false for every tag (the [FIX 2] valid_tag_set_view
// encapsulation's nullptr-equivalent state).
TEST(TableViewTest, ValidTagsForAgreesWithFieldValidFor) {
    std::vector<std::byte> buf(2u * 1024u * 1024u);
    std::pmr::monotonic_buffer_resource mr{buf.data(), buf.size()};
    auto dict = load_test_dictionary(&mr);
    auto tv = dict.as_table_view();

    // Known msg_type: view must agree with field_valid_for for both a
    // declared tag and an undeclared one.
    auto const logon_view = tv.valid_tags_for("A");
    EXPECT_TRUE(logon_view.contains(49)) << "SenderCompID(49) must be in Logon's valid_tags_for view";
    EXPECT_TRUE(logon_view.contains(98)) << "EncryptMethod(98) must be in Logon's valid_tags_for view";
    EXPECT_FALSE(logon_view.contains(11))
        << "ClOrdID(11) is not declared for Logon; must NOT be in the view";
    EXPECT_FALSE(logon_view.contains(9999)) << "unknown tag must NOT be in the view";

    // Unknown msg_type: view must reject every tag, matching
    // field_valid_for("Z", tag) == false for every tag.
    auto const unknown_view = tv.valid_tags_for("Z");
    EXPECT_FALSE(unknown_view.contains(49))
        << "unknown msg_type's view must reject a tag that IS valid for another msg_type";
    EXPECT_FALSE(unknown_view.contains(9999)) << "unknown msg_type's view must reject any tag";
}

// ── T007-4: group_member_tags agrees with source Dictionary ─────────────────

TEST(TableViewTest, GroupMemberTagsAgreesWithDictionary) {
    std::vector<std::byte> buf(2u * 1024u * 1024u);
    std::pmr::monotonic_buffer_resource mr{buf.data(), buf.size()};
    auto dict = load_test_dictionary(&mr);
    auto tv = dict.as_table_view();

    // NoContraBrokers(382) has member ContraBroker(375).
    auto members = tv.group_member_tags(382);
    ASSERT_FALSE(members.empty()) << "group_member_tags(382) must not be empty";
    bool has_375 = false;
    for (auto t : members) {
        if (t == 375) has_375 = true;
    }
    EXPECT_TRUE(has_375) << "ContraBroker(375) must be in group_member_tags(NoContraBrokers=382)";

    // Unknown no_tag → empty span.
    EXPECT_TRUE(tv.group_member_tags(9999).empty())
        << "group_member_tags for unknown no_tag must be empty";
}

// ── Gate B r1 (PR #194 FIX 1.b): group presence-bit counter-tests ──────────
// `group_bit`/`set_group_bit` gate the 3-arg (msg_type, parent_path, no_tag)
// context-aware accessors ONLY (validator.hpp Step 3 / the parser's
// group_member_fn_t); the 1-arg bare accessors never consult the bit. A
// no_tag registered EXCLUSIVELY via the context-scoped population surface
// (set_group_first_ctx/add_group_member_ctx — no bare set_group_first/
// add_group_member call at all) must still be found through the 3-arg
// accessors, proving the bit is set on the context-only population path
// (mirrors the context-only fixtures in
// tests/wire/nested_group_extent_test.cpp::BenignSameMembershipReuseAcrossContexts
// and tests/codegen/nested_group_read_test.cpp).
TEST(TableViewTest, GroupContextOnlyRegistrationFoundThroughContextAccessor) {
    fixpp::dict::table_view tv;
    std::array<std::uint16_t, 1> const parent_path{453};
    // Context-scoped ONLY — no add_group_member(802, ...) / set_group_first(802, ...).
    tv.set_group_first_ctx("D", parent_path, 802, 523);
    tv.add_group_member_ctx("D", parent_path, 802, 524);

    // The context-aware 3-arg overloads must resolve this no_tag under the
    // exact (msg_type, parent_path) it was registered with — proving the
    // presence bit is SET on this population path.
    EXPECT_EQ(tv.group_first_field("D", parent_path, 802), std::uint16_t{523})
        << "context-only registration must be visible through the 3-arg accessor "
           "(the bit gate must not have suppressed it)";
    auto const members = tv.group_member_tags("D", parent_path, 802);
    ASSERT_FALSE(members.empty()) << "context-only members must be visible through the 3-arg accessor";
    bool has_524 = false;
    for (auto t : members) {
        if (t == 524) has_524 = true;
    }
    EXPECT_TRUE(has_524) << "member 524 registered via add_group_member_ctx must be present";

    // The BARE (1-arg) legacy store was never populated for 802 — the bare
    // accessors must return the empty/zero answer (distinct store, same bit).
    EXPECT_EQ(tv.group_first_field(802), std::uint16_t{0})
        << "the bare legacy store was never populated for 802 via add_group_member/"
           "set_group_first — only the context store was";

    // A no_tag NEVER populated by ANY path (clear-bit) must short-circuit to
    // the empty/zero answer through the 3-arg accessors too.
    EXPECT_EQ(tv.group_first_field("D", parent_path, std::uint16_t{7777}), std::uint16_t{0})
        << "an entirely unregistered no_tag (clear bit) must return 0 through the 3-arg accessor";
    EXPECT_TRUE(tv.group_member_tags("D", parent_path, std::uint16_t{7777}).empty())
        << "an entirely unregistered no_tag (clear bit) must return an empty span through the "
           "3-arg accessor";
}

// ── fixpp#215 item 2 — group_first_field_exact reports a context MISS ────────
//
// The 3-arg `group_first_field` answers a context miss out of the LEGACY BARE
// store (L-063-3) and returns a plain std::uint16_t, so the caller cannot tell
// "the context store answered" from "the context missed and you are holding the
// globally-first-seen variant". Harmless where the value is only reported;
// load-bearing where it decides control flow.
//
// This fixture is the discriminator: 802 is registered in BOTH stores, but the
// context store knows it under ONE path and ONE msg_type only. Every probe off
// that exact key is a miss with a non-zero bare answer waiting behind it — the
// precise shape that is invisible through the old accessor.
TEST(TableViewTest, GroupFirstFieldExactReportsAContextMissInsteadOfMaskingIt) {
    fixpp::dict::table_view tv;
    std::array<std::uint16_t, 1> const registered_path{453};
    std::array<std::uint16_t, 1> const other_path{555};

    tv.set_group_first(802, 600);                            // legacy bare store
    tv.set_group_first_ctx("D", registered_path, 802, 523);  // context store — this key only

    // 1. Context HIT — both accessors give the CONTEXT answer (523, not 600).
    EXPECT_EQ(tv.group_first_field("D", registered_path, 802), std::uint16_t{523});
    auto const hit = tv.group_first_field_exact("D", registered_path, 802);
    ASSERT_TRUE(hit.has_value()) << "a registered context must not report a miss";
    EXPECT_EQ(*hit, std::uint16_t{523});

    // 2. Context MISS on the PATH axis — the whole point of the accessor.
    EXPECT_EQ(tv.group_first_field("D", other_path, 802), std::uint16_t{600})
        << "the 3-arg accessor MASKS the miss: it answers 600 out of the bare store, and 600 is "
           "indistinguishable from a genuine context hit at the call site";
    EXPECT_FALSE(tv.group_first_field_exact("D", other_path, 802).has_value())
        << "group_first_field_exact must report the miss rather than fall back to 600";

    // 3. Context MISS on the MSG_TYPE axis — same conflation, other half of the key.
    EXPECT_EQ(tv.group_first_field("E", registered_path, 802), std::uint16_t{600});
    EXPECT_FALSE(tv.group_first_field_exact("E", registered_path, 802).has_value())
        << "a miss on the msg_type axis must be reported too — the key is (msg_type, path, no_tag)";

    // 4. NOT A GROUP AT ALL is a real answer, not a miss. The group bit is exact
    //    (a clear bit proves BOTH stores miss), so this must come back as
    //    engaged-zero — otherwise a caller that fails closed on nullopt would
    //    reject every non-group tag it was handed.
    auto const not_a_group = tv.group_first_field_exact("D", registered_path, std::uint16_t{7777});
    ASSERT_TRUE(not_a_group.has_value())
        << "an unregistered no_tag is a definitive 'not a group', not a context miss";
    EXPECT_EQ(*not_a_group, std::uint16_t{0});
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

// ── T007-6 / 075-live-wire-enum-validation FR-021 artifact #4 (T025) ────────
// FLIPPED at T025: enum_valid() is now REAL (075 T017). The ORIGINAL fixture
// (`load_test_dictionary`) declares Side(54) with ZERO <value> children, so
// this test previously passed VACUOUSLY — it sat on the Floor-1 empty-codeset
// accept path and "proved" nothing about the real domain check
// ([[feedback_coverage_push_enshrines_bugs]]). Rewritten to load a SEPARATE
// fixture carrying a real Side(54) codeset ({"1","2"}) and assert the actual
// domain check, while KEEPING a direct pin on the absent-tag/empty-codeset
// accept floor (FR-003) using ClOrdID(11), which carries no codeset in either
// fixture.
fixpp::dict::Dictionary load_test_dictionary_with_side_enum(std::pmr::memory_resource* mr) {
    constexpr std::string_view kXml =
        R"(<fix type='FIX' major='4' minor='4' servicepack='0'>)"
        R"(<fields>)"
        R"(<field number='8'   name='BeginString'      type='STRING'/>)"
        R"(<field number='9'   name='BodyLength'       type='LENGTH'/>)"
        R"(<field number='10'  name='CheckSum'         type='STRING'/>)"
        R"(<field number='11'  name='ClOrdID'          type='STRING'/>)"
        R"(<field number='21'  name='HandlInst'        type='CHAR'/>)"
        R"(<field number='35'  name='MsgType'          type='STRING'/>)"
        R"(<field number='49'  name='SenderCompID'     type='STRING'/>)"
        R"(<field number='54'  name='Side'             type='CHAR'>)"
        R"(<value enum='1' description='Buy'/>)"
        R"(<value enum='2' description='Sell'/>)"
        R"(</field>)"
        R"(<field number='55'  name='Symbol'           type='STRING'/>)"
        R"(<field number='56'  name='TargetCompID'     type='STRING'/>)"
        R"(</fields>)"
        R"(<messages>)"
        R"(<message name='NewOrderSingle' msgtype='D' msgcat='app'>)"
        R"(  <field name='BeginString'  required='N'/>)"
        R"(  <field name='BodyLength'   required='N'/>)"
        R"(  <field name='CheckSum'     required='N'/>)"
        R"(  <field name='MsgType'      required='N'/>)"
        R"(  <field name='ClOrdID'      required='Y'/>)"
        R"(  <field name='HandlInst'    required='Y'/>)"
        R"(  <field name='Side'         required='Y'/>)"
        R"(  <field name='Symbol'       required='Y'/>)"
        R"(</message>)"
        R"(</messages>)"
        R"(</fix>)";
    return fixpp::dict::XmlLoader{}.load_from_string(kXml, mr);
}

TEST(TableViewTest, EnumValidRealDomainCheck) {
    std::vector<std::byte> buf(2u * 1024u * 1024u);
    std::pmr::monotonic_buffer_resource mr{buf.data(), buf.size()};
    auto dict = load_test_dictionary_with_side_enum(&mr);
    auto tv = dict.as_table_view();

    // In-domain value ("1" — Buy) must be accepted.
    std::array<std::byte, 1> const one{std::byte{'1'}};
    EXPECT_TRUE(tv.enum_valid(54, std::span<const std::byte>{one.data(), one.size()}))
        << "Side(54)=1 is declared and must be accepted";

    // Out-of-domain value ("Z") must be rejected — the real domain check.
    std::array<std::byte, 1> const z{std::byte{'Z'}};
    EXPECT_FALSE(tv.enum_valid(54, std::span<const std::byte>{z.data(), z.size()}))
        << "Side(54)=Z is not in {1,2} and must be rejected (FR-003/FR-006)";
}

TEST(TableViewTest, EnumValidAbsentTagAcceptFloor) {
    // FR-003: a tag absent from the enum store (or with an empty codeset)
    // must accept regardless of value — the anti-reject-everything floor.
    // ClOrdID(11) carries no <value> children in this fixture.
    std::vector<std::byte> buf(2u * 1024u * 1024u);
    std::pmr::monotonic_buffer_resource mr{buf.data(), buf.size()};
    auto dict = load_test_dictionary_with_side_enum(&mr);
    auto tv = dict.as_table_view();

    std::array<std::byte, 1> val{std::byte{0}};
    EXPECT_TRUE(tv.enum_valid(11, std::span<const std::byte>{val.data(), val.size()}))
        << "ClOrdID(11) has no codeset — the absent-codeset accept floor must hold (FR-003)";
    EXPECT_TRUE(tv.enum_valid(9999, std::span<const std::byte>{}))
        << "an unknown tag must accept — absent-tag accept floor must hold (FR-003)";
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
    auto const xml_path = std::filesystem::path{FIXPP_DICT_DATA_DIR} / "FIX44.xml";
    auto dict = fixpp::dict::XmlLoader{}.load(xml_path, &mr);
    auto tv = dict.as_table_view();

    // Logon (A) → SenderCompID(49) valid + required.
    EXPECT_TRUE(tv.field_valid_for("A", 49)) << "SenderCompID must be valid for Logon in FIX44";
    auto logon_req = tv.required_fields("A");
    bool has_sender = false;
    for (auto t : logon_req) {
        if (t == 49) has_sender = true;
    }
    EXPECT_TRUE(has_sender) << "SenderCompID(49) must be required for Logon in FIX44";

    // NewOrderSingle (D) exists.
    EXPECT_TRUE(tv.field_valid_for("D", 11)) << "ClOrdID must be valid for NewOrderSingle in FIX44";

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
    EXPECT_TRUE(has_first) << "group_member_tags must contain the group_first_field tag";
}

// ── T005 Gate A P3-b: UtcTimestamp collapses to String ──────────────────────
// Pin the decision made in Gate A P3-b: UtcTimestamp (and all UTC* variants)
// map to field_type::String.  This means a malformed SendingTime(52) value
// is not catchable by the Phase-1 type-check arm — by design (the SendingTime
// guard in 038 already handles it before validation runs).
// Anchor: 041-validation-gate-wiring/plan.md Gate A P3-b.
TEST(TableViewTest, FieldTypeFromDataTypeUtcTimestampMapsToString) {
    using fixpp::dict::field_data_type;
    using fixpp::dict::field_type;

    EXPECT_EQ(field_type_from_data_type(field_data_type::UtcTimestamp), field_type::String)
        << "UtcTimestamp must map to String (Gate A P3-b pin)";
    EXPECT_EQ(field_type_from_data_type(field_data_type::UtcTimeOnly), field_type::String)
        << "UtcTimeOnly must map to String";
    EXPECT_EQ(field_type_from_data_type(field_data_type::UtcDateOnly), field_type::String)
        << "UtcDateOnly must map to String";
    EXPECT_EQ(field_type_from_data_type(field_data_type::LocalMktDate), field_type::String)
        << "LocalMktDate must map to String (064 AC-L8: same coarse collapse as "
           "UtcTimestamp, so DATE->LocalMktDate introduces no new rejections)";
}

// ── Phase 9.H (proxy_corrupt C2) viability gate ──────────────────────────────
// The live `reject-invalid-admin` interop cell turns on validate_inbound_messages
// against a live QuickFIX-J peer, using the production FIX44 dictionary. For that
// to work the dictionary's validation surface must satisfy two properties:
//   (a) every field QFJ's legitimate admin Logon carries (standard header tags +
//       EncryptMethod(98)/HeartBtInt(108)/ResetSeqNumFlag(141)) is valid-for "A",
//       so enabling inbound validation does NOT reject the live Logon (the session
//       must still reach Active);
//   (b) Symbol(55) is out-of-context on TestRequest(35=1) → the corrupt-admin
//       induction (one 35=1 carrying 55=BAD) yields wire_unexpected_tag →
//       Reject(35=3, 373=2).
// Anchor: phases/phase-9/work-plan.md §9.H; captured legit frames at
//   phase-9-harness/results/HP-QFj-*-reject-invalid-admin/counterparty-transcript.txt.
TEST(TableViewTest, Fix44ValidationSurfaceForProxyCorruptCell) {
    std::pmr::monotonic_buffer_resource mr;
    auto const path = std::filesystem::path{FIXPP_DICT_DATA_DIR} / "FIX44.xml";
    auto dict = fixpp::dict::XmlLoader{}.load(path, &mr);
    auto tv = dict.as_table_view();

    // (a) Legit QFJ Logon(35=A) admin fields — all must validate, or the live
    //     Logon would be rejected before the cell ever induces the corrupt frame.
    for (std::uint16_t tag : {std::uint16_t{34}, std::uint16_t{49}, std::uint16_t{56},
                              std::uint16_t{52}, std::uint16_t{98}, std::uint16_t{108},
                              std::uint16_t{141}}) {
        EXPECT_TRUE(tv.field_valid_for("A", tag))
            << "Logon(35=A) tag " << tag << " must validate (else the live Logon is rejected)";
    }
    // Legit TestRequest(35=1) carries only TestReqID(112).
    EXPECT_TRUE(tv.field_valid_for("1", 112))
        << "TestReqID(112) must be valid on TestRequest";
    // (b) The induction: Symbol(55) must be out-of-context on TestRequest.
    EXPECT_FALSE(tv.field_valid_for("1", 55))
        << "Symbol(55) must be out-of-context on TestRequest(35=1) for the "
           "proxy_corrupt induction to yield Reject(35=3, 373=2)";
}
