// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/config/toml_config_loader.cpp
// 044-toml-session-config — Phase 3c: parse → merge → map → resolve pipeline.
//
// T007: TOML parse front-end.
// T008: noexcept-boundary wrapper (trap_throw_to_expected in loader_internal.hpp).
// T009: collect-ALL accumulator + [default]-merge.
// T017: wire map_scalars + map_structured_members + resolve_selectors.
//
// tomlplusplus is a PRIVATE dependency (FR-004/SC-006):
// it MUST NOT appear in any public header under include/fixpp/config/.
//
// TOML exception mode: exceptions are LEFT ON (the toml++ default).
// toml::parse_file may throw toml::parse_error. This file wraps the parse
// call in a try/catch so no throw escapes the noexcept boundary (validation
// rule 9 / D-3). This avoids needing TOML_EXCEPTIONS=0 (which would introduce
// a per-TU ABI-namespace divergence risk via TOML_ABI_NAMESPACE_BOOL).

#include <fixpp/config/toml_config_loader.hpp>

#include "loader_internal.hpp"
#include "mappers.hpp"

// toml++ header-only include — PRIVATE to this TU only (FR-004).
// Routed through the hardened shim (T039): NEVER include <toml++/toml.hpp>
// directly from a config TU (ODR — see toml_include.hpp).
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "toml_include.hpp"

