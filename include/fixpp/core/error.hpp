#pragma once
// include/fixpp/core/error.hpp
// Engine-wide error enum for fixpp::core. Owned by 2k; this feature (001-core-decimal)
// contributes the four decimal variants per data-model.md Entity 5.
// 002-dictionary-xml-loader contributes the three dict_* variants per its
// research.md D-10. Variant numbering is additive (no renumbering of existing
// slots) per `[const §X.4]` forwards-compat.

#include <cstdint>
#include <expected>

namespace fixpp::core {

enum class error : std::uint8_t {
    // slot 0 reserved for ok (never stored in unexpected)
    out_of_memory = 1,

    // decimal variants — owned by 001-core-decimal, contributes to 2k
    decimal_invalid_input = 10,
    decimal_overflow = 11,
    decimal_precision_loss = 12,
    decimal_buffer_too_small = 13,

    // dict variants — owned by 002-dictionary-xml-loader (research.md D-10);
    // additive at unused slots per `[const §X.4]`. Each exception type in
    // `<fixpp/dict/error.hpp>` carries the matching variant via `code()`.
    dict_xml_parse_failed = 20,
    dict_unknown_version = 21,
    dict_xml_oom = 22,

    // dict codegen / reify variants — owned by 003-dictionary-codegen
    // (research.md D-10/D-21; data-model "Error mapping"; spec AC-VP6).
    // Additive at unused slots 23..28 per `[const §X.4]`; non-renumbering;
    // existing slots above preserved verbatim. The 2b/wire "field absent"
    // error from `MessageView::get<1128>()` is 2b-owned, NOT a slot here
    // (cross-feature note, contracts/version_profile.hpp / spec A6).
    dict_reify_msg_type_mismatch = 23,
    dict_reify_unknown_msg_type = 24,
    dict_reify_oom = 25,
    dict_unresolved_application_version = 26,
    dict_unknown_appl_ver_id = 27,
    dict_no_dictionary_for_application_version = 28,
    // R6 placeholder: owning_<Msg>::from_view wired to accept view but the frozen
    // wire stub carries no frame bytes (2b swaps in the real body). This distinct
    // error code means tests cannot go green for the wrong reason: a positive
    // oracle must assert exactly this code until 2b lands (gate-b/r1 RC#1).
    dict_reify_wire_body_not_ready = 29,  // cutover-obsolete once 2b lands: the
    // real OffsetTable-backed MessageView carries frame bytes, so
    // owning_<Msg>::from_view no longer needs this "no body yet" sentinel.
    // Slot KEPT (non-renumbering, `[const §X.4]`); annotated, not removed.

    // wire variants — owned by 004-wire-codec (data-model "Error mapping";
    // contracts/wire_errors.hpp; `[2b §6.7]`). Appended at unused slots 30..42,
    // non-renumbering (`[const §X.4]`). v0.1's wire_tag_count_exceeded was
    // dropped (distinct-tag cap removed, RC#1) and is NOT reintroduced. The
    // 2i C-ABI FIXPP_ERR_WIRE_* coalescing + tools/abi_history audit-trail
    // entry are deferred to 2i under the same time-bounded waiver shape as
    // 002/003 (no C-ABI surface added here — research D-13; this is C++
    // core::error, not the C-ABI fixpp_error_t).
    wire_frame_too_large = 30,           // [2b §6.1.3]
    wire_invalid_body_length = 31,       // [2b §6.1.3]
    wire_checksum_mismatch = 32,         // [2b §6.1.5]
    wire_framing_resync = 33,            // [2b §6.1.2]
    wire_invalid_field_format = 34,      // [2b §6.2]
    wire_offset_table_full = 35,         // [2b §1.2/§4.4]
    wire_group_too_large = 36,           // [2b §1.2/§4.4]
    wire_tag_out_of_range = 37,          // [2b §1.2]
    wire_required_field_missing = 38,    // [2b §6.5.4]
    wire_header_out_of_order = 39,       // [2b §6.5.1]
    wire_field_value_out_of_range = 40,  // [2b §6.5.3]
    wire_field_value_truncated = 41,     // [2b §6.5 rule 3]/[2b §6.7]: the
    // validator's §6.5-rule-3 type-check site RE-MAPS 2a/001's
    // decimal_precision_loss (=12) onto this wire-domain slot so the
    // Session-Reject path carries a wire_* code (re-mapped, not redefined).
    wire_unexpected_tag = 42,  // [2b §6.5.5] SessionRejectReason=2

