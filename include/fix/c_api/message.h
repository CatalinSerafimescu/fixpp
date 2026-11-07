/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * include/fix/c_api/message.h — C-ABI message handles (CA-008/CA-009/CA-010, Feature C)
 *
 * 099-fixt-appl-version-validation: fixpp_msg_version returns the resolved version as four
 * bytes, BREAKING in C-ABI 1.14.
 *
 * 100-wire-group-read-integrity: fixpp_msg_get_group and fixpp_group_get_nested_group answer
 * from one ordered table of endings, BREAKING in C-ABI 1.15 (the CA-010 block).
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

/* ── fixpp_resolved_msg_version_t — resolved message version (CA-008) ──────
 *
 * Returned by value through fixpp_msg_version(); nothing aliases the wire. Its
 * bytes are the constants of dict.h, which are not wire values (1137=9 is
 * FIXPP_APPL_VER_FIX50SP2).
 *
 * BREAKING (C-ABI 1.14, [const §X.7]): this struct held BeginString and raw
 * DefaultApplVerID(1137) as pointers into the wire. A caller that read BeginString
 * or raw 1137 reads fixpp_msg_get_string(msg, 8, ...) or
 * fixpp_msg_get_string(msg, 1137, ...).
 */
typedef struct fixpp_resolved_msg_version {
    uint8_t kind;        /* FIXPP_MSG_KIND_SESSION_ADMIN = 0, FIXPP_MSG_KIND_APPLICATION = 1 */
    uint8_t session;     /* FIXPP_SESSION_VER_*, values of dict::session_version */
    uint8_t application; /* FIXPP_APPL_VER_*, values of dict::application_version; UNKNOWN = 0 */
    uint8_t _reserved;   /* 0 */
} fixpp_resolved_msg_version_t;

