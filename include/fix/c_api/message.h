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
 * CA-008 function declarations (T006/US1) and CA-010-read (T013/US3) land here.
 */

#ifndef FIXPP_C_API_MESSAGE_H
#define FIXPP_C_API_MESSAGE_H

/* NOLINTBEGIN(hicpp-deprecated-headers,modernize-deprecated-headers):
   C-ABI header; C-style includes are correct for pure-C consumers. */
#include <stdbool.h>  /* NOLINT(hicpp-deprecated-headers,modernize-deprecated-headers) */
#include <stddef.h>   /* NOLINT(hicpp-deprecated-headers,modernize-deprecated-headers) */
#include <stdint.h>   /* NOLINT(hicpp-deprecated-headers,modernize-deprecated-headers) */
/* NOLINTEND(hicpp-deprecated-headers,modernize-deprecated-headers) */

#include "fix/c_api/decimal.h"
#include "fix/c_api/error.h"
#include "fix/c_api/export.h"
#include "fix/c_api/handles.h"

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

/* ── fixpp_resolved_msg_version_t — resolved wire message version (CA-008) ──
 *
 * Returned by fixpp_msg_version(). The pointers alias the wire buffer
 * (flyweight rule [2b §6.4]) — no copy, no free. Lifetime: bounded by the
 * parent fixpp_msg_t dispatch window.
 *
 * begin_string: tag 8 (BeginString) value, e.g. "FIX.4.4" or "FIXT.1.1".
 *               Always present in a valid inbound message.
 * appl_ver_id:  tag 1137 (DefaultApplVerID) value, present only for FIXT sessions.
 *               NULL (and appl_ver_id_len == 0) if the field is absent.
 */
typedef struct fixpp_resolved_msg_version {
    const char* begin_string;     /* aliased wire value of tag 8, NUL-terminated in wire */
    size_t      begin_string_len; /* byte length (excluding any NUL) */
    const char* appl_ver_id;      /* aliased wire value of tag 1137; NULL if absent */
    size_t      appl_ver_id_len;  /* byte length; 0 if absent */
} fixpp_resolved_msg_version_t;

/* ── CA-008 field read accessors ────────────────────────────────────────────
 *
 * Backing: wire::MessageView<Index>::get(tag) → expected_t<field_view>
 *          (parser.hpp:200); get_decimal(tag, mr) (parser.hpp:215); msg_type()
 *          (parser.hpp:143). Steady-state thunks: abort on any escaping C++
 *          exception ([2i §5.2]). Zero global-heap (SC-003).
 *
 * Return codes common to all accessors:
 *   FIXPP_ERR_OK              — success
 *   FIXPP_ERR_NULL_HANDLE     — msg or output pointer is NULL
 *   FIXPP_ERR_INVALID_HANDLE  — destroyed / tombstoned / wrong-tag handle
 *                               OR wrong flavour (outbound handle, view==NULL)
 *   FIXPP_ERR_TAG_NOT_FOUND   — the tag is absent from the message
 */

/** Read the string value of `tag` from the inbound message.
 *  *value_out ALIASES the wire buffer (no copy, no free).
 *  Lifetime: valid until the inbound dispatch window closes.
 *
 *  Reentrancy: requires-session-lock (inbound dispatch window; session strand only).
 */
FIXPP_API_EXPORT fixpp_error_t fixpp_msg_get_string(const fixpp_msg_t* msg, uint16_t tag,
                                              const char** value_out, size_t* len_out);

/** Read the raw bytes of `tag` from the inbound message (type-agnostic).
 *  *bytes_out ALIASES the wire buffer. Lifetime as fixpp_msg_get_string.
 *
 *  Reentrancy: requires-session-lock
 */
FIXPP_API_EXPORT fixpp_error_t fixpp_msg_get_bytes(const fixpp_msg_t* msg, uint16_t tag,
                                             const uint8_t** bytes_out, size_t* len_out);

/** Read the integer value of `tag` (ASCII decimal → int64_t).
 *  Returns FIXPP_ERR_WIRE_INVALID_FRAME when the field bytes are non-numeric.
 *
 *  Reentrancy: requires-session-lock
 */
FIXPP_API_EXPORT fixpp_error_t fixpp_msg_get_int(const fixpp_msg_t* msg, uint16_t tag,
                                           int64_t* value_out);

/** Read the double value of `tag` (ASCII float → double).
 *  Returns FIXPP_ERR_WIRE_INVALID_FRAME when the field bytes are non-numeric.
 *
 *  Reentrancy: requires-session-lock
 */
FIXPP_API_EXPORT fixpp_error_t fixpp_msg_get_double(const fixpp_msg_t* msg, uint16_t tag,
                                              double* value_out);

