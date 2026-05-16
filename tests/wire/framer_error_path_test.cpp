// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/wire/framer_error_path_test.cpp — T055 coverage hardening.
// Targeted error-path tests for fixpp::wire::Framer / parse_frame covering
// the uncovered branches in src/wire/framer.cpp:
//   - bytes[0] != '8' → wire_framing_resync
//   - bytes[1] != '=' → wire_framing_resync
//   - 9= missing (next field is not 9=) → wire_invalid_body_length
//   - body_length == 0 → wire_invalid_body_length
//   - body_length_end == body_length_value (empty value) → wire_invalid_body_length
//   - frame_len > max_frame_bytes after full parse → wire_frame_too_large
//   - checksum non-digit byte → wire_checksum_mismatch
//   - 10= SOH mismatch (6th checksum-trailer byte not SOH) → wire_checksum_mismatch
//   - 10= marker not present at checksum_off → wire_invalid_body_length
//   - trailing SOH of body missing → wire_invalid_body_length
//   - carry append fails during non-carry trailing save → wire_frame_too_large
//   - out buffer full (produced == out.size()) triggers break

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <fixpp/core/error.hpp>
#include <fixpp/wire/framer.hpp>

namespace {

using fixpp::core::error;
using fixpp::wire::Framer;
using fixpp::wire::frame_view;
using fixpp::wire::pmr_carry_buffer;

constexpr char soh = '\x01';

[[nodiscard]] std::vector<std::byte> to_bytes(std::string_view text) {
    std::vector<std::byte> out;
    out.reserve(text.size());
    for (char ch : text) {
        out.push_back(static_cast<std::byte>(ch));
    }
    return out;
}

// Build a well-formed frame from a body string (no framing tags included).
[[nodiscard]] std::vector<std::byte> make_frame(std::string_view body) {
    std::string frame = "8=FIX.4.4";
    frame.push_back(soh);
    frame.append("9=");
    frame.append(std::to_string(body.size()));
    frame.push_back(soh);
    frame.append(body);

    unsigned checksum = 0;
    for (unsigned char ch : frame) {
        checksum += static_cast<unsigned>(ch);
    }
    checksum %= 256U;

    frame.push_back('1');
    frame.push_back('0');
    frame.push_back('=');
    frame.push_back(static_cast<char>('0' + ((checksum / 100U) % 10U)));
    frame.push_back(static_cast<char>('0' + ((checksum / 10U) % 10U)));
    frame.push_back(static_cast<char>('0' + (checksum % 10U)));
    frame.push_back(soh);

    return to_bytes(frame);
}

// Feed a single-chunk frame into a framer and return the error code.
fixpp::core::expected_t<std::span<frame_view>>
feed_one(Framer& framer, std::vector<std::byte> const& frame,
         pmr_carry_buffer& carry, std::array<frame_view, 4>& out) {
    return framer.feed(
        std::span<const std::byte>{frame.data(), frame.size()},
        carry, out);
}

// ── wire_framing_resync: bytes[0] != '8' ─────────────────────────────────────

TEST(FramerErrorPath, ResyncOnNonBeginStringByte) {
    std::array<std::byte, 1024> arena_storage{};
    std::pmr::monotonic_buffer_resource arena{arena_storage.data(),
                                              arena_storage.size()};
    Framer framer{};
    pmr_carry_buffer carry{256, &arena};
    std::array<frame_view, 4> out{};

    // Start with 'X' instead of '8'.
    auto bad_frame = to_bytes("X=FIX.4.4\x01" "9=5\x01" "35=D\x01" "10=000\x01");
    auto result = framer.feed(bad_frame, carry, out);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), error::wire_framing_resync);
    EXPECT_EQ(framer.pending_bytes(), 0U);
}

// ── wire_framing_resync: bytes[1] != '=' ─────────────────────────────────────

TEST(FramerErrorPath, ResyncOnNonEqualAfterEight) {
    std::array<std::byte, 1024> arena_storage{};
    std::pmr::monotonic_buffer_resource arena{arena_storage.data(),
                                              arena_storage.size()};
    Framer framer{};
    pmr_carry_buffer carry{256, &arena};
    std::array<frame_view, 4> out{};

    // "8X..." — second byte is not '='.
    auto bad_frame = to_bytes("8XFIX.4.4\x01" "9=5\x01" "35=D\x01" "10=000\x01");
    auto result = framer.feed(bad_frame, carry, out);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), error::wire_framing_resync);
    EXPECT_EQ(framer.pending_bytes(), 0U);
}

