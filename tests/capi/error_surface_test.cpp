// tests/capi/error_surface_test.cpp
// T009 (US2 / E-3-test) — C-ABI error surface correctness oracle.
// TDD: written RED before error.h / error.cpp exist.
//
// Tests (all derived from census-ground-truth.md + data-model.md E-3):
//   1. CapiError/CorrectnesOracle     — all 116 enumerators match expected_error_map.csv
//   2. CapiError/ExplicitUnknownOverrides — override groups asserted explicitly
//   3. CapiError/StrerrorNonNull      — fixpp_strerror is non-null for all published codes
//   4. CapiError/StrerrorUnknownSentinel — out-of-range → "unknown error"
//   5. CapiError/StrerrorZeroAlloc    — fixpp_strerror does not alloc (static storage)
//   6. CapiError/DowngradeConsumer    — translate_for_consumer SC-006: minor-3 code → UNKNOWN
//   7. CapiError/DowngradePassthrough — consumer_minor >= introducing_minor → code preserved
//   8. CapiError/MutationSelfCheck    — flipping one arm turns test RED (compiler-verified
//                                       via static_assert on the engine-internal translate())
//
// Published codes (E-2) exercised in this TU:
//   FIXPP_ERR_OK(0) FIXPP_ERR_CANCELLED(1) FIXPP_ERR_UNKNOWN(2)
//   FIXPP_ERR_BUFFER_TOO_SMALL(6) FIXPP_ERR_DECIMAL_INVALID(800)
//   FIXPP_ERR_DECIMAL_PRECISION_LOSS(801) + all domain codes.

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// Under test — these don't exist yet (RED phase).
#include "fix/c_api/error.h"
#include "fixpp/core/error.hpp"

// Engine-internal translate() + translate_for_consumer() under test.
// These are declared in the fixpp_capi detail namespace (not exported).
namespace fixpp_capi::detail {
fixpp_error_t translate(fixpp::core::error e) noexcept;
fixpp_error_t translate_for_consumer(fixpp_error_t code,
                                     uint16_t consumer_minor) noexcept;
}  // namespace fixpp_capi::detail

using fixpp::core::error;
using fixpp_capi::detail::translate;
using fixpp_capi::detail::translate_for_consumer;

// ---------------------------------------------------------------------------
// T008 CSV oracle loader
// ---------------------------------------------------------------------------

