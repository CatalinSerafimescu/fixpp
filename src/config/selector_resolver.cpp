// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/config/selector_resolver.cpp
// 044-toml-session-config Phase-3c — object-selector resolvers.
//
// T015: Engine-scope selectors — clock, store, cert_source, dictionary.
// T016: Transport selector — build TLS or plain factory.
//
// tomlplusplus is a PRIVATE dependency (FR-004/SC-006): it MUST NOT appear
// in any public header under include/fixpp/config/.
//
// Calling convention for factory construction (D-3 / validation rule 9):
//   - Noexcept expected_t factories: check-the-expected pattern (no try/catch).
//   - Throwing ctors (system_clock_source, XmlLoader::load): wrap in
//     trap_throw_to_expected from loader_internal.hpp.
// See loader_internal.hpp comment block for the closed list of trap_throw sites.

#include <filesystem>
#include <fixpp/config/config_bundle.hpp>
#include <fixpp/config/toml_config_loader.hpp>
#include <fixpp/core/system_clock_source.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fixpp/session/file_store_factory.hpp>
#include <fixpp/session/memory_store_factory.hpp>
#include <fixpp/session/security_profile.hpp>
#include <fixpp/tls/file_cert_source.hpp>
#include <fixpp/tls/security_profile.hpp>
#include <fixpp/transport/transport_factory.hpp>
#include <memory>
#include <string>
#include <string_view>

#include "mappers.hpp"

namespace fixpp::config::detail {

// ─────────────────────────────────────────────────────────────────────────────
// resolve_engine_clock — build engine.clock from [clock] table (D-5).
//
// clock.kind must be "system" (the ONLY accepted value in step 1).
// Returns via acc; sets bundle.engine.clock on success.
// ─────────────────────────────────────────────────────────────────────────────
static void resolve_engine_clock(const toml::table& root_tbl, const LoadOptions& opts,
                                 ConfigBundle& bundle, DiagnosticAccumulator& acc) {
    const toml::table* clk_tbl = nullptr;
    if (const auto* n = root_tbl.get("clock"); n && n->is_table()) {
        clk_tbl = n->as_table();
    }

    if (!clk_tbl) {
        acc.add(LoadDiagnostic{
            .key_path = "clock.kind",
            .reason = reason_class::missing_required,
            .message = "engine-level [clock] selector is required",
        });
        return;
    }

    // Extract kind.
    std::string_view kind_tok;
    if (const auto* kind_n = clk_tbl->get("kind"); kind_n && kind_n->is_string()) {
        kind_tok = kind_n->as_string()->get();
    } else if (!clk_tbl->get("kind")) {
        acc.add(LoadDiagnostic{
            .key_path = "clock.kind",
            .reason = reason_class::missing_required,
            .message = "clock.kind is required (only accepted value: \"system\")",
        });
        return;
    } else {
        acc.add(LoadDiagnostic{
            .key_path = "clock.kind",
            .reason = reason_class::malformed_value,
            .message = "clock.kind must be a string",
        });
        return;
    }

    if (kind_tok.empty()) {
        acc.add(LoadDiagnostic{
            .key_path = "clock.kind",
            .reason = reason_class::empty_required,
            .message = "clock.kind must not be empty (only accepted value: \"system\")",
        });
        return;
    }

    if (kind_tok != "system") {
        acc.add(LoadDiagnostic{
            .key_path = "clock.kind",
            .reason = reason_class::unknown_enum,
            .message = std::string{"unknown clock.kind: \""} + std::string{kind_tok} +
                       R"(" (only accepted value: "system"))",
        });
        return;
    }

    // system_clock_source ctor may throw — use trap_throw_to_expected (D-3).
    auto result = trap_throw_to_expected(
        "engine.clock", reason_class::invalid_or_contradictory_selector,
        [&] { return std::make_shared<core::system_clock_source>(opts.engine_executor); });

