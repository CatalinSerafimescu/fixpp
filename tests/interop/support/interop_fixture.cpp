// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/support/interop_fixture.cpp — 016 T004 impl.

#include "support/interop_fixture.hpp"

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/use_future.hpp>
#include <fixpp/core/system_clock_source.hpp>
#include <memory>
#include <utility>

// (#292) The destructor deliberately leaks the Engine on the
// stop()-did-not-complete path (see the header). Tell LeakSanitizer so the
// intentional leak does not bury the ADD_FAILURE that path exists to report.
//
// WAIVED, GRAPH-WIDE, stated rather than understated (gate-b/r1 P2-1): this is
// not a one-allocation excuse. LSan treats the ignored Engine as a ROOT, so
// everything reachable through it — registry, Sessions, listeners, transports,
// clock, logger/provider state, buffers — also drops out of leak reports, and
// those resources are retained until process exit. The round-1 rationale said "reached
// only when the test has ALREADY failed, so no red run is turned green". That is
// WRONG for this binary (gate-b/r2 P2-3): the miss witnesses run inside
// EXPECT_NONFATAL_FAILURE, which CONSUMES the expected failure and leaves
// interop_support_smoke_test GREEN — so an unrelated real leak reachable through
// one of those ignored graphs would be hidden in a green sanitizer binary.
//
// Accepted anyway, with that cost stated rather than denied: the alternative is
// an LSan report burying the named diagnostic this path exists to produce, and
// the suppressed graphs are default-EngineConfig Engines with NO registered
// sessions, no listeners and no stores — the reachable set is the clock and the
// strand. Real exposure, small and bounded to this binary.
//
// Shape follows the repo's established sanitizer-detection idiom (two separate
// #if blocks, not an #elif chain) — see tests/alloc_guard/
// test_validate_gate_alloc_guard.cpp:95-105. An #elif chain would skip the
// __SANITIZE_ADDRESS__ arm on any compiler that defines __has_feature without
// reporting address_sanitizer through it.
//
// MSVC is excluded deliberately, and this is not hypothetical: windows-msvc-asan
// is a real tier2 lane (tier2.yml), its Conan profile sets /fsanitize=address so
// __SANITIZE_ADDRESS__ IS defined, tests/interop/CMakeLists.txt has no platform
// guard, and the preset inherits FIXPP_WERROR=ON. MSVC's ASan ships no
// LeakSanitizer and no <sanitizer/lsan_interface.h>, so reaching the include
// there is a hard compile error. There is nothing to suppress on that lane
// anyway — no leak detector runs.
#if !defined(_MSC_VER)
#  if defined(__has_feature)
#    if __has_feature(address_sanitizer)
#      define FIXPP_INTEROP_HAVE_LSAN 1
#    endif
#  endif
#  if !defined(FIXPP_INTEROP_HAVE_LSAN) && defined(__SANITIZE_ADDRESS__)
#    define FIXPP_INTEROP_HAVE_LSAN 1
#  endif
#endif

#if defined(FIXPP_INTEROP_HAVE_LSAN)
#  include <sanitizer/lsan_interface.h>
#  define FIXPP_INTEROP_LSAN_IGNORE(p) __lsan_ignore_object(p)
#else
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

InteropEngineFixture::InteropEngineFixture(fixpp::core::EngineConfig cfg,
                                           std::chrono::milliseconds teardown_bound)
    : clock_(std::make_shared<fixpp::core::system_clock_source>(ioc_.get_executor())),
      engine_(std::make_unique<fixpp::session::Engine>(ioc_.get_executor(),
                                                       with_clock(std::move(cfg), clock_))),
      teardown_bound_(teardown_bound) {}

