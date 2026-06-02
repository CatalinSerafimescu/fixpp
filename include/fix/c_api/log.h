/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * include/fix/c_api/log.h
 *
 * C-ABI placeholder for the fixpp async logger surface.
 *
 * v1.0: this header contains ONLY the API version macro and include guard.
 * No extern "C" symbols are defined here (FR-020 — the log C-ABI surface
 * is deferred to a 2i amendment; live symbols require a C-ABI subscription
 * surface ([2k §7 Option B]) that is out of scope for v1.0).
 *
 * The [1000,1099] fixpp_error_t block is reserved for this feature per
 * [2i §1.1]; see tools/abi_history/error_codes_v1.txt for the occupancy
 * mapping.
 *
 * Anchor: contracts/error-block.md FR-020 / [2k §7].
 */
#ifndef FIXPP_C_API_LOG_H
#define FIXPP_C_API_LOG_H

/** API version of the fixpp log C-ABI surface. v1.0: no symbols defined. */
#define FIXPP_LOG_API_VERSION 1

#endif /* FIXPP_C_API_LOG_H */
