// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/session/resend_state.cpp
//
// fixpp::session::ResendState — Resend sub-protocol state value type bodies.
// Anchors: FR-009..FR-015, D-2..D-5; [FIX-SL §4.3]; data-model.md §E-2;
// contracts/resend_state.hpp.
//
// Phase 2 SHAPE: link-green implementations. Behavioral wiring lands in
// Phase 3 T025/T026 when ResendState is populated by enter_awaiting_resend.

#include "fixpp/session/resend_state.hpp"

namespace fixpp::session {

// reset: clear all fields back to default-constructed state on gap close.
// Clears inbound_held (returns PMR storage to the arena per its policy).
// Called when awaiting_resend_ transitions from true → false.
void ResendState::reset() noexcept {
    outstanding_begin      = 0;
    outstanding_end        = 0;
    started_at             = {};
    inbound_filled_through = 0;
    outbound_replay_cursor = 0;
    inbound_held.clear();
}

// invariants_hold: validate per data-model.md §E-2 rules.
//   outstanding_begin <= outstanding_end, OR outstanding_end == 0 (infinity).
[[nodiscard]] bool ResendState::invariants_hold() const noexcept {
    if (outstanding_end == 0) {
        // 0 means infinity per D-2 / FIX-SL §4.3.2; always valid.
        return true;
    }
    return outstanding_begin <= outstanding_end;
}

}  // namespace fixpp::session