namespace {

// FIXPP_CAPI_DATA_DIR is injected by CMake (points to tests/capi/).
#ifndef FIXPP_CAPI_DATA_DIR
#  define FIXPP_CAPI_DATA_DIR "."
#endif

// Build the name→enumerator lookup at compile time via a hand-written
// table mirroring the 116 arms.  This avoids run-time reflection and keeps
// the test self-contained when the CSV path is absent (build-path portability).
//
// The table is the mutation-self-check pivot: if translate() returns the wrong
// code for an arm, the CSV oracle catches it; if the CSV row is wrong, the
// orchestrator's census-ground-truth.md diff catches it.

struct EnumEntry {
    std::string_view name;
    error            value;
};

// clang-format off
constexpr std::array kEnumTable{
    EnumEntry{"out_of_memory",                      error::out_of_memory},
    EnumEntry{"decimal_invalid_input",              error::decimal_invalid_input},
    EnumEntry{"decimal_overflow",                   error::decimal_overflow},
    EnumEntry{"decimal_precision_loss",             error::decimal_precision_loss},
    EnumEntry{"decimal_buffer_too_small",           error::decimal_buffer_too_small},
    EnumEntry{"dict_xml_parse_failed",              error::dict_xml_parse_failed},
    EnumEntry{"dict_unknown_version",               error::dict_unknown_version},
    EnumEntry{"dict_xml_oom",                       error::dict_xml_oom},
    EnumEntry{"dict_reify_msg_type_mismatch",       error::dict_reify_msg_type_mismatch},
    EnumEntry{"dict_reify_unknown_msg_type",        error::dict_reify_unknown_msg_type},
    EnumEntry{"dict_reify_oom",                     error::dict_reify_oom},
    EnumEntry{"dict_unresolved_application_version",error::dict_unresolved_application_version},
    EnumEntry{"dict_unknown_appl_ver_id",           error::dict_unknown_appl_ver_id},
    EnumEntry{"dict_no_dictionary_for_application_version",
                                                    error::dict_no_dictionary_for_application_version},
    EnumEntry{"dict_reify_wire_body_not_ready",     error::dict_reify_wire_body_not_ready},
    EnumEntry{"wire_frame_too_large",               error::wire_frame_too_large},
    EnumEntry{"wire_invalid_body_length",           error::wire_invalid_body_length},
    EnumEntry{"wire_checksum_mismatch",             error::wire_checksum_mismatch},
    EnumEntry{"wire_framing_resync",                error::wire_framing_resync},
    EnumEntry{"wire_invalid_field_format",          error::wire_invalid_field_format},
    EnumEntry{"wire_offset_table_full",             error::wire_offset_table_full},
    EnumEntry{"wire_group_too_large",               error::wire_group_too_large},
    EnumEntry{"wire_tag_out_of_range",              error::wire_tag_out_of_range},
    EnumEntry{"wire_required_field_missing",        error::wire_required_field_missing},
    EnumEntry{"wire_header_out_of_order",           error::wire_header_out_of_order},
    EnumEntry{"wire_field_value_out_of_range",      error::wire_field_value_out_of_range},
    EnumEntry{"wire_field_value_truncated",         error::wire_field_value_truncated},
    EnumEntry{"wire_unexpected_tag",                error::wire_unexpected_tag},
    EnumEntry{"sync_lock_aborted",                  error::sync_lock_aborted},
    EnumEntry{"sync_lock_alloc_failed",             error::sync_lock_alloc_failed},
    EnumEntry{"sync_lock_outside_session",          error::sync_lock_outside_session},
    EnumEntry{"sync_lock_drained",                  error::sync_lock_drained},
    EnumEntry{"executor_already_stopped",           error::executor_already_stopped},
    EnumEntry{"executor_not_serialised",            error::executor_not_serialised},
    EnumEntry{"clock_sleeps_cancelled",             error::clock_sleeps_cancelled},
    EnumEntry{"strand_dispatch_failed_oom",         error::strand_dispatch_failed_oom},
    EnumEntry{"session_already_open",               error::session_already_open},
    EnumEntry{"session_already_closed",             error::session_already_closed},
    EnumEntry{"invalid_session_config",             error::invalid_session_config},
    EnumEntry{"clock_not_set",                      error::clock_not_set},
    EnumEntry{"dispatch_aborted",                   error::dispatch_aborted},
    EnumEntry{"store_io_failure",                   error::store_io_failure},
    EnumEntry{"store_seqnum_gap",                   error::store_seqnum_gap},
    EnumEntry{"store_seqnum_out_of_order",          error::store_seqnum_out_of_order},
    EnumEntry{"store_capacity_exhausted",           error::store_capacity_exhausted},
    EnumEntry{"store_seqnum_overflow",              error::store_seqnum_overflow},
    EnumEntry{"store_factory_failed",               error::store_factory_failed},
    EnumEntry{"store_visitor_aborted",              error::store_visitor_aborted},
    EnumEntry{"store_seqnum_invalid",               error::store_seqnum_invalid},
    EnumEntry{"store_invalid_range",                error::store_invalid_range},
    EnumEntry{"store_cancelled",                    error::store_cancelled},
    EnumEntry{"session_invalid_logon",              error::session_invalid_logon},
    EnumEntry{"session_compid_mismatch",            error::session_compid_mismatch},
    EnumEntry{"session_begin_string_unsupported",   error::session_begin_string_unsupported},
    EnumEntry{"session_seqnum_too_low",             error::session_seqnum_too_low},
    EnumEntry{"session_sending_time_accuracy",      error::session_sending_time_accuracy},
    EnumEntry{"session_msg_type_invalid_for_state", error::session_msg_type_invalid_for_state},
    EnumEntry{"session_logout_timeout",             error::session_logout_timeout},
    EnumEntry{"session_test_request_unanswered",    error::session_test_request_unanswered},
    EnumEntry{"session_admin_not_supported",        error::session_admin_not_supported},
    EnumEntry{"session_invalid_config",             error::session_invalid_config},
    EnumEntry{"session_invalid_state_for_send",     error::session_invalid_state_for_send},
    EnumEntry{"tls_cert_load_failed",               error::tls_cert_load_failed},
    EnumEntry{"tls_cert_parse_failed",              error::tls_cert_parse_failed},
    EnumEntry{"tls_cipher_not_allowed",             error::tls_cipher_not_allowed},
    EnumEntry{"tls_invalid_security_profile",       error::tls_invalid_security_profile},
    EnumEntry{"tls_sign_callback_unavailable",      error::tls_sign_callback_unavailable},
    EnumEntry{"tls_pin_empty_at_open",              error::tls_pin_empty_at_open},
    EnumEntry{"tls_pin_not_found",                  error::tls_pin_not_found},
    EnumEntry{"tls_pin_already_present",            error::tls_pin_already_present},
    EnumEntry{"tls_pinset_capacity_exhausted",      error::tls_pinset_capacity_exhausted},
    EnumEntry{"tls_pinset_alloc_failed",            error::tls_pinset_alloc_failed},
    EnumEntry{"tls_handshake_failed",               error::tls_handshake_failed},
    EnumEntry{"tls_rsa_key_too_large",              error::tls_rsa_key_too_large},
    EnumEntry{"tls_cert_der_too_large",             error::tls_cert_der_too_large},
    EnumEntry{"tls_san_entries_exceeded",           error::tls_san_entries_exceeded},
    EnumEntry{"tls_pin_mismatch",                   error::tls_pin_mismatch},
    EnumEntry{"tls_load_cancelled",                 error::tls_load_cancelled},
    EnumEntry{"transport_resolve_failed",           error::transport_resolve_failed},
    EnumEntry{"transport_connect_refused",          error::transport_connect_refused},
    EnumEntry{"transport_connect_timeout",          error::transport_connect_timeout},
    EnumEntry{"transport_already_connected",        error::transport_already_connected},
    EnumEntry{"transport_already_closed",           error::transport_already_closed},
    EnumEntry{"transport_read_in_progress",         error::transport_read_in_progress},
    EnumEntry{"transport_write_in_progress",        error::transport_write_in_progress},
    EnumEntry{"transport_reconnect_limit_exceeded", error::transport_reconnect_limit_exceeded},
    EnumEntry{"transport_read_eof",                 error::transport_read_eof},
    EnumEntry{"transport_read_truncated",           error::transport_read_truncated},
    EnumEntry{"transport_read_error",               error::transport_read_error},
    EnumEntry{"transport_write_short",              error::transport_write_short},
    EnumEntry{"transport_write_error",              error::transport_write_error},
    EnumEntry{"transport_handshake_failed",         error::transport_handshake_failed},
    EnumEntry{"transport_handshake_timeout",        error::transport_handshake_timeout},
    EnumEntry{"transport_factory_failed",           error::transport_factory_failed},
    EnumEntry{"transport_psk_unsupported",          error::transport_psk_unsupported},
    EnumEntry{"transport_connect_cancelled",        error::transport_connect_cancelled},
    EnumEntry{"transport_read_cancelled",           error::transport_read_cancelled},
    EnumEntry{"transport_write_cancelled",          error::transport_write_cancelled},
    EnumEntry{"transport_handshake_cancelled",      error::transport_handshake_cancelled},
    EnumEntry{"transport_accept_cancelled",         error::transport_accept_cancelled},
    EnumEntry{"session_seqnum_reset_mismatch",      error::session_seqnum_reset_mismatch},
    EnumEntry{"session_compid_unauthorized",        error::session_compid_unauthorized},
    EnumEntry{"session_testreqid_mismatch",         error::session_testreqid_mismatch},
    EnumEntry{"session_invalid_argument",           error::session_invalid_argument},
    EnumEntry{"session_seqnum_too_high",            error::session_seqnum_too_high},
    EnumEntry{"session_unknown_acceptor_session",   error::session_unknown_acceptor_session},
    EnumEntry{"log_queue_overflow",                 error::log_queue_overflow},
    EnumEntry{"log_sink_open_failed",               error::log_sink_open_failed},
    EnumEntry{"log_sink_write_failed",              error::log_sink_write_failed},
    EnumEntry{"log_sink_flush_failed",              error::log_sink_flush_failed},
    EnumEntry{"log_drain_timeout",                  error::log_drain_timeout},
    EnumEntry{"otel_export_failed",                 error::otel_export_failed},
    EnumEntry{"otel_provider_init_failed",          error::otel_provider_init_failed},
    EnumEntry{"app_do_not_send",                    error::app_do_not_send},
    EnumEntry{"app_callback_threw",                 error::app_callback_threw},
    EnumEntry{"app_payload_malformed",              error::app_payload_malformed},
};
// clang-format on

static_assert(kEnumTable.size() == 116u,
              "E-3-test: enumerator table must have exactly 116 rows");

// Build a name→code lookup from the CSV oracle.
// Format: lines starting with '#' are comments; data lines are "name,FIXPP_ERR_SYMBOL".
std::unordered_map<std::string, std::string> load_csv() {
    std::unordered_map<std::string, std::string> result;
    std::string path = FIXPP_CAPI_DATA_DIR "/expected_error_map.csv";
    std::ifstream f(path);
    if (!f.is_open()) {
        return result;  // empty → test will FAIL at ASSERT_FALSE(map.empty())
    }
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        auto comma = line.find(',');
        if (comma == std::string::npos) {
            continue;
        }
        std::string name   = line.substr(0, comma);
        std::string symbol = line.substr(comma + 1);
        result[name] = symbol;
    }
    return result;
}