// ── wire_invalid_body_length: next field after 8= is not 9= ─────────────────

TEST(FramerErrorPath, InvalidBodyLengthWhenSecondTagIsNot9) {
    std::array<std::byte, 1024> arena_storage{};
    std::pmr::monotonic_buffer_resource arena{arena_storage.data(),
                                              arena_storage.size()};
    Framer framer{};
    pmr_carry_buffer carry{256, &arena};
    std::array<frame_view, 4> out{};

    // "8=FIX.4.4\x01 35=D\x01 ..." — second field is 35 not 9.
    auto bad_frame = to_bytes("8=FIX.4.4\x01" "35=D\x01" "9=5\x01" "10=000\x01");
    auto result = framer.feed(bad_frame, carry, out);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), error::wire_invalid_body_length);
    EXPECT_EQ(framer.pending_bytes(), 0U);
}

// ── wire_invalid_body_length: 9= with empty value ────────────────────────────

TEST(FramerErrorPath, InvalidBodyLengthEmptyValue) {
    std::array<std::byte, 1024> arena_storage{};
    std::pmr::monotonic_buffer_resource arena{arena_storage.data(),
                                              arena_storage.size()};
    Framer framer{};
    pmr_carry_buffer carry{256, &arena};
    std::array<frame_view, 4> out{};

    // "9=" immediately followed by SOH (empty value).
    auto bad_frame = to_bytes("8=FIX.4.4\x01" "9=\x01" "35=D\x01" "10=000\x01");
    auto result = framer.feed(bad_frame, carry, out);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), error::wire_invalid_body_length);
    EXPECT_EQ(framer.pending_bytes(), 0U);
}

// ── wire_invalid_body_length: body_length == 0 explicitly ────────────────────

TEST(FramerErrorPath, InvalidBodyLengthZeroValue) {
    std::array<std::byte, 1024> arena_storage{};
    std::pmr::monotonic_buffer_resource arena{arena_storage.data(),
                                              arena_storage.size()};
    Framer framer{};
    pmr_carry_buffer carry{256, &arena};
    std::array<frame_view, 4> out{};

    // "9=0" — explicitly zero body_length.
    auto bad_frame = to_bytes("8=FIX.4.4\x01" "9=0\x01" "35=D\x01" "10=000\x01");
    auto result = framer.feed(bad_frame, carry, out);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), error::wire_invalid_body_length);
    EXPECT_EQ(framer.pending_bytes(), 0U);
}

// ── wire_invalid_body_length: body last byte not SOH ─────────────────────────
// Build a frame where the last byte of the body-covered region is not SOH.
// Approach: construct a frame where checksum_off+7 fits (full "10=NNN\x01"
// present and parseable), then directly corrupt the byte at
// body_off + body_length - 1 to a non-SOH value.
// The body_length_end check at framer line 158 fires AFTER the checksum_off+7
// bounds check passes.

TEST(FramerErrorPath, InvalidBodyLengthBodyLastByteNotSOH) {
    std::array<std::byte, 1024> arena_storage{};
    std::pmr::monotonic_buffer_resource arena{arena_storage.data(),
                                              arena_storage.size()};
    Framer framer{};
    pmr_carry_buffer carry{256, &arena};
    std::array<frame_view, 4> out{};

    // Build a well-formed frame. Then corrupt the LAST byte of the body
    // (the SOH before the "10=" marker) to 'X'. This makes body_off +
    // body_length - 1 point to 'X' (not SOH), triggering the rejection.
    //
    // Layout of make_frame(body):
    //   8=FIX.4.4\x01          ← 10 bytes
    //   9=N\x01                ← N varies
    //   <body>                 ← body bytes (last byte should be SOH)
    //   10=NNN\x01             ← 7 bytes
    //
    // We replace the SOH at position (frame.size() - 8) — the byte just before
    // the "10=" trailer — with 'X'. The 9= body_length covers from body_off to
    // (frame.size()-8), so its last byte becomes 'X'.
    auto frame = make_frame("35=D\x01" "49=SENDER\x01" "56=TARGET\x01");
    ASSERT_GE(frame.size(), 8U);
    // frame.size()-7 = '1' of "10=", frame.size()-8 = SOH ending the body.
    frame[frame.size() - 8U] = std::byte{'X'};  // corrupt body's trailing SOH

    auto result = framer.feed(frame, carry, out);
    ASSERT_FALSE(result.has_value())
        << "body last byte not SOH must yield invalid_body_length";
    EXPECT_EQ(result.error(), error::wire_invalid_body_length);
    EXPECT_EQ(framer.pending_bytes(), 0U);
}

