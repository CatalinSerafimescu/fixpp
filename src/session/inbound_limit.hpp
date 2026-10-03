#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// src/session/inbound_limit.hpp — the session's one inbound limit L
// (093-inbound-frame-dispositions, data-model E-2, research R-3). Private: not
// installed (no install() rule names src/).
//
// L is the largest inbound frame a session admits. It is computed from the
// SessionConfig alone, so the accept loop can compute it before a Session exists,
// and Session::open() stores it.

#include <cstdint>
#include <fixpp/session/session_config.hpp>

namespace fixpp::session {

// L when no MaxMessageSize(383) is advertised (spec FR-010).
inline constexpr std::uint32_t kDefaultInboundLimit = 65536U;

// The advertised MaxMessageSize(383) if set, else kDefaultInboundLimit.
[[nodiscard]] inline std::uint32_t inbound_limit_for(SessionConfig const& cfg) noexcept {
    return cfg.advertised_max_message_size.value_or(kDefaultInboundLimit);
}

}  // namespace fixpp::session
