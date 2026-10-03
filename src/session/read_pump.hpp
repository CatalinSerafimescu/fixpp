#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// src/session/read_pump.hpp — the framing settings the session's two inbound Framers
// share: run_read_pump's and the accept loop's first-frame read
// (src/session/engine.cpp, src/session/read_first_frame_bounded.hpp).
// 093-inbound-frame-dispositions (contract C-1; plan OD-16). Private: not installed
// (no install() rule names src/).

#include <algorithm>
#include <cstddef>
#include <fixpp/session/session_config.hpp>
#include <fixpp/wire/framer.hpp>

namespace fixpp::session::detail {

// The read pump's carry capacity. It must be below the 128 KiB over-size frame
// engine_readpump_test's OverCapacityFrameClosesSession sends (kOversizeBody), so
// wire_frame_too_large fires in the Framer, and large enough to hold any valid FIX admin
// frame. [015 tasks.md T015; FR-004/012; C2]
inline constexpr std::size_t kReadPumpCarryCapacity = 64U * 1024U;  // 64 KiB

// The Config both inbound Framers run (contract C-1): resync on, and a BeginString cap
// that is the larger of the Framer's default (the longest supported profile
// identifier) and the configured BeginString's length, so a session configured with a
// longer BeginString still frames its own frames (OD-16).
[[nodiscard]] inline fixpp::wire::Framer::Config inbound_framer_config(
    SessionConfig const& cfg) noexcept {
    fixpp::wire::Framer::Config c;
    c.resync_on_garble = true;
    c.max_begin_string_bytes = std::max(c.max_begin_string_bytes, cfg.begin_string.size());
    return c;
}

}  // namespace fixpp::session::detail
