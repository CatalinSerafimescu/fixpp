/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * include/fix/c_api/version.h — C-ABI version detection surface (CA-004)
 *
 * [2i §4.5] / data-model E-4.  C-clean: no C++ syntax.
 *
 * Two independent version tracks ([arch §9.2]):
 *   fixpp_version()          → C-ABI surface version (MAJOR.MINOR.PATCH below)
 *   fixpp_library_version()  → C++ library SemVer (currently 0.0.1)
 *
 * A C-ABI consumer MUST check fixpp_version().major == FIXPP_C_ABI_VERSION_MAJOR
 * for major-compatibility; minor/patch differences are backward-compatible within
 * the same major per [const §X.4].
 *
 * Reentrancy: both accessors are thread-safe (value return, no global mutation).
 */

#ifndef FIXPP_C_API_VERSION_H
#define FIXPP_C_API_VERSION_H

/* NOLINTBEGIN(hicpp-deprecated-headers,modernize-deprecated-headers):
   C-ABI header; C-style includes are correct for pure-C consumers. */
#include <stdint.h>
/* NOLINTEND(hicpp-deprecated-headers,modernize-deprecated-headers) */

#include <fix/c_api/export.h>

/* ── Version macros ─────────────────────────────────────────────────────────
   NOLINTBEGIN(cppcoreguidelines-macro-usage,cppcoreguidelines-macro-to-enum,
               modernize-macro-to-enum) */

/** C-ABI surface version — bumped independently of the C++ library version.
 *  GA freeze: MAJOR 0->1 froze the C-ABI surface's shape ([const §X.1]). Until
 *  fixpp's first public release a breaking change bumps MINOR and is declared
 *  BREAKING ([const §X.7]); additive changes bump MINOR too. The compatibility
 *  promise starts at the first public release, where this version resets to
 *  1.0.0; from then on a breaking change requires a MAJOR bump.
 *
 *  MINOR is PRESERVED at 5 across the freeze (not reset to 0): it is the fifth
 *  additive minor of the C-ABI (0.1..0.5), and the forward-compat error-code
 *  downgrade ([const §X.4] / src/capi/error.cpp introducing_minor) is keyed on
 *  this minor. Resetting it to 0 would place the current version BELOW the
 *  introducing_minor (2/4) of already-published codes, so a conforming consumer
 *  would see those codes downgraded to UNKNOWN — an incoherent baseline. 1.5.0
 *  keeps the downgrade frame continuous. It is a review baseline, not a
 *  compatibility promise: the first stable ABI is the 1.0.0 of the first
 *  public release.
 *
 *  Evolution of the downgrade frame:
 *    - Future MINORs within major 1 (1.6, 1.7, ...) just continue: a new code
 *      gets introducing_minor = the minor it appears at, so an older consumer
 *      forward-compat-downgrades it to UNKNOWN. No reset needed.
 *    - The first public release ([const §X.7]) resets the version to 1.0.0 and
 *      rebases the introducing_minor table exactly as the next bullet describes;
 *      no consumer predates it, so no conforming consumer sees a downgrade.
 *    - The next BREAKING MAJOR (2.0.0) is where MINOR resets to 0 AND the
 *      introducing_minor table is rebased so every surviving code is the 2.0
 *      baseline (introducing_minor 0); new 2.x codes gate at 1,2,... A major
 *      bump is allowed to break, and a 1.x consumer is already refused by the
 *      major==major check, so the reset is free and coherent there — unlike at
 *      this non-breaking 0->1 freeze, where preserving MINOR is required.
 *
 *  This is the C-ABI SURFACE version only — fixpp_library_version() (the C++
 *  SemVer) is unaffected. Byte-frozen by tools/check_capi_freeze.sh (NBC-1). */

