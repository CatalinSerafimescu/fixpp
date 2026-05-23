// contracts/session.hpp — shape oracle (NOT the build header)
// Session lifecycle + the fromAdmin/fromApp + send surface.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <memory>
#include <span>
#include <cstddef>
// asio::awaitable, fixpp::core::expected_t, error, Clock, session_executor,
// fixpp::session::{MessageStore, SessionConfig, fsm_state, seqnum_t}

namespace fixpp::session {

// One Session = one counterparty pair (D-11; no registry/demux here).
// Emits NO extern "C" symbol ([const §X.2], FR-015). All public surfaces
// noexcept across the inbound-process / timer-fire window; a throwing user
// callback TRAPS (core::detail::trap_throw), never propagates (FR-015).
//
// Reentrancy contract (documented per entry point, [const §X.5] / FR-016):
//   open()/close()         — session-strand; not reentrant from a callback.
//   on_inbound_frame(...)  — session-strand; the engine's transport feeds it.
//   send(...)              — session-strand; durable-before-transmit (I-3).
//   fromAdmin/fromApp      — user callbacks dispatched via cancellable_
//                            dispatch ([2d §6.5]) on the per-session strand
//                            by default (threading_mode::per_session_strand).
class Session {
public:
    // Resolve effective_clock = clock_override ?: EngineConfig::clock ONCE
    // ([2d §7.9]); validate config (session_invalid_config on bad sentinel);
    // start FSM in NotConnected. Initiator → emits Logon (seq=1) → LogonSent.
    [[nodiscard]] asio::awaitable<expected_t<void>> open() noexcept;

    enum class close_mode { graceful, terminal };
    // graceful = two-phase ([2d §6.5]): Logout exchange + close-timeout under
    // a CHILD cancellation_state; phase-2 root total only after phase-1.
    // terminal = skip phase-1. Idempotent (I-9): 2nd call same outcome;
    // never-opened/already-closed → error::session_already_closed.
    [[nodiscard]] asio::awaitable<expected_t<void>>
        close(close_mode) noexcept;

    // Engine feeds verified inbound frames (post-Framer/Parser, [2e]
    // inbound ordering: store(inbound) BEFORE this dispatches fromAdmin/
    // fromApp). Returns the FSM-defined disposition.
    [[nodiscard]] asio::awaitable<expected_t<void>>
        on_inbound_frame(std::span<const std::byte> frame) noexcept;

    // User output. Stamps SendingTime(52) from effective_clock, assigns
    // MsgSeqNum(34), Writer::commit, store(seq, committed, outbound) BEFORE
    // transport::async_write (durable-before-transmit, I-3).
    [[nodiscard]] asio::awaitable<expected_t<void>>
        send(std::span<const std::byte> app_payload) noexcept;

    [[nodiscard]] fsm_state state() const noexcept;

private:
    std::unique_ptr<MessageStore> store_;   // [2e §4.1] seam, CONSUMED (E6)
    // effective_clock_, fsm_, seqnum state (E3), timer set, SessionConfig view
};

}  // namespace fixpp::session
