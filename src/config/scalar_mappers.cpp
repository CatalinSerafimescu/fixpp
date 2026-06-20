// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/config/scalar_mappers.cpp
// 044-toml-session-config Phase-3b — scalar + structured-member mappers.
//
// T013: map_scalars  — ~28 Bucket-A scalar/bool/duration/enum fields.
// T014: map_structured_members — security_profile, compid_authorization_policy,
//       reconnect_endpoint, reconnect_policy.
//
// tomlplusplus is a PRIVATE dependency (FR-004/SC-006): it MUST NOT appear
// in any public header under include/fixpp/config/.
//
// Enum token spellings are sourced from the real enum definitions (D-9 /
// data-model §D-9): session_role, threading_mode, lock_policy,
// reset_seqnum_policy, backpressure_mode, fix_time_precision,
// SecurityProfile::kind, application_version.

#include <bit>
#include <cstdint>
#include <string>
#include <string_view>

// 045-observability-config T007: syslog facility map.
// FIXPP_HAS_SYSLOG is defined by syslog_sink.hpp when <syslog.h> is available.
#include <fixpp/log/syslog_sink.hpp>

#include "mappers.hpp"

// Real enum / struct headers — match the canonical spellings.
#include <fixpp/core/fix_time.hpp>
#include <fixpp/dict/version_profile.hpp>
#include <fixpp/session/compid_authorization_policy.hpp>
#include <fixpp/session/security_profile.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/transport/endpoint.hpp>
#include <fixpp/transport/reconnect_policy.hpp>
#include <utility>

// FIXPP_SUPPRESS_DEPRECATED_BEGIN/END (portable -Wdeprecated-declarations
// push/pop) is shared across config TUs — defined in loader_internal.hpp
// (reached here via mappers.hpp). Used below to wrap the deliberate
// insecure_plain_tcp mapping arm without globally silencing the diagnostic.