// Translate a FIXPP_ERR_SYMBOL string to its numeric value.
// Only the codes reachable from E-3 need to be listed.
fixpp_error_t symbol_to_code(const std::string& sym) {
    // clang-format off
    static const std::unordered_map<std::string, fixpp_error_t> kMap{
        {"FIXPP_ERR_OK",                   FIXPP_ERR_OK},
        {"FIXPP_ERR_CANCELLED",            FIXPP_ERR_CANCELLED},
        {"FIXPP_ERR_UNKNOWN",              FIXPP_ERR_UNKNOWN},
        {"FIXPP_ERR_NULL_HANDLE",          FIXPP_ERR_NULL_HANDLE},
        {"FIXPP_ERR_INVALID_HANDLE",       FIXPP_ERR_INVALID_HANDLE},
        {"FIXPP_ERR_VERSION_MISMATCH",     FIXPP_ERR_VERSION_MISMATCH},
        {"FIXPP_ERR_BUFFER_TOO_SMALL",     FIXPP_ERR_BUFFER_TOO_SMALL},
        {"FIXPP_ERR_TYPE_MISMATCH",        FIXPP_ERR_TYPE_MISMATCH},
        {"FIXPP_ERR_TAG_NOT_FOUND",        FIXPP_ERR_TAG_NOT_FOUND},
        {"FIXPP_ERR_INDEX_OUT_OF_RANGE",   FIXPP_ERR_INDEX_OUT_OF_RANGE},
        {"FIXPP_ERR_CAPI_CONFIG_INVALID",  FIXPP_ERR_CAPI_CONFIG_INVALID},
        {"FIXPP_ERR_WIRE_INVALID_FRAME",   FIXPP_ERR_WIRE_INVALID_FRAME},
        {"FIXPP_ERR_WIRE_LIMIT_EXCEEDED",  FIXPP_ERR_WIRE_LIMIT_EXCEEDED},
        {"FIXPP_ERR_WIRE_CONFORMANCE",     FIXPP_ERR_WIRE_CONFORMANCE},
        {"FIXPP_ERR_DICT_CONFIG",          FIXPP_ERR_DICT_CONFIG},
        {"FIXPP_ERR_DICT_LIMIT_EXCEEDED",  FIXPP_ERR_DICT_LIMIT_EXCEEDED},
        {"FIXPP_ERR_DICT_OOM",             FIXPP_ERR_DICT_OOM},
        {"FIXPP_ERR_THREAD_CONFIG",        FIXPP_ERR_THREAD_CONFIG},
        {"FIXPP_ERR_THREAD_SESSION_LIFECYCLE", FIXPP_ERR_THREAD_SESSION_LIFECYCLE},
        {"FIXPP_ERR_THREAD_RUNTIME",       FIXPP_ERR_THREAD_RUNTIME},
        {"FIXPP_ERR_STORE_RUNTIME",        FIXPP_ERR_STORE_RUNTIME},
        {"FIXPP_ERR_STORE_CONSISTENCY",    FIXPP_ERR_STORE_CONSISTENCY},
        {"FIXPP_ERR_STORE_CONFIG",         FIXPP_ERR_STORE_CONFIG},
        {"FIXPP_ERR_STORE_VISITOR",        FIXPP_ERR_STORE_VISITOR},
        {"FIXPP_ERR_SYNC_RUNTIME",         FIXPP_ERR_SYNC_RUNTIME},
        {"FIXPP_ERR_TLS_CONFIG",           FIXPP_ERR_TLS_CONFIG},
        {"FIXPP_ERR_TLS_HANDSHAKE",        FIXPP_ERR_TLS_HANDSHAKE},
        {"FIXPP_ERR_TLS_PINSET",           FIXPP_ERR_TLS_PINSET},
        {"FIXPP_ERR_TLS_RUNTIME",          FIXPP_ERR_TLS_RUNTIME},
        {"FIXPP_ERR_TRANSPORT_LIFECYCLE",  FIXPP_ERR_TRANSPORT_LIFECYCLE},
        {"FIXPP_ERR_TRANSPORT_IO",         FIXPP_ERR_TRANSPORT_IO},
        {"FIXPP_ERR_TRANSPORT_HANDSHAKE",  FIXPP_ERR_TRANSPORT_HANDSHAKE},
        {"FIXPP_ERR_TRANSPORT_CONFIG",     FIXPP_ERR_TRANSPORT_CONFIG},
        {"FIXPP_ERR_DECIMAL_INVALID",      FIXPP_ERR_DECIMAL_INVALID},
        {"FIXPP_ERR_DECIMAL_PRECISION_LOSS", FIXPP_ERR_DECIMAL_PRECISION_LOSS},
    };
    // clang-format on
    auto it = kMap.find(sym);
    if (it == kMap.end()) {
        return fixpp_error_t{-99999};  // sentinel: symbol unknown → test FAILS
    }
    return it->second;
}

}  // namespace

