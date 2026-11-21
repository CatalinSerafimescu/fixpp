// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/session/detail/log_rate_gate.hpp — internal detail header (no public symbol).
//
// Feature 105 (specs/105-transport-liveness/, research D8; data-model E-7): the rate state of a
// log record written at most once per window, and its admit step. Hoisted from Session (its
// nested log_rate_gate of features 093 and 101), so the engine's accept loop holds one too for its
// pre-session refusal record; Session keeps the name by a member alias, and the fields are
// unchanged.
//
// The window's end is computed by the caller (detail::deadline_after in
// src/session/read_first_frame_bounded.hpp, which an installed header cannot include), so the
// saturating step stays in one place.
#pragma once

#include <cstdint>
#include <fixpp/core/clock.hpp>
#include <optional>
#include <utility>

namespace fixpp::session::detail {

// Whether a record has been written, the earliest time of the next one, and the occurrences
// suppressed since the last.
struct log_rate_gate {
    bool logged = false;
    fixpp::core::steady_time_point next;
    std::uint64_t unlogged = 0;
};

// Admits a record at `now`: returns the occurrences suppressed since the previous record and
// starts the next window, which ends at `window_end` (`now` plus the window, saturated by the
// caller). Inside a window it counts `occurrences` as suppressed and returns nullopt.
[[nodiscard]] inline std::optional<std::uint64_t> admit_log_record(
    log_rate_gate& gate, fixpp::core::steady_time_point now,
    fixpp::core::steady_time_point window_end, std::uint64_t occurrences) noexcept {
    if (gate.logged && now < gate.next) {
        gate.unlogged += occurrences;
        return std::nullopt;
    }
    gate.next = window_end;
    gate.logged = true;
    return std::exchange(gate.unlogged, 0);
}

}  // namespace fixpp::session::detail