/*  1.9 (BREAKING, [const §X.7]; 091, fixpp#418): a Length+Data pair a loaded
 *  dictionary declares only inside a component or group is now a dictionary
 *  pair. The effects that have a carrying declaration are noted on it (dict.h
 *  fixpp_dict_load_from_xml; message.h fixpp_msg_commit, fixpp_msg_set_data,
 *  fixpp_entry_set_data and the accessor preamble; session.h
 *  fixpp_session_send, fixpp_session_register_callback,
 *  fixpp_session_register_send_callback). The effects with no carrying
 *  declaration are recorded here:
 *    - a frame a pre-1.9 engine stored with a malformed pair of that kind now
 *      fails replay (build_replay_frame) and is gap-filled rather than resent;
 *    - the session's header and Logon scans (scan_frame_header, interpret_logon,
 *      the store's frame_has_genuine_tag554 masking) read such a Data by count.
 *  interpret_logon refuses a Logon carrying a Length immediately followed by
 *  its paired Data whose counted extent reaches or passes the end of the
 *  whole framed message, or whose following byte is not SOH, whether of a
 *  component/group-only pair or of a standard pair such as
 *  RawDataLength(95) and RawData(96) (so shipped dictionaries are affected
 *  too): a Logon of that shape that was accepted is now refused, on either
 *  role. Amended in C-ABI 1.11 (093): such a Logon whose third field is not
 *  MsgType(35) is disregarded before interpret_logon reads it (contract C-2
 *  step 1), and is not refused. The observers are every call whose result depends on the session
 *  having logged on; each also carries its own note in session.h:
 *  fixpp_session_is_established stays false; fixpp_session_close, once the
 *  refused session has drained, returns FIXPP_ERR_THREAD_SESSION_LIFECYCLE,
 *  translated for the consumer's ABI minor (fixpp_engine_create), not
 *  FIXPP_ERR_OK; fixpp_session_send on that session returns
 *  FIXPP_ERR_SESSION_INVALID_STATE, translated for the consumer's ABI minor
 *  (fixpp_engine_create), not FIXPP_ERR_OK; neither the receive
 *  callback (fixpp_session_register_callback) nor the toApp callback
 *  (fixpp_session_register_send_callback) is ever invoked for it.
 *  Independently of the loader change that opens this entry,
 *  fixpp_msg_commit (FR-021, fixpp#506) checks a group's count field as a
 *  field with an empty value, so a group whose count tag is the Length or the
 *  Data half of a pair, a standard pair or one the session dictionary
 *  declares, which returned FIXPP_ERR_OK when its instance-count digits completed the pair,
 *  now returns FIXPP_ERR_WIRE_CONFORMANCE; this is reachable on every session
 *  with no dictionary, and on a dictionary session only where that dictionary
 *  declares a group on a tag that is a pair half.
 *  No error code is added. */

/*  1.10 (BREAKING, [const §X.7]; 092, fixpp#507): the session disposes of an
 *  inbound frame whose header scan meets a malformed tag (a non-digit tag
 *  byte, an empty tag, a tag above 65535, or a field with no '=' before its
 *  SOH) or a Length+Data mismatch before any handler reads the frame, and the
 *  expected inbound sequence number no longer wraps (FR-019): an inbound
 *  message that would advance it past its maximum (4294967295), where it
 *  wrapped to 0 and the session continued, ends the session with no Reject
 *  and no Logout. The observers are every call whose result depends on the
 *  session being logged on: fixpp_session_is_established,
 *  fixpp_session_close, fixpp_session_send, fixpp_session_register_callback
 *  and fixpp_session_register_send_callback. The effects that have a
 *  carrying declaration, FR-019's included, are noted on those declarations
 *  in session.h. The effects with no carrying declaration are recorded here:
 *    - on an established session, a frame with a Length+Data mismatch that
 *      ended the session is disregarded, with no Reject and no advance of the
 *      expected inbound sequence number, when the mismatch comes before
 *      MsgSeqNum(34) or the frame's third field is not MsgType(35);
 *    - once fixpp has sent a Logout, a faulty frame is not taken as the
 *      peer's Logout reply; fixpp_session_close returns the same code, at
 *      the latest when the logout timeout runs out;
 *    - on an established session that has not sent a Logout, a faulty frame
 *      whose third field is MsgType(35) and whose MsgSeqNum(34) was read
 *      before the fault, which ended the session at the CompID or BeginString
 *      check, the SendingTime check, the Logout handler or the too-low
 *      sequence check, draws a session Reject and the session stays up;
 *    - outside a resend recovery, on an established session that has not
 *      sent a Logout, a faulty SequenceReset whose third field is MsgType(35)
 *      and whose MsgSeqNum(34) was read before the fault is Rejected and its
 *      NewSeqNo(36) is not applied;
 *    - on an established session that has not sent a Logout, a faulty
 *      application message whose third field is MsgType(35) and whose
 *      MsgSeqNum(34) was read before the fault draws a session Reject on the
 *      wire; the receive callback is not invoked for it either way.
 *  No error code is added. */

