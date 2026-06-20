// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/config/config_bundle.hpp
// fixpp::config — ConfigBundle, EngineEstablishment, SessionDefinition (E-1..E-3).
// 044-toml-session-config (FR-002 / data-model §E).
#pragma once

#include <expected>
#include <memory>
#include <vector>

// session_config.hpp is included for by-value SessionConfig member.
#include <fixpp/session/session_config.hpp>

// Forward declarations for shared_ptr<T> members — callers do not need the
// full types unless they dereference the pointers.
namespace fixpp::core   { class Clock; }
namespace fixpp::session { class MessageStoreFactory; }
namespace fixpp::tls    { class cert_source; }
namespace fixpp::transport { class TransportFactory; }
namespace fixpp::dict   { class Dictionary; }
namespace fixpp::log    { class Logger; }

#include <fixpp/config/load_diagnostic.hpp>

namespace fixpp::config {

// One [[session]] block after [default] merge.
// File-expressible fields are SET; host-supplied fields stay default
// (executor_override, arenas, observability overrides) per data-model E-3.
struct SessionDefinition {
    fixpp::session::SessionConfig config;  // file-set fields only; host completes
};

// Engine-establishment slice (data-model E-2).
// Maps onto EngineConfig anchors; the host supplies executor / file_io_executor /
// arenas / Application after load.
struct EngineEstablishment {
    std::shared_ptr<fixpp::core::Clock>                             clock;
    std::shared_ptr<fixpp::session::MessageStoreFactory>            default_store_factory;
    std::shared_ptr<fixpp::tls::cert_source>                        default_cert_source;
    std::shared_ptr<fixpp::transport::TransportFactory>             default_transport_factory;
    std::vector<std::shared_ptr<const fixpp::dict::Dictionary>>     dictionaries;
    // 045-observability-config (logging leg, E-2): null → engine logger no-op.
    // NO tracer/meter field this step (deferred — OTLP export unimplemented).
    std::shared_ptr<fixpp::log::Logger>                             logger;
};

struct ConfigBundle {
    EngineEstablishment         engine;
    std::vector<SessionDefinition> sessions;  // >= 1
};

// Success → ConfigBundle; failure → complete vector of per-key diagnostics
// collected in one pass (FR-018). Uses std::expected directly because
// fixpp::core::expected_t<T> is single-param (hard-bound to fixpp::core::error).
using LoadResult = std::expected<ConfigBundle, std::vector<LoadDiagnostic>>;

}  // namespace fixpp::config
