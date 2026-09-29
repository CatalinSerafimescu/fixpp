// tests/core/test_092_error_completeness.cpp
//
// 092-garbled-frame-reject T060 — exact-SET completeness for the two error
// enumerators 092 appends for the validator's field faults, modelled on
// test_020_error_completeness.cpp: assert the exact slot set with a
// message-bearing boundary, since a subset-presence check passes on a row
// deletion.
//
// Anchors: specs/092-garbled-frame-reject/data-model.md E-6; research.md R-7;
// spec FR-012; [const §X.4] append-only.

#include <gtest/gtest.h>

#include <cstdint>
#include <fixpp/core/error.hpp>
#include <set>
#include <string_view>

using fixpp::core::error;

// ── Exact-set: the 092 block is EXACTLY {132, 133} ──────────────────────────
TEST(Error092Completeness, ExactSetEquality) {
    // Referenced by name, so a deletion or rename fails to compile this TU; the
    // literals are spelled here independently of error.hpp.
    const std::set<std::uint8_t> named_092_slots = {
        static_cast<std::uint8_t>(error::wire_invalid_tag_number),
        static_cast<std::uint8_t>(error::wire_length_data_mismatch),
    };
    EXPECT_EQ(named_092_slots, (std::set<std::uint8_t>{132U, 133U}))
        << "092 must add EXACTLY two enumerators, at slots 132 and 133";
    EXPECT_EQ(static_cast<std::uint8_t>(error::wire_invalid_tag_number), 132U);
    EXPECT_EQ(static_cast<std::uint8_t>(error::wire_length_data_mismatch), 133U);

    // The slot before the block is 020's app_payload_malformed.
    EXPECT_EQ(static_cast<std::uint8_t>(error::app_payload_malformed), 131U);

    // Message-table boundary: both 092 slots carry a message, and the slot
    // after the block is unknown (nothing appended beyond 092).
    EXPECT_NE(fixpp::core::error_message(error::wire_invalid_tag_number),
              std::string_view{"unknown error"});
    EXPECT_NE(fixpp::core::error_message(error::wire_length_data_mismatch),
              std::string_view{"unknown error"});
    // The cast names the slot one past the last enumerator on purpose: the
    // assertion is that the slot has no message.
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    EXPECT_EQ(fixpp::core::error_message(static_cast<error>(134U)),
              std::string_view{"unknown error"})
        << "slot 134 must be unknown — nothing added beyond the 092 block";
}

// ── Messages: non-empty, distinct, to_string parity ─────────────────────────
TEST(Error092Completeness, MessagesNonEmptyAndDistinct) {
    const auto tag_msg = fixpp::core::error_message(error::wire_invalid_tag_number);
    const auto ld_msg = fixpp::core::error_message(error::wire_length_data_mismatch);
    EXPECT_FALSE(tag_msg.empty());
    EXPECT_FALSE(ld_msg.empty());
    EXPECT_NE(tag_msg, ld_msg);
    // Neither reuses the Index build's generic message.
    EXPECT_NE(tag_msg, fixpp::core::error_message(error::wire_invalid_field_format));
    EXPECT_NE(ld_msg, fixpp::core::error_message(error::wire_invalid_field_format));
    EXPECT_EQ(fixpp::core::to_string(error::wire_invalid_tag_number), tag_msg);
    EXPECT_EQ(fixpp::core::to_string(error::wire_length_data_mismatch), ld_msg);
}