InteropEngineFixture::~InteropEngineFixture() {
    // (#292) Drive the fixture's own stop() to completion if it has not already
    // completed. Deliberately NOT gated on engine_->stopped(): that flag is set
    // at step 1 of stop()'s teardown (engine.cpp:1196), so it reports true while
    // steps 2-5 are still suspended. Gating on it would take the "safe" branch
    // for exactly the partially-torn-down case that is least safe.
    // NESTED guards, and the nesting is the point (gate-b/r2 P1-1).
    //
    // Round 1 wrapped the whole body in ONE try/catch to stop ADD_FAILURE()
    // escaping a noexcept destructor. That fix was incomplete in a way that
    // reintroduced the very defect this PR exists to remove: a throw out of
    // stop_within() jumped straight to the outer handler, skipping the
    // stop_completed_ check, the release() AND the report — so the Engine was
    // destroyed SILENTLY with teardown incomplete. Because stop() sets stopped_
    // at step 1, ~Engine's assert would have passed on the way out.
    //
    // INNER catch: stop() finished by throwing, which means teardown did NOT
    // complete. Swallow the exception but FALL THROUGH to the failure path.
    // OUTER catch: last resort for the report itself — ADD_FAILURE() is not
    // nothrow (gtest's AddTestPartResult throws GoogleTestFailureException under
    // --gtest_throw_on_failure, and the streaming chain can throw on allocation).
    try {
        try {
            // No `if (!stop_completed_)` guard: stop_within already returns
            // immediately on that flag, so the branch would duplicate it.
            (void)stop_within(teardown_bound_);
        } catch (...) {
            // Deliberately NOT reported here — stop_completed_ is still false,
            // so the path below reports and releases. Returning or rethrowing
            // here is what round 1 got wrong.
        }

        if (stop_completed_) {
            return;
        }

        // Already-failing path: stop() timed out, or it threw partway through
        // teardown. Either way its frame may still be suspended inside ioc_
        // holding Engine&. Letting ~Engine run would either trip its stopped_
        // assert and ABORT (stop never entered), or — worse, because it is
        // silent — pass that assert while teardown frames are still live (stop
        // entered but did not finish). Leak on purpose: the Engine, its
        // EngineConfig-owned clock and its control_strand_ then outlive ioc_, so
        // the frames ~ioc_ destroys next still have live referents. See the
        // header for why reordering members instead would be strictly worse.
        auto* leaked = engine_.release();
        FIXPP_INTEROP_LSAN_IGNORE(leaked);

        ADD_FAILURE() << "InteropEngineFixture: Engine::stop() did not complete successfully "
                         "(it timed out within the "
                      << teardown_bound_.count()
                      << " ms teardown bound, or it threw); the Engine was leaked deliberately "
                         "so this failure is reported instead of ~Engine being destroyed while "
                         "its teardown frames are still suspended (#292).";
    } catch (...) {
        // Nothing may escape a noexcept destructor.
    }
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
    // engine the first one is still tearing down. shared_future::get() does not
    // invalidate, so valid() is a permanent "already spawned" flag — including on
    // the path where get() threw. See the header for why that matters.
    if (!stop_fut_.valid()) {
        stop_fut_ = asio::co_spawn(ioc_, engine_->stop(), asio::use_future).share();
    }

    // We must keep pumping the io_context for stop()'s teardown coroutines
    // (cancellation, join-before-clear) to make progress.
    const auto t_end = t0 + bound;
    const auto ready = [this] {
        return stop_fut_.wait_for(std::chrono::seconds{0}) == std::future_status::ready;
    };
    while (std::chrono::steady_clock::now() < t_end) {
        if (ready()) {
            break;
        }
        if (ioc_.stopped()) {
            ioc_.restart();
        }
        // Clamp the final slice to the remaining budget (gate-b/r1 P1-1). An
        // unclamped run_for can START at t_end - 1ms and run a whole 5 ms slice,
        // so completion at t_end + 4 ms was accepted as "within bound". The
        // shared pump does the same clamp for the same reason.
        // `remaining` is recomputed and TESTED here rather than reusing the loop
        // condition's sample (gate-b/r2 P3-1): the deadline can pass BETWEEN the
        // two now() calls, handing run_for a negative duration. Current asio
        // treats that as already-expired, but the arithmetic should not rely on it.
        const auto remaining = t_end - std::chrono::steady_clock::now();
        if (remaining <= std::chrono::steady_clock::duration::zero()) {
            break;
        }
        ioc_.run_for(std::min<std::chrono::steady_clock::duration>(kPumpSlice, remaining));
    }

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0);

    // Completion is recorded ONLY after the future resolved AND get() returned
    // normally. get() surfaces any exception stop() threw, propagating it to the
    // caller. shared_future::get() does NOT invalidate and rethrows on every
    // subsequent call, so a throwing stop() leaves the operation observably
    // finished (valid, ready) rather than permanently unready — which is what
    // keeps the spawn-once guard correct on that path.
    //
    // ORDER IS LOAD-BEARING: get() first, then set the flag. Swapping them makes
    // a throwing stop() look like a completed one, and the destructor would then
    // destroy the Engine as though teardown had succeeded.
    if (ready()) {
        stop_fut_.get();
        stop_completed_ = true;
    }
    return elapsed;
}

}  // namespace fixpp::interop