// ── wire_invalid_body_length: 10= marker not present at checksum_off ─────────
// Build a correct frame then corrupt the '1' byte of "10=" so the framer
// rejects it at the checksum-marker presence check.

TEST(FramerErrorPath, InvalidBodyLengthChecksumMarkerAbsent) {
    std::array<std::byte, 1024> arena_storage{};
    std::pmr::monotonic_buffer_resource arena{arena_storage.data(),
                                              arena_storage.size()};
    Framer framer{};
    pmr_carry_buffer carry{256, &arena};
    std::array<frame_view, 4> out{};

    // Build a correct frame, then replace the '1' byte of "10=" with 'X'.
    // The checksum_off is frame.size() - 7 (the start of "10=NNN\x01").
    auto frame = make_frame("35=D\x01" "49=SENDER\x01" "56=TARGET\x01");
    ASSERT_GE(frame.size(), 7U);
    // frame.size()-7 is the '1' of "10="
    frame[frame.size() - 7U] = std::byte{'X'};

    auto result = framer.feed(frame, carry, out);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), error::wire_invalid_body_length);
    EXPECT_EQ(framer.pending_bytes(), 0U);
}

// ── wire_checksum_mismatch: non-digit in checksum digits ─────────────────────

TEST(FramerErrorPath, ChecksumMismatchNonDigitInChecksumField) {
    std::array<std::byte, 1024> arena_storage{};
    std::pmr::monotonic_buffer_resource arena{arena_storage.data(),
                                              arena_storage.size()};
    Framer framer{};
    pmr_carry_buffer carry{256, &arena};
    std::array<frame_view, 4> out{};

    // Build a correct frame first, then corrupt a checksum digit with a non-digit.
    auto frame = make_frame("35=D\x01" "49=SENDER\x01" "56=TARGET\x01");
    // The frame ends with "10=NNN\x01" (7 bytes). Layout from end:
    //   frame.size()-1: '\x01'
    //   frame.size()-2: N3 (third digit)
    //   frame.size()-3: N2 (second digit)
    //   frame.size()-4: N1 (first digit) ← replace with 'X'
    //   frame.size()-5: '='
    //   frame.size()-6: '0'
    //   frame.size()-7: '1'
    ASSERT_GE(frame.size(), 7U);
    frame[frame.size() - 4U] = std::byte{'X'};  // first digit → 'X'

    auto result = framer.feed(frame, carry, out);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), error::wire_checksum_mismatch);
    EXPECT_EQ(framer.pending_bytes(), 0U);
}

// ── wire_checksum_mismatch: 10= trailing SOH not present ─────────────────────

TEST(FramerErrorPath, ChecksumMismatchTrailingSOHAbsent) {
    std::array<std::byte, 1024> arena_storage{};
    std::pmr::monotonic_buffer_resource arena{arena_storage.data(),
                                              arena_storage.size()};
    Framer framer{};
    pmr_carry_buffer carry{256, &arena};
    std::array<frame_view, 4> out{};

    // Build a correct frame, then replace the trailing SOH of "10=NNN\x01"
    // with a non-SOH byte so the SOH check at checksum_off+6 fails.
    auto frame = make_frame("35=D\x01" "49=SENDER\x01" "56=TARGET\x01");
    ASSERT_GE(frame.size(), 1U);
    frame[frame.size() - 1U] = std::byte{'X'};  // replace trailing SOH

    auto result = framer.feed(frame, carry, out);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), error::wire_checksum_mismatch);
    EXPECT_EQ(framer.pending_bytes(), 0U);
}

// ── wire_frame_too_large: frame_len > max_frame_bytes after full parse ────────
// The frame structure is valid but frame_len (checksum_off + 7) exceeds
// max_frame_bytes at the final size check inside parse_frame.

