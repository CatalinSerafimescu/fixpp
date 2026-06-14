// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_resend_reply_possdup.cpp — T001/T004 [US1] Phase 1+3
//
// 037-resend-reply-possdup-tags: GapFill carries PossDupFlag(43)=Y + OrigSendingTime(122).
//
// Cell 1 (T004): Call build_sequence_reset_gapfill directly with a known sending_time.
// Assert IN ORDER:
//   (1) field_value(35)=="4" AND field_value(123)=="Y" (this IS a GapFill)
//   (2) count_tag(43)==1 AND field_value(43)=="Y"
//   (3) count_tag(122)==1
//   (4) field_value(122)==field_value(52)  (122 = own 52, as per FR-002/D-1)
//   (5) tags 8/35/34/49/52/56/36/123 all present
//
// RED: today's builder emits no 43/122 → assertions (2)/(3)/(4) fail.
// GREEN after T006 (append 43=Y + 122=<sending_time> in build_sequence_reset_gapfill).
//
// Anchors: spec.md FR-001/FR-002/FR-003; plan.md T004; tasks.md §Phase 3.
// Build: cmake --build build/linux-clang-debug --target session_resend_reply_possdup -j2
// Run:   ctest --test-dir build/linux-clang-debug -R session_resend_reply_possdup -V

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <fixpp/session/admin_messages.hpp>

namespace {

// ── FIX frame field helpers ──────────────────────────────────────────────────
//
// count_tag: count occurrences of <tag>= at a field boundary (SOH-anchored).
// field_value: return the value of the FIRST occurrence of <tag>, or nullopt.
//
// Both walk field-by-field in the numeric-tag/equals/value/SOH grammar,
// matching the frame_field_extract.cpp parse model so that "122=" is not
// confused with "123=".

static std::size_t count_tag(std::span<const std::byte> frame, std::uint32_t tag) noexcept {
    std::size_t count = 0;
    std::size_t i = 0;
    const std::size_t n = frame.size();
    while (i < n) {
        // Parse tag digits.
        const std::size_t tag_begin = i;
        while (i < n && frame[i] != std::byte{'='} && frame[i] != std::byte{0x01}) {
            ++i;
        }
        if (i >= n || frame[i] != std::byte{'='}) {
            break;  // malformed
        }
        // Parse the tag number.
        std::uint32_t parsed = 0;
        for (std::size_t k = tag_begin; k < i; ++k) {
            char c = static_cast<char>(frame[k]);
            if (c < '0' || c > '9') { parsed = 0; break; }
            parsed = parsed * 10U + static_cast<std::uint32_t>(c - '0');
        }
        ++i;  // past '='
        // Skip value until SOH.
        while (i < n && frame[i] != std::byte{0x01}) {
            ++i;
        }
        if (parsed == tag) {
            ++count;
        }
        if (i < n) ++i;  // past SOH
    }
    return count;
}

static std::optional<std::string_view> field_value(std::span<const std::byte> frame,
                                                    std::uint32_t tag) noexcept {
    std::size_t i = 0;
    const std::size_t n = frame.size();
    while (i < n) {
        const std::size_t tag_begin = i;
        while (i < n && frame[i] != std::byte{'='} && frame[i] != std::byte{0x01}) {
            ++i;
        }
        if (i >= n || frame[i] != std::byte{'='}) {
            break;
        }
        std::uint32_t parsed = 0;
        for (std::size_t k = tag_begin; k < i; ++k) {
            char c = static_cast<char>(frame[k]);
            if (c < '0' || c > '9') { parsed = 0; break; }
            parsed = parsed * 10U + static_cast<std::uint32_t>(c - '0');
        }
        ++i;  // past '='
        const std::size_t val_begin = i;
        while (i < n && frame[i] != std::byte{0x01}) {
            ++i;
        }
        if (parsed == tag) {
            return std::string_view{reinterpret_cast<const char*>(frame.data() + val_begin),
                                    i - val_begin};
        }
        if (i < n) ++i;
    }
    return std::nullopt;
}

}  // namespace

