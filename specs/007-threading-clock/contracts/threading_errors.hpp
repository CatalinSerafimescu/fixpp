// SPDX-License-Identifier: AGPL-3.0-or-later
//
// specs/007-threading-clock/contracts/threading_errors.hpp
//
// ─────────────────────────────────────────────────────────────────────────────
// SHAPE ORACLE (NOT THE BUILD HEADER)
// This file is a planning-phase contract artifact.
// It specifies the exact names and slot assignments that the implementation
// MUST match. It is NOT compiled into the library.
//
// Build header target: include/fixpp/core/error.hpp
//   (additive extension — append slots 47–55 per [const §X.4] non-renumbering)
// ─────────────────────────────────────────────────────────────────────────────
//
// Source of truth: .specify/2d-threading.md v0.4 §6.7
// Style precedent: specs/006-async-mutex/contracts/sync_errors.hpp (the bundle
// invokes this precedent at research D-2 / plan.md — sync_* 43–46). Per
// [2d §6.7] the variants are presented as a fixpp::core::error / "session"
// table, NOT a distinct enum type; and research D-2 fixes "no separate
// fixpp::session::error enum is introduced; the project keeps a single
// core::error". This oracle therefore documents ONLY the additive append —
// it deliberately declares NO new enum type (an implementer copying a
// standalone enum would fork the slot space and break the single-error.hpp
// occupancy invariant — plan.md / D-7).
//
// 2d introduces 9 new `fixpp::core::error` variants at the first free slots
// after the sync_* block (last occupied: sync_lock_drained = 46; first free
// = 47; verified against include/fixpp/core/error.hpp on branch
// 007-threading-clock — 43–46 = merged 006 sync_*).
//
// Non-renumbering invariant: once these slots are published, they are NEVER
// renumbered per [const §X.4]. Future additions start at 56 or higher.
//
// C-ABI mapping (delegated to 2i — not assigned here; per-doc-prefix
// FIXPP_ERR_THREAD_* discipline, mirroring [2c] FIXPP_ERR_DICT_* / [2b]
// FIXPP_ERR_WIRE_* / [2f] FIXPP_ERR_SYNC_*; FINAL coalescing is 2i's call):
//   executor_already_stopped   → FIXPP_ERR_THREAD_CONFIG
//   executor_not_serialised    → FIXPP_ERR_THREAD_CONFIG
//   clock_sleeps_cancelled     → FIXPP_ERR_CANCELLED  (reused, [const §XI.2];
//                                  joins sync_lock_aborted / dispatch_aborted)
//   strand_dispatch_failed_oom → FIXPP_ERR_THREAD_RUNTIME
//   session_already_open       → FIXPP_ERR_THREAD_SESSION_LIFECYCLE
//   session_already_closed     → FIXPP_ERR_THREAD_SESSION_LIFECYCLE
//   invalid_session_config     → FIXPP_ERR_THREAD_CONFIG
//   clock_not_set              → FIXPP_ERR_THREAD_CONFIG
//   dispatch_aborted           → FIXPP_ERR_CANCELLED  (reused)
//
// NOT introduced (design-doc dropped variants — confirmed against [2d §6.7]):
//   trace_context_provider_threw      — dropped C-P2-4 (value-typed
//                                       initial_trace_context; no callable to throw)
//   cancellation_propagation_timeout  — dropped N-P2-1 (close-timeout value
//                                       lives in the session-module spec / 005)
//   version_registry_dictionary_missing — dropped Opus N2-P2-1; the FIXT.1.1
//     miss routes through the EXISTING [2c §6.7]
//     dict_no_dictionary_for_application_version (slot 28 →
//     FIXPP_ERR_DICT_CONFIG). Seam 20 enforces.
//
// ─────────────────────────────────────────────────────────────────────────────

