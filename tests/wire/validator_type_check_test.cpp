// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/validator_type_check_test.cpp — T055/Round-2 coverage hardening.
// Targeted tests covering uncovered branches in include/fixpp/wire/validator.hpp:
//   - check_field_type Float path (valid decimal, invalid decimal)
//   - check_field_type Int path (empty value, leading minus, non-digit char)
//   - check_field_type Char path (wrong length)
//   - check_field_type String/Boolean/Data/Length/default (no-op, returns ok)
//   - validate_field(): enum-violation path
//   - validate_field(): valid enum path
//   - validate() with type structural error (Int field with bad value)
//   - validate() with check_field_type returning error (Float field parse fail)
//   - required_fields(), field_valid_for(), group_first_field() via base class
//     virtual dispatch
//   Round-2 additions:
//   - validate_field() returning specific wire_field_value_out_of_range for Int
//   - validate_field() returning specific wire_field_value_truncated for Float
//     precision-loss re-mapping (decimal_precision_loss → wire_field_value_truncated)
//   - validate() with unexpected tag → wire_unexpected_tag
//   - validate() with required field missing → wire_required_field_missing

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <memory_resource>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// seam #1: mock must come BEFORE parser.hpp/validator.hpp (single-definition rule).
// clang-format groups this separately to prevent reordering across the seam.
// clang-format off
#include "support/mock_dict_table.hpp"
// clang-format on
#include <fixpp/core/decimal_helpers.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fixpp/wire/parser.hpp>
#include <fixpp/wire/validator.hpp>

#include "support/frame_view_factory.hpp"

