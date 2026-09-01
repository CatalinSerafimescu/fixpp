#pragma once
// include/fixpp/core/error.hpp
// Engine-wide error enum for fixpp::core. Owned by 2k; this feature (001-core-decimal)
// contributes the four decimal variants per data-model.md Entity 5.
// 002-dictionary-xml-loader contributes the three dict_* variants per its
// research.md D-10. Variant numbering is additive (no renumbering of existing
// slots) per `[const §X.4]` forwards-compat.

#include <cstdint>
#include <expected>
#include <string_view>

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
    //      FIXPP_ERR_SESSION_FATAL       ← { session_seqnum_too_low }
    //                                       (slot 70 session_seqnum_gap_unrecoverable
    //                                        deleted pre-v1.0 per 013 T006a Option D;
    //                                        slot 70 is a numeric hole [const §X.4])
    //      FIXPP_ERR_SESSION_REJECT      ← { session_sending_time_accuracy,
    //                                        session_msg_type_invalid_for_state,
    //                                        session_admin_not_supported }
    //      FIXPP_ERR_SESSION_LIFECYCLE   ← { session_logout_timeout,
    //                                        session_test_request_unanswered,
    //                                        session_invalid_config }
    session_invalid_logon = 66,             // FR-003/004, US1#3/#4, [FIX-SL §4.2]/§4.3 —
                                            //   Logon refused; session never reaches Active.
                                            //   → FIXPP_ERR_SESSION_REFUSAL
    session_compid_mismatch = 67,           // FR-004, [FIX-SL §4.2.2] — SenderCompID/
                                            //   TargetCompID do not match the configured
                                            //   counterparty identity (point-to-point 49/56).
                                            //   → FIXPP_ERR_SESSION_REFUSAL
    session_begin_string_unsupported = 68,  // FR-003, [FIX-SL §4.2.1] — BeginString(8) is
                                            //   not in the supported version set (FIX.4.2/4.4
                                            //   for 005; FIXT.1.1/5.0SP2 deferred).
                                            //   → FIXPP_ERR_SESSION_REFUSAL
    session_seqnum_too_low = 69,            // FR-008, [FIX-SL §4.1] — inbound MsgSeqNum <
                                            //   next-expected, no PossDupFlag=Y: session-fatal
                                            //   (ordered-sequence integrity). PossDup handling
                                            //   is deferred (S-010); treated as the no-PossDup
                                            //   case in 005. → FIXPP_ERR_SESSION_FATAL
    // slot 70: session_seqnum_gap_unrecoverable — DELETED pre-v1.0 per 013
    //   T006a Option D (2026-05-28). Too-high inbound seqnum transitions are
    //   handled by the 013 recovery sub-protocol (FR-009 ResendRequest path);
    //   emit-sites migrated to FR-009 AwaitingResend path per 013 Phase 3
    //   T023–T026 (discharged).
    //   Slot 70 is a NUMERIC HOLE per [const §X.4] — never renumber.
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
    session_logout_timeout = 73,              // FR-005 (005 admin path) + 013 FR-008
                                              //   (FSM-driven graceful Logout timeout) —
                                              //   [FIX-SL §4.6.2] graceful-close timed out:
                                              //   phase-1 child cancellation_state expires;
                                              //   session force-disconnects → Disconnected
                                              //   (I-9, D-8: 2 s QuickFIX LogoutTimeout
                                              //   default; 013 Q5=A confirmation).
                                              //   → FIXPP_ERR_SESSION_LIFECYCLE
    session_test_request_unanswered = 74,     // FR-006 (005 admin path) + 013 FR-004
                                              //   (FSM-driven inbound liveness watch) —
                                              //   [FIX-SL §4.5.2]/§4.5.5 — inbound silence
                                              //   exceeded test_request_threshold (1×HeartBtInt,
                                              //   D-8) without a Heartbeat echo.
                                              //   Session unhealthy → disconnect.
                                              //   → FIXPP_ERR_SESSION_LIFECYCLE
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
    session_invalid_state_for_send =
        77,  // FR-005 (010), [FIX-SL §4.5.4] — Session::send(...) called
             //   while the FSM is not in `Active` (e.g. NotConnected,
             //   LogonSent, Disconnected). Replaces the 005-era reuse
             //   of `session_invalid_logon` at this site (semantic
             //   near-fit; the caller's state mismatch is distinct
             //   from a Logon refusal). No reject loop (I-5).
             //   → FIXPP_ERR_SESSION_REJECT

    // ── 011-tls-policy: 16 tls_* variants per [2g §6.6] lines 980-1003 +
    //    /clarify Q2 additive amendment (tls_pin_empty_at_open). Non-renumbering
    //    append at unused slots 78–93 per [const §X.4]. Design-doc table order.
    //
    //    C-ABI prefix-group coalescing (documented for 2i; no extern "C"
    //    surface added by this feature — data-model.md E-15):
    //      FIXPP_ERR_TLS_CONFIG     ← { tls_cert_load_failed,
    //                                   tls_cert_parse_failed,
    //                                   tls_cipher_not_allowed,
    //                                   tls_invalid_security_profile,
    //                                   tls_sign_callback_unavailable,
    //                                   tls_pin_empty_at_open }
    //      FIXPP_ERR_TLS_PINSET    ← { tls_pin_not_found,
    //                                   tls_pin_already_present,
    //                                   tls_pinset_capacity_exhausted }
    //      FIXPP_ERR_TLS_RUNTIME   ← { tls_pinset_alloc_failed }
    //      FIXPP_ERR_TLS_HANDSHAKE ← { tls_handshake_failed (GROUPING),
    //                                   tls_rsa_key_too_large,
    //                                   tls_cert_der_too_large,
    //                                   tls_san_entries_exceeded,
    //                                   tls_pin_mismatch }
    //      FIXPP_ERR_CANCELLED     ← { tls_load_cancelled }  (reused; joins
    //                                   dispatch_aborted / clock_sleeps_cancelled
    //                                   / store_cancelled).
    tls_cert_load_failed = 78,           // [2g §4.2] — file_cert_source could not
                                         //   read a cert file at construction; OR
                                         //   local cert exceeds DoS caps at load;
                                         //   OR make_file_cert_source returning the
                                         //   same condition through expected_t<...>.
                                         //   → FIXPP_ERR_TLS_CONFIG
    tls_cert_parse_failed = 79,          // [2g §4.2] / [2g §4.5] — PEM/DER parse
                                         //   failed (malformed envelope, unexpected
                                         //   ASN.1 shape, unsupported encoding).
                                         //   → FIXPP_ERR_TLS_CONFIG
    tls_cipher_not_allowed = 80,         // [2g §4.4] / [2g §4.5] / [2g §6.1] —
                                         //   runtime config attempted to enable a
                                         //   cipher not on the four CipherPolicy
                                         //   allow-lists; the compile-time
                                         //   static_assert chain catches at build,
                                         //   this covers the C-ABI / config-file
                                         //   path through CipherPolicy::is_allowed.
                                         //   → FIXPP_ERR_TLS_CONFIG
    tls_invalid_security_profile = 81,   // [2g §4.5] — SecurityProfile::unset reached
                                         //   make_ssl_ctx_config; OR mtls_pinned with
                                         //   null Pinset; OR one_way_ca with non-null
                                         //   Pinset; OR any profile with null clock.
                                         //   → FIXPP_ERR_TLS_CONFIG
    tls_sign_callback_unavailable = 82,  // [2g §4.1] — impl returned local_credentials
                                         //   whose signer variant carried a
                                         //   software_key_ref with a null handle AND
                                         //   the awaitable signer path was also empty.
                                         //   Preserved for the C-ABI path where the
                                         //   discriminator may carry an unset value.
                                         //   → FIXPP_ERR_TLS_CONFIG
    tls_pin_empty_at_open = 83,          // [2g §4.5] / /clarify Q2 amendment —
                                         //   make_ssl_ctx_config(mtls_pinned, ...,
                                         //   empty_pinset, ...) returns this distinct
                                         //   variant. Surfaces at session-open
                                         //   (config-time), NOT per-handshake; distinct
                                         //   from tls_pin_mismatch so operator logs
                                         //   separate config problem from peer-cert
                                         //   problem.
                                         //   → FIXPP_ERR_TLS_CONFIG
    tls_pin_not_found = 84,              // [2g §4.3] — Pinset::remove(fp) with a
                                         //   fingerprint not in the set.
                                         //   → FIXPP_ERR_TLS_PINSET
    tls_pin_already_present = 85,        // [2g §4.3] — Pinset::add(p) with a pin
                                         //   already in the set (by SHA-256).
                                         //   → FIXPP_ERR_TLS_PINSET
    tls_pinset_capacity_exhausted = 86,  // [2g §4.3] / [2g §1.1] — Pinset::add
                                         //   with size() == max_pins.
                                         //   → FIXPP_ERR_TLS_PINSET
    tls_pinset_alloc_failed = 87,        // [2g §4.3] — PMR allocation throw on
                                         //   snapshot clone routed through
                                         //   [2a §4.2] trap_throw.
                                         //   → FIXPP_ERR_TLS_RUNTIME
    tls_handshake_failed = 88,           // [2g §6.6] line 995 — GROUPING variant;
                                         //   the C-ABI coalescing scheme depends on
                                         //   this. The diagnostic field carries the
                                         //   specific sub-reason ("expired",
                                         //   "not_yet_valid", "rsa_under_min",
                                         //   "sigalg_disallowed", "ecdsa_curve",
                                         //   "chain_too_deep", "x509_v1", etc.).
                                         //   verify_peer returns this variant on any
                                         //   non-DoS-cap, non-pinning rejection.
                                         //   → FIXPP_ERR_TLS_HANDSHAKE
    tls_rsa_key_too_large = 89,          // [2g §6.6] — DoS bound; RSA key bits
                                         //   exceed Config::max_rsa_key_bits. Sub-
                                         //   reason "rsa_over_max".
                                         //   → FIXPP_ERR_TLS_HANDSHAKE
    tls_cert_der_too_large = 90,         // [2g §6.6] — DoS bound; DER envelope
                                         //   exceeds Config::max_cert_der_bytes.
                                         //   → FIXPP_ERR_TLS_HANDSHAKE
    tls_san_entries_exceeded = 91,       // [2g §6.6] — DoS bound; san_dns_names
                                         //   + san_uris > Config::max_san_entries.
                                         //   → FIXPP_ERR_TLS_HANDSHAKE
    tls_pin_mismatch = 92,               // [2g §6.6] — peer cert SHA-256 not in the
                                         //   captured handshake-time Pinset snapshot
                                         //   under SecurityProfile::mtls_pinned.
                                         //   Uses cfg.pinset_snapshot per
                                         //   [2g §6.5.1] BINDING CONTRACT.
                                         //   → FIXPP_ERR_TLS_HANDSHAKE
    tls_load_cancelled = 93,             // [2g §4.1] / [2g §6.4] —
                                         //   cert_source::load_credentials' awaitable
                                         //   was cancelled; OR async_signer_ref::sign
                                         //   was cancelled.
                                         //   → FIXPP_ERR_CANCELLED (reused)

    // ── 012-2h-transport: 22 transport_* variants per [2h §6.6]:1167-1204 +
    //    data-model E-13. Non-renumbering append at unused slots 94–115, next
    //    contiguous range after 011's 78–93 `tls_*` block per [const §X.4].
    //    Design-doc table order. Boundary confirmed: tls_load_cancelled=93 is
    //    the 011 boundary; 012 starts at 94.
    //
    //    C-ABI prefix-group coalescing (documented for 2i; no extern "C"
    //    surface added by this feature — data-model E-13):
    //      FIXPP_ERR_TRANSPORT_LIFECYCLE ← { transport_resolve_failed,
    //                                        transport_connect_refused,
    //                                        transport_connect_timeout,
    //                                        transport_already_connected,
    //                                        transport_already_closed,
    //                                        transport_read_in_progress,
    //                                        transport_write_in_progress,
    //                                        transport_reconnect_limit_exceeded }
    //      FIXPP_ERR_TRANSPORT_IO        ← { transport_read_eof,
    //                                        transport_read_truncated,
    //                                        transport_read_error,
    //                                        transport_write_short,
    //                                        transport_write_error }
    //      FIXPP_ERR_TRANSPORT_HANDSHAKE ← { transport_handshake_failed,
    //                                        transport_handshake_timeout }
    //      FIXPP_ERR_TRANSPORT_CONFIG    ← { transport_factory_failed,
    //                                        transport_psk_unsupported }
    //      FIXPP_ERR_CANCELLED           ← { transport_connect_cancelled,
    //                                        transport_read_cancelled,
    //                                        transport_write_cancelled,
    //                                        transport_handshake_cancelled,
    //                                        transport_accept_cancelled }
    //                                        (reused; joins tls_load_cancelled /
    //                                         store_cancelled / dispatch_aborted /
    //                                         clock_sleeps_cancelled).
    //
    //    FR-034a: transport_handshake_failed is a GROUPING variant (joins
    //    FIXPP_ERR_TLS_HANDSHAKE at C ABI); the diagnostic field carries the
    //    OpenSSL error string + [2g §6.6] tls_* sub-reason on verify_peer
    //    rejection. FR-034: 15 tls_* variants from 011 surface UNCHANGED.

    // ── LIFECYCLE (8 variants, slots 94–101) ─────────────────────────────────
    transport_resolve_failed = 94,             // [2h §6.6]:1170 — DNS / getaddrinfo
                                               //   resolution of Endpoint::host failed;
                                               //   no TCP connection attempted.
                                               //   → FIXPP_ERR_TRANSPORT_LIFECYCLE
    transport_connect_refused = 95,            // [2h §6.6]:1171 — TCP connect() returned
                                               //   ECONNREFUSED / WSAECONNREFUSED; peer
                                               //   port not listening.
                                               //   → FIXPP_ERR_TRANSPORT_LIFECYCLE
    transport_connect_timeout = 96,            // [2h §6.6]:1172 — connect_timeout
                                               //   (Config::connect_timeout, default 30 s)
                                               //   elapsed before TCP SYN-ACK received.
                                               //   → FIXPP_ERR_TRANSPORT_LIFECYCLE
    transport_already_connected = 97,          // [2h §6.6]:1173 — async_connect or
                                               //   async_handshake called a second time on
                                               //   a still-OPEN Transport (one-shot per
                                               //   [2h §4.1]). If it is closed → 98 (#339).
                                               //   → FIXPP_ERR_TRANSPORT_LIFECYCLE
    transport_already_closed = 98,             // [2h §6.6]:1174 — cancel() or any async_*
                                               //   once state == closed. close() is not the
                                               //   only door: a failed TLS handshake also
                                               //   lands there (#339). 2nd close() → {}.
                                               //   → FIXPP_ERR_TRANSPORT_LIFECYCLE
    transport_read_in_progress = 99,           // [2h §6.6]:1175 — concurrent second
                                               //   async_read_some while a first is
                                               //   in-flight; returns IMMEDIATELY per
                                               //   [2h §4.1] RC#3 close / spec FR-007.
                                               //   → FIXPP_ERR_TRANSPORT_LIFECYCLE
    transport_write_in_progress = 100,         // [2h §6.6]:1176 — concurrent second
                                               //   async_write while a first is in-flight;
                                               //   returns IMMEDIATELY per [2h §4.1] RC#3
                                               //   close / spec FR-007.
                                               //   → FIXPP_ERR_TRANSPORT_LIFECYCLE
    transport_reconnect_limit_exceeded = 101,  // [2h §6.6]:1177 — ReconnectPolicy
                                               //   max_attempts exhausted (US2); FSM
                                               //   transitions to Disconnected-terminal.
                                               //   → FIXPP_ERR_TRANSPORT_LIFECYCLE

    // ── IO (5 variants, slots 102–106) ───────────────────────────────────────
    transport_read_eof = 102,        // [2h §6.6]:1180 — async_read_some got
                                     //   EOF (peer closed write side / TCP FIN).
                                     //   → FIXPP_ERR_TRANSPORT_IO
    transport_read_truncated = 103,  // [2h §6.6]:1181 — partial TLS
                                     //   close-notify received (peer dropped
                                     //   without clean bidi shutdown); logged
                                     //   at warn per [2g §7.8], non-fatal.
                                     //   → FIXPP_ERR_TRANSPORT_IO
    transport_read_error = 104,      // [2h §6.6]:1182 — OS-level read error
                                     //   other than EOF (ECONNRESET, ETIMEDOUT,
                                     //   TLS decrypt failure etc.).
                                     //   → FIXPP_ERR_TRANSPORT_IO
    transport_write_short = 105,     // [2h §6.6]:1183 — composed write completed
                                     //   fewer bytes than requested (torn write);
                                     //   FSM disconnects + recovers via
                                     //   ResendRequest per [FIX-SL §4.5.2].
                                     //   Persisted frame NOT rolled back per
                                     //   [2e §6.1.4].
                                     //   → FIXPP_ERR_TRANSPORT_IO
    transport_write_error = 106,     // [2h §6.6]:1184 — OS-level write error
                                     //   (EPIPE, ECONNRESET, TLS encrypt failure).
                                     //   → FIXPP_ERR_TRANSPORT_IO

    // ── HANDSHAKE (2 variants, slots 107–108) ─────────────────────────────────
    transport_handshake_failed = 107,   // [2h §6.6]:1187 — GROUPING variant;
                                        //   TLS handshake rejected by peer cert
                                        //   validation, cipher mismatch, etc.
                                        //   Diagnostic field carries OpenSSL error
                                        //   string + [2g §6.6] tls_* sub-reason
                                        //   on verify_peer rejection. Joins
                                        //   FIXPP_ERR_TLS_HANDSHAKE at C ABI.
                                        //   FR-034a + FR-034.
                                        //   → FIXPP_ERR_TRANSPORT_HANDSHAKE
    transport_handshake_timeout = 108,  // [2h §6.6]:1188 — Config::tls_handshake_
                                        //   timeout (default 30 s) elapsed before
                                        //   OpenSSL handshake completed; SSL* state
                                        //   broken → caller MUST close() per
                                        //   [2h §6.4].
                                        //   → FIXPP_ERR_TRANSPORT_HANDSHAKE

    // ── CONFIG (2 variants, slots 109–110) ────────────────────────────────────
    transport_factory_failed = 109,   // [2h §6.6]:1191 — TransportFactory::make()
                                      //   could not construct (OS socket resource
                                      //   exhaustion, SSL_CTX_new failure, PMR
                                      //   throw routed via [2a §4.2] trap_throw).
                                      //   → FIXPP_ERR_TRANSPORT_CONFIG
    transport_psk_unsupported = 110,  // [2h §6.6]:1192 — caller requested a
                                      //   PSK-mode TLS session; v1.0 does NOT
                                      //   implement PSK (deferred per [const §XII.6]
                                      //   / T-012 P2). Returns this variant at
                                      //   handshake time.
                                      //   → FIXPP_ERR_TRANSPORT_CONFIG

    // ── CANCELLED-reuse (5 variants, slots 111–115) ───────────────────────────
    transport_connect_cancelled = 111,    // [2h §6.6]:1195 — async_connect awaitable
                                          //   cancelled via cancellation_type::total.
                                          //   → FIXPP_ERR_CANCELLED (reused)
    transport_read_cancelled = 112,       // [2h §6.6]:1196 — async_read_some awaitable
                                          //   cancelled; partial bytes LOST per ASIO
                                          //   contract.
                                          //   → FIXPP_ERR_CANCELLED (reused)
    transport_write_cancelled = 113,      // [2h §6.6]:1197 — async_write awaitable
                                          //   cancelled mid-flight. Persisted frame NOT
                                          //   rolled back per [2e §6.1.4].
                                          //   → FIXPP_ERR_CANCELLED (reused)
    transport_handshake_cancelled = 114,  // [2h §6.6]:1198 — async_handshake awaitable
                                          //   cancelled; SSL* state broken → caller
                                          //   MUST close() per [2h §6.4].
                                          //   → FIXPP_ERR_CANCELLED (reused)
    transport_accept_cancelled = 115,     // [2h §6.6]:1199 — async_accept awaitable
                                          //   on Listener cancelled (US3 acceptor path).
                                          //   → FIXPP_ERR_CANCELLED (reused)

    // ── 013-session-reconnect-binding: 4 session_* variants per
    //    contracts/session_errors.hpp + data-model.md §E-1/§E-3/§E-7.
    //    Non-renumbering append at unused slots 116–119, next contiguous range
    //    after 012's transport_* block (95–115) per [const §X.4].
    //    Boundary confirmed: transport_accept_cancelled=115 is the 012 boundary;
    //    013 starts at 116.
    //
    //    C-ABI prefix-group coalescing (documented for 2i; no extern "C"
    //    surface added by this feature):
    //      FIXPP_ERR_SESSION_FATAL       ← { session_seqnum_reset_mismatch }
    //      FIXPP_ERR_SESSION_REFUSAL     ← { session_compid_unauthorized }
    //      FIXPP_ERR_SESSION_REJECT      ← { session_testreqid_mismatch,
    //                                        session_invalid_argument }
    //      (slots 73 + 74 already in FIXPP_ERR_SESSION_LIFECYCLE — no new C-ABI symbol)
    //
    //    Slots 73 (session_logout_timeout) + 74 (session_test_request_unanswered)
    //    REUSED from 005-era per contracts/session_errors.hpp F1/D1 2026-05-28
    //    reference-engine sweep (QFC + QFJ + Fix8 confirmed zero precedent for
    //    typed code-level discrimination of logout timeout classes).
    session_seqnum_reset_mismatch = 116,  // FR-017 / US1 AC7 / D-7 — bilateral_strict:
                                          //   we sent Logon with ResetSeqNumFlag(141)=Y;
                                          //   peer's response Logon lacks 141=Y; we Logout
                                          //   + disconnect.
                                          //   → FIXPP_ERR_SESSION_FATAL
    session_compid_unauthorized = 117,    // FR-021 / US2 AC2 / D-9 —
                                          //   CompIdAuthorizationPolicy::authorize returned
                                          //   unauthorized: principal not in bindings, OR
                                          //   principal exists but asserted CompID not in
                                          //   its allow-list, OR policy is empty (default-deny).
                                          //   → FIXPP_ERR_SESSION_REFUSAL
    session_testreqid_mismatch = 118,     // FR-006 / US1 AC4 — inbound Heartbeat(0) carried
                                          //   TestReqID(112) that does NOT match the most
                                          //   recent outbound TestRequest's TestReqID; per
                                          //   [FIX-SL §4.5.4] this is a session-level error.
                                          //   → FIXPP_ERR_SESSION_REJECT
    session_invalid_argument = 119,       // FR-033 — runtime rejection of an invalid argument
                                          //   to a public API entry point (currently:
                                          //   reload_credentials(nullptr)). Returned via
                                          //   expected_t::unexpected from noexcept boundaries
                                          //   per data-model.md §E-6/§E-7. Distinct from
                                          //   session_invalid_config (slot 76, operator
                                          //   config-time) and session_invalid_state_for_send
                                          //   (slot 77, FSM-state-wrong-for-operation).
                                          //   (Renumbered 121→119 per /speckit-analyze F1/D1
                                          //   2026-05-28.)
                                          //   → FIXPP_ERR_SESSION_REJECT

    // ── 014-transport-active-binding: 1 session_* variant per
    //    contracts/error_slots.hpp + data-model.md §E-4.
    //    Non-renumbering append at unused slot 120, next contiguous slot
    //    after 013's session_* block (116–119) per [const §X.4].
    //    Boundary confirmed: session_invalid_argument=119 is the 013 boundary;
    //    014 starts at 120. Slot 70 remains a PERMANENT NUMERIC HOLE
    //    (session_seqnum_gap_unrecoverable, deleted per 013 T006a Option D;
    //    never renumber). Slot 74 (session_test_request_unanswered) keeps its
    //    real meaning (FR-004 inbound liveness window) — 014 stops misusing it
    //    as the seqnum-too-high stand-in (see seqnum_manager.cpp).
    //
    //    C-ABI prefix-group coalescing (documented for 2i; no extern "C"
    //    surface added by this feature — contracts/error_slots.hpp):
    //      FIXPP_ERR_SESSION_FATAL ← { session_seqnum_too_high } (new member)
    session_seqnum_too_high = 120,  // FR-016 / E-4 — inbound seqnum > next_expected
                                    //   in LogonSent / LogonReceived states where
                                    //   too-high is session-fatal (Active-state
                                    //   too-high is intercepted earlier →
                                    //   AwaitingResend per 013 FR-009). Replaces
                                    //   the slot-74 stand-in
                                    //   (session_test_request_unanswered) that was
                                    //   returned by SeqnumManager::check_inbound's
                                    //   too-high branch as a semantic misnomer.
                                    //   Zero behavioural change — all 3 production
                                    //   callers discard the code (session.cpp).
                                    //   → FIXPP_ERR_SESSION_FATAL

    // ── 015-runtime-engine: 1 session_* variant per data-model.md §E-7.
    //    Non-renumbering append at unused slot 121, next contiguous slot
    //    after 014's session_seqnum_too_high = 120 per [const §X.4].
    //    Slot 70 remains a PERMANENT NUMERIC HOLE; never renumber.
    //    This code is the connection-level disposition reason — it is never set on
    //    a Session event (no Session is created for unmatched inbound Logons).
    session_unknown_acceptor_session = 121,  // FR-005/006 / C7 — inbound Logon from a
                                             //   peer whose reversed CompID
                                             //   (SessionId::reversed_from_logon) does not
                                             //   match any registered acceptor session in
                                             //   the Engine registry. The accept loop
                                             //   closes the transport (no Session is
                                             //   created). The code names the disposition;
                                             //   surfacing it on a log/observability sink is
                                             //   deferred — 015 has no log surface (FR-013).
                                             //   Static matching only in 015 (R2).
                                             //   → FIXPP_ERR_SESSION_REJECT

    // ── 017-log-otel: 7 log_*/otel_* variants per contracts/error-block.md +
    //    [2k §6.3]. Non-renumbering append at unused slots 122–128, next
    //    contiguous range after 015's session_unknown_acceptor_session = 121
    //    per [const §X.4]. The [1000,1099] C-ABI block is a FUTURE v1.x
    //    fixpp_error_t mapping reservation (see tools/abi_history/
    //    error_codes_v1.txt); the enum is std::uint8_t-backed so [1000,1099]
    //    are NOT enumerator values here.
    //
    //    C-ABI fixpp_error_t block [1000,1099] (documented for 2i; no extern "C"
    //    surface added by this feature — FR-020):
    //      FIXPP_ERR_LOG_QUEUE_OVERFLOW   ← 1000 (internal-only; maps to
    //                                           Logger::drop_count() counter)
    //      FIXPP_ERR_LOG_SINK_OPEN        ← 1001
    //      FIXPP_ERR_LOG_SINK_WRITE       ← 1002
    //      FIXPP_ERR_LOG_SINK_FLUSH       ← 1003
    //      FIXPP_ERR_LOG_DRAIN_TIMEOUT    ← 1004
    //      (1005–1009 reserved for future log variants)
    //      FIXPP_ERR_OTEL_EXPORT_FAILED   ← 1010
    //      FIXPP_ERR_OTEL_PROVIDER_INIT   ← 1011
    //      (1012–1099 reserved for future otel variants)
    log_queue_overflow = 122,         // [2k §6.3] — MPSC ring overflowed; record
                                      //   dropped (drop_newest mode). Increments
                                      //   Logger::drop_count() atomically. NOT
                                      //   returned to callers (macros are void).
                                      //   → C-ABI 1000 (future mapping; no return path in v1.0)
    log_sink_open_failed = 123,       // [2k §6.3] — Sink::open() returned an error
                                      //   at Logger startup; sink disabled; Logger
                                      //   continues with remaining sinks.
                                      //   → C-ABI 1001
    log_sink_write_failed = 124,      // [2k §6.3] — Sink::emit() threw (caught by
                                      //   drain); increments sink_error_count(i).
                                      //   → C-ABI 1002
    log_sink_flush_failed = 125,      // [2k §6.3] — Sink::flush() threw (caught
                                      //   by drain).
                                      //   → C-ABI 1003
    log_drain_timeout = 126,          // [2k §6.3] — Logger::shutdown(drain_timeout)
                                      //   timed out; returned via expected_t<void>;
                                      //   increments timeout_drop_count() (NOT
                                      //   drop_count() — separate counters).
                                      //   → C-ABI 1004
    otel_export_failed = 127,         // [2k §6.3] — OTLP trace/metric/log batch
                                      //   export failure; internal counter; engine
                                      //   continues.
                                      //   → C-ABI 1010
    otel_provider_init_failed = 128,  // [2k §6.3] — TracerProvider/MeterProvider
                                      //   construction failed; returned via
                                      //   expected_t<void>; no-op provider
                                      //   substituted.
                                      //   → C-ABI 1011

    // ── 019-app-callbacks: 2 app_* variants per specs/019-app-callbacks/
    //    {research.md D7, data-model.md §"New error:: enumerators"}.
    //    Non-renumbering append at unused slots 129–130, next contiguous range
    //    after 017's otel_provider_init_failed = 128 per [const §X.4].
    //    Slot 70 remains a PERMANENT NUMERIC HOLE; never renumber.
    app_do_not_send = 129,     // FR-007 — toApp veto sentinel: the Application
                               //   returned unexpected(app_do_not_send) from
                               //   toApp(), signalling DoNotSend. The Engine
                               //   does NOT transmit the outbound message and
                               //   surfaces this code as the Engine::send()
                               //   awaitable result to the caller (INV-5).
                               //   → C-ABI reserved (future mapping)
    app_callback_threw = 130,  // FR-011 — session terminated by a throwing
                               //   callback: the Application threw an exception
                               //   from any callback site (fromApp/fromAdmin/
                               //   toApp/toAdmin/onCreate/onLogon/onLogout).
                               //   The engine caught at the dispatch boundary,
                               //   logged, terminal-closed the session, and
                               //   recorded this code. Never propagated inward.
                               //   → C-ABI reserved (future mapping)
    // ── 020-g2-business-messages (slot 131) ───────────────────────────────
    //    Append-only at the next contiguous slot after 019's 130 per
    //    [const §X.4]; the exact-SET completeness of this boundary (131
    //    present, no ±N drift, no unexpected) is asserted at T020 per
    //    [[feedback_completeness_gate_exact_set_not_subset]].
    app_payload_malformed = 131,  // FR-016 — the opaque application payload
                                  //   handed to Engine::send failed fail-closed
                                  //   validation BEFORE seqnum assignment: it was
                                  //   empty, did not begin with exactly one 35=
                                  //   MsgType field, carried a duplicate 35=, or
                                  //   embedded a session header/trailer tag
                                  //   (8/9/34/49/52/56/10). No transmit, no
                                  //   seqnum consumption (INV-8).
                                  //   → C-ABI reserved (future mapping)
};

