// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/support/interop_fixture.cpp — 016 T004 impl.

#include "support/interop_fixture.hpp"

#include <asio/co_spawn.hpp>
#include <asio/use_future.hpp>
#include <fixpp/core/system_clock_source.hpp>
#include <future>
#include <utility>

#include "support/pump_until_ready.hpp"

namespace fixpp::interop {

namespace {
// io_context pump granularity. Small enough that wall-clock deadlines are honored
// promptly, large enough to avoid busy-spinning the test thread.
constexpr std::chrono::milliseconds kPumpSlice{5};

// Inject a real system clock into the EngineConfig when the caller left it null,
// so outbound admin messages carry a populated SendingTime(52) a real counterparty
// accepts. Honors an explicit caller-supplied clock (does not overwrite).
fixpp::core::EngineConfig with_clock(fixpp::core::EngineConfig cfg,
                                     std::shared_ptr<fixpp::core::Clock> clock) {
    if (!cfg.clock) {
        cfg.clock = std::move(clock);
    }
    return cfg;
}
}  // namespace

InteropEngineFixture::InteropEngineFixture(fixpp::core::EngineConfig cfg)
    : clock_(std::make_shared<fixpp::core::system_clock_source>(ioc_.get_executor())),
      engine_(ioc_.get_executor(), with_clock(std::move(cfg), clock_)) {}

InteropEngineFixture::~InteropEngineFixture() {
    // The Engine dtor asserts stopped(); guarantee a completed stop() first.
    // Generous bound — teardown correctness, not the watchdog assertion.
    if (!engine_.stopped()) {
        stop_within(std::chrono::seconds{30});
    }
}

void InteropEngineFixture::start() {
    // The fixture injects a real clock via with_clock() at construction (see
    // the with_clock() call in the ctor), so validate_engine_config succeeds
    // unconditionally here.  Assert to surface any future misconfiguration
    // rather than silently running without session loops.  [041 T019 / C-4]
    auto r = engine_.start();
    assert(r.has_value() && "InteropEngineFixture::start() — engine_.start() failed");
    (void)r;
}

bool InteropEngineFixture::run_until(const std::function<bool()>& ready,
                                     std::chrono::milliseconds deadline) {
    // pump_until does not revive a context already stopped at entry (a work
    // guard does not clear stopped()); this fixture's contract does. Restart
    // here, before handing off to the shared primitive.
    if (ioc_.stopped()) {
        ioc_.restart();
    }
    return fixpp::test_support::pump_until(
        ioc_, [&ready] { return ready(); }, deadline, kPumpSlice);
}

std::chrono::milliseconds InteropEngineFixture::stop_within(std::chrono::milliseconds bound) {
    if (engine_.stopped()) {
        return std::chrono::milliseconds{0};
    }

    const auto t0 = std::chrono::steady_clock::now();
    if (ioc_.stopped()) {
        ioc_.restart();
    }
    // co_spawn Engine::stop() as a detached-with-future operation and pump until it
    // resolves or `bound` elapses. We must keep pumping the io_context for stop()'s
    // teardown coroutines (cancellation, join-before-clear) to make progress.
    auto fut = asio::co_spawn(ioc_, engine_.stop(), asio::use_future);

    const auto t_end = t0 + bound;
    while (std::chrono::steady_clock::now() < t_end) {
        if (fut.wait_for(std::chrono::seconds{0}) == std::future_status::ready) {
            break;
        }
        if (ioc_.stopped()) {
            ioc_.restart();
        }
        ioc_.run_for(kPumpSlice);
    }

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0);

    // If stop() completed, surface any exception it threw (propagate to the test).
    if (fut.wait_for(std::chrono::seconds{0}) == std::future_status::ready) {
        fut.get();
    }
    return elapsed;
}

}  // namespace fixpp::interop