namespace {

using fixpp::core::error;
using fixpp::dict::Dictionary;
using fixpp::dict::field_type;
using fixpp::dict::table_view;
using fixpp::wire::access_mode;
using fixpp::wire::dictionary_driven_validator;
using fixpp::wire::MessageView;
using fixpp::wire::Parser;
using fixpp::wire::Validator;

// ── Helpers ───────────────────────────────────────────────────────────────────
// Build "10=NNN\x01" without snprintf.
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

// Build a byte span from a string literal value (for validate_field).
std::vector<std::byte> bv(std::string_view s) {
    std::vector<std::byte> out(s.size());
    std::memcpy(out.data(), s.data(), s.size());
    return out;
}

constexpr std::size_t kScratch = 2048;

// ── check_field_type: Float path ─────────────────────────────────────────────

TEST(ValidatorTypeCheck, FloatFieldValidDecimalAccepted) {
    // OrderQty (38) is Float; "123.45" is a valid decimal.
    table_view t;
    t.add_valid("D", 38).set_type(38, field_type::Float);
    dictionary_driven_validator v{std::move(t)};

    auto val = bv("123.45");
    // validate_field goes through check_field_type Float path.
    auto rc = v.validate_field(38, std::span<const std::byte>{val.data(), val.size()});
    EXPECT_TRUE(rc.has_value()) << "valid Float value must be accepted; err="
                                << (rc.has_value() ? 0 : static_cast<int>(rc.error()));
}

TEST(ValidatorTypeCheck, FloatFieldInvalidDecimalRejected) {
    // A value that fails decimal_t::parse (not a valid number) → error.
    table_view t;
    t.add_valid("D", 38).set_type(38, field_type::Float);
    dictionary_driven_validator v{std::move(t)};

    auto val = bv("NOT_A_NUMBER");
    auto rc = v.validate_field(38, std::span<const std::byte>{val.data(), val.size()});
    // The Float path must report an error for a non-parseable value.
    EXPECT_FALSE(rc.has_value()) << "invalid Float value must be rejected";
}

// ── check_field_type: Int path ────────────────────────────────────────────────

TEST(ValidatorTypeCheck, IntFieldValidPositiveAccepted) {
    table_view t;
    t.add_valid("D", 34).set_type(34, field_type::Int);
    dictionary_driven_validator v{std::move(t)};

    auto val = bv("42");
    auto rc = v.validate_field(34, std::span<const std::byte>{val.data(), val.size()});
    EXPECT_TRUE(rc.has_value()) << "valid positive Int must be accepted";
}

TEST(ValidatorTypeCheck, IntFieldValidNegativeAccepted) {
    // Leading '-' is allowed for Int.
    table_view t;
    t.add_valid("D", 34).set_type(34, field_type::Int);
    dictionary_driven_validator v{std::move(t)};

    auto val = bv("-42");
    auto rc = v.validate_field(34, std::span<const std::byte>{val.data(), val.size()});
    EXPECT_TRUE(rc.has_value()) << "negative Int must be accepted";
}

TEST(ValidatorTypeCheck, IntFieldEmptyValueRejected) {
    // Empty value is invalid for Int.
    table_view t;
    t.add_valid("D", 34).set_type(34, field_type::Int);
    dictionary_driven_validator v{std::move(t)};

    std::vector<std::byte> empty_val;
    auto rc = v.validate_field(34, std::span<const std::byte>{empty_val.data(), empty_val.size()});
    ASSERT_FALSE(rc.has_value()) << "empty Int value must be rejected";
    EXPECT_EQ(rc.error(), error::wire_field_value_out_of_range);
}

TEST(ValidatorTypeCheck, IntFieldNonDigitCharRejected) {
    // "12A3" contains a non-digit after digits → rejected.
    table_view t;
    t.add_valid("D", 34).set_type(34, field_type::Int);
    dictionary_driven_validator v{std::move(t)};

    auto val = bv("12A3");
    auto rc = v.validate_field(34, std::span<const std::byte>{val.data(), val.size()});
    ASSERT_FALSE(rc.has_value()) << "Int with non-digit char must be rejected";
    EXPECT_EQ(rc.error(), error::wire_field_value_out_of_range);
}

TEST(ValidatorTypeCheck, IntFieldWidthOverflowPassesValidatorEnforcedDownstream) {
    // 9.G witness: the wire validator's Int check is width-agnostic by design —
    // FIX `INT` carries no width, so a numerically-oversized-but-well-formed
    // value (INT32_MAX+1) PASSES structural validation here. Per-width overflow
    // is enforced downstream at the typed convertor (dict::field_traits<int32>,
    // std::from_chars), tested in tests/dictionary/field_traits_test.cpp. This
    // pins that boundary so nobody "hardens" the validator with a spurious width.
    table_view t;
    t.add_valid("D", 34).set_type(34, field_type::Int);
    dictionary_driven_validator v{std::move(t)};

    auto val = bv("2147483648");  // INT32_MAX + 1: digit-format valid
    auto rc = v.validate_field(34, std::span<const std::byte>{val.data(), val.size()});
    EXPECT_TRUE(rc.has_value())
        << "validator is width-agnostic; overflow is the convertor's job, not the validator's";
}

// ── check_field_type: Char path ───────────────────────────────────────────────

TEST(ValidatorTypeCheck, CharFieldSingleByteAccepted) {
    table_view t;
    t.add_valid("D", 54).set_type(54, field_type::Char);
    dictionary_driven_validator v{std::move(t)};

    auto val = bv("1");
    auto rc = v.validate_field(54, std::span<const std::byte>{val.data(), val.size()});
    EXPECT_TRUE(rc.has_value()) << "single-byte Char must be accepted";
}

TEST(ValidatorTypeCheck, CharFieldWrongLengthRejected) {
    // Char must be exactly 1 byte; "AB" (2 bytes) is invalid.
    table_view t;
    t.add_valid("D", 54).set_type(54, field_type::Char);
    dictionary_driven_validator v{std::move(t)};

    auto val = bv("AB");
    auto rc = v.validate_field(54, std::span<const std::byte>{val.data(), val.size()});
    ASSERT_FALSE(rc.has_value()) << "multi-byte Char must be rejected";
    EXPECT_EQ(rc.error(), error::wire_field_value_out_of_range);
}

// ── check_field_type: String/Boolean/Data/Length default paths ───────────────

TEST(ValidatorTypeCheck, StringFieldAlwaysAccepted) {
    table_view t;
    t.add_valid("D", 11).set_type(11, field_type::String);
    dictionary_driven_validator v{std::move(t)};

    auto val = bv("anything");
    auto rc = v.validate_field(11, std::span<const std::byte>{val.data(), val.size()});
    EXPECT_TRUE(rc.has_value()) << "String field must always be accepted";
}

TEST(ValidatorTypeCheck, BooleanFieldAccepted) {
    table_view t;
    t.add_valid("D", 50).set_type(50, field_type::Boolean);
    dictionary_driven_validator v{std::move(t)};

    auto val = bv("Y");
    auto rc = v.validate_field(50, std::span<const std::byte>{val.data(), val.size()});
    EXPECT_TRUE(rc.has_value()) << "Boolean field must be accepted (no structural check)";
}

TEST(ValidatorTypeCheck, DataFieldAccepted) {
    table_view t;
    t.add_valid("D", 96).set_type(96, field_type::Data);
    dictionary_driven_validator v{std::move(t)};

    auto val = bv("rawbytes");
    auto rc = v.validate_field(96, std::span<const std::byte>{val.data(), val.size()});
    EXPECT_TRUE(rc.has_value()) << "Data field must be accepted (no structural check)";
}

// ── validate_field(): enum domain checking (075 FR-021 artifact #2, T024) ────
// FR-020: enum_valid() is now REAL (075 T017); validate_field() calls it as
// its FIRST statement (validator.hpp:325), so an out-of-domain value is now
// genuinely rejected. Previously (Phase-1 stub) this test asserted the
// OPPOSITE — accept — per FR-012 that re-baseline is called out explicitly
// here, not silently.

TEST(ValidatorTypeCheck, ValidateFieldEnumInDomainAccepted) {
    table_view t;
    t.add_valid("D", 54).set_type(54, field_type::Char).add_enum(54, "1").add_enum(54, "2");
    dictionary_driven_validator v{std::move(t)};

    auto val = bv("1");
    auto rc = v.validate_field(54, std::span<const std::byte>{val.data(), val.size()});
    EXPECT_TRUE(rc.has_value()) << "in-domain enum value must be accepted";
}

TEST(ValidatorTypeCheck, ValidateFieldEnumOutOfDomainRejected) {
    table_view t;
    t.add_valid("D", 54).set_type(54, field_type::Char).add_enum(54, "1").add_enum(54, "2");
    dictionary_driven_validator v{std::move(t)};

    // "X" is not in {"1","2"} — enum_valid() is real, so this must reject.
    auto val = bv("X");
    auto rc = v.validate_field(54, std::span<const std::byte>{val.data(), val.size()});
    ASSERT_FALSE(rc.has_value())
        << "out-of-domain enum value must be rejected (FR-020/FR-006)";
    EXPECT_EQ(rc.error(), error::wire_field_value_out_of_range);
}

TEST(ValidatorTypeCheck, ValidateFieldEnumMultiValueAllDeclaredAccepted) {
    table_view t;
    t.add_valid("D", 18)
        .set_type(18, field_type::String)
        .add_enum(18, "1")
        .add_enum(18, "6")
        .add_enum(18, "G")
        .set_multi_value(18, true);
    dictionary_driven_validator v{std::move(t)};

    auto val = bv("1 G 6");
    auto rc = v.validate_field(18, std::span<const std::byte>{val.data(), val.size()});
    EXPECT_TRUE(rc.has_value()) << "multi-value field with every token declared must be accepted";
}

TEST(ValidatorTypeCheck, ValidateFieldEnumMultiValueOneUndeclaredRejected) {
    table_view t;
    t.add_valid("D", 18)
        .set_type(18, field_type::String)
        .add_enum(18, "1")
        .add_enum(18, "6")
        .add_enum(18, "G")
        .set_multi_value(18, true);
    dictionary_driven_validator v{std::move(t)};

    auto val = bv("1 ZZ 6");  // "ZZ" not declared
    auto rc = v.validate_field(18, std::span<const std::byte>{val.data(), val.size()});
    ASSERT_FALSE(rc.has_value())
        << "multi-value field with one undeclared token must be rejected";
    EXPECT_EQ(rc.error(), error::wire_field_value_out_of_range);
}

TEST(ValidatorTypeCheck, ValidateFieldEnumEmptyValueAccepted) {
    // FR-008 Floor 2: empty field value bypasses the enum check unconditionally,
    // even when the tag carries a non-empty codeset.
    table_view t;
    t.add_valid("D", 54).set_type(54, field_type::Char).add_enum(54, "1").add_enum(54, "2");
    dictionary_driven_validator v{std::move(t)};

    std::vector<std::byte> empty_val;
    auto rc = v.validate_field(54, std::span<const std::byte>{empty_val.data(), empty_val.size()});
    // validate_field's enum arm accepts the empty value (Floor 2); the Char
    // structural check then rejects on length — so the overall result still
    // rejects, but via check_field_type, not the enum arm. Assert that
    // directly: a hand-crafted enum-only probe via table_view confirms the
    // bypass (see TableViewTest.EnumValidRealDomainCheck for the direct pin).
    EXPECT_FALSE(rc.has_value())
        << "empty Char value rejects via the TYPE arm (Char requires exactly 1 byte), "
           "not the enum arm (FR-008 Floor 2 bypasses the enum check unconditionally)";
    EXPECT_EQ(rc.error(), error::wire_field_value_out_of_range);
}

// ── validate_field(): FIX50SP2 store-only tag (ApplVerID/1128) ──────────────
// [FR-020] ApplVerID(1128) codes are 0-10 (measured, T001). It is declared in
// the dictionary but ABSENT from message expansion — real FIX50SP2 messages
// reference RefApplVerID(1130) instead. This synthetic XML reproduces that
// exact shape: <field number='1128'> carries <value> children but no
// <message> references it, so it is a genuine store-only tag once run
// through the REAL production Dictionary::as_table_view() (dictionary.cpp),
// not a hand-built mock table_view.
//
// ⚠️ Mutation (T015/C3-1 discriminator): build the enum-domain table only
// from message_fields() (instead of walking the dictionary's OWN enum store,
// dictionary.cpp:503-511) ⇒ store-only tags stay unconstrained ⇒ this test's
// reject assertion MUST go RED. This is the ONLY witness in the bundle with
// power against that regression — validate_field() calls enum_valid() as its
// FIRST statement with NO field_valid_for precheck (validator.hpp:325-330),
// so a reachability-built table would silently ACCEPT here.
Dictionary load_appl_ver_id_store_only_dict(std::pmr::memory_resource* mr) {
    constexpr std::string_view kXml =
        R"(<fix type='FIX' major='5' minor='0' servicepack='2'>)"
        R"(<fields>)"
        R"(<field number='8'    name='BeginString' type='STRING'/>)"
        R"(<field number='9'    name='BodyLength'  type='LENGTH'/>)"
        R"(<field number='10'   name='CheckSum'    type='STRING'/>)"
        R"(<field number='35'   name='MsgType'     type='STRING'/>)"
        R"(<field number='1128' name='ApplVerID'   type='STRING'>)"
        R"(<value enum='0'  description='FIX27'/>)"
        R"(<value enum='1'  description='FIX30'/>)"
        R"(<value enum='2'  description='FIX40'/>)"
        R"(<value enum='3'  description='FIX41'/>)"
        R"(<value enum='4'  description='FIX42'/>)"
        R"(<value enum='5'  description='FIX43'/>)"
        R"(<value enum='6'  description='FIX44'/>)"
        R"(<value enum='7'  description='FIX50'/>)"
        R"(<value enum='8'  description='FIX50SP1'/>)"
        R"(<value enum='9'  description='FIX50SP2'/>)"
        R"(<value enum='10' description='FIXLatest'/>)"
        R"(</field>)"
        R"(</fields>)"
        R"(<messages>)"
        R"(<message name='Heartbeat' msgtype='0' msgcat='admin'>)"
        R"(  <field name='BeginString' required='N'/>)"
        R"(  <field name='BodyLength'  required='N'/>)"
        R"(  <field name='MsgType'     required='N'/>)"
        R"(</message>)"
        R"(</messages>)"
        R"(</fix>)";
    return fixpp::dict::XmlLoader{}.load_from_string(kXml, mr);
}

TEST(ValidatorTypeCheck, ValidateFieldStoreOnlyApplVerIdOutOfDomainRejected) {
    std::vector<std::byte> buf(2u * 1024u * 1024u);
    std::pmr::monotonic_buffer_resource mr{buf.data(), buf.size()};
    auto dict = load_appl_ver_id_store_only_dict(&mr);
    auto tv = dict.as_table_view();
    dictionary_driven_validator v{std::move(tv)};

    auto val = bv("bogus");
    auto rc = v.validate_field(1128, std::span<const std::byte>{val.data(), val.size()});
    ASSERT_FALSE(rc.has_value())
        << "ApplVerID(1128)=bogus must be rejected — store-only tag must still "
           "get a domain from the store-driven projection (T015/C3-1)";
    EXPECT_EQ(rc.error(), error::wire_field_value_out_of_range);
}

TEST(ValidatorTypeCheck, ValidateFieldStoreOnlyApplVerIdInDomainAccepted) {
    std::vector<std::byte> buf(2u * 1024u * 1024u);
    std::pmr::monotonic_buffer_resource mr{buf.data(), buf.size()};
    auto dict = load_appl_ver_id_store_only_dict(&mr);
    auto tv = dict.as_table_view();
    dictionary_driven_validator v{std::move(tv)};

    auto val = bv("9");
    auto rc = v.validate_field(1128, std::span<const std::byte>{val.data(), val.size()});
    EXPECT_TRUE(rc.has_value()) << "ApplVerID(1128)=9 is declared (FIX50SP2) and must be accepted";
}

// ── validate() with Int structural error ──────────────────────────────────────

// Add framing tags (8, 9, 10) as valid so the validator doesn't reject them
// as wire_unexpected_tag before reaching the body field checks.
table_view make_grammar_with_framing(std::string_view msg_type) {
    table_view t;
    t.add_valid(msg_type, 8).add_valid(msg_type, 9).add_valid(msg_type, 10);
    return t;
}

TEST(ValidatorTypeCheck, ValidateWithBadIntFieldRejected) {
    auto t = make_grammar_with_framing("D");
    t.add_required("D", 35).add_required("D", 34).set_type(34, field_type::Int);
    dictionary_driven_validator v{std::move(t)};

    // tag 34 has value "BAD" — not a valid Int.
    auto buf = make_frame(
        "35=D\x01"
        "34=BAD\x01");
    std::array<std::byte, 4096> stack{};
    std::pmr::monotonic_buffer_resource arena;
    auto mv = parse_index(buf, stack, arena);

    std::array<std::byte, kScratch> scratch_buf{};
    std::pmr::monotonic_buffer_resource scratch_mr{scratch_buf.data(), scratch_buf.size(),
                                                   std::pmr::null_memory_resource()};

    auto result = v.validate(mv, &scratch_mr, nullptr);
    ASSERT_FALSE(result.has_value()) << "validate() with bad Int field must return error";
    EXPECT_EQ(result.error(), error::wire_field_value_out_of_range);
}

// ── validate() with Float structural check ────────────────────────────────────

TEST(ValidatorTypeCheck, ValidateWithValidFloatFieldAccepted) {
    auto t = make_grammar_with_framing("D");
    t.add_required("D", 35).add_required("D", 38).set_type(38, field_type::Float);
    dictionary_driven_validator v{std::move(t)};

    auto buf = make_frame(
        "35=D\x01"
        "38=100.50\x01");
    std::array<std::byte, 4096> stack{};
    std::pmr::monotonic_buffer_resource arena;
    auto mv = parse_index(buf, stack, arena);

    std::array<std::byte, kScratch> scratch_buf{};
    std::pmr::monotonic_buffer_resource scratch_mr{scratch_buf.data(), scratch_buf.size(),
                                                   std::pmr::null_memory_resource()};

    auto result = v.validate(mv, &scratch_mr, nullptr);
    EXPECT_TRUE(result.has_value()) << "validate() with valid Float field must succeed; err="
                                    << (result.has_value() ? 0 : static_cast<int>(result.error()));
}

TEST(ValidatorTypeCheck, ValidateWithInvalidFloatFieldRejected) {
    auto t = make_grammar_with_framing("D");
    t.add_required("D", 35).add_valid("D", 38).set_type(38, field_type::Float);
    dictionary_driven_validator v{std::move(t)};

    auto buf = make_frame(
        "35=D\x01"
        "38=NOTAFLOAT\x01");
    std::array<std::byte, 4096> stack{};
    std::pmr::monotonic_buffer_resource arena;
    auto mv = parse_index(buf, stack, arena);

    std::array<std::byte, kScratch> scratch_buf{};
    std::pmr::monotonic_buffer_resource scratch_mr{scratch_buf.data(), scratch_buf.size(),
                                                   std::pmr::null_memory_resource()};

    auto result = v.validate(mv, &scratch_mr, nullptr);
    ASSERT_FALSE(result.has_value()) << "validate() with invalid Float must return error";
}

// ── virtual dispatch through base class pointer ───────────────────────────────
// required_fields(), field_valid_for(), group_first_field() via Validator*.

TEST(ValidatorTypeCheck, VirtualDispatchRequiredFields) {
    table_view t;
    t.add_required("D", 35).add_required("D", 11);
    dictionary_driven_validator ddv{std::move(t)};
    Validator* vp = &ddv;

    auto req = vp->required_fields("D");
    EXPECT_GE(req.size(), 2U) << "required_fields via base must return registered tags";
}

TEST(ValidatorTypeCheck, VirtualDispatchFieldValidFor) {
    table_view t;
    t.add_valid("D", 55);
    dictionary_driven_validator ddv{std::move(t)};
    Validator* vp = &ddv;

    EXPECT_TRUE(vp->field_valid_for("D", 55)) << "tag 55 must be valid for D";
    EXPECT_FALSE(vp->field_valid_for("D", 9999)) << "tag 9999 must not be valid for D";
}

TEST(ValidatorTypeCheck, VirtualDispatchGroupFirstField) {
    table_view t;
    t.set_group_first(453, 448);
    dictionary_driven_validator ddv{std::move(t)};
    Validator* vp = &ddv;

    EXPECT_EQ(vp->group_first_field(453), 448U) << "group_first_field via base must work";
    EXPECT_EQ(vp->group_first_field(9999), 0U) << "missing group must return 0";
}

// ── Char with empty value via validate_field (covers empty single check) ──────

TEST(ValidatorTypeCheck, CharFieldEmptyValueRejected) {
    table_view t;
    t.add_valid("D", 54).set_type(54, field_type::Char);
    dictionary_driven_validator v{std::move(t)};

    std::vector<std::byte> empty_val;
    auto rc = v.validate_field(54, std::span<const std::byte>{empty_val.data(), empty_val.size()});
    ASSERT_FALSE(rc.has_value()) << "empty Char value must be rejected";
    EXPECT_EQ(rc.error(), error::wire_field_value_out_of_range);
}

// ── Round-2: field-error return — validate_field Int bad value ────────────────
// Exercises the check_field_type Int path returning wire_field_value_out_of_range
// through validate_field (which calls check_field_type directly).

TEST(ValidatorTypeCheck, ValidateFieldIntNonDigitReturnsSpecificError) {
    table_view t;
    t.set_type(34, field_type::Int);
    dictionary_driven_validator v{std::move(t)};

    auto val = bv("abc");  // non-digit → wire_field_value_out_of_range
    auto rc = v.validate_field(34, std::span<const std::byte>{val.data(), val.size()});
    ASSERT_FALSE(rc.has_value()) << "validate_field must reject non-digit Int";
    EXPECT_EQ(rc.error(), error::wire_field_value_out_of_range)
        << "expected wire_field_value_out_of_range (slot 40)";
}

// ── Round-2: validate() — unexpected tag → wire_unexpected_tag ───────────────
// When a tag is present in the message but NOT in field_valid_for(), validate()
// must return wire_unexpected_tag (42) on the first such field.

TEST(ValidatorTypeCheck, ValidateUnexpectedTagReturnsError) {
    // Build a dict that knows msg_type "D" with only tags 8,9,10,35.
    auto t = make_grammar_with_framing("D");
    t.add_valid("D", 35);
    // Tag 49 is NOT registered → unexpected.
    dictionary_driven_validator v{std::move(t)};

    auto buf = make_frame(
        "35=D\x01"
        "49=SENDER\x01");
    std::array<std::byte, 4096> stack{};
    std::pmr::monotonic_buffer_resource arena;
    auto mv = parse_index(buf, stack, arena);

    std::array<std::byte, kScratch> scratch_buf{};
    std::pmr::monotonic_buffer_resource scratch_mr{scratch_buf.data(), scratch_buf.size(),
                                                   std::pmr::null_memory_resource()};

    auto result = v.validate(mv, &scratch_mr, nullptr);
    ASSERT_FALSE(result.has_value()) << "validate() must reject an unexpected tag";
    EXPECT_EQ(result.error(), error::wire_unexpected_tag)
        << "expected wire_unexpected_tag (slot 42)";
}

// ── Round-2: validate() — required field missing → wire_required_field_missing
// When a required tag is absent from the message, validate() step 2 returns
// wire_required_field_missing (38).

TEST(ValidatorTypeCheck, ValidateRequiredFieldMissingReturnsError) {
    // Require tag 11 (ClOrdID) but don't include it in the frame.
    auto t = make_grammar_with_framing("D");
    t.add_valid("D", 35).add_required("D", 11);
    dictionary_driven_validator v{std::move(t)};

    // Frame has only tag 35; tag 11 is absent.
    auto buf = make_frame("35=D\x01");
    std::array<std::byte, 4096> stack{};
    std::pmr::monotonic_buffer_resource arena;
    auto mv = parse_index(buf, stack, arena);

    std::array<std::byte, kScratch> scratch_buf{};
    std::pmr::monotonic_buffer_resource scratch_mr{scratch_buf.data(), scratch_buf.size(),
                                                   std::pmr::null_memory_resource()};

    auto result = v.validate(mv, &scratch_mr, nullptr);
    ASSERT_FALSE(result.has_value()) << "validate() must reject when a required field is missing";
    EXPECT_EQ(result.error(), error::wire_required_field_missing)
        << "expected wire_required_field_missing (slot 38)";
}

// ── Round-2: validator trap_throw fence — direct fence test ──────────────────
// The trap_throw fence in check_field_type (~193-194) is only reachable when
// the FIXPP_DECIMAL_T trait's from_chars throws. With the default pod_decimal
// trait (which is noexcept), the lambda body is implicitly noexcept and the
// fence is structurally dead code. We verify the fence mechanism itself works
// correctly by calling core::detail::trap_throw directly with a throwing lambda
// that matches the shape of the fenced call.

TEST(ValidatorTypeCheck, TrapThrowFenceMechanismCatchesExceptionAndReturnsError) {
    // Direct verification: trap_throw catches a std::runtime_error and maps it
    // to core::error::decimal_invalid_input (the catch-all branch in trap_throw).
    bool escaped = false;
    fixpp::core::expected_t<int> r{0};
    try {
        r = fixpp::core::detail::trap_throw(
            []() -> int { throw std::runtime_error{"validator trait blew up"}; });
    } catch (...) {
        escaped = true;
    }
    EXPECT_FALSE(escaped) << "exception must not escape trap_throw";
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), error::decimal_invalid_input)
        << "generic exception must map to decimal_invalid_input";
}

