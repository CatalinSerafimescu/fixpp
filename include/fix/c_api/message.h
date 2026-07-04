/*
 * include/fix/c_api/message.h — C-ABI message handles (CA-008/CA-009/CA-010, Feature C)
 *
 * [2i §4.6/§4.9/§4.10/§10] /
 * specs/051-c-abi-message-accessors/contracts/{message-read,message-write,toapp-callback}.md.
 * C-clean: no C++ symbols, no C++ syntax. Compiles as C11.
 *
 * Three opaque handle types used by Feature C:
 *   - fixpp_msg_t       (declared in handles.h; referenced here via export.h)
 *   - fixpp_group_t     : inbound (read-cursor) repeating-group handle — outbound
 *                         construction uses fixpp_group_builder_t/fixpp_entry_t, NOT
 *                         this type ([2i §4.6], [2c §4.7]/W-007). NON-owning — do NOT
 *                         destroy; lifetime bounded by the parent fixpp_msg_t: the
 *                         inbound/toApp dispatch window, or — for a detached clone —
 *                         until fixpp_msg_destroy.
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
 *  Lifetime: handle-flavour dependent — bounded by the enclosing fixpp_msg_t's
 *  inbound/toApp dispatch window, or (for a detached clone) until fixpp_msg_destroy. */
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
 * (flyweight rule [2b §6.4]) — no copy, no free. Lifetime is handle-flavour
 * dependent — bounded by the parent fixpp_msg_t's inbound/toApp dispatch window,
 * or (for a detached clone) until fixpp_msg_destroy.
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
 *
 * Returned-pointer lifetime is HANDLE-FLAVOUR dependent: for an inbound / toApp
 * handle the aliases are valid ONLY within the receive/send callback (dispatch)
 * window; for a detached fixpp_msg_clone they are valid until fixpp_msg_destroy
 * (the clone owns its arena, survives session/engine teardown, and its reads are
 * THREAD_SAFE). The per-accessor "valid until the dispatch window closes" wording
 * below is the inbound case; substitute "until fixpp_msg_destroy" for a clone.
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
 *  All fields alias the wire buffer; lifetime per the handle-flavour note above
 *  (inbound/toApp dispatch window, or until fixpp_msg_destroy for a clone).
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
 * All cursors (fixpp_group_t) alias the parent message's parse arena; lifetime is
 * handle-flavour dependent — bounded by the parent fixpp_msg_t's inbound/toApp
 * dispatch window, or (for a detached clone) until fixpp_msg_destroy. NON-owning —
 * do NOT destroy.
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

/* ── CA-009 outbound message construct / set / commit (T009/US2) ────────────
 *
 * Outbound accumulator lifecycle:
 *   fixpp_msg_create_outbound -> fixpp_msg_set_X (zero or more) ->
 *   fixpp_msg_commit -> fixpp_session_send(*payload_out, len_out) ->
 *   fixpp_msg_destroy
 *
 * The committed span (*payload_out) is valid until the next mutation or
 * destroy.  The immediate-destroy pattern (commit->send->destroy) is safe
 * because fixpp_session_send blocks on fut.get() and Engine::send deep-copies
 * the payload at entry (NORMATIVE Codex #4, FR-021).
 *
 * Reentrancy: all create/set/commit/remove/destroy are FIXPP_REQUIRES_SESSION_LOCK
 *             (must not be called concurrently on the same handle).
 *             fixpp_msg_destroy is FIXPP_THREAD_SAFE (NULL-safe; single-destroy --
 *             double-destroy of the same non-null pointer is UB, B-051-2).
 *             fixpp_msg_clone is FIXPP_REQUIRES_SESSION_LOCK on the source.
 */

/** Create an outbound message accumulator bound to `session`.
 *
 *  The returned handle lives outside the per-message arena (operator new);
 *  its internal accumulator is allocated into the per-message monotonic arena
 *  owned by the handle itself (D3 -- not the session arena).
 *  The handle is tied to the session's liveness token (E-9): after
 *  fixpp_session_close or fixpp_engine_destroy, any set/commit call
 *  returns FIXPP_ERR_INVALID_HANDLE (lazy tombstone).
 *
 *  Return codes:
 *    FIXPP_ERR_OK             -- success; *msg_out is live
 *    FIXPP_ERR_NULL_HANDLE    -- session or msg_out is NULL
 *    FIXPP_ERR_INVALID_HANDLE -- session is destroyed / engine is gone
 *    FIXPP_ERR_DICT_CONFIG    -- msg_type not found in the session dictionary
 *
 *  Reentrancy: requires-session-lock
 */