    if (!result) {
        acc.add(std::move(result).error());
        return;
    }
    bundle.engine.clock = std::move(*result);
}

// ─────────────────────────────────────────────────────────────────────────────
// resolve_engine_store — build engine.default_store_factory from [store] table (D-5).
// ─────────────────────────────────────────────────────────────────────────────
static void resolve_engine_store(const toml::table& root_tbl, const std::filesystem::path& base_dir,
                                 ConfigBundle& bundle, DiagnosticAccumulator& acc) {
    const toml::table* store_tbl = nullptr;
    if (const auto* n = root_tbl.get("store"); n && n->is_table()) {
        store_tbl = n->as_table();
    }

    if (!store_tbl) {
        acc.add(LoadDiagnostic{
            .key_path = "store.kind",
            .reason = reason_class::missing_required,
            .message = "engine-level [store] selector is required",
        });
        return;
    }

    std::string_view kind_tok;
    if (const auto* kind_n = store_tbl->get("kind"); kind_n && kind_n->is_string()) {
        kind_tok = kind_n->as_string()->get();
    } else {
        acc.add(LoadDiagnostic{
            .key_path = "store.kind",
            .reason = reason_class::missing_required,
            .message = "store.kind is required",
        });
        return;
    }

    if (kind_tok.empty()) {
        acc.add(LoadDiagnostic{
            .key_path = "store.kind",
            .reason = reason_class::empty_required,
            .message = "store.kind must not be empty",
        });
        return;
    }

    if (kind_tok == "memory") {
        bundle.engine.default_store_factory = std::make_shared<session::MemoryStoreFactory>();
    } else if (kind_tok == "file") {
        // Require store.directory (a path relative to the config file's directory).
        std::string_view dir_sv;
        if (const auto* dir_n = store_tbl->get("directory"); dir_n && dir_n->is_string()) {
            dir_sv = dir_n->as_string()->get();
        } else {
            acc.add(LoadDiagnostic{
                .key_path = "store.directory",
                .reason = reason_class::missing_required,
                .message = "store.directory is required when store.kind=\"file\"",
            });
            return;
        }

        session::FileStore::Config cfg;
        cfg.directory = resolve_path(base_dir, std::filesystem::path{dir_sv});
        // Optional fields (policy, max_frame_bytes) are left at Config defaults;
        // no fixture exercises them in step-1 (parsing deferred).
        // file_io_executor and store_resource are host-supplied at make() time (D-5).

        bundle.engine.default_store_factory =
            std::make_shared<session::FileStoreFactory>(std::move(cfg));
    } else {
        acc.add(LoadDiagnostic{
            .key_path = "store.kind",
            .reason = reason_class::unknown_enum,
            .message = std::string{"unknown store.kind: \""} + std::string{kind_tok} +
                       "\" (step-1 accepted: {file, memory})",
        });
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// resolve_engine_cert_source — build engine.default_cert_source from
// [cert_source] table (D-5).
//
// step-1 accepted kind: "file"
// make_file_cert_source is noexcept expected_t — check-the-expected, no trap_throw.
// ─────────────────────────────────────────────────────────────────────────────
static void resolve_engine_cert_source(const toml::table& root_tbl,
                                       const std::filesystem::path& base_dir,
                                       const LoadOptions& opts, ConfigBundle& bundle,
                                       DiagnosticAccumulator& acc) {
    const toml::table* cs_tbl = nullptr;
    if (const auto* n = root_tbl.get("cert_source"); n && n->is_table()) {
        cs_tbl = n->as_table();
    }

    if (!cs_tbl) {
        // Not required unless a TLS session exists — but for the accumulation
        // pipeline we note the absence; the transport resolver will fail if TLS
        // transport requires a cert source and none is available.
        // (E-3 conditional-required rule: absence checked at transport-resolve time.)
        return;
    }

    std::string_view kind_tok;
    if (const auto* kind_n = cs_tbl->get("kind"); kind_n && kind_n->is_string()) {
        kind_tok = kind_n->as_string()->get();
    } else {
        acc.add(LoadDiagnostic{
            .key_path = "cert_source.kind",
            .reason = reason_class::missing_required,
            .message = "cert_source.kind is required",
        });
        return;
    }

    if (kind_tok.empty()) {
        acc.add(LoadDiagnostic{
            .key_path = "cert_source.kind",
            .reason = reason_class::empty_required,
            .message = "cert_source.kind must not be empty",
        });
        return;
    }

    if (kind_tok != "file") {
        acc.add(LoadDiagnostic{
            .key_path = "cert_source.kind",
            .reason = reason_class::unknown_enum,
            .message = std::string{"unknown cert_source.kind: \""} + std::string{kind_tok} +
                       R"(" (step-1 accepted: "file"))",
        });
        return;
    }

    // Extract cert_file, key_file, ca_file paths.
    auto get_path = [&](const char* key) -> std::string {
        if (const auto* n = cs_tbl->get(key); n && n->is_string()) {
            std::string_view sv = n->as_string()->get();
            if (sv.empty()) return {};
            return resolve_path(base_dir, std::filesystem::path{sv}).string();
        }
        return {};
    };

    std::string leaf_path = get_path("cert_file");
    std::string key_path = get_path("key_file");
    std::string ca_path = get_path("ca_file");

    tls::file_cert_source::Config cfg;
    cfg.leaf_path = std::move(leaf_path);
    cfg.private_key_path = std::move(key_path);
    cfg.ca_bundle_path = std::move(ca_path);
    // chain_path = "" — no "chain_file" TOML key in step 1 (D-5).
    // password_cb = null — step 1 supports plaintext keys only (E-6).

    auto cs_result = tls::file_cert_source::make_file_cert_source(std::move(cfg), opts.resource);
    if (!cs_result) {
        // make_file_cert_source returns expected_t — convert error to diagnostic.
        acc.add(LoadDiagnostic{
            .key_path = "cert_source",
            .reason = reason_class::invalid_or_contradictory_selector,
            .message = "cert_source file loading failed",
        });
        return;
    }
    bundle.engine.default_cert_source = std::move(*cs_result);
}

// ─────────────────────────────────────────────────────────────────────────────
// resolve_engine_dictionary — build engine.dictionaries from [dictionary] table (D-5).
//
// step-1 accepted kind: "path"
// XmlLoader::load throws — use trap_throw_to_expected.
// ─────────────────────────────────────────────────────────────────────────────
static void resolve_engine_dictionary(const toml::table& root_tbl,
                                      const std::filesystem::path& base_dir,
                                      const LoadOptions& opts, ConfigBundle& bundle,
                                      DiagnosticAccumulator& acc) {
    const toml::table* dict_tbl = nullptr;
    if (const auto* n = root_tbl.get("dictionary"); n && n->is_table()) {
        dict_tbl = n->as_table();
    }

    if (!dict_tbl) {
        // Required per E-3 required-at-load table, but diagnostic is emitted
        // at transport-resolve time if sessions need it.
        return;
    }

    std::string_view kind_tok;
    if (const auto* kind_n = dict_tbl->get("kind"); kind_n && kind_n->is_string()) {
        kind_tok = kind_n->as_string()->get();
    } else {
        acc.add(LoadDiagnostic{
            .key_path = "dictionary.kind",
            .reason = reason_class::missing_required,
            .message = "dictionary.kind is required",
        });
        return;
    }

    if (kind_tok.empty()) {
        acc.add(LoadDiagnostic{
            .key_path = "dictionary.kind",
            .reason = reason_class::empty_required,
            .message = "dictionary.kind must not be empty",
        });
        return;
    }

    if (kind_tok != "path") {
        // "version" → recognized_not_yet_supported_step2 (OQ-1 option A / E-4).
        if (kind_tok == "version") {
            acc.add(LoadDiagnostic{
                .key_path = "dictionary.kind",
                .reason = reason_class::recognized_not_yet_supported_step2,
                .message =
                    "dictionary.kind=\"version\" is recognized but deferred to step 2 (OQ-1)",
            });
        } else {
            acc.add(LoadDiagnostic{
                .key_path = "dictionary.kind",
                .reason = reason_class::unknown_enum,
                .message = std::string{"unknown dictionary.kind: \""} + std::string{kind_tok} +
                           R"(" (step-1 accepted: "path"))",
            });
        }
        return;
    }

    // Extract path param.
    std::string rel_path_str;
    if (const auto* p_n = dict_tbl->get("path"); p_n && p_n->is_string()) {
        rel_path_str = std::string{p_n->as_string()->get()};
    } else {
        acc.add(LoadDiagnostic{
            .key_path = "dictionary.path",
            .reason = reason_class::missing_required,
            .message = "dictionary.path is required when dictionary.kind=\"path\"",
        });
        return;
    }

    if (rel_path_str.empty()) {
        acc.add(LoadDiagnostic{
            .key_path = "dictionary.path",
            .reason = reason_class::empty_required,
            .message = "dictionary.path must not be empty",
        });
        return;
    }

    std::filesystem::path xml_path = resolve_path(base_dir, std::filesystem::path{rel_path_str});

    // XmlLoader::load THROWS — use trap_throw_to_expected.
    auto load_result = trap_throw_to_expected(
        "dictionary.path", reason_class::invalid_or_contradictory_selector, [&] {
            dict::XmlLoader loader;
            dict::Dictionary d = loader.load(xml_path, opts.resource);
            return std::make_shared<const dict::Dictionary>(std::move(d));
        });

    if (!load_result) {
        acc.add(std::move(load_result).error());
        return;
    }
    bundle.engine.dictionaries.push_back(std::move(*load_result));
}

