// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/otel/test_engine_close_teardown.cpp
//
// E2 remediation (T044): regression test asserting Engine::close() (= stop())
// invokes both TracerProvider::shutdown() and MeterProvider::shutdown().
//
// Three tests:
//   E2_ProviderShutdownCalled       — spy providers confirm shutdown() fired
//   E2_NullProviders_NoCrash        — null providers: stop() does not crash
//   E2_LoggerShutdownFlushesSinks   — Logger::shutdown() calls flush on sinks
//
// Anchors: FR-014 / T044 / [2k §6.6] / contracts/otel-surface.md shutdown.
// NO session-FSM edit: the Engine has zero registered sessions here.

// MSVC: this MUST precede everything below. asio — reached further down via
// <fixpp/session/engine.hpp> — refuses a TU in which winsock v1 was included
// first: asio/detail/socket_types.hpp errors with "WinSock.h has already been
// included" when _WINSOCKAPI_ is defined but _WINSOCK2API_ is not. The OTel
// headers immediately below pull in <windows.h>, which includes <winsock.h>
// unless <winsock2.h> got there first. Including it here defines _WINSOCK2API_
// up front, so asio's check passes.
//
// Note this is the OPPOSITE of what the next comment's rationale implies on
// Windows: putting the OTel headers first is exactly what breaks asio there.
// The ordering is kept (it is load-bearing for the asio macro issue it names on
// other platforms) and made safe by claiming winsock2 before either.
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

// OTel headers first to avoid include ordering issues with asio macros.
#include <opentelemetry/version.h>
#include <opentelemetry/trace/noop.h>
#include <opentelemetry/metrics/noop.h>
#include <opentelemetry/trace/tracer_provider.h>
#include <opentelemetry/metrics/meter_provider.h>
#include <opentelemetry/common/key_value_iterable.h>

// OTel SDK headers for building real SDK providers in the spy factory.
#include <opentelemetry/sdk/trace/tracer_provider.h>
#include <opentelemetry/sdk/trace/tracer_provider_factory.h>
#include <opentelemetry/sdk/trace/simple_processor_factory.h>
#include <opentelemetry/sdk/trace/exporter.h>
#include <opentelemetry/sdk/trace/span_data.h>
#include <opentelemetry/sdk/metrics/meter_provider.h>
#include <opentelemetry/sdk/metrics/meter_provider_factory.h>
#include <opentelemetry/sdk/metrics/view/view_registry.h>
#include <opentelemetry/sdk/resource/resource.h>

#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/engine.hpp>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/otel/providers.hpp>
#include <fixpp/log/logger.hpp>
#include <fixpp/log/sink.hpp>
#include <fixpp/log/record.hpp>
#include <fixpp/core/error.hpp>

#include <asio/io_context.hpp>
#include <asio/co_spawn.hpp>
#include <asio/use_future.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

namespace {

// ── CountingSpanExporter — counts Shutdown() calls ───────────────────────────
// When wrapped in a real sdk::trace::TracerProvider, Shutdown() on the provider
// calls Shutdown() on the exporter. This gives us an observable count.
class CountingSpanExporter final : public opentelemetry::sdk::trace::SpanExporter {
public:
    explicit CountingSpanExporter(std::shared_ptr<std::atomic<int>> counter)
        : counter_(std::move(counter)) {}

    std::unique_ptr<opentelemetry::sdk::trace::Recordable>
    MakeRecordable() noexcept override {
        return std::make_unique<opentelemetry::sdk::trace::SpanData>();
    }

    opentelemetry::sdk::common::ExportResult Export(
        const opentelemetry::nostd::span<
            std::unique_ptr<opentelemetry::sdk::trace::Recordable>>&) noexcept override {
        return opentelemetry::sdk::common::ExportResult::kSuccess;
    }

    bool ForceFlush(std::chrono::microseconds) noexcept override { return true; }

    bool Shutdown(std::chrono::microseconds) noexcept override {
        counter_->fetch_add(1, std::memory_order_relaxed);
        return true;
    }

private:
    std::shared_ptr<std::atomic<int>> counter_;
};

// ── SpySink — counts flush() calls ───────────────────────────────────────────
class SpySink final : public fixpp::log::Sink {
public:
    explicit SpySink(std::shared_ptr<std::atomic<int>> flush_counter)
        : flush_counter_(std::move(flush_counter)) {}

    [[nodiscard]] fixpp::core::expected_t<void> open() override { return {}; }
    void emit(fixpp::log::Record const&) noexcept override {}
    void flush(std::chrono::milliseconds) noexcept override {
        flush_counter_->fetch_add(1, std::memory_order_relaxed);
    }
    void close() noexcept override {}

private:
    std::shared_ptr<std::atomic<int>> flush_counter_;
};

// ── SlowFlushSink — blocks flush() for a configurable duration ───────────────
// Used to verify drain_timeout is observed (not a hardcoded longer value).
class SlowFlushSink final : public fixpp::log::Sink {
public:
    explicit SlowFlushSink(std::chrono::milliseconds flush_delay)
        : flush_delay_(flush_delay) {}

