/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * include/fix/c_api/engine.h — C-ABI engine lifecycle + engine-config builder
 *                              (CA-005, Feature B)
 *
 * [2i §4.2/§5.2/§4.10] / specs/050-c-abi-session-send-recv/contracts/lifecycle-surface.md.
 * C-clean: no C++ symbols, no C++ syntax. Compiles as C11.
 *
 * The C-ABI engine owns an internal io_context + worker thread(s) (research D-2:
 * the C++ Engine "owns NO worker threads", engine.hpp:222 — a C consumer has no
 * asio executor to supply, so the boundary owns one). Lifecycle is register-then-
 * start, which succeeds once (research D-1):
 *
 *   fixpp_engine_create                       (build EngineConfig; stand up the loop)
 *     → fixpp_session_open × N                (= Engine::register_session, BEFORE start)
 *     → fixpp_engine_start                    (= Engine::start(), once successfully; spawns role loops)
 *     → drive (send / receive) ...
 *     → fixpp_session_close                   (per session)
 *     → fixpp_engine_destroy                  (co_await stop(); join workers; ~Engine)
 *
 * open != connected: establishment is asynchronous after start; poll
 * fixpp_session_is_established (session.h) before sending.
 *
 * Thunk split ([2i §5.2]): create/start + the config-builder symbols are
 * construction-time (catch std::exception → *_CONFIG; no exception escapes).
 * Reentrancy ([2i §4.10] / [const §X.5]): all symbols here are SINGLE_THREAD.
 */

#ifndef FIXPP_C_API_ENGINE_H
#define FIXPP_C_API_ENGINE_H

/* NOLINTBEGIN(hicpp-deprecated-headers,modernize-deprecated-headers):
   C-ABI header; C-style includes are correct for pure-C consumers. */
#include <stdint.h>
/* NOLINTEND(hicpp-deprecated-headers,modernize-deprecated-headers) */

#include <fix/c_api/error.h>
#include <fix/c_api/export.h>
#include <fix/c_api/handles.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Engine-config builder (opaque; FR-014) ────────────────────────────────
 * THREAD: SINGLE_THREAD per handle. Each setter mutates the under-construction
 * EngineConfig. The builder is CONSUMED (moved into the engine) by
 * fixpp_engine_create on success and must NOT be destroyed afterwards; on
 * failure it is untouched and the caller still owns it (must destroy). */

/** Create an engine-config builder. THUNK: construction-time. Reentrancy: single-thread. */
FIXPP_API_EXPORT fixpp_error_t fixpp_engine_config_create(fixpp_engine_config_t** out_cfg);

/** Set the worker-thread count for the internal io_context (default 1).
 *  Reentrancy: single-thread. */
FIXPP_API_EXPORT fixpp_error_t fixpp_engine_config_set_worker_threads(
    fixpp_engine_config_t* cfg, uint32_t n);

/** Install a real-time clock (REQUIRED non-null; Engine::start rejects null).
 *  Reentrancy: single-thread. */
FIXPP_API_EXPORT fixpp_error_t fixpp_engine_config_set_realtime_clock(
    fixpp_engine_config_t* cfg);

/** fixpp_engine_config_add_dictionary — add a dictionary to the engine's list.
 *
 *  Copies the shared pointer, so the caller may destroy its handle afterwards. Several
 *  calls accumulate, in order, so of two dictionaries of one version the later answers.
 *  fixpp_engine_create copies the list. A validating FIXT session needs the FIXT.1.1
 *  dictionary here, and one for its default: fixpp_session_open refuses it otherwise.
 *  (099-fixt-appl-version-validation, C-ABI 1.14.)
 *
 *  Reentrancy: single-thread.
 *  @return FIXPP_ERR_OK
 *  @return FIXPP_ERR_NULL_HANDLE          cfg is NULL
 *  @return FIXPP_ERR_CAPI_CONFIG_INVALID  dict is NULL or an empty wrapper
 *  @return FIXPP_ERR_INVALID_HANDLE       dict is a destroyed or wrong-type handle
 */
FIXPP_API_EXPORT fixpp_error_t fixpp_engine_config_add_dictionary(
    fixpp_engine_config_t* cfg, fixpp_dict_t* dict);

/** Destroy an engine-config builder. NULL-safe; never-throws. Do NOT call after
 *  the builder was consumed by a successful fixpp_engine_create.
 *  Reentrancy: single-thread. */
FIXPP_API_EXPORT void fixpp_engine_config_destroy(fixpp_engine_config_t* cfg);

/* ── Engine lifecycle ──────────────────────────────────────────────────────*/

/**
 * fixpp_engine_create — create an engine and stand up its internal event loop.
 *
 * Records the consumer's ABI minor (FR-002 / L-049-1) so every fallible C-ABI
 * return on this engine runs Feature A's forward-compat downgrade
 * (translate_for_consumer). `cfg` is CONSUMED on success (moved in; invalidated).
 *
 * Reentrancy: single-thread. THUNK: construction-time (catch → *_CONFIG; never throws).
 * @return FIXPP_ERR_OK + a non-null owning *out_engine; or a config code (null
 *         *out_engine); FIXPP_ERR_VERSION_MISMATCH if consumer_major != MAJOR.
 */
FIXPP_API_EXPORT fixpp_error_t fixpp_engine_create(fixpp_engine_config_t* cfg,
                                                   uint16_t consumer_major,
                                                   uint16_t consumer_minor,
                                                   fixpp_engine_t** out_engine);

/**
 * fixpp_engine_start — start the engine (= Engine::start()); succeeds once.
 *
 * Spawns the role loops; sessions establish asynchronously. Call AFTER all
 * fixpp_session_open calls. A null clock returns a configuration error.
 *
 * On an engine with a FIXT session it blocks the calling thread for the walks that build
 * the FIXT route tables, before any worker runs. A table build failure returns
 * FIXPP_ERR_UNKNOWN and leaves the engine not started, so a later call may retry; a
 * second call after a success returns a domain error. A refused config is reported by
 * fixpp_session_open, not here. (099-fixt-appl-version-validation, C-ABI 1.14.)
 *
 * Reentrancy: single-thread. THUNK: construction-time.
 */
FIXPP_API_EXPORT fixpp_error_t fixpp_engine_start(fixpp_engine_t* engine);

/**
 * fixpp_engine_destroy — stop and destroy the engine.
 *
 * Drives Engine::stop() UNCONDITIONALLY to completion (even on a never-started
 * engine — ~Engine asserts stopped(); stop() is idempotent from any state,
 * FR-003), resets the work-guard, joins the worker thread(s), deletes the Engine.
 *
 * Reentrancy: single-thread. Idempotent, NULL-safe, never-throws (NULL / already-
 * destroyed → no-op).
 */
FIXPP_API_EXPORT void fixpp_engine_destroy(fixpp_engine_t* engine);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FIXPP_C_API_ENGINE_H */