/** Read the decimal value of `tag` via the 2a decimal trait.
 *  Returns FIXPP_ERR_DECIMAL_INVALID or FIXPP_ERR_DECIMAL_PRECISION_LOSS
 *  on trait parse failure. Uses a function-local scratch arena (zero global-heap).
 *
 *  Reentrancy: requires-session-lock
 */
FIXPP_API_EXPORT fixpp_error_t fixpp_msg_get_decimal(const fixpp_msg_t* msg, uint16_t tag,
                                               fixpp_decimal_t* value_out);

/** Test whether `tag` is present in the inbound message.
 *  *present_out is true iff the tag exists; does NOT distinguish absent from
 *  zero-length values.
 *
 *  Reentrancy: requires-session-lock
 */
FIXPP_API_EXPORT fixpp_error_t fixpp_msg_has_tag(const fixpp_msg_t* msg, uint16_t tag,
                                           bool* present_out);

/** Return the resolved wire message version (tag 8 + optional tag 1137).
 *  All fields alias the wire buffer; lifetime bounded by the dispatch window.
 *  Always succeeds for a valid inbound message (tag 8 is a required header field).
 *
 *  Reentrancy: requires-session-lock
 */
FIXPP_API_EXPORT fixpp_error_t fixpp_msg_version(const fixpp_msg_t* msg,
                                           fixpp_resolved_msg_version_t* version_out);

/** Convenience: return the MsgType (tag 35) value aliasing the wire buffer.
 *  Equivalent to fixpp_msg_get_string(msg, 35, value_out, len_out) but optimised
 *  via MessageView::msg_type() (parser.hpp:143).
 *
 *  Reentrancy: requires-session-lock
 */
FIXPP_API_EXPORT fixpp_error_t fixpp_msg_get_msg_type(const fixpp_msg_t* msg,
                                                const char** value_out, size_t* len_out);

/* ── CA-010 repeating-group read ───────────────────────────────────────────
 *
 * Backing: wire::OffsetTable::group_slices(group_tag) (D-4) — per-instance
 * arena slices materialized once, cached. Per-entry field reads walk the
 * instance slice for the target tag via field_iterator. Nested groups recurse
 * on the sub-slice.
 *
 * All cursors (fixpp_group_t) alias the parent message's parse arena; lifetime
 * bounded by the parent fixpp_msg_t dispatch window. NON-owning — do NOT destroy.
 *
 * Error codes:
 *   FIXPP_ERR_TAG_NOT_FOUND      — group or field absent
 *   FIXPP_ERR_TYPE_MISMATCH      — group_tag is present but is not a group delimiter
 *                                  (i.e. the tag is found in the message but has no
 *                                  group slices — a scalar field used as a group tag)
 *   FIXPP_ERR_INDEX_OUT_OF_RANGE — entry_index >= count
 */

/** Obtain the group cursor and instance count for group_tag (the NoXxx count field).
 *  *group_out aliases the parent message's parse arena.
 *
 *  Reentrancy: requires-session-lock
 */
FIXPP_API_EXPORT fixpp_error_t fixpp_msg_get_group(const fixpp_msg_t* msg, uint16_t group_tag,
                                             const fixpp_group_t** group_out,
                                             size_t* count_out);

/** Read the string value of `tag` from entry `i` of the group cursor.
 *  *v_out aliases the wire buffer.
 *
 *  Reentrancy: requires-session-lock
 */
FIXPP_API_EXPORT fixpp_error_t fixpp_group_get_field_string(const fixpp_group_t* g, size_t i,
                                                      uint16_t tag, const char** v_out,
                                                      size_t* len_out);

/** Read the integer value of `tag` from entry `i`.
 *
 *  Reentrancy: requires-session-lock
 */
FIXPP_API_EXPORT fixpp_error_t fixpp_group_get_field_int(const fixpp_group_t* g, size_t i,
                                                   uint16_t tag, int64_t* v_out);

/** Read the double value of `tag` from entry `i`.
 *
 *  Reentrancy: requires-session-lock
 */
FIXPP_API_EXPORT fixpp_error_t fixpp_group_get_field_double(const fixpp_group_t* g, size_t i,
                                                      uint16_t tag, double* v_out);

/** Read the decimal value of `tag` from entry `i`.
 *
 *  Reentrancy: requires-session-lock
 */
FIXPP_API_EXPORT fixpp_error_t fixpp_group_get_field_decimal(const fixpp_group_t* g, size_t i,
                                                       uint16_t tag, fixpp_decimal_t* v_out);

/** Descend into a nested group `nested_tag` within entry `i` of cursor `g`.
 *  *nested_out aliases the parent cursor's backing; lifetime bounded by it.
 *
 *  Reentrancy: requires-session-lock
 */
FIXPP_API_EXPORT fixpp_error_t fixpp_group_get_nested_group(const fixpp_group_t* g, size_t i,
                                                      uint16_t nested_tag,
                                                      const fixpp_group_t** nested_out,
                                                      size_t* nested_count_out);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FIXPP_C_API_MESSAGE_H */