/*  1.11 (BREAKING, [const §X.7]; 093, fixpp#514, #515, #516): how the session
 *  frames inbound bytes and which frames it acts on. The observers are the
 *  five calls the 1.10 entry names: fixpp_session_is_established,
 *  fixpp_session_close, fixpp_session_send, fixpp_session_register_callback
 *  and fixpp_session_register_send_callback. Each carries every change below
 *  in its 1.11 clause in session.h, where the 1.10 sentences 093 falsifies
 *  are amended in place, marked 1.11. One line per change:
 *    - a frame the Framer finds garbled is disregarded and counted, not
 *      session-ending (the five observers);
 *    - a frame whose third field is not MsgType(35) is disregarded, and a
 *      Logon of that shape is not refused (the five observers; the amended
 *      1.10 Logon sentence);
 *    - a frame over the inbound limit closes the session in every state, and
 *      one within it is admitted however the stream is split into reads (the
 *      five observers);
 *    - a frame the session admits always parses for dispatch (amended in C-ABI 1.15, 100: not
 *      one crafted against the index seed, which fails the parse; the 1.15
 *      entry) (the five observers; the amended 1.10 parse-failure bullet);
 *    - a connection not logged on by the establishment timeout is closed (the
 *      five observers);
 *    - every frame that passes the fault and third-field checks counts as
 *      inbound traffic for the heartbeat interval (the five observers).
 *  Added: fixpp_session_config_set_logon_timeout_ms and
 *  fixpp_session_garbled_frame_count (session.h).
 *  No error code is added. */

