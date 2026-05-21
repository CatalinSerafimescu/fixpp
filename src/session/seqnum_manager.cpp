// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/session/seqnum_manager.cpp
//
// fixpp::session seqnum manager — out-of-line bodies (005, T018 stub).
//
// Phase 2 ships a STUB that links cleanly (T018). The full implementation
// (increment-by-one inbound/outbound counters serialised by async_mutex,
// too-low session-fatal, too-high session-fatal, seqnum_max overflow →
// store_seqnum_overflow) lands per US2 (T031/T032/T033, Phase 4).
//
// Anchors: data-model.md E3; research D-7; spec FR-008; [FIX-SL §4.1];
// contracts/seqnum.hpp. async_mutex usage per [2f §7.3]/[2f §7.6] (D-7).

// (No #includes needed for the stub: all symbols referenced are forward-
// declared in the header or not yet present. This translation unit exists to
// satisfy the CMake link graph with an explicit object file.)