FIXPP_API_EXPORT fixpp_error_t fixpp_msg_create_outbound(fixpp_session_t* session,
                                                    const char* msg_type, size_t msg_type_len,
                                                    fixpp_msg_t** msg_out);

/** Destroy an outbound or clone message handle.  NULL-safe (NULL -> OK).
 *  Single-destroy: double-destroy of the same non-null pointer is UB (B-051-2).
 *  Always returns FIXPP_ERR_OK.
 *
 *  Reentrancy: thread-safe
 */
FIXPP_API_EXPORT fixpp_error_t fixpp_msg_destroy(fixpp_msg_t* msg);

/** Clone an inbound (view-backed) message into a session-independent handle.
 *  An outbound accumulator handle (committed or not) has no wire view and returns
 *  FIXPP_ERR_INVALID_HANDLE (v1.0 scope — outbound-accumulator clone is L-051-2).
 *
 *  The clone owns its own arena; it is NOT tied to the source session's
 *  liveness token (session close does NOT tombstone the clone).
 *  Clone reads (get, has_tag) are THREAD_SAFE.
 *
 *  Return codes:
 *    FIXPP_ERR_OK             -- success; *clone_out is live
 *    FIXPP_ERR_NULL_HANDLE    -- src or clone_out is NULL
 *    FIXPP_ERR_INVALID_HANDLE -- src is destroyed / tombstoned
 *
 *  Reentrancy: requires-session-lock (on the source handle's owning session)
 */
FIXPP_API_EXPORT fixpp_error_t fixpp_msg_clone(const fixpp_msg_t* src, fixpp_msg_t** clone_out);

/** Set a STRING field on an outbound accumulator.
 *
 *  Deep-copies value[0..len-1] into the per-message arena (caller may free
 *  value immediately).  Overwrites any prior value for `tag`.
 *  Framing tags 8/9/34/49/52/56/10 return FIXPP_ERR_MSG_FRAMING_TAG_FORBIDDEN.
 *
 *  Return codes:
 *    FIXPP_ERR_OK                        -- success
 *    FIXPP_ERR_NULL_HANDLE               -- msg or value is NULL
 *    FIXPP_ERR_INVALID_HANDLE            -- msg is destroyed / session closed
 *    FIXPP_ERR_MSG_FRAMING_TAG_FORBIDDEN -- tag is a framing tag
 *    FIXPP_ERR_DICT_CONFIG               -- tag not declared for this MsgType
 *    (note: set_string is always-OK on any dict field type -- no TYPE_MISMATCH)
 *
 *  Reentrancy: requires-session-lock
 */
FIXPP_API_EXPORT fixpp_error_t fixpp_msg_set_string(fixpp_msg_t* msg, uint16_t tag,
                                               const char* value, size_t len);

/** Set a raw-bytes field (type-agnostic escape hatch for Data or extension tags).
 *  No dictionary type check; framing-tag check still applies.
 *
 *  Reentrancy: requires-session-lock
 */
FIXPP_API_EXPORT fixpp_error_t fixpp_msg_set_bytes(fixpp_msg_t* msg, uint16_t tag,
                                              const uint8_t* bytes, size_t len);

/** Set an integer field (serialised as ASCII decimal).
 *  Reentrancy: requires-session-lock
 */
FIXPP_API_EXPORT fixpp_error_t fixpp_msg_set_int(fixpp_msg_t* msg, uint16_t tag, int64_t value);

/** Set a floating-point field. Serialised as the shortest round-tripping
 *  locale-independent fixed-point ASCII form (never scientific notation; up to 17
 *  significant digits — use fixpp_msg_set_decimal for controlled scale). Returns
 *  FIXPP_ERR_DECIMAL_INVALID if the value is non-finite (NaN/Inf) or outside the
 *  representable FIX-decimal range (|value| beyond ~9.2e18, or non-zero |value|
 *  below ~1e-38) — the setter never emits bytes the engine's own FLOAT parser
 *  would reject.
 *  Reentrancy: requires-session-lock
 */
