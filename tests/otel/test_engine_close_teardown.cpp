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
#include <opentelemetry/common/key_value_iterable.h>
#include <opentelemetry/metrics/meter_provider.h>
#include <opentelemetry/metrics/noop.h>
#include <opentelemetry/trace/noop.h>
#include <opentelemetry/trace/tracer_provider.h>
#include <opentelemetry/version.h>

// OTel SDK headers for building real SDK providers in the spy factory.
#include <gtest/gtest.h>
#include <opentelemetry/sdk/metrics/meter_provider.h>
#include <opentelemetry/sdk/metrics/meter_provider_factory.h>
#include <opentelemetry/sdk/metrics/view/view_registry.h>
#include <opentelemetry/sdk/resource/resource.h>
#include <opentelemetry/sdk/trace/exporter.h>
#include <opentelemetry/sdk/trace/simple_processor_factory.h>
#include <opentelemetry/sdk/trace/span_data.h>
#include <opentelemetry/sdk/trace/tracer_provider.h>
#include <opentelemetry/sdk/trace/tracer_provider_factory.h>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <atomic>
#include <chrono>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/log/logger.hpp>
#include <fixpp/log/record.hpp>
#include <fixpp/log/sink.hpp>
#include <fixpp/otel/providers.hpp>
#include <fixpp/session/engine.hpp>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>

#include "support/pump_until_ready.hpp"

namespace {

// ── CountingSpanExporter — counts Shutdown() calls ───────────────────────────
// When wrapped in a real sdk::trace::TracerProvider, Shutdown() on the provider
// calls Shutdown() on the exporter. This gives us an observable count.
class CountingSpanExporter final : public opentelemetry::sdk::trace::SpanExporter {
public:
    explicit CountingSpanExporter(std::shared_ptr<std::atomic<int>> counter)
        : counter_(std::move(counter)) {}