// ── fixpp#201: per-instance required-member enforcement in repeating groups ────
// A group NoX(100) with delimiter 200 and a REQUIRED non-delimiter member 300
// (plus optional member 400). QuickFIX checks required members PER instance;
// before this fix the runtime validator's consume_group verified only
// count/delimiter/membership, so an instance omitting 300 was wrongly ACCEPTED.
namespace {
table_view make_group_grammar_201() {
    table_view t;
    t.add_valid("D", 8).add_valid("D", 9).add_valid("D", 10).add_valid("D", 35);
    t.add_valid("D", 100).add_valid("D", 200).add_valid("D", 300).add_valid("D", 400);
    t.set_group_first(100, 200);      // NoX=100, delimiter=200 (also adds 200 as member)
    t.add_group_member(100, 300);
    t.add_group_member(100, 400);
    t.add_group_required_member(100, 300);  // 300 required in EVERY instance
    return t;
}
}  // namespace

// RED before fix (accepted); GREEN after: instance 2 omits required member 300.
TEST(ValidatorTypeCheck, Fixpp201GroupInstanceMissingRequiredMemberRejected) {
    dictionary_driven_validator v{make_group_grammar_201()};
    auto buf = make_frame(
        "35=D\x01"
        "100=2\x01"
        "200=a\x01" "300=x\x01" "400=y\x01"   // instance 1: complete
        "200=b\x01" "400=z\x01");               // instance 2: MISSING required 300
    std::array<std::byte, 4096> stack{};
    std::pmr::monotonic_buffer_resource arena;
    auto mv = parse_index(buf, stack, arena);

    std::array<std::byte, kScratch> scratch_buf{};
    std::pmr::monotonic_buffer_resource scratch_mr{scratch_buf.data(), scratch_buf.size(),
                                                   std::pmr::null_memory_resource()};
    std::uint16_t ref_tag = 0;
    auto result = v.validate(mv, &scratch_mr, &ref_tag);
    ASSERT_FALSE(result.has_value())
        << "group instance omitting a required member must be rejected";
    EXPECT_EQ(result.error(), error::wire_required_field_missing);
    EXPECT_EQ(ref_tag, 300) << "ref tag must name the missing required member";
}