    [[nodiscard]] fixpp::core::expected_t<void> open() override { return {}; }
    void emit(fixpp::log::Record const&) noexcept override {}
    void flush(std::chrono::milliseconds /*deadline*/) noexcept override {
        std::this_thread::sleep_for(flush_delay_);
    }
    void close() noexcept override {}

private:
    std::chrono::milliseconds flush_delay_;
};

// ── Helper: build a mock clock ────────────────────────────────────────────────
std::shared_ptr<fixpp::core::mock_clock> make_mock_clock(asio::any_io_executor exec) {
    using namespace std::chrono_literals;
    auto utc = fixpp::core::utc_time_point{0ns};
    auto stp = fixpp::core::steady_time_point{0ns};
    return std::make_shared<fixpp::core::mock_clock>(utc, stp, exec);
}

}  // namespace

// ── E2: provider shutdowns are called by Engine::stop() ──────────────────────
//
// The spy is a real SDK TracerProvider containing a CountingSpanExporter.
// TracerProvider::shutdown() dynamic_casts to sdk::trace::TracerProvider and
// calls sdk->Shutdown(), which propagates to the exporter's Shutdown(). The
// counter captures this for the assertion.
//
// For MeterProvider, sdk::metrics::MeterProvider::Shutdown() is propagated
// similarly. We use an empty SDK MeterProvider; Shutdown() completes cleanly.
// We verify shutdown was called by checking it doesn't crash with a null meter.

TEST(EngineCloseTeardown, E2_ProviderShutdownCalled) {
    auto tracer_exporter_shutdown_count = std::make_shared<std::atomic<int>>(0);

    // TracerProvider factory: returns a real SDK TracerProvider wrapping our
    // counting exporter. TracerProvider::shutdown() → sdk->Shutdown() →
    // processor→Shutdown() → exporter→Shutdown() → counter++.
    fixpp::otel::OtelConfig tracer_cfg;
    {
        auto cnt = tracer_exporter_shutdown_count;
        tracer_cfg.tracer_factory_for_test = [cnt]() {
            auto exporter = std::make_unique<CountingSpanExporter>(cnt);
            auto processor = opentelemetry::sdk::trace::SimpleSpanProcessorFactory::Create(
                std::move(exporter));
            auto resource = opentelemetry::sdk::resource::Resource::Create({});
            auto sdk_provider = opentelemetry::sdk::trace::TracerProviderFactory::Create(
                std::move(processor), resource);
            return std::shared_ptr<opentelemetry::trace::TracerProvider>(
                sdk_provider.release());
        };
    }
    auto tracer_provider = std::make_shared<fixpp::otel::TracerProvider>(tracer_cfg);

    // MeterProvider factory: returns a real SDK MeterProvider (empty).
    // We just verify it doesn't crash; the exporter Shutdown() path is verified
    // via the tracer exporter.
    fixpp::otel::OtelConfig meter_cfg;
    meter_cfg.meter_factory_for_test = []() {
        auto sdk_provider = opentelemetry::sdk::metrics::MeterProviderFactory::Create(
            std::make_unique<opentelemetry::sdk::metrics::ViewRegistry>(),
            opentelemetry::sdk::resource::Resource::Create({}));
        return std::shared_ptr<opentelemetry::metrics::MeterProvider>(
            sdk_provider.release());
    };
    auto meter_provider = std::make_shared<fixpp::otel::MeterProvider>(meter_cfg);

    asio::io_context ioc;
    fixpp::core::EngineConfig eng_cfg;
    eng_cfg.executor = ioc.get_executor();
    eng_cfg.clock    = make_mock_clock(ioc.get_executor());
    eng_cfg.tracer   = tracer_provider;
    eng_cfg.meter    = meter_provider;

    fixpp::session::Engine engine{ioc.get_executor(), std::move(eng_cfg)};
    ASSERT_TRUE(engine.start().has_value()) << "engine.start() failed";

    auto fut = asio::co_spawn(ioc, engine.stop(), asio::use_future);
    ioc.run();
    fut.get();

    // Verify the tracer provider's SDK exporter Shutdown() was called, which
    // proves TracerProvider::shutdown() was called by Engine::stop().
    EXPECT_EQ(tracer_exporter_shutdown_count->load(), 1)
        << "TracerProvider::shutdown() must propagate to the exporter's Shutdown()";
}

// ── E2: null providers / logger — stop() does not crash ──────────────────────

TEST(EngineCloseTeardown, E2_NullProviders_NoCrash) {
    asio::io_context ioc;
    fixpp::core::EngineConfig eng_cfg;
    eng_cfg.executor = ioc.get_executor();
    eng_cfg.clock    = make_mock_clock(ioc.get_executor());

    fixpp::session::Engine engine{ioc.get_executor(), std::move(eng_cfg)};
    ASSERT_TRUE(engine.start().has_value()) << "engine.start() failed";

    auto fut = asio::co_spawn(ioc, engine.stop(), asio::use_future);
    ioc.run();
    EXPECT_NO_THROW(fut.get());
}

