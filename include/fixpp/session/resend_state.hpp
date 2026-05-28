// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/session/resend_state.hpp
//
// fixpp::session::ResendState — per-session Resend sub-protocol state.
// Anchors: FR-009..FR-015, D-2..D-5; [FIX-SL §4.3]; data-model.md §E-2;
// contracts/resend_state.hpp.
//
// Value-typed; embedded in ReconnectFsm. No allocations on the FSM transition
// path (Active ↔ AwaitingResend transient flag); inbound_held may grow on the
// cold AwaitingResend path (PMR-arena-backed).
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <vector>

namespace fixpp::session {

// Held inbound frame buffered during AwaitingResend (FR-009).
// Deep-copied from the framer's transient view into PMR storage owned by the
// parent vector's allocator. [data-model §E-2 / /speckit-analyze C1]
struct held_inbound_msg {
    // Inbound MsgSeqNum (> next_expected_inbound at hold time).
    std::uint32_t msg_seqnum = 0;

    // Wire SendingTime(52); preserved for FR-015 PossDupFlag dedup and any
    // later OrigSendingTime(122) re-emit.
    std::chrono::system_clock::time_point sending_time;

    // Raw inbound frame bytes, deep-copied from the framer's transient view
    // into PMR storage owned by the parent vector's allocator.
    std::pmr::vector<std::byte> payload;
};

// ResendState — populated on FR-009 entry (Active → awaiting_resend=true);
// reset on gap close (awaiting_resend=false) or session exit. Value-typed;
// embedded in ReconnectFsm. Default-construction is alloc-free (empty vector);
// `inbound_held` may grow during AwaitingResend (cold path, PMR-arena-backed).
// Validation per data-model.md §E-2:
//   outstanding_begin <= outstanding_end, OR outstanding_end == 0 (infinity).
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
    // issued a counter-ResendRequest. When 0, no outbound replay in flight.
    std::uint32_t outbound_replay_cursor = 0;

    // Inbound messages with MsgSeqNum > next_expected_inbound that arrived
    // during AwaitingResend. Held in strict-MsgSeqNum order until gap closes.
    // Unbounded in v1.0 per [const §XV.15] block-mode + spec.md FR-009.
    std::pmr::vector<held_inbound_msg> inbound_held;

    // Reset to default-constructed shape on gap close. Clears inbound_held
    // (returning storage to the PMR arena per its policy). noexcept.
    void reset() noexcept;

    // Validation per data-model.md §E-2 rules.
    [[nodiscard]] bool invariants_hold() const noexcept;
};

}  // namespace fixpp::session