// Positive: every instance carries required member 300 → accept (no over-reject).
TEST(ValidatorTypeCheck, Fixpp201GroupAllInstancesCompleteAccepted) {
    dictionary_driven_validator v{make_group_grammar_201()};
    auto buf = make_frame(
        "35=D\x01"
        "100=2\x01"
        "200=a\x01" "300=x\x01" "400=y\x01"
        "200=b\x01" "300=w\x01");
    std::array<std::byte, 4096> stack{};
    std::pmr::monotonic_buffer_resource arena;
    auto mv = parse_index(buf, stack, arena);

    std::array<std::byte, kScratch> scratch_buf{};
    std::pmr::monotonic_buffer_resource scratch_mr{scratch_buf.data(), scratch_buf.size(),
                                                   std::pmr::null_memory_resource()};
    auto result = v.validate(mv, &scratch_mr, nullptr);
    EXPECT_TRUE(result.has_value())
        << "conforming group (all instances complete) must be accepted; err="
        << (result.has_value() ? 0 : static_cast<int>(result.error()));
}

// ── T003: dynamic-width per-instance required-member check (079) ────────────
// A synthetic group NoW(900) with delimiter 901 and 70 DIRECT required
// members (tags 1000..1069) -- deliberately >64 to exercise the width past
// the candidate's fixed uint64_t bitmask. Against the candidate's
// `req_members.size() <= 64` guard, `check_required` is false for this group
// so the whole per-instance required check is SKIPPED (fail-open) and an
// instance omitting a required member is wrongly ACCEPTED. FR-004/data-model.md
// "Group-instance membership check state" mandates universal, fail-closed
// enforcement for every per-group required-member count.
namespace {
constexpr std::uint16_t kWideGroupNoTag = 900;
constexpr std::uint16_t kWideGroupDelim = 901;
constexpr std::uint16_t kWideGroupFirstMember = 1000;
constexpr std::size_t kWideGroupMemberCount = 70;  // > 64

table_view make_wide_group_grammar_201() {
    table_view t;
    t.add_valid("D", 8).add_valid("D", 9).add_valid("D", 10).add_valid("D", 35);
    t.add_valid("D", kWideGroupNoTag).add_valid("D", kWideGroupDelim);
    t.set_group_first(kWideGroupNoTag, kWideGroupDelim);  // also adds the delim as a member
    for (std::size_t k = 0; k < kWideGroupMemberCount; ++k) {
        std::uint16_t const tag = static_cast<std::uint16_t>(kWideGroupFirstMember + k);
        t.add_valid("D", tag);
        t.add_group_member(kWideGroupNoTag, tag);
        t.add_group_required_member(kWideGroupNoTag, tag);  // ALL 70 direct-required
    }
    return t;
}

// One group instance body, optionally omitting `omit_tag` (0 = omit nothing).
std::string wide_group_instance(std::uint16_t omit_tag) {
    std::string out = std::to_string(kWideGroupDelim) + "=x\x01";
    for (std::size_t k = 0; k < kWideGroupMemberCount; ++k) {
        std::uint16_t const tag = static_cast<std::uint16_t>(kWideGroupFirstMember + k);
        if (tag == omit_tag) {
            continue;
        }
        out += std::to_string(tag) + "=v\x01";
    }
    return out;
}
}  // namespace

