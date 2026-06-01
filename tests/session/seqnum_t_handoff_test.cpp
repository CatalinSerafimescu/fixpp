// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/seqnum_t_handoff_test.cpp
//
// Seam #13 — 005-owned seqnum_t build/consumer check.
//
// Verifies that <fixpp/session/seqnum.hpp> resolves to the 005-owned canonical
// type (I-10, SC-010, D-1, [2e §10 Q9] handoff). This is a BUILD and VALUE
// check: if seqnum.hpp contains only the placeholder comment block and has not
// been promoted to the 005-owned form, the static_asserts below fire at
// compile time, and/or the TEST assertions fail at runtime.
//
// Anchor: data-model.md E4, research D-1, contracts/seqnum.hpp.
// FR: SC-010 (consumer-side build check).
#include <gtest/gtest.h>

#include <cstdint>
#include <fixpp/session/seqnum.hpp>
#include <limits>
#include <type_traits>

namespace fixpp::session::test {
namespace {

// ── Static (compile-time) checks ─────────────────────────────────────────────
// These fire at build time if the type or constant declarations are absent or
// wrong; the TDD "red" phase here is a compile error, not a link error.

static_assert(std::is_same_v<seqnum_t, std::uint32_t>,
              "seqnum_t must be std::uint32_t (D-1, [2e §4.7])");

static_assert(seqnum_min == 1u, "seqnum_min must be 1 per [FIX-SL §4.1]");

static_assert(seqnum_max == std::numeric_limits<std::uint32_t>::max(),
              "seqnum_max must be UINT32_MAX per data-model.md E4");

// Overflow check: seqnum_max + 1 must wrap (checked purely at the type level;
// the session-fatal no-wrap contract is enforced at runtime by store_seqnum_overflow).
static_assert(static_cast<seqnum_t>(static_cast<std::uint64_t>(seqnum_max) + 1) == 0u,
              "seqnum_t wraps arithmetically; session-fatal guard is in the store");

// ── Runtime checks ───────────────────────────────────────────────────────────

TEST(SeqnumTHandoff, TypeWidthIs32Bits) {
    // sizeof(seqnum_t) == 4 bytes: matches the [2e §4.7] width convention
    // (QuickFIX C++, fix8, QuickFIX/J are all 32-bit).
    EXPECT_EQ(sizeof(seqnum_t), 4u);
}

TEST(SeqnumTHandoff, MinIs1) {
    // FIX seqnums start at 1 per [FIX-SL §4.1]; 0 is not a valid wire value.
    EXPECT_EQ(seqnum_min, seqnum_t{1});
}

TEST(SeqnumTHandoff, MaxIsUint32Max) {
    // UINT32_MAX — session-fatal no-wrap is enforced by store_seqnum_overflow
    // when next_seqnum(dir, true) hits this ceiling (D-1, [2e §6.7]).
    EXPECT_EQ(seqnum_max, std::numeric_limits<std::uint32_t>::max());
}

TEST(SeqnumTHandoff, MinLessThanMax) { EXPECT_LT(seqnum_min, seqnum_max); }

TEST(SeqnumTHandoff, HeaderResolvesWithoutPlaceholderAnnotation) {
    // If the PLACEHOLDER comment block is still present but the type+consts
    // declarations are also present, this test still passes (type-OK).
    // The purpose is to confirm the include chain resolves and the values
    // are byte-identical to what 008's consumers expected from the placeholder.
    seqnum_t s = seqnum_min;
    EXPECT_EQ(s, 1u);
    s = seqnum_max;
    EXPECT_EQ(s, std::numeric_limits<std::uint32_t>::max());
}

}  // namespace
}  // namespace fixpp::session::test