namespace fixpp::config::detail {

namespace {

// ---------------------------------------------------------------------------
// SourceLoc helper — extracts line/column from a toml::node (FR-017).
// toml++ line/column numbers are 1-based for parsed-from-file nodes.
// ---------------------------------------------------------------------------

SourceLoc loc_from_node(const toml::node& n) noexcept {
    const auto& src = n.source().begin;
    return SourceLoc{
        .line = static_cast<std::uint32_t>(src.line),
        .col = static_cast<std::uint32_t>(src.column),
    };
}

// ---------------------------------------------------------------------------
// SourceLoc lookup from raw (original) table by key (FR-017).
//
// toml++ zeros the source_ field on toml::table copy (the copy ctor sets
// source_ = {}). map_scalars receives `merged` (a copy), so loc_from_node on
// nodes from `merged` always returns {0,0}. When `raw_session` is provided,
// we look up the SAME key in the original (un-copied) table where source_ is
// preserved, to recover the actual file location.
//
// Falls back to {0,0} when:
//  - raw_session == nullptr  (pure code-constructed table, no source)
//  - key absent in raw_session  (key came from [default], not this session)
// ---------------------------------------------------------------------------

SourceLoc loc_for_key(const toml::table* raw_session, std::string_view key) noexcept {
    if (!raw_session) return {};
    const auto* n = raw_session->get(key);
    if (!n) return {};
    return loc_from_node(*n);
}

// Same as above but for a key inside a sub-table of raw_session.
// `sub_key` is the sub-table key; `field_key` is the key inside it.
SourceLoc loc_for_subkey(const toml::table* raw_session, std::string_view sub_key,
                         std::string_view field_key) noexcept {
    if (!raw_session) return {};
    const auto* sub_node = raw_session->get(sub_key);
    if (!sub_node || !sub_node->is_table()) return {};
    const auto* n = sub_node->as_table()->get(field_key);
    if (!n) return {};
    return loc_from_node(*n);
}

// Duration parser (parse_duration_to_ms / ParsedDuration) and the dotted
// key-path builder (kp) are toml-free and now live once in loader_internal
// (reached via mappers.hpp → loader_internal.hpp) so this TU and
// logger_resolver.cpp share one definition and the overflow guards cannot
// drift. See loader_internal.hpp.

}  // anonymous namespace

// ---------------------------------------------------------------------------
// T013 — map_scalars
// ---------------------------------------------------------------------------

void map_scalars(const toml::table& merged, fixpp::session::SessionConfig& out,
                 DiagnosticAccumulator& acc, std::string_view key_prefix,
                 const toml::table* raw_session) {
    // ── String scalars ────────────────────────────────────────────────────────

    if (const auto* n = merged.get("sender_comp_id"); n && n->is_string()) {
        out.sender_comp_id = std::string{n->as_string()->get()};
    }

    if (const auto* n = merged.get("target_comp_id"); n && n->is_string()) {
        out.target_comp_id = std::string{n->as_string()->get()};
    }

    if (const auto* n = merged.get("begin_string"); n && n->is_string()) {
        out.begin_string = std::string{n->as_string()->get()};
    }

    // ── Credentials (FR-019: store verbatim, redact in diagnostics) ───────────

    if (const auto* n = merged.get("username"); n != nullptr) {
        if (n->is_string()) {
            out.username = std::string{n->as_string()->get()};
        } else {
            // Present but wrong type — emit malformed_value; redact via display_value.
            const std::string kp_username = kp(key_prefix, "username");
            acc.add(LoadDiagnostic{
                .key_path = kp_username,
                .reason = reason_class::malformed_value,
                .location = loc_for_key(raw_session, "username"),
                .message = std::string{"username must be a string; got: "} +
                           std::string{display_value(kp_username, "")},
            });
        }
    }

    if (const auto* n = merged.get("password"); n != nullptr) {
        if (n->is_string()) {
            out.password = std::string{n->as_string()->get()};
        } else {
            // Present but wrong type — emit malformed_value; redact via display_value.
            const std::string kp_password = kp(key_prefix, "password");
            acc.add(LoadDiagnostic{
                .key_path = kp_password,
                .reason = reason_class::malformed_value,
                .location = loc_for_key(raw_session, "password"),
                .message = std::string{"password must be a string; got: "} +
                           std::string{display_value(kp_password, "")},
            });
        }
    }

    // ── role enum ─────────────────────────────────────────────────────────────
    // Canonical spellings: "initiator", "acceptor"
    // (session_config.hpp session_role enum, lines 112-115)

    if (const auto* n = merged.get("role"); n && n->is_string()) {
        std::string_view tok = n->as_string()->get();
        if (tok == "initiator") {
            out.role = fixpp::session::session_role::initiator;
        } else if (tok == "acceptor") {
            out.role = fixpp::session::session_role::acceptor;
        } else {
            acc.add(LoadDiagnostic{
                .key_path = kp(key_prefix, "role"),
                .reason = reason_class::unknown_enum,
                .location = loc_for_key(raw_session, "role"),
                .message = std::string{"unknown role token: \""} + std::string{tok} +
                           R"(" (valid values: "initiator", "acceptor"))",
            });
        }
    }

    // ── mode enum ─────────────────────────────────────────────────────────────
    // Canonical spellings: "per_session_strand", "direct_executor"
    // (session_config.hpp threading_mode enum, lines 98-101)

