// SPDX-License-Identifier: AGPL-3.0-or-later
//
// fixpp — 013-session-reconnect-binding
// Contract: ResendState — per-session Resend sub-protocol state.
// Anchors: FR-009..FR-015, D-2..D-5; [FIX-SL §4.3].

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <vector>

namespace fixpp::session {

// Held inbound frame buffered during AwaitingResend (FR-009).
// Added 2026-05-28 per /speckit-analyze C1 resolution to back the FR-009
// SPEC-FIX reference to `ResendState::inbound_held`. See data-model.md §E-2.
struct held_inbound_msg {
    // Inbound MsgSeqNum (> next_expected_inbound at hold time; caller must
    // enforce — the queue is not self-validating).
    std::uint32_t msg_seqnum = 0;

    // Wire SendingTime(52); preserved for FR-015 PossDupFlag dedup and any
    // later OrigSendingTime(122) re-emit (the queue is not currently a replay
    // source in v1.0, but the field is reserved for it).
    std::chrono::system_clock::time_point sending_time;

    // Raw inbound frame bytes, deep-copied from the framer's transient view
    // into PMR storage owned by the parent vector's allocator. The framer's
    // inbound `string_view` does NOT outlive AwaitingResend.
    std::pmr::vector<std::byte> payload;
};

// ResendState — populated on FR-009 entry (Active → awaiting_resend=true);
// reset on gap close (awaiting_resend=false) or session exit. Value-typed;
// embedded in ReconnectFsm. Default-construction is alloc-free (empty vector);
// `inbound_held` may grow during AwaitingResend (cold path, PMR-arena-backed).
// The "FSM transition Active↔AwaitingResend is alloc-free" plan.md §Constraints
// invariant still holds: the transition is just the bool flip + `reset()` (which
// clears the vector back to the arena).
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

    // Inbound messages with MsgSeqNum > next_expected_inbound that arrived
    // during AwaitingResend. Held in strict-MsgSeqNum order until the gap
    // closes (callback delivery then drains in order). Unbounded in v1.0 per
    // [const §XV.15] block-mode + spec.md FR-009 SPEC-FIX wording.
    std::pmr::vector<held_inbound_msg> inbound_held;

    // Reset to default-constructed shape on gap close (awaiting_resend → false).
    // Clears `inbound_held` (returning storage to the PMR arena per its policy).
    void reset() noexcept;

    // Validation per data-model.md §E-2 rules.
    [[nodiscard]] bool invariants_hold() const noexcept;
};

}  // namespace fixpp::session
