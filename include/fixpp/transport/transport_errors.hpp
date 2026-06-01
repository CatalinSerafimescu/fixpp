// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// fixpp::transport::errors — ergonomic re-exports of the 22 transport_* variants
// from fixpp::core::error under the fixpp::transport::errors:: namespace.
//
// Re-emitted verbatim per T007 from data-model E-13 + [2h §6.6]:1167-1204.
//
// ─────────────────────────────────────────────────────────────────────────────
// C-ABI coalescing groups (for 2i extern "C" surface; NOT exported in v1.0):
//
//   FIXPP_ERR_TRANSPORT_LIFECYCLE ← { transport_resolve_failed,
//                                     transport_connect_refused,
//                                     transport_connect_timeout,
//                                     transport_already_connected,
//                                     transport_already_closed,
//                                     transport_read_in_progress,
//                                     transport_write_in_progress,
//                                     transport_reconnect_limit_exceeded }
//
//   FIXPP_ERR_TRANSPORT_IO        ← { transport_read_eof,
//                                     transport_read_truncated,
//                                     transport_read_error,
//                                     transport_write_short,
//                                     transport_write_error }
//
//   FIXPP_ERR_TRANSPORT_HANDSHAKE ← { transport_handshake_failed,
//                                     transport_handshake_timeout }
//
//   FIXPP_ERR_TRANSPORT_CONFIG    ← { transport_factory_failed,
//                                     transport_psk_unsupported }
//
//   FIXPP_ERR_CANCELLED           ← { transport_connect_cancelled,
//                                     transport_read_cancelled,
//                                     transport_write_cancelled,
//                                     transport_handshake_cancelled,
//                                     transport_accept_cancelled }
//                                   (reused slot pool — joins tls_load_cancelled /
//                                    store_cancelled / dispatch_aborted /
//                                    clock_sleeps_cancelled)
//
// ─────────────────────────────────────────────────────────────────────────────
// FR-034a — transport_handshake_failed GROUPING-variant diagnostic sub-reason
// contract:
//   The diagnostic field on this variant carries:
//     (a) the OpenSSL error string (SSL_error_string / ERR_error_string) for
//         the handshake failure site, AND
//     (b) the [2g §6.6] tls_* sub-reason (e.g. tls_pin_mismatch,
//         tls_cert_parse_failed) when the rejection originates inside
//         verify_peer; the tls_* sub-reason IS the discriminant for the
//         FIXPP_ERR_TLS_HANDSHAKE C-ABI coalescing group and surfaces UNCHANGED
//         per FR-034 "15 tls_* variants surface UNCHANGED" partition.
//   At the C ABI, transport_handshake_failed JOINS the existing
//   FIXPP_ERR_TLS_HANDSHAKE group — callers checking only the coalesced group
//   need no changes to handle handshake failures from either the 011-tls-policy
//   layer or the 012-2h-transport layer.
//
// FR-034 — 15 tls_* variants (slots 78–92; tls_cert_load_failed ..
//   tls_pin_mismatch) surface UNCHANGED through the transport stack. The
//   transport layer propagates them via the transport_handshake_failed
//   GROUPING variant's diagnostic field; it does NOT re-map them to new slots.
// ─────────────────────────────────────────────────────────────────────────────

#pragma once

#include <fixpp/core/error.hpp>

namespace fixpp::transport::errors {

// ── LIFECYCLE ─────────────────────────────────────────────────────────────────
inline constexpr auto transport_resolve_failed = core::error::transport_resolve_failed;
inline constexpr auto transport_connect_refused = core::error::transport_connect_refused;
inline constexpr auto transport_connect_timeout = core::error::transport_connect_timeout;
inline constexpr auto transport_already_connected = core::error::transport_already_connected;
inline constexpr auto transport_already_closed = core::error::transport_already_closed;
inline constexpr auto transport_read_in_progress = core::error::transport_read_in_progress;
inline constexpr auto transport_write_in_progress = core::error::transport_write_in_progress;
inline constexpr auto transport_reconnect_limit_exceeded =
    core::error::transport_reconnect_limit_exceeded;

// ── IO ────────────────────────────────────────────────────────────────────────
inline constexpr auto transport_read_eof = core::error::transport_read_eof;
inline constexpr auto transport_read_truncated = core::error::transport_read_truncated;
inline constexpr auto transport_read_error = core::error::transport_read_error;
inline constexpr auto transport_write_short = core::error::transport_write_short;
inline constexpr auto transport_write_error = core::error::transport_write_error;

// ── HANDSHAKE ─────────────────────────────────────────────────────────────────
inline constexpr auto transport_handshake_failed = core::error::transport_handshake_failed;
inline constexpr auto transport_handshake_timeout = core::error::transport_handshake_timeout;

// ── CONFIG ────────────────────────────────────────────────────────────────────
inline constexpr auto transport_factory_failed = core::error::transport_factory_failed;
inline constexpr auto transport_psk_unsupported = core::error::transport_psk_unsupported;

// ── CANCELLED (reused FIXPP_ERR_CANCELLED pool) ───────────────────────────────
inline constexpr auto transport_connect_cancelled = core::error::transport_connect_cancelled;
inline constexpr auto transport_read_cancelled = core::error::transport_read_cancelled;
inline constexpr auto transport_write_cancelled = core::error::transport_write_cancelled;
inline constexpr auto transport_handshake_cancelled = core::error::transport_handshake_cancelled;
inline constexpr auto transport_accept_cancelled = core::error::transport_accept_cancelled;

}  // namespace fixpp::transport::errors
