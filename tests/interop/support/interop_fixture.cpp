// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/support/interop_fixture.cpp — 016 T004 impl.

#include "support/interop_fixture.hpp"

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/use_future.hpp>
#include <fixpp/core/system_clock_source.hpp>
#include <future>
#include <memory>
#include <utility>

// (#292) The destructor deliberately leaks the Engine on the
// stop()-did-not-complete path (see the header). Tell LeakSanitizer so the
// intentional leak does not bury the ADD_FAILURE that path exists to report.
#if defined(__has_feature)
#  if __has_feature(address_sanitizer)
#    include <sanitizer/lsan_interface.h>
#    define FIXPP_INTEROP_LSAN_IGNORE(p) __lsan_ignore_object(p)
#  endif
#elif defined(__SANITIZE_ADDRESS__)
#  include <sanitizer/lsan_interface.h>
#  define FIXPP_INTEROP_LSAN_IGNORE(p) __lsan_ignore_object(p)
#endif
#ifndef FIXPP_INTEROP_LSAN_IGNORE
#  define FIXPP_INTEROP_LSAN_IGNORE(p) ((void)(p))
#endif

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
      engine_(std::make_unique<fixpp::session::Engine>(ioc_.get_executor(),
                                                       with_clock(std::move(cfg), clock_))) {}

InteropEngineFixture::~InteropEngineFixture() {
    // (#292) Drive the fixture's own stop() to completion if it has not already
    // completed. Deliberately NOT gated on engine_->stopped(): that flag is set
    // at step 1 of stop()'s teardown (engine.cpp:1196), so it reports true while
    // steps 2-5 are still suspended. Gating on it would take the "safe" branch
    // for exactly the partially-torn-down case that is least safe.
    if (!stop_completed_) {
        try {
            (void)stop_within(teardown_bound_);
        } catch (...) {
            // stop() completed by THROWING. A destructor is implicitly noexcept,
            // so letting this escape would call std::terminate and destroy the
            // named failure this branch exists to report. The throw is reported
            // below via the not-completed path.
        }
    }

    if (stop_completed_) {
        return;
    }

    // Already-failing path. The stop() frame is still suspended inside ioc_ and
    // holds Engine&. Letting ~Engine run would either trip its stopped_ assert
    // and ABORT (stop never entered), or — worse, because it is silent — pass
    // that assert while teardown frames are still live (stop entered but did not
    // finish). Leak on purpose: the Engine, its EngineConfig-owned clock and its
    // control_strand_ then outlive ioc_, so the frames ~ioc_ destroys next still
    // have live referents. See the header for why reordering members instead
    // would be strictly worse.
    auto* leaked = engine_.release();
    FIXPP_INTEROP_LSAN_IGNORE(leaked);

    ADD_FAILURE() << "InteropEngineFixture: Engine::stop() did not complete within the "
                  << teardown_bound_.count()
                  << " ms teardown bound; the Engine was leaked deliberately so this "
                     "failure is reported instead of ~Engine being destroyed while its "
                     "teardown frames are still suspended (#292).";
}

void InteropEngineFixture::start() {
    // The fixture injects a real clock via with_clock() at construction (see
    // the with_clock() call in the ctor), so validate_engine_config succeeds
    // unconditionally here.  Assert to surface any future misconfiguration
    // rather than silently running without session loops.  [041 T019 / C-4]
    auto r = engine_->start();
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
    // (#292) Gate on the OPERATION's completion, not on Engine::stopped(). The
    // old early return here used stopped(), so a second call returned 0 ms
    // "already stopped" while the first stop() was still suspended mid-teardown.
    if (stop_completed_) {
        return std::chrono::milliseconds{0};
    }

    const auto t0 = std::chrono::steady_clock::now();
    if (ioc_.stopped()) {
        ioc_.restart();
    }

    // Spawn EXACTLY ONCE. A second stop_within() must pump the operation already
    // in flight; co_spawning stop() again would run a second teardown over an
    // engine the first one is still tearing down.
    if (!stop_spawned_) {
        stop_fut_ = asio::co_spawn(ioc_, engine_->stop(), asio::use_future);
        stop_spawned_ = true;
    }

    // We must keep pumping the io_context for stop()'s teardown coroutines
    // (cancellation, join-before-clear) to make progress.
    const auto t_end = t0 + bound;
    const auto ready = [this] {
        return stop_fut_.valid() &&
               stop_fut_.wait_for(std::chrono::seconds{0}) == std::future_status::ready;
    };
    while (std::chrono::steady_clock::now() < t_end) {
        if (ready()) {
            break;
        }
        if (ioc_.stopped()) {
            ioc_.restart();
        }
        ioc_.run_for(kPumpSlice);
    }

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0);

    // Completion is recorded ONLY after the future resolved. get() both surfaces
    // any exception stop() threw (propagated to the test) and invalidates the
    // future, which is why stop_spawned_ is tracked separately.
    if (ready()) {
        stop_fut_.get();
        stop_completed_ = true;
    }
    return elapsed;
}

}  // namespace fixpp::interop
