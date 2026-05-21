// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/session/seqnum.hpp
//
// fixpp::session::seqnum_t — 005-OWNED canonical FIX-wire sequence-number type.
//
// Anchor: data-model.md E4; research D-1; .specify/2e-msgstore.md v0.5 §3.1/
// §4.7/§10 Q9 ([2e §10 Q9] handoff CLOSED). FR-003. SC-010.
//
// 005-session-establishment-fsm OWNS this type (T008). The 008-message-store
// placeholder is promoted in-place per D-1/[2e §10 Q9]: the alias and constants
// remain byte-identical to the 008 placeholder so every existing 008/007
// `#include <fixpp/session/seqnum.hpp>` resolves to this canonical type with
// zero edits ([const §VI.5] single-edit handoff — mechanism: in-place promotion).
//
// Width: uint32_t — matches observed FIX-engine convention (QuickFIX C++/J,
// fix8) and the 008 placeholder. Overflow at seqnum_max is session-fatal with
// no wrap, surfaced via [2e §6.7] store_seqnum_overflow (D-1 / FR-008 / I-8).
// uint64_t widening is a clean post-v1.0 option (single-file edit, no 2i ABI
// freeze yet) — see D-1 alternatives. Seam #13 (tests/session/seqnum_t_handoff_test.cpp).
#pragma once

#include <cstdint>
#include <limits>

namespace fixpp::session {

using seqnum_t = std::uint32_t;

inline constexpr seqnum_t seqnum_min = 1;
inline constexpr seqnum_t seqnum_max = std::numeric_limits<seqnum_t>::max();

}  // namespace fixpp::session