/* ── CA-008 field read accessors ────────────────────────────────────────────
 *
 * Backing: wire::MessageView<Index>::get(tag) → expected_t<field_view>
 *          (parser.hpp:200); get_decimal(tag, mr) (parser.hpp:215); msg_type()
 *          (parser.hpp:143). Steady-state thunks: abort on any escaping C++
 *          exception ([2i §5.2]). Zero global-heap (SC-003).
 *          Amended in C-ABI 1.15 (100): fixpp_msg_get_group and
 *          fixpp_group_get_nested_group catch the std::bad_alloc of their cursor
 *          shell's allocation and return FIXPP_ERR_WIRE_LIMIT_EXCEEDED (the
 *          CA-010 block); no exception leaves either.
 *
 * Return codes common to all accessors:
 *   FIXPP_ERR_OK              — success
 *   FIXPP_ERR_NULL_HANDLE     — msg or output pointer is NULL
 *   FIXPP_ERR_INVALID_HANDLE  — destroyed / tombstoned / wrong-tag handle
 *                               OR wrong flavour (outbound handle, view==NULL)
 *   FIXPP_ERR_TAG_NOT_FOUND   — the tag is absent from the message
 *
 * BREAKING (C-ABI 1.9): a Length+Data pair a loaded dictionary declares only
 * inside a component or group is now a dictionary pair. This paragraph covers
 * the readers fixpp_msg_get_{string,bytes,int,double,decimal}, fixpp_msg_has_tag,
 * fixpp_msg_version, fixpp_msg_get_msg_type, fixpp_msg_field_count,
 * fixpp_msg_field_at, fixpp_msg_get_group,
 * fixpp_group_get_field_{string,int,double,decimal} and
 * fixpp_group_get_nested_group, on the inbound, clone and toApp views (the last
 * is what fixpp_session_register_send_callback exposes). A counted Data of such
 * a pair can absorb fields these readers used to return: a getter that returned
 * FIXPP_ERR_OK can now return FIXPP_ERR_TAG_NOT_FOUND, and fixpp_msg_get_group
 * can report a changed instance count; fixpp_msg_field_at can go out of range;
 * fixpp_msg_has_tag and fixpp_msg_field_count change value; fixpp_msg_version's
 * kind, read from 35, says application when 35 is absorbed (tag 8 is fixed first
 * by the Framer and cannot be); fixpp_msg_get_msg_type can return FIXPP_ERR_TAG_NOT_FOUND
 * when such a pair precedes 35, which is reachable while the session's
 * validate_inbound_messages is unset, since that header-order check is the only
 * rule placing 35 third. To re-derive whether a C-ABI session can set it, grep
 * validate_inbound_messages in src/capi, and read its default in
 * include/fixpp/session/session_config.hpp.
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

/** Return the message's resolved version, by value: the session's route for a FIXT
 *  application message, read from BeginString for a non-FIXT one.
 *
 *  kind is read from the message's 35 on every message, session from its 8. On a FIXT
 *  application message, application is what the session resolved, or
 *  FIXPP_APPL_VER_UNKNOWN when nothing was, with FIXPP_ERR_OK; on a FIXT or non-FIXT
 *  admin message it is FIXPP_APPL_VER_UNKNOWN; on a non-FIXT application message it
 *  names BeginString's version. A BeginString no FIXPP_SESSION_VER_* names reads
 *  FIXPP_SESSION_VER_UNKNOWN. A clone answers as its source did, and a toApp view reads
 *  FIXPP_APPL_VER_UNKNOWN, since no session wrote it.
 *
 *  @return FIXPP_ERR_OK
 *  @return FIXPP_ERR_NULL_HANDLE     version_out is NULL, or msg is
 *  @return FIXPP_ERR_INVALID_HANDLE  destroyed or wrong-type handle, or an outbound one
 *  @return FIXPP_ERR_TAG_NOT_FOUND   tag 8 is absent: *version_out is all zero
 *
 *  BREAKING (C-ABI 1.14, [const §X.7]): the result's meaning and the struct's layout
 *  changed; the migration line is on fixpp_resolved_msg_version_t.
 *
 *  Reentrancy: thread-safe. On a const message it reads the view and writes only
 *  version_out, and takes no lock. An inbound handle stays valid only until the callback
 *  returns, which this does not extend.
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
 * Backing: wire::OffsetTable::group_slices_status(group_tag) (D-4; 100) — per-instance
 * arena slices materialized once, cached. Per-entry field reads walk the
 * instance slice for the target tag via field_iterator. Nested groups recurse
 * on the sub-slice.
 *
 * All cursors (fixpp_group_t) alias the parent message's parse arena; lifetime is
 * handle-flavour dependent — bounded by the parent fixpp_msg_t's inbound/toApp
 * dispatch window, or (for a detached clone) until fixpp_msg_destroy. NON-owning —
 * do NOT destroy.
 *
 * Error codes of the per-entry field reads (fixpp_group_get_field_*):
 *   FIXPP_ERR_TAG_NOT_FOUND      — the field is absent from the entry
 *   FIXPP_ERR_INDEX_OUT_OF_RANGE — entry_index >= count
 *
 * BREAKING (C-ABI 1.15, [const §X.7]; 100-wire-group-read-integrity, fixpp#541,
 * #644, #647): the two group getters, fixpp_msg_get_group and
 * fixpp_group_get_nested_group, answer from one ordered table; the first
 * condition that holds is the answer. "Registered" means the dictionary
 * registers the tag as a group in the read's context (the message type and the
 * enclosing groups). "Present" means the tag occurs anywhere in the bytes the
 * read covers: the whole message for the flat getter, the whole instance i,
 * nested instances included, for the nested getter (so a tag that occurs only
 * inside a deeper nested instance is present in instance i, and reads row 3 if
 * it is not registered there).
 *   0a  either out-parameter pointer is NULL: FIXPP_ERR_NULL_HANDLE, both
 *       outputs untouched.
 *   0b  both out-parameter pointers are non-NULL, and the handle is unusable or
 *       the instance index is out of range; both outputs are written NULL / 0.
 *       Flat, in this order: a NULL msg -> FIXPP_ERR_NULL_HANDLE; a msg that is
 *       not an inbound message handle (wrong handle tag, or no wire view) ->
 *       FIXPP_ERR_INVALID_HANDLE. Nested, in this order: a NULL g ->
 *       FIXPP_ERR_NULL_HANDLE; i >= the cursor's count ->
 *       FIXPP_ERR_INDEX_OUT_OF_RANGE.
 *   1   the tag is not in the message (flat) or not in instance i (nested), on
 *       an ample or an exhausted arena: FIXPP_ERR_TAG_NOT_FOUND, NULL / 0.
 *   2   the tag is present and registered, and the read could not complete: an
 *       allocation from the per-message arena failed (instance slices, a cache
 *       row, a nested sub-table, the cursor shell), or a group wire limit was
 *       hit (top level: the per-instance entry cap, the nesting depth; nested:
 *       the sub-table's wire_offset_table_full, its entry cap or its index probe
 *       budget). A nested group whose sub-table could not be built is included,
 *       a zero-count one too: FIXPP_ERR_WIRE_LIMIT_EXCEEDED, NULL / 0. A read
 *       that failed answers row 2 on the first and on every repeat read. A
 *       completed read answers row 2 on a repeat only when that call's cursor
 *       shell does not fit.
 *   3   the tag is present and not established as a group: not registered in
 *       this context, wherever it sits and whatever its value; or (nested)
 *       instance i's sub-table build failed for a reason other than an
 *       allocation or a wire limit, such as malformed data (a custom dictionary
 *       whose group delimiter is a Data tag whose Length is the previous
 *       instance's last member splits that Data value differently in the
 *       sub-table): FIXPP_ERR_TYPE_MISMATCH, NULL / 0.
 *   4   the tag is registered and zero instances are read: a declared count of
 *       0 (a value with no leading digit reads as 0), a count field last in its
 *       instance, or a declared count whose next field is not a member:
 *       FIXPP_ERR_OK, *out NULL, *count_out 0.
 *   5   the tag is registered and one or more instances are read: FIXPP_ERR_OK,
 *       the cursor, the instance count.
 * A read is reported failed only for a present, registered tag, so neither an
 * absent tag nor a present scalar answers FIXPP_ERR_WIRE_LIMIT_EXCEEDED; rows 1
 * and 3 need no memory, so an exhausted arena does not change them. A nested
 * group stopped by the per-instance entry cap or the nesting depth
 * (wire_group_too_large) maps to row 2 too, and is defensive: a nested sub-table
 * is built with the default entry cap, which equals the per-instance cap, so an
 * instance that large fails the sub-table's build first; and a parent read that
 * completed bounds the nested depth when the nested read uses the bundle the
 * parent was read with, as both getters do. Row 4 has no cursor, so there is
 * nothing to read with fixpp_group_get_*; a cursor is never released in any
 * case. No exception leaves either getter; other reads over the same arena keep
 * their own documented endings.
 * Changes from 1.14:
 *   - flat, row 4: a registered zero count mid-body or as the last body field,
 *     whether member or non-member fields follow it (a header or trailer
 *     group's count tag holding 0 included), was FIXPP_ERR_TYPE_MISMATCH;
 *   - flat, row 4: a registered lying count (n >= 1, next field not a member)
 *     was FIXPP_ERR_TYPE_MISMATCH (the nested getter already answered OK / 0);
 *   - flat, row 3: a nested group's count tag read at the top level with a
 *     value n >= 1 (NoPartySubIDs(802) outside its NoPartyIDs(453) parent, for
 *     example) was a cursor; with value 0 it was and stays
 *     FIXPP_ERR_TYPE_MISMATCH;
 *   - flat, row 3: a body group read with a value n >= 1 in a context its
 *     dictionary does not declare (a declared message whose entry does not
 *     declare it, or an undeclared message type) was a cursor; it now answers
 *     as QuickFIX getGroup does. Header and trailer groups, at any depth, are
 *     outside this population; a tag the header declares as a scalar and some
 *     message declares as a body group, read in a message type that does not
 *     declare that group, is inside it (QuickFIX reads it as a header scalar);
 *   - flat, row 2: was FIXPP_ERR_TYPE_MISMATCH for instance slices that did not
 *     fit and for a group over a wire limit, and an escaping std::bad_alloc for
 *     the cursor shell;
 *   - nested, row 3: a present scalar on an exhausted arena was
 *     FIXPP_ERR_WIRE_LIMIT_EXCEEDED when the sub-table build or its cache row did
 *     not fit, OK / 0 when both did;
 *   - nested, row 3: a present scalar mid-instance or last in its instance was
 *     OK / 0;
 *   - nested, row 3: a registered count tag read in a context it has no record
 *     for was OK / 0, or a cursor;
 *   - nested, row 1: an absent tag on an exhausted arena was
 *     FIXPP_ERR_WIRE_LIMIT_EXCEEDED or FIXPP_ERR_TAG_NOT_FOUND, depending on
 *     whether the sub-table build and its cache row fitted;
 *   - nested, row 2: the cursor shell was an escaping std::bad_alloc; a nested
 *     sub-table over its entry cap read OK / 0;
 *   - nested, row 0b: a NULL g with both out-parameters non-NULL returned
 *     FIXPP_ERR_NULL_HANDLE without writing them;
 *   - nested, row 3: a present tag in an instance whose sub-table build failed
 *     for malformed data (row 3's cause) was OK / 0;
 *   - both, row 5 (custom dictionaries only): a registered group whose
 *     occurrence the dictionary defines differently from the definition its bare
 *     group store holds for that count tag (a header or trailer group, or a
 *     group nested in one, read in a message type the dictionary does not
 *     declare; a count tag a declared message reaches at a second parent path,
 *     read at the path with no exact context entry) was FIXPP_ERR_TYPE_MISMATCH,
 *     or a group whose instances were lost or mis-split; it is now a cursor
 *     split at the occurrence's own definition, as QuickFIX C++ reads it;
 *   - the session observers (session.h): a frame crafted against the index seed
 *     now fails the parse (session.h's 1.15 notes).
 * Migration from 1.14. Flat getter, FIXPP_ERR_TYPE_MISMATCH: a caller that read
 * it as "zero instances" now gets FIXPP_ERR_OK with count 0 and a NULL cursor
 * for a registered group; a caller that read it as "could not read" now gets
 * FIXPP_ERR_WIRE_LIMIT_EXCEEDED; FIXPP_ERR_TYPE_MISMATCH now means only
 * "present, not a group here". Nested getter, OK / 0: a caller that read it as
 * "scalar or empty" now gets FIXPP_ERR_TYPE_MISMATCH for a scalar, and OK / 0
 * only for a registered group with zero instances. Both getters: test
 * FIXPP_ERR_OK with count 0 for an empty group, FIXPP_ERR_TYPE_MISMATCH for "not
 * a group", FIXPP_ERR_TAG_NOT_FOUND for "absent", and
 * FIXPP_ERR_WIRE_LIMIT_EXCEEDED for "could not complete", which includes a
 * cursor shell that does not fit (in 1.14 an escaping exception).
 */