namespace fixpp::config {

namespace {

// ---------------------------------------------------------------------------
// Source-region → SourceLoc conversion (T007)
// ---------------------------------------------------------------------------

SourceLoc loc_from_region(const toml::source_region& sr) noexcept {
    // source_region::begin is a source_position {line, column} (1-based uint32).
    // Use the begin position as the canonical diagnostic location.
    return SourceLoc{
        .line = sr.begin.line,
        .col = sr.begin.column,
    };
}

// ---------------------------------------------------------------------------
// [default]-merge helper (T009 / D-8)
// ---------------------------------------------------------------------------
//
// Deep-merges `defaults` under `session_tbl`. The session's own keys override
// keys from defaults; keys absent from defaults but present in the session are
// valid. Only top-level sub-tables are recursed into (one level); arrays and
// scalars are copied wholesale if absent from the session.
//
// The merged result is a new table (owned). `session_tbl` is the per-session
// table from [[session]]; `defaults` is the [default] table.
//
// This helper is toml::table-typed and therefore lives in this .cpp only —
// it MUST NOT be placed in loader_internal.hpp (which must stay toml-free so
// Phase 3 can include it without dragging toml++ into selector_resolver.cpp).
[[nodiscard]] toml::table merge_defaults(const toml::table& defaults,
                                         const toml::table& session_tbl) {
    // Start with a copy of the session table (session keys win).
    toml::table merged = session_tbl;

    // For each key in defaults, insert it only if absent from the session.
    for (auto&& [key, def_node] : defaults) {
        if (merged.contains(key)) {
            // Session key overrides default — skip.
            // If both sides have a sub-table, recurse one level to allow
            // partial override of nested keys (e.g. session specifies
            // [security_profile] kind= but not other keys from default).
            if (def_node.is_table() && merged[key].is_table()) {
                auto* merged_sub = merged[key].as_table();
                const auto* def_sub = def_node.as_table();
                for (auto&& [sub_key, sub_val] : *def_sub) {
                    if (!merged_sub->contains(sub_key)) {
                        merged_sub->insert(sub_key, sub_val);
                    }
                }
            }
        } else {
            // Key absent from session — inherit from default.
            merged.insert(key, def_node);
        }
    }
    return merged;
}

// ---------------------------------------------------------------------------
// T022 — per-session required-key enumeration
// ---------------------------------------------------------------------------
//
// Called after map_scalars + map_structured_members for each merged session
// table. Accumulates (never stops at first) missing_required / empty_required
// for mandatory and conditionally-mandatory session-scalar fields.
//
// Do NOT check clock/store/transport.kind/security_profile.kind/cert_source —
// those are owned by resolve_selectors and double-reporting would corrupt
// collect-ALL counts (T019 ExactCount/ExactSet).
//
// Anchors: data-model E-3 (Bucket B table), plan.md SC-003/FR-013/FR-017.
void check_required_keys(const toml::table& merged, std::string_view key_prefix,
                         detail::DiagnosticAccumulator& acc) {
    // Helper: check a string field is present and non-empty.
    auto check_string = [&](std::string_view field) {
        const std::string path = std::string{key_prefix} + "." + std::string{field};
        const auto* node = merged.get(field);
        if (!node || !node->is_string()) {
            // Absent or not a string → missing_required.
            acc.add(LoadDiagnostic{
                .key_path = path,
                .reason = reason_class::missing_required,
                .location = {},
                .message = std::string{field} + " is required",
            });
        } else if (node->value<std::string>().value_or("").empty()) {
            // Present but empty → empty_required.
            acc.add(LoadDiagnostic{
                .key_path = path,
                .reason = reason_class::empty_required,
                .location = {},
                .message = std::string{field} + " must not be empty",
            });
        }
    };

    // Unconditional required string fields.
    check_string("sender_comp_id");
    check_string("target_comp_id");
    check_string("begin_string");

    // Determine role (default = "initiator").
    std::string role = "initiator";
    if (const auto* rnode = merged.get("role"); rnode && rnode->is_string()) {
        role = rnode->value<std::string>().value_or("initiator");
    }

    // Conditional — initiator endpoint: host and port required.
    if (role == "initiator") {
        const std::string host_path = std::string{key_prefix} + ".transport.host";
        const std::string port_path = std::string{key_prefix} + ".transport.port";

        // Inspect transport sub-table.
        const toml::table* transport = nullptr;
        if (const auto* tnode = merged.get("transport"); tnode && tnode->is_table()) {
            transport = tnode->as_table();
        }

        // host: must be a non-empty string.
        bool host_ok = false;
        if (transport) {
            if (const auto* hnode = transport->get("host"); hnode && hnode->is_string()) {
                if (!hnode->value<std::string>().value_or("").empty()) {
                    host_ok = true;
                }
            }
        }
        if (!host_ok) {
            acc.add(LoadDiagnostic{
                .key_path = host_path,
                .reason = reason_class::missing_required,
                .location = {},
                .message = "transport.host is required for an initiator session",
            });
        }

        // port: must be an integer > 0.
        bool port_ok = false;
        if (transport) {
            if (const auto* pnode = transport->get("port"); pnode && pnode->is_integer()) {
                const auto v = pnode->value<int64_t>().value_or(0);
                if (v > 0) {
                    port_ok = true;
                }
            }
        }
        if (!port_ok) {
            acc.add(LoadDiagnostic{
                .key_path = port_path,
                .reason = reason_class::missing_required,
                .location = {},
                .message = "transport.port is required for an initiator session",
            });
        }
    }

    // Conditional — FIXT.1.1: default_appl_ver_id required.
    std::string begin_string_val;
    if (const auto* bnode = merged.get("begin_string"); bnode && bnode->is_string()) {
        begin_string_val = bnode->value<std::string>().value_or("");
    }
    if (begin_string_val == "FIXT.1.1") {
        const std::string appl_path = std::string{key_prefix} + ".default_appl_ver_id";
        const auto* anode = merged.get("default_appl_ver_id");
        if (!anode || !anode->is_string()) {
            acc.add(LoadDiagnostic{
                .key_path = appl_path,
                .reason = reason_class::missing_required,
                .location = {},
                .message = "default_appl_ver_id is required for a FIXT.1.1 session",
            });
        } else if (anode->value<std::string>().value_or("").empty()) {
            acc.add(LoadDiagnostic{
                .key_path = appl_path,
                .reason = reason_class::empty_required,
                .location = {},
                .message = "default_appl_ver_id must not be empty for a FIXT.1.1 session",
            });
        }
    }

    // Unconditional: security_profile.kind is the primary no-implicit-default
    // boundary (data-model E-3 / [const §XII.5]). Owned HERE (loader-boundary),
    // not the resolver — so a missing profile surfaces as missing_required at
    // the named key, never an opaque downstream invalid_or_contradictory_selector.
    {
        const std::string kind_path = std::string{key_prefix} + ".security_profile.kind";
        const toml::table* sp = nullptr;
        if (const auto* spnode = merged.get("security_profile"); spnode && spnode->is_table()) {
            sp = spnode->as_table();
        }
        const toml::node* kind_node = sp ? sp->get("kind") : nullptr;
        if (!kind_node || !kind_node->is_string()) {
            acc.add(LoadDiagnostic{
                .key_path = kind_path,
                .reason = reason_class::missing_required,
                .location = {},
                .message = "security_profile.kind is required",
            });
        } else if (kind_node->value<std::string>().value_or("").empty()) {
            acc.add(LoadDiagnostic{
                .key_path = kind_path,
                .reason = reason_class::empty_required,
                .location = {},
                .message = "security_profile.kind must not be empty",
            });
        }
    }
}

// ---------------------------------------------------------------------------
// Key-recognition pass (FR-018a / data-model E-6)
// ---------------------------------------------------------------------------
//
// Walks the TOP-LEVEL keys of a table and classifies each:
//   - in the step-2 DEFERRED set → recognized_not_yet_supported_step2
//     (a deferral reads differently from a typo)
//   - in the RECOGNIZED set      → handled by the mappers/resolvers (skip)
//   - otherwise                  → unknown_key
//
// Only top-level keys are walked; sub-table interiors (transport.host,
// cert_source.cert_file, …) are validated by the mappers/resolvers. A single
// union recognized-set is applied at both engine (root) and per-session scope
// — slightly permissive (e.g. a session-scalar name at root is tolerated) but
// it never false-flags a legitimately-placed key, which would break US1 / the
// positive cells. `reject_policy` is RECOGNIZED here (scalar_mappers already
// emits its step-2 diagnostic — re-flagging would corrupt collect-ALL counts).
void recognize_keys(const toml::table& tbl, std::string_view key_prefix,
                    detail::DiagnosticAccumulator& acc) {
    static const std::unordered_set<std::string_view> kDeferred = {
        // "logger" moved to kRecognized (045 FR-022 — logging leg now supported)
        "log_sink",
        "tracer",
        "meter",
        "otlp",
        "prometheus",
        "exporter",
        "tap",
        "tap_consumer",
        "arena",
        "message_arena",
        "session_arena",
        "framer_carry_arena",
        "dialect_overlay",
    };
    static const std::unordered_set<std::string_view> kRecognized = {
        // structural
        "default",
        "session",
        // 045-observability-config: logger is now a supported top-level key (FR-022)
        "logger",
        // selectors / sub-tables
        "clock",
        "store",
        "cert_source",
        "dictionary",
        "transport",
        "security_profile",
        "compid_authorization_policy",
        "reconnect_endpoint",
        "reconnect_policy",
        // per-session scalars (data-model E-3 Bucket A + reject_policy)
        "sender_comp_id",
        "target_comp_id",
        "begin_string",
        "role",
        "mode",
        "locks",
        "already_serialized_executor",
        "heartbeat_interval",
        "test_request_threshold",
        "sending_time_threshold",
        "reject_policy",
        "app_backpressure",
        "reset_seqnum_policy",
        "reset_seqnum_policy_field",
        "reset_on_logon",
        "reset_on_logout",
        "reset_on_disconnect",
        "refresh_on_logon",
        "logout_disconnect_timeout_ms",
        "redeliver_poss_dup",
        "allow_pos_dup",
        "sending_time_precision",
        "enable_next_expected_msg_seq_num",
        "check_comp_id",
        "validate_sequence_numbers",
        "validate_inbound_messages",
        "default_appl_ver_id",
        "username",
        "password",
    };

    for (auto&& [key, node] : tbl) {
        const std::string_view k = key.str();
        std::string path =
            key_prefix.empty() ? std::string{k} : std::string{key_prefix} + "." + std::string{k};
        if (kDeferred.contains(k)) {
            acc.add(LoadDiagnostic{
                .key_path = std::move(path),
                .reason = reason_class::recognized_not_yet_supported_step2,
                .location = loc_from_region(node.source()),
                .message = std::string{k} + " is recognized but not yet supported",
            });
        } else if (!kRecognized.contains(k)) {
            acc.add(LoadDiagnostic{
                .key_path = std::move(path),
                .reason = reason_class::unknown_key,
                .location = loc_from_region(node.source()),
                .message = "unrecognized key '" + std::string{k} + "'",
            });
        }
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// load_toml_config — entry point (T007 + T009 + T017)
// ---------------------------------------------------------------------------
//
// Phase 3c:
//   read file → parse (T007) → on parse/IO error return std::unexpected
//   → merge [default] into each [[session]] (T009)
//   → map scalars + structured members per session (T013/T014)
//   → resolve engine-scope selectors + transport factory (T015/T016)
//   → return ConfigBundle or accumulated diagnostics.
[[nodiscard]] LoadResult load_toml_config(const std::filesystem::path& path,
                                          LoadOptions opts) noexcept {
    // ── #4 (Gate B r1): null-resource guard ─────────────────────────────────
    // LoadOptions::resource defaults to std::pmr::get_default_resource() (non-null)
    // but the public struct allows LoadOptions{..., nullptr}.  XmlLoader::load and
    // make_file_cert_source both document mr!=nullptr as a caller-precondition with
    // release UB (xml_loader.hpp:51-54).  Substitute the default resource BEFORE any
    // use so the noexcept "every input → ConfigBundle or diagnostics, never UB"
    // contract holds unconditionally (FR-012 / validation rule 9).
    if (opts.resource == nullptr) {
        opts.resource = std::pmr::get_default_resource();
    }

    // ── T007: Parse front-end ────────────────────────────────────────────────
    // Wrap toml::parse_file in try/catch to prevent any throw escaping the
    // noexcept boundary (validation rule 9 / D-3).
    //
    // toml::parse_file handles both "file not found" and "malformed TOML"
    // through the exception path (with TOML_EXCEPTIONS=1, the default).
    // Both map to parse_error reason_class (the E-5 enum has no separate
    // io_error value).
    toml::table root_tbl;
    try {
        root_tbl = toml::parse_file(path.string());
    } catch (const toml::parse_error& e) {
        // parse_error carries source_region with line/col.
        LoadDiagnostic diag;
        diag.key_path = "";  // file-level, no key
        diag.reason = reason_class::parse_error;
        diag.location = loc_from_region(e.source());
        diag.message = std::string(e.description());
        return std::unexpected(std::vector<LoadDiagnostic>{std::move(diag)});
    } catch (const std::exception& e) {
        // IO or other std exception (e.g. file not found before parsing begins).
        LoadDiagnostic diag;
        diag.key_path = "";
        diag.reason = reason_class::parse_error;
        // location stays {0,0}: no source position available for IO errors
        diag.message = e.what();
        return std::unexpected(std::vector<LoadDiagnostic>{std::move(diag)});
    } catch (...) {
        LoadDiagnostic diag;
        diag.key_path = "";
        diag.reason = reason_class::parse_error;
        diag.message = "unknown exception during TOML parse";
        return std::unexpected(std::vector<LoadDiagnostic>{std::move(diag)});
    }

    // ── T009: [default]-merge + collect-ALL accumulator ─────────────────────
    detail::DiagnosticAccumulator acc;

    // Extract the optional [default] table.
    const toml::table* defaults_ptr = nullptr;
    if (auto* node = root_tbl.get("default"); node && node->is_table()) {
        defaults_ptr = node->as_table();
    }

    // Extract the [[session]] array-of-tables.
    // ≥1 [[session]] is required (D-8 / data-model E-1).
    const toml::array* sessions_arr = nullptr;
    if (auto* node = root_tbl.get("session"); node && node->is_array()) {
        sessions_arr = node->as_array();
    }

    if (!sessions_arr || sessions_arr->empty()) {
        acc.add(LoadDiagnostic{
            .key_path = "session",
            .reason = reason_class::missing_required,
            .location = {},
            .message = "at least one [[session]] table is required",
        });
    }

    // T022: engine-scope [dictionary] required (resolver returns silently when absent).
    // Checked here (against root_tbl) rather than per-session because [dictionary]
    // is a top-level TOML section, NOT merged into each [[session]] table.
    // key_path = "dictionary" (no session prefix) — consistent with clock.kind / store.kind.
    if (!root_tbl.contains("dictionary") || !root_tbl.get("dictionary")->is_table()) {
        acc.add(LoadDiagnostic{
            .key_path = "dictionary",
            .reason = reason_class::missing_required,
            .location = {},
            .message = "[dictionary] section is required",
        });
    }

    // Structural guard (sessions only) — #2 Gate B r1 fix:
    //
    // A missing/empty [[session]] array is a TRUE prerequisite: the loop below
    // dereferences sessions_arr (->size(), range-for) and would null-deref.
    // Short-circuit ONLY on the sessions condition so collect-ALL (FR-018) spans
    // engine, [default], and per-session scopes in one pass.
    //
    // A missing [dictionary] is NOT a prerequisite: resolve_engine_dictionary
    // returns silently on an absent dict (selector_resolver.cpp:288-292) and the
    // missing-dict diagnostic was already accumulated above at :431-438.  Returning
    // here for the dictionary case truncated root/default/per-session diagnostics
    // (breaking FR-018 collect-ALL).
    if (!sessions_arr || sessions_arr->empty()) {
        return std::unexpected(std::move(acc).release());
    }

    // Key recognition (FR-018a / E-6) — flag step-2-deferred (e.g. [logger],
    // dialect_overlay) + unknown keys at every authoring scope:
    //   - engine/root scope (e.g. a root-level [logger]);
    //   - the [default] table (a typo/step-2 key authored ONLY in [default]
    //     would otherwise be silently inherited by every session, unchecked);
    //   - per-session scope (walked inside the loop, over the RAW session table).
    recognize_keys(root_tbl, /*key_prefix=*/"", acc);
    if (defaults_ptr) {
        recognize_keys(*defaults_ptr, /*key_prefix=*/"default", acc);
    }

    // ── T009 / T017: Merge + map per session ────────────────────────────────
    //
    // 1. Deep-merge [default] under each [[session]].
    // 2. Call map_scalars + map_structured_members to populate SessionConfig.
    // 3. Collect the merged tables for the selector resolver (which needs the
    //    transport sub-table to build the engine-default factory, D-6).

    ConfigBundle bundle;
    bundle.sessions.reserve(sessions_arr->size());

    // Captured MERGED tables (owned values); pointers into these are passed
    // to resolve_selectors after the loop. Must outlive the resolver call.
    std::vector<toml::table> owned_merged_tables;
    std::vector<const toml::table*> merged_session_ptrs;
    owned_merged_tables.reserve(sessions_arr->size());
    merged_session_ptrs.reserve(sessions_arr->size());

    // Capture base_dir once (FR-016a / D-7).
    const std::filesystem::path base_dir = path.parent_path();

    std::size_t session_idx = 0;
    for (const auto& elem : *sessions_arr) {
        if (!elem.is_table()) {
            // A non-table element in [[session]] is a TOML schema violation.
            acc.add(LoadDiagnostic{
                .key_path = "session",
                .reason = reason_class::parse_error,
                .location = {},
                .message = "each [[session]] element must be a TOML table",
            });
            ++session_idx;
            continue;
        }
        const toml::table& session_raw = *elem.as_table();

        // Deep-merge [default] under this session (session keys override).
        toml::table merged =
            defaults_ptr ? merge_defaults(*defaults_ptr, session_raw) : session_raw;

        // Map scalar + structured fields onto a SessionConfig.
        SessionDefinition def;
        const std::string key_prefix = "session[" + std::to_string(session_idx) + "]";

        // Pass &session_raw for diagnostic SourceLoc lookup: toml++ zeroes
        // node source_regions on table copy, so the merged copy has no line/col;
        // the raw [[session]] table retains them for keys authored in-session
        // (T023 / FR-017).
        detail::map_scalars(merged, def.config, acc, key_prefix, &session_raw);
        detail::map_structured_members(merged, def.config, acc, key_prefix, &session_raw);
        check_required_keys(merged, key_prefix, acc);

        // Per-session key recognition: flag session-level step-2-deferred keys
        // (e.g. dialect_overlay) and typo'd unknown keys (FR-018a / E-6).
        // Walk the RAW session table (real source locations; no inherited-default
        // duplication) — NOT the merged copy.
        recognize_keys(session_raw, key_prefix, acc);

        bundle.sessions.push_back(std::move(def));

        // Stash the merged table so resolve_selectors can read transport.kind.
        owned_merged_tables.push_back(std::move(merged));
        merged_session_ptrs.push_back(&owned_merged_tables.back());

        ++session_idx;
    }

    // ── T015/T016: Resolve engine selectors + transport factory ─────────────
    // Note: the resolver runs even when the loader-boundary checks above added
    // diagnostics, so collect-ALL (FR-018) spans the loader boundary AND the
    // selector layer in one pass. The loader-boundary check is the PRIMARY one
    // for security_profile.kind (it emits the data-model E-3 missing_required at
    // the named key); the resolver may additionally surface a selector-level
    // diagnostic for the same broken input — redundant but not contradictory.
    detail::resolve_selectors(root_tbl, merged_session_ptrs, base_dir, opts, bundle, acc);

    // ── T013 (045-observability-config): Resolve [logger] (if present) ──────
    //
    // [logger] is optional (FR-003/SC-004): absent → engine.logger stays null.
    // Called AFTER 044 resolution so collect-ALL spans the whole file.
    //
    // Seam for Phase-4 T016 preflight: insert the side-effect-free resource
    // preflight (dir-exists, cert PEM-magic, endpoint non-empty) BETWEEN
    // resolve_engine_logger and construct_loggers_if_clean when T016 is wired.
    PendingLoggerSet pending_loggers;

    if (const auto* logger_node = root_tbl.get("logger");
        logger_node && logger_node->is_table()) {
        SourceLoc logger_loc;
        {
            const auto& src = logger_node->source().begin;
            logger_loc = SourceLoc{
                .line = static_cast<std::uint32_t>(src.line),
                .col  = static_cast<std::uint32_t>(src.column),
            };
        }
        detail::resolve_engine_logger(*logger_node->as_table(), "logger", logger_loc,
                                      base_dir, opts, pending_loggers, acc,
                                      /*is_engine=*/true, /*session_index=*/0);
    }

    // ── T012: Construct live Logger(s) only when the whole-file acc is clean ─
    // (research D-7 / FR-015): the SOLE side-effectful step.
    detail::construct_loggers_if_clean(std::move(pending_loggers), bundle, acc);

    if (!acc.empty()) {
        return std::unexpected(std::move(acc).release());
    }

    return bundle;
}

}  // namespace fixpp::config