// ─────────────────────────────────────────────────────────────────────────────
// parse_security_profile — extract tls::SecurityProfile from a [security_profile]
// sub-table, returning SecurityProfile::unset when the sub-table is absent.
// Emits a diagnostic for mtls_pinned (deferred) but NOT for unrecognized tokens
// (those are caught by map_structured_members; here we just return unset).
// ─────────────────────────────────────────────────────────────────────────────
static tls::SecurityProfile parse_security_profile(const toml::table& merged,
                                                   std::string_view key_prefix,
                                                   DiagnosticAccumulator& acc) {
    const toml::table* sp_tbl = nullptr;
    if (const auto* sp_n = merged.get("security_profile"); sp_n && sp_n->is_table()) {
        sp_tbl = sp_n->as_table();
    }
    if (!sp_tbl) {
        return tls::SecurityProfile::unset;
    }

    const auto* k_n = sp_tbl->get("kind");
    if (!k_n || !k_n->is_string()) {
        return tls::SecurityProfile::unset;
    }

    std::string_view sp_tok = k_n->as_string()->get();
    if (sp_tok == "mtls_ca") {
        return tls::SecurityProfile::mtls_ca;
    }
    if (sp_tok == "one_way_ca") {
        FIXPP_SUPPRESS_DEPRECATED_BEGIN
        return tls::SecurityProfile::one_way_ca;
        FIXPP_SUPPRESS_DEPRECATED_END
    }
    if (sp_tok == "mtls_pinned") {
        acc.add(LoadDiagnostic{
            .key_path = std::string{key_prefix} + ".security_profile.kind",
            .reason = reason_class::recognized_not_yet_supported_step2,
            .message = "security_profile.kind=\"mtls_pinned\" is recognized but "
                       "requires a Pinset that cannot be sourced from a file in step 1 (D-9a)",
        });
        // Return unset so callers can propagate the error via acc without crashing.
        return tls::SecurityProfile::unset;
    }
    // Unrecognized → unset; make_ssl_ctx_config will reject it.
    return tls::SecurityProfile::unset;
}

