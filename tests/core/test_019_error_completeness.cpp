// tests/core/test_019_error_completeness.cpp
//
// T004 — Completeness test: assert fixpp::core::error gained EXACTLY the 2 new
// enumerators at slots 129–130 with exact-SET equality (per
// [[feedback_completeness_gate_exact_set_not_subset]]), and smoke-assert that
// error_message/to_string returns a non-empty string for each (FR-015 /
// research D7). Also verifies the 019-specific uint8_t slot values.
//
// Anchor: specs/019-app-callbacks/research.md D7; data-model.md §"New error::
// enumerators"; [const §X.4] append-only; tasks.md T004.

#include <gtest/gtest.h>

#include <fixpp/core/error.hpp>

#include <cstdint>
#include <set>
#include <string_view>

using fixpp::core::error;

// The 2 enumerators introduced by 019-app-callbacks at slots 129–130.
static const std::set<error> k019_expected_set = {
    error::app_do_not_send,
    error::app_callback_threw,
};

// Last pre-019 enumerator (017 boundary): slot 128.
static const std::uint8_t k_slot_before_019 = 128u;

// Expected slot for each 019 enumerator.
struct SlotCase {
    error e;
    std::uint8_t expected_slot;
    const char* name;
};

static const SlotCase k019_slots[] = {
    {error::app_do_not_send,    129, "app_do_not_send"},
    {error::app_callback_threw, 130, "app_callback_threw"},
};

// ── Exact-set equality (MISSING + UNEXPECTED diff) ──────────────────────────

TEST(Error019Completeness, ExactSetEquality) {
    // The 2 named enumerators (referenced by name in k019_expected_set, so a
    // DELETION or RENAME fails to compile this TU) must occupy EXACTLY the slot
    // set {129,130} — no alias (two names at one slot), none outside the block.
    std::set<std::uint8_t> named_slots;
    for (error e : k019_expected_set) {
        named_slots.insert(static_cast<std::uint8_t>(e));
    }
    const std::set<std::uint8_t> expected_slots = {129u, 130u};
    EXPECT_EQ(named_slots, expected_slots)
        << "the 2 named 019 enumerators must occupy exactly slots 129-130 "
           "(no alias, none outside the block)";

    // Message-table boundary: slot 131 must return "unknown error" (no 019+1
    // enumerator was added without a message entry). [const §X.4] append-only.
    EXPECT_EQ(fixpp::core::error_message(static_cast<error>(131u)),
              std::string_view{"unknown error"})
        << "slot 131 carries a message — a message-bearing enumerator was added "
           "beyond the 019 [129,130] block (see [const §X.4] append-only review)";
}

// ── Slot values ─────────────────────────────────────────────────────────────

TEST(Error019Completeness, SlotValues) {
    // Slot 128 is the pre-019 boundary (017's otel_provider_init_failed).
    EXPECT_EQ(static_cast<std::uint8_t>(error::otel_provider_init_failed),
              k_slot_before_019);

    // Verify each 019 enumerator occupies its contracted slot.
    for (const auto& c : k019_slots) {
        EXPECT_EQ(static_cast<std::uint8_t>(c.e), c.expected_slot)
            << c.name << " must be at slot " << c.expected_slot;
    }
}

// ── error_message non-empty smoke ────────────────────────────────────────────

TEST(Error019Completeness, ErrorMessageNonEmpty) {
    for (const auto& c : k019_slots) {
        std::string_view msg = fixpp::core::error_message(c.e);
        EXPECT_FALSE(msg.empty())
            << "error_message(" << c.name << ") must not be empty";
        EXPECT_NE(msg, "unknown error")
            << "error_message(" << c.name << ") returned 'unknown error'";
    }
}

// ── to_string non-empty smoke ─────────────────────────────────────────────────

TEST(Error019Completeness, ToStringNonEmpty) {
    for (const auto& c : k019_slots) {
        std::string_view s = fixpp::core::to_string(c.e);
        EXPECT_FALSE(s.empty())
            << "to_string(" << c.name << ") must not be empty";
        EXPECT_NE(s, "unknown error")
            << "to_string(" << c.name << ") returned 'unknown error'";
    }
}

// ── Enum is append-only: pre-019 highest slot still correct ─────────────────

TEST(Error019Completeness, AppendOnlySlot128Unchanged) {
    // [const §X.4] non-renumbering contract: slot 128 must remain
    // otel_provider_init_failed (017's last slot).
    EXPECT_EQ(static_cast<std::uint8_t>(error::otel_provider_init_failed), 128u);
}
