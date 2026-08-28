// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/support/wait_until.hpp
//
// Bounded wait for an executor that drives ITSELF.
//
// Hoisted (issue #315) from three file-local copies that had each solved the
// same problem separately: `wait_pred_nodrive` (test_engine_session_strand.cpp),
// `wait_for_pred_nodrive` (test_business_messages_roundtrip.cpp, whose own
// comment already recorded that it mirrored the first), and a family of inline
// `while (!flag) sleep_for(...)` loops across tests/capi and tests/session.
//
// `pump_until` in pump_until_ready.hpp requires the CALLING thread to drive the
// io_context. That is wrong -- and in one case not even expressible -- when the
// executor services its own work:
//
//   - an `asio::thread_pool` runs its own threads, so there is nothing for the
//     test thread to pump and `run_for` is not callable on it at all;
//   - an `io_context` that worker threads are already inside `run()` on: calling
//     `restart()` from another thread while workers are in `run()` is asio UB
//     (observed as a SEGFAULT under gcc-release);
//   - the C ABI owns its own internal io_context and worker threads entirely,
//     so a tests/capi caller has no context in scope to drive.
//
// So this polls and yields instead. Waiting on the OBSERVABLE EVENT rather than
// a fixed `sleep_for` is the whole point: a fixed window that under-serves does
// not hang, it silently shortens the corpus that every assertion downstream
// runs over (issue #284).
//
// ── Why this is NOT in pump_until_ready.hpp, where #315 proposed it ───────────
//
// That header pulls in gtest, asio's io_context and work guard, fixpp's Clock
// and Transport. tests/capi holds the largest group of call sites and includes
// none of that -- those tests exercise the C ABI and deliberately stay clear of
// the C++ internals. Putting the twin there would have forced asio into every
// one of them to gain a function that uses no asio at all.
//
// `pump_until_ready.hpp` includes THIS header, so every existing pump caller
// still sees `wait_until_observed` through the include it already has, which is the
// adjacency #315 actually asked for. The dependency edge only runs one way.
//
// ── What belongs on this helper, and what does not ───────────────────────────
//
// #315 asked for "a repo-wide grep for a sleep-poll wait loop in tests/ finds
// only the shared helper's own definition". That bar is not reachable and should
// not be aimed for: a scan for `sleep_for` inside a loop matches 37 sites here,
// and most are not this shape at all. A site migrates only if ALL FOUR hold:
//
//   1. the loop body does nothing but sleep. A body that also calls `drain(clk)`
//      (test_sleep_cancel_race) or `publish(i++)` (test_atomic_shared_ptr_
//      concurrency) is DRIVING something, and handing that work to a predicate
//      would break the "ready only observes" contract above;
//   2. the exit condition is predicate-or-DEADLINE -- not retry-on-error
//      (store_temp_dir), not a producer loop, and not an ITERATION ceiling. The
//      weak_ptr drain in test_atomic_shared_ptr_concurrency polls `!expired()`
//      for 200 turns rather than for a duration; converting that bound to a
//      wall-clock one would change what it asserts, so it stays;
//   3. the target can reach tests/. `capi_public_roundtrip_test` declares no
//      include directories at all, on purpose;
//   4. the local copy is not load-bearing for what the test proves. Same target:
//      its poll helpers ARE the demonstration that the public C API needs nothing
//      else in scope, so consolidating them would quietly retire the proof.
//
// A `while (flag) sleep` loop that BLOCKS a pool thread rather than waits on one
// (the latch in test_file_store_offload_thread) also fails (1): it is the thing
// being blocked, not a waiter.
//
// Applying this test took the tree from 37 sleep-poll loops to 15, of which one
// IS this definition. Note criterion (2) is about the deadline, not about where
// the predicate is written: two tests/log sites spelled it `while (now < deadline)
// { if (p) break; }` and read as fixed windows at a glance. They were not, and
// they migrated.