    if (const auto* n = merged.get("mode"); n && n->is_string()) {
        std::string_view tok = n->as_string()->get();
        if (tok == "per_session_strand") {
            out.mode = fixpp::session::threading_mode::per_session_strand;
        } else if (tok == "direct_executor") {
            out.mode = fixpp::session::threading_mode::direct_executor;
        } else {
            acc.add(LoadDiagnostic{
                .key_path = kp(key_prefix, "mode"),
                .reason = reason_class::unknown_enum,
                .location = loc_for_key(raw_session, "mode"),
                .message = std::string{"unknown mode token: \""} + std::string{tok} +
                           R"(" (valid values: "per_session_strand", "direct_executor"))",
            });
        }
    }

    // ── locks enum ────────────────────────────────────────────────────────────
    // Canonical spellings: "mutex", "spin"
    // (session_config.hpp lock_policy enum, lines 103-106)

    if (const auto* n = merged.get("locks"); n && n->is_string()) {
        std::string_view tok = n->as_string()->get();
        if (tok == "mutex") {
            out.locks = fixpp::session::lock_policy::mutex;
        } else if (tok == "spin") {
            out.locks = fixpp::session::lock_policy::spin;
        } else {
            acc.add(LoadDiagnostic{
                .key_path = kp(key_prefix, "locks"),
                .reason = reason_class::unknown_enum,
                .location = loc_for_key(raw_session, "locks"),
                .message = std::string{"unknown locks token: \""} + std::string{tok} +
                           R"(" (valid values: "mutex", "spin"))",
            });
        }
    }

    // ── already_serialized_executor (bool) ────────────────────────────────────

    if (const auto* n = merged.get("already_serialized_executor"); n && n->is_boolean()) {
        out.already_serialized_executor = n->as_boolean()->get();
    }

    // ── Threading cross-checks (T024; validation rules 7/7a; research D-6b) ──
    //
    // These checks fire AFTER all three threading fields are mapped so that
    // the order of keys in the file doesn't affect the result.
    //
    // Rule 7:  mode=direct_executor + locks=spin is contradictory: direct_executor
    //          implies the caller owns serialisation; spin is meaningless and
    //          mirrors the runtime reject in Session::open() (session.cpp:904).
    //
    // Rule 7a: mode=direct_executor WITHOUT already_serialized_executor=true is
    //          an invalid configuration (fail-closed per FR-011; only one arm fires
    //          — the spin contradiction takes priority so we avoid a double diagnostic
    //          on a fixture that violates both).
    //
    if (out.mode == fixpp::session::threading_mode::direct_executor) {
        if (out.locks == fixpp::session::lock_policy::spin) {
            // Rule 7 — spin contradicts direct_executor.
            acc.add(LoadDiagnostic{
                .key_path = kp(key_prefix, "mode"),
                .reason = reason_class::invalid_or_contradictory_selector,
                .location = loc_for_key(raw_session, "mode"),
                .message = "mode=\"direct_executor\" combined with locks=\"spin\" is "
                           "contradictory: direct_executor bypasses the session strand "
                           "(serialisation is the caller's responsibility), making spin "
                           "locks meaningless; use mode=\"per_session_strand\" or "
                           "remove the locks field",
            });
        } else if (!out.already_serialized_executor) {
            // Rule 7a — FR-011: direct_executor without attestation.
            acc.add(LoadDiagnostic{
                .key_path = kp(key_prefix, "mode"),
                .reason = reason_class::invalid_or_contradictory_selector,
                .location = loc_for_key(raw_session, "mode"),
                .message = "mode=\"direct_executor\" requires "
                           "already_serialized_executor=true; omitting the attestation "
                           "is a safety violation (FR-011)",
            });
        }
    }

    // ── Duration fields ───────────────────────────────────────────────────────
    // All three require an explicit unit suffix (D-3 / data-model validation).

    // heartbeat_interval → optional<std::chrono::seconds>
    if (const auto* n = merged.get("heartbeat_interval"); n && n->is_string()) {
        std::string_view tok = n->as_string()->get();
        auto d = parse_duration_to_ms(tok, kp(key_prefix, "heartbeat_interval"), acc,
                                      loc_for_key(raw_session, "heartbeat_interval"));
        if (d.ok) {
            out.heartbeat_interval = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::milliseconds{d.value_ms});
        }
    }

    // test_request_threshold → optional<std::chrono::milliseconds>
    if (const auto* n = merged.get("test_request_threshold"); n && n->is_string()) {
        std::string_view tok = n->as_string()->get();
        auto d = parse_duration_to_ms(tok, kp(key_prefix, "test_request_threshold"), acc,
                                      loc_for_key(raw_session, "test_request_threshold"));
        if (d.ok) {
            out.test_request_threshold = std::chrono::milliseconds{d.value_ms};
        }
    }

    // sending_time_threshold → optional<std::chrono::milliseconds>
    if (const auto* n = merged.get("sending_time_threshold"); n && n->is_string()) {
        std::string_view tok = n->as_string()->get();
        auto d = parse_duration_to_ms(tok, kp(key_prefix, "sending_time_threshold"), acc,
                                      loc_for_key(raw_session, "sending_time_threshold"));
        if (d.ok) {
            out.sending_time_threshold = std::chrono::milliseconds{d.value_ms};
        }
    }

    // ── logout_disconnect_timeout_ms (bare uint32 integer in TOML) ───────────
    // NOT a duration string — a bare integer millisecond count (see fixture).

    if (const auto* n = merged.get("logout_disconnect_timeout_ms"); n && n->is_integer()) {
        auto v = n->as_integer()->get();
        if (v >= 0 && std::cmp_less_equal(v, UINT32_MAX)) {
            out.logout_disconnect_timeout_ms = static_cast<std::uint32_t>(v);
        } else {
            acc.add(LoadDiagnostic{
                .key_path = kp(key_prefix, "logout_disconnect_timeout_ms"),
                .reason = reason_class::out_of_range,
                .location = loc_for_key(raw_session, "logout_disconnect_timeout_ms"),
                .message = "logout_disconnect_timeout_ms out of uint32 range",
            });
        }
    }

    // ── Bool knobs ────────────────────────────────────────────────────────────

    if (const auto* n = merged.get("reset_on_logon"); n && n->is_boolean()) {
        out.reset_on_logon = n->as_boolean()->get();
    }
    if (const auto* n = merged.get("reset_on_logout"); n && n->is_boolean()) {
        out.reset_on_logout = n->as_boolean()->get();
    }
    if (const auto* n = merged.get("reset_on_disconnect"); n && n->is_boolean()) {
        out.reset_on_disconnect = n->as_boolean()->get();
    }
    if (const auto* n = merged.get("refresh_on_logon"); n && n->is_boolean()) {
        out.refresh_on_logon = n->as_boolean()->get();
    }
    if (const auto* n = merged.get("redeliver_poss_dup"); n && n->is_boolean()) {
        out.redeliver_poss_dup = n->as_boolean()->get();
    }
    if (const auto* n = merged.get("allow_pos_dup"); n && n->is_boolean()) {
        out.allow_pos_dup = n->as_boolean()->get();
    }
    if (const auto* n = merged.get("enable_next_expected_msg_seq_num"); n && n->is_boolean()) {
        out.enable_next_expected_msg_seq_num = n->as_boolean()->get();
    }
    if (const auto* n = merged.get("check_comp_id"); n && n->is_boolean()) {
        out.check_comp_id = n->as_boolean()->get();
    }
    if (const auto* n = merged.get("validate_sequence_numbers"); n && n->is_boolean()) {
        out.validate_sequence_numbers = n->as_boolean()->get();
    }
    if (const auto* n = merged.get("validate_inbound_messages"); n && n->is_boolean()) {
        out.validate_inbound_messages = n->as_boolean()->get();
    }

    // ── reset_seqnum_policy enum ──────────────────────────────────────────────
    // TOML key: "reset_seqnum_policy"; struct field: reset_seqnum_policy_field
    // Canonical spellings: "bilateral_strict", "bilateral_lenient", "unilateral"
    // (session_config.hpp reset_seqnum_policy enum, lines 92-96)

    if (const auto* n = merged.get("reset_seqnum_policy"); n && n->is_string()) {
        std::string_view tok = n->as_string()->get();
        if (tok == "bilateral_strict") {
            out.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_strict;
        } else if (tok == "bilateral_lenient") {
            out.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
        } else if (tok == "unilateral") {
            out.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::unilateral;
        } else {
            acc.add(LoadDiagnostic{
                .key_path = kp(key_prefix, "reset_seqnum_policy"),
                .reason = reason_class::unknown_enum,
                .location = loc_for_key(raw_session, "reset_seqnum_policy"),
                .message = std::string{"unknown reset_seqnum_policy token: \""} + std::string{tok} +
                           "\" (valid values: \"bilateral_strict\", \"bilateral_lenient\", "
                           "\"unilateral\")",
            });
        }
    }

    // ── sending_time_precision enum ───────────────────────────────────────────
    // Canonical spellings: "seconds", "millis", "micros", "nanos"
    // (fix_time.hpp fix_time_precision enum, lines 43-48)

    if (const auto* n = merged.get("sending_time_precision"); n && n->is_string()) {
        std::string_view tok = n->as_string()->get();
        if (tok == "seconds") {
            out.sending_time_precision = fixpp::core::fix_time_precision::seconds;
        } else if (tok == "millis") {
            out.sending_time_precision = fixpp::core::fix_time_precision::millis;
        } else if (tok == "micros") {
            out.sending_time_precision = fixpp::core::fix_time_precision::micros;
        } else if (tok == "nanos") {
            out.sending_time_precision = fixpp::core::fix_time_precision::nanos;
        } else {
            acc.add(LoadDiagnostic{
                .key_path = kp(key_prefix, "sending_time_precision"),
                .reason = reason_class::unknown_enum,
                .location = loc_for_key(raw_session, "sending_time_precision"),
                .message = std::string{"unknown sending_time_precision token: \""} +
                           std::string{tok} +
                           R"(" (valid values: "seconds", "millis", "micros", "nanos"))",
            });
        }
    }

    // ── app_backpressure enum ─────────────────────────────────────────────────
    // Canonical spellings: "block", "disconnect_and_recover"
    // (session_config.hpp SessionConfig::backpressure_mode nested enum, lines 154-157)

    if (const auto* n = merged.get("app_backpressure"); n && n->is_string()) {
        std::string_view tok = n->as_string()->get();
        if (tok == "block") {
            out.app_backpressure = fixpp::session::SessionConfig::backpressure_mode::block;
        } else if (tok == "disconnect_and_recover") {
            out.app_backpressure =
                fixpp::session::SessionConfig::backpressure_mode::disconnect_and_recover;
        } else {
            acc.add(LoadDiagnostic{
                .key_path = kp(key_prefix, "app_backpressure"),
                .reason = reason_class::unknown_enum,
                .location = loc_for_key(raw_session, "app_backpressure"),
                .message = std::string{"unknown app_backpressure token: \""} + std::string{tok} +
                           R"(" (valid values: "block", "disconnect_and_recover"))",
            });
        }
    }

    // ── reject_policy ─────────────────────────────────────────────────────────
    // RejectPolicy is only forward-declared in session_config.hpp ("owned by 005")
    // — no enumerators are available. The field cannot be mapped by token in
    // Phase 3b. If the key is present, emit a recognized_not_yet_supported_step2
    // diagnostic so the caller is aware; leave the field at its struct default.
    if (const auto* n = merged.get("reject_policy"); n != nullptr) {
        acc.add(LoadDiagnostic{
            .key_path = kp(key_prefix, "reject_policy"),
            .reason = reason_class::recognized_not_yet_supported_step2,
            .message = "reject_policy mapping deferred: enum definition is in feature 005 "
                       "(forward-declared only); mapping will be added when 005 ships",
        });
    }

    // ── default_appl_ver_id (optional, only for FIXT) ─────────────────────────
    // Canonical spellings: wire ApplVerID tag values — "2".."9"
    // (version_profile.hpp application_version enum, wire mapping table)
    // Note: D-9 says canonical spellings come from the enum defs + wire table.
    // The TOML config uses the same wire-value strings as the FIX protocol.

    if (const auto* n = merged.get("default_appl_ver_id"); n && n->is_string()) {
        std::string_view tok = n->as_string()->get();
        using av = fixpp::dict::application_version;
        std::optional<av> ver;
        if (tok == "2") {
            ver = av::v40;
        } else if (tok == "3") {
            ver = av::v41;
        } else if (tok == "4") {
            ver = av::v42;
        } else if (tok == "5") {
            ver = av::v43;
        } else if (tok == "6") {
            ver = av::v44;
        } else if (tok == "7") {
            ver = av::v50;
        } else if (tok == "8") {
            ver = av::v50sp1;
        } else if (tok == "9") {
            ver = av::v50sp2;
        } else {
            acc.add(LoadDiagnostic{
                .key_path = kp(key_prefix, "default_appl_ver_id"),
                .reason = reason_class::unknown_enum,
                .location = loc_for_key(raw_session, "default_appl_ver_id"),
                .message = std::string{"unknown default_appl_ver_id token: \""} + std::string{tok} +
                           R"(" (expected ApplVerID wire values: "2".."9"))",
            });
        }
        if (ver.has_value()) {
            out.default_appl_ver_id = *ver;
        }
    }
}