    // sync variants — owned by 006-async-mutex (data-model.md Error mapping;
    // contracts/sync_errors.hpp; [2f §6.5]). Appended at unused slots 43–46,
    // non-renumbering ([const §X.4]). C-ABI FIXPP_ERR_SYNC_* coalescing +
    // tools/abi_history audit-trail entry deferred to 2i (same time-bounded
    // waiver shape as 002/003/004; no C-ABI surface added here).
    sync_lock_aborted = 43,          // [2f §4.5] — cancellation won the
                                     //   CAS-arbitration race against the drain;
                                     //   waiter was not granted ownership.
                                     //   Joins FIXPP_ERR_CANCELLED at the C ABI.
    sync_lock_alloc_failed = 44,     // [2f §4.3] — PMR fallback's allocate()
                                     //   threw std::bad_alloc (mr exhausted) or
                                     //   the embedded inline 32-byte slot_storage_
                                     //   buffer overflowed and null_memory_resource
                                     //   rejected the allocation (trap_throw per
                                     //   [2a §4.2]).
    sync_lock_outside_session = 45,  // [2f §4.3.2] — async_lock_via_session_
                                     //   executor called outside a session
                                     //   serialisation domain (bound executor is
                                     //   not a session_executor value).
    sync_lock_drained = 46,          // [2f §4.7.2] NEW v1.1 / RC-B —
                                     //   cancel_and_drain() set draining_ = true;
                                     //   subsequent async_lock(...) fast-fails
                                     //   without enqueuing.

    // threading variants — owned by 007-threading-clock (data-model.md Error
    // model; contracts/threading_errors.hpp; [2d §6.7]). Appended at unused
    // slots 47–55, non-renumbering ([const §X.4]); design-doc table order. NO
    // separate enum type (D-2 — single core::error; the "session" group is the
    // lifecycle subset, for C-ABI coalescing only). C-ABI
    // FIXPP_ERR_THREAD_{CONFIG,SESSION_LIFECYCLE,RUNTIME} / FIXPP_ERR_CANCELLED
    // coalescing + tools/abi_history audit-trail entry deferred to 2i (same
    // time-bounded waiver shape as 002/003/004/006; no C-ABI surface added
    // here). NOT introduced (design-doc dropped — [2d §6.7] / D-7):
    // trace_context_provider_threw (C-P2-4), cancellation_propagation_timeout
    // (N-P2-1), version_registry_dictionary_missing (Opus N2-P2-1 — the
    // FIXT.1.1 miss routes through the EXISTING slot-28
    // dict_no_dictionary_for_application_version).
    executor_already_stopped = 47,    // [2d §4.4] — resolved executor
                                      //   (executor_override.value_or(
                                      //   EngineConfig::executor)) joined before
                                      //   Engine::open / Session::open.
                                      //   → FIXPP_ERR_THREAD_CONFIG
    executor_not_serialised = 48,     // [2d §4.5]/§6.1 — mode==direct_executor
                                      //   without already_serialized_executor
                                      //   (root cause #1 / C-P1-2); enforced at
                                      //   the SINGLE point make_session_executor.
                                      //   → FIXPP_ERR_THREAD_CONFIG
    clock_sleeps_cancelled = 49,      // [2d §4.1]/§6.6 — a sleep_until waiter
                                      //   completed via cancel_sleeps. Maps to
                                      //   asio::error::operation_aborted at the
                                      //   awaitable level; this value is the
                                      //   OPTIONAL expected_t projection (NOT the
                                      //   sleep_until return type — [2d §4.1]).
                                      //   Joins FIXPP_ERR_CANCELLED ([const §XI.2]).
    strand_dispatch_failed_oom = 50,  // [2d §6.2]/§6.5 — PMR fallback for the
                                      //   strand's posted handler / the
                                      //   cancellable_dispatch node exhausted the
                                      //   per-session arena. Forced disconnect.
                                      //   → FIXPP_ERR_THREAD_RUNTIME
    session_already_open = 51,        // [2d §4.7] — Session::open() called twice
                                      //   on the same handle (programmer error).
                                      //   → FIXPP_ERR_THREAD_SESSION_LIFECYCLE
    session_already_closed = 52,      // [2d §4.7]:830-832,863 / [2d §6.5]:1172 —
                                      //   close() on a NEVER-OPENED or an
                                      //   ALREADY-CLOSED (drained) session. NOT
                                      //   returned for an ALREADY-CLOSING
                                      //   (in-flight) session — that returns the
                                      //   SAME in-flight awaitable, no error.
                                      //   Idempotency; non-fatal.
                                      //   → FIXPP_ERR_THREAD_SESSION_LIFECYCLE
    invalid_session_config = 53,      // [2d §4.5]/§6.1 — incompatible combo
                                      //   (direct_executor+lock_policy::spin even
                                      //   when attested; null EngineConfig::
                                      //   executor; null dictionary;
                                      //   default-constructed security_profile
                                      //   sentinel — N-P2-3).
                                      //   → FIXPP_ERR_THREAD_CONFIG
    clock_not_set = 54,               // [2d §4.4] — EngineConfig::clock is null
                                      //   at Engine::open, regardless of
                                      //   per-session clock_override (root #2).
                                      //   → FIXPP_ERR_THREAD_CONFIG
    dispatch_aborted = 55,            // [2d §6.5] — cancellable_dispatch's slot
                                      //   fired BEFORE the posted handler was
                                      //   picked up; handler reaped (not invoked).
                                      //   Expected on the §4.7 phase-2 close path.
                                      //   Joins FIXPP_ERR_CANCELLED (reused).

