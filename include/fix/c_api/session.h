/*
 * include/fix/c_api/session.h — C-ABI session lifecycle, send, receive callback,
 *                               and session-config builder (CA-005/006/007, Feature B)
 *
 * [2i §4.2/§4.6/§4.9/§4.10/§5.2/§10] /
 * specs/050-c-abi-session-send-recv/contracts/{lifecycle-surface,send-and-receive,config-builders}.md.
 * C-clean: no C++ symbols, no C++ syntax. Compiles as C11.
 *
 * A fixpp_session_t is a NON-owning observer of an engine-owned Session, keyed by
 * the derived SessionId. fixpp_session_open = Engine::register_session (BEFORE
 * fixpp_engine_start); it returns a resolvable handle but the session is NOT yet
 * connected (open != connected). Send takes a COMMITTED application-message-
 * payload byte span (35=<type> + app fields; the session frames it and assigns
 * MsgSeqNum) — not a fixpp_msg_t (the outbound construction surface is Feature C,
 * [2i §10]); inbound delivery hands the callback a fixpp_msg_t valid only for the dispatch
 * window. The receive callback runs synchronously ON the session strand.
 */

#ifndef FIXPP_C_API_SESSION_H
#define FIXPP_C_API_SESSION_H

/* NOLINTBEGIN(hicpp-deprecated-headers,modernize-deprecated-headers):
   C-ABI header; C-style includes are correct for pure-C consumers. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* NOLINTEND(hicpp-deprecated-headers,modernize-deprecated-headers) */

#include <fix/c_api/error.h>
#include <fix/c_api/export.h>
#include <fix/c_api/handles.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Frozen enums (Gate-A pinned; values are ABI-stable) ───────────────────*/

/** Session role. */
typedef enum fixpp_session_role {
    FIXPP_ROLE_INITIATOR = 0,
    FIXPP_ROLE_ACCEPTOR  = 1
} fixpp_session_role;

/** Security profile kind. TLS maps to mutual-TLS/CA (cert+key = client cert/key);
 *  INSECURE_PLAIN_TCP ignores cert/key and requires the explicit opt-in
 *  ([const §XII.5]). Other C++ TLS sub-kinds are a v1.x setter refinement. */
typedef enum fixpp_security_kind {
    FIXPP_SECURITY_TLS               = 0,
    FIXPP_SECURITY_INSECURE_PLAIN_TCP = 1
} fixpp_security_kind;

/* ── Receive callback ──────────────────────────────────────────────────────
 * Invoked synchronously ON the session strand, on a fixpp-owned worker thread,
 * for each inbound application message (FR-013). `inbound` is valid ONLY for the
 * duration of the call ([2i §4.6]); copy out anything you need before returning.
 * The callback MUST NOT make a blocking C-ABI call (fixpp_session_send / _close /
 * fixpp_engine_destroy) on its own engine/session — that deadlocks (FR-013a):
 * the callback holds the strand and the blocking thunk posts onto the same strand
 * and waits. Reply by copying out and sending from a non-callback thread. */
typedef void (*fixpp_recv_cb)(const fixpp_msg_t* inbound, void* userdata);

/* ── Send (toApp) callback ─────────────────────────────────────────────────
 * Verdict the send callback returns to steer the originate path.  CLOSED enum —
 * NOT an alias of fixpp_error_t (so an accidental `return FIXPP_ERR_*` cannot be
 * a legal send/veto verdict and silently terminal-close the session).
 * Fixed integer constants for a stable C ABI.
 * [data-model E-6 / contracts/toapp-callback.md] */
typedef enum fixpp_toapp_verdict {
    FIXPP_TOAPP_SEND  = 0,  /**< Proceed — transmit the message. */
    FIXPP_TOAPP_VETO  = 1,  /**< Suppress — mapped to app_do_not_send. */
    FIXPP_TOAPP_ERROR = 2   /**< Callback signalled failure — mapped to app_callback_threw. */
} fixpp_toapp_verdict;

/** Send (toApp) callback type.  Invoked on the session strand BEFORE an
 *  application message is transmitted.  `outbound` is a read-only framed
 *  fixpp_msg_t valid only for the duration of the call.  Returns a verdict.
 *  Any out-of-range value is treated as FIXPP_TOAPP_ERROR.
 *  Reentrancy: requires-session-lock (runs on exec_; must not allocate
 *  from the global heap; must not call back into a blocking session API).
 *  [contracts/toapp-callback.md] */
typedef fixpp_toapp_verdict (*fixpp_send_cb)(const fixpp_msg_t* outbound, void* userdata);

/** Sequence-number reset policy (FR-005b / E-4). Values mirror the C++ enum class
 *  reset_seqnum_policy : uint8_t in session_config.hpp (same integer ordinals).
 *  ABI-stable: new values are additive; out-of-range values → CAPI_CONFIG_INVALID. */
typedef enum fixpp_reset_seqnum_policy {
    FIXPP_RESET_SEQNUM_BILATERAL_STRICT  = 0,  /**< Both peers must present 141=Y (default). */
    FIXPP_RESET_SEQNUM_BILATERAL_LENIENT = 1,  /**< Either peer presenting 141=Y is sufficient. */
    FIXPP_RESET_SEQNUM_UNILATERAL        = 2   /**< Always reset regardless of peer 141=Y. */
} fixpp_reset_seqnum_policy;

/* ── Session-config builder (opaque; FR-014) ───────────────────────────────
 * Reentrancy: single-thread per handle (each setter below restates the class so
 * the per-symbol reentrancy gate sees exactly one token). CONSUMED by
 * fixpp_session_open on success (moved into the registry, invalidated; do NOT
 * destroy afterwards); on failure untouched (caller still owns it). */

/** Create a session-config builder. Reentrancy: single-thread. */
FIXPP_API_EXPORT fixpp_error_t fixpp_session_config_create(fixpp_session_config_t** out_cfg);