    std::unique_ptr<opentelemetry::sdk::trace::Recordable> MakeRecordable() noexcept override {
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

// ── SlowFlushSink — blocks flush() until released, or until max_block ────────
// Used to verify drain_timeout is observed (not a hardcoded longer value).
//
// THE BLOCK IS RELEASE-GATED RATHER THAN A FIXED SLEEP, and that is what lets
// `max_block` be sized for discrimination instead of for wall clock (#446).
// `Logger::shutdown()` abandons a drain that timed out WITHOUT joining it
// (src/log/logger.cpp, step 4b runs only on the signalled path), but `~Logger`
// joins unconditionally (`~Impl`). So a sink that sleeps a fixed duration is
// paid in full on the CORRECT path too, at the join — buying margin by
// lengthening the sleep would lengthen every green run by the same amount.
// Gated, the correct path pays only the drain timeout and releases, while the
// defect path — which never reaches the release, because it is still inside
// stop() — waits out `max_block`. The block is bounded rather than indefinite
// so that a caller which never releases (an early return on a teardown miss)
// cannot wedge the join.
class SlowFlushSink final : public fixpp::log::Sink {
public:
    // Shared with the test, which releases it once the measurement it gates is
    // taken. `flush()` runs on the Logger's drain thread; the test runs on its
    // own. Hence the mutex/condvar rather than a flag.
    struct Gate {
        std::mutex mutex;
        std::condition_variable cv;
        bool released{false};

        void release() {
            {
                std::scoped_lock lock(mutex);
                released = true;
            }
            cv.notify_all();
        }
    };

    SlowFlushSink(std::shared_ptr<Gate> gate, std::chrono::milliseconds max_block)
        : gate_(std::move(gate)), max_block_(max_block) {}

    [[nodiscard]] fixpp::core::expected_t<void> open() override { return {}; }
    void emit(fixpp::log::Record const&) noexcept override {}
    void flush(std::chrono::milliseconds /*deadline*/) noexcept override {
        std::unique_lock lock(gate_->mutex);
        (void)gate_->cv.wait_for(lock, max_block_, [this] { return gate_->released; });
    }
    void close() noexcept override {}

private:
    std::shared_ptr<Gate> gate_;
    std::chrono::milliseconds max_block_;
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
            auto processor =
                opentelemetry::sdk::trace::SimpleSpanProcessorFactory::Create(std::move(exporter));
            auto resource = opentelemetry::sdk::resource::Resource::Create({});
            auto sdk_provider = opentelemetry::sdk::trace::TracerProviderFactory::Create(
                std::move(processor), resource);
            return std::shared_ptr<opentelemetry::trace::TracerProvider>(sdk_provider.release());
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
        return std::shared_ptr<opentelemetry::metrics::MeterProvider>(sdk_provider.release());
    };
    auto meter_provider = std::make_shared<fixpp::otel::MeterProvider>(meter_cfg);

    asio::io_context ioc;
    fixpp::core::EngineConfig eng_cfg;
    eng_cfg.executor = ioc.get_executor();
    eng_cfg.clock = make_mock_clock(ioc.get_executor());
    eng_cfg.tracer = tracer_provider;
    eng_cfg.meter = meter_provider;

    fixpp::session::Engine engine{ioc.get_executor(), std::move(eng_cfg)};
    ASSERT_TRUE(engine.start().has_value()) << "engine.start() failed";

    auto fut = asio::co_spawn(ioc, engine.stop(), asio::use_future);
    if (!fixpp::test_support::run_to_exhaustion_or_report(
            ioc, fut, "EngineCloseTeardown::E2_ProviderShutdownCalled")) {
        return;
    }
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
    eng_cfg.clock = make_mock_clock(ioc.get_executor());

    fixpp::session::Engine engine{ioc.get_executor(), std::move(eng_cfg)};
    ASSERT_TRUE(engine.start().has_value()) << "engine.start() failed";

    auto fut = asio::co_spawn(ioc, engine.stop(), asio::use_future);
    if (!fixpp::test_support::run_to_exhaustion_or_report(
            ioc, fut, "EngineCloseTeardown::E2_NullProviders_NoCrash")) {
        return;
    }
    EXPECT_NO_THROW(fut.get());
}

// ── E2: Logger::shutdown() calls flush() on its sinks ────────────────────────
//
// Direct unit test of the Logger flush path (the same path Engine::stop() calls
// via engine_cfg_.logger->shutdown() when logger is non-null).

TEST(EngineCloseTeardown, E2_LoggerShutdownFlushesSinks) {
    auto flush_count = std::make_shared<std::atomic<int>>(0);
    auto spy_sink = std::make_unique<SpySink>(flush_count);

    fixpp::log::LoggerConfig lcfg;
    lcfg.capacity = 128U;
    lcfg.drain_timeout = std::chrono::milliseconds{500};

    std::pmr::vector<std::unique_ptr<fixpp::log::Sink>> sinks{};
    sinks.push_back(std::move(spy_sink));

    fixpp::log::Logger logger{lcfg, std::move(sinks)};
    (void)logger.shutdown(std::chrono::milliseconds{1000});

    EXPECT_GE(flush_count->load(), 1) << "Logger::shutdown() must call flush() on its sinks";
}

// ── E2: Engine teardown honors LoggerConfig::drain_timeout (RC#2) ────────────
//
// Verifies that Engine::stop() uses the operator-configured drain_timeout from
// LoggerConfig rather than a hardcoded 5 s literal. [2k §6.6].
//
// DISCRIMINATING strategy: configure Logger with drain_timeout = 50ms; wire a
// SlowFlushSink whose flush() blocks until the test releases it, for at most
// k_flush_block. Call Engine::stop(). Assert:
//   (a) stop() returns well below k_flush_block (drain_timeout was respected)
//   (b) logger->timeout_drop_count() incremented (Logger::shutdown returned
//       log_drain_timeout, proving the 50ms deadline was actually applied)
//
// Under the OLD hardcoded shutdown(5000ms): the drain is not cut short, so the
// gate is never released (the release is after stop() returns) and flush()
// waits out k_flush_block → stop() takes ~k_flush_block, which is above (a)'s
// bound, AND the drain completes inside the 5000ms budget → no timeout →
// timeout_drop_count() == 0. Both assertions FAIL.
// Under the correct path (uses LoggerConfig::drain_timeout = 50ms): the gated
// flush cannot return before the 50ms wait expires → shutdown returns
// log_drain_timeout → timeout_drop_count() > 0, and stop() returns at ~50ms
// plus engine teardown. Both assertions pass.
//
// ⚠️ (b)'s discrimination is a CONDITION on k_flush_block, not a free property:
// it must stay below the hardcoded value the defect path would use, or the
// defect path would ALSO time out and (b) would stop telling the two apart.
// ⚠️ THE GREEN PATH'S OUTCOME IS STRUCTURAL, NOT A RACE. In the correct path
// stop() can only return via the drain timeout: the gate is released only after
// stop() returns, so drain_done_ cannot be set before the 50ms wait expires.

TEST(EngineCloseTeardown, E2_EngineTeardownHonorsDrainTimeout) {
    constexpr auto k_drain_timeout = std::chrono::milliseconds{50};

    // How long an unreleased flush() blocks. This is the DEFECT path's floor:
    // `cv::wait_for` does not return early without the predicate, so a drain
    // that is not cut short cannot finish sooner than this, however fast the
    // runner is. Kept well below the 5000ms a hardcoded drain would allow, so
    // that path still completes inside its budget and (b) stays discriminating.
    constexpr auto k_flush_block = std::chrono::milliseconds{3000};

    // (a)'s bound is derived from that floor, the side that CANNOT move — not
    // from the correct path's latency, the side that can. The previous bound
    // was 3× k_drain_timeout plus a margin, i.e. derived from the moving side,
    // and a loaded MSVC ASan runner measured 560ms against it (#446).
    // A scheduling stall is ADDITIVE and only lengthens the correct path, so a
    // false red now needs one stall of about k_stall_margin. Size that against
    // the largest stall a sanitizer runner can produce, never against the
    // correct path's measured latency (#470).
    constexpr auto k_stall_margin = std::chrono::milliseconds{1500};
    constexpr auto k_max_stop_ms = k_flush_block - k_stall_margin;

    auto gate = std::make_shared<SlowFlushSink::Gate>();
    auto slow_sink = std::make_unique<SlowFlushSink>(gate, k_flush_block);

    fixpp::log::LoggerConfig lcfg;
    lcfg.capacity = 128U;
    lcfg.drain_timeout = k_drain_timeout;

    std::pmr::vector<std::unique_ptr<fixpp::log::Sink>> sinks{};
    sinks.push_back(std::move(slow_sink));

    auto logger = std::make_shared<fixpp::log::Logger>(lcfg, std::move(sinks));

    asio::io_context ioc;
    fixpp::core::EngineConfig eng_cfg;
    eng_cfg.executor = ioc.get_executor();
    eng_cfg.clock = make_mock_clock(ioc.get_executor());
    eng_cfg.logger = logger;

    fixpp::session::Engine engine{ioc.get_executor(), std::move(eng_cfg)};
    ASSERT_TRUE(engine.start().has_value()) << "engine.start() failed";

    // Time Engine::stop() — it must return bounded by drain_timeout, not
    // blocked for the full k_flush_delay.
    auto t0 = std::chrono::steady_clock::now();
    auto fut = asio::co_spawn(ioc, engine.stop(), asio::use_future);
    if (!fixpp::test_support::run_to_exhaustion_or_report(
            ioc, fut, "EngineCloseTeardown::E2_EngineTeardownHonorsDrainTimeout")) {
        return;
    }
    EXPECT_NO_THROW(fut.get());
    auto stop_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0);

