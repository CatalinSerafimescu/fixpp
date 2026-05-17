// contracts/seqnum.hpp — shape oracle (NOT the build header)
// 005-OWNED type; promotes the [2e §4.7] placeholder in place (D-1 / [2e §10 Q9]).
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <cstdint>
#include <limits>

namespace fixpp::session {

// FIX MsgSeqNum(34) semantics. uint32 = observed-engine convention
// (QuickFIX C++/J, fix8) and byte-identical to the [2e §4.7] placeholder,
// so every existing 2e `#include <fixpp/session/seqnum.hpp>` resolves to
// THIS canonical type with zero 2e edits ([const §VI.5] handoff).
using seqnum_t = std::uint32_t;

inline constexpr seqnum_t seqnum_min = 1;  // FIX seqnums start at 1, [FIX-SL §4.1]
inline constexpr seqnum_t seqnum_max = std::numeric_limits<seqnum_t>::max();

// Counter manager contract (E3): increment-by-one per accepted/sent admin
// message; too-low(no PossDup) = session-fatal; too-high = RecoveryPending;
// `next == seqnum_max` + increment requested ⇒ surface [2e §6.7]
// store_seqnum_overflow (session-fatal, NO wrap). Bookkeeping serialized by
// fixpp::sync::async_mutex (D-7; structurally zero contention under the
// per-session-strand single domain).

}  // namespace fixpp::session