    // ── 008-message-store: 10 store_* variants per [2e §6.7] / FR-021 /
    //    FR-023 / research D-6. Non-renumbering append at unused slots 56–65,
    //    pre-publication per [const §X.4]. Design-doc table order. Cursor
    //    after this block ends at slot 66 (next downstream feature).
    //
    //    C-ABI prefix-group coalescing (documented for `2i`; no extern "C"
    //    surface added by this feature — research D-6):
    //      FIXPP_ERR_STORE_RUNTIME      ← { store_io_failure,
    //                                       store_capacity_exhausted,
    //                                       store_seqnum_overflow }
    //      FIXPP_ERR_STORE_CONSISTENCY  ← { store_seqnum_gap,
    //                                       store_seqnum_out_of_order,
    //                                       store_seqnum_invalid,
    //                                       store_invalid_range }
    //      FIXPP_ERR_STORE_CONFIG       ← { store_factory_failed }
    //      FIXPP_ERR_STORE_VISITOR      ← { store_visitor_aborted }
    //      FIXPP_ERR_CANCELLED          ← { store_cancelled }   (reused; joins
    //                                                            dispatch_aborted /
    //                                                            clock_sleeps_cancelled).
    //
    //    NOT introduced (recorded for future readers / 2i):
    //      store_concurrent_writer — REMOVED v0.2 per Codex P1-5 (FIFO-fair
    //        async_mutex makes the variant impossible).
    //      store_shim_timeout      — REMOVED v0.3 per Codex C-R2-P2-1
    //        ([2e §4.8.B] Path A retired; no runtime adapter).
    store_io_failure = 56,           // FileStore I/O fault (disk full, hardware
                                     //   fault, ENOSPC, EACCES, mid-flush error
                                     //   from flush_for_session_close()).
                                     //   → FIXPP_ERR_STORE_RUNTIME
    store_seqnum_gap = 57,           // retrieve over a never-persisted gap
                                     //   (unless trailing edge of end == 0).
                                     //   → FIXPP_ERR_STORE_CONSISTENCY
    store_seqnum_out_of_order = 58,  // store(seq, ...) with seq !=
                                     //   next_seqnum(dir, false) inside the
                                     //   writer-mutex CS (I-05; Opus N2-P2-3).
                                     //   → FIXPP_ERR_STORE_CONSISTENCY
    store_capacity_exhausted = 59,   // MemoryStore::store under bounded policy
                                     //   at per-direction cap (I-08).
                                     //   → FIXPP_ERR_STORE_RUNTIME
    store_seqnum_overflow = 60,      // next_seqnum(dir, true) when current
                                     //   == seqnum_max (session-fatal; I-18).
                                     //   → FIXPP_ERR_STORE_RUNTIME
    store_factory_failed = 61,       // MessageStoreFactory::make() validation
                                     //   failure (CompID filesystem safety
                                     //   per [2e §D.4], storage-DoS, sentinel
                                     //   mismatch, advisory lock contention,
                                     //   OOM at config validation, empty
                                     //   resolved file_io_executor).
                                     //   → FIXPP_ERR_STORE_CONFIG
    store_visitor_aborted = 62,      // retrieve_visitor::on_frame returned
                                     //   visit_result::abort (default
                                     //   abort_error()); PMR poison routed
                                     //   via trap_throw (I-20 / I-21).
                                     //   → FIXPP_ERR_STORE_VISITOR
    store_seqnum_invalid = 63,       // retrieve(begin=0, ...) — FIX wire
                                     //   seqnums start at 1 per [FIX-SL §4.1].
                                     //   → FIXPP_ERR_STORE_CONSISTENCY
    store_invalid_range = 64,        // retrieve(begin, end, ...) with
                                     //   end != 0 && end < begin.
                                     //   → FIXPP_ERR_STORE_CONSISTENCY
    store_cancelled = 65,            // Cancellation winning before a method's
                                     //   linearisation point per [2e §6.1.4]
                                     //   (I-07).
                                     //   → FIXPP_ERR_CANCELLED (reused; joins
                                     //     dispatch_aborted /
                                     //     clock_sleeps_cancelled).