/** Obtain the group cursor and instance count for group_tag (the NoXxx count field).
 *  *group_out aliases the parent message's parse arena.
 *
 *  Returns, BREAKING in C-ABI 1.15 (the ordered table in the CA-010 block):
 *    FIXPP_ERR_NULL_HANDLE         — group_out or count_out is NULL (outputs
 *                                    untouched), or msg is NULL (outputs NULL / 0)
 *    FIXPP_ERR_INVALID_HANDLE      — msg is not an inbound message handle
 *                                    (outputs NULL / 0)
 *    FIXPP_ERR_TAG_NOT_FOUND       — group_tag is not in the message
 *    FIXPP_ERR_WIRE_LIMIT_EXCEEDED — group_tag is present and registered and the
 *                                    read could not complete: the arena did not
 *                                    fit the instance slices, a cache row or the
 *                                    cursor shell, or an instance exceeded the
 *                                    per-instance entry cap or the nesting depth
 *    FIXPP_ERR_TYPE_MISMATCH       — group_tag is present and not a group in the
 *                                    message's context
 *    FIXPP_ERR_OK                  — a registered group: *count_out instances,
 *                                    *group_out NULL when there are none
 *  No exception leaves this function.
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
 *  Returns, BREAKING in C-ABI 1.15 (the ordered table in the CA-010 block):
 *    FIXPP_ERR_NULL_HANDLE         — nested_out or nested_count_out is NULL
 *                                    (outputs untouched), or g is NULL (outputs
 *                                    NULL / 0)
 *    FIXPP_ERR_INDEX_OUT_OF_RANGE  — i >= g's count (outputs NULL / 0)
 *    FIXPP_ERR_TAG_NOT_FOUND       — nested_tag is not in instance i
 *    FIXPP_ERR_WIRE_LIMIT_EXCEEDED — nested_tag is present in instance i and
 *                                    registered there, and the read could not
 *                                    complete: the arena did not fit the
 *                                    sub-table, a cache row, the instance slices
 *                                    or the cursor shell, or the sub-table's
 *                                    build exceeded its entry cap or its index
 *                                    probe budget (wire_offset_table_full). A
 *                                    nested wire_group_too_large maps here too
 *                                    and is defensive (the CA-010 block).
 *    FIXPP_ERR_TYPE_MISMATCH       — nested_tag is present in instance i and not
 *                                    a group there, or instance i's sub-table
 *                                    build failed for malformed data
 *    FIXPP_ERR_OK                  — a registered group: *nested_count_out
 *                                    instances, *nested_out NULL when there are
 *                                    none
 *  No exception leaves this function.
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
 *    FIXPP_ERR_WIRE_CONFORMANCE -- msg_type is empty or holds SOH (0x01); a session
 *                               with a dictionary reports DICT_CONFIG first.
 *                               (1.6, BREAKING: a session without a dictionary
 *                               used to accept it.)
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
 *  (1.7, BREAKING), two limbs:
 *
 *  Limb 1 -- a DICT-BACKED source whose re-parse of the copied frame fails
 *  now REFUSES: no clone handle, *clone_out stays NULL, and the source
 *  handle is unchanged and still usable. Before 1.7 this silently returned
 *  FIXPP_ERR_OK with a dictionary-free clone. The code is the caller-visible
 *  translation of the wire failure that caused the re-parse to fail; the
 *  three routes reachable today are FIXPP_ERR_UNKNOWN (an out-of-memory
 *  allocation failure -- documented v1.0 behaviour, see the live
 *  spec/behaviors-and-limitations.md L-049-2), FIXPP_ERR_WIRE_LIMIT_EXCEEDED
 *  (a capacity or tag-range failure), and FIXPP_ERR_WIRE_INVALID_FRAME (a
 *  malformed field). This is not a closed list -- any future route added to
 *  the engine's internal error translation reaches the caller the same way.
 *
 *  Limb 2 -- a non-allocation exception raised during clone's own
 *  construction now terminates the process after a fatal log, instead of
 *  returning FIXPP_ERR_CAPI_CONFIG_INVALID. Its trigger set is NOT
 *  enumerated: whether such an exception can be produced on this path at
 *  all is undecided; the behaviour change is declared regardless, because a
 *  return becoming a process abort is observable on this symbol whatever
 *  its reachability.
 *
 *  Return codes:
 *    FIXPP_ERR_OK                    -- success; *clone_out is live
 *    FIXPP_ERR_NULL_HANDLE            -- src or clone_out is NULL
 *    FIXPP_ERR_INVALID_HANDLE         -- src is destroyed / tombstoned, or is an
 *                                        outbound accumulator handle (no wire view)
 *    FIXPP_ERR_CAPI_CONFIG_INVALID    -- std::bad_alloc during clone construction
 *                                        (preserved from before 1.7, narrowed)
 *    FIXPP_ERR_UNKNOWN                -- (1.7, BREAKING) limb 1: dict-backed
 *                                        re-parse failed, out-of-memory route
 *    FIXPP_ERR_WIRE_LIMIT_EXCEEDED    -- (1.7, BREAKING) limb 1: dict-backed
 *                                        re-parse failed, capacity/range route
 *    FIXPP_ERR_WIRE_INVALID_FRAME     -- (1.7, BREAKING) limb 1: dict-backed
 *                                        re-parse failed, malformed-field route
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
 *    FIXPP_ERR_WIRE_CONFORMANCE          -- (1.6, BREAKING) value holds SOH (0x01) and `tag` is not
 *                                           the Data half of a Length+Data pair; nothing
 *                                           is written
 *    (note: set_string is always-OK on any dict field type -- no TYPE_MISMATCH)
 *
 *  Reentrancy: requires-session-lock
 */
