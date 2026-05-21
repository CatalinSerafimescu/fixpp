// SPDX-License-Identifier: AGPL-3.0-or-later
//
// specs/008-message-store/contracts/store_errors.hpp
//
// SHAPE ORACLE — declaration-only contract for the 10 store_* error
// variants appended to fixpp::core::error at unused slots 56–65, design-doc
// table order. Anchor: .specify/2e-msgstore.md v0.4 §6.7. FR-021 / FR-022 /
// FR-023. Research D-6.
//
// NON-RENUMBERING append per [const §X.4]. The 007 cursor ended at slot 55
// (dispatch_aborted); 2e starts at slot 56. The next downstream feature
// inherits the cursor at slot 66.
//
// NOT introduced (recorded for 2i + future readers):
//   - store_concurrent_writer — REMOVED in v0.2 per Codex P1-5 (FIFO-fair
//     async_mutex makes the variant impossible).
//   - store_shim_timeout — REMOVED in v0.3 per Codex C-R2-P2-1 escalation
//     ([2e §4.8.B] Path A retired; no runtime adapter).
//
// C-ABI prefix-group mapping (documented for 2i; no extern "C" added here):
//   FIXPP_ERR_STORE_RUNTIME      ← { store_io_failure, store_capacity_exhausted,
//                                    store_seqnum_overflow }
//   FIXPP_ERR_STORE_CONSISTENCY  ← { store_seqnum_gap, store_seqnum_out_of_order,
//                                    store_seqnum_invalid, store_invalid_range }
//   FIXPP_ERR_STORE_CONFIG       ← { store_factory_failed }
//   FIXPP_ERR_STORE_VISITOR      ← { store_visitor_aborted }
//   FIXPP_ERR_CANCELLED          ← { store_cancelled }     (reused; joins
//                                                           2d's dispatch_aborted
//                                                           / clock_sleeps_cancelled)
//
// This file is a DOCUMENTATION ORACLE only — the actual variants are
// appended to the existing enum at include/fixpp/core/error.hpp (additive
// edit per the plan).
#pragma once

#include <cstdint>

namespace fixpp::core {

// EXACT slot allocation pinned at /plan; the implementation MUST append
// these to the existing `enum class error : std::uint8_t { ... }` at the
// listed slot values, in this order, between the existing slot-55
// dispatch_aborted and any future addition.
//
//   store_io_failure          = 56,  // FileStore I/O fault (disk full, hardware
//                                    // fault, ENOSPC, EACCES, mid-flush error
//                                    // from flush_for_session_close()).
//                                    // → FIXPP_ERR_STORE_RUNTIME
//   store_seqnum_gap          = 57,  // retrieve over a never-persisted gap
//                                    // (unless trailing edge of end == 0).
//                                    // → FIXPP_ERR_STORE_CONSISTENCY
//   store_seqnum_out_of_order = 58,  // store(seq, ...) with seq !=
//                                    // next_seqnum(dir, false) inside the
//                                    // writer-mutex CS (I-05; Opus N2-P2-3).
//                                    // → FIXPP_ERR_STORE_CONSISTENCY
//   store_capacity_exhausted  = 59,  // MemoryStore::store under bounded policy
//                                    // at per-direction cap (I-08).
//                                    // → FIXPP_ERR_STORE_RUNTIME
//   store_seqnum_overflow     = 60,  // next_seqnum(dir, true) when current
//                                    // == seqnum_max (session-fatal; I-18).
//                                    // → FIXPP_ERR_STORE_RUNTIME
//   store_factory_failed      = 61,  // MessageStoreFactory::make() validation
//                                    // failure (storage-DoS, sentinel
//                                    // mismatch, advisory lock contention,
//                                    // OOM at config validation).
//                                    // → FIXPP_ERR_STORE_CONFIG
//   store_visitor_aborted     = 62,  // retrieve_visitor::on_frame returned
//                                    // visit_result::abort (default
//                                    // abort_error()); PMR poison routed
//                                    // via trap_throw (I-20 / I-21).
//                                    // → FIXPP_ERR_STORE_VISITOR
//   store_seqnum_invalid      = 63,  // retrieve(begin=0, ...) — FIX wire
//                                    // seqnums start at 1 per [FIX-SL §4.1].
//                                    // → FIXPP_ERR_STORE_CONSISTENCY
//   store_invalid_range       = 64,  // retrieve(begin, end, ...) with
//                                    // end != 0 && end < begin.
//                                    // → FIXPP_ERR_STORE_CONSISTENCY
//   store_cancelled           = 65,  // Cancellation winning before a method's
//                                    // linearisation point per §6.1.4 (I-07).
//                                    // → FIXPP_ERR_CANCELLED (reused;
//                                    //   joins 2d's dispatch_aborted /
//                                    //   clock_sleeps_cancelled).

}  // namespace fixpp::core
