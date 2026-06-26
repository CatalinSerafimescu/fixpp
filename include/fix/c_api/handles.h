/*
 * include/fix/c_api/handles.h — opaque handle catalogue (CA-001)
 *
 * [2i §4.2/§4.2.1] / data-model E-1. C-clean: no C++ symbols, no C++ syntax.
 *
 * The five v1.0 opaque handles ([arch §4.10]). Each is an INCOMPLETE forward
 * struct — the concrete definition is engine-internal and arrives with the
 * owning feature (create/operate/destroy FUNCTIONS are Features B/C, NOT here).
 * Feature A publishes only the typedefs + the destroy/invalidation discipline
 * and the null/invalid-handle code contract.
 *
 * ── Destroy / invalidation discipline ([2i §4.2.1], per-handle — NOT uniform):
 *   - fixpp_engine_t : owning. Future fixpp_engine_destroy(engine) — idempotent,
 *                      NULL-safe (NULL / already-destroyed → no-op), never throws;
 *                      double-destroy is safe. (Feature B / [2j])
 *   - fixpp_dict_t   : owning (refcounted shared_ptr). Created by
 *                      fixpp_dict_load_from_xml(...) and destroyed by
 *                      fixpp_dict_destroy(dict) with the same idempotent
 *                      NULL-safe never-throwing discipline. ([2c])
 *   - fixpp_msg_t    : outbound/clone — fixpp_msg_destroy(msg): owning, NULL-safe,
 *                      never throws, but SINGLE-DESTROY only — a double-destroy of
 *                      the same non-null pointer is UB (B-051-2; a per-op handle,
 *                      NOT the idempotent double-destroy-safe discipline of the
 *                      singleton engine/dict handles above). Inbound — engine-
 *                      destroyed at parse-window close; the consumer does NOT
 *                      destroy it.
 *   - fixpp_session_t: NON-owning observer. There is NO fixpp_session_destroy;
 *                      the session is closed via the lifecycle
 *                      fixpp_session_close(session) (Feature B), and the handle
 *                      invalidates once close returns.
 *   - fixpp_store_t  : NON-owning observer of a session-owned store ([2e §6.7] N1).
 *                      NO destroy at all — it invalidates when its owning session
 *                      closes.
 *
 * ── Null vs invalid handle (codes published by fix/c_api/error.h):
 *   Every (future) handle-taking function checks NULL FIRST → FIXPP_ERR_NULL_HANDLE;
 *   a non-null but destroyed/corrupted handle → FIXPP_ERR_INVALID_HANDLE.
 *   Feature A publishes the codes + documents the rule; the enforcement arrives
 *   with the functions in Features B/C.
 *
 * No C++ symbol leakage (§X.2): the typedefs are opaque; nothing from `fixpp::`
 * appears here. Handles add NO exported symbols (typedefs are compile-time only).
 */

#ifndef FIXPP_C_API_HANDLES_H
#define FIXPP_C_API_HANDLES_H

/* The null/invalid-handle code contract is defined in error.h; included so a
   TU using handles.h has FIXPP_ERR_NULL_HANDLE / FIXPP_ERR_INVALID_HANDLE. */
#include "fix/c_api/error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Incomplete forward typedefs — concrete definitions are engine-internal. */
typedef struct fixpp_engine  fixpp_engine_t;
typedef struct fixpp_session fixpp_session_t;
typedef struct fixpp_msg     fixpp_msg_t;
typedef struct fixpp_dict    fixpp_dict_t;
typedef struct fixpp_store   fixpp_store_t;

/* Opaque config-builder handles (CA-005, Feature B, FR-014). Owning: created by
   fixpp_{engine,session}_config_create, destroyed by *_config_destroy OR consumed
   (moved into the engine/registry + invalidated) by fixpp_engine_create /
   fixpp_session_open. Opaque so a future config field is a new setter, not a
   struct-layout break (Article X). */
typedef struct fixpp_engine_config  fixpp_engine_config_t;
typedef struct fixpp_session_config fixpp_session_config_t;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FIXPP_C_API_HANDLES_H */
