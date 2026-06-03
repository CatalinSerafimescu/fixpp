// tests/core/test_017_error_completeness.cpp
//
// T008 — Completeness test: assert fixpp::core::error gained EXACTLY the 7 new
// enumerators at slots 122–128 with exact-SET equality (per
// [[feedback_completeness_gate_exact_set_not_subset]]), and smoke-assert that
// error_message/to_string returns a non-empty string for each (FR-015 /
// contracts/error-block.md). Also verifies the 017-specific uint8_t slot values.
//
// Anchor: contracts/error-block.md / [2k §6.3] / [const §X.4] / FR-015.

#include <gtest/gtest.h>

#include <fixpp/core/error.hpp>

#include <cstdint>
#include <set>
#include <string>
#include <string_view>

using fixpp::core::error;

// The 7 enumerators introduced by 017-log-otel at slots 122–128.
static const std::set<error> k017_expected_set = {
    error::log_queue_overflow,
    error::log_sink_open_failed,
    error::log_sink_write_failed,
    error::log_sink_flush_failed,
    error::log_drain_timeout,
    error::otel_export_failed,
    error::otel_provider_init_failed,
};

// Last pre-017 enumerator (015 boundary): slot 121.
static const std::uint8_t k_slot_before_017 = 121u;

// Expected slot for each 017 enumerator.
struct SlotCase {
    error e;
    std::uint8_t expected_slot;
    const char* name;
};

static const SlotCase k017_slots[] = {
    {error::log_queue_overflow,     122, "log_queue_overflow"},
    {error::log_sink_open_failed,   123, "log_sink_open_failed"},
    {error::log_sink_write_failed,  124, "log_sink_write_failed"},
    {error::log_sink_flush_failed,  125, "log_sink_flush_failed"},
    {error::log_drain_timeout,      126, "log_drain_timeout"},
    {error::otel_export_failed,     127, "otel_export_failed"},
    {error::otel_provider_init_failed, 128, "otel_provider_init_failed"},
};

// ── Exact-set equality (MISSING + UNEXPECTED diff) ──────────────────────────

TEST(Error017Completeness, ExactSetEquality) {
    // The 7 named enumerators (referenced by name in k017_expected_set, so a
    // DELETION or RENAME fails to compile this TU) must occupy EXACTLY the slot
    // set {122..128} — no alias (two names at one slot), none outside the block.
    // Casting the slot range to `error` would be vacuous (any uint8_t casts to a
    // valid enum value whether or not the enumerator is defined), so we instead
    // map the NAMED enumerators to their slots and compare to the contracted set.
    std::set<std::uint8_t> named_slots;
    for (error e : k017_expected_set) {
        named_slots.insert(static_cast<std::uint8_t>(e));
    }
    const std::set<std::uint8_t> expected_slots = {122u, 123u, 124u, 125u,
                                                   126u, 127u, 128u};
    EXPECT_EQ(named_slots, expected_slots)
        << "the 7 named 017 enumerators must occupy exactly slots 122-128 "
           "(no alias, none outside the block)";

    // EXACT-SET semantics, honestly scoped: the `named_slots == {122..128}`
    // check above IS the exact-set gate for the 7 KNOWN enumerators — deletion
    // or rename fails to compile (the names are referenced), an alias collapses
    // named_slots below 7, and a slot drift makes the sets unequal. The
    // orthogonal "no UNKNOWN 8th enumerator was added" direction is NOT
    // runtime-checkable in C++ without reflection (any uint8_t casts to a valid
    // enum value, and a message-less addition is indistinguishable at runtime —
    // verified by bite-test). The message-table boundary below is the closest
    // honest proxy: a properly-added variant MUST carry an error_message entry
    // (T006 contract + ErrorMessageNonEmpty), so a message at slot 131 (post-019)
    // signals an out-of-block addition.
    // NOTE: Slots 129–130 were added by 019-app-callbacks (app_do_not_send=129,
    // app_callback_threw=130) — the boundary moved from 128 to 130. The 019
    // error-completeness test (tests/core/test_019_error_completeness.cpp) pins
    // those slots exactly. [const §X.4] append-only review + abidiff gate govern.
    EXPECT_EQ(fixpp::core::error_message(static_cast<error>(131u)),
              std::string_view{"unknown error"})
        << "slot 131 carries a message — a message-bearing enumerator was added "
           "beyond the 019 [129,130] block (see [const §X.4] append-only review)";
}

// ── Slot values ─────────────────────────────────────────────────────────────

TEST(Error017Completeness, SlotValues) {
    // Slot 121 is the pre-017 boundary.
    EXPECT_EQ(static_cast<std::uint8_t>(error::session_unknown_acceptor_session),
              k_slot_before_017);

    // Verify each 017 enumerator occupies its contracted slot.
    for (const auto& c : k017_slots) {
        EXPECT_EQ(static_cast<std::uint8_t>(c.e), c.expected_slot)
            << c.name << " must be at slot " << c.expected_slot;
    }
}

// ── error_message non-empty smoke ────────────────────────────────────────────

TEST(Error017Completeness, ErrorMessageNonEmpty) {
    for (const auto& c : k017_slots) {
        std::string_view msg = fixpp::core::error_message(c.e);
        EXPECT_FALSE(msg.empty())
            << "error_message(" << c.name << ") must not be empty";
        EXPECT_NE(msg, "unknown error")
            << "error_message(" << c.name << ") returned 'unknown error'";
    }
}

// ── to_string non-empty smoke ─────────────────────────────────────────────────

TEST(Error017Completeness, ToStringNonEmpty) {
    for (const auto& c : k017_slots) {
        std::string_view s = fixpp::core::to_string(c.e);
        EXPECT_FALSE(s.empty())
            << "to_string(" << c.name << ") must not be empty";
        EXPECT_NE(s, "unknown error")
            << "to_string(" << c.name << ") returned 'unknown error'";
    }
}

// ── Enum is append-only: pre-017 highest slot still correct ─────────────────

TEST(Error017Completeness, AppendOnlySlot121Unchanged) {
    // [const §X.4] non-renumbering contract: slot 121 must remain
    // session_unknown_acceptor_session.
    EXPECT_EQ(static_cast<std::uint8_t>(error::session_unknown_acceptor_session), 121u);
}