// ── Naming ───────────────────────────────────────────────────────────────────
//
// `pump_*` stays reserved for the drive-an-io_context family. The two are never
// interchangeable at a call site where only one can be correct -- driving a
// context that worker threads already run is UB, and polling a context nobody
// drives waits out the full budget for an event that can never arrive. A shared
// prefix would invite exactly that substitution, so this one does not take it.
//
// `_observed` rather than a bare `wait_until`, for two reasons: prose in these
// tests already uses "wait_until" for the PUMP pattern, and `std::condition_
// variable::wait_until` takes an absolute time point where this takes a
// duration. The name says what the caller gets -- the predicate was OBSERVED
// true -- and collides with neither.

#pragma once

#include <chrono>
#include <thread>

namespace fixpp::test_support {

// Default poll slice.
//
// CHOSEN, not inherited: the migrated sites used 1 ms, 2 ms and 5 ms, and #315
// records that nothing anywhere documented why any of them. 1 ms, because
//
//   (a) it is the FINEST value already in use, so the migration cannot lengthen
//       any existing site's detection latency. A migration that quietly slows a
//       wait is a regression nobody would ever notice;
//   (b) it matches `kPumpSlice`, so a reader carries one number, not two;
//   (c) what it trades away -- more wakeups while waiting -- is paid only on the
//       timeout path, i.e. only when a test is already failing.
//
// Unlike `kPumpSlice` this is NOT a cost floor. `run_for` on a context with
// outstanding work burns its whole slice however fast the work is; a sleeping
// thread that gets woken does not. Here the slice bounds only the OVERSHOOT
// past the moment the predicate became true (expected slice/2 on success).
inline constexpr auto kWaitSlice = std::chrono::milliseconds{1};

// Wait until `ready()` is true, or `budget` elapses. Returns whether it became
// true. `[[nodiscard]]`: dropping the result converts a lost wake into a silent
// pass, which is the failure this whole family exists to prevent.
//
// `budget` HAS NO DEFAULT, deliberately. Every site migrated onto this helper
// arrived with its own (2 s, 3 s, 4 s, 5 s); a default would have silently
// redefined each one, and shortening a budget is a flake while lengthening one
// delays every failure. Stating it at the call site keeps that visible.
//
// CONTRACT: `ready` is called from the WAITING thread while other threads run,
// so every shared access it makes must be race-free -- an atomic load, or a lock.
// A plain read of a non-atomic object written by another thread is a data race
// that TSan will report and that non-TSan builds may simply lose.
//
// Race-freedom is the requirement; ORDERING is a separate question the caller
// owns. A relaxed load is enough when the predicate only needs the flag itself
// (a counter reaching N, with a join afterwards to order everything else). Use
// acquire when returning true is meant to publish other writes to this thread --
// most `flag says the callback ran, now read what it wrote` sites need it.
//
// `ready` is evaluated exactly once per iteration and at least once overall, so
// a predicate that also captures what it observed (`[&]{ p = read(); return p != 0; }`)
// sees each observation exactly once. The deadline is tested AFTER the
// predicate, so an already-expired budget still gets one honest look.
//
// Prefer a condition_variable where the waiter and the writer are the same
// object: #317 replaced one instance of this shape with
// `CaptureTransport::await_test_req_ids` because its predicate rebuilt an
// O(corpus) structure per tick. This helper is for predicates with no writer to
// hook -- it cannot be notified, so it cannot beat a cv on either latency or
// cost.
template <class Ready>
[[nodiscard]] bool wait_until_observed(Ready ready, std::chrono::steady_clock::duration budget,
                              std::chrono::steady_clock::duration slice = kWaitSlice) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    for (;;) {
        if (ready()) return true;
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(slice);
    }
}

// Failure text for a wait that ran out of budget. Stream the site name after it.
//
// DELIBERATELY NOT `kPumpBudgetMiss`: the mechanism differs, and so does the
// thing to go looking at. A miss here means the executor never produced the
// event; a pump miss means THIS thread failed to drive a context that nobody
// else was driving.
inline constexpr const char* kWaitBudgetMiss =
    "#315: the self-driving executor did not produce the awaited event within the wait "
    "budget. Site: ";

}  // namespace fixpp::test_support
