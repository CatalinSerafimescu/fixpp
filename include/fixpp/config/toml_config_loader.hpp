// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/config/toml_config_loader.hpp
// fixpp::config — load_toml_config() entry point + LoadOptions.
// 044-toml-session-config (FR-001 / DECISION-1 / data-model E-2).
//
// Consumers: #include <fixpp/config/toml_config_loader.hpp>
//            link fixpp::config_toml
//
// FR-004/SC-006: tomlplusplus MUST NOT appear in this or any public header.
// It is a PRIVATE implementation detail of the fixpp_config_toml target.
#pragma once

#include <filesystem>
#include <memory_resource>

#include <asio/any_io_executor.hpp>

#include <fixpp/config/config_bundle.hpp>

namespace fixpp::config {

// Host-supplied inputs available AT LOAD TIME (the closed at-load input set).
// Every other host-supplied value (Application, file_io_executor, per-session
// arenas, logger/tracer/meter) is injected AFTER load (data-model E-2).
struct LoadOptions {
    // ENGINE-LEVEL executor (EngineConfig::executor). Required at load time
    // because the loader builds executor-dependent objects: system_clock_source
    // (non-copyable/non-movable ctor requires executor — system_clock_source.hpp:55)
    // and the TLS TransportFactory whose SslCtxConfig::clock is REQUIRED
    // (security_profile.hpp:97). The SAME instance is set on EngineConfig::executor
    // by the host after load; the loader does NOT populate that field (DECISION-1).
    asio::any_io_executor engine_executor;

    // Cold-path load-time arena for load-time object construction:
    // XmlLoader::load(path, mr) (mr != nullptr precondition; xml_loader.hpp:51-58)
    // and make_file_cert_source(cfg, mr) (file_cert_source.hpp:56).
    // NOT a session/message hot-path arena — distinct from SessionConfig::message_arena
    // and related hot-path resources (those remain host-supplied AFTER load, step-2).
    // Defaults to the process-wide default resource.
    std::pmr::memory_resource* resource = std::pmr::get_default_resource();
};

// Read `path`, parse TOML, validate fail-closed, resolve built-in factory
// selectors, and return a deliberately-incomplete ConfigBundle (FR-001/FR-002/
// FR-012). Relative paths inside the file resolve against path.parent_path()
// (FR-016a). All failure flows through LoadResult; never throws (noexcept).
[[nodiscard]] LoadResult load_toml_config(const std::filesystem::path& path,
                                          LoadOptions opts) noexcept;

}  // namespace fixpp::config
