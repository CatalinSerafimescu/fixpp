// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tools/otel_smoke/otel_smoke.cpp
//
// Standalone OTel smoke driver for fixpp. It reuses the already-built static
// libraries from build/linux-clang-release and emits:
//   - log records through a Logger with FileSink + OtlpLogSink
//   - one OTLP metric through OtelDualExportBuilder
//   - one span through fixpp::otel::TracerProvider (expected to be discarded)

#include <fixpp/log/file_sink.hpp>
#include <fixpp/log/logger.hpp>
#include <fixpp/log/otlp_log_sink.hpp>
#include <fixpp/otel/exporters.hpp>
#include <fixpp/otel/providers.hpp>

#include <opentelemetry/metrics/sync_instruments.h>
#include <opentelemetry/sdk/metrics/meter_provider.h>
#include <opentelemetry/trace/span.h>
#include <opentelemetry/trace/span_metadata.h>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory_resource>
#include <string>
#include <thread>

namespace {

struct Args {
    std::filesystem::path log_dir;
    std::string otlp_http_base;
};

Args parse_args(int argc, char** argv) {
    if (argc != 3) {
        throw std::runtime_error(
            "usage: otel_smoke <log-dir> <otlp-http-base-url>");
    }
    return Args{
        .log_dir = argv[1],
        .otlp_http_base = argv[2],
    };
}

std::unique_ptr<fixpp::log::Logger> make_logger(std::filesystem::path const& log_dir,
                                                std::string const& otlp_http_base) {
    fixpp::log::FileSinkConfig file_cfg;
    file_cfg.directory = log_dir;
    file_cfg.base_name = "fixpp_otel_smoke";
    file_cfg.max_file_bytes = 1024 * 1024;
    file_cfg.max_keep_count = 2;
    file_cfg.async_fsync = true;

    fixpp::log::OtlpLogSinkConfig otlp_cfg;
    otlp_cfg.endpoint = otlp_http_base + "/v1/logs";
    otlp_cfg.export_timeout = std::chrono::seconds{5};
    otlp_cfg.max_export_batch = 16;
    otlp_cfg.max_export_retries = 1;

    std::pmr::vector<std::unique_ptr<fixpp::log::Sink>> sinks{
        std::pmr::get_default_resource()};
    sinks.push_back(std::make_unique<fixpp::log::FileSink>(file_cfg));
    sinks.push_back(std::make_unique<fixpp::log::OtlpLogSink>(otlp_cfg));

    fixpp::log::LoggerConfig logger_cfg;
    logger_cfg.capacity = 1024;
    logger_cfg.drain_timeout = std::chrono::seconds{10};

    return std::make_unique<fixpp::log::Logger>(std::move(logger_cfg), std::move(sinks));
}

void emit_logs(fixpp::log::Logger& logger) {
    FIXPP_LOG0(&logger, info, fixpp::log::cat::control, "test message");
    FIXPP_LOG0(&logger, warn, fixpp::log::cat::otel, "msg {}", fixpp::log::ArgValue::from_u64(7));
    FIXPP_LOG0(&logger, error, fixpp::log::cat::control, "record {}",
               fixpp::log::ArgValue::from_u64(99));
}

void emit_metric(std::string const& otlp_http_base) {
    fixpp::otel::OtlpMetricConfig metric_cfg;
    metric_cfg.endpoint = otlp_http_base;
    metric_cfg.export_interval = std::chrono::milliseconds{60'000};
    metric_cfg.export_timeout = std::chrono::milliseconds{5'000};

    auto meter_provider = fixpp::otel::OtelDualExportBuilder{}.with_otlp(metric_cfg).build();
    auto* sdk_mp =
        dynamic_cast<opentelemetry::sdk::metrics::MeterProvider*>(meter_provider.get());
    if (sdk_mp == nullptr) {
        throw std::runtime_error("metric provider is not an SDK MeterProvider");
    }

    auto meter = meter_provider->GetMeter("fixpp.otel.smoke");
    auto counter = meter->CreateUInt64Counter("fixpp.smoke.counter",
                                              "fixpp OTel smoke counter");
    counter->Add(3);

    if (!sdk_mp->ForceFlush(std::chrono::microseconds{10'000'000})) {
        throw std::runtime_error("metric ForceFlush failed");
    }
    sdk_mp->Shutdown();
}

void emit_span(std::string const& otlp_http_base) {
    fixpp::otel::OtelConfig cfg;
    cfg.endpoint = otlp_http_base;
    cfg.resource.service_name = "fixpp-otel-smoke";
    cfg.resource.service_version = "0";
    cfg.resource.deployment_environment = "smoke";

    fixpp::otel::TracerProvider provider{cfg};
    auto tracer = provider.get_tracer("fixpp.otel.smoke");

    opentelemetry::trace::StartSpanOptions opts;
    opts.parent = opentelemetry::trace::SpanContext::GetInvalid();
    auto span = tracer->StartSpan("fixpp.smoke.span", opts);
    span->SetAttribute("smoke.run", true);
    span->SetStatus(opentelemetry::trace::StatusCode::kOk);
    span->End();

    provider.shutdown();
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const auto args = parse_args(argc, argv);
        std::filesystem::create_directories(args.log_dir);

        auto logger = make_logger(args.log_dir, args.otlp_http_base);
        emit_logs(*logger);
        auto logger_shutdown = logger->shutdown();
        if (!logger_shutdown.has_value()) {
            throw std::runtime_error("logger shutdown failed");
        }

        std::this_thread::sleep_for(std::chrono::milliseconds{250});
        emit_metric(args.otlp_http_base);
        std::this_thread::sleep_for(std::chrono::milliseconds{250});
        emit_span(args.otlp_http_base);

        std::cout << "log_file=" << (args.log_dir / "fixpp_otel_smoke.log") << '\n';
        std::cout << "otlp_http_base=" << args.otlp_http_base << '\n';
        return 0;
    } catch (std::exception const& ex) {
        std::cerr << "otel_smoke error: " << ex.what() << '\n';
        return 1;
    }
}
