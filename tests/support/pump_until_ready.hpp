// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/support/pump_until_ready.hpp
//
// Bounded pumps for manually-driven io_contexts, and the teardown guard their
// failure path requires.
//
// Hoisted (issue #284) from tests/session/logout_exchange_test.cpp, which
// introduced `pump_until_ready` for exactly this defect. Parameterised on the
// way out, because it is the seventh member of a family that had each solved
// the same problem separately and incompatibly — see the sibling census in
// #284, alongside the census of raw `run_for(W); restart(); get()` sites that
// have no helper at all.

#pragma once

#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>
#include <chrono>
#include <future>

#include <fixpp/core/clock.hpp>

namespace fixpp::test_support {

// Default budget for a bounded pump. Generous by design: it converts a wedge
// into a diagnosable failure, it is not a performance assertion.
inline constexpr auto kPumpBudget = std::chrono::seconds{10};

// Default slice. Note this is a COST FLOOR, not just a resolution: `run_for`
// on a context with outstanding work never drains early, so a pump built on it
// burns a whole slice per call however fast the work is. Measured on 106 calls
// of a 5-post operation: 20 ms slice = 2151 ms, 1 ms slice = 124 ms. Callers
// that pump often should pass something small.
inline constexpr auto kPumpSlice = std::chrono::milliseconds{1};

// Drive `ioc` until `ready()` returns true, or `budget` elapses. Returns
// whether it became ready.
//
// Replaces the `ioc.run_for(FIXED); ioc.restart(); fut.get()` idiom, which
// deadlocks when the awaited op posts its completion back to `ioc` AFTER the
// fixed window closes and nothing pumps `ioc` again (observed as a 120 s ctest
// timeout on slow MSVC-debug runs of the FileStore-offloaded test, and again on
// the linux-clang-coverage lane as issue #284).
//
// The window expiring is a LIVE outcome, not a pathology: `io_context::run_for`
// is `run_until`, and `run_one_until` tests `now < abs_time` BEFORE dispatching
// (asio impl/io_context.hpp), so an expired deadline returns leaving ready
// handlers QUEUED. Worse, a co_spawned coroutine holds an outstanding-work
// guard, so `run_for` never drains early — whether the op finished inside the
// window is purely a scheduling question.
//
// A work_guard keeps `ioc` alive across slices; the trailing restart() leaves
// it runnable for the next phase (a stopped context makes the next run_for a
// silent no-op).
template <class Ready>
[[nodiscard]] bool pump_until(asio::io_context& ioc, Ready ready,
                              std::chrono::steady_clock::duration budget = kPumpBudget,
                              std::chrono::steady_clock::duration slice = kPumpSlice) {
    auto wg = asio::make_work_guard(ioc);
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (!ready() && std::chrono::steady_clock::now() < deadline) {
        ioc.run_for(slice);
    }
    wg.reset();
    const bool done = ready();
    ioc.restart();
    return done;
}

// Future-shaped specialisation of `pump_until`.
//
// Callers MUST check the bool BEFORE fut.get(), so a genuine lost-wake FAILs
// loudly at the budget instead of hanging out the ctest timeout. On the false
// path the awaited coroutine is still SUSPENDED, holding references to whatever
// the caller passed it; destroying that frame after those objects die is
// undefined, so the caller must also quiesce before teardown — `quiesce_on_exit`
// below is the scope-guard form that covers ASSERT_*'s early return.
//
// This is a TEST-harness utility: production drives the io_context with a
// continuous ioc.run() on worker threads (src/capi/engine.cpp), so a real
// client never encounters this — it is an artifact of use_future + a manually
// pumped, fixed-window io_context. [[feedback_mock_clock_advance_before_timer_armed_race]]
template <class Fut>
[[nodiscard]] bool pump_until_ready(asio::io_context& ioc, Fut& fut,
                                    std::chrono::steady_clock::duration budget = kPumpBudget,
                                    std::chrono::steady_clock::duration slice = kPumpSlice) {
    return pump_until(
        ioc,
        [&fut] { return fut.wait_for(std::chrono::seconds{0}) == std::future_status::ready; },
        budget, slice);
}

// Failure text for a `pump_until*` that ran out of budget. Stream the site name
// after it.
inline constexpr const char* kPumpBudgetMiss =
    "#284: the operation did not complete within the bounded-pump budget. Site: ";

// Destroy any still-suspended coroutine frames while the objects they reference
// are alive.
//
// Declare AFTER the fixtures whose lifetimes are at stake, so it runs BEFORE
// them, on every exit path including the early `return` an ASSERT_* performs.
// Reordering the declarations instead cannot work: the clock's parked waiters
// hold work guards on the io_context, so EITHER destruction order has a
// dangling side. The only safe state is no pending state.
//
// `run_for` rather than a `run_one_for` loop deliberately: `run_one_until` also
// tests its deadline before dispatching, so a slice-at-a-time drain can return
// on "nothing was ready within the slice" and leave work queued — the very
// mechanism documented above. `run_for` drains until the context genuinely runs
// out of work, and costs ~nothing when there is none.
struct quiesce_on_exit {
    asio::io_context& ioc;
    fixpp::core::Clock& clock;

    ~quiesce_on_exit() {
        clock.cancel_sleeps();
        ioc.restart();
        ioc.run_for(std::chrono::seconds{5});
    }
};

}  // namespace fixpp::test_support
