// tests/core/test_020_error_completeness.cpp
//
// 020-g2-business-messages T020 — exact-SET completeness for the single new
// error enumerator `app_payload_malformed = 131`, per
// [[feedback_completeness_gate_exact_set_not_subset]] (a recurring P1 class: a
// subset-presence check passes on row deletion — assert the exact slot SET with a
// message-bearing boundary instead).
//
// Anchor: specs/020-g2-business-messages/research.md D1 (opaque-payload
// validation); data-model.md INV-8; spec FR-016; tasks.md T020; [const §X.4]
// append-only.

#include <gtest/gtest.h>

#include <cstdint>
#include <fixpp/core/error.hpp>
#include <set>
#include <string_view>

using fixpp::core::error;

// ── Exact-set: the 020 block is EXACTLY {131} = app_payload_malformed ──────────
TEST(Error020Completeness, ExactSetEquality) {
    // The named enumerator (referenced by name → a DELETION/RENAME fails to
    // compile this TU) must occupy EXACTLY slot 131 — the next contiguous slot
    // after the 019 [129,130] block.
    const std::set<std::uint8_t> named_020_slots = {
        static_cast<std::uint8_t>(error::app_payload_malformed),
    };
    EXPECT_EQ(named_020_slots, (std::set<std::uint8_t>{131U}))
        << "020 must add EXACTLY app_payload_malformed at slot 131";

    // Slot 130 is the pre-020 boundary (019's app_callback_threw).
    EXPECT_EQ(static_cast<std::uint8_t>(error::app_callback_threw), 130U);

    // Message-table boundary: slot 131 carries the 020 message. The forward
    // "unknown" boundary is the slot after the newest block's last enumerator
    // (092's wire_length_data_mismatch; its own gate is test_092_*).
    EXPECT_NE(fixpp::core::error_message(error::app_payload_malformed),
              std::string_view{"unknown error"})
        << "slot 131 (app_payload_malformed) must carry a real message";
    EXPECT_EQ(fixpp::core::error_message(static_cast<error>(
                  static_cast<std::uint8_t>(error::wire_length_data_mismatch) + 1U)),
              std::string_view{"unknown error"})
        << "the slot after wire_length_data_mismatch must be unknown — nothing "
           "added beyond the 092 block";
}

// ── Message non-empty + to_string parity ──────────────────────────────────────
TEST(Error020Completeness, MessageNonEmpty) {
    const auto msg = fixpp::core::error_message(error::app_payload_malformed);
    EXPECT_FALSE(msg.empty());
    EXPECT_EQ(fixpp::core::to_string(error::app_payload_malformed), msg);
}