// The nine variants are appended to the existing fixpp::core::error enum in
// include/fixpp/core/error.hpp. This oracle documents only the additive
// extension; the full enum definition lives in the build header. NO separate
// enum type is declared (D-2 — single core::error; the "session" group is the
// lifecycle subset, for C-ABI coalescing only).
//
// Addition shape (copy-paste target for the implementation):
//
//     // threading variants — owned by 007-threading-clock (data-model.md
//     // Error model; contracts/threading_errors.hpp; [2d §6.7]). Appended at
//     // unused slots 47–55, non-renumbering ([const §X.4]). C-ABI
//     // FIXPP_ERR_THREAD_* coalescing + tools/abi_history audit-trail entry
//     // deferred to 2i (same time-bounded waiver shape as 002/003/004/006;
//     // no C-ABI surface added here).
//     executor_already_stopped   = 47,  // [2d §4.4] — resolved executor
//                                        //   (executor_override.value_or(
//                                        //   EngineConfig::executor)) joined
//                                        //   before Engine::open / Session::open.
//                                        //   → FIXPP_ERR_THREAD_CONFIG
//     executor_not_serialised    = 48,  // [2d §4.5]/§6.1 — mode==direct_executor
//                                        //   without already_serialized_executor
//                                        //   (root cause #1 / C-P1-2).
//                                        //   → FIXPP_ERR_THREAD_CONFIG
//     clock_sleeps_cancelled     = 49,  // [2d §4.1]/§6.6 — a sleep_until waiter
//                                        //   completed via cancel_sleeps. Maps to
//                                        //   asio::error::operation_aborted at the
//                                        //   awaitable level; this enum value is
//                                        //   the OPTIONAL expected_t projection
//                                        //   for callers that prefer it (NOT the
//                                        //   sleep_until return type — [2d §4.1]).
//                                        //   Joins FIXPP_ERR_CANCELLED ([const §XI.2]).
//     strand_dispatch_failed_oom = 50,  // [2d §6.2]/§6.5 — PMR fallback for the
//                                        //   strand's posted handler / the
//                                        //   cancellable_dispatch node exhausted
//                                        //   the per-session arena. Forced
//                                        //   disconnect. → FIXPP_ERR_THREAD_RUNTIME
//     session_already_open       = 51,  // [2d §4.7] — Session::open() called twice
//                                        //   on the same handle (programmer error).
//                                        //   → FIXPP_ERR_THREAD_SESSION_LIFECYCLE
//     session_already_closed     = 52,  // [2d §4.7]:830-832,863 / [2d §6.5]:1172
//                                        //   — close() on a NEVER-OPENED or an
//                                        //   ALREADY-CLOSED (drained) session.
//                                        //   NOT returned for an ALREADY-CLOSING
//                                        //   (in-flight) session — that case
//                                        //   returns the SAME in-flight awaitable
//                                        //   with NO error. Idempotency contract;
//                                        //   non-fatal — caller may ignore.
//                                        //   → FIXPP_ERR_THREAD_SESSION_LIFECYCLE
//     invalid_session_config     = 53,  // [2d §4.5]/§6.1 — incompatible combo
//                                        //   (direct_executor+lock_policy::spin
//                                        //   even when attested; null
//                                        //   EngineConfig::executor; null
//                                        //   dictionary; default-constructed
//                                        //   security_profile sentinel — N-P2-3).
//                                        //   → FIXPP_ERR_THREAD_CONFIG
//     clock_not_set              = 54,  // [2d §4.4] — EngineConfig::clock is null
//                                        //   at Engine::open, regardless of
//                                        //   per-session clock_override (hardened
//                                        //   invariant per root cause #2).
//                                        //   → FIXPP_ERR_THREAD_CONFIG
//     dispatch_aborted           = 55,  // [2d §6.5] — cancellable_dispatch's slot
//                                        //   fired BEFORE the posted handler was
//                                        //   picked up; the handler was reaped
//                                        //   (not invoked). Surfaces as the
//                                        //   awaitable's expected_t<void>{ unexpect,
//                                        //   error::dispatch_aborted }. Expected on
//                                        //   the §4.7 phase-2 close path.
//                                        //   Joins FIXPP_ERR_CANCELLED (reused).

//
// ─────────────────────────────────────────────────────────────────────────────
// Slot occupancy reference (as of 007-threading-clock branch):
//   slot 1      = out_of_memory
//   slots 10–13 = decimal_invalid_input / decimal_overflow /
//                 decimal_precision_loss / decimal_buffer_too_small
//   slots 20–22 = dict_xml_parse_failed / dict_unknown_version / dict_xml_oom
//   slots 23–29 = dict_reify_msg_type_mismatch / dict_reify_unknown_msg_type /
//                 dict_reify_oom / dict_unresolved_application_version /
//                 dict_unknown_appl_ver_id /
//                 dict_no_dictionary_for_application_version /
//                 dict_reify_wire_body_not_ready
//   slots 30–42 = wire_* (13 variants — see specs/006 sync_errors.hpp)
//   slots 43–46 = sync_lock_aborted / sync_lock_alloc_failed /
//                 sync_lock_outside_session / sync_lock_drained (merged 006)
//   slots 47–55 = executor_already_stopped / executor_not_serialised /
//                 clock_sleeps_cancelled / strand_dispatch_failed_oom /
//                 session_already_open / session_already_closed /
//                 invalid_session_config / clock_not_set /
//                 dispatch_aborted  ← THIS FEATURE
// ─────────────────────────────────────────────────────────────────────────────