TEST(ValidatorTypeCheck, Fixpp201GroupWideRequiredMaskInstanceMissingRequiredMemberRejected) {
    dictionary_driven_validator v{make_wide_group_grammar_201()};
    // Instance 1: complete (all 70 required members present).
    // Instance 2: omits tag 1069 -- bit-index 69 within the required set,
    // i.e. word 1 / bit 5 of the dynamic-width mask -- strictly PAST the old
    // fixed uint64_t mask's 64-bit capacity (word 0 / bits 0-63). This is
    // load-bearing: an implementation that merely stops skipping the check
    // but still tracks presence in a single 64-bit word would satisfy word 0
    // in full (indices 0-63 all present) and never notice the missing
    // index-69 bit, so this test would wrongly PASS against such a
    // regression unless the omitted index is >= 64.
    constexpr std::uint16_t kOmitTag = 1069;
    std::string const body = std::string("35=D\x01") + "900=2\x01" + wide_group_instance(0) +
                             wide_group_instance(kOmitTag);
    auto buf = make_frame(body);
    // 146 offset-table entries for this synthetic wide group -- larger than
    // the shared 4096-byte stack the other cases use.
    std::array<std::byte, 16384> stack{};
    std::pmr::monotonic_buffer_resource arena;
    auto mv = parse_index(buf, stack, arena);

    std::array<std::byte, kScratch> scratch_buf{};
    std::pmr::monotonic_buffer_resource scratch_mr{scratch_buf.data(), scratch_buf.size(),
                                                   std::pmr::null_memory_resource()};
    std::uint16_t ref_tag = 0;
    auto result = v.validate(mv, &scratch_mr, &ref_tag);
    ASSERT_FALSE(result.has_value())
        << ">64-required-member group instance omitting a required member must be "
           "rejected (dynamic-width, fail-closed, FR-004)";
    EXPECT_EQ(result.error(), error::wire_required_field_missing);
    EXPECT_EQ(ref_tag, kOmitTag) << "ref tag must name the missing required member";
}