// ---------------------------------------------------------------------------
// T014 — map_structured_members
// ---------------------------------------------------------------------------

void map_structured_members(const toml::table& merged, fixpp::session::SessionConfig& out,
                            DiagnosticAccumulator& acc, std::string_view key_prefix,
                            const toml::table* raw_session) {
    // ── [session.security_profile] ────────────────────────────────────────────
    // Sub-table; contains "kind" string token.
    // Targets: out.security_profile.k  (type: fixpp::session::SecurityProfile::kind)
    // Canonical tokens: "mtls_ca", "mtls_pinned", "one_way_ca",
    //                   "insecure_plain_tcp"  [[deprecated]]
    // (session/security_profile.hpp SecurityProfile::kind nested enum)
    // NOTE: "unset" is an internal sentinel NOT accepted from config files
    //       (data-model E-3: accepted profiles = {mtls_ca, one_way_ca,
    //       insecure_plain_tcp}). It falls through to unknown_enum.

    if (const auto* sp_node = merged.get("security_profile"); sp_node && sp_node->is_table()) {
        const toml::table& sp = *sp_node->as_table();

        if (const auto* kind_n = sp.get("kind"); kind_n && kind_n->is_string()) {
            std::string_view tok = kind_n->as_string()->get();
            using K = fixpp::session::SecurityProfile::kind;

            if (tok == "mtls_ca") {
                out.security_profile.k = K::mtls_ca;
            } else if (tok == "mtls_pinned") {
                out.security_profile.k = K::mtls_pinned;
            } else if (tok == "one_way_ca") {
                out.security_profile.k = K::one_way_ca;
            } else if (tok == "insecure_plain_tcp") {
                FIXPP_SUPPRESS_DEPRECATED_BEGIN
                out.security_profile.k = K::insecure_plain_tcp;
                FIXPP_SUPPRESS_DEPRECATED_END
            } else {
                acc.add(LoadDiagnostic{
                    .key_path = kp(key_prefix, "security_profile.kind"),
                    .reason = reason_class::unknown_enum,
                    .location = loc_for_subkey(raw_session, "security_profile", "kind"),
                    .message = std::string{"unknown security_profile.kind token: \""} +
                               std::string{tok} +
                               "\" (valid values: \"mtls_ca\", \"mtls_pinned\","
                               " \"one_way_ca\", \"insecure_plain_tcp\")",
                });
            }
        }
    }

    // ── [session.compid_authorization_policy] ─────────────────────────────────
    // Sub-table; keys are principal CompIDs, values are arrays of allowed
    // target CompIDs.
    // Targets: out.compid_authorization_policy.add_binding(principal, compid)

    if (const auto* cap_node = merged.get("compid_authorization_policy");
        cap_node && cap_node->is_table()) {
        const toml::table& cap = *cap_node->as_table();

        for (auto&& [principal, allowed_node] : cap) {
            if (!allowed_node.is_array()) {
                acc.add(LoadDiagnostic{
                    .key_path = kp(key_prefix, std::string{"compid_authorization_policy."} +
                                                   std::string{principal}),
                    .reason = reason_class::malformed_value,
                    .location = loc_from_node(allowed_node),
                    .message =
                        "compid_authorization_policy value must be an array of CompID strings",
                });
                continue;
            }
            const toml::array& compids = *allowed_node.as_array();
            for (const auto& cid_node : compids) {
                if (!cid_node.is_string()) {
                    acc.add(LoadDiagnostic{
                        .key_path = kp(key_prefix, std::string{"compid_authorization_policy."} +
                                                       std::string{principal}),
                        .reason = reason_class::malformed_value,
                        .location = loc_from_node(cid_node),
                        .message = "each CompID in compid_authorization_policy must be a string",
                    });
                    continue;
                }
                out.compid_authorization_policy.add_binding(principal, cid_node.as_string()->get());
            }
        }
    }

    // ── [session.transport] → out.reconnect_endpoint ─────────────────────────
    // Sub-table (same table that also carries "kind" for Phase 3c selector).
    // Phase 3b maps only host/port (D-6a: reconnect_endpoint flows through
    // SessionConfig::reconnect_endpoint, NOT the factory).

    if (const auto* t_node = merged.get("transport"); t_node && t_node->is_table()) {
        const toml::table& t = *t_node->as_table();

        if (const auto* host_n = t.get("host"); host_n && host_n->is_string()) {
            out.reconnect_endpoint.host = std::string{host_n->as_string()->get()};
        }
        if (const auto* port_n = t.get("port"); port_n && port_n->is_integer()) {
            auto v = port_n->as_integer()->get();
            if (v >= 0 && v <= 65535) {
                out.reconnect_endpoint.port = static_cast<std::uint16_t>(v);
            } else {
                acc.add(LoadDiagnostic{
                    .key_path = kp(key_prefix, "transport.port"),
                    .reason = reason_class::out_of_range,
                    .location = loc_from_node(*port_n),
                    .message = "transport.port must be in [0, 65535]",
                });
            }
        }
    }

    // ── [session.reconnect_policy] ────────────────────────────────────────────
    // Sub-table; optional (absent → leave out.reconnect_policy = nullopt).
    // Fields: schedule (array of duration strings), jitter (float),
    //         max_attempts (integer), session_id_seed (integer).

    if (const auto* rp_node = merged.get("reconnect_policy"); rp_node && rp_node->is_table()) {
        const toml::table& rp = *rp_node->as_table();

        fixpp::transport::ReconnectPolicy policy;

        // schedule — array of duration strings → pmr::vector<chrono::milliseconds>
        if (const auto* sched_n = rp.get("schedule"); sched_n && sched_n->is_array()) {
            const toml::array& sched = *sched_n->as_array();
            policy.schedule.clear();
            policy.schedule.reserve(sched.size());
            for (const auto& entry : sched) {
                if (!entry.is_string()) {
                    acc.add(LoadDiagnostic{
                        .key_path = kp(key_prefix, "reconnect_policy.schedule"),
                        .reason = reason_class::malformed_value,
                        .location = loc_from_node(entry),
                        .message = "each reconnect_policy.schedule entry must be a duration string",
                    });
                    continue;
                }
                auto d = parse_duration_to_ms(entry.as_string()->get(),
                                              kp(key_prefix, "reconnect_policy.schedule"), acc,
                                              loc_from_node(entry));
                if (d.ok) {
                    policy.schedule.emplace_back(d.value_ms);
                }
            }
        }

        if (const auto* j_n = rp.get("jitter"); j_n && j_n->is_floating_point()) {
            policy.jitter = j_n->as_floating_point()->get();
        }

        if (const auto* ma_n = rp.get("max_attempts"); ma_n && ma_n->is_integer()) {
            auto v = ma_n->as_integer()->get();
            if (v >= 0) {
                policy.max_attempts = static_cast<std::uint32_t>(v);
            }
        }

        if (const auto* seed_n = rp.get("session_id_seed"); seed_n && seed_n->is_integer()) {
            policy.session_id_seed = static_cast<std::uint64_t>(seed_n->as_integer()->get());
        }

        out.reconnect_policy = std::move(policy);
    }
}