FIXPP_API_EXPORT fixpp_error_t fixpp_msg_set_string(fixpp_msg_t* msg, uint16_t tag,
                                               const char* value, size_t len);

/** Set a raw-bytes field (type-agnostic escape hatch for Data or extension tags).
 *  No dictionary type check; framing-tag check still applies.
 *  Since 1.6, fixpp_msg_commit refuses a malformed Length+Data pair, or SOH outside a
 *  Data value, whichever setter wrote the field. For a Data field prefer
 *  fixpp_msg_set_data, which writes the Length too.
 *
 *  Reentrancy: requires-session-lock
 */
FIXPP_API_EXPORT fixpp_error_t fixpp_msg_set_bytes(fixpp_msg_t* msg, uint16_t tag,
                                              const uint8_t* bytes, size_t len);

/** Set a Length+Data pair (1.6). `data_tag` is the Data field; its Length field is
 *  written from `len`.
 *
 *  The bytes are copied verbatim and may hold any value, SOH included: the Length
 *  makes them framing-safe. The Length paired with `data_tag` comes from the FIX
 *  standard or, for a pair the standard does not define, from the session's
 *  dictionary.
 *
 *  With neither half present, appends the Length then the Data. With the Length
 *  immediately followed by the Data, overwrites both in place. Any other state (one
 *  half only, the halves apart, or the Data first) is refused and nothing is
 *  written; remove the stray half first. No existing field moves, so open group
 *  builders stay valid.
 *
 *  BREAKING (C-ABI 1.9): a Length+Data pair a loaded dictionary declares only
 *  inside a component or group is now a dictionary pair. For the Data tag of
 *  such a pair this call returned FIXPP_ERR_TYPE_MISMATCH (the tag was not a
 *  Data half); that refusal no longer fires, and the call reaches the later
 *  refusals below, e.g. FIXPP_ERR_WIRE_CONFORMANCE for len == 0, or
 *  FIXPP_ERR_DICT_CONFIG for a half not declared for this MsgType.
 *
 *  Return codes:
 *    FIXPP_ERR_OK                        -- success
 *    FIXPP_ERR_NULL_HANDLE               -- msg or bytes is NULL
 *    FIXPP_ERR_INVALID_HANDLE            -- msg is destroyed / session closed
 *    FIXPP_ERR_MSG_FRAMING_TAG_FORBIDDEN -- data_tag, or the Length tag the pair
 *                                           gives it, is a framing tag
 *    FIXPP_ERR_TYPE_MISMATCH             -- data_tag is not the Data half of a pair, a
 *                                           half collides with a group, or the pair's
 *                                           current state is not Length-then-Data
 *    FIXPP_ERR_WIRE_CONFORMANCE          -- len is 0 (an empty Data value is malformed)
 *    FIXPP_ERR_DICT_CONFIG               -- a half is not declared for this MsgType
 *
 *  Reentrancy: requires-session-lock
 */