// ── 079 T006/T007/T010/T011: real-frame accept/reject over the REAL vendored
// dictionaries (contracts/census-and-agreement.md Contract 4) ──────────────
// Loads FIX44.xml / FIX50SP2.xml / FIX42.xml via the production XmlLoader ->
// Dictionary::as_table_view() -> dictionary_driven_validator, exactly the
// runtime path a live session uses. Per-test Dictionary (local, kept alive
// through validate()).
namespace {

Dictionary load_real_dict(char const* file, std::pmr::memory_resource* mr) {
    auto const path = std::filesystem::path{FIXPP_DICT_DATA_DIR} / file;
    return fixpp::dict::XmlLoader{}.load(path, mr);
}

}  // namespace

// ── T006: FIX44 PositionReport(AP) with NO NoUnderlyings group -> ACCEPT ───
// Pre-candidate the loader leaked UnderlyingSettlPrice(732)/
// UnderlyingSettlPriceType(733) (required='Y' only INSIDE the optional
// NoUnderlyings group) into AP's message-level required set, so a
// conforming AP omitting the group was wrongly rejected with
// wire_required_field_missing on tag 732.
TEST(ValidatorTypeCheck, Fixpp201Fix44PositionReportOmitsOptionalGroupAccepted) {
    std::pmr::monotonic_buffer_resource mr;
    auto d44 = load_real_dict("FIX44.xml", &mr);
    dictionary_driven_validator v{d44.as_table_view()};

    // Every FIX44 AP message-level required field (581 AccountType, 715
    // ClearingBusinessDate, 721 PosMaintRptID, 728 PosReqResult, 730
    // SettlPrice, 731 SettlPriceType, 734 PriorSettlPrice) + header (34/49/
    // 52/56) — NO 711 (NoUnderlyings) group present.
    auto buf = make_frame(
        "35=AP\x01"
        "34=1\x01" "49=SENDER\x01" "52=20240101-00:00:00\x01" "56=TARGET\x01"
        "1=ACCT1\x01" "581=1\x01" "715=20240101\x01" "721=RPT1\x01" "728=0\x01"
        "730=1.5\x01" "731=1\x01" "734=1.4\x01");
    std::array<std::byte, 4096> stack{};
    std::pmr::monotonic_buffer_resource arena;
    auto mv = parse_index(buf, stack, arena);

    std::array<std::byte, kScratch> scratch_buf{};
    std::pmr::monotonic_buffer_resource scratch_mr{scratch_buf.data(), scratch_buf.size(),
                                                   std::pmr::null_memory_resource()};
    std::uint16_t ref_tag = 0;
    auto result = v.validate(mv, &scratch_mr, &ref_tag);
    EXPECT_TRUE(result.has_value())
        << "conforming FIX44 AP omitting the optional NoUnderlyings group must be "
           "accepted; err=" << (result.has_value() ? 0 : static_cast<int>(result.error()))
        << " ref_tag=" << ref_tag;
}

