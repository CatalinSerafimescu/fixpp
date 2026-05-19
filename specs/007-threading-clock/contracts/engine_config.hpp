// SPDX-License-Identifier: AGPL-3.0-or-later
// SHAPE ORACLE — NOT the build header. [2d §4.4] EngineConfig.
#pragma once
#include "clock.hpp"
#include "trace_context.hpp"
#include <asio/any_io_executor.hpp>
#include <memory>
#include <memory_resource>
#include <vector>
#include <atomic>

namespace fixpp::dict     { class Dictionary; }
namespace fixpp::core     { class Logger; }
namespace fixpp::otel     { class TracerProvider; class MeterProvider; }
namespace fixpp::session  { class MessageStoreFactory; }
namespace fixpp::tls      { class cert_source; }
namespace fixpp::transport{ class TransportFactory; }
namespace fixpp::service  { class ControlPlaneFactory; }

namespace fixpp::core {

// Value-typed. Engine::open(EngineConfig) consumes it once. clock == nullptr
// is rejected with error::clock_not_set at Engine::open REGARDLESS of session
// clock_override (root cause #2). dictionaries → dict::version_registry built
// at Engine::open via the merged-003 [2c §4.9] API (D-13). engine_trace_context
// is held by the engine as a std::atomic<trace_context> snapshot (seqlock
// fallback if not lock-free; D-1).
struct EngineConfig {
    asio::any_io_executor    executor;
    std::shared_ptr<Clock>   clock;                 // rejected if null @ Engine::open

    std::vector<std::shared_ptr<const fixpp::dict::Dictionary>> dictionaries;

    std::pmr::memory_resource* default_message_resource = std::pmr::get_default_resource();
    std::pmr::memory_resource* default_session_resource = std::pmr::get_default_resource();

    std::shared_ptr<fixpp::core::Logger>          logger;   // null → no-op
    std::shared_ptr<fixpp::otel::TracerProvider>  tracer;   // null → no-op
    std::shared_ptr<fixpp::otel::MeterProvider>   meter;    // null → no-op

    std::shared_ptr<fixpp::session::MessageStoreFactory> default_store_factory;
    std::shared_ptr<fixpp::tls::cert_source>             default_cert_source;
    std::shared_ptr<fixpp::transport::TransportFactory>  default_transport_factory;

    fixpp::otel::trace_context engine_trace_context{};   // engine holds an atomic snapshot

    std::unique_ptr<fixpp::service::ControlPlaneFactory> control_plane_factory;  // null permitted
};

}  // namespace fixpp::core