/*  1.12 (BREAKING, [const §X.7]; fixpp#536, #537, #538): what the session
 *  does with a frame above the expected inbound sequence number and how a gap
 *  the peer does not fill ends (#537), what one session's close does to the
 *  other sessions of its engine (#536), and what a resend answer holds after
 *  a Logon exchange that reset the sequence numbers (#538). The observers are
 *  the calls the 1.10 entry names: fixpp_session_is_established,
 *  fixpp_session_close, fixpp_session_send, fixpp_session_register_callback
 *  and fixpp_session_register_send_callback. Each carries the changes named
 *  for it below in its 1.12 clause in session.h, where the 1.10 sentences
 *  fixpp#537 falsifies are amended in place, marked 1.12. One line per
 *  change:
 *    - a frame above the expected number that arrives while a resend is
 *      outstanding is ignored, not session-ending (every observer; the
 *      amended 1.10 bullets);
 *    - with a heartbeat interval set, a gap the peer does not fill ends with
 *      a Logout once the expected number has not advanced for two heartbeat
 *      intervals; an interval of 0 turns that deadline off, and a session
 *      whose interval was never set has it, at the session's default
 *      interval (every observer; the amended 1.10 bullets);
 *    - where the session's CompIDs leave no room for that Logout's text, the
 *      gap ends at the same point with no Logout (every observer);
 *    - a Heartbeat above the expected number answers a TestRequest the
 *      session is waiting on, during a resend or as the frame that opens the
 *      gap, and one that carries another TestReqID(112) ends the session at
 *      that frame (every observer);
 *    - a ResendRequest the session cannot build ends the session and closes
 *      the connection (every observer);
 *    - a session's close, and the peer's answer to a close's Logout, no
 *      longer stop the Heartbeats and TestRequests of the engine's other
 *      sessions, and another session's close no longer ends a session's wait
 *      for the answer to its own Logout early: corrections
 *      (fixpp_session_close, fixpp_session_is_established);
 *    - after a Logon exchange that reset the sequence numbers and left the
 *      first outbound number at 2, a message sent is resent on the peer's
 *      ResendRequest, not gap-filled: a correction (fixpp_session_send).
 *  The effects with no carrying declaration are recorded here:
 *    - a store read that fails while the session answers a ResendRequest ends
 *      the session, where the slot was gap-filled. No reach through the C
 *      ABI as shipped: a session opened through it runs on the in-memory
 *      store;
 *    - during fixpp_engine_stop, where an inbound frame's processing meets
 *      the stop, fixpp_session_is_established turns false before the peer's
 *      connection closes, where the connection closed first. No declaration
 *      orders the two, and both orders occur under fixpp_engine_stop before
 *      and after this entry: a change of output the documentation leaves
 *      unspecified;
 *    - the code a cancelled inbound frame returns inside the engine changes.
 *      No reach: no C function receives it;
 *    - an outbound frame whose store call is cancelled is not transmitted,
 *      the send fails and the session ends, where on one timing the frame
 *      was transmitted and the send succeeded. No reach through the C ABI as
 *      shipped: a session opened through it runs on the in-memory store, and
 *      neither fixpp_session_send nor fixpp_session_close can be cancelled
 *      by its caller;
 *    - a close whose Logout was not sent does not wait for the logout
 *      timeout. Through the C ABI as shipped no fixpp_session_close returns
 *      sooner for it;
 *    - a send whose sequence number was taken back while its frame was being
 *      stored is not transmitted and fails. No reach through the C ABI as
 *      shipped.
 *  No symbol, signature or error code is added. */

/*  1.13 (additive; 098-dialect-overlay): a base dictionary loaded with one
 *  dialect overlay. The new function returns the DICT codes
 *  (FIXPP_ERR_DICT_CONFIG, FIXPP_ERR_DICT_LIMIT_EXCEEDED, FIXPP_ERR_DICT_OOM);
 *  fixpp_dict_load_from_xml is unchanged. No error code is added.
 *  Added: fixpp_dict_load_from_xml_with_overlay (dict.h). */
/*  1.14 (BREAKING, [const §X.7]; 099-fixt-appl-version-validation): fixpp_msg_version
 *  and fixpp_resolved_msg_version_t (message.h). The struct is four bytes, {kind, session,
 *  application, _reserved}, the resolved version of the message, where it held BeginString
 *  and raw DefaultApplVerID(1137) as pointers into the wire. The symbol, its signature and
 *  its handle errors stay.
 *  Migration: a caller that read BeginString or raw 1137 reads
 *  fixpp_msg_get_string(msg, 8, ...) or fixpp_msg_get_string(msg, 1137, ...). Those are
 *  wire values, and the struct's bytes are the constants of dict.h, which do not coincide.
 *  What a FIXT session validates by changes with this minor; the setters below are what
 *  reach it from C. A refused config is reported by fixpp_session_open, and a table build
 *  failure by fixpp_engine_start (session.h, engine.h).
 *  Added: fixpp_session_config_set_default_appl_ver_id,
 *  fixpp_session_config_set_validate_inbound_messages (session.h),
 *  fixpp_engine_config_add_dictionary (engine.h), and the constants FIXPP_APPL_VER_*,
 *  FIXPP_SESSION_VER_* and FIXPP_MSG_KIND_* (dict.h). No error code is added. */
