// SPDX-License-Identifier: AGPL-3.0-or-later
// include/fixpp/wire/length_data_check.hpp — the Length+Data conformance rule for a
// message being WRITTEN (fixpp#428, design `.specify/426-428-length-data-pairs.md`
// §5.3; #418 is meant to be its second caller).
//
// Feed the fields of one container (the top level, or one repeating-group
// instance) in the order they will be serialised, then call finish(). The checker
// reports the first field that makes the output malformed under TagValue v1.0
// §4.2.4/§4.2.5 or FIX 4.4 Vol 1 `data`:
//   - a Data field not immediately preceded by its Length field;
//   - a Length field not immediately followed by its Data field;
//   - a Length value that is not one or more ASCII digits, is zero, or overflows
//     (leading zeros are accepted: TagValue Table 1 `int`);
//   - a Data value that is empty, or whose byte count differs from its Length.
// Which tags pair comes from `dict_hooks` (design §3: the standard table first).
//
// Kept separate from length_data_carry.hpp: the carry READS a message and must
// cope with what a peer sent; this checker refuses what fixpp would SEND.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "dict_hooks.hpp"
#include "tag_scan.hpp"  // accumulate_bounded

namespace fixpp::wire {

class length_data_checker {
public:
    explicit constexpr length_data_checker(dict_hooks const& hooks) noexcept : hooks_{hooks} {}

    // One field, in serialisation order. Returns false at the first violation and
    // on every call after it.
    [[nodiscard]] constexpr bool observe(std::uint16_t tag,
                                         std::span<std::byte const> value) noexcept {
        if (failed_) {
            return false;
        }
        if (awaited_data_ != 0) {
            bool const ok = tag == awaited_data_ && !value.empty() && value.size() == awaited_len_;
            awaited_data_ = 0;
            if (!ok) {
                failed_ = true;
                return false;
            }
            return true;
        }
        if (hooks_.length_tag_for_data(tag) != 0) {
            failed_ = true;  // a Data field whose Length is not right before it
            return false;
        }
        if (std::uint16_t const data = hooks_.data_tag_for_length(tag); data != 0) {
            std::uint32_t count = 0;
            if (value.empty()) {
                failed_ = true;
                return false;
            }
            for (auto const b : value) {
                auto const c = static_cast<unsigned char>(b);
                if (c < '0' || c > '9' || !accumulate_bounded(count, c, 0xFFFFFFFFU)) {
                    failed_ = true;
                    return false;
                }
            }
            if (count == 0) {
                failed_ = true;
                return false;
            }
            awaited_data_ = data;
            awaited_len_ = count;
        }
        return true;
    }

    // End of the container. False when a Length is still waiting for its Data, or
    // an earlier field failed. Ready for the next container afterwards.
    [[nodiscard]] constexpr bool finish() noexcept {
        bool const ok = !failed_ && awaited_data_ == 0;
        failed_ = false;
        awaited_data_ = 0;
        return ok;
    }

private:
    dict_hooks hooks_;
    std::uint16_t awaited_data_ = 0;
    std::uint32_t awaited_len_ = 0;
    bool failed_ = false;
};

}  // namespace fixpp::wire
