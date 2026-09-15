// SPDX-License-Identifier: AGPL-3.0-or-later
// include/fixpp/wire/length_data_carry.hpp — Length+Data handling shared by the
// hand-rolled field scanners outside the wire parser (fixpp#426, design
// `.specify/426-428-length-data-pairs.md` §4).
//
// A Data value may contain SOH, so a scanner that splits every field at SOH reads
// the rest of such a value as fields. `OffsetTable::build` and
// `MessageView::field_iterator::advance` carry a Length's count into the next
// field themselves; the session scanners use this one carry so they split a
// message on the same boundaries:
//   - a Data value is counted only when it is the field right after its Length;
//   - a counted value ends where the count says, and the next byte must be SOH.
// What a scanner does with a malformed count differs per scanner (design §4).
//
// Kept out of tag_scan.hpp, which is a std-only leaf; this needs dict_hooks.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "dict_hooks.hpp"
#include "tag_scan.hpp"  // parse_bounded_u32

namespace fixpp::wire {

// Where a counted value starting at `vstart` ends: the index of the SOH that
// must follow it, or nullopt when the count runs past `buf` or the byte after
// the value is not SOH. A value reaching exactly the end of `buf` is malformed
// too, since nothing terminates it. The bound is a subtraction, so
// `vstart + count` cannot wrap on any width (W-P2-1a).
[[nodiscard]] constexpr std::optional<std::size_t> counted_value_end(std::span<std::byte const> buf,
                                                                     std::size_t vstart,
                                                                     std::uint32_t count) noexcept {
    if (vstart > buf.size() || count >= buf.size() - vstart) {
        return std::nullopt;
    }
    std::size_t const end = vstart + count;
    if (buf[end] != std::byte{0x01}) {
        return std::nullopt;
    }
    return end;
}

namespace detail {
inline constexpr std::array<std::byte, 4> counted_value_end_probe = {
    std::byte{'a'}, std::byte{0x01}, std::byte{'b'}, std::byte{0x01}};
}  // namespace detail

// A count of 3 from offset 0 ends on the trailing SOH, stepping over the inner one.
static_assert(counted_value_end(detail::counted_value_end_probe, 0, 3) ==
              std::optional<std::size_t>{3});
// A count that lands on a non-SOH byte is malformed.
static_assert(!counted_value_end(detail::counted_value_end_probe, 0, 2).has_value());
// A count reaching the end of the buffer, or past it, is malformed.
static_assert(!counted_value_end(detail::counted_value_end_probe, 0, 4).has_value());
static_assert(!counted_value_end(detail::counted_value_end_probe, 2, 0xFFFFFFFFU).has_value());

class length_data_carry {
public:
    struct value_extent {
        std::size_t end;  // the index of the SOH ending the value, or buf.size()
        bool counted;     // read by the count of the field before it
    };

    // Reads the value of field `tag`, which starts at `vstart` (<= buf.size()), and
    // arms for the next field. When the previous field was the Length counting
    // `tag`, the value is read by that count; otherwise it runs to the next SOH or
    // the end of `buf`. Returns nullopt only for a malformed count (see
    // counted_value_end); what the scanner does then is its own (design §4). Call
    // it for every field in order, and reset() for a field skipped as malformed,
    // so a count never reaches past the next field.
    [[nodiscard]] std::optional<value_extent> read_value(std::span<std::byte const> buf,
                                                         std::size_t vstart, std::uint16_t tag,
                                                         dict_hooks const& hooks) noexcept {
        bool const counted = data_tag_ != 0 && data_tag_ == tag;
        data_tag_ = 0;
        std::size_t end = vstart;
        if (counted) {
            auto const counted_end = counted_value_end(buf, vstart, count_);
            if (!counted_end) {
                return std::nullopt;
            }
            end = *counted_end;
        } else {
            while (end < buf.size() && buf[end] != std::byte{0x01}) {
                ++end;
            }
        }
        data_tag_ = hooks.data_tag_for_length(tag);
        if (data_tag_ != 0) {
            // The count is the value's leading ASCII digits, as OffsetTable::build
            // reads it, saturating so a lying count never wraps to a small plausible
            // one (W-P2-1c); a saturated count always fails counted_value_end.
            count_ = parse_bounded_u32(buf.subspan(vstart, end - vstart));
        }
        return value_extent{.end = end, .counted = counted};
    }

    // Disarms without a field, for a scanner that skips a malformed field.
    constexpr void reset() noexcept { data_tag_ = 0; }

private:
    std::uint16_t data_tag_ = 0;
    std::uint32_t count_ = 0;
};

}  // namespace fixpp::wire