/** SenderCompID / TargetCompID (both required; empty → config error).
 *
 *  Since 1.7 (BREAKING), a value holding a byte < 0x20 (incl. SOH \x01) or
 *  '=' (0x3D) → FIXPP_ERR_CAPI_CONFIG_INVALID, checked for BOTH arguments
 *  before either is stored — a bad target leaves a previously-stored sender
 *  unchanged.
 *
 *  Return codes:
 *    FIXPP_ERR_OK                  -- stored
 *    FIXPP_ERR_NULL_HANDLE         -- cfg is NULL
 *    FIXPP_ERR_CAPI_CONFIG_INVALID -- sender or target is NULL/empty, or
 *                                     (1.7, BREAKING) either holds a
 *                                     forbidden byte
 *
 *  Reentrancy: single-thread. */
FIXPP_API_EXPORT fixpp_error_t fixpp_session_config_set_comp_ids(
    fixpp_session_config_t* cfg, const char* sender, const char* target);

/** BeginString (e.g. "FIX.4.4" / "FIXT.1.1").
 *
 *  Since 1.7 (BREAKING), a value holding a byte < 0x20 (incl. SOH \x01) or
 *  '=' (0x3D) → FIXPP_ERR_CAPI_CONFIG_INVALID.
 *
 *  Return codes:
 *    FIXPP_ERR_OK                  -- stored
 *    FIXPP_ERR_NULL_HANDLE         -- cfg is NULL
 *    FIXPP_ERR_CAPI_CONFIG_INVALID -- begin_string is NULL/empty, or
 *                                     (1.7, BREAKING) holds a forbidden byte
 *
 *  Reentrancy: single-thread. */
FIXPP_API_EXPORT fixpp_error_t fixpp_session_config_set_begin_string(
    fixpp_session_config_t* cfg, const char* begin_string);

/** Session role (initiator / acceptor). Reentrancy: single-thread. */
FIXPP_API_EXPORT fixpp_error_t fixpp_session_config_set_role(
    fixpp_session_config_t* cfg, fixpp_session_role role);

/** Heartbeat interval in seconds. Reentrancy: single-thread. */
FIXPP_API_EXPORT fixpp_error_t fixpp_session_config_set_heartbeat_seconds(
    fixpp_session_config_t* cfg, uint32_t n);

/** Security profile. `cert`/`key` are PEM file paths (ignored for the plaintext
 *  kind, which requires the explicit insecure opt-in). Reentrancy: single-thread. */
FIXPP_API_EXPORT fixpp_error_t fixpp_session_config_set_security(
    fixpp_session_config_t* cfg, fixpp_security_kind kind,
    const char* cert, const char* key);

/** Dictionary (required). Pass a handle created by fixpp_dict_load_from_xml()
 *  and release it with fixpp_dict_destroy() after the setter returns if the
 *  caller no longer needs its own reference. Reentrancy: single-thread. */
FIXPP_API_EXPORT fixpp_error_t fixpp_session_config_set_dictionary(
    fixpp_session_config_t* cfg, fixpp_dict_t* dict);

/** Reset sequence numbers on logon. Reentrancy: single-thread. */
FIXPP_API_EXPORT fixpp_error_t fixpp_session_config_set_reset_on_logon(
    fixpp_session_config_t* cfg, bool reset_on_logon);

/**
 * fixpp_session_config_set_reset_seqnum_policy — set the seqnum-reset acceptance policy.
 *
 * Writes SessionConfig::reset_seqnum_policy_field.  The enumerator values mirror
 * the C++ enum class reset_seqnum_policy : uint8_t (same integer ordinals; 0=strict,
 * 1=lenient, 2=unilateral).  The default (bilateral_strict) requires BOTH peers to
 * send ResetSeqNumFlag(141)=Y for a reset to be accepted.  Use bilateral_lenient when
 * only one side sends 141=Y (e.g. initiator reset_on_logon=true, acceptor=false).
 *
 * Reentrancy: single-thread.
 * @return FIXPP_ERR_OK; FIXPP_ERR_NULL_HANDLE (NULL cfg); FIXPP_ERR_CAPI_CONFIG_INVALID
 *         (out-of-range enum value).
 */
FIXPP_API_EXPORT fixpp_error_t fixpp_session_config_set_reset_seqnum_policy(
    fixpp_session_config_t* cfg, fixpp_reset_seqnum_policy kind);

/**
 * fixpp_session_config_set_tcp_endpoint — set the session's TCP endpoint.
 *
 * For an INITIATOR: the peer endpoint to connect to.  For an ACCEPTOR: the bind
 * endpoint (port 0 = OS-assigned ephemeral; read back via
 * fixpp_session_acceptor_bound_endpoint after engine start).  Sets
 * SessionConfig::reconnect_endpoint = {host, port} and the internal
 * SessionConfig::transport_send placeholder that the engine's auto-derived plaintext
 * factory replaces at connect/accept — the consumer never references transport_send.
 * Call BEFORE fixpp_session_open (open copies the config by value).
 *
 * Reentrancy: single-thread.
 * @return FIXPP_ERR_OK; FIXPP_ERR_NULL_HANDLE (NULL cfg or host);
 *         FIXPP_ERR_CAPI_CONFIG_INVALID (empty host string).
 */
FIXPP_API_EXPORT fixpp_error_t fixpp_session_config_set_tcp_endpoint(
    fixpp_session_config_t* cfg, const char* host, uint16_t port);

/**
 * fixpp_session_config_set_logon_timeout_ms — set the establishment timeout
 * (C-ABI 1.11; 093, fixpp#514).
 *
 * Writes SessionConfig::logon_timeout_ms: how long, in milliseconds, a
 * connection may take to log on. The deadline runs from the accept on an
 * acceptor, and from the end of the connect, with the Logon sent, on an
 * initiator. A connection not logged on by then is closed, including one whose
 * Logon was refused. The default is 10000.
 *
 * Return codes:
 *   FIXPP_ERR_OK                  -- stored
 *   FIXPP_ERR_NULL_HANDLE         -- cfg is NULL
 *   FIXPP_ERR_CAPI_CONFIG_INVALID -- ms is 0; nothing is stored
 *
 * Reentrancy: single-thread.
 */