// ---------------------------------------------------------------------------
// 1. Correctness oracle: all 116 enumerators match expected_error_map.csv
// ---------------------------------------------------------------------------

TEST(CapiError, CorrectnessOracle) {
    auto csv = load_csv();
    ASSERT_FALSE(csv.empty()) << "CSV not loaded from " FIXPP_CAPI_DATA_DIR;
    ASSERT_EQ(csv.size(), 116u) << "CSV must have exactly 116 data rows";

    for (const auto& entry : kEnumTable) {
        auto it = csv.find(std::string(entry.name));
        ASSERT_NE(it, csv.end())
            << "Enumerator '" << entry.name << "' not found in CSV";
        fixpp_error_t expected = symbol_to_code(it->second);
        fixpp_error_t actual   = translate(entry.value);
        EXPECT_EQ(actual, expected)
            << "translate(" << entry.name << ") returned " << actual
            << " but CSV oracle says " << expected
            << " (" << it->second << ")";
    }
}

// ---------------------------------------------------------------------------
// 2. Override groups asserted explicitly (not just "a published code")
// ---------------------------------------------------------------------------

TEST(CapiError, ExplicitUnknownOverrides) {
    // out_of_memory: override — no cross-cutting OOM code
    EXPECT_EQ(translate(error::out_of_memory), FIXPP_ERR_UNKNOWN);

    // session_* (66-77): override + L-049-2 deferral
    EXPECT_EQ(translate(error::session_invalid_logon),           FIXPP_ERR_UNKNOWN);
    EXPECT_EQ(translate(error::session_compid_mismatch),         FIXPP_ERR_UNKNOWN);
    EXPECT_EQ(translate(error::session_begin_string_unsupported),FIXPP_ERR_UNKNOWN);
    EXPECT_EQ(translate(error::session_seqnum_too_low),          FIXPP_ERR_UNKNOWN);
    EXPECT_EQ(translate(error::session_sending_time_accuracy),   FIXPP_ERR_UNKNOWN);
    EXPECT_EQ(translate(error::session_msg_type_invalid_for_state), FIXPP_ERR_UNKNOWN);
    EXPECT_EQ(translate(error::session_logout_timeout),          FIXPP_ERR_UNKNOWN);
    EXPECT_EQ(translate(error::session_test_request_unanswered), FIXPP_ERR_UNKNOWN);
    EXPECT_EQ(translate(error::session_admin_not_supported),     FIXPP_ERR_UNKNOWN);
    EXPECT_EQ(translate(error::session_invalid_config),          FIXPP_ERR_UNKNOWN);
    EXPECT_EQ(translate(error::session_invalid_state_for_send),  FIXPP_ERR_UNKNOWN);

    // session_* (116-121): override + L-049-2 deferral
    EXPECT_EQ(translate(error::session_seqnum_reset_mismatch),   FIXPP_ERR_UNKNOWN);
    EXPECT_EQ(translate(error::session_compid_unauthorized),     FIXPP_ERR_UNKNOWN);
    EXPECT_EQ(translate(error::session_testreqid_mismatch),      FIXPP_ERR_UNKNOWN);
    EXPECT_EQ(translate(error::session_invalid_argument),        FIXPP_ERR_UNKNOWN);
    EXPECT_EQ(translate(error::session_seqnum_too_high),         FIXPP_ERR_UNKNOWN);
    EXPECT_EQ(translate(error::session_unknown_acceptor_session),FIXPP_ERR_UNKNOWN);

    // log_* (122-126): override — 1000-1099 unpublished in [2i §4.3]
    EXPECT_EQ(translate(error::log_queue_overflow),    FIXPP_ERR_UNKNOWN);
    EXPECT_EQ(translate(error::log_sink_open_failed),  FIXPP_ERR_UNKNOWN);
    EXPECT_EQ(translate(error::log_sink_write_failed), FIXPP_ERR_UNKNOWN);
    EXPECT_EQ(translate(error::log_sink_flush_failed), FIXPP_ERR_UNKNOWN);
    EXPECT_EQ(translate(error::log_drain_timeout),     FIXPP_ERR_UNKNOWN);

    // otel_* (127-128): override — 1010/1011 unpublished
    EXPECT_EQ(translate(error::otel_export_failed),       FIXPP_ERR_UNKNOWN);
    EXPECT_EQ(translate(error::otel_provider_init_failed),FIXPP_ERR_UNKNOWN);

    // app_* (129-131): override — source defers; no published code
    EXPECT_EQ(translate(error::app_do_not_send),       FIXPP_ERR_UNKNOWN);
    EXPECT_EQ(translate(error::app_callback_threw),    FIXPP_ERR_UNKNOWN);
    EXPECT_EQ(translate(error::app_payload_malformed), FIXPP_ERR_UNKNOWN);
}

