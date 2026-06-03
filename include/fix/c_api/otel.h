/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * include/fix/c_api/otel.h
 *
 * C-ABI placeholder for the fixpp OpenTelemetry observability surface.
 *
 * v1.0: this header contains ONLY the API version macro and include guard.
 * No extern "C" symbols are defined here (FR-020 — the OTel C-ABI surface
 * is deferred to a 2i amendment; providers/exporters are C++ only in v1.0).
 *
 * The [1010,1011] fixpp_error_t values (otel_export_failed,
 * otel_provider_init_failed) are in the [1000,1099] block reserved for
 * 017-log-otel per [2i §1.1]; see tools/abi_history/error_codes_v1.txt.
 *
 * Anchor: contracts/error-block.md FR-020 / [2k §7].
 */
#ifndef FIXPP_C_API_OTEL_H
#define FIXPP_C_API_OTEL_H

/** API version of the fixpp OTel C-ABI surface. v1.0: no symbols defined. */
#define FIXPP_OTEL_API_VERSION 1

#endif /* FIXPP_C_API_OTEL_H */
