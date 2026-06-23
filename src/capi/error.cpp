// src/capi/error.cpp — C-ABI error surface (CA-002, T011)
// [2i §4.3/§4.4] / data-model E-3/E-5 / research D-1/D-2/D-3/D-8.
//
// translate() and translate_for_consumer() are engine-internal
// (namespace fixpp_capi::detail, NOT exported — preserves §X.2: the C
// consumer never sees fixpp::core::error). Only fixpp_strerror crosses the
// extern "C" boundary.
//
// translate() is a TOTAL switch with NO default — -Wswitch enforces that every
// one of the 116 fixpp::core::error enumerators is mapped (totality). Totality
// is NOT correctness: the per-arm coalescing is the audited E-3 decision,
// verified by the enumerating correctness-oracle test against the checked-in
// expected_error_map.csv (mutation-tested). The override groups
// (session_*/log_*/otel_*/app_*/out_of_memory) map to FIXPP_ERR_UNKNOWN as
// documented v1.0 behaviour (L-049-2); Feature B publishes FIXPP_ERR_SESSION_*.

#include "fix/c_api/error.h"

#include "fixpp/core/error.hpp"

#include <cstdint>

namespace fixpp_capi::detail {

using fixpp::core::error;

// translate — coalesce a fixpp::core::error onto its published fixpp_error_t.
// Total switch, no default (see file header). 116 arms per data-model E-3 /
// expected_error_map.csv.
fixpp_error_t translate(error e) noexcept {
    switch (e) {
        // ── cross-cutting: out_of_memory (slot 1) — OVERRIDE ───────────────
        // No cross-cutting OOM code in [2i §4.3]; a switch(error) cannot be
        // call-site-dependent. Domain OOM (dict_xml_oom) maps to its own code.
        case error::out_of_memory:
            return FIXPP_ERR_UNKNOWN;

        // ── decimal (slots 10-13) ──────────────────────────────────────────
        case error::decimal_invalid_input:
        case error::decimal_overflow:  // preserved: slot 11 coalesces to INVALID
            return FIXPP_ERR_DECIMAL_INVALID;
        case error::decimal_precision_loss:
            return FIXPP_ERR_DECIMAL_PRECISION_LOSS;
        case error::decimal_buffer_too_small:
            return FIXPP_ERR_BUFFER_TOO_SMALL;

        // ── dict (slots 20-29) — [2c §6.7]: config-class → DICT_CONFIG;
        //    *_oom → DICT_OOM ([2i §4.3]:523) ─────────────────────────────────
        case error::dict_xml_parse_failed:
        case error::dict_unknown_version:
        case error::dict_reify_msg_type_mismatch:
        case error::dict_reify_unknown_msg_type:
        case error::dict_unresolved_application_version:
        case error::dict_unknown_appl_ver_id:
        case error::dict_no_dictionary_for_application_version:
        case error::dict_reify_wire_body_not_ready:
            return FIXPP_ERR_DICT_CONFIG;
        case error::dict_xml_oom:
        case error::dict_reify_oom:
            return FIXPP_ERR_DICT_OOM;

        // ── wire (slots 30-42) — [2b §6.7]: framing→INVALID_FRAME,
        //    capacity→LIMIT_EXCEEDED, conformance→CONFORMANCE,
        //    truncation→DECIMAL_PRECISION_LOSS ─────────────────────────────────
        case error::wire_invalid_body_length:
        case error::wire_checksum_mismatch:
        case error::wire_framing_resync:
        case error::wire_invalid_field_format:
            return FIXPP_ERR_WIRE_INVALID_FRAME;
        case error::wire_frame_too_large:
        case error::wire_offset_table_full:
        case error::wire_group_too_large:
        case error::wire_tag_out_of_range:
            return FIXPP_ERR_WIRE_LIMIT_EXCEEDED;
        case error::wire_required_field_missing:
        case error::wire_header_out_of_order:
        case error::wire_field_value_out_of_range:
        case error::wire_unexpected_tag:
            return FIXPP_ERR_WIRE_CONFORMANCE;
        case error::wire_field_value_truncated:
            return FIXPP_ERR_DECIMAL_PRECISION_LOSS;

        // ── sync (slots 43-46) ─────────────────────────────────────────────
        case error::sync_lock_aborted:  // "Joins FIXPP_ERR_CANCELLED" (error.hpp:86)
            return FIXPP_ERR_CANCELLED;
        case error::sync_lock_alloc_failed:
        case error::sync_lock_outside_session:
        case error::sync_lock_drained:
            return FIXPP_ERR_SYNC_RUNTIME;

        // ── threading (slots 47-55) — four targets ─────────────────────────
        case error::executor_already_stopped:
        case error::executor_not_serialised:
            return FIXPP_ERR_THREAD_CONFIG;
        case error::clock_sleeps_cancelled:  // "Joins CANCELLED" (error.hpp:131)
            return FIXPP_ERR_CANCELLED;
        case error::strand_dispatch_failed_oom:
            return FIXPP_ERR_THREAD_RUNTIME;
        case error::session_already_open:
        case error::session_already_closed:
            return FIXPP_ERR_THREAD_SESSION_LIFECYCLE;
        case error::invalid_session_config:
        case error::clock_not_set:
            return FIXPP_ERR_THREAD_CONFIG;
        case error::dispatch_aborted:  // "Joins CANCELLED (reused)" (error.hpp:163)
            return FIXPP_ERR_CANCELLED;

        // ── store (slots 56-65) — grouped ← prose (error.hpp:172-181) ──────
        case error::store_io_failure:
        case error::store_capacity_exhausted:
        case error::store_seqnum_overflow:
            return FIXPP_ERR_STORE_RUNTIME;
        case error::store_seqnum_gap:
        case error::store_seqnum_out_of_order:
        case error::store_seqnum_invalid:
        case error::store_invalid_range:
            return FIXPP_ERR_STORE_CONSISTENCY;
        case error::store_factory_failed:
            return FIXPP_ERR_STORE_CONFIG;
        case error::store_visitor_aborted:
            return FIXPP_ERR_STORE_VISITOR;
        case error::store_cancelled:  // reused (error.hpp:181)
            return FIXPP_ERR_CANCELLED;

        // ── session (slots 66-77 + 116-121) — OVERRIDE + L-049-2 ───────────
        // [2i §4.3] publishes NO session block (D-8); no Feature-A producer.
        // Feature B publishes the FIXPP_ERR_SESSION_* block + amends [2i].
        case error::session_invalid_logon:
        case error::session_compid_mismatch:
        case error::session_begin_string_unsupported:
        case error::session_seqnum_too_low:
        case error::session_sending_time_accuracy:
        case error::session_msg_type_invalid_for_state:
        case error::session_logout_timeout:
        case error::session_test_request_unanswered:
        case error::session_admin_not_supported:
        case error::session_invalid_config:
        case error::session_invalid_state_for_send:
        case error::session_seqnum_reset_mismatch:
        case error::session_compid_unauthorized:
        case error::session_testreqid_mismatch:
        case error::session_invalid_argument:
        case error::session_seqnum_too_high:
        case error::session_unknown_acceptor_session:
            return FIXPP_ERR_UNKNOWN;  // L-049-2: publish FIXPP_ERR_SESSION_* in Feature B

        // ── tls (slots 78-93) — grouped ← prose (error.hpp:340-356) ────────
        case error::tls_cert_load_failed:
        case error::tls_cert_parse_failed:
        case error::tls_cipher_not_allowed:
        case error::tls_invalid_security_profile:
        case error::tls_sign_callback_unavailable:
        case error::tls_pin_empty_at_open:
            return FIXPP_ERR_TLS_CONFIG;
        case error::tls_pin_not_found:
        case error::tls_pin_already_present:
        case error::tls_pinset_capacity_exhausted:
            return FIXPP_ERR_TLS_PINSET;
        case error::tls_pinset_alloc_failed:
            return FIXPP_ERR_TLS_RUNTIME;
        case error::tls_handshake_failed:
        case error::tls_rsa_key_too_large:
        case error::tls_cert_der_too_large:
        case error::tls_san_entries_exceeded:
        case error::tls_pin_mismatch:
            return FIXPP_ERR_TLS_HANDSHAKE;
        case error::tls_load_cancelled:  // reused (error.hpp:355)
            return FIXPP_ERR_CANCELLED;

        // ── transport (slots 94-115) — grouped ← prose (error.hpp:450-472) ─
        case error::transport_resolve_failed:
        case error::transport_connect_refused:
        case error::transport_connect_timeout:
        case error::transport_already_connected:
        case error::transport_already_closed:
        case error::transport_read_in_progress:
        case error::transport_write_in_progress:
        case error::transport_reconnect_limit_exceeded:
            return FIXPP_ERR_TRANSPORT_LIFECYCLE;
        case error::transport_read_eof:
        case error::transport_read_truncated:
        case error::transport_read_error:
        case error::transport_write_short:
        case error::transport_write_error:
            return FIXPP_ERR_TRANSPORT_IO;
        case error::transport_handshake_failed:
        case error::transport_handshake_timeout:
            return FIXPP_ERR_TRANSPORT_HANDSHAKE;
        case error::transport_factory_failed:
        case error::transport_psk_unsupported:
            return FIXPP_ERR_TRANSPORT_CONFIG;
        case error::transport_connect_cancelled:
        case error::transport_read_cancelled:
        case error::transport_write_cancelled:
        case error::transport_handshake_cancelled:
        case error::transport_accept_cancelled:
            return FIXPP_ERR_CANCELLED;

        // ── log/otel (slots 122-128) — OVERRIDE + L-049-2 ──────────────────
        // [2i §4.3] publishes no #define for the 1000-1099 block in v1.0.
        case error::log_queue_overflow:
        case error::log_sink_open_failed:
        case error::log_sink_write_failed:
        case error::log_sink_flush_failed:
        case error::log_drain_timeout:
        case error::otel_export_failed:
        case error::otel_provider_init_failed:
            return FIXPP_ERR_UNKNOWN;  // L-049-2: log/otel C-ABI block deferred

        // ── app (slots 129-131) — OVERRIDE ─────────────────────────────────
        // error.hpp annotates these "C-ABI reserved (future mapping)".
        case error::app_do_not_send:
        case error::app_callback_threw:
        case error::app_payload_malformed:
            return FIXPP_ERR_UNKNOWN;
    }
    // No default above → -Wswitch proves all 116 enumerators are handled.
    // Reached only for an out-of-range value cast into `error` (not a real
    // enumerator); fail safe to UNKNOWN.
    return FIXPP_ERR_UNKNOWN;
}

// translate_for_consumer — forward-compat downgrade (E-5 / research D-3).
// Every code published in C-ABI 0.2.0 has introducing_minor == 2 (the .2 of
// 0.2.0; [const §X.4] keys downgrade on the consumer's published ABI minor and
// stability binds only at MAJOR==1). When a later minor adds codes this
// becomes a per-code lookup seeded from tools/abi_history/error_codes_v1.txt's
// introducing column. For Feature A the value is uniform.
fixpp_error_t translate_for_consumer(fixpp_error_t code, std::uint16_t consumer_minor) noexcept {
    constexpr std::uint16_t kIntroducingMinor = 2;  // all current published codes
    if (kIntroducingMinor > consumer_minor) {
        return FIXPP_ERR_UNKNOWN;  // code is newer than the consumer's ABI minor
    }
    return code;
}

}  // namespace fixpp_capi::detail