FIXPP_API_EXPORT fixpp_error_t fixpp_session_config_set_logon_timeout_ms(
    fixpp_session_config_t* cfg, uint32_t ms);

/** Destroy a session-config builder. NULL-safe; never-throws. Do NOT call after
 *  the builder was consumed by a successful fixpp_session_open.
 *  Reentrancy: single-thread. */
FIXPP_API_EXPORT void fixpp_session_config_destroy(fixpp_session_config_t* cfg);

/* ── Session lifecycle ─────────────────────────────────────────────────────*/

/**
 * fixpp_session_open — register a session with the engine (= register_session).
 *
 * MUST be called BEFORE fixpp_engine_start; a call after start returns a domain
 * error (C-ABI-enforced register-before-start). Returns a NON-owning handle keyed
 * by SessionId; open != connected (poll fixpp_session_is_established). `cfg` is
 * CONSUMED on success.
 *
 * Reentrancy: single-thread. THUNK: construction-time (catch → *_CONFIG; never throws).
 */
FIXPP_API_EXPORT fixpp_error_t fixpp_session_open(fixpp_engine_t* engine,
                                                  fixpp_session_config_t* cfg,
                                                  fixpp_session_t** out_session);

/**
 * fixpp_session_close — gracefully close a session (= Session::close).
 *
 * Posts the close onto the session's serialisation domain and blocks until it
 * completes, using ASIO native cancellation that closes the transport (so a
 * blocked idle read breaks, FR-005). The handle is invalidated once close
 * returns; there is no separate session-destroy.
 *
 * Reaped-session contract (issue #151): if the session was established at least
 * once (logged on) and its peer has since disconnected — leaving it reaped/drained
 * by the engine — the first close returns FIXPP_ERR_OK: closing a once-live,
 * now-drained session is an idempotent success, not an error. A session that was
 * opened but never established (never started/connected, or published but never
 * logged on) returns FIXPP_ERR_THREAD_SESSION_LIFECYCLE. Either way the handle is
 * invalidated, so a subsequent close returns FIXPP_ERR_INVALID_HANDLE.
 *
 * BREAKING (C-ABI 1.9; 091 FR-020): a Logon carrying a Length immediately
 * followed by its paired Data whose counted extent reaches or passes the end
 * of the whole framed message, or whose following byte is not SOH (standard
 * pairs included), which was accepted, is now refused, on either role.
 * Amended in C-ABI 1.11 (093): such a Logon whose third field is not
 * MsgType(35) is disregarded before interpret_logon reads it, and is not
 * refused. Once a
 * session whose Logon was refused that way has drained, close returns
 * FIXPP_ERR_THREAD_SESSION_LIFECYCLE, translated for the consumer's ABI minor
 * (fixpp_engine_create), where it returned FIXPP_ERR_OK: that session never
 * established, so it is not the established-then-reaped case above.
 *
 * BREAKING (C-ABI 1.10; 092, fixpp#507): a Logon carrying a malformed tag (a
 * non-digit tag byte, an empty tag, a tag above 65535, or a field with no '='
 * before its SOH), which was accepted, is now refused, on either role.
 * Amended in C-ABI 1.11 (093): such a Logon whose third field is not
 * MsgType(35) is disregarded instead, and is not refused. Once a
 * session whose Logon was refused that way has drained, close returns
 * FIXPP_ERR_THREAD_SESSION_LIFECYCLE, translated for the consumer's ABI minor
 * (fixpp_engine_create), where it returned FIXPP_ERR_OK: that session never
 * established. For a session that established and that another 1.10 effect
 * then ends (see fixpp_session_is_established), close returns FIXPP_ERR_OK,
 * as for any session that was established at least once.
 *
 * BREAKING (C-ABI 1.11; 093, fixpp#514, #515, #516): the session frames and
 * disposes of inbound bytes as follows, where it did otherwise. Most of these
 * effects keep up, or let establish, a session that ended or did not
 * establish; the establishment timeout, and a Logon disregarded where it was
 * accepted, do the reverse:
 *   - a frame the Framer finds garbled (a wrong BodyLength or CheckSum, bytes
 *     before a frame start, or a BeginString or a BodyLength digit run longer
 *     than its cap) is disregarded and counted
 *     (fixpp_session_garbled_frame_count), in every state, where it ended the
 *     session; the two over-cap shapes were framed before;
 *   - a frame whose third field is not MsgType(35) is disregarded and counted
 *     in every state but Disconnected, where it was processed; a Logon of
 *     that shape is disregarded where it was accepted or refused (for a
 *     malformed tag or a malformed Length+Data count, among others);
 *   - a frame of at most the inbound limit (64 KiB through the C ABI) is
 *     admitted however the stream is split into reads, where some near that
 *     size ended the session; a frame over the limit ends the connection, in
 *     every state, as soon as its BodyLength(9) is read;
 *   - a frame the session admits always parses for dispatch, where one dense
 *     with fields could exhaust the parse buffer and end the session;
 *   - a connection not logged on by the establishment timeout
 *     (fixpp_session_config_set_logon_timeout_ms; 10 s by default) is
 *     closed, including one whose Logon was refused, where it stayed open; a
 *     peer that answers the Logon later than that no longer establishes;
 *   - every frame that passes the fault and third-field checks counts as
 *     inbound traffic for the heartbeat interval, where some that returned
 *     early did not (one above or below the expected MsgSeqNum(34), a
 *     SequenceReset, a Reject, among others); the TestRequest such traffic
 *     drew is not sent, and a session that ended when it went unanswered
 *     stays up.
 * close returns FIXPP_ERR_OK for a session that was established at least once
 * and FIXPP_ERR_THREAD_SESSION_LIFECYCLE, translated for the consumer's ABI
 * minor (fixpp_engine_create), for one that never was. So where one of these
 * effects decides whether a session establishes (a Logon disregarded rather
 * than accepted or refused, a garbled frame before the Logon reply, the
 * establishment timeout), close's result changes with it; for a session
 * established before the effect, close returns FIXPP_ERR_OK either way.
 *
 * Reentrancy: single-thread — non-callback / non-session-strand caller only; no
 * concurrent close on the same handle (the thunk posts onto the session domain
 * and BLOCKS, so a callback/strand caller deadlocks — FR-013a).
 */