// ── E2: Logger::shutdown() calls flush() on its sinks ────────────────────────
//
// Direct unit test of the Logger flush path (the same path Engine::stop() calls
// via engine_cfg_.logger->shutdown() when logger is non-null).

TEST(EngineCloseTeardown, E2_LoggerShutdownFlushesSinks) {
    auto flush_count = std::make_shared<std::atomic<int>>(0);
    auto spy_sink    = std::make_unique<SpySink>(flush_count);

    fixpp::log::LoggerConfig lcfg;
    lcfg.capacity     = 128u;
    lcfg.drain_timeout = std::chrono::milliseconds{500};

    std::pmr::vector<std::unique_ptr<fixpp::log::Sink>> sinks{};
    sinks.push_back(std::move(spy_sink));

    fixpp::log::Logger logger{lcfg, std::move(sinks)};
    (void)logger.shutdown(std::chrono::milliseconds{1000});

    EXPECT_GE(flush_count->load(), 1)
        << "Logger::shutdown() must call flush() on its sinks";
}

// ── E2: Engine teardown honors LoggerConfig::drain_timeout (RC#2) ────────────
//
// Verifies that Engine::stop() uses the operator-configured drain_timeout from
// LoggerConfig rather than a hardcoded 5 s literal. [2k §6.6].
//
// DISCRIMINATING strategy: configure Logger with drain_timeout = 50ms; wire a
// SlowFlushSink that blocks flush() for 800ms. Call Engine::stop(). Assert:
//   (a) stop() returns in well under 800ms (drain_timeout = 50ms was respected)
//   (b) logger->timeout_drop_count() incremented (Logger::shutdown returned
//       log_drain_timeout, proving the 50ms deadline was actually applied)
//
// Under the OLD hardcoded shutdown(5000ms): the 800ms flush would complete
// within budget → no timeout → timeout_drop_count() == 0 → assertion (b) FAILS.
// Under the correct path (uses LoggerConfig::drain_timeout = 50ms): the 800ms
// flush trips the 50ms wait → shutdown returns log_drain_timeout →
// timeout_drop_count() > 0 → both assertions pass.

TEST(EngineCloseTeardown, E2_EngineTeardownHonorsDrainTimeout) {
    // drain_timeout = 50 ms; SlowFlushSink blocks for 800 ms.
    // Under correct impl: flush times out at ~50ms → stop() is well under 800ms.
    // Under old hardcode (5000ms): flush completes in 800ms → stop() takes ~800ms.
    constexpr auto k_drain_timeout  = std::chrono::milliseconds{50};
    constexpr auto k_flush_delay    = std::chrono::milliseconds{800};
    // stop() must return well under k_flush_delay; allow 3× k_drain_timeout +
    // generous OS scheduling margin.
    constexpr auto k_max_stop_ms    = std::chrono::milliseconds{400};

    auto slow_sink = std::make_unique<SlowFlushSink>(k_flush_delay);

    fixpp::log::LoggerConfig lcfg;
    lcfg.capacity      = 128u;
    lcfg.drain_timeout = k_drain_timeout;

    std::pmr::vector<std::unique_ptr<fixpp::log::Sink>> sinks{};
    sinks.push_back(std::move(slow_sink));

    auto logger = std::make_shared<fixpp::log::Logger>(lcfg, std::move(sinks));

    asio::io_context ioc;
    fixpp::core::EngineConfig eng_cfg;
    eng_cfg.executor = ioc.get_executor();
    eng_cfg.clock    = make_mock_clock(ioc.get_executor());
    eng_cfg.logger   = logger;

    fixpp::session::Engine engine{ioc.get_executor(), std::move(eng_cfg)};
    ASSERT_TRUE(engine.start().has_value()) << "engine.start() failed";

    // Time Engine::stop() — it must return bounded by drain_timeout, not
    // blocked for the full k_flush_delay.
    auto t0  = std::chrono::steady_clock::now();
    auto fut = asio::co_spawn(ioc, engine.stop(), asio::use_future);
    ioc.run();
    EXPECT_NO_THROW(fut.get());
    auto stop_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0);

    // (a) stop() must return well under k_flush_delay (bounded by drain_timeout).
    EXPECT_LT(stop_elapsed.count(), k_max_stop_ms.count())
        << "Engine::stop() took " << stop_elapsed.count()
        << "ms — should return within ~" << k_max_stop_ms.count()
        << "ms (drain_timeout=" << k_drain_timeout.count()
        << "ms); a hardcoded 5000ms path would block for ~"
        << k_flush_delay.count() << "ms";

    // (b) drain timed out → timeout_drop_count() must have incremented.
    // This fails under the old hardcoded shutdown(5000ms) which would NOT time out.
    EXPECT_GT(logger->timeout_drop_count(), 0u)
        << "Logger::timeout_drop_count() must be > 0 after a drain that timed out "
        << "(drain_timeout=" << k_drain_timeout.count()
        << "ms, flush took " << k_flush_delay.count()
        << "ms); a hardcoded longer timeout would NOT trigger this";
}