// ─────────────────────────────────────────────────────────────────────────────
// build_file_cert_source — resolve [cert_source] paths and call
// make_file_cert_source.  Returns nullptr (+ diagnostic) on failure.
// cs_tbl is the [cert_source] TOML table (engine-root or per-session).
// ─────────────────────────────────────────────────────────────────────────────
static std::shared_ptr<tls::cert_source> build_file_cert_source(
    const toml::table& cs_tbl, const std::filesystem::path& base_dir, const LoadOptions& opts,
    std::string_view key_prefix, DiagnosticAccumulator& acc) {
    auto get_path_str = [&](const char* key) -> std::string {
        if (const auto* n = cs_tbl.get(key); n && n->is_string()) {
            std::string_view sv = n->as_string()->get();
            if (sv.empty()) return {};
            return resolve_path(base_dir, std::filesystem::path{sv}).string();
        }
        return {};
    };

    tls::file_cert_source::Config cfg;
    cfg.leaf_path = get_path_str("cert_file");
    cfg.private_key_path = get_path_str("key_file");
    cfg.ca_bundle_path = get_path_str("ca_file");

    auto cs_result = tls::file_cert_source::make_file_cert_source(std::move(cfg), opts.resource);
    if (!cs_result) {
        acc.add(LoadDiagnostic{
            .key_path = std::string{key_prefix},
            .reason = reason_class::invalid_or_contradictory_selector,
            .message = "cert_source file loading failed",
        });
        return nullptr;
    }
    return std::move(*cs_result);
}