TEST(FramerErrorPath, FrameTooLargeAtFinalSizeCheck) {
    // Build a frame that fits the default max (256 KiB). Set max_frame_bytes
    // to a value LESS than the frame but GREATER than the body (so the body_length
    // check at line 137 passes, but the final frame_len check at line 191 fails).
    //
    // body = "35=D\x01 49=SENDER\x01 56=TARGET\x01" = 6+10+10 = 26 bytes.
    // 8=FIX.4.4\x01 = 10 bytes. 9=26\x01 = 6 bytes. Total frame = 10+6+26+7 = 49 bytes.
    // Set max_frame_bytes = 48 → body_length (26) ≤ 48, but frame_len (49) > 48.
    auto frame = make_frame("35=D\x01" "49=SENDER\x01" "56=TARGET\x01");
    std::size_t frame_len = frame.size();
    ASSERT_GT(frame_len, 0U);

    // max_frame_bytes = frame_len - 1: body fits (body_length < max_frame_bytes)
    // but frame_len > max_frame_bytes.
    Framer::Config cfg{.max_frame_bytes = frame_len - 1};
    ASSERT_GT(cfg.max_frame_bytes, 26U)
        << "need max_frame_bytes > body_length for this test to reach final check";

    std::array<std::byte, 1024> arena_storage{};
    std::pmr::monotonic_buffer_resource arena{arena_storage.data(),
                                              arena_storage.size()};
    Framer framer{cfg};
    pmr_carry_buffer carry{frame.size(), &arena};
    std::array<frame_view, 4> out{};

    auto result = framer.feed(frame, carry, out);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), error::wire_frame_too_large);
    EXPECT_EQ(framer.pending_bytes(), 0U);
}

// ── out buffer full: produced == out.size() breaks the loop ──────────────────
// Feed two frames with only one output slot; the second frame is not emitted
// but also not an error — the framer must carry it.

TEST(FramerErrorPath, OutBufferFullDropsToCarry) {
    std::array<std::byte, 2048> arena_storage{};
    std::pmr::monotonic_buffer_resource arena{arena_storage.data(),
                                              arena_storage.size()};

    auto f1 = make_frame("35=0\x01" "49=A\x01" "56=B\x01");
    auto f2 = make_frame("35=0\x01" "49=C\x01" "56=D\x01");
    std::vector<std::byte> pipeline;
    pipeline.insert(pipeline.end(), f1.begin(), f1.end());
    pipeline.insert(pipeline.end(), f2.begin(), f2.end());

    Framer framer{};
    pmr_carry_buffer carry{pipeline.size(), &arena};
    // Only one output slot.
    std::array<frame_view, 1> out{};

    auto result = framer.feed(
        std::span<const std::byte>{pipeline.data(), pipeline.size()},
        carry, out);
    ASSERT_TRUE(result.has_value())
        << "out-buffer-full must NOT be an error, second frame goes to carry";
    EXPECT_EQ(result.value().size(), 1U)
        << "only one frame should be emitted with one output slot";
    EXPECT_GT(framer.pending_bytes(), 0U)
        << "second frame bytes must be pending";
}

// ── carry append fail during trailing save ────────────────────────────────────
// This exercises the `carry.append(source.subspan(offset))` path in Framer::feed
// when there is trailing partial data but the carry buffer is too small to hold it.

TEST(FramerErrorPath, CarryAppendFailOnTrailingSave) {
    // Start with an empty carry. Feed two frames concatenated, but only consume
    // the first. The second partial frame would need to go into carry.
    // Make carry capacity = 0 so the trailing append fails.
    std::array<std::byte, 64> arena_storage{};
    std::pmr::monotonic_buffer_resource arena{arena_storage.data(),
                                              arena_storage.size()};

    auto f1 = make_frame("35=0\x01" "49=A\x01");
    // Create a partial second "frame" (just a few bytes, no complete frame).
    std::string partial = "8=FIX.4.4";
    partial.push_back(soh);

    std::vector<std::byte> input;
    input.insert(input.end(), f1.begin(), f1.end());
    for (char c : partial) {
        input.push_back(static_cast<std::byte>(c));
    }

    Framer framer{};
    // Carry capacity = f1.size() only (enough for the first frame but not a
    // second partial). Actually the trailing save happens when carry is empty
    // initially (using_carry=false path). Make carry capacity = 1 so the
    // trailing append of the partial bytes fails.
    pmr_carry_buffer carry{1, &arena};  // capacity = 1 byte — too small for trailing
    std::array<frame_view, 4> out{};

    auto result = framer.feed(
        std::span<const std::byte>{input.data(), input.size()},
        carry, out);
    // The first frame should parse, then the partial fails to save → error.
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), error::wire_frame_too_large);
}

}  // namespace