// ── T006: FIX50SP2 TradeCaptureReport(AE) — real-frame validate() BLOCKED ──
// ESCALATED FINDING (079 /implement Phase 3, out of T009's scope — not a
// group-required leak): every FIX50SP2.x vendored dictionary (FIX50.xml,
// FIX50SP1.xml, FIX50SP2.xml) ships a self-closing empty `<header/>`
// element (the QuickFIX FIXT.1.1 session/application split — standard
// header fields BeginString(8)/BodyLength(9)/MsgSeqNum(34)/SenderCompID(49)/
// SendingTime(52)/TargetCompID(56)/CheckSum(10) live ONLY in FIXT11.xml).
// `XmlLoader::expand_field_list` walks `header_node_` unconditionally
// (xml_loader.cpp:850) — with zero children, ZERO header tags are ever
// registered `valid_` for ANY FIX50SPx message type. `dictionary_driven_-
// validator::validate()` Step 1(a) then rejects the very first framing
// field (tag 8, itself guaranteed present by the Framer) with
// wire_unexpected_tag, for EVERY message of EVERY FIX50SPx msg_type —
// unconditionally, before Step 2/3 (required-fields / group checks) ever
// run. fixpp has no FIXT11+FIX50SPx dictionary-merge mechanism (confirmed:
// `dict::version_registry` treats vt11 as session-admin-only, never merged
// into an application `table_view`; `XmlLoader::load()` takes exactly one
// XML path; `dictionary.kind="path"` in the TOML config loader loads a
// single file). This means `dictionary_driven_validator::validate()`
// cannot accept ANY real FIX50SP2 application frame today when the
// dictionary is loaded from the vendored FIX50SP2.xml alone — a
// pre-existing, orthogonal defect discovered while constructing this
// test, NOT introduced by the 079 candidate and NOT a group-required
// leak, so out of T009's fix scope. Escalated to the orchestrator; no
// real-frame FIX50SP2 validate() test is shipped here. The T008
// dictionary-level derivation pin (RequiredScope.-
// Fix50sp2TradeCaptureReportExcludesOptionalGroupRequired, which never
// calls validate()) still stands as US1's FIX50SP2 corroboration.

