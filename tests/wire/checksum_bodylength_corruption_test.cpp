// SPDX-License-Identifier: AGPL-3.0-or-later

#include <array>
#include <cstddef>
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

void append_checksum_field(std::string& frame, unsigned checksum) {
    frame.push_back('1');
    frame.push_back('0');
    frame.push_back('=');
    frame.push_back(static_cast<char>('0' + ((checksum / 100U) % 10U)));
    frame.push_back(static_cast<char>('0' + ((checksum / 10U) % 10U)));
    frame.push_back(static_cast<char>('0' + (checksum % 10U)));
    frame.push_back(soh);
}

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

    append_checksum_field(frame, checksum);
    return to_bytes(frame);
}

void xor_checksum_digit_for_test(std::vector<std::byte>& frame) {
    ASSERT_GE(frame.size(), 5U);
    frame[frame.size() - 2U] ^= std::byte{0x01};
}

void replace_body_length_for_test(std::vector<std::byte>& frame,
                                  std::string_view replacement) {
    std::size_t start = 0;
    while (start + 1U < frame.size()) {
        if (frame[start] == std::byte{'9'} && frame[start + 1U] == std::byte{'='}) {
            break;
        }
        ++start;
    }
    ASSERT_LT(start + 1U, frame.size());

    std::size_t value_begin = start + 2U;
    std::size_t value_end = value_begin;
    while (value_end < frame.size() && frame[value_end] != std::byte{0x01}) {
        ++value_end;
    }
    ASSERT_LT(value_end, frame.size());
    ASSERT_EQ(replacement.size(), value_end - value_begin);

    for (std::size_t i = 0; i < replacement.size(); ++i) {
        frame[value_begin + i] = static_cast<std::byte>(replacement[i]);
    }
}

}  // namespace

TEST(WireChecksumBodyLengthCorruption, RejectsChecksumMismatchBeforeEmit) {
    std::vector<std::byte> frame =
        make_frame(std::string{"35=0\x01" "49=SENDER\x01" "56=TARGET\x01"});
    xor_checksum_digit_for_test(frame);

    std::array<std::byte, 256> arena_storage{};
    std::pmr::monotonic_buffer_resource arena{arena_storage.data(),
                                              arena_storage.size()};
    Framer framer{};
    pmr_carry_buffer carry{frame.size(), &arena};
    std::array<frame_view, 2> out{};

    auto result = framer.feed(frame, carry, out);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), error::wire_checksum_mismatch);
    EXPECT_EQ(framer.pending_bytes(), 0U);
}

TEST(WireChecksumBodyLengthCorruption, RejectsSpacePaddedBodyLengthBeforeEmit) {
    std::vector<std::byte> frame =
        make_frame(std::string{"35=0\x01" "49=SENDER\x01" "56=TARGET\x01"});
    replace_body_length_for_test(frame, " 2");

    std::array<std::byte, 256> arena_storage{};
    std::pmr::monotonic_buffer_resource arena{arena_storage.data(),
                                              arena_storage.size()};
    Framer framer{};
    pmr_carry_buffer carry{frame.size(), &arena};
    std::array<frame_view, 2> out{};

    auto result = framer.feed(frame, carry, out);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), error::wire_invalid_body_length);
    EXPECT_EQ(framer.pending_bytes(), 0U);
}

TEST(WireChecksumBodyLengthCorruption,
     RejectsInconsistentBodyLengthBeforeChecksumValidation) {
    std::vector<std::byte> frame =
        make_frame(std::string{"35=0\x01" "49=SENDER\x01" "56=TARGET\x01"});
    replace_body_length_for_test(frame, "12");

    std::array<std::byte, 256> arena_storage{};
    std::pmr::monotonic_buffer_resource arena{arena_storage.data(),
                                              arena_storage.size()};
    Framer framer{};
    pmr_carry_buffer carry{frame.size(), &arena};
    std::array<frame_view, 2> out{};

    auto result = framer.feed(frame, carry, out);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), error::wire_invalid_body_length);
    EXPECT_EQ(framer.pending_bytes(), 0U);
}

TEST(WireChecksumBodyLengthCorruption,
     RejectsOversizedFrameWithoutUnboundedCarryGrowth) {
    std::vector<std::byte> frame = make_frame(
        std::string{"35=D\x01" "49=SENDER\x01" "56=TARGET\x01" "58=PAYLOAD1234567890\x01"});

    std::array<std::byte, 256> arena_storage{};
    std::pmr::monotonic_buffer_resource arena{arena_storage.data(),
                                              arena_storage.size()};
    Framer framer{Framer::Config{.max_frame_bytes = 24U}};
    pmr_carry_buffer carry{frame.size(), &arena};
    std::array<frame_view, 2> out{};

    auto result = framer.feed(frame, carry, out);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), error::wire_frame_too_large);
    EXPECT_EQ(framer.pending_bytes(), 0U);

    auto second = framer.feed(frame, carry, out);
    ASSERT_FALSE(second.has_value());
    EXPECT_EQ(second.error(), error::wire_frame_too_large);
    EXPECT_EQ(framer.pending_bytes(), 0U);
}