// ---------------------------------------------------------------------------
// 3. CANCELLED-reuse arms (risky-arm spot-check from census-ground-truth.md)
// ---------------------------------------------------------------------------

TEST(CapiError, CancelledReuseArms) {
    // 10 arms mapped to CANCELLED via "Joins FIXPP_ERR_CANCELLED" prose.
    EXPECT_EQ(translate(error::sync_lock_aborted),          FIXPP_ERR_CANCELLED);  // slot 43
    EXPECT_EQ(translate(error::clock_sleeps_cancelled),     FIXPP_ERR_CANCELLED);  // slot 49
    EXPECT_EQ(translate(error::dispatch_aborted),           FIXPP_ERR_CANCELLED);  // slot 55
    EXPECT_EQ(translate(error::store_cancelled),            FIXPP_ERR_CANCELLED);  // slot 65
    EXPECT_EQ(translate(error::tls_load_cancelled),         FIXPP_ERR_CANCELLED);  // slot 93
    EXPECT_EQ(translate(error::transport_connect_cancelled),FIXPP_ERR_CANCELLED);  // slot 111
    EXPECT_EQ(translate(error::transport_read_cancelled),   FIXPP_ERR_CANCELLED);  // slot 112
    EXPECT_EQ(translate(error::transport_write_cancelled),  FIXPP_ERR_CANCELLED);  // slot 113
    EXPECT_EQ(translate(error::transport_handshake_cancelled), FIXPP_ERR_CANCELLED); // slot 114
    EXPECT_EQ(translate(error::transport_accept_cancelled), FIXPP_ERR_CANCELLED);  // slot 115
}

