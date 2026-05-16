// SPDX-License-Identifier: AGPL-3.0-or-later
// src/wire/framer.cpp — fixpp::wire out-of-line implementation.
// US3 / T040 framing algorithm.

#include <fixpp/wire/framer.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include <fixpp/core/error.hpp>
#include <fixpp/wire/errors.hpp>
#include <fixpp/wire/view.hpp>

namespace fixpp::wire {
namespace {

constexpr std::byte soh_byte{0x01};

struct parsed_frame {
    enum class status : std::uint8_t {
        complete,
        partial,
        error,
    };

    status status_code = status::partial;
    std::size_t frame_len = 0;
    std::size_t body_off = 0;
    std::size_t body_len = 0;
    core::error error_code = core::error::wire_invalid_body_length;
};

[[nodiscard]] constexpr bool is_digit(std::byte b) noexcept {
    auto const ch = static_cast<unsigned char>(b);
    return ch >= static_cast<unsigned char>('0')
           && ch <= static_cast<unsigned char>('9');
}

[[nodiscard]] parsed_frame make_error(core::error code) noexcept {
    return parsed_frame{
        .status_code = parsed_frame::status::error,
        .frame_len = 0,
        .body_off = 0,
        .body_len = 0,
        .error_code = code,
    };
}

[[nodiscard]] parsed_frame make_partial() noexcept {
    return parsed_frame{
        .status_code = parsed_frame::status::partial,
    };
}

[[nodiscard]] parsed_frame make_complete(std::size_t frame_len,
                                         std::size_t body_off,
                                         std::size_t body_len) noexcept {
    return parsed_frame{
        .status_code = parsed_frame::status::complete,
        .frame_len = frame_len,
        .body_off = body_off,
        .body_len = body_len,
    };
}

[[nodiscard]] std::size_t find_soh(std::span<const std::byte> bytes,
                                   std::size_t start) noexcept {
    for (std::size_t i = start; i < bytes.size(); ++i) {
        if (bytes[i] == soh_byte) {
            return i;
        }
    }
    return bytes.size();
}

[[nodiscard]] parsed_frame parse_frame(std::span<const std::byte> bytes,
                                       std::size_t max_frame_bytes) noexcept {
    if (bytes.empty()) {
        return make_partial();
    }
    if (bytes[0] != std::byte{'8'}) {
        return make_error(core::error::wire_framing_resync);
    }
    if (bytes.size() < 2U) {
        return make_partial();
    }
    if (bytes[1] != std::byte{'='}) {
        return make_error(core::error::wire_framing_resync);
    }

    std::size_t const beginstring_end = find_soh(bytes, 2U);
    if (beginstring_end == bytes.size()) {
        return make_partial();
    }

    std::size_t const body_length_tag = beginstring_end + 1U;
    if (body_length_tag >= bytes.size()) {
        return make_partial();
    }
    if (body_length_tag + 1U >= bytes.size()) {
        return make_partial();
    }
    if (bytes[body_length_tag] != std::byte{'9'}
        || bytes[body_length_tag + 1U] != std::byte{'='}) {
        return make_error(core::error::wire_invalid_body_length);
    }

    std::size_t const body_length_value = body_length_tag + 2U;
    std::size_t const body_length_end = find_soh(bytes, body_length_value);
    if (body_length_end == bytes.size()) {
        return make_partial();
    }
    if (body_length_end == body_length_value) {
        return make_error(core::error::wire_invalid_body_length);
    }

    std::size_t body_length = 0;
    for (std::size_t i = body_length_value; i < body_length_end; ++i) {
        if (!is_digit(bytes[i])) {
            return make_error(core::error::wire_invalid_body_length);
        }
        std::size_t const digit =
            static_cast<std::size_t>(static_cast<unsigned char>(bytes[i]))
            - static_cast<std::size_t>('0');
        if (body_length
            > ((max_frame_bytes - digit) / static_cast<std::size_t>(10))) {
            return make_error(core::error::wire_frame_too_large);
        }
        body_length = (body_length * static_cast<std::size_t>(10)) + digit;
    }

    if (body_length == 0U) {
        return make_error(core::error::wire_invalid_body_length);
    }
    if (body_length > max_frame_bytes) {
        return make_error(core::error::wire_frame_too_large);
    }

    std::size_t const body_off = body_length_end + 1U;
    if (body_off > bytes.size()) {
        return make_partial();
    }
    if (body_off + body_length > bytes.size()) {
        return make_partial();
    }

    std::size_t const checksum_off = body_off + body_length;
    if (checksum_off + 7U > bytes.size()) {
        return make_partial();
    }
    if (bytes[checksum_off] != std::byte{'1'}
        || bytes[checksum_off + 1U] != std::byte{'0'}
        || bytes[checksum_off + 2U] != std::byte{'='}) {
        return make_error(core::error::wire_invalid_body_length);
    }
    if (bytes[body_off + body_length - 1U] != soh_byte) {
        return make_error(core::error::wire_invalid_body_length);
    }

    std::array<unsigned, 3> checksum_digits{};
    for (std::size_t i = 0; i < checksum_digits.size(); ++i) {
        std::byte const digit = bytes[checksum_off + 3U + i];
        if (!is_digit(digit)) {
            return make_error(core::error::wire_checksum_mismatch);
        }
        checksum_digits[i] =
            static_cast<unsigned>(static_cast<unsigned char>(digit))
            - static_cast<unsigned>('0');
    }
    if (bytes[checksum_off + 6U] != soh_byte) {
        return make_error(core::error::wire_checksum_mismatch);
    }

    unsigned checksum = 0;
    for (std::size_t i = 0; i < checksum_off; ++i) {
        checksum += static_cast<unsigned>(
            static_cast<unsigned char>(bytes[i]));
    }
    checksum %= 256U;

    unsigned const encoded_checksum =
        (checksum_digits[0] * 100U) + (checksum_digits[1] * 10U)
        + checksum_digits[2];
    if (encoded_checksum != checksum) {
        return make_error(core::error::wire_checksum_mismatch);
    }

    std::size_t const frame_len = checksum_off + 7U;
    if (frame_len > max_frame_bytes) {
        return make_error(core::error::wire_frame_too_large);
    }

    return make_complete(frame_len, body_off, body_length);
}

}  // namespace

core::expected_t<std::span<frame_view>>
Framer::feed(std::span<const std::byte> incoming,
             pmr_carry_buffer& carry,
             std::span<frame_view> out) noexcept {
    if (pending_ < carry.size()) {
        carry.consume_front(carry.size() - pending_);
    }

    std::span<const std::byte> source = incoming;
    bool using_carry = false;
    if (!carry.empty()) {
        if (!carry.append(incoming)) {
            carry.clear();
            pending_ = 0;
            return fail<std::span<frame_view>>(core::error::wire_frame_too_large);
        }
        source = carry.bytes();
        using_carry = true;
    }

    std::size_t offset = 0;
    std::size_t produced = 0;
    while (offset < source.size()) {
        if (produced == out.size()) {
            break;
        }

        parsed_frame const frame =
            parse_frame(source.subspan(offset), cfg_.max_frame_bytes);
        if (frame.status_code == parsed_frame::status::partial) {
            break;
        }
        if (frame.status_code == parsed_frame::status::error) {
            carry.clear();
            pending_ = 0;
            switch (frame.error_code) {
                case core::error::wire_frame_too_large:
                    return fail<std::span<frame_view>>(
                        core::error::wire_frame_too_large);
                case core::error::wire_checksum_mismatch:
                    return fail<std::span<frame_view>>(
                        core::error::wire_checksum_mismatch);
                case core::error::wire_framing_resync:
                    return fail<std::span<frame_view>>(
                        core::error::wire_framing_resync);
                case core::error::wire_invalid_body_length:
                default:
                    return fail<std::span<frame_view>>(
                        core::error::wire_invalid_body_length);
            }
        }

        std::byte const* frame_ptr = source.data() + offset;
        out[produced] = frame_view{
            frame_ptr,
            frame.frame_len,
            frame.body_off,
            frame.body_len,
            detail::generation_token{},
        };
        ++produced;
        offset += frame.frame_len;
    }

    std::size_t const trailing = source.size() - offset;
    if (using_carry) {
        pending_ = trailing;
        return out.first(produced);
    }

    carry.clear();
    pending_ = 0;
    if (trailing != 0U) {
        if (!carry.append(source.subspan(offset))) {
            return fail<std::span<frame_view>>(core::error::wire_frame_too_large);
        }
        pending_ = trailing;
    }

    return out.first(produced);
}

}  // namespace fixpp::wire