FIXPP_API_EXPORT fixpp_error_t fixpp_session_close(fixpp_session_t* session);

/**
 * fixpp_session_is_established — poll whether the session is logged on.
 *
 * Writes *out_established = (Engine::lookup(id) != null && Session::is_open()).
 * A consumer waits on this before sending (open != connected).
 *
 * BREAKING (C-ABI 1.9; 091 FR-020): a Logon carrying a Length immediately
 * followed by its paired Data whose counted extent reaches or passes the end
 * of the whole framed message, or whose following byte is not SOH (standard
 * pairs included), which was accepted, is now refused, on either role.
 * Amended in C-ABI 1.11 (093): such a Logon whose third field is not
 * MsgType(35) is disregarded before interpret_logon reads it, and is not
 * refused. For a
 * session whose Logon was refused that way, *out_established stays false where
 * it became true.
 *
 * BREAKING (C-ABI 1.10; 092, fixpp#507): the session disposes of an inbound
 * frame whose header scan meets a malformed tag (a non-digit tag byte, an
 * empty tag, a tag above 65535, or a field with no '=' before its SOH) or a
 * Length+Data mismatch before any handler reads it; "faulty" below means such
 * a frame. Each effect below keeps a session from establishing, or ends it,
 * where it established or continued:
 *   - a Logon carrying a malformed tag, which was accepted, is refused, on
 *     either role (amended in C-ABI 1.11, 093: such a Logon whose third field
 *     is not MsgType(35) is disregarded instead, and is not refused);
 *   - on an established session, a faulty Logon whose third field is
 *     MsgType(35) and whose MsgSeqNum(34) was read before the fault ends the
 *     session, with no Reject and no Logout;
 *   - on an established session, a faulty frame whose fault comes before its
 *     MsgSeqNum(34), or whose third field is not MsgType(35), and which was
 *     accepted and advanced the expected inbound sequence number, is
 *     disregarded without advancing it; if the peer resends the same bytes,
 *     the session ends at the peer's next new message that is not a
 *     Heartbeat;
 *   - on an established session in a resend recovery, a faulty SequenceReset,
 *     GapFill or Reset mode, whose third field is MsgType(35) and whose
 *     MsgSeqNum(34) was read before the fault, is Rejected and not applied, so
 *     the gap it would have closed stays open, and the session ends at the
 *     peer's next new message that is not a Heartbeat;
 *   - with a heartbeat interval set, a faulty frame does not count as inbound
 *     traffic, so a peer whose frames over the heartbeat interval and the
 *     TestRequest grace window are all faulty ends the session;
 *   - on an established session, a frame the header scan finds fault-free but
 *     the session cannot parse for dispatch (one that exhausts the per-message
 *     parse arena, for example), which was dropped while the session
 *     continued, ends the session (amended in C-ABI 1.11, 093: a frame the
 *     session admits now always parses for dispatch, so this no longer
 *     occurs);
 *   - an inbound message that would advance the expected inbound sequence
 *     number past its maximum (4294967295), where that number wrapped to 0 and
 *     the session continued, ends the session with no Reject and no Logout
 *     (092 FR-019).
 * For a session whose Logon is refused that way, *out_established stays false
 * where it became true; for a session any other of these effects ends, it
 * turns false where it stayed true.
 *
 * BREAKING (C-ABI 1.11; 093, fixpp#514, #515, #516): the session frames and
 * disposes of inbound bytes as follows, where it did otherwise. Most of these
 * effects keep up, or let establish, a session that ended or did not
 * establish; the establishment timeout, and a Logon disregarded where it was
 * accepted, do the reverse:
 *   - a frame the Framer finds garbled (a wrong BodyLength or CheckSum, bytes
 *     before a frame start, or a BeginString or a BodyLength digit run longer
 *     than its cap) is disregarded and counted
 *     (fixpp_session_garbled_frame_count), in every state, where it ended the
 *     session; the two over-cap shapes were framed before;
 *   - a frame whose third field is not MsgType(35) is disregarded and counted
 *     in every state but Disconnected, where it was processed; a Logon of
 *     that shape is disregarded where it was accepted or refused (for a
 *     malformed tag or a malformed Length+Data count, among others);
 *   - a frame of at most the inbound limit (64 KiB through the C ABI) is
 *     admitted however the stream is split into reads, where some near that
 *     size ended the session; a frame over the limit ends the connection, in
 *     every state, as soon as its BodyLength(9) is read;
 *   - a frame the session admits always parses for dispatch, where one dense
 *     with fields could exhaust the parse buffer and end the session;
 *   - a connection not logged on by the establishment timeout
 *     (fixpp_session_config_set_logon_timeout_ms; 10 s by default) is
 *     closed, including one whose Logon was refused, where it stayed open; a
 *     peer that answers the Logon later than that no longer establishes;
 *   - every frame that passes the fault and third-field checks counts as
 *     inbound traffic for the heartbeat interval, where some that returned
 *     early did not (one above or below the expected MsgSeqNum(34), a
 *     SequenceReset, a Reject, among others); the TestRequest such traffic
 *     drew is not sent, and a session that ended when it went unanswered
 *     stays up.
 * Where one of these effects keeps a session up or lets it establish,
 * *out_established is true where it was false; where one ends a session or
 * keeps it from establishing, it is false where it was true.
 *
 * Reentrancy: thread-safe. O(1) lock-free (atomic reader snapshot).
 */
