// SPDX-License-Identifier: AGPL-3.0-or-later

#include "frame_field_extract.hpp"

#include <charconv>

namespace fixpp::session::test_support {

namespace {
constexpr std::byte kSoh{0x01};
constexpr std::byte kEq{0x3D};  // '='
}  // namespace

std::optional<std::string_view> extract_field(std::span<const std::byte> frame,
                                              std::uint16_t tag) noexcept {
    // Walk field-by-field. Each field begins at frame[0] or after an SOH and
    // takes the form <digits>=<value>SOH.
    std::size_t i = 0;
    const std::size_t n = frame.size();
    while (i < n) {
        // Parse tag digits.
        const std::size_t tag_begin = i;
        while (i < n && frame[i] != kEq && frame[i] != kSoh) {
            ++i;
        }
        if (i >= n || frame[i] != kEq) {
            // Malformed field (no '='); abort scan.
            return std::nullopt;
        }
        const auto* digits = reinterpret_cast<const char*>(frame.data() + tag_begin);
        const std::size_t digit_len = i - tag_begin;
        std::uint32_t parsed_tag = 0;
        const auto [ptr, ec] =
            std::from_chars(digits, digits + digit_len, parsed_tag);
        const bool tag_ok = (ec == std::errc{} && ptr == digits + digit_len);
        ++i;  // step past '='

        // Find SOH terminator.
        const std::size_t val_begin = i;
        while (i < n && frame[i] != kSoh) {
            ++i;
        }
        if (tag_ok && parsed_tag == tag) {
            return std::string_view{reinterpret_cast<const char*>(frame.data() + val_begin),
                                    i - val_begin};
        }
        if (i < n) {
            ++i;  // step past SOH
        }
    }
    return std::nullopt;
}

}  // namespace fixpp::session::test_support
