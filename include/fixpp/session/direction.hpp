// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/session/direction.hpp
//
// fixpp::session::direction_t — per-MessageStore-method direction tag.
//
// Anchor: .specify/2e-msgstore.md v0.5 §4.6. FR-002. Entity E8.
// Frozen for v1.0; values reserved per [const §X.4]. Future v-values are
// reserved (e.g., admin-direction tags); do NOT renumber.
//
// Mirror of specs/008-message-store/contracts/direction.hpp (shape oracle).
#pragma once

#include <cstdint>

namespace fixpp::session {

enum class direction_t : std::uint8_t {
    inbound = 0,
    outbound = 1,
};

}  // namespace fixpp::session