/*  1.15 (BREAKING, [const §X.7]; 100-wire-group-read-integrity, fixpp#541, #644,
 *  #645, #647): fixpp_msg_get_group and fixpp_group_get_nested_group answer from
 *  one ordered table of endings (message.h, the CA-010 block, which carries the
 *  changes from 1.14 and the migration). A registered group with zero instances
 *  reads FIXPP_ERR_OK with count 0 on both getters, where the flat getter
 *  answered FIXPP_ERR_TYPE_MISMATCH. A group read that could not complete (an
 *  arena allocation, a group wire limit, the cursor shell) reads
 *  FIXPP_ERR_WIRE_LIMIT_EXCEEDED on both, where the flat getter answered
 *  FIXPP_ERR_TYPE_MISMATCH or let std::bad_alloc escape; no exception leaves
 *  either getter. A tag present but not registered as a group in the read's
 *  context reads FIXPP_ERR_TYPE_MISMATCH on both, where the flat getter could
 *  return a cursor and the nested getter FIXPP_ERR_OK with count 0. The nested
 *  getter writes NULL / 0 to both outputs for a NULL g.
 *  The session observers, fixpp_session_is_established, fixpp_session_close,
 *  fixpp_session_send, fixpp_session_register_callback and
 *  fixpp_session_register_send_callback: a frame crafted against the index
 *  seed, whose index build exceeds its probe budget, fails the parse and ends
 *  the session with no Reject, no Logout and no callback, where it parsed with
 *  a field missing from its index. Their 1.11 clauses in session.h, and the
 *  1.11 entry above, are amended in place, marked 1.15.
 *  No symbol, signature or error code is added. */
/*  1.16 (BREAKING, [const §X.7]; 102-dict-width-multichar-type, fixpp#654): the two
 *  dictionary loads return another result for some inputs, and every call that reads a
 *  dictionary in two regions answers from a corrected one. No symbol, signature, type or
 *  error code changes. Three kinds (dict.h carries each load's own note):
 *  - a failure turned into a success: fixpp_dict_load_from_xml_with_overlay, an overlay
 *    whose only refusal was a component or group side table that would stop fitting 16
 *    bits (was FIXPP_ERR_DICT_LIMIT_EXCEEDED), or an Orchestra input whose only refusal was
 *    a component, group, componentRef or groupRef id in 65536..2147483647 (was
 *    FIXPP_ERR_DICT_CONFIG); fixpp_dict_load_from_xml, such an id (was
 *    FIXPP_ERR_CAPI_CONFIG_INVALID). An id above 2147483647 is still refused;
 *  - the same code, and a different dictionary: a base of more than 65,535 components,
 *    whose component identities and message expansions through a componentRef past that
 *    point are now their own, and with them every table a session builds from the
 *    dictionary (the valid-tag and required sets, the group contexts, the Length+Data
 *    pairs, the header store and the section records); a base whose group side table
 *    passes UINT16_MAX entries, whose groups' members and bare member store are now their own;
 *  - a failure turned into a different failure: on the overlay call, that side-table fault
 *    together with a fault the guard detects after it now returns that fault's code; a
 *    high-id Orchestra input refused by a guard the id moved now returns the expansion
 *    budget's FIXPP_ERR_DICT_LIMIT_EXCEEDED, or goes on to the later guards where an
 *    overlay's restatement of such a base definition was counted as new.
 *  The readers, builders and setters that read such a dictionary are marked in message.h
 *  (the C-ABI 1.16 paragraph beside the 1.9 reader paragraph), and the session calls and
 *  fixpp_engine_start in session.h (its C-ABI 1.16 paragraph). What no declaration carries,
 *  the counterparty's side: a validating session over such a dictionary can send a Reject it
 *  did not send, or not send one it sent, and the header placement and the Length+Data carry
 *  of the frames a session sends follow the corrected header store and pairs.
 *  Migration: a consumer that relied on these inputs being refused checks the dictionary's
 *  content itself; no code that handles the refusal needs to change. */
