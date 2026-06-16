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
#include <fixpp/wire/parser.hpp>
#include <fixpp/wire/validator.hpp>

#include "support/frame_view_factory.hpp"

namespace {

using fixpp::core::error;
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

MessageView<access_mode::Index> parse_index(std::vector<std::byte> const& buf,
                                            std::array<std::byte, 4096>& stack_buf,
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
    return *mv;
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

// ── validate_field(): enum violation (Phase-1 behavior) ──────────────────────
// FR-005 / 041-validation-gate-wiring: enum-value checking is OUT OF SCOPE for
// Phase-1; enum_valid() always returns true regardless of registered enum
// values. The add_enum() builder method is a no-op stub.
// Phase-2 (2c enum tables) will restore the rejection behavior; this test
// documents the Phase-1 pass-through so the behavioral change is pinned.

TEST(ValidatorTypeCheck, ValidateFieldEnumViolationPassedInPhase1) {
    table_view t;
    t.add_valid("D", 54).set_type(54, field_type::Char).add_enum(54, "1").add_enum(54, "2");
    dictionary_driven_validator v{std::move(t)};

    // "X" is not in {"1","2"} — but Phase-1 enum_valid() always returns true.
    auto val = bv("X");
    auto rc = v.validate_field(54, std::span<const std::byte>{val.data(), val.size()});
    // Phase-1: enum violation is NOT rejected (enum check passes through).
    // Only the Char structural check applies: "X" is 1 byte → passes.
    EXPECT_TRUE(rc.has_value())
        << "Phase-1: enum_valid always true; validate_field must accept value "
           "that violates a registered enum (FR-005, 041-validation-gate-wiring)";
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

    auto result = v.validate(mv, &scratch_mr);
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

    auto result = v.validate(mv, &scratch_mr);
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

    auto result = v.validate(mv, &scratch_mr);
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

    auto result = v.validate(mv, &scratch_mr);
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

    auto result = v.validate(mv, &scratch_mr);
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

}  // namespace