FIXPP_API_EXPORT fixpp_error_t fixpp_msg_set_data(fixpp_msg_t* msg, uint16_t data_tag,
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

/** Remove a tag from the accumulator.
 *
 *  Idempotent (absent returns OK) ONLY when no group builder is open --
 *  (1.7, BREAKING): an open builder holds an INDEX into `entries` (or a
 *  parent instance's fields), and erasing shifts every later index, so the
 *  call refuses instead of erasing while ANY group builder is open -- even
 *  for the two classes that are individually harmless: an absent tag
 *  (nothing would move) and a present tag positioned after every live
 *  root's group entry (erase would not shift it). The refusal is keyed on
 *  the builder stack being non-empty, not on the erased tag or its
 *  position, so no tag choice defeats it.
 *
 *  Return codes:
 *    FIXPP_ERR_OK              -- erased (or absent), no group builder open
 *    FIXPP_ERR_NULL_HANDLE     -- msg is NULL
 *    FIXPP_ERR_INVALID_HANDLE  -- msg is destroyed/inbound/session closed, or
 *                                 (1.7, BREAKING) a group builder is open
 *
 *  Reentrancy: requires-session-lock
 */
FIXPP_API_EXPORT fixpp_error_t fixpp_msg_remove_tag(fixpp_msg_t* msg, uint16_t tag);

/** Serialise the accumulator into an app-payload span.
 *
 *  The payload is: 35=type SOH tag=value SOH ... (no framing tags 8/9/34/49/52/56/10).
 *  *payload_out aliases the per-message arena; valid until the next mutation or destroy.
 *
 *  BREAKING (C-ABI 1.9): a Length+Data pair a loaded dictionary declares only
 *  inside a component or group is now a dictionary pair. A malformed pair of
 *  that kind, which returned FIXPP_ERR_OK, now returns
 *  FIXPP_ERR_WIRE_CONFORMANCE.
 *
 *  BREAKING (C-ABI 1.9, FR-021), a cause separate from the loader's above: a
 *  group's count field is checked as a field with an empty value, so a group
 *  whose count tag is the Length or the Data half of a pair (the FIX
 *  standard's, or the session dictionary's) is not read as that half. Such a
 *  group, which returned FIXPP_ERR_OK when its instance-count digits completed
 *  the pair, now returns FIXPP_ERR_WIRE_CONFORMANCE.
 *
 *  Return codes:
 *    FIXPP_ERR_OK              -- success
 *    FIXPP_ERR_NULL_HANDLE     -- msg or payload_out or len_out is NULL
 *    FIXPP_ERR_INVALID_HANDLE  -- msg is destroyed or session closed or open group
 *    FIXPP_ERR_TYPE_MISMATCH   -- group grammar violated (empty instance or
 *                                 non-delimiter-first instance; INV-4)
 *    FIXPP_ERR_WIRE_LIMIT_EXCEEDED -- serialised body exceeds ~3800 B
 *    FIXPP_ERR_WIRE_CONFORMANCE    -- (1.6, BREAKING) the output would be malformed, in any group
 *                                     instance too: a Data field not immediately after
 *                                     its Length, a Length not immediately before its
 *                                     Data, a Length that is not positive ASCII digits
 *                                     equal to the Data byte count (leading zeros are
 *                                     accepted), an empty Data value, SOH in a field
 *                                     that is not a Data field, or (1.9, BREAKING) a
 *                                     group whose count tag is the Length or the Data
 *                                     half of a pair
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
 *      -> fixpp_entry_set_{string,int,double,decimal,data}(entry, tag, …) [per field]
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
 *  FIXPP_ERR_INVALID_HANDLE, assessed-unreachable defence-in-depth
 *  ([const §IX.1], not BREAKING — msg-index-bounds.md), if the builder's
 *  resolved group index is out of range for the container it names.
 *  Reentrancy: requires-session-lock */
FIXPP_API_EXPORT fixpp_error_t fixpp_group_builder_add_entry(fixpp_group_builder_t* builder,
                                                       fixpp_entry_t** entry_out);

/** Set a STRING field on the current entry. Framing tags → MSG_FRAMING_TAG_FORBIDDEN.
 *  Since 1.6 (BREAKING), a value holding SOH (0x01) on a tag that is not the Data half of a
 *  Length+Data pair → FIXPP_ERR_WIRE_CONFORMANCE, nothing written.
 *  FIXPP_ERR_INVALID_HANDLE, assessed-unreachable defence-in-depth
 *  ([const §IX.1], not BREAKING — msg-index-bounds.md), if the entry's
 *  resolved group instance index is out of range.
 *  Reentrancy: requires-session-lock */
FIXPP_API_EXPORT fixpp_error_t fixpp_entry_set_string(fixpp_entry_t* entry, uint16_t tag,
                                                 const char* value, size_t len);

/** Set a Length+Data pair on the current entry (1.6): fixpp_msg_set_data's contract
 *  on this group instance, except that no MsgType-grammar check runs (as for every
 *  entry setter) and a Data field that is the group's delimiter →
 *  FIXPP_ERR_TYPE_MISMATCH, since its Length would have to come first.
 *  BREAKING (C-ABI 1.9): a Length+Data pair a loaded dictionary declares only
 *  inside a component or group is now a dictionary pair. For the Data tag of
 *  such a pair this call returned FIXPP_ERR_TYPE_MISMATCH (the tag was not a
 *  Data half); that refusal no longer fires, and the call reaches the later
 *  refusals of fixpp_msg_set_data's contract, e.g. FIXPP_ERR_WIRE_CONFORMANCE
 *  for len == 0.
 *  FIXPP_ERR_INVALID_HANDLE, assessed-unreachable defence-in-depth
 *  ([const §IX.1], not BREAKING — msg-index-bounds.md), if the entry's
 *  resolved group or its resolved instance index is out of range.
 *  Reentrancy: requires-session-lock */
FIXPP_API_EXPORT fixpp_error_t fixpp_entry_set_data(fixpp_entry_t* entry, uint16_t data_tag,
                                               const uint8_t* bytes, size_t len);

/** Set an INTEGER field on the current entry.
 *  FIXPP_ERR_INVALID_HANDLE, assessed-unreachable defence-in-depth
 *  ([const §IX.1], not BREAKING — msg-index-bounds.md), if the entry's
 *  resolved group instance index is out of range.
 *  Reentrancy: requires-session-lock */
FIXPP_API_EXPORT fixpp_error_t fixpp_entry_set_int(fixpp_entry_t* entry, uint16_t tag, int64_t value);

/** Set a DOUBLE field on the current entry. Serialised as locale-independent
 *  fixed-point ASCII (never scientific); FIXPP_ERR_DECIMAL_INVALID for non-finite
 *  or out-of-range values (see fixpp_msg_set_double).
 *  FIXPP_ERR_INVALID_HANDLE, assessed-unreachable defence-in-depth
 *  ([const §IX.1], not BREAKING — msg-index-bounds.md), if the entry's
 *  resolved group instance index is out of range.
 *  Reentrancy: requires-session-lock */
FIXPP_API_EXPORT fixpp_error_t fixpp_entry_set_double(fixpp_entry_t* entry, uint16_t tag, double value);

/** Set a DECIMAL field on the current entry.
 *  FIXPP_ERR_INVALID_HANDLE, assessed-unreachable defence-in-depth
 *  ([const §IX.1], not BREAKING — msg-index-bounds.md), if the entry's
 *  resolved group instance index is out of range.
 *  Reentrancy: requires-session-lock */
FIXPP_API_EXPORT fixpp_error_t fixpp_entry_set_decimal(fixpp_entry_t* entry, uint16_t tag,
                                                  fixpp_decimal_t value);

/** Begin a NESTED group `group_tag` within `entry` (FR-012). Closed by the same
 *  fixpp_msg_group_end under the LIFO contract. TYPE_MISMATCH if not a group.
 *  FIXPP_ERR_INVALID_HANDLE, assessed-unreachable defence-in-depth
 *  ([const §IX.1], not BREAKING — msg-index-bounds.md), if the entry's
 *  resolved group instance index is out of range.
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
