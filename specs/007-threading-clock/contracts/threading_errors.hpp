// SPDX-License-Identifier: AGPL-3.0-or-later
// SHAPE ORACLE — NOT the build header. [2d §6.7] — the 9 threading error
// variants appended to fixpp::core::error at slots 47–55 (ADDITIVE,
// NON-RENUMBERING, [const §X.4]). First free on this branch = 47 (43–46 =
// merged 006 sync_*; verified). The C-ABI coalescing groups are documented
// for 2i; FINAL coalescing is 2i's call (per-doc-prefix FIXPP_ERR_THREAD_*
// discipline, mirroring [2c] FIXPP_ERR_DICT_* / [2b] FIXPP_ERR_WIRE_* /
// [2f] FIXPP_ERR_SYNC_*).
#pragma once
#include <cstdint>

namespace fixpp::core {

// Appended to the SINGLE existing core::error enum (D-2 — no separate
// fixpp::session::error enum is introduced; the "session" group is the
// lifecycle subset, for C-ABI coalescing only).
enum class error_threading_slots : std::uint8_t {  // illustrative — real values live in core::error
    executor_already_stopped   = 47,   // FIXPP_ERR_THREAD_CONFIG
    executor_not_serialised    = 48,   // FIXPP_ERR_THREAD_CONFIG          (root cause #1 / C-P1-2)
    clock_sleeps_cancelled     = 49,   // FIXPP_ERR_CANCELLED  (reused, [const §XI.2])
    strand_dispatch_failed_oom = 50,   // FIXPP_ERR_THREAD_RUNTIME
    session_already_open       = 51,   // FIXPP_ERR_THREAD_SESSION_LIFECYCLE
    session_already_closed     = 52,   // FIXPP_ERR_THREAD_SESSION_LIFECYCLE (idempotency; non-fatal)
    invalid_session_config     = 53,   // FIXPP_ERR_THREAD_CONFIG          (incl. direct_executor+spin; N-P2-3)
    clock_not_set              = 54,   // FIXPP_ERR_THREAD_CONFIG          (root cause #2 — hard @ Engine::open)
    dispatch_aborted           = 55,   // FIXPP_ERR_CANCELLED  (reused; expected on phase-2 close)
};

// NOT introduced (design-doc dropped variants — confirmed against [2d §6.7]):
//   trace_context_provider_threw      — dropped C-P2-4 (value-typed initial_trace_context)
//   cancellation_propagation_timeout  — dropped N-P2-1 (close-timeout lives in 005)
//   version_registry_dictionary_missing — dropped Opus N2-P2-1; the FIXT.1.1
//     miss routes through the EXISTING [2c §6.7] dict_no_dictionary_for_
//     application_version (slot 28 → FIXPP_ERR_DICT_CONFIG). Seam 20 enforces.

}  // namespace fixpp::core
