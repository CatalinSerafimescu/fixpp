// SPDX-License-Identifier: AGPL-3.0-or-later
//
// specs/008-message-store/contracts/direction.hpp
//
// SHAPE ORACLE — declaration-only contract for fixpp::session::direction_t.
// Anchor: .specify/2e-msgstore.md v0.4 §4.6. FR-002.
//
// Frozen for v1.0; values reserved per [const §X.4]. Future v-values are
// reserved (e.g., admin-direction tags); do NOT renumber.
#pragma once

#include <cstdint>

namespace fixpp::session {

enum class direction_t : std::uint8_t {
    inbound  = 0,
    outbound = 1,
};

}  // namespace fixpp::session
