// SPDX-License-Identifier: AGPL-3.0-or-later
//
// specs/006-async-mutex/contracts/sync_errors.hpp
//
// ─────────────────────────────────────────────────────────────────────────────
// SHAPE ORACLE (NOT THE BUILD HEADER)
// This file is a planning-phase contract artifact.
// It specifies the exact types, names, and slot assignments that the
// implementation MUST match. It is NOT compiled into the library.
//
// Build header target: include/fixpp/core/error.hpp
//   (additive extension — append slots 43–46 per [const §X.4] non-renumbering)
// ─────────────────────────────────────────────────────────────────────────────
//
// Source of truth: .specify/2f-async-mutex.md v1.5 §6.5
//
// 2f introduces 4 new `fixpp::core::error` variants at the first free slots
// after the wire_* block (last occupied: wire_unexpected_tag = 42; first
// free = 43; verified against include/fixpp/core/error.hpp on branch
// 006-async-mutex 2026-05-18).
//
// Non-renumbering invariant: once these slots are published, they are NEVER
// renumbered per [const §X.4]. Future additions start at 47 or higher.
//
// C-ABI mapping (delegated to 2i — not assigned here):
//   sync_lock_aborted          → FIXPP_ERR_CANCELLED
//                                  (joins dispatch_aborted / clock_sleeps_cancelled /
//                                   store_cancelled per [2d §6.7])
//   sync_lock_alloc_failed     → FIXPP_ERR_SYNC_RUNTIME
//   sync_lock_outside_session  → FIXPP_ERR_SYNC_RUNTIME
//   sync_lock_drained          → FIXPP_ERR_SYNC_RUNTIME
//
// NOTE: 2f does NOT introduce an `async_mutex_destroyed_with_waiters` variant.
// The ~async_mutex() destructor fires std::terminate() (RC#3 fix); there is
// no expected_t form for the precondition violation.
//
// ─────────────────────────────────────────────────────────────────────────────

// The four variants are appended to the existing fixpp::core::error enum
// in include/fixpp/core/error.hpp. This oracle documents only the additive
// extension; the full enum definition lives in the build header.
//
// Addition shape (copy-paste target for the implementation):
//
//     // sync variants — owned by 006-async-mutex (data-model.md Error mapping;
//     // contracts/sync_errors.hpp; [2f §6.5]). Appended at unused slots 43–46,
//     // non-renumbering ([const §X.4]). C-ABI FIXPP_ERR_SYNC_* coalescing +
//     // tools/abi_history audit-trail entry deferred to 2i (same time-bounded
//     // waiver shape as 002/003/004; no C-ABI surface added here).
//     sync_lock_aborted         = 43,  // [2f §4.5] — cancellation won the
//                                      //   CAS-arbitration race against the drain;
//                                      //   waiter was not granted ownership.
//                                      //   Joins FIXPP_ERR_CANCELLED at the C ABI.
//     sync_lock_alloc_failed    = 44,  // [2f §4.3] — PMR fallback's allocate()
//                                      //   threw std::bad_alloc (mr exhausted) or
//                                      //   the embedded inline 32-byte slot_storage_
//                                      //   buffer overflowed and null_memory_resource
//                                      //   rejected the allocation (trap_throw per
//                                      //   [2a §4.2]).
//     sync_lock_outside_session = 45,  // [2f §4.3.2] — async_lock_via_session_
//                                      //   executor called outside a session
//                                      //   serialisation domain (bound executor is
//                                      //   not a session_executor value).
//     sync_lock_drained         = 46,  // [2f §4.7.2] NEW v1.1 / RC-B —
//                                      //   cancel_and_drain() set draining_ = true;
//                                      //   subsequent async_lock(...) fast-fails
//                                      //   without enqueuing.

//
// ─────────────────────────────────────────────────────────────────────────────
// Slot occupancy reference (as of 006-async-mutex branch, 2026-05-18):
//   slot 1      = out_of_memory
//   slots 10–13 = decimal_invalid_input / decimal_overflow /
//                 decimal_precision_loss / decimal_buffer_too_small
//   slots 20–22 = dict_xml_parse_failed / dict_unknown_version / dict_xml_oom
//   slots 23–29 = dict_reify_msg_type_mismatch / dict_reify_unknown_msg_type /
//                 dict_reify_oom / dict_unresolved_application_version /
//                 dict_unknown_appl_ver_id /
//                 dict_no_dictionary_for_application_version /
//                 dict_reify_wire_body_not_ready
//   slots 30–42 = wire_frame_too_large / wire_invalid_body_length /
//                 wire_checksum_mismatch / wire_framing_resync /
//                 wire_invalid_field_format / wire_offset_table_full /
//                 wire_group_too_large / wire_tag_out_of_range /
//                 wire_required_field_missing / wire_header_out_of_order /
//                 wire_field_value_out_of_range / wire_field_value_truncated /
//                 wire_unexpected_tag
//   slots 43–46 = sync_lock_aborted / sync_lock_alloc_failed /
//                 sync_lock_outside_session / sync_lock_drained  ← THIS FEATURE
// ─────────────────────────────────────────────────────────────────────────────
