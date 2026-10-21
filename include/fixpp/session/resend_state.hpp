// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/session/resend_state.hpp
//
// fixpp::session::ResendState — per-session Resend sub-protocol state.
// Anchors: FR-009..FR-015, D-2..D-5; [FIX-SL §4.3]; data-model.md §E-2;
// contracts/resend_state.hpp.
//
// Value-typed; embedded in ReconnectFsm. No allocations on the FSM transition
// path (Active ↔ AwaitingResend transient flag); inbound_held has no writer
// (see the member's comment).
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <vector>

namespace fixpp::session {

// The shape 013 FR-009 gave an inbound frame held during AwaitingResend; nothing
// creates one (see ResendState::inbound_held).
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
// `inbound_held` has no writer (see the member's comment).
// Validation per data-model.md §E-2:
//   outstanding_begin <= outstanding_end, OR outstanding_end == 0 (infinity).
struct ResendState {
    // FR-009 — BeginSeqNo we sent in ResendRequest(2).
    std::uint32_t outstanding_begin = 0;

    // FR-009 — EndSeqNo we sent; 0 means "infinity" per D-2 / FIX-SL §4.3.2.
    std::uint32_t outstanding_end = 0;

    // fixpp#537 (`.specify/536-537-538-recovery-liveness.md` §4.3, D-537-5) supersedes
    // this field's first meaning, the start of a recovery for SC-005's elapsed metric.
    // It is the steady-clock instant of the resend's last progress: written on entering
    // AwaitingResend and again each time NextNumIn advances while the resend is
    // outstanding (ReconnectFsm::mark_resend_progress). The deadline of an unfilled gap
    // counts from it. Not written on a session with no clock.
    std::chrono::steady_clock::time_point started_at;

    // High-water-mark of what we've received in the replay; advances as
    // PossDup=Y application messages OR GapFill SequenceReset arrives.
    std::uint32_t inbound_filled_through = 0;

    // Outbound-replay cursor — current position in our outbound replay if peer
    // issued a counter-ResendRequest. When 0, no outbound replay in flight.
    std::uint32_t outbound_replay_cursor = 0;

    // No writer: no frame is held (re-check: `git grep -n inbound_held -- src include`).
    // A frame above next_expected_inbound that arrives while a resend is outstanding is
    // ignored, and the peer's replay fetches it (fixpp#537;
    // .specify/536-537-538-recovery-liveness.md §4.3, D-537-1 and D-537-2; §4.4 rejects
    // holding it). 013 FR-009's queue was not built (B&L L-013-2). Removing the member
    // changes this installed header's layout.
    std::pmr::vector<held_inbound_msg> inbound_held;

    // Reset to default-constructed shape on gap close. Clears inbound_held
    // (returning storage to the PMR arena per its policy). noexcept.
    void reset() noexcept;

    // Validation per data-model.md §E-2 rules.
    [[nodiscard]] bool invariants_hold() const noexcept;
};

}  // namespace fixpp::session