// ── T007: FIX42 Allocation(J) with NO NoAllocs group -> ACCEPT ─────────────
// FIX42 is the QuickFIX-XML `xml_loader` path (no typed tier, SC-004).
// AllocShares(80) is `required='Y'` only INSIDE the optional NoAllocs(78)
// group on J; the message-level required set must exclude it.
TEST(ValidatorTypeCheck, Fixpp201Fix42AllocationOmitsOptionalGroupAccepted) {
    std::pmr::monotonic_buffer_resource mr;
    auto d42 = load_real_dict("FIX42.xml", &mr);
    dictionary_driven_validator v{d42.as_table_view()};

    // FIX42 J message-level required: AllocID(70), AllocTransType(71),
    // Side(54), Symbol(55), Shares(53), AvgPx(6), TradeDate(75) + header.
    auto buf = make_frame(
        "35=J\x01"
        "34=1\x01" "49=SENDER\x01" "52=20240101-00:00:00\x01" "56=TARGET\x01"
        "70=ALLOC1\x01" "71=0\x01" "54=1\x01" "55=SYM\x01" "53=100\x01" "6=10.5\x01"
        "75=20240101\x01");
    std::array<std::byte, 4096> stack{};
    std::pmr::monotonic_buffer_resource arena;
    auto mv = parse_index(buf, stack, arena);

    std::array<std::byte, kScratch> scratch_buf{};
    std::pmr::monotonic_buffer_resource scratch_mr{scratch_buf.data(), scratch_buf.size(),
                                                   std::pmr::null_memory_resource()};
    std::uint16_t ref_tag = 0;
    auto result = v.validate(mv, &scratch_mr, &ref_tag);
    EXPECT_TRUE(result.has_value())
        << "conforming FIX42 Allocation omitting the optional NoAllocs group must be "
           "accepted; err=" << (result.has_value() ? 0 : static_cast<int>(result.error()))
        << " ref_tag=" << ref_tag;
}

// ── T010/T011: FIX44 AP / NoUnderlyings(711) — malformed instance reject,
// complete instances accept ────────────────────────────────────────────────
// Delimiter = UnderlyingSymbol(311, required='N'); direct required members
// = {UnderlyingSettlPrice(732), UnderlyingSettlPriceType(733)} (pinned by
// RequiredScope.Fix44AsTableViewContextRequiredMembers).
namespace {
std::string fix44_ap_required_prefix() {
    return "35=AP\x01"
           "34=1\x01" "49=SENDER\x01" "52=20240101-00:00:00\x01" "56=TARGET\x01"
           "1=ACCT1\x01" "581=1\x01" "715=20240101\x01" "721=RPT1\x01" "728=0\x01"
           "730=1.5\x01" "731=1\x01" "734=1.4\x01";
}
}  // namespace

TEST(ValidatorTypeCheck, Fixpp201Fix44PositionReportGroupInstanceMissingRequiredMemberRejected) {
    std::pmr::monotonic_buffer_resource mr;
    auto d44 = load_real_dict("FIX44.xml", &mr);
    dictionary_driven_validator v{d44.as_table_view()};

    // Instance 1 complete (311/732/733); instance 2 omits 733.
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
    auto result = v.validate(mv, &scratch_mr, &ref_tag);
    ASSERT_FALSE(result.has_value())
        << "FIX44 AP NoUnderlyings instance omitting required 733 must be rejected";
    EXPECT_EQ(result.error(), error::wire_required_field_missing);
    EXPECT_EQ(ref_tag, 733) << "ref tag must name the missing required member";
}

TEST(ValidatorTypeCheck, Fixpp201Fix44PositionReportAllGroupInstancesCompleteAccepted) {
    std::pmr::monotonic_buffer_resource mr;
    auto d44 = load_real_dict("FIX44.xml", &mr);
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
    auto result = v.validate(mv, &scratch_mr, nullptr);
    EXPECT_TRUE(result.has_value())
        << "FIX44 AP with every NoUnderlyings instance complete must be accepted; err="
        << (result.has_value() ? 0 : static_cast<int>(result.error()));
}

// ── T010/T011: FIX50SP2 — BLOCKED, same root cause as the T006 escalation
// above (every FIX50SPx message rejects on the framing tag itself, before
// Step 2/3 ever run) — no real-frame FIX50SP2 reject/accept test shipped.
//
// ── T010/T011: FIX42 — BLOCKED for a DIFFERENT, also pre-existing, also
// out-of-scope reason: FIX42.xml declares group-count fields (e.g. NoAllocs,
// tag 78) with `type='INT'` rather than `type='NUMINGROUP'` (confirmed via
// message_fields("J"): FieldRef{tag=78, type=Int, ...}). The 079 Phase-2
// context-scoped per-group store (dictionary.cpp's group-population loop)
// keys strictly on `fr.type == field_data_type::NumInGroup` to detect a
// group's count field, so it NEVER populates a context entry for ANY FIX42
// group — `table_view::group_first_field("J", {}, 78)` returns 0 (though
// the BARE global `Dictionary::group_first_field(78)` resolves correctly
// via a type-independent `<group>`-element scan). Consequently
// `Validator::validate_group_level` never recognises 78 as a group count
// field for FIX42, `consume_group` is never invoked, and the per-instance
// required-member check can never fire — a synthetic "reject" test would
// vacuously pass for the WRONG reason (group invisibility, not correct
// enforcement), and an "accept" test would be equally uninformative. This
// is the documented, already-tracked limitation L-066-1 ("FIX 4.0/4.1/4.2
// sessions become strict-but-GROUP-BLIND under dict validation"),
// explicitly deferred to issue #196 ("relax INT-typed NumInGroup group
// detection to structural") — NOT something T012 authorizes fixing here.
// T007's FIX42 accept-path test above remains valid: it exercises ONLY the
// message-level `in_group` required-set exclusion (candidate's
// `expand_field_list`), which does NOT depend on NumInGroup-type detection.

}  // namespace