    // ── 005-session-establishment-fsm: 11 session_* variants per data-model.md
    //    "Error mapping" §56..N / FR-003/004/005/006/007/008/013/017.
    //    Non-renumbering append at unused slots 66–76, post-publication of
    //    006/007/008 (occupied: 1,10-13,20-29,30-42,43-65). Data-model.md
    //    planned these at 43–53 before 006/007/008 merged; they are published
    //    here at the next contiguous free slots per [const §X.4].
    //
    //    REUSE — do NOT duplicate these pre-existing variants:
    //      session_already_open   = 51  ([2d §4.7] / FR-018)
    //      session_already_closed = 52  ([2d §4.7] / FR-005 idempotency)
    //      invalid_session_config = 53  ([2d §4.5] / FR-018 open-validation)
    //      clock_sleeps_cancelled = 49  ([2d §4.1]/§6.6)
    //      dispatch_aborted       = 55  ([2d §6.5] / close path)
    //      store_seqnum_overflow  = 60  ([2e §6.7] / seqnum_max session-fatal)
    //
    //    C-ABI prefix-group coalescing (documented for 2i; no extern "C"
    //    surface added by this feature — research D-12):
    //      FIXPP_ERR_SESSION_REFUSAL     ← { session_invalid_logon,
    //                                        session_compid_mismatch,
    //                                        session_begin_string_unsupported }
    //      FIXPP_ERR_SESSION_FATAL       ← { session_seqnum_too_low,
    //                                        session_seqnum_gap_unrecoverable }
    //      FIXPP_ERR_SESSION_REJECT      ← { session_sending_time_accuracy,
    //                                        session_msg_type_invalid_for_state,
    //                                        session_admin_not_supported }
    //      FIXPP_ERR_SESSION_LIFECYCLE   ← { session_logout_timeout,
    //                                        session_test_request_unanswered,
    //                                        session_invalid_config }
    session_invalid_logon = 66,               // FR-003/004, US1#3/#4, [FIX-SL §4.2]/§4.3 —
                                              //   Logon refused; session never reaches Active.
                                              //   → FIXPP_ERR_SESSION_REFUSAL
    session_compid_mismatch = 67,             // FR-004, [FIX-SL §4.2.2] — SenderCompID/
                                              //   TargetCompID do not match the configured
                                              //   counterparty identity (point-to-point 49/56).
                                              //   → FIXPP_ERR_SESSION_REFUSAL
    session_begin_string_unsupported = 68,    // FR-003, [FIX-SL §4.2.1] — BeginString(8) is
                                              //   not in the supported version set (FIX.4.2/4.4
                                              //   for 005; FIXT.1.1/5.0SP2 deferred).
                                              //   → FIXPP_ERR_SESSION_REFUSAL
    session_seqnum_too_low = 69,              // FR-008, [FIX-SL §4.1] — inbound MsgSeqNum <
                                              //   next-expected, no PossDupFlag=Y: session-fatal
                                              //   (ordered-sequence integrity). PossDup handling
                                              //   is deferred (S-010); treated as the no-PossDup
                                              //   case in 005. → FIXPP_ERR_SESSION_FATAL
    session_seqnum_gap_unrecoverable = 70,    // FR-008/FR-001, Session-2026-05-18 — inbound
                                              //   MsgSeqNum too-high: session-fatal disconnect.
                                              //   Replaces the removed session_recovery_pending
                                              //   (D-2 Gate A round 1). Real ResendRequest-driven
                                              //   recovery is the deferred session-recovery feature
                                              //   ([2e §3.1] / [2e §4 last bullet]).
                                              //   → FIXPP_ERR_SESSION_FATAL
    session_sending_time_accuracy = 71,       // Clarification Q3, FR-013, [FIX-SL §4.2.3] —
                                              //   inbound SendingTime(52) diverges > MaxLatency
                                              //   (default 120 s, D-8). Disposition: emit
                                              //   Reject(SessionRejectReason=10, ref tag 52) →
                                              //   Logout → disconnect; Logon-path → logout-with-
                                              //   error, no standalone Reject.
                                              //   → FIXPP_ERR_SESSION_REJECT
    session_msg_type_invalid_for_state = 72,  // FR-007, [FIX-SL §4.5.4] — message type not
                                              //   legal in the current FSM state (e.g. Heartbeat
                                              //   before Active). Surfaced as a session Reject
                                              //   with RefMsgType. No reject loop (I-5).
                                              //   → FIXPP_ERR_SESSION_REJECT
    session_logout_timeout = 73,              // FR-005, [FIX-SL §4.6.2] — graceful-close
                                              //   (Logout exchange) timed out: phase-1 child
                                              //   cancellation_state expires; session force-
                                              //   disconnects → Disconnected (I-9, D-8: 2 s
                                              //   QuickFIX LogoutTimeout default).
                                              //   → FIXPP_ERR_SESSION_LIFECYCLE
    session_test_request_unanswered = 74,     // FR-006, [FIX-SL §4.5.5] — inbound silence
                                              //   exceeded test_request_threshold (1×HeartBtInt,
                                              //   D-8) without a Heartbeat echo: session unhealthy
                                              //   → disconnect. → FIXPP_ERR_SESSION_LIFECYCLE
    session_admin_not_supported = 75,         // FR-017, [FIX-SL §4.10] — deferred admin type
                                              //   received (ResendRequest/SequenceReset): defined
                                              //   bounded transition (session-level Reject, never
                                              //   undefined/silent). Recovery is the deferred
                                              //   session-recovery feature's.
                                              //   → FIXPP_ERR_SESSION_REJECT
    session_invalid_config = 76,              // [2d §4.5] N-P2-3 / Session::open validation —
                                              //   session-level config error NOT caught at engine
                                              //   level (e.g. missing CompID strings, null
                                              //   begin_string, out-of-range config field value).
                                              //   Distinct from invalid_session_config (slot 53)
                                              //   which catches threading/executor/security_profile
                                              //   config errors at Session::open level.
                                              //   → FIXPP_ERR_SESSION_LIFECYCLE
    session_invalid_state_for_send = 77,       // FR-005 (010), [FIX-SL §4.5.4] — Session::send(...) called
                                            //   while the FSM is not in `Active` (e.g. NotConnected,
                                            //   LogonSent, Disconnected). Replaces the 005-era reuse
                                            //   of `session_invalid_logon` at this site (semantic
                                            //   near-fit; the caller's state mismatch is distinct
                                            //   from a Logon refusal). No reject loop (I-5).
                                            //   → FIXPP_ERR_SESSION_REJECT
};

template <class T>
using expected_t = std::expected<T, error>;

}  // namespace fixpp::core