FIXPP_API_EXPORT fixpp_error_t fixpp_msg_set_double(fixpp_msg_t* msg, uint16_t tag, double value);

/** Set a fixpp_decimal_t field (serialised via fixpp_decimal_format).
 *  Reentrancy: requires-session-lock
 */
FIXPP_API_EXPORT fixpp_error_t fixpp_msg_set_decimal(fixpp_msg_t* msg, uint16_t tag,
                                                fixpp_decimal_t value);

/** Remove a tag from the accumulator.  Idempotent (absent returns OK).
 *  Reentrancy: requires-session-lock
 */
FIXPP_API_EXPORT fixpp_error_t fixpp_msg_remove_tag(fixpp_msg_t* msg, uint16_t tag);

/** Serialise the accumulator into an app-payload span.
 *
 *  The payload is: 35=type SOH tag=value SOH ... (no framing tags 8/9/34/49/52/56/10).
 *  *payload_out aliases the per-message arena; valid until the next mutation or destroy.
 *
 *  Return codes:
 *    FIXPP_ERR_OK              -- success
 *    FIXPP_ERR_NULL_HANDLE     -- msg or payload_out or len_out is NULL
 *    FIXPP_ERR_INVALID_HANDLE  -- msg is destroyed or session closed or open group
 *    FIXPP_ERR_TYPE_MISMATCH   -- group grammar violated (empty instance or
 *                                 non-delimiter-first instance; INV-4)
 *    FIXPP_ERR_WIRE_LIMIT_EXCEEDED -- serialised body exceeds ~3800 B
 *
 *  Reentrancy: requires-session-lock
 */
FIXPP_API_EXPORT fixpp_error_t fixpp_msg_commit(fixpp_msg_t* msg, const uint8_t** payload_out,
                                           size_t* len_out);

/* ── CA-010 outbound repeating-group builder (T016/US4) ─────────────────────
 *
 * Build a repeating group on an outbound accumulator:
 *   fixpp_msg_group_begin(msg, NoXxx, &builder)
 *   -> fixpp_group_builder_add_entry(builder, &entry) [per instance]
 *      -> fixpp_entry_set_{string,int,double,decimal}(entry, tag, …) [per field]
 *      -> fixpp_entry_group_begin(entry, NoYyy, &nested) [optional nested group]
 *   -> fixpp_msg_group_end(msg, builder)
 *
 * LIFO close-order (FR-012 / E-4): builders MUST be ended in reverse of begin
 * order; ending a parent while a younger nested builder is still open →
 * FIXPP_ERR_INVALID_HANDLE. fixpp_msg_commit with any open builder →
 * FIXPP_ERR_INVALID_HANDLE. Reuse of an ended builder/entry →
 * FIXPP_ERR_INVALID_HANDLE. All are FIXPP_REQUIRES_SESSION_LOCK.
 *
 * NOTE: there is no fixpp_entry_set_bytes — entry fields are dictionary-typed;
 * the raw-bytes escape (fixpp_msg_set_bytes) exists only at message level.
 */

/** Begin a repeating group `group_tag` (a NoXxx count field) on an outbound msg.
 *  TYPE_MISMATCH if group_tag is not a group; INVALID_HANDLE on inbound/tombstoned.
 *  Reentrancy: requires-session-lock */
FIXPP_API_EXPORT fixpp_error_t fixpp_msg_group_begin(fixpp_msg_t* msg, uint16_t group_tag,
                                                fixpp_group_builder_t** builder_out);

/** Append a new entry (group instance) to `builder`; returns a writable entry.
 *  Reentrancy: requires-session-lock */
FIXPP_API_EXPORT fixpp_error_t fixpp_group_builder_add_entry(fixpp_group_builder_t* builder,
                                                       fixpp_entry_t** entry_out);

/** Set a STRING field on the current entry. Framing tags → MSG_FRAMING_TAG_FORBIDDEN.
 *  Reentrancy: requires-session-lock */