FIXPP_API_EXPORT fixpp_error_t fixpp_session_is_established(fixpp_session_t* session,
                                                           bool* out_established);

/**
 * fixpp_session_acceptor_bound_endpoint — read back an acceptor's OS-assigned bound port.
 *
 * Writes *port_out = Engine::acceptor_bound_endpoint(id).port.  For the port-0
 * ephemeral-bind workflow: a session not yet bound (engine not yet started, or the
 * accept-loop has not fired) yields *port_out = 0 with FIXPP_ERR_OK — poll until
 * non-zero.  The bind host is consumer-known (no host out-param).
 *
 * Reentrancy: thread-safe. O(1) snapshot read, like fixpp_session_is_established.
 * THUNK: steady-state — an escaping exception is an invariant violation → fatal
 * log + abort, NOT translated to a code.
 * @return FIXPP_ERR_OK (+ *port_out, possibly 0 if not yet bound);
 *         FIXPP_ERR_NULL_HANDLE (NULL session or port_out);
 *         FIXPP_ERR_INVALID_HANDLE (destroyed/invalidated session).
 */
FIXPP_API_EXPORT fixpp_error_t fixpp_session_acceptor_bound_endpoint(fixpp_session_t* session,
                                                                      uint16_t* port_out);

/**
 * fixpp_session_garbled_frame_count — read the session's garbled-frame count
 * (C-ABI 1.11; 093, fixpp#514).
 *
 * Writes *out = the number of garbled inbound regions the session has
 * disregarded: frames the Framer finds garbled, and frames whose third field is
 * not MsgType(35). The count only grows. Before the session exists (before
 * fixpp_engine_start, and after it until the engine has built the session for a
 * connection) *out is 0 and the call returns FIXPP_ERR_OK. On an acceptor, the
 * garbles of a connection that never yields a session are not counted. The value
 * is not ordered with the rest of the session's state: a caller that needs it to
 * reflect a given frame synchronises through the session's traffic.
 *
 * Return codes:
 *   FIXPP_ERR_OK             -- *out written
 *   FIXPP_ERR_NULL_HANDLE    -- out is NULL (nothing written), or session is
 *                               NULL (*out written 0)
 *   FIXPP_ERR_INVALID_HANDLE -- the handle was closed, or its engine destroyed
 *                               (*out written 0)
 *
 * Reentrancy: thread-safe. A scoped Engine::lookup lease, released before
 * return. THUNK: steady-state — an escaping exception is an invariant violation
 * → fatal log + abort, NOT translated to a code.
 */
FIXPP_API_EXPORT fixpp_error_t fixpp_session_garbled_frame_count(const fixpp_session_t* session,
                                                                 uint64_t* out);