// ─────────────────────────────────────────────────────────────────────────────
// build_tls_factory — assemble a TLS transport factory from a cert_source +
// security_profile + clock.  Returns a shared_ptr; null + diagnostic on failure.
// ─────────────────────────────────────────────────────────────────────────────
static std::shared_ptr<transport::TransportFactory> build_tls_factory(
    tls::SecurityProfile profile, const std::shared_ptr<tls::cert_source>& cs,
    const std::shared_ptr<core::Clock>& clock, std::string_view key_prefix,
    DiagnosticAccumulator& acc) {
    auto ssl_cfg_result = tls::make_ssl_ctx_config(profile, cs, clock);
    if (!ssl_cfg_result) {
        acc.add(LoadDiagnostic{
            .key_path = std::string{key_prefix},
            .reason = reason_class::invalid_or_contradictory_selector,
            .message = "make_ssl_ctx_config failed (check profile/cert_source/clock combination)",
        });
        return nullptr;
    }

    transport::Transport::Config t_cfg{};
    auto factory_result =
        transport::make_asio_tls_transport_factory(t_cfg, std::move(*ssl_cfg_result));
    if (!factory_result) {
        acc.add(LoadDiagnostic{
            .key_path = std::string{key_prefix},
            .reason = reason_class::invalid_or_contradictory_selector,
            .message = "make_asio_tls_transport_factory failed (cert loading error?)",
        });
        return nullptr;
    }

    // Convert unique_ptr → shared_ptr (D-6).
    return std::shared_ptr<transport::TransportFactory>(std::move(*factory_result));
}

