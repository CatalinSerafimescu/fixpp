#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// src/session/inbound_limit.hpp — the session's one inbound limit L, and the carry
// derived from it (093-inbound-frame-dispositions, data-model E-2, research R-3); the
// parse buffer's sizing is in parse_capacity.hpp. Private: not installed.
//
// L is the largest inbound frame a session admits. It is computed from the
// SessionConfig alone, so the accept loop can compute it before a Session exists,
// and Session::open() stores it.

#include <cstddef>
#include <cstdint>
#include <fixpp/session/session_config.hpp>

#include "parse_capacity.hpp"  // kDefaultInboundLimit, kContainerSlack, N(L), B(L)
#include "read_pump.hpp"       // kReadPumpReadSize

namespace fixpp::session {

// The range an advertised MaxMessageSize(383) must fall in (spec FR-010, plan OD-2):
// the floor is the acceptor's first-frame byte budget, the ceiling the owner's ruling.
inline constexpr std::uint32_t kMinAdvertisedMaxMessageSize = 4096U;
inline constexpr std::uint32_t kMaxAdvertisedMaxMessageSize = 262144U;

// The advertised MaxMessageSize(383) if set, else kDefaultInboundLimit.
[[nodiscard]] inline std::uint32_t inbound_limit_for(SessionConfig const& cfg) noexcept {
    return cfg.advertised_max_message_size.value_or(kDefaultInboundLimit);
}

// False when an advertised MaxMessageSize(383) lies outside its range. Engine::
// register_session and Session::open() refuse such a config (invalid_session_config).
[[nodiscard]] inline bool advertised_max_message_size_in_range(SessionConfig const& cfg) noexcept {
    auto const& v = cfg.advertised_max_message_size;
    return !v || (*v >= kMinAdvertisedMaxMessageSize && *v <= kMaxAdvertisedMaxMessageSize);
}

namespace detail {

// The carry block open() allocates from SessionConfig::framer_carry_arena: L plus the
// pump's read size, so a frame of exactly L never depends on how the stream was
// segmented, plus the carry's own container proxy (data-model E-2).
[[nodiscard]] constexpr std::size_t inbound_carry_block_bytes(std::uint32_t limit) noexcept {
    return std::size_t{limit} + kReadPumpReadSize + kContainerSlack;
}

}  // namespace detail

}  // namespace fixpp::session
