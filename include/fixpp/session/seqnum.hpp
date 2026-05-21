// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/session/seqnum.hpp
//
// fixpp::session::seqnum_t — placeholder FIX-wire sequence-number type.
//
// Anchor: .specify/2e-msgstore.md v0.5 §3.1 / §4.7 / §10 Q9. FR-003.
// Entity E9. Research D-1.
//
// PLACEHOLDER (cross-doc handoff). The canonical seqnum_t type is owned by
// the deferred Phase-4 session-module spec (`005-session-establishment-fsm`).
// This feature authors the header FRESH (no existing
// <fixpp/session/seqnum.hpp> on the 007 baseline — Clarifications Session
// 2026-05-20 Q2). When the Phase-4 spec lands, the header is either
// re-exported from there or deleted with includes repointed (single-line
// edit per [const §VI.5]).
//
// Width choice (uint32_t vs uint64_t) is Phase-4-owned per [2e §10 Q9];
// 008 MUST consume the convention, not foreclose it. uint32_t matches the
// observed FIX convention; switching to uint64_t at Phase-4 doubles
// counter-record + index-entry memory but eliminates store_seqnum_overflow
// for never-reset use cases (audit-trail sessions running 24/7 for years).
//
// Mirror of specs/008-message-store/contracts/seqnum.hpp (shape oracle).
#pragma once

#include <cstdint>
#include <limits>

namespace fixpp::session {

using seqnum_t = std::uint32_t;

inline constexpr seqnum_t seqnum_min = 1;
inline constexpr seqnum_t seqnum_max = std::numeric_limits<seqnum_t>::max();

}  // namespace fixpp::session
