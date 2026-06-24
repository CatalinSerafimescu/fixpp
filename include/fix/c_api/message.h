/*
 * include/fix/c_api/message.h — C-ABI message handles (CA-008/CA-009/CA-010, Feature C)
 *
 * [2i §4.6/§4.9/§4.10/§10] /
 * specs/051-c-abi-message-accessors/contracts/{message-read,message-write,toapp-callback}.md.
 * C-clean: no C++ symbols, no C++ syntax. Compiles as C11.
 *
 * Three opaque handle types used by Feature C:
 *   - fixpp_msg_t       (declared in handles.h; referenced here via export.h)
 *   - fixpp_group_t     : inbound repeating-group cursor; valid for the parent
 *                         fixpp_msg_t dispatch window ([2i §4.6], [2c §4.7]/W-007).
 *                         NON-owning — do NOT destroy; lifetime bounded by the
 *                         parent fixpp_msg_t (inbound) or outbound accumulator.
 *   - fixpp_group_builder_t : outbound repeating-group builder; owning, invalidated
 *                         at fixpp_msg_group_end (LIFO close-order contract, FR-012).
 *   - fixpp_entry_t     : per-entry read/write cursor; valid while the enclosing
 *                         fixpp_group_builder_t (write) or fixpp_group_t (read)
 *                         is open.
 *
 * ── Per-symbol reentrancy annotations (doc-only; no macro):
 *   FIXPP_REQUIRES_SESSION_LOCK — runs on the session strand (session-strand only;
 *       caller is never allowed to race concurrent invocations on the same handle).
 *   FIXPP_THREAD_SAFE           — callable from any thread without external sync
 *       (typically lock-free reads of atomic/const state; documented at each site).
 *
 * Function declarations land per-story (US1..US6) in subsequent tasks.
 * This header provides only the foundation opaque typedefs shared across all
 * Feature-C stories.
 */

#ifndef FIXPP_C_API_MESSAGE_H
#define FIXPP_C_API_MESSAGE_H

/* NOLINTBEGIN(hicpp-deprecated-headers,modernize-deprecated-headers):
   C-ABI header; C-style includes are correct for pure-C consumers. */
#include <stdbool.h>  /* NOLINT(hicpp-deprecated-headers,modernize-deprecated-headers) */
#include <stddef.h>   /* NOLINT(hicpp-deprecated-headers,modernize-deprecated-headers) */
#include <stdint.h>   /* NOLINT(hicpp-deprecated-headers,modernize-deprecated-headers) */
/* NOLINTEND(hicpp-deprecated-headers,modernize-deprecated-headers) */

#include "fix/c_api/export.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Opaque handle typedefs (Feature C) ────────────────────────────────────
 *
 * Concrete definitions are engine-internal (src/capi/capi_internal.hpp);
 * public headers expose only incomplete forward typedefs per [2i §4.2/§4.2.1].
 */

/** Inbound repeating-group read cursor (CA-010-read).
 *  Aliased into the parent message's wire buffer — zero-copy, zero-alloc.
 *  NON-owning: do NOT pass to any destroy function.
 *  Lifetime: bounded by the enclosing fixpp_msg_t dispatch window. */
typedef struct fixpp_group fixpp_group_t;

/** Outbound repeating-group builder (CA-010-write).
 *  Owning: LIFO open/close order enforced (FR-012); invalidated by fixpp_msg_group_end.
 *  Reentrancy: FIXPP_REQUIRES_SESSION_LOCK (runs on the session-build thread). */
typedef struct fixpp_group_builder fixpp_group_builder_t;

/** Per-entry read/write cursor.
 *  For read (CA-010-read): aliases into the parent fixpp_group_t slice.
 *  For write (CA-010-write): borrows into the enclosing fixpp_group_builder_t.
 *  Lifetime: bounded by the enclosing group cursor / builder. */
typedef struct fixpp_entry fixpp_entry_t;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FIXPP_C_API_MESSAGE_H */
