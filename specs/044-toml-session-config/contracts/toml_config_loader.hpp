// SPDX-License-Identifier: AGPL-3.0-or-later
//
// CONTRACT (Phase 1 shape oracle — NOT the shipped header).
// specs/044-toml-session-config/contracts/toml_config_loader.hpp
//
// fixpp::config — native TOML config-file loader (session establishment).
// PATH-B pure config translation; parser dependency isolated in the
// fixpp_config_toml target, NOT in the embeddable core (FR-004 / SC-006).
//
// This file pins the PUBLIC API shape the implementation realizes. The real
// headers land under include/fixpp/config/ (config_bundle.hpp,
// load_diagnostic.hpp, toml_config_loader.hpp). No C-ABI surface; no new
// fixpp_error_t value ([const §X.4] / research D-3).
#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <memory_resource>
#include <string>
#include <vector>

#include <asio/any_io_executor.hpp>          // engine executor (load-time input)
#include <fixpp/core/engine_config.hpp>      // EngineConfig (host completes)
#include <fixpp/session/session_config.hpp>  // SessionConfig (loader hydrates)

namespace fixpp::config {

// ── Diagnostics (E-5) ───────────────────────────────────────────────────────
enum class reason_class : std::uint8_t {
    parse_error,
    unknown_key,
    recognized_not_yet_supported_step2,
    missing_required,
    empty_required,
    malformed_value,
    out_of_range,
    unknown_enum,
    invalid_or_contradictory_selector,
};

struct SourceLoc {
    std::uint32_t line = 0;
    std::uint32_t col = 0;
};

struct LoadDiagnostic {
    std::string key_path;     // e.g. "session[0].heartbeat_interval"
    reason_class reason{};
    SourceLoc location{};
    std::string message;      // credential VALUES redacted (FR-019)
};

// ── Bundle (E-1..E-3) ───────────────────────────────────────────────────────
// One per [[session]] after [default] merge. Carries a partially-populated
// SessionConfig: file-expressible fields SET; host-supplied fields LEFT DEFAULT
// (executor_override, arenas, observability overrides) per data-model E-3.
struct SessionDefinition {
    fixpp::session::SessionConfig config;  // file-set fields only; host completes
};

// Engine-establishment slice (data-model E-2). Maps onto EngineConfig anchors;
// the host supplies executor / file_io_executor / arenas / Application after.
struct EngineEstablishment {
    std::shared_ptr<fixpp::core::Clock> clock;
    std::shared_ptr<fixpp::session::MessageStoreFactory> default_store_factory;
    std::shared_ptr<fixpp::tls::cert_source> default_cert_source;
    std::shared_ptr<fixpp::transport::TransportFactory> default_transport_factory;
    std::vector<std::shared_ptr<const fixpp::dict::Dictionary>> dictionaries;
};

struct ConfigBundle {
    EngineEstablishment engine;
    std::vector<SessionDefinition> sessions;  // >= 1
};

// ── Result (E-5 / research D-3) ─────────────────────────────────────────────
// Success → ConfigBundle. Failure → the COMPLETE vector of per-key diagnostics
// found in one pass (FR-018). fixpp::core::expected_t<T> is hard-bound to
// `error` (single type param, error.hpp:780), so the loader uses std::expected
// directly to carry a custom error payload. Still a C++-only type; never
// crosses the C ABI and mints NO new fixpp_error_t value ([const §X.4]).
using LoadResult = std::expected<ConfigBundle, std::vector<LoadDiagnostic>>;

// ── Load options ────────────────────────────────────────────────────────────
// The complete set of host-supplied inputs the loader needs AT LOAD (beyond the
// file path) to build its objects. This is the closed at-load input set — every
// other host-supplied value (Application, file_io_executor, the per-session
// message/session/framer arenas, logger/tracer/meter) is injected by the host
// AFTER load (research D-4 / data-model E-2), NOT here.
struct LoadOptions {
    // The ENGINE-LEVEL executor (EngineConfig::executor) — the one host-supplied
    // value available BEFORE load (the host builds its io_context first). The
    // loader needs it to build the executor-DEPENDENT objects at load time: the
    // live system_clock_source (its ctor REQUIRES the executor and is
    // non-copyable/non-movable, system_clock_source.hpp:55) and, transitively,
    // the TLS TransportFactory (whose SslCtxConfig::clock is REQUIRED — a null
    // clock is rejected by make_ssl_ctx_config, security_profile.hpp:97). The
    // SAME instance is later set on EngineConfig::executor by the host; the
    // loader does not populate that field (DECISION-1).
    asio::any_io_executor engine_executor;

    // The COLD-PATH load-time arena for the load-time object construction the
    // loader performs: XmlLoader::load(path, mr) (precondition mr != nullptr —
    // UB-in-release on null, xml_loader.hpp:51-58) and make_file_cert_source(cfg,
    // mr) (file_cert_source.hpp:56). This is NOT a session/message hot-path
    // arena — it is distinct from SessionConfig::message_arena /
    // default_message_resource / default_session_resource, which stay
    // host-supplied AFTER load (step-2 / FR-009). Defaults to the process-wide
    // default resource. (Codex Gate A round 2 #1; data-model E-2.)
    std::pmr::memory_resource* resource = std::pmr::get_default_resource();
};

// ── Entry point ─────────────────────────────────────────────────────────────
// Reads `path`, parses TOML, validates fail-closed, resolves selectors against
// the existing built-in factories, and returns a deliberately-incomplete bundle
// (FR-001/FR-002/FR-012). Relative paths inside the file resolve against
// path.parent_path() (FR-016a).
//
// `opts` carries the closed at-load input set (LoadOptions above): the engine
// executor (builds clock + TLS factory) and the cold-path memory resource (dict
// + cert load-time allocation). The host completes the bundle with the remaining
// host-supplied values AFTER load (research D-4 / data-model E-2).
//
// noexcept: all failure flows through LoadResult. The loader MUST wrap every
// throwing construction/parse site (toml++ parse, system_clock_source ctor,
// XmlLoader::load, the file_cert_source direct ctor) in try/catch and convert
// to a LoadDiagnostic (fail-closed, FR-012); it MUST prefer the noexcept
// expected_t-returning factories where they exist (make_file_cert_source,
// make_asio_{tls,plain}_transport_factory, make_ssl_ctx_config). See research
// D-3 noexcept-boundary note and data-model validation rule 9.
[[nodiscard]] LoadResult load_toml_config(const std::filesystem::path& path,
                                          LoadOptions opts) noexcept;

}  // namespace fixpp::config