// ── Cell 1: GapFill carries PossDupFlag(43)=Y + OrigSendingTime(122) ─────────
//
// T004 [P] [US1]: Write FIRST, must FAIL today (builder emits no 43/122).
//
// Honesty: (1) asserts this IS a GapFill (35=4 + 123=Y) before asserting 43/122.
// FR-001: 43=Y present exactly once.
// FR-002/D-1: 122 == own 52 (the sending_time param is used for both).
// FR-003: standard header tags 8/35/34/49/52/56 + body tags 36/123 present.
//
// RED assertion lines: EXPECT_EQ(count_tag(43),1) and EXPECT_EQ(count_tag(122),1).
TEST(ResendReplyPossDup, Cell1_GapFill_Carries_43Y_And_122_EqOwn52) {
    constexpr std::string_view kSender = "ISLD";
    constexpr std::string_view kTarget = "TW";
    constexpr std::string_view kBeginString = "FIX.4.2";
    constexpr std::string_view kSendingTime = "20260614-12:00:00.000";
    constexpr fixpp::session::seqnum_t kSeq = 5;
    constexpr fixpp::session::seqnum_t kNewSeqno = 10;

    std::array<std::byte, 512> buf{};
    auto result = fixpp::session::build_sequence_reset_gapfill(
        std::span<std::byte>{buf}, kSeq, kSender, kTarget, kNewSeqno, kBeginString, kSendingTime);

    ASSERT_TRUE(result.has_value()) << "build_sequence_reset_gapfill must succeed";
    const auto frame = *result;

    // (1) Honesty: confirm this IS a GapFill. [FR-003]
    ASSERT_EQ(field_value(frame, 35), "4")
        << "Cell1 pre-condition: MsgType must be 4 (SequenceReset)";
    ASSERT_EQ(field_value(frame, 123), "Y")
        << "Cell1 pre-condition: GapFillFlag(123) must be Y";

    // (2) PossDupFlag(43): exactly one occurrence, value Y. [FR-001]
    // RED: builder emits no 43 today → count==0.
    EXPECT_EQ(count_tag(frame, 43), 1u)
        << "Cell1 RED: 43=Y must appear exactly once in the GapFill (FR-001); "
           "today's builder does not append tag 43 → count==0";
    EXPECT_EQ(field_value(frame, 43), "Y")
        << "Cell1 RED: PossDupFlag(43) value must be Y (FR-001)";

    // (3) OrigSendingTime(122): exactly one occurrence. [FR-002]
    // RED: builder emits no 122 today → count==0.
    EXPECT_EQ(count_tag(frame, 122), 1u)
        << "Cell1 RED: 122 must appear exactly once in the GapFill (FR-002); "
           "today's builder does not append tag 122 → count==0";

    // (4) 122 == own 52 (the same sending_time param). [FR-002/D-1]
    const auto val_52 = field_value(frame, 52);
    const auto val_122 = field_value(frame, 122);
    ASSERT_TRUE(val_52.has_value()) << "Tag 52 (SendingTime) must be present";
    EXPECT_EQ(val_122, val_52)
        << "Cell1: 122 (OrigSendingTime) must equal 52 (own SendingTime) per FR-002/D-1";

    // (5) Standard field set present. [FR-003]
    // (8/52/36 presence is subsumed by the EXPECT_EQ value checks below; 123 by the
    //  ASSERT_EQ at the honesty step. Only 34/49/56 need an explicit presence check.)
    EXPECT_TRUE(field_value(frame,  34).has_value()) << "Tag 34 (MsgSeqNum) must be present";
    EXPECT_TRUE(field_value(frame,  49).has_value()) << "Tag 49 (SenderCompID) must be present";
    EXPECT_TRUE(field_value(frame,  56).has_value()) << "Tag 56 (TargetCompID) must be present";
    EXPECT_EQ(field_value(frame,   8), kBeginString) << "Tag 8 must match begin_string param";
    EXPECT_EQ(field_value(frame,  52), kSendingTime) << "Tag 52 must match sending_time param";
    EXPECT_EQ(field_value(frame,  36), "10")         << "Tag 36 must match new_seqno=10";
}