// ── 035-filestore-io-offload: FR-021 store_* error-set freeze guard ──────────
// The store_* set is EXACTLY 10 contiguous variants: store_io_failure(56) …
// store_cancelled(65). An accidental 11th variant (inserted or appended) that
// shifts either endpoint will break this assertion.  See data-model §5.
static_assert(static_cast<int>(error::store_io_failure) == 56 &&
                  static_cast<int>(error::store_cancelled) == 65,
              "FR-021: store_* error set frozen at 10 variants (56–65); "
              "see 035-filestore-io-offload data-model §5");

template <class T>
using expected_t = std::expected<T, error>;

// error_message — returns a short human-readable description of the error
// variant. Covers the full enum surface including the 017-log-otel additions
// (slots 122–128). Returns a non-empty string_view for every defined variant.
// For unknown/future enumerators, returns "unknown error".
// Anchor: FR-015 / contracts/error-block.md (017-log-otel).
[[nodiscard]] constexpr std::string_view error_message(error e) noexcept {
    switch (e) {
        case error::out_of_memory:
            return "out of memory";
        case error::decimal_invalid_input:
            return "decimal: invalid input";
        case error::decimal_overflow:
            return "decimal: overflow";
        case error::decimal_precision_loss:
            return "decimal: precision loss";
        case error::decimal_buffer_too_small:
            return "decimal: buffer too small";
        case error::dict_xml_parse_failed:
            return "dict: XML parse failed";
        case error::dict_unknown_version:
            return "dict: unknown version";
        case error::dict_xml_oom:
            return "dict: XML out of memory";
        case error::dict_reify_msg_type_mismatch:
            return "dict: reify message type mismatch";
        case error::dict_reify_unknown_msg_type:
            return "dict: reify unknown message type";
        case error::dict_reify_oom:
            return "dict: reify out of memory";
        case error::dict_unresolved_application_version:
            return "dict: unresolved application version";
        case error::dict_unknown_appl_ver_id:
            return "dict: unknown ApplVerID";
        case error::dict_no_dictionary_for_application_version:
            return "dict: no dictionary for application version";
        case error::dict_reify_wire_body_not_ready:
            return "dict: reify wire body not ready";
        case error::wire_frame_too_large:
            return "wire: frame too large";
        case error::wire_invalid_body_length:
            return "wire: invalid body length";
        case error::wire_checksum_mismatch:
            return "wire: checksum mismatch";
        case error::wire_framing_resync:
            return "wire: framing resync";
        case error::wire_invalid_field_format:
            return "wire: invalid field format";
        case error::wire_offset_table_full:
            return "wire: offset table full";
        case error::wire_group_too_large:
            return "wire: group too large";
        case error::wire_tag_out_of_range:
            return "wire: tag out of range";
        case error::wire_required_field_missing:
            return "wire: required field missing";
        case error::wire_header_out_of_order:
            return "wire: header out of order";
        case error::wire_field_value_out_of_range:
            return "wire: field value out of range";
        case error::wire_field_value_truncated:
            return "wire: field value truncated";
        case error::wire_unexpected_tag:
            return "wire: unexpected tag";
        case error::sync_lock_aborted:
            return "sync: lock aborted";
        case error::sync_lock_alloc_failed:
            return "sync: lock alloc failed";
        case error::sync_lock_outside_session:
            return "sync: lock outside session";
        case error::sync_lock_drained:
            return "sync: lock drained";
        case error::executor_already_stopped:
            return "executor already stopped";
        case error::executor_not_serialised:
            return "executor not serialised";
        case error::clock_sleeps_cancelled:
            return "clock sleeps cancelled";
        case error::strand_dispatch_failed_oom:
            return "strand dispatch failed: out of memory";
        case error::session_already_open:
            return "session already open";
        case error::session_already_closed:
            return "session already closed";
        case error::invalid_session_config:
            return "invalid session config";
        case error::clock_not_set:
            return "clock not set";
        case error::dispatch_aborted:
            return "dispatch aborted";
        case error::store_io_failure:
            return "store: I/O failure";
        case error::store_seqnum_gap:
            return "store: seqnum gap";
        case error::store_seqnum_out_of_order:
            return "store: seqnum out of order";
        case error::store_capacity_exhausted:
            return "store: capacity exhausted";
        case error::store_seqnum_overflow:
            return "store: seqnum overflow";
        case error::store_factory_failed:
            return "store: factory failed";
        case error::store_visitor_aborted:
            return "store: visitor aborted";
        case error::store_seqnum_invalid:
            return "store: seqnum invalid";
        case error::store_invalid_range:
            return "store: invalid range";
        case error::store_cancelled:
            return "store: cancelled";
        case error::session_invalid_logon:
            return "session: invalid logon";
        case error::session_compid_mismatch:
            return "session: CompID mismatch";
        case error::session_begin_string_unsupported:
            return "session: BeginString unsupported";
        case error::session_seqnum_too_low:
            return "session: seqnum too low";
        case error::session_sending_time_accuracy:
            return "session: sending time accuracy";
        case error::session_msg_type_invalid_for_state:
            return "session: message type invalid for state";
        case error::session_logout_timeout:
            return "session: logout timeout";
        case error::session_test_request_unanswered:
            return "session: test request unanswered";
        case error::session_admin_not_supported:
            return "session: admin not supported";
        case error::session_invalid_config:
            return "session: invalid config";
        case error::session_invalid_state_for_send:
            return "session: invalid state for send";
        case error::tls_cert_load_failed:
            return "TLS: cert load failed";
        case error::tls_cert_parse_failed:
            return "TLS: cert parse failed";
        case error::tls_cipher_not_allowed:
            return "TLS: cipher not allowed";
        case error::tls_invalid_security_profile:
            return "TLS: invalid security profile";
        case error::tls_sign_callback_unavailable:
            return "TLS: sign callback unavailable";
        case error::tls_pin_empty_at_open:
            return "TLS: pin set empty at open";
        case error::tls_pin_not_found:
            return "TLS: pin not found";
        case error::tls_pin_already_present:
            return "TLS: pin already present";
        case error::tls_pinset_capacity_exhausted:
            return "TLS: pinset capacity exhausted";
        case error::tls_pinset_alloc_failed:
            return "TLS: pinset alloc failed";
        case error::tls_handshake_failed:
            return "TLS: handshake failed";
        case error::tls_rsa_key_too_large:
            return "TLS: RSA key too large";
        case error::tls_cert_der_too_large:
            return "TLS: cert DER too large";
        case error::tls_san_entries_exceeded:
            return "TLS: SAN entries exceeded";
        case error::tls_pin_mismatch:
            return "TLS: pin mismatch";
        case error::tls_load_cancelled:
            return "TLS: load cancelled";
        case error::transport_resolve_failed:
            return "transport: resolve failed";
        case error::transport_connect_refused:
            return "transport: connect refused";
        case error::transport_connect_timeout:
            return "transport: connect timeout";
        case error::transport_already_connected:
            return "transport: already connected";
        case error::transport_already_closed:
            return "transport: already closed";
        case error::transport_read_in_progress:
            return "transport: read in progress";
        case error::transport_write_in_progress:
            return "transport: write in progress";
        case error::transport_reconnect_limit_exceeded:
            return "transport: reconnect limit exceeded";
        case error::transport_read_eof:
            return "transport: read EOF";
        case error::transport_read_truncated:
            return "transport: read truncated";
        case error::transport_read_error:
            return "transport: read error";
        case error::transport_write_short:
            return "transport: write short";
        case error::transport_write_error:
            return "transport: write error";
        case error::transport_handshake_failed:
            return "transport: handshake failed";
        case error::transport_handshake_timeout:
            return "transport: handshake timeout";
        case error::transport_factory_failed:
            return "transport: factory failed";
        case error::transport_psk_unsupported:
            return "transport: PSK unsupported";
        case error::transport_connect_cancelled:
            return "transport: connect cancelled";
        case error::transport_read_cancelled:
            return "transport: read cancelled";
        case error::transport_write_cancelled:
            return "transport: write cancelled";
        case error::transport_handshake_cancelled:
            return "transport: handshake cancelled";
        case error::transport_accept_cancelled:
            return "transport: accept cancelled";
        case error::session_seqnum_reset_mismatch:
            return "session: seqnum reset mismatch";
        case error::session_compid_unauthorized:
            return "session: CompID unauthorized";
        case error::session_testreqid_mismatch:
            return "session: TestReqID mismatch";
        case error::session_invalid_argument:
            return "session: invalid argument";
        case error::session_seqnum_too_high:
            return "session: seqnum too high";
        case error::session_unknown_acceptor_session:
            return "session: unknown acceptor session";
        // ── 017-log-otel (slots 122–128) ──────────────────────────────────
        case error::log_queue_overflow:
            return "log: queue overflow";
        case error::log_sink_open_failed:
            return "log: sink open failed";
        case error::log_sink_write_failed:
            return "log: sink write failed";
        case error::log_sink_flush_failed:
            return "log: sink flush failed";
        case error::log_drain_timeout:
            return "log: drain timeout";
        case error::otel_export_failed:
            return "otel: export failed";
        case error::otel_provider_init_failed:
            return "otel: provider init failed";
        // ── 019-app-callbacks (slots 129–130) ─────────────────────────────
        case error::app_do_not_send:
            return "app: do not send (toApp veto)";
        case error::app_callback_threw:
            return "app: callback threw (session terminated)";
        // ── 020-g2-business-messages (slot 131) ───────────────────────────
        case error::app_payload_malformed:
            return "app: malformed opaque payload (rejected pre-seqnum)";
    }
    return "unknown error";
}

// to_string — convenience alias returning the same string_view as error_message.
// Anchor: FR-015 / contracts/error-block.md (017-log-otel).
[[nodiscard]] constexpr std::string_view to_string(error e) noexcept { return error_message(e); }

}  // namespace fixpp::core
