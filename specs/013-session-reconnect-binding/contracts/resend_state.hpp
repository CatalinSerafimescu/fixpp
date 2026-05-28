// SPDX-License-Identifier: AGPL-3.0-or-later
//
// fixpp — 013-session-reconnect-binding
// Contract: ResendState — per-session Resend sub-protocol state.
// Anchors: FR-009..FR-015, D-2..D-5; [FIX-SL §4.3].

#pragma once

#include <chrono>
#include <cstdint>

namespace fixpp::session {

// ResendState — populated on FR-009 entry (Active → awaiting_resend=true);
// reset on gap close (awaiting_resend=false) or session exit. Value-typed;
// embedded in ReconnectFsm; no allocations.
struct ResendState {
    // FR-009 — BeginSeqNo we sent in ResendRequest(2).
    std::uint32_t outstanding_begin = 0;

    // FR-009 — EndSeqNo we sent; 0 means "infinity" per D-2 / FIX-SL §4.3.2.
    std::uint32_t outstanding_end = 0;

    // Populated on ResendRequest emit; consumed by SC-005 recovery-elapsed
    // metric (≥ 50 % of nominal throughput on 1000-message gap).
    std::chrono::steady_clock::time_point started_at;

    // High-water-mark of what we've received in the replay; advances as
    // PossDup=Y application messages OR GapFill SequenceReset arrives.
    std::uint32_t inbound_filled_through = 0;

    // Outbound-replay cursor — current position in our outbound replay if peer
    // issued a counter-ResendRequest. Not always populated (peer may not
    // counter-resend); when 0, no outbound replay in flight.
    std::uint32_t outbound_replay_cursor = 0;

    // Reset to default-constructed shape on gap close (awaiting_resend → false).
    void reset() noexcept;

    // Validation per data-model.md §E-2 rules.
    [[nodiscard]] bool invariants_hold() const noexcept;
};

}  // namespace fixpp::session