// ---------------------------------------------------------------------------
// T007 (045-observability-config) — map_syslog_facility
// ---------------------------------------------------------------------------
//
// Maps a POSIX syslog facility name to its LOG_* integer value.
// Two error classes (data-model E-4):
//   - name not in the closed set → unknown_enum + legal-set message
//   - name in the set but LOG_* undefined on this build →
//       invalid_or_contradictory_selector
// On a non-POSIX build (no FIXPP_HAS_SYSLOG), the closed set still exists but
// every entry is "in-set but build-undefined" → invalid_or_contradictory_selector.
// (Callers guard the kind="syslog" path with #ifdef FIXPP_HAS_SYSLOG separately.)

bool map_syslog_facility(std::string_view name, int& out_facility,
                         const std::string& key_path, SourceLoc loc,
                         DiagnosticAccumulator& acc) {
    // Closed set of accepted facility names (canonical lowercase POSIX).
    // Each entry: { name, LOG_* macro value (or -1 if undefined on this build) }
    // Listed in data-model E-4 / research D-3 order.
    struct Entry {
        std::string_view name;
        int              value;  // -1 = in-set but LOG_* not defined on this build
    };
    static const Entry kFacilities[] = {
#ifdef FIXPP_HAS_SYSLOG
#  ifdef LOG_KERN
        {"kern",     LOG_KERN},
#  else
        {"kern",     -1},
#  endif
#  ifdef LOG_USER
        {"user",     LOG_USER},
#  else
        {"user",     -1},
#  endif
#  ifdef LOG_MAIL
        {"mail",     LOG_MAIL},
#  else
        {"mail",     -1},
#  endif
#  ifdef LOG_DAEMON
        {"daemon",   LOG_DAEMON},
#  else
        {"daemon",   -1},
#  endif
#  ifdef LOG_AUTH
        {"auth",     LOG_AUTH},
#  else
        {"auth",     -1},
#  endif
#  ifdef LOG_SYSLOG
        {"syslog",   LOG_SYSLOG},
#  else
        {"syslog",   -1},
#  endif
#  ifdef LOG_LPR
        {"lpr",      LOG_LPR},
#  else
        {"lpr",      -1},
#  endif
#  ifdef LOG_NEWS
        {"news",     LOG_NEWS},
#  else
        {"news",     -1},
#  endif
#  ifdef LOG_UUCP
        {"uucp",     LOG_UUCP},
#  else
        {"uucp",     -1},
#  endif
#  ifdef LOG_CRON
        {"cron",     LOG_CRON},
#  else
        {"cron",     -1},
#  endif
#  ifdef LOG_AUTHPRIV
        {"authpriv", LOG_AUTHPRIV},
#  else
        {"authpriv", -1},
#  endif
#  ifdef LOG_FTP
        {"ftp",      LOG_FTP},
#  else
        {"ftp",      -1},
#  endif
#  ifdef LOG_LOCAL0
        {"local0",   LOG_LOCAL0},
#  else
        {"local0",   -1},
#  endif
#  ifdef LOG_LOCAL1
        {"local1",   LOG_LOCAL1},
#  else
        {"local1",   -1},
#  endif
#  ifdef LOG_LOCAL2
        {"local2",   LOG_LOCAL2},
#  else
        {"local2",   -1},
#  endif
#  ifdef LOG_LOCAL3
        {"local3",   LOG_LOCAL3},
#  else
        {"local3",   -1},
#  endif
#  ifdef LOG_LOCAL4
        {"local4",   LOG_LOCAL4},
#  else
        {"local4",   -1},
#  endif
#  ifdef LOG_LOCAL5
        {"local5",   LOG_LOCAL5},
#  else
        {"local5",   -1},
#  endif
#  ifdef LOG_LOCAL6
        {"local6",   LOG_LOCAL6},
#  else
        {"local6",   -1},
#  endif
#  ifdef LOG_LOCAL7
        {"local7",   LOG_LOCAL7},
#  else
        {"local7",   -1},
#  endif
#else
        // Non-POSIX build: closed set still defined, every entry is "unavailable"
        {"kern", -1}, {"user", -1}, {"mail", -1}, {"daemon", -1},
        {"auth", -1}, {"syslog", -1}, {"lpr", -1}, {"news", -1},
        {"uucp", -1}, {"cron", -1}, {"authpriv", -1}, {"ftp", -1},
        {"local0", -1}, {"local1", -1}, {"local2", -1}, {"local3", -1},
        {"local4", -1}, {"local5", -1}, {"local6", -1}, {"local7", -1},
#endif
    };
    static constexpr std::string_view kLegalSet =
        "kern, user, mail, daemon, auth, syslog, lpr, news, uucp, cron, "
        "authpriv, ftp, local0, local1, local2, local3, local4, local5, local6, local7";

    for (const auto& e : kFacilities) {
        if (e.name == name) {
            if (e.value == -1) {
                acc.add(LoadDiagnostic{
                    .key_path = key_path,
                    .reason = reason_class::invalid_or_contradictory_selector,
                    .location = loc,
                    .message = "syslog facility \"" + std::string{name} +
                               "\" is not available on this build",
                });
                return false;
            }
            out_facility = e.value;
            return true;
        }
    }

    // Name not in the closed set.
    acc.add(LoadDiagnostic{
        .key_path = key_path,
        .reason = reason_class::unknown_enum,
        .location = loc,
        .message = "unknown syslog facility \"" + std::string{name} +
                   "\" (valid values: " + std::string{kLegalSet} + ")",
    });
    return false;
}

// ---------------------------------------------------------------------------
// T007 (045-observability-config) — validate_pow2_capacity
// ---------------------------------------------------------------------------

bool validate_pow2_capacity(std::uint32_t value, const std::string& key_path,
                            SourceLoc loc, DiagnosticAccumulator& acc) {
    if (value == 0 || !std::has_single_bit(value)) {
        acc.add(LoadDiagnostic{
            .key_path = key_path,
            .reason = reason_class::out_of_range,
            .location = loc,
            .message = "capacity must be a non-zero power of 2 (e.g. 65536)",
        });
        return false;
    }
    return true;
}

}  // namespace fixpp::config::detail