/**
 * fixpp_session_send — send an application-message payload (= Engine::send).
 *
 * `frame`/`len` is an APPLICATION-MESSAGE PAYLOAD as a committed byte span (a
 * byte buffer, NOT a fixpp_msg_t — [2i §10], decoupled from Feature C's outbound
 * construction): it MUST lead with "35=<msgtype>\x01" (an application MsgType)
 * and contain only application fields, SOH-terminated. The session itself stamps
 * the header/trailer (8/9/49/56/52/10) and ASSIGNS the in-sequence MsgSeqNum(34),
 * so the payload MUST NOT contain session framing tags (8/9/34/49/52/56/10) at a
 * field boundary — a payload that does is rejected → FIXPP_ERR_APP_PAYLOAD_MALFORMED
 * with no transmit. The span is borrowed
 * (read-only during the call; the engine deep-copies); the caller may free/reuse
 * on return. Honours durable-before-transmit by reference (FR-009).
 *
 * BREAKING (C-ABI 1.9): a Length+Data pair a loaded dictionary declares only
 * inside a component or group is now a dictionary pair. A payload carrying a
 * malformed pair of that kind, which returned FIXPP_ERR_OK, now returns
 * FIXPP_ERR_APP_PAYLOAD_MALFORMED, translated for the consumer's ABI minor
 * (fixpp_engine_create), with no transmit; a count that covers a
 * following field (e.g. 43, 122 or a header-class tag) is now transmitted
 * verbatim as Data, not excised or reordered. Also (091 FR-020), a Logon
 * carrying a Length immediately followed by its paired Data whose counted
 * extent reaches or passes the end of the whole framed message, or whose
 * following byte is not SOH (standard pairs included), which was accepted, is
 * now refused, on either role (amended in C-ABI 1.11, 093: such a Logon whose
 * third field is not MsgType(35) is disregarded before interpret_logon reads
 * it, and is not refused); a send on that session, issued after that
 * Logon, which returned FIXPP_ERR_OK, now returns
 * FIXPP_ERR_SESSION_INVALID_STATE, translated for the consumer's ABI minor
 * (fixpp_engine_create). For a send either change refuses, the
 * toApp callback (fixpp_session_register_send_callback) is not invoked: the
 * pair refusal comes before the send path builds its toApp view, and a send
 * on that session is refused at the engine's Active check, before it reaches
 * that path.
 *
 * BREAKING (C-ABI 1.10; 092, fixpp#507): the session disposes of an inbound
 * frame whose header scan meets a malformed tag (a non-digit tag byte, an
 * empty tag, a tag above 65535, or a field with no '=' before its SOH) or a
 * Length+Data mismatch before any handler reads it; "faulty" below means such
 * a frame. Each effect below keeps a session from establishing, or ends it,
 * where it established or continued:
 *   - a Logon carrying a malformed tag, which was accepted, is refused, on
 *     either role (amended in C-ABI 1.11, 093: such a Logon whose third field
 *     is not MsgType(35) is disregarded instead, and is not refused);
 *   - on an established session, a faulty Logon whose third field is
 *     MsgType(35) and whose MsgSeqNum(34) was read before the fault ends the
 *     session, with no Reject and no Logout;
 *   - on an established session, a faulty frame whose fault comes before its
 *     MsgSeqNum(34), or whose third field is not MsgType(35), and which was
 *     accepted and advanced the expected inbound sequence number, is
 *     disregarded without advancing it; if the peer resends the same bytes,
 *     the session ends at the peer's next new message that is not a
 *     Heartbeat;
 *   - on an established session in a resend recovery, a faulty SequenceReset,
 *     GapFill or Reset mode, whose third field is MsgType(35) and whose
 *     MsgSeqNum(34) was read before the fault, is Rejected and not applied, so
 *     the gap it would have closed stays open, and the session ends at the
 *     peer's next new message that is not a Heartbeat;
 *   - with a heartbeat interval set, a faulty frame does not count as inbound
 *     traffic, so a peer whose frames over the heartbeat interval and the
 *     TestRequest grace window are all faulty ends the session;
 *   - on an established session, a frame the header scan finds fault-free but
 *     the session cannot parse for dispatch (one that exhausts the per-message
 *     parse arena, for example), which was dropped while the session
 *     continued, ends the session (amended in C-ABI 1.11, 093: a frame the
 *     session admits now always parses for dispatch, so this no longer
 *     occurs);
 *   - an inbound message that would advance the expected inbound sequence
 *     number past its maximum (4294967295), where that number wrapped to 0 and
 *     the session continued, ends the session with no Reject and no Logout
 *     (092 FR-019).
 * A send on a session one of these effects refused or ended, issued after
 * that effect, which returned FIXPP_ERR_OK, now returns
 * FIXPP_ERR_SESSION_INVALID_STATE, translated for the consumer's ABI minor
 * (fixpp_engine_create).
 *
 * BREAKING (C-ABI 1.11; 093, fixpp#514, #515, #516): the session frames and
 * disposes of inbound bytes as follows, where it did otherwise. Most of these
 * effects keep up, or let establish, a session that ended or did not
 * establish; the establishment timeout, and a Logon disregarded where it was
 * accepted, do the reverse:
 *   - a frame the Framer finds garbled (a wrong BodyLength or CheckSum, bytes
 *     before a frame start, or a BeginString or a BodyLength digit run longer
 *     than its cap) is disregarded and counted
 *     (fixpp_session_garbled_frame_count), in every state, where it ended the
 *     session; the two over-cap shapes were framed before;
 *   - a frame whose third field is not MsgType(35) is disregarded and counted
 *     in every state but Disconnected, where it was processed; a Logon of
 *     that shape is disregarded where it was accepted or refused (for a
 *     malformed tag or a malformed Length+Data count, among others);
 *   - a frame of at most the inbound limit (64 KiB through the C ABI) is
 *     admitted however the stream is split into reads, where some near that
 *     size ended the session; a frame over the limit ends the connection, in
 *     every state, as soon as its BodyLength(9) is read;
 *   - a frame the session admits always parses for dispatch, where one dense
 *     with fields could exhaust the parse buffer and end the session;
 *   - a connection not logged on by the establishment timeout
 *     (fixpp_session_config_set_logon_timeout_ms; 10 s by default) is
 *     closed, including one whose Logon was refused, where it stayed open; a
 *     peer that answers the Logon later than that no longer establishes;
 *   - every frame that passes the fault and third-field checks counts as
 *     inbound traffic for the heartbeat interval, where some that returned
 *     early did not (one above or below the expected MsgSeqNum(34), a
 *     SequenceReset, a Reject, among others); the TestRequest such traffic
 *     drew is not sent, and a session that ended when it went unanswered
 *     stays up.
 * Where one of these effects keeps a session up or lets it establish, a send on
 * that session issued after the effect, which returned
 * FIXPP_ERR_SESSION_INVALID_STATE, translated for the consumer's ABI minor
 * (fixpp_engine_create), returns FIXPP_ERR_OK; where one ends a session or
 * keeps it from establishing, the reverse.
 *
 * Reentrancy: thread-safe — callable from any consumer thread (the any-thread
 * Engine::send contract) EXCEPT from inside the receive callback, where the
 * blocking wrapper deadlocks (FR-013a; recorded Gate-A deviation from
 * [2i §4.10]'s REQUIRES_SESSION_LOCK example).
 * THUNK: steady-state — an escaping exception is an invariant violation → fatal
 * log + abort, NOT translated to a code.
 */
FIXPP_API_EXPORT fixpp_error_t fixpp_session_send(fixpp_session_t* session,
                                                  const uint8_t* frame, size_t len);