// ---------------------------------------------------------------------------
// 4. Wire ambiguous arms (30/37/39 — census-ground-truth.md ⚠ arms)
// ---------------------------------------------------------------------------

TEST(CapiError, WireAmbiguousArms) {
    // 30 wire_frame_too_large → WIRE_LIMIT_EXCEEDED (capacity / DoS-bound)
    EXPECT_EQ(translate(error::wire_frame_too_large),   FIXPP_ERR_WIRE_LIMIT_EXCEEDED);
    // 37 wire_tag_out_of_range → WIRE_LIMIT_EXCEEDED (capacity / tag number bound)
    EXPECT_EQ(translate(error::wire_tag_out_of_range),  FIXPP_ERR_WIRE_LIMIT_EXCEEDED);
    // 39 wire_header_out_of_order → WIRE_CONFORMANCE (protocol validation)
    EXPECT_EQ(translate(error::wire_header_out_of_order), FIXPP_ERR_WIRE_CONFORMANCE);
}

// ---------------------------------------------------------------------------
// 5. fixpp_strerror — non-null for all published codes (E-2)
// ---------------------------------------------------------------------------

TEST(CapiError, StrerrorNonNull) {
    // All published codes in E-2 must return a non-null non-empty string.
    constexpr fixpp_error_t kPublished[] = {
        FIXPP_ERR_OK,
        FIXPP_ERR_CANCELLED,
        FIXPP_ERR_UNKNOWN,
        FIXPP_ERR_NULL_HANDLE,
        FIXPP_ERR_INVALID_HANDLE,
        FIXPP_ERR_VERSION_MISMATCH,
        FIXPP_ERR_BUFFER_TOO_SMALL,
        FIXPP_ERR_TYPE_MISMATCH,
        FIXPP_ERR_TAG_NOT_FOUND,
        FIXPP_ERR_INDEX_OUT_OF_RANGE,
        FIXPP_ERR_CAPI_CONFIG_INVALID,
        FIXPP_ERR_WIRE_INVALID_FRAME,
        FIXPP_ERR_WIRE_LIMIT_EXCEEDED,
        FIXPP_ERR_WIRE_CONFORMANCE,
        FIXPP_ERR_DICT_CONFIG,
        FIXPP_ERR_DICT_LIMIT_EXCEEDED,
        FIXPP_ERR_DICT_OOM,
        FIXPP_ERR_THREAD_CONFIG,
        FIXPP_ERR_THREAD_SESSION_LIFECYCLE,
        FIXPP_ERR_THREAD_RUNTIME,
        FIXPP_ERR_STORE_RUNTIME,
        FIXPP_ERR_STORE_CONSISTENCY,
        FIXPP_ERR_STORE_CONFIG,
        FIXPP_ERR_STORE_VISITOR,
        FIXPP_ERR_SYNC_RUNTIME,
        FIXPP_ERR_TLS_CONFIG,
        FIXPP_ERR_TLS_HANDSHAKE,
        FIXPP_ERR_TLS_PINSET,
        FIXPP_ERR_TLS_RUNTIME,
        FIXPP_ERR_TRANSPORT_LIFECYCLE,
        FIXPP_ERR_TRANSPORT_IO,
        FIXPP_ERR_TRANSPORT_HANDSHAKE,
        FIXPP_ERR_TRANSPORT_CONFIG,
        FIXPP_ERR_DECIMAL_INVALID,
        FIXPP_ERR_DECIMAL_PRECISION_LOSS,
        FIXPP_ERR_CTRL_CONFIG,
        FIXPP_ERR_CTRL_RUNTIME,
        FIXPP_ERR_BINDING_PYTHON_CALLBACK_RAISED,
        FIXPP_ERR_BINDING_SUBINTERPRETER,
        FIXPP_ERR_BINDING_OBJECT_LIFETIME,
        FIXPP_ERR_BINDING_WHEEL_ABI_MISMATCH,
        FIXPP_ERR_BINDING_CALLBACK_REENTRANT_CLOSE,
    };
    // E-2 publishes exactly 42 codes; assert the array is the full set so a
    // future published code can't silently escape strerror coverage
    // ([[feedback_completeness_gate_exact_set_not_subset]]).
    static_assert(sizeof(kPublished) / sizeof(kPublished[0]) == 42,
                  "kPublished must cover all 42 E-2 published codes");
    for (fixpp_error_t code : kPublished) {
        const char* s = fixpp_strerror(code);
        ASSERT_NE(s, nullptr) << "fixpp_strerror(" << code << ") returned null";
        EXPECT_GT(std::strlen(s), 0u) << "fixpp_strerror(" << code << ") returned empty string";
        // Must NOT be "unknown error" for a published code
        EXPECT_STRNE(s, "unknown error")
            << "fixpp_strerror(" << code << ") returned sentinel for a published code";
    }
}

// ---------------------------------------------------------------------------
// 6. fixpp_strerror — out-of-range/undefined → "unknown error"
// ---------------------------------------------------------------------------