/*  1.17 (BREAKING, [const §X.7]; 103-resend-logon-reset-fidelity): the default reset
 *  policy, and how a session ends a recovery. No symbol, signature, type or error code
 *  changes. A consumer that never calls fixpp_session_config_set_reset_seqnum_policy now
 *  runs FIXPP_RESET_SEQNUM_BILATERAL_LENIENT, where it ran STRICT: its initiator's Logon
 *  carries no 141=Y unless a reset setting is on and both numbers are 1, and its
 *  acceptor accepts a Logon without 141=Y. The observers, one line per change, each
 *  carried by the session.h declarations named (each has a C-ABI 1.17 clause):
 *  - the default: fixpp_reset_seqnum_policy, fixpp_session_config_create,
 *    fixpp_session_config_set_reset_seqnum_policy;
 *  - a STRICT acceptor refuses a Logon without 141=Y with a Logout carrying Text, and
 *    with reset_on_logon does not reset for it: fixpp_reset_seqnum_policy,
 *    fixpp_session_config_set_reset_on_logon;
 *  - a default session establishes where STRICT refused it: fixpp_session_is_established,
 *    fixpp_session_close, fixpp_session_register_callback,
 *    fixpp_session_register_send_callback;
 *  - a too-high Logout is acknowledged after a bounded recovery, and a send is refused
 *    while it is pending and during any Logout ending: fixpp_session_is_established,
 *    fixpp_session_close, fixpp_session_send, and the two register calls;
 *  - the gap deadline acts on every frame that does not advance the expected number, and
 *    a too-low Heartbeat without PossDupFlag(43)=Y ends the session with a Logout:
 *    fixpp_session_is_established and the two register calls;
 *  - sends and a graceful close's Logout wait for a running resend answer:
 *    fixpp_session_send, fixpp_session_close.
 *  The 1.12 sentences these change are amended in place in each observer, marked "amended
 *  in C-ABI 1.17, 103". What no declaration carries, the counterparty's side: a
 *  ResendRequest with BeginSeqNo(7) = 0 is answered as one with 7 = 1, and one without
 *  tag 7 is Rejected; a too-high ResendRequest is answered; every too-low frame without
 *  43=Y draws a Logout with Text; liveness, Logout and in-flight sends are ordered around
 *  a resend answer; resend state clears with its connection (no reach: the C ABI has no
 *  reconnect call). Migration: a consumer that wants the former default calls
 *  fixpp_session_config_set_reset_seqnum_policy with FIXPP_RESET_SEQNUM_BILATERAL_STRICT,
 *  whose 1.17 meaning fixpp_reset_seqnum_policy's clause states. */