    // The measurement (a) gates is taken; let the drain thread finish so
    // ~Logger's unconditional join does not wait out k_flush_block.
    gate->release();

    // Reported UNCONDITIONALLY, not only on failure: ctest runs
    // --output-on-failure, so a passing run otherwise prints no timing at all
    // and the margin this bound actually has stays unknown (#446).
    std::cout << "[ INFO     ] E2_EngineTeardownHonorsDrainTimeout: stop_elapsed="
              << stop_elapsed.count() << "ms, bound=" << k_max_stop_ms.count()
              << "ms, defect-path floor=" << k_flush_block.count() << "ms\n";

    // (a) stop() must return well below k_flush_block (bounded by drain_timeout).
    EXPECT_LT(stop_elapsed.count(), k_max_stop_ms.count())
        << "Engine::stop() took " << stop_elapsed.count() << "ms — should return within ~"
        << k_max_stop_ms.count() << "ms (drain_timeout=" << k_drain_timeout.count()
        << "ms); a hardcoded 5000ms path would block for ~" << k_flush_block.count() << "ms";

    // (b) drain timed out → timeout_drop_count() must have incremented.
    // This fails under the old hardcoded shutdown(5000ms) which would NOT time out.
    EXPECT_GT(logger->timeout_drop_count(), 0U)
        << "Logger::timeout_drop_count() must be > 0 after a drain that timed out "
        << "(drain_timeout=" << k_drain_timeout.count() << "ms, flush blocks for up to "
        << k_flush_block.count() << "ms); a hardcoded longer timeout would NOT trigger this";
}