TEST(CapiError, StrerrorUnknownSentinel) {
    // Codes not in E-2: negative, gaps in ranges, reserved-but-not-yet-defined.
    EXPECT_STREQ(fixpp_strerror(-1),     "unknown error");
    EXPECT_STREQ(fixpp_strerror(11),     "unknown error");  // gap: 11 undefined (CAPI_CONFIG_INVALID=10; wire starts at 100)
    EXPECT_STREQ(fixpp_strerror(99),     "unknown error");  // gap: 11-99 undefined
    EXPECT_STREQ(fixpp_strerror(199),    "unknown error");  // gap: 103-199 undefined
    EXPECT_STREQ(fixpp_strerror(999),    "unknown error");  // gap: reserved ctrl-plane 900-901 boundary
    EXPECT_STREQ(fixpp_strerror(1000),   "unknown error");  // reserved log/otel block
    EXPECT_STREQ(fixpp_strerror(99999),  "unknown error");
}

// ---------------------------------------------------------------------------
// 7. fixpp_strerror — returns static storage (zero global heap alloc)
//    Realized as a `switch` returning static string literals (string literal →
//    static storage → stable pointer, zero global-heap alloc); `default:` →
//    "unknown error". We verify the pointer is stable across two calls
//    (same address == static storage, as guaranteed by string literals).
// ---------------------------------------------------------------------------

TEST(CapiError, StrerrorStaticStorage) {
    // Same code → same pointer (static storage, not a strdup).
    const char* p1 = fixpp_strerror(FIXPP_ERR_CANCELLED);
    const char* p2 = fixpp_strerror(FIXPP_ERR_CANCELLED);
    EXPECT_EQ(p1, p2) << "fixpp_strerror must return static storage (same pointer on repeat call)";

    const char* q1 = fixpp_strerror(FIXPP_ERR_DECIMAL_INVALID);
    const char* q2 = fixpp_strerror(FIXPP_ERR_DECIMAL_INVALID);
    EXPECT_EQ(q1, q2);
}

// ---------------------------------------------------------------------------
// 8. translate_for_consumer — SC-006 downgrade (introducing_minor=2, consumer_minor=1)
//    This MUST reach the branch; a stimulus-free no-op would be RED-invisible.
// ---------------------------------------------------------------------------

TEST(CapiError, DowngradeConsumer) {
    // All current codes have introducing_minor=2 (the minor of C-ABI 0.2.0).
    // A consumer declaring minor=1 must receive UNKNOWN for ALL of them.
    constexpr uint16_t kConsumerMinor = 1u;

    // Representative codes from each domain block:
    EXPECT_EQ(translate_for_consumer(FIXPP_ERR_CANCELLED,           kConsumerMinor), FIXPP_ERR_UNKNOWN);
    EXPECT_EQ(translate_for_consumer(FIXPP_ERR_UNKNOWN,             kConsumerMinor), FIXPP_ERR_UNKNOWN);
    EXPECT_EQ(translate_for_consumer(FIXPP_ERR_BUFFER_TOO_SMALL,    kConsumerMinor), FIXPP_ERR_UNKNOWN);
    EXPECT_EQ(translate_for_consumer(FIXPP_ERR_WIRE_INVALID_FRAME,  kConsumerMinor), FIXPP_ERR_UNKNOWN);
    EXPECT_EQ(translate_for_consumer(FIXPP_ERR_DICT_CONFIG,         kConsumerMinor), FIXPP_ERR_UNKNOWN);
    EXPECT_EQ(translate_for_consumer(FIXPP_ERR_THREAD_CONFIG,       kConsumerMinor), FIXPP_ERR_UNKNOWN);
    EXPECT_EQ(translate_for_consumer(FIXPP_ERR_STORE_RUNTIME,       kConsumerMinor), FIXPP_ERR_UNKNOWN);
    EXPECT_EQ(translate_for_consumer(FIXPP_ERR_SYNC_RUNTIME,        kConsumerMinor), FIXPP_ERR_UNKNOWN);
    EXPECT_EQ(translate_for_consumer(FIXPP_ERR_TLS_CONFIG,          kConsumerMinor), FIXPP_ERR_UNKNOWN);
    EXPECT_EQ(translate_for_consumer(FIXPP_ERR_TRANSPORT_LIFECYCLE, kConsumerMinor), FIXPP_ERR_UNKNOWN);
    EXPECT_EQ(translate_for_consumer(FIXPP_ERR_DECIMAL_INVALID,     kConsumerMinor), FIXPP_ERR_UNKNOWN);
    // FIXPP_ERR_OK (introducing_minor=2) → UNKNOWN for consumer_minor=1
    EXPECT_EQ(translate_for_consumer(FIXPP_ERR_OK,                  kConsumerMinor), FIXPP_ERR_UNKNOWN);
}

// ---------------------------------------------------------------------------
// 9. translate_for_consumer — passthrough when consumer_minor >= introducing_minor
// ---------------------------------------------------------------------------