/**
 * fixpp_session_register_callback — register the inbound receive callback.
 *
 * MUST be called BEFORE fixpp_engine_start; a call after start returns
 * FIXPP_ERR_CAPI_CONFIG_INVALID (the callback map is read on the session strand
 * without a mutex — a post-start registration would race fromApp; FR-011). On a
 * subsequent inbound application message the engine invokes `cb` with an inbound
 * fixpp_msg_t and `userdata`. See fixpp_recv_cb for the dispatch-window lifetime
 * and the no-blocking-call-from-callback rule (FR-013a).
 *
 * BREAKING (C-ABI 1.9): a Length+Data pair a loaded dictionary declares only
 * inside a component or group is now a dictionary pair. An inbound message
 * carrying a malformed pair of that kind, which was delivered to `cb` before,
 * is now dropped: `cb` is not invoked. The session answers it with a session
 * Reject when the session is established and has not sent a Logout, the
 * message's third field is MsgType(35) and its MsgSeqNum(34) was read before
 * the pair (092 contract C-2, rows D-5/D-6); otherwise it disregards it (rows
 * D-7/D-8/D-9). A pair mismatch before MsgSeqNum(34) is row D-7, disregarded,
 * not Rejected. The 1.10 clause below states the conditions.
 * Also (091 FR-020), a Logon carrying a Length immediately followed
 * by its paired Data whose counted extent reaches or passes the end of the
 * whole framed message, or whose following byte is not SOH (standard pairs
 * included), which was accepted, is now refused, on either role (amended in
 * C-ABI 1.11, 093: such a Logon whose third field is not MsgType(35) is
 * disregarded before interpret_logon reads it, and is not refused); inbound
 * application messages on that session, delivered to `cb` before, are never
 * delivered.
 *
 * BREAKING (C-ABI 1.10; 092, fixpp#507): the session disposes of an inbound
 * frame whose header scan meets a malformed tag (a non-digit tag byte, an
 * empty tag, a tag above 65535, or a field with no '=' before its SOH) or a
 * Length+Data mismatch before any handler reads it; "faulty" below means such
 * a frame. Each effect below keeps a session from establishing, or ends it,
 * where it established or continued:
 *   - a Logon carrying a malformed tag, which was accepted, is refused, on
 *     either role (amended in C-ABI 1.11, 093: such a Logon whose third field
 *     is not MsgType(35) is disregarded instead, and is not refused);
 *   - on an established session, a faulty Logon whose third field is
 *     MsgType(35) and whose MsgSeqNum(34) was read before the fault ends the
 *     session, with no Reject and no Logout;
 *   - on an established session, a faulty frame whose fault comes before its
 *     MsgSeqNum(34), or whose third field is not MsgType(35), and which was
 *     accepted and advanced the expected inbound sequence number, is
 *     disregarded without advancing it; if the peer resends the same bytes,
 *     the session ends at the peer's next new message that is not a
 *     Heartbeat;
 *   - on an established session in a resend recovery, a faulty SequenceReset,
 *     GapFill or Reset mode, whose third field is MsgType(35) and whose
 *     MsgSeqNum(34) was read before the fault, is Rejected and not applied, so
 *     the gap it would have closed stays open, and the session ends at the
 *     peer's next new message that is not a Heartbeat;
 *   - with a heartbeat interval set, a faulty frame does not count as inbound
 *     traffic, so a peer whose frames over the heartbeat interval and the
 *     TestRequest grace window are all faulty ends the session;
 *   - on an established session, a frame the header scan finds fault-free but
 *     the session cannot parse for dispatch (one that exhausts the per-message
 *     parse arena, for example), which was dropped while the session
 *     continued, ends the session (amended in C-ABI 1.11, 093: a frame the
 *     session admits now always parses for dispatch, so this no longer
 *     occurs);
 *   - an inbound message that would advance the expected inbound sequence
 *     number past its maximum (4294967295), where that number wrapped to 0 and
 *     the session continued, ends the session with no Reject and no Logout
 *     (092 FR-019).
 * On a session one of these effects refused or ended, `cb` is not invoked for
 * any inbound application message after that effect; under 092 FR-019 it is
 * not invoked for the message that ends the session either.
 *
 * BREAKING (C-ABI 1.11; 093, fixpp#514, #515, #516): the session frames and
 * disposes of inbound bytes as follows, where it did otherwise. Most of these
 * effects keep up, or let establish, a session that ended or did not
 * establish; the establishment timeout, and a Logon disregarded where it was
 * accepted, do the reverse:
 *   - a frame the Framer finds garbled (a wrong BodyLength or CheckSum, bytes
 *     before a frame start, or a BeginString or a BodyLength digit run longer
 *     than its cap) is disregarded and counted
 *     (fixpp_session_garbled_frame_count), in every state, where it ended the
 *     session; the two over-cap shapes were framed before;
 *   - a frame whose third field is not MsgType(35) is disregarded and counted
 *     in every state but Disconnected, where it was processed; a Logon of
 *     that shape is disregarded where it was accepted or refused (for a
 *     malformed tag or a malformed Length+Data count, among others);
 *   - a frame of at most the inbound limit (64 KiB through the C ABI) is
 *     admitted however the stream is split into reads, where some near that
 *     size ended the session; a frame over the limit ends the connection, in
 *     every state, as soon as its BodyLength(9) is read;
 *   - a frame the session admits always parses for dispatch, where one dense
 *     with fields could exhaust the parse buffer and end the session;
 *   - a connection not logged on by the establishment timeout
 *     (fixpp_session_config_set_logon_timeout_ms; 10 s by default) is
 *     closed, including one whose Logon was refused, where it stayed open; a
 *     peer that answers the Logon later than that no longer establishes;
 *   - every frame that passes the fault and third-field checks counts as
 *     inbound traffic for the heartbeat interval, where some that returned
 *     early did not (one above or below the expected MsgSeqNum(34), a
 *     SequenceReset, a Reject, among others); the TestRequest such traffic
 *     drew is not sent, and a session that ended when it went unanswered
 *     stays up.
 * On a session one of these effects keeps up or lets establish, `cb` is invoked
 * for the inbound application messages after the effect, where it was not; on
 * one it ends or keeps from establishing, the reverse. `cb` is not invoked for
 * a disregarded frame, so an application message whose third field is not
 * MsgType(35), which was delivered to `cb`, is not.
 *
 * Reentrancy: single-thread. THUNK: construction-time.
 */
FIXPP_API_EXPORT fixpp_error_t fixpp_session_register_callback(
    fixpp_session_t* session, fixpp_recv_cb cb, void* userdata);

