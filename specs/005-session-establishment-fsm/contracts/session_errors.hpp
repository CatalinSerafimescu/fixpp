// contracts/session_errors.hpp — shape oracle (NOT the build header)
// New fixpp::core::error variants appended at unused slots 43..N,
// non-renumbering ([const §X.4]; D-9). Per-doc-prefix discipline:
// 2a DECIMAL / 2b WIRE / 2c DICT / 2d THREAD / 2e STORE → 005 SESSION.
// C-ABI coalescing target FIXPP_ERR_SESSION_* owned by 2i (not added here).
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

// PLANNED allocation (NOT yet published — pre-impl bundle; numbers/list
// decided at Gate A / pinned at /speckit-tasks before any C-ABI release).
// Appended to include/fixpp/core/error.hpp (occupied: 1,10-13,20-29,30-42):
//   session_invalid_logon              = 43   FR-003/004
//   session_compid_mismatch            = 44   FR-004  [FIX-SL §4.2.2]
//   session_begin_string_unsupported   = 45   FR-003  [FIX-SL §4.2.1]
//   session_seqnum_too_low             = 46   FR-008  [FIX-SL §4.1] (fatal)
//   session_seqnum_gap_unrecoverable   = 47   FR-008/FR-001 (too-high =
//                                              session-fatal; recovery
//                                              deferred — Session-2026-05-18;
//                                              replaces session_recovery_pending)
//   session_sending_time_accuracy      = 48   Q3/FR-013 (RejectReason=10)
//   session_msg_type_invalid_for_state = 49   FR-007  [FIX-SL §4.5.4]
//   session_logout_timeout             = 50   FR-005  [FIX-SL §4.6.2]
//   session_test_request_unanswered    = 51   FR-006  [FIX-SL §4.5.5]
//   session_admin_not_supported        = 52   FR-017  [FIX-SL §4.10]
//   session_invalid_config             = 53   [2d §4.5] open-validation
//
// REUSE (do NOT duplicate):
//   [2e §6.7] store_seqnum_overflow — seqnum_max overflow, session-fatal.
//   [2d §6.5/§6.7] session_already_closed / dispatch_aborted /
//     clock_sleeps_cancelled — 005 (first threading consumer) introduces
//     ONLY the minimal set it exercises, at the next contiguous slots;
//     full [2d] thread_* set lands with a later 2d/transport feature.
//     (Cross-doc sequencing — Gate A confirms; mirrors 004's wire_* intro.)
//
// Final slot numbers/list pinned at Gate A; once published in a tagged
// C-ABI release, a numeric value never changes meaning ([const §X.4]).
