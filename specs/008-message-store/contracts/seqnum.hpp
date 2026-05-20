// SPDX-License-Identifier: AGPL-3.0-or-later
//
// specs/008-message-store/contracts/seqnum.hpp
//
// SHAPE ORACLE — declaration-only contract for fixpp::session::seqnum_t
// (placeholder). Anchor: .specify/2e-msgstore.md v0.4 §3.1 / §4.7 / §10 Q9.
// FR-003 / research D-1.
//
// PLACEHOLDER. Canonical seqnum_t type is owned by the deferred Phase-4
// session-module spec. This feature authors the header FRESH (no existing
// <fixpp/session/seqnum.hpp> on the 007 baseline — Clarifications Q2). When
// the Phase-4 spec lands the header is either re-exported from there or
// deleted with includes repointed (single-line edit per [const §VI.5]).
//
// Width choice (uint32_t vs uint64_t) is Phase-4-owned per [2e §10 Q9];
// 2e MUST consume the convention, not foreclose it. uint32_t matches the
// observed convention; switching to uint64_t at Phase-4 doubles
// counter-record + index-entry memory but eliminates store_seqnum_overflow
// for never-reset use cases (audit-trail sessions running 24/7 for years).
#pragma once

#include <cstdint>
#include <limits>

namespace fixpp::session {

using seqnum_t = std::uint32_t;

inline constexpr seqnum_t seqnum_min = 1;
inline constexpr seqnum_t seqnum_max = std::numeric_limits<seqnum_t>::max();

}  // namespace fixpp::session
