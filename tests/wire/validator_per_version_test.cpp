// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/validator_per_version_test.cpp — T044 (US4, SC-005).
// Parameterized GTest over the four FIX wire versions {4.2, 4.4, 5.0SP2, T1.1}.
// For each version: one conforming message (expect has_value()) plus ≥ 3
// non-conforming variants (missing-required / bad-enum / unexpected-tag).
// ZERO false accepts: every non-conforming message must be rejected.
//
// RED until T046 defines dictionary_driven_validator's five overrides.
//
// Include order is mandatory (seam #1 single-definition rule):
//   mock_dict_table.hpp MUST precede <fixpp/wire/validator.hpp>.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

// ── seam #1 ──────────────────────────────────────────────────────────────────
#include "support/mock_dict_table.hpp"
#include <fixpp/wire/validator.hpp>
#include <fixpp/wire/parser.hpp>
#include "support/frame_view_factory.hpp"

namespace {

using fixpp::wire::access_mode;
using fixpp::wire::MessageView;
using fixpp::wire::Parser;
using fixpp::wire::dictionary_driven_validator;
using fixpp::dict::table_view;
using fixpp::dict::field_type;
using fixpp::core::error;

// ── Frame builder ─────────────────────────────────────────────────────────────

// Builds a CheckSum+BodyLength-correct FIX frame for the given BeginString
// version prefix.  version_tag8 must be one of:
//   "FIX.4.2" | "FIX.4.4" | "FIX.5.0SP2" | "FIXT.1.1"
std::vector<std::byte> make_versioned_frame(std::string_view version_tag8,
                                             std::string_view body_fields) {
    std::string body{body_fields};
    std::string nine = "9=" + std::to_string(body.size()) + "\x01";
    std::string pre = "8=" + std::string{version_tag8} + "\x01" + nine + body;
    unsigned sum = 0;
    for (unsigned char c : pre) {
        sum += c;
    }
    char chk[16];
    std::snprintf(chk, sizeof(chk), "10=%03u\x01", sum % 256U);
    std::string full = pre + chk;
    std::vector<std::byte> out(full.size());
    std::memcpy(out.data(), full.data(), full.size());
    return out;
}

// ── Per-version grammar factory ───────────────────────────────────────────────
// Every version defines a "Heartbeat (35=0)" grammar:
//   required: 8, 9, 35, 49, 56, 34, 10
//   valid (optional): 112 (TestReqID), 98 (EncryptMethod in some versions)
//   tag 98 (EncryptMethod) is enumerated: only "0" (no encryption) and "1"
// Tag 9999 is NOT registered as valid (used in unexpected-tag tests).
table_view make_heartbeat_grammar() {
    table_view t;
    t.add_required("0", 8)
     .add_required("0", 9)
     .add_required("0", 35)
     .add_required("0", 49)
     .add_required("0", 56)
     .add_required("0", 34)
     .add_required("0", 10)
     .add_valid("0", 112)     // TestReqID (optional)
     .add_valid("0", 98)      // EncryptMethod (enumerated optional)
     .set_type(34, field_type::Int)
     .set_type(112, field_type::String)
     .set_type(98, field_type::Int)
     .add_enum(98, "0")
     .add_enum(98, "1");
    return t;
}

// ── MessageView builder ───────────────────────────────────────────────────────

constexpr std::size_t kParseArenaSize  = 4096;
constexpr std::size_t kScratchSize     = 2048;

// In-place arena construction to avoid placement new/delete complexity.
// Returns a default-constructed MessageView on failure (GTest already marks
// the test failed via ADD_FAILURE()).
MessageView<access_mode::Index>
parse_index(std::vector<std::byte> const& buf,
            std::array<std::byte, kParseArenaSize>& stack_buf,
            std::pmr::monotonic_buffer_resource& arena_out) {
    new (&arena_out) std::pmr::monotonic_buffer_resource{
        stack_buf.data(), stack_buf.size(),
        std::pmr::null_memory_resource()};

    auto fv = fixpp::wire::test::make_frame_view(buf);
    if (!fv.has_value()) {
        ADD_FAILURE() << "make_frame_view failed: "
                      << static_cast<int>(fv.error());
        return {};
    }
    Parser<access_mode::Index> parser{table_view{}};
    auto mv = parser.parse(*fv, &arena_out);
    if (!mv.has_value()) {
        ADD_FAILURE() << "parser.parse failed: "
                      << static_cast<int>(mv.error());
        return {};
    }
    return *mv;
}

// ── Test parameter ────────────────────────────────────────────────────────────

struct VersionParam {
    std::string_view label;        // human-readable (used in SCOPED_TRACE)
    std::string_view tag8_value;   // the BeginString value after "8="
};

// The four wire versions from SC-005.
constexpr VersionParam kVersions[] = {
    {"FIX.4.2",    "FIX.4.2"},
    {"FIX.4.4",    "FIX.4.4"},
    {"FIX.5.0SP2", "FIX.5.0SP2"},
    {"FIXT.1.1",   "FIXT.1.1"},
};

// ── Parameterized test fixture ────────────────────────────────────────────────

class ValidatorPerVersion : public ::testing::TestWithParam<VersionParam> {};

// Helper: run validate and return the result.
fixpp::core::expected_t<void>
do_validate(dictionary_driven_validator const& v,
            std::vector<std::byte> const& buf) {
    std::array<std::byte, kParseArenaSize> stack{};
    std::pmr::monotonic_buffer_resource arena;
    auto mv = parse_index(buf, stack, arena);

    std::array<std::byte, kScratchSize> scratch_buf{};
    std::pmr::monotonic_buffer_resource scratch_mr{
        scratch_buf.data(), scratch_buf.size(),
        std::pmr::null_memory_resource()};

    return v.validate(mv, &scratch_mr);
}

// ── 1. Conforming Heartbeat is accepted ───────────────────────────────────────
TEST_P(ValidatorPerVersion, ConformingHeartbeatAccepted) {
    auto const& p = GetParam();
    SCOPED_TRACE(p.label);

    dictionary_driven_validator v{make_heartbeat_grammar()};

    auto buf = make_versioned_frame(
        p.tag8_value,
        "35=0\x01" "49=SENDER\x01" "56=TARGET\x01" "34=1\x01");

    auto result = do_validate(v, buf);
    EXPECT_TRUE(result.has_value())
        << "conforming Heartbeat for " << p.label << " must validate OK"
        << "; error=" << (result.has_value() ? 0
                                              : static_cast<int>(result.error()));
}

// ── 2. Missing required field → wire_required_field_missing ──────────────────
// SenderCompID (tag 49) is required but absent.
TEST_P(ValidatorPerVersion, MissingRequiredSenderRejected) {
    auto const& p = GetParam();
    SCOPED_TRACE(p.label);

    dictionary_driven_validator v{make_heartbeat_grammar()};

    auto buf = make_versioned_frame(
        p.tag8_value,
        "35=0\x01" "56=TARGET\x01" "34=1\x01");  // tag 49 omitted

    auto result = do_validate(v, buf);
    ASSERT_FALSE(result.has_value())
        << p.label << ": missing SenderCompID (49) must be rejected";
    EXPECT_EQ(result.error(), error::wire_required_field_missing)
        << p.label << ": expected wire_required_field_missing (38), got "
        << static_cast<int>(result.error());
}

// ── 3. Out-of-range enum → wire_field_value_out_of_range ─────────────────────
// EncryptMethod (tag 98) = "9" is not in the {"0","1"} enum set.
TEST_P(ValidatorPerVersion, BadEnumEncryptMethodRejected) {
    auto const& p = GetParam();
    SCOPED_TRACE(p.label);

    dictionary_driven_validator v{make_heartbeat_grammar()};

    auto buf = make_versioned_frame(
        p.tag8_value,
        "35=0\x01" "49=SENDER\x01" "56=TARGET\x01" "34=1\x01"
        "98=9\x01");  // illegal enum value for EncryptMethod

    auto result = do_validate(v, buf);
    ASSERT_FALSE(result.has_value())
        << p.label << ": EncryptMethod=9 (invalid enum) must be rejected";
    EXPECT_EQ(result.error(), error::wire_field_value_out_of_range)
        << p.label << ": expected wire_field_value_out_of_range (40), got "
        << static_cast<int>(result.error());
}

// ── 4. Unexpected tag → wire_unexpected_tag ───────────────────────────────────
// Tag 9999 is not registered as valid for "0" (Heartbeat) in any version.
TEST_P(ValidatorPerVersion, UnexpectedTagRejected) {
    auto const& p = GetParam();
    SCOPED_TRACE(p.label);

    dictionary_driven_validator v{make_heartbeat_grammar()};

    auto buf = make_versioned_frame(
        p.tag8_value,
        "35=0\x01" "49=SENDER\x01" "56=TARGET\x01" "34=1\x01"
        "9999=BOGUS\x01");  // not valid for Heartbeat

    auto result = do_validate(v, buf);
    ASSERT_FALSE(result.has_value())
        << p.label << ": unexpected tag 9999 must be rejected";
    EXPECT_EQ(result.error(), error::wire_unexpected_tag)
        << p.label << ": expected wire_unexpected_tag (42), got "
        << static_cast<int>(result.error());
}

INSTANTIATE_TEST_SUITE_P(
    AllVersions,
    ValidatorPerVersion,
    ::testing::ValuesIn(kVersions),
    [](::testing::TestParamInfo<VersionParam> const& info) {
        // Replace dots with underscores for valid GTest name.
        std::string name{info.param.label};
        for (char& c : name) {
            if (c == '.' || c == ' ') {
                c = '_';
            }
        }
        return name;
    });

}  // namespace