FIXPP_API_EXPORT fixpp_error_t fixpp_entry_set_string(fixpp_entry_t* entry, uint16_t tag,
                                                 const char* value, size_t len);

/** Set an INTEGER field on the current entry. Reentrancy: requires-session-lock */
FIXPP_API_EXPORT fixpp_error_t fixpp_entry_set_int(fixpp_entry_t* entry, uint16_t tag, int64_t value);

/** Set a DOUBLE field on the current entry. Serialised as locale-independent
 *  fixed-point ASCII (never scientific); FIXPP_ERR_DECIMAL_INVALID for non-finite
 *  or out-of-range values (see fixpp_msg_set_double). Reentrancy: requires-session-lock */
FIXPP_API_EXPORT fixpp_error_t fixpp_entry_set_double(fixpp_entry_t* entry, uint16_t tag, double value);

/** Set a DECIMAL field on the current entry. Reentrancy: requires-session-lock */
FIXPP_API_EXPORT fixpp_error_t fixpp_entry_set_decimal(fixpp_entry_t* entry, uint16_t tag,
                                                  fixpp_decimal_t value);

/** Begin a NESTED group `group_tag` within `entry` (FR-012). Closed by the same
 *  fixpp_msg_group_end under the LIFO contract. TYPE_MISMATCH if not a group.
 *  Reentrancy: requires-session-lock */
FIXPP_API_EXPORT fixpp_error_t fixpp_entry_group_begin(fixpp_entry_t* entry, uint16_t group_tag,
                                                  fixpp_group_builder_t** builder_out);

/** Seal `builder` (must be the innermost open builder — LIFO). Invalidates the
 *  builder + its entry handles. Reentrancy: requires-session-lock */
FIXPP_API_EXPORT fixpp_error_t fixpp_msg_group_end(fixpp_msg_t* msg, fixpp_group_builder_t* builder);

/* ── CA-053: field iteration (US3) ─────────────────────────────────────────── */

/** A single field occurrence returned by fixpp_msg_field_at.
 *  `value` ALIASES the wire buffer (no copy, no free).
 *  Lifetime: valid for the msg handle's dispatch window (inbound) or until
 *  fixpp_msg_destroy (clone). */
typedef struct fixpp_msg_field {
    uint16_t        tag;    /* FIX tag number */
    const uint8_t  *value;  /* aliases the wire buffer; no copy, no free */
    size_t          len;    /* byte length of the value (excludes SOH) */
} fixpp_msg_field_t;

/** Count all field occurrences in wire/document order.
 *  Each repeating-group delimiter and member field is counted individually
 *  (multiset; FR-006).  Writes the total to *count_out.
 *
 *  For clone handles (fixpp_msg_clone), field_count is THREAD_SAFE.
 *
 *  Return codes:
 *    FIXPP_ERR_OK             -- *count_out is valid
 *    FIXPP_ERR_NULL_HANDLE    -- msg or count_out is NULL
 *    FIXPP_ERR_INVALID_HANDLE -- msg is destroyed, type-mismatched, or outbound
 *
 *  Reentrancy: requires-session-lock
 */
FIXPP_API_EXPORT fixpp_error_t fixpp_msg_field_count(const fixpp_msg_t* msg, size_t* count_out);

/** Return the field occurrence at zero-based `index` in wire/document order.
 *  For clone handles (fixpp_msg_clone), field_at is THREAD_SAFE.
 *
 *  Return codes:
 *    FIXPP_ERR_OK                   -- *field_out is valid
 *    FIXPP_ERR_NULL_HANDLE          -- msg or field_out is NULL
 *    FIXPP_ERR_INVALID_HANDLE       -- msg is destroyed, type-mismatched, or outbound
 *    FIXPP_ERR_INDEX_OUT_OF_RANGE   -- index >= field_count
 *
 *  Reentrancy: requires-session-lock
 */
FIXPP_API_EXPORT fixpp_error_t fixpp_msg_field_at(const fixpp_msg_t* msg, size_t index,
                                             fixpp_msg_field_t* field_out);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FIXPP_C_API_MESSAGE_H */