// fixpp_strerror — exported. Returns a pointer into static storage (string
// literals have static lifetime; the same literal address is returned on every
// call → zero allocation, stable pointer). Out-of-range/undefined/reserved →
// "unknown error".
extern "C" const char* fixpp_strerror(fixpp_error_t code) {
    switch (code) {
        // cross-cutting
        case FIXPP_ERR_OK:                       return "no error";
        case FIXPP_ERR_CANCELLED:                return "operation cancelled";
        case FIXPP_ERR_UNKNOWN:                  return "unknown error code";
        case FIXPP_ERR_NULL_HANDLE:              return "null handle";
        case FIXPP_ERR_INVALID_HANDLE:           return "invalid or destroyed handle";
        case FIXPP_ERR_VERSION_MISMATCH:         return "C ABI major version mismatch";
        case FIXPP_ERR_BUFFER_TOO_SMALL:         return "output buffer too small";
        case FIXPP_ERR_TYPE_MISMATCH:            return "type mismatch";
        case FIXPP_ERR_TAG_NOT_FOUND:            return "tag not found";
        case FIXPP_ERR_INDEX_OUT_OF_RANGE:       return "index out of range";
        case FIXPP_ERR_CAPI_CONFIG_INVALID:      return "C ABI configuration invalid";
        // wire
        case FIXPP_ERR_WIRE_INVALID_FRAME:       return "wire: invalid frame";
        case FIXPP_ERR_WIRE_LIMIT_EXCEEDED:      return "wire: limit exceeded";
        case FIXPP_ERR_WIRE_CONFORMANCE:         return "wire: conformance violation";
        // dict
        case FIXPP_ERR_DICT_CONFIG:              return "dictionary: configuration error";
        case FIXPP_ERR_DICT_LIMIT_EXCEEDED:      return "dictionary: limit exceeded";
        case FIXPP_ERR_DICT_OOM:                 return "dictionary: out of memory";
        // threading
        case FIXPP_ERR_THREAD_CONFIG:            return "threading: configuration error";
        case FIXPP_ERR_THREAD_SESSION_LIFECYCLE: return "threading: session lifecycle error";
        case FIXPP_ERR_THREAD_RUNTIME:           return "threading: runtime error";
        // store
        case FIXPP_ERR_STORE_RUNTIME:            return "store: runtime error";
        case FIXPP_ERR_STORE_CONSISTENCY:        return "store: consistency violation";
        case FIXPP_ERR_STORE_CONFIG:             return "store: configuration error";
        case FIXPP_ERR_STORE_VISITOR:            return "store: visitor aborted";
        // sync
        case FIXPP_ERR_SYNC_RUNTIME:             return "sync: runtime error";
        // tls
        case FIXPP_ERR_TLS_CONFIG:               return "TLS: configuration error";
        case FIXPP_ERR_TLS_HANDSHAKE:            return "TLS: handshake failed";
        case FIXPP_ERR_TLS_PINSET:               return "TLS: pinset error";
        case FIXPP_ERR_TLS_RUNTIME:              return "TLS: runtime error";
        // transport
        case FIXPP_ERR_TRANSPORT_LIFECYCLE:      return "transport: lifecycle error";
        case FIXPP_ERR_TRANSPORT_IO:             return "transport: I/O error";
        case FIXPP_ERR_TRANSPORT_HANDSHAKE:      return "transport: handshake error";
        case FIXPP_ERR_TRANSPORT_CONFIG:         return "transport: configuration error";
        // decimal
        case FIXPP_ERR_DECIMAL_INVALID:          return "decimal: invalid value";
        case FIXPP_ERR_DECIMAL_PRECISION_LOSS:   return "decimal: precision loss";
        // control-plane
        case FIXPP_ERR_CTRL_CONFIG:              return "control-plane: configuration error";
        case FIXPP_ERR_CTRL_RUNTIME:             return "control-plane: runtime error";
        // bindings
        case FIXPP_ERR_BINDING_PYTHON_CALLBACK_RAISED:   return "binding: Python callback raised";
        case FIXPP_ERR_BINDING_SUBINTERPRETER:           return "binding: subinterpreter violation";
        case FIXPP_ERR_BINDING_OBJECT_LIFETIME:          return "binding: object lifetime violation";
        case FIXPP_ERR_BINDING_WHEEL_ABI_MISMATCH:       return "binding: wheel ABI mismatch";
        case FIXPP_ERR_BINDING_CALLBACK_REENTRANT_CLOSE: return "binding: reentrant close from callback";
        default:
            return "unknown error";
    }
}