// ─────────────────────────────────────────────────────────────────────────────
// resolve_transport — build engine.default_transport_factory from the first
// session's [transport] sub-table and the engine-level cert_source + clock (D-6).
//
// Also mints per-session transport_factory_override for sessions whose
// cert/profile DIVERGES from the engine default (D-6a).
//
// make_ssl_ctx_config and make_asio_*_transport_factory are noexcept expected_t
// — check-the-expected, NO trap_throw (D-3 / loader_internal.hpp).
// ─────────────────────────────────────────────────────────────────────────────
static void resolve_transport(const std::vector<const toml::table*>& merged_session_tables,
                              const std::filesystem::path& base_dir, const LoadOptions& opts,
                              ConfigBundle& bundle, DiagnosticAccumulator& acc) {
    if (merged_session_tables.empty()) {
        return;  // Missing sessions caught earlier; no-op here.
    }

    // Use the FIRST session's transport table to drive the engine-default factory.
    // (Data-model E-2: engine.default_transport_factory is the anchor; session-
    // diverging factories go to SessionDefinition::config.transport_factory_override.)
    const toml::table& session_merged = *merged_session_tables[0];

    // Extract transport.kind.
    const toml::table* transport_tbl = nullptr;
    if (const auto* t_n = session_merged.get("transport"); t_n && t_n->is_table()) {
        transport_tbl = t_n->as_table();
    }

    if (!transport_tbl) {
        acc.add(LoadDiagnostic{
            .key_path = "session[0].transport.kind",
            .reason = reason_class::missing_required,
            .message = "[session.transport] is required",
        });
        return;
    }

    std::string_view transport_kind_tok;
    if (const auto* kind_n = transport_tbl->get("kind"); kind_n && kind_n->is_string()) {
        transport_kind_tok = kind_n->as_string()->get();
    } else {
        acc.add(LoadDiagnostic{
            .key_path = "session[0].transport.kind",
            .reason = reason_class::missing_required,
            .message = "transport.kind is required",
        });
        return;
    }

    if (transport_kind_tok.empty()) {
        acc.add(LoadDiagnostic{
            .key_path = "session[0].transport.kind",
            .reason = reason_class::empty_required,
            .message = "transport.kind must not be empty",
        });
        return;
    }

    if (transport_kind_tok == "tls") {
        // TLS factory requires: engine.clock + engine.default_cert_source + session
        // security_profile.

        // Extract session[0] security_profile → engine-default profile.
        tls::SecurityProfile tls_profile =
            parse_security_profile(session_merged, "session[0]", acc);

        // If parse_security_profile added a deferral diagnostic, bail.
        if (!acc.empty()) {
            return;
        }

        // Validate prerequisites (E-3: cert_source required for TLS profiles).
        if (bundle.engine.clock == nullptr) {
            acc.add(LoadDiagnostic{
                .key_path = "engine.clock",
                .reason = reason_class::missing_required,
                .message = "engine.clock is required to build a TLS transport factory (D-6)",
            });
            return;
        }
        if (bundle.engine.default_cert_source == nullptr) {
            acc.add(LoadDiagnostic{
                .key_path = "cert_source",
                .reason = reason_class::missing_required,
                .message = "engine-level cert_source is required for a TLS transport (E-3)",
            });
            return;
        }

        // Build engine-default TLS factory.
        // CRITICAL: pass bundle.engine.clock BY COPY (not move) so engine.clock
        // AND the factory's ssl_cfg_.clock both hold the shared_ptr → use_count > 1 (D-6).
        auto default_factory =
            build_tls_factory(tls_profile,
                              bundle.engine.default_cert_source,  // copy (shared_ptr)
                              bundle.engine.clock,  // copy (shared_ptr) — D-6 shared clock
                              "session[0].transport", acc);

        if (!default_factory) {
            return;  // diagnostic already added by build_tls_factory
        }
        bundle.engine.default_transport_factory = std::move(default_factory);

        // ── Per-session divergence scan (D-6a) — #1 Gate B r1 ──────────────
        // For each session:
        //   (a) Validate transport.kind (missing / empty / non-string / unknown).
        //       session[0] is already validated above; sessions 1+ were not
        //       checked before this fix, so they silently ran on the engine-default
        //       (wrong) TLS factory (data-model E-3 row 62 / FR-007 / FR-012).
        //   (b) If kind diverges from the engine default ("tls"):
        //       - "tls" with a different cert/profile → mint a TLS override (original logic).
        //       - "plaintext" → mint a plaintext factory override so the session
        //         runs on the correct transport, not the engine-default TLS one.
        //       Sessions whose kind matches ("tls", same cert, same profile) stay null.

        for (std::size_t i = 0; i < merged_session_tables.size(); ++i) {
            const toml::table& sess = *merged_session_tables[i];
            const std::string idx_str = std::to_string(i);
            const std::string s_prefix = "session[" + idx_str + "]";

            // ── (a) Validate per-session transport.kind ──────────────────────
            // session[0] is validated in the main body above — skip it here.
            if (i == 0) {
                // session[0] drove the engine-default factory; already validated.
                continue;
            }

            const toml::table* s_transport_tbl = nullptr;
            if (const auto* t_n = sess.get("transport"); t_n && t_n->is_table()) {
                s_transport_tbl = t_n->as_table();
            }
            if (!s_transport_tbl) {
                // Missing [session.transport] for a non-first session.
                acc.add(LoadDiagnostic{
                    .key_path = s_prefix + ".transport.kind",
                    .reason = reason_class::missing_required,
                    .message = "[session.transport] is required (data-model E-3 row 62)",
                });
                continue;
            }

            std::string_view s_transport_kind;
            const auto* kind_node = s_transport_tbl->get("kind");
            if (kind_node && kind_node->is_string()) {
                s_transport_kind = kind_node->as_string()->get();
            } else if (!kind_node) {
                acc.add(LoadDiagnostic{
                    .key_path = s_prefix + ".transport.kind",
                    .reason = reason_class::missing_required,
                    .message = "transport.kind is required",
                });
                continue;
            } else {
                // Present but not a string.
                acc.add(LoadDiagnostic{
                    .key_path = s_prefix + ".transport.kind",
                    .reason = reason_class::missing_required,
                    .message = "transport.kind must be a string",
                });
                continue;
            }

            if (s_transport_kind.empty()) {
                acc.add(LoadDiagnostic{
                    .key_path = s_prefix + ".transport.kind",
                    .reason = reason_class::empty_required,
                    .message = "transport.kind must not be empty",
                });
                continue;
            }

            // ── (b) Handle per-session kind relative to engine default ───────

            if (s_transport_kind == "tls") {
                // TLS-on-TLS-engine: check cert/profile divergence as before.
                tls::SecurityProfile s_profile = parse_security_profile(sess, s_prefix, acc);
                if (!acc.empty()) {
                    return;  // mtls_pinned deferral or similar
                }

                const toml::table* s_cs_tbl = nullptr;
                if (const auto* cs_n = sess.get("cert_source"); cs_n && cs_n->is_table()) {
                    s_cs_tbl = cs_n->as_table();
                }

                const bool cert_diverges = (s_cs_tbl != nullptr);
                const bool profile_diverges = (s_profile != tls_profile);

                if (!cert_diverges && !profile_diverges) {
                    // Matches engine default TLS → leave override null.
                    continue;
                }

                // Mint a FRESH TLS factory for this divergent session.
                std::shared_ptr<tls::cert_source> s_cert_source;
                if (s_cs_tbl) {
                    s_cert_source = build_file_cert_source(*s_cs_tbl, base_dir, opts,
                                                           s_prefix + ".cert_source", acc);
                    if (!s_cert_source) {
                        return;  // diagnostic already added
                    }
                } else {
                    // Profile diverges but certs are the same.
                    s_cert_source = bundle.engine.default_cert_source;
                }

                auto s_factory = build_tls_factory(s_profile, s_cert_source,
                                                   bundle.engine.clock,
                                                   s_prefix + ".transport", acc);
                if (!s_factory) {
                    return;  // diagnostic already added
                }
                bundle.sessions[i].config.transport_factory_override = std::move(s_factory);

            } else if (s_transport_kind == "plaintext") {
                // Plaintext-on-TLS-engine: this session declared a different kind
                // from the engine default.  Mint a per-session plaintext factory
                // override so it runs on the correct transport (D-6a / FR-007).
                //
                // Guard: do NOT flag a contradiction with engine.default_cert_source
                // — the cert material belongs to OTHER TLS sessions, not this one.
                transport::Transport::Config t_cfg{};
                auto factory_result = transport::make_asio_plain_transport_factory(t_cfg);
                if (!factory_result) {
                    acc.add(LoadDiagnostic{
                        .key_path = s_prefix + ".transport",
                        .reason = reason_class::invalid_or_contradictory_selector,
                        .message = "make_asio_plain_transport_factory failed",
                    });
                    return;
                }
                bundle.sessions[i].config.transport_factory_override =
                    std::shared_ptr<transport::TransportFactory>(std::move(*factory_result));

            } else {
                // Unknown kind (e.g. "websocket").
                acc.add(LoadDiagnostic{
                    .key_path = s_prefix + ".transport.kind",
                    .reason = reason_class::unknown_enum,
                    .message = std::string{"unknown transport.kind: \""} +
                               std::string{s_transport_kind} +
                               R"(" (accepted: "tls", "plaintext"))",
                });
                // continue to next session (accumulate all errors — FR-018)
            }
        }

    } else if (transport_kind_tok == "plaintext") {
        // T025 (validation rule D-6): plaintext transport + cert_source present
        // is a file-internal contradiction — certs have no role in a plaintext
        // connection and their presence indicates a likely misconfiguration.
        if (bundle.engine.default_cert_source != nullptr) {
            acc.add(LoadDiagnostic{
                .key_path = "session[0].transport",
                .reason = reason_class::invalid_or_contradictory_selector,
                .message = "transport.kind=\"plaintext\" is contradictory with a "
                           "[cert_source] section: TLS credentials are unused by a "
                           "plaintext transport; remove [cert_source] or switch to "
                           "transport.kind=\"tls\"",
            });
            return;
        }

        transport::Transport::Config t_cfg{};
        auto factory_result = transport::make_asio_plain_transport_factory(t_cfg);

        if (!factory_result) {
            acc.add(LoadDiagnostic{
                .key_path = "session[0].transport",
                .reason = reason_class::invalid_or_contradictory_selector,
                .message = "make_asio_plain_transport_factory failed",
            });
            return;
        }

        bundle.engine.default_transport_factory =
            std::shared_ptr<transport::TransportFactory>(std::move(*factory_result));

    } else {
        acc.add(LoadDiagnostic{
            .key_path = "session[0].transport.kind",
            .reason = reason_class::unknown_enum,
            .message = std::string{"unknown transport.kind: \""} + std::string{transport_kind_tok} +
                       R"(" (accepted: "tls", "plaintext"))",
        });
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// resolve_selectors — public entry point (declared in mappers.hpp).
//
// Resolves engine-scope selectors (clock, store, cert_source, dictionary) then
// the per-session transport selector to build engine.default_transport_factory.
//
// `merged_session_tables` is the vector of MERGED TOML tables (one per session,
// post [default]-merge) produced by the session loop in toml_config_loader.cpp.
// ─────────────────────────────────────────────────────────────────────────────
void resolve_selectors(const toml::table& root_tbl,
                       const std::vector<const toml::table*>& merged_session_tables,
                       const std::filesystem::path& base_dir, const LoadOptions& opts,
                       ConfigBundle& bundle, DiagnosticAccumulator& acc) {
    // Engine-scope selectors: read from root_tbl directly (D-5).
    resolve_engine_clock(root_tbl, opts, bundle, acc);
    resolve_engine_store(root_tbl, base_dir, bundle, acc);
    resolve_engine_cert_source(root_tbl, base_dir, opts, bundle, acc);
    resolve_engine_dictionary(root_tbl, base_dir, opts, bundle, acc);

    // Transport factory: synthesised from engine cert_source + clock + session profile.
    // base_dir + opts are forwarded for per-session cert_source resolution (D-6a).
    resolve_transport(merged_session_tables, base_dir, opts, bundle, acc);
}

}  // namespace fixpp::config::detail
