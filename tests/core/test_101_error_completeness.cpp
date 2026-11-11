// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/core/test_101_error_completeness.cpp
//
// 101-inbound-reject-fidelity T022 — exact-SET completeness for the two error enumerators 101
// appends for the validator (research D2; data-model `core::error`), modelled on
// test_092_error_completeness.cpp: the exact slot set, the messages, the C-ABI targets, the
// SessionRejectReason each validator result maps to, and the slot after the block unknown.
// Since 101 the "nothing added beyond the newest block" boundary lives here; test_017, test_019,
// test_020 and AbiSymbolGolden.ErrorEnumUnchanged name the same boundary by the last enumerator.
//
// The literals are spelled here independently of error.hpp, of src/capi/error.cpp and of
// reject_reason_map.hpp: a check that read its expectation from the code under test could not
// fail.

#include <gtest/gtest.h>

#include <cstdint>
#include <fixpp/core/error.hpp>
#include <fixpp/wire/reject_reason_map.hpp>
#include <set>
#include <string_view>

#include "fix/c_api/error.h"

// Engine-internal, not exported (src/capi/error.cpp's file header); declared as
// tests/capi/error_surface_test.cpp declares it.
namespace fixpp_capi::detail {
fixpp_error_t translate(fixpp::core::error e) noexcept;
}  // namespace fixpp_capi::detail

using fixpp::core::error;

// ── Exact set: the 101 block is the two slots after the 092 block ──────────────────────────
TEST(Error101Completeness, ExactSetEquality) {
    const std::set<std::uint8_t> named_101_slots = {
        static_cast<std::uint8_t>(error::wire_invalid_msg_type),
        static_cast<std::uint8_t>(error::wire_undefined_tag),
    };
    EXPECT_EQ(named_101_slots, (std::set<std::uint8_t>{134U, 135U}))
        << "101 adds exactly two enumerators, at slots 134 and 135";
    EXPECT_EQ(static_cast<std::uint8_t>(error::wire_invalid_msg_type), 134U);
    EXPECT_EQ(static_cast<std::uint8_t>(error::wire_undefined_tag), 135U);

    // The slot before the block is 092's last.
    EXPECT_EQ(static_cast<std::uint8_t>(error::wire_length_data_mismatch), 133U);

    // The slot after the block is unknown: nothing appended beyond 101.
    // The cast names the slot one past the last enumerator on purpose: the
    // assertion is that the slot has no message.
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    EXPECT_EQ(fixpp::core::error_message(static_cast<error>(136U)),
              std::string_view{"unknown error"})
        << "slot 136 must be unknown — nothing added beyond the 101 block";
}

// ── Messages: the strings T021 wrote, to_string parity ─────────────────────────────────────
TEST(Error101Completeness, Messages) {
    EXPECT_EQ(fixpp::core::error_message(error::wire_invalid_msg_type),
              std::string_view{"wire: invalid msg type"});
    EXPECT_EQ(fixpp::core::error_message(error::wire_undefined_tag),
              std::string_view{"wire: undefined tag"});
    EXPECT_EQ(fixpp::core::to_string(error::wire_invalid_msg_type),
              fixpp::core::error_message(error::wire_invalid_msg_type));
    EXPECT_EQ(fixpp::core::to_string(error::wire_undefined_tag),
              fixpp::core::error_message(error::wire_undefined_tag));
}

// ── C-ABI: both are dictionary conformance results, no new C code ([const §X.1]) ───────────
TEST(Error101Completeness, TranslateTargets) {
    EXPECT_EQ(fixpp_capi::detail::translate(error::wire_invalid_msg_type),
              FIXPP_ERR_WIRE_CONFORMANCE);
    EXPECT_EQ(fixpp_capi::detail::translate(error::wire_undefined_tag), FIXPP_ERR_WIRE_CONFORMANCE);
}

// ── SessionRejectReason(373) of every slot the validator returns ───────────────────────────
// The two new slots, and the rows a Session cell cannot reach: reason 6 (decimal_t::parse never
// returns decimal_precision_loss for a representable mantissa, so the validator does not return
// wire_field_value_truncated) and reason 14 on a non-FIXT session (Step 0 answers it only on a
// frame whose field 3 is not 35, which 093 disregards before the validation gate).
TEST(Error101Completeness, SessionRejectReasonOfEveryValidatorResult) {
    using fixpp::wire::wire_error_to_session_reject_reason;
    EXPECT_EQ(wire_error_to_session_reject_reason(error::wire_invalid_msg_type), 11);
    EXPECT_EQ(wire_error_to_session_reject_reason(error::wire_undefined_tag), 0);
    EXPECT_EQ(wire_error_to_session_reject_reason(error::wire_unexpected_tag), 2);
    EXPECT_EQ(wire_error_to_session_reject_reason(error::wire_required_field_missing), 1);
    EXPECT_EQ(wire_error_to_session_reject_reason(error::wire_field_value_out_of_range), 5);
    EXPECT_EQ(wire_error_to_session_reject_reason(error::wire_field_value_truncated), 6);
    EXPECT_EQ(wire_error_to_session_reject_reason(error::wire_header_out_of_order), 14);
    EXPECT_EQ(wire_error_to_session_reject_reason(error::wire_invalid_tag_number), 0);
    EXPECT_EQ(wire_error_to_session_reject_reason(error::wire_length_data_mismatch), 5);
}
