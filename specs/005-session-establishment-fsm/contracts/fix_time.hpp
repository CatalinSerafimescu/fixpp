// contracts/fix_time.hpp — shape oracle (NOT the build header)
// FOLDED core/ time-helper #4 (D-3). Lives in core/, NOT session/ — it is
// the deferred core/ module-exit row; folding it here closes core/'s exit.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <chrono>
#include <span>
// fixpp::core::expected_t, error

namespace fixpp::core {

// Reuse [2d §4.1] alias; add a duration alias.
using utc_time_point = std::chrono::time_point<std::chrono::system_clock>;
using duration       = std::chrono::system_clock::duration;

enum class fix_time_precision : std::uint8_t {
    seconds = 0,   // YYYYMMDD-HH:MM:SS
    millis  = 1,   // YYYYMMDD-HH:MM:SS.sss  (FIX 4.x default)
    micros  = 2,   // YYYYMMDD-HH:MM:SS.ssssss (where the version permits)
};

// Format to the exact FIX UTCTimestamp grammar into a fixed-capacity
// caller buffer (no heap, [const §VIII.5]); returns the written view.
// Never coarser than seconds.
[[nodiscard]] expected_t<std::span<char>>
    utc_time_to_fix_string(utc_time_point tp,
                           fix_time_precision prec,
                           std::span<char> out) noexcept;

// Parse YYYYMMDD-HH:MM:SS[.sss[sss]]; round-trips losslessly at the
// emitted precision (I-6, SC-007/SC-010 corpus incl. epoch edges,
// leap-second-adjacent, sub-second). Bad grammar ⇒ expected unexpected.
[[nodiscard]] expected_t<utc_time_point>
    fix_string_to_utc_time(std::span<const char> s) noexcept;

}  // namespace fixpp::core