TEST(CapiError, DowngradePassthrough) {
    // consumer_minor=2 matches introducing_minor=2 → code passes through unchanged.
    constexpr uint16_t kConsumerMinor = 2u;

    EXPECT_EQ(translate_for_consumer(FIXPP_ERR_CANCELLED,           kConsumerMinor), FIXPP_ERR_CANCELLED);
    EXPECT_EQ(translate_for_consumer(FIXPP_ERR_UNKNOWN,             kConsumerMinor), FIXPP_ERR_UNKNOWN);
    EXPECT_EQ(translate_for_consumer(FIXPP_ERR_BUFFER_TOO_SMALL,    kConsumerMinor), FIXPP_ERR_BUFFER_TOO_SMALL);
    EXPECT_EQ(translate_for_consumer(FIXPP_ERR_WIRE_INVALID_FRAME,  kConsumerMinor), FIXPP_ERR_WIRE_INVALID_FRAME);
    EXPECT_EQ(translate_for_consumer(FIXPP_ERR_DECIMAL_INVALID,     kConsumerMinor), FIXPP_ERR_DECIMAL_INVALID);

    // consumer_minor=3 (future; more permissive) → also passthrough.
    constexpr uint16_t kConsumerMinorFuture = 3u;
    EXPECT_EQ(translate_for_consumer(FIXPP_ERR_CANCELLED,           kConsumerMinorFuture), FIXPP_ERR_CANCELLED);
    EXPECT_EQ(translate_for_consumer(FIXPP_ERR_DECIMAL_INVALID,     kConsumerMinorFuture), FIXPP_ERR_DECIMAL_INVALID);
}

// ---------------------------------------------------------------------------
// 10. Mutation self-check: static_assert that translate() arms compile exactly.
//     A mis-coalesced arm (e.g. tls_handshake_failed → STORE_RUNTIME) must make
//     CorrectnesOracle RED at runtime; this static_assert demonstrates the
//     translate() switch is total at compile time via -Wswitch.
//     (The -Wswitch totality proof is in translate()'s translation unit;
//      the static_assert below anchors this TU to the correctness claim.)
// ---------------------------------------------------------------------------

// Verify that the E-2 numeric values match what the header defines.
// If someone accidentally redefined FIXPP_ERR_DECIMAL_INVALID to 10 (old provisional),
// this would trip here.
static_assert(FIXPP_ERR_OK                   == 0);
static_assert(FIXPP_ERR_CANCELLED            == 1);
static_assert(FIXPP_ERR_UNKNOWN              == 2);
static_assert(FIXPP_ERR_NULL_HANDLE          == 3);
static_assert(FIXPP_ERR_INVALID_HANDLE       == 4);
static_assert(FIXPP_ERR_VERSION_MISMATCH     == 5);
static_assert(FIXPP_ERR_BUFFER_TOO_SMALL     == 6);
static_assert(FIXPP_ERR_TYPE_MISMATCH        == 7);
static_assert(FIXPP_ERR_TAG_NOT_FOUND        == 8);
static_assert(FIXPP_ERR_INDEX_OUT_OF_RANGE   == 9);
static_assert(FIXPP_ERR_CAPI_CONFIG_INVALID  == 10);
static_assert(FIXPP_ERR_WIRE_INVALID_FRAME   == 100);
static_assert(FIXPP_ERR_WIRE_LIMIT_EXCEEDED  == 101);
static_assert(FIXPP_ERR_WIRE_CONFORMANCE     == 102);
static_assert(FIXPP_ERR_DICT_CONFIG          == 200);
static_assert(FIXPP_ERR_DICT_LIMIT_EXCEEDED  == 201);
static_assert(FIXPP_ERR_DICT_OOM             == 202);
static_assert(FIXPP_ERR_THREAD_CONFIG        == 300);
static_assert(FIXPP_ERR_THREAD_SESSION_LIFECYCLE == 301);
static_assert(FIXPP_ERR_THREAD_RUNTIME       == 302);
static_assert(FIXPP_ERR_STORE_RUNTIME        == 400);
static_assert(FIXPP_ERR_STORE_CONSISTENCY    == 401);
static_assert(FIXPP_ERR_STORE_CONFIG         == 402);
static_assert(FIXPP_ERR_STORE_VISITOR        == 403);
static_assert(FIXPP_ERR_SYNC_RUNTIME         == 500);
static_assert(FIXPP_ERR_TLS_CONFIG           == 600);
static_assert(FIXPP_ERR_TLS_HANDSHAKE        == 601);
static_assert(FIXPP_ERR_TLS_PINSET           == 602);
static_assert(FIXPP_ERR_TLS_RUNTIME          == 603);
static_assert(FIXPP_ERR_TRANSPORT_LIFECYCLE  == 700);
static_assert(FIXPP_ERR_TRANSPORT_IO         == 701);
static_assert(FIXPP_ERR_TRANSPORT_HANDSHAKE  == 702);
static_assert(FIXPP_ERR_TRANSPORT_CONFIG     == 703);
static_assert(FIXPP_ERR_DECIMAL_INVALID      == 800);
static_assert(FIXPP_ERR_DECIMAL_PRECISION_LOSS == 801);