/**
 * fixpp_session_register_send_callback — register the outbound send (toApp) callback.
 *
 * MUST be called BEFORE fixpp_engine_start; a call after start returns
 * FIXPP_ERR_CAPI_CONFIG_INVALID (the callback map is read on the session strand
 * without a mutex — a post-start registration would race toApp; FR-011). On each
 * originate-path application message the engine invokes `cb` with a READ-ONLY
 * framed fixpp_msg_t (framing tags 8/9/34/49/52/56/10 ARE readable — contracts/
 * toapp-callback.md) and `userdata`, BEFORE transmitting. The verdict steers the
 * engine: FIXPP_TOAPP_SEND → transmit; FIXPP_TOAPP_VETO → suppress
 * (app_do_not_send); FIXPP_TOAPP_ERROR or any out-of-range value → terminal-close
 * (app_callback_threw). ResendRequest retransmissions are NOT surfaced (L-019-4).
 *
 * `cb` may be NULL to deregister. Re-registration overwrites.
 *
 * BREAKING (C-ABI 1.9): a Length+Data pair a loaded dictionary declares only
 * inside a component or group is now a dictionary pair. For a send
 * fixpp_session_send refuses because of a malformed pair of that kind, `cb` is
 * not invoked. Also (091 FR-020), a Logon carrying a Length immediately
 * followed by its paired Data whose counted extent reaches or passes the end
 * of the whole framed message, or whose following byte is not SOH (standard
 * pairs included), which was accepted, is now refused, on either role
 * (amended in C-ABI 1.11, 093: such a Logon whose third field is not
 * MsgType(35) is disregarded before interpret_logon reads it, and is not
 * refused); on that session `cb`, invoked before for each send, is never
 * invoked.
 *
 * BREAKING (C-ABI 1.10; 092, fixpp#507): the session disposes of an inbound
 * frame whose header scan meets a malformed tag (a non-digit tag byte, an
 * empty tag, a tag above 65535, or a field with no '=' before its SOH) or a
 * Length+Data mismatch before any handler reads it; "faulty" below means such
 * a frame. Each effect below keeps a session from establishing, or ends it,
 * where it established or continued:
 *   - a Logon carrying a malformed tag, which was accepted, is refused, on
 *     either role (amended in C-ABI 1.11, 093: such a Logon whose third field
 *     is not MsgType(35) is disregarded instead, and is not refused);
 *   - on an established session, a faulty Logon whose third field is
 *     MsgType(35) and whose MsgSeqNum(34) was read before the fault ends the
 *     session, with no Reject and no Logout;
 *   - on an established session, a faulty frame whose fault comes before its
 *     MsgSeqNum(34), or whose third field is not MsgType(35), and which was
 *     accepted and advanced the expected inbound sequence number, is
 *     disregarded without advancing it; if the peer resends the same bytes,
 *     the session ends at the peer's next new message that is not a
 *     Heartbeat;
 *   - on an established session in a resend recovery, a faulty SequenceReset,
 *     GapFill or Reset mode, whose third field is MsgType(35) and whose
 *     MsgSeqNum(34) was read before the fault, is Rejected and not applied, so
 *     the gap it would have closed stays open, and the session ends at the
 *     peer's next new message that is not a Heartbeat;
 *   - with a heartbeat interval set, a faulty frame does not count as inbound
 *     traffic, so a peer whose frames over the heartbeat interval and the
 *     TestRequest grace window are all faulty ends the session;
 *   - on an established session, a frame the header scan finds fault-free but
 *     the session cannot parse for dispatch (one that exhausts the per-message
 *     parse arena, for example), which was dropped while the session
 *     continued, ends the session (amended in C-ABI 1.11, 093: a frame the
 *     session admits now always parses for dispatch, so this no longer
 *     occurs);
 *   - an inbound message that would advance the expected inbound sequence
 *     number past its maximum (4294967295), where that number wrapped to 0 and
 *     the session continued, ends the session with no Reject and no Logout
 *     (092 FR-019).
 * On a session one of these effects refused or ended, `cb` is not invoked for
 * a send issued after that effect: fixpp_session_send refuses that send at the
 * engine's Active check, before it reaches the toApp path.
 *
 * BREAKING (C-ABI 1.11; 093, fixpp#514, #515, #516): the session frames and
 * disposes of inbound bytes as follows, where it did otherwise. Most of these
 * effects keep up, or let establish, a session that ended or did not
 * establish; the establishment timeout, and a Logon disregarded where it was
 * accepted, do the reverse:
 *   - a frame the Framer finds garbled (a wrong BodyLength or CheckSum, bytes
 *     before a frame start, or a BeginString or a BodyLength digit run longer
 *     than its cap) is disregarded and counted
 *     (fixpp_session_garbled_frame_count), in every state, where it ended the
 *     session; the two over-cap shapes were framed before;
 *   - a frame whose third field is not MsgType(35) is disregarded and counted
 *     in every state but Disconnected, where it was processed; a Logon of
 *     that shape is disregarded where it was accepted or refused (for a
 *     malformed tag or a malformed Length+Data count, among others);
 *   - a frame of at most the inbound limit (64 KiB through the C ABI) is
 *     admitted however the stream is split into reads, where some near that
 *     size ended the session; a frame over the limit ends the connection, in
 *     every state, as soon as its BodyLength(9) is read;
 *   - a frame the session admits always parses for dispatch, where one dense
 *     with fields could exhaust the parse buffer and end the session;
 *   - a connection not logged on by the establishment timeout
 *     (fixpp_session_config_set_logon_timeout_ms; 10 s by default) is
 *     closed, including one whose Logon was refused, where it stayed open; a
 *     peer that answers the Logon later than that no longer establishes;
 *   - every frame that passes the fault and third-field checks counts as
 *     inbound traffic for the heartbeat interval, where some that returned
 *     early did not (one above or below the expected MsgSeqNum(34), a
 *     SequenceReset, a Reject, among others); the TestRequest such traffic
 *     drew is not sent, and a session that ended when it went unanswered
 *     stays up.
 * On a session one of these effects keeps up or lets establish, `cb` is invoked
 * for a send issued after the effect, where fixpp_session_send refused that
 * send at the engine's Active check; on one it ends or keeps from
 * establishing, the reverse.
 *
 * Reentrancy: single-thread. THUNK: construction-time. The installed callback
 * runs on the session strand (see fixpp_send_cb typedef; [contracts/toapp-callback.md]).
 */
FIXPP_API_EXPORT fixpp_error_t fixpp_session_register_send_callback(
    fixpp_session_t* session, fixpp_send_cb cb, void* userdata);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FIXPP_C_API_SESSION_H */
