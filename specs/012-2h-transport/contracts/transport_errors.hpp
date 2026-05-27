// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// fixpp::transport::errors — re-export shape for the 22 error::transport_*
// variants declared at slots 94..115 of fixpp::core::error per [2h §6.6].
// Re-emitted verbatim from [2h §6.6]; the variant defs themselves land in
// include/fixpp/core/error.hpp (the single point of truth across the codebase
// per [[project_2e_design_doc_only_seqnum_handoff]] slot-pinning rule).
//
// 011 (TLS policy) took slots 78..93 per its line-71 narrative; 012 (transport)
// takes the next free contiguous block 94..115. NEVER renumber existing slots.
//
// C-ABI mapping (delegated to 2i per the per-doc-prefix discipline):
//   - LIFECYCLE: resolve / refused / timeout / already-connected /
//                already-closed / read-in-progress / write-in-progress /
//                reconnect-limit-exceeded → FIXPP_ERR_TRANSPORT_LIFECYCLE
//   - IO:        read-eof / read-truncated / read-error / write-short /
//                write-error → FIXPP_ERR_TRANSPORT_IO
//   - HANDSHAKE: handshake-failed / handshake-timeout → FIXPP_ERR_TRANSPORT_
//                HANDSHAKE (joins [2g §6.6] FIXPP_ERR_TLS_HANDSHAKE at C ABI)
//   - CONFIG:    factory-failed / psk-unsupported → FIXPP_ERR_TRANSPORT_CONFIG
//   - CANCEL:    connect/read/write/handshake/accept_cancelled → reuses
//                FIXPP_ERR_CANCELLED per [const §XI.2]
//
// The 11 `tls_*` variants from [2g §6.6] surface UNCHANGED through this layer
// per spec FR-034 — 2h MUST NOT re-translate or coalesce them under a
// transport_* prefix. transport_handshake_failed carries a diagnostic-field
// sub-reason (the underlying OpenSSL error string + the [2g §6.6] tls_*
// sub-reason if verify_peer rejected); joins [2g §6.6] tls_handshake_failed
// group at the C ABI — 2i's final coalescing call.

#pragma once

#include <fixpp/core/error.hpp>

namespace fixpp::transport::errors {

// ─────────────────────────────────────────────────────────────────────────────
// Ergonomic at-site aliases for the 22 transport_* variants.
//
// Aliases re-export the canonical fixpp::core::error variants at the
// transport-module namespace level so user code at TU sites in include/fixpp/
// transport/ or src/transport/ can write `errors::connect_refused` instead of
// `fixpp::core::error::transport_connect_refused`. The canonical variants are
// the binding identifier — these aliases are sugar only.
// ─────────────────────────────────────────────────────────────────────────────

// ── Connect / lifecycle (8) → FIXPP_ERR_TRANSPORT_LIFECYCLE ─────────────────
inline constexpr auto resolve_failed         = fixpp::core::error::transport_resolve_failed;
inline constexpr auto connect_refused        = fixpp::core::error::transport_connect_refused;
inline constexpr auto connect_timeout        = fixpp::core::error::transport_connect_timeout;
inline constexpr auto already_connected      = fixpp::core::error::transport_already_connected;
inline constexpr auto already_closed         = fixpp::core::error::transport_already_closed;
inline constexpr auto read_in_progress       = fixpp::core::error::transport_read_in_progress;
inline constexpr auto write_in_progress      = fixpp::core::error::transport_write_in_progress;
inline constexpr auto reconnect_limit_exceeded
                                             = fixpp::core::error::transport_reconnect_limit_exceeded;

// ── I/O (5) → FIXPP_ERR_TRANSPORT_IO ────────────────────────────────────────
inline constexpr auto read_eof               = fixpp::core::error::transport_read_eof;
inline constexpr auto read_truncated         = fixpp::core::error::transport_read_truncated;  // covers TLS close-truncated per [2h §6.6]
inline constexpr auto read_error             = fixpp::core::error::transport_read_error;
inline constexpr auto write_short            = fixpp::core::error::transport_write_short;
inline constexpr auto write_error            = fixpp::core::error::transport_write_error;

// ── Handshake (2) → FIXPP_ERR_TRANSPORT_HANDSHAKE ───────────────────────────
// Joins [2g §6.6] FIXPP_ERR_TLS_HANDSHAKE at the C ABI (2i's coalescing call).
// handshake_failed is the GROUPING variant carrying a diagnostic-field
// sub-reason (OpenSSL error string + [2g §6.6] tls_* sub-reason from
// verify_peer if rejected). The 11 [2g §6.6] tls_* variants surface
// UNCHANGED — 2h MUST NOT re-translate them.
inline constexpr auto handshake_failed       = fixpp::core::error::transport_handshake_failed;
inline constexpr auto handshake_timeout      = fixpp::core::error::transport_handshake_timeout;

// ── Configuration (2) → FIXPP_ERR_TRANSPORT_CONFIG ──────────────────────────
inline constexpr auto factory_failed         = fixpp::core::error::transport_factory_failed;
inline constexpr auto psk_unsupported        = fixpp::core::error::transport_psk_unsupported;  // T-012 P2 deferred per [const §XII.6]

// ── Cancellation (5) → FIXPP_ERR_CANCELLED (reuse) ──────────────────────────
inline constexpr auto connect_cancelled      = fixpp::core::error::transport_connect_cancelled;
inline constexpr auto read_cancelled         = fixpp::core::error::transport_read_cancelled;
inline constexpr auto write_cancelled        = fixpp::core::error::transport_write_cancelled;
inline constexpr auto handshake_cancelled    = fixpp::core::error::transport_handshake_cancelled;
inline constexpr auto accept_cancelled       = fixpp::core::error::transport_accept_cancelled;

// 22 total. Matches spec FR-034 and [2h §6.6]. C-ABI coalescing is 2i's call;
// the C++ source-of-truth is the fixpp::core::error enum slots 94..115.

}  // namespace fixpp::transport::errors