/*  1.18 (BREAKING, [const §X.7]; 105-transport-liveness): how a session ends, and when its
 *  connection closes. No symbol, signature, type or error code changes. The observers, each
 *  with a C-ABI 1.18 clause in session.h: fixpp_session_is_established, fixpp_session_send,
 *  fixpp_session_close, fixpp_session_register_callback and
 *  fixpp_session_register_send_callback. The 1.11, 1.12 and 1.17 sentences these change are
 *  amended in place in each observer, marked "amended in C-ABI 1.18, 105". What no
 *  declaration carries, the counterparty's side:
 *  - every ending closes the connection, with or without a Logout, before or after the
 *    session established, where some endings left it open;
 *  - a TestRequest the peer leaves unanswered ends the session with a Logout whose Text(58)
 *    is "TestRequest unanswered, TestReqID <id>", and the connection closes once that
 *    Logout's write has returned, with no wait for the peer's Logout;
 *  - after every other Logout the session sends to end it, before or after it established,
 *    it waits for the peer's Logout, at most logout_disconnect_timeout_ms from the start of
 *    that Logout's write, then closes the connection. During the wait after it established
 *    it answers the peer's frames as while established (a Heartbeat for a TestRequest, a
 *    resend answer, a ResendRequest for a gap, a Reject) and originates nothing; a peer
 *    frame that would end the session is absorbed. The peer's Logout, at any MsgSeqNum(34),
 *    ends the wait and draws no reply;
 *  - every Logout the session writes to end a session is limited: a write that has not
 *    completed logout_disconnect_timeout_ms after it started closes the connection then;
 *  - the session sends a TestRequest after 1.2 times HeartBtInt of inbound silence, where
 *    it sent one after HeartBtInt, and gives up after twice that silence, counted from the
 *    last inbound frame;
 *  - while a TestRequest is pending, a Heartbeat whose TestReqID(112) is that of an earlier
 *    TestRequest the session sent on this connection neither answers it nor ends the
 *    session; one the session never sent on this connection ends it, on every Heartbeat
 *    route.
 *  Migration: no code change; a consumer that read fixpp_session_is_established false as
 *  "a send is admitted no more" reads it during the wait as still true while sends are
 *  refused. */
#define FIXPP_C_ABI_VERSION_MAJOR 1
#define FIXPP_C_ABI_VERSION_MINOR 18 /* 1.18: endings close the connection, R-2 wait (105), BREAKING */
#define FIXPP_C_ABI_VERSION_PATCH 0

/** Composite: (MAJOR<<16)|(MINOR<<8)|PATCH — single-integer compatibility check. */
#define FIXPP_C_ABI_VERSION \
    (((FIXPP_C_ABI_VERSION_MAJOR) << 16) | \
     ((FIXPP_C_ABI_VERSION_MINOR) << 8)  | \
      (FIXPP_C_ABI_VERSION_PATCH))

/* NOLINTEND(cppcoreguidelines-macro-usage,cppcoreguidelines-macro-to-enum,
             modernize-macro-to-enum) */

/* ── Version descriptor ──────────────────────────────────────────────────── */

/**
 * fixpp_version_t — plain-old-data version descriptor.
 *
 * _reserved is zero on write; consumers MUST ignore it for forward-compat.
 * Layout frozen per [const §X.1] once FIXPP_C_ABI_VERSION_MAJOR >= 1.
 */
typedef struct fixpp_version {
    uint16_t major;
    uint16_t minor;
    uint16_t patch;
    uint16_t _reserved; /* zero-initialised; reserved for future use */
} fixpp_version_t;

/* ── Accessors ───────────────────────────────────────────────────────────── */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * fixpp_version — return the C-ABI surface version.
 *
 * The returned value equals the FIXPP_C_ABI_VERSION_{MAJOR,MINOR,PATCH} macros
 * compiled into the engine binary.  A consumer linked against the same binary
 * can use the composite FIXPP_C_ABI_VERSION macro for a single-integer check.
 *
 * @return fixpp_version_t with major/minor/patch equal to the macros above.
 *
 * Thread-safety: thread-safe.
 */
FIXPP_API_EXPORT fixpp_version_t fixpp_version(void);

/**
 * fixpp_library_version — return the C++ library version.
 *
 * This is the semantic version of the underlying C++ library (currently 0.0.1)
 * and is on a separate, independent increment track from the C-ABI version
 * per [arch §9.2].  It is informational; compatibility is governed by
 * fixpp_version() / FIXPP_C_ABI_VERSION_MAJOR.
 *
 * @return fixpp_version_t with the library's MAJOR.MINOR.PATCH.
 *
 * Thread-safety: thread-safe.
 */
FIXPP_API_EXPORT fixpp_version_t fixpp_library_version(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FIXPP_C_API_VERSION_H */
