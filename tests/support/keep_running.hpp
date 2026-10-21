// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/support/keep_running.hpp — test-only.
//
// keep_running_for: a work guard that keeps an io_context the test thread pumps from
// stopping between two pumps.
//
// Why it is needed. A FileStore call's offload completes through a work guard on the
// caller's io_context that asio's co_spawn keeps in the offload's frame
// (asio/impl/co_spawn.hpp, co_spawn_state::handler_work), and that frame is destroyed on
// the pool thread after the completion has been posted. When that guard is the io_context's
// last outstanding work, releasing it stops the io_context (asio's scheduler::work_finished),
// from the pool thread, possibly after the test thread's pump has returned and restarted it.
// The next pump then runs nothing: pump_until spins out its budget, and
// run_window_then_ready's window returns at once. An io_context is exposed when work tracked
// on it can be released on another thread while it holds no further outstanding work
// between two pumps: no engine socket, no parked timer.
//
// What it costs and what it forbids, while the guard is held:
//   - a run_for window runs its whole length, where it used to return when the io_context
//     ran out of work;
//   - cancel_and_drain_or_report (pump_until_ready.hpp) and anything else that waits for
//     ioc.stopped(), or calls ioc.run() to the end, cannot finish. Release the guard (its
//     reset()) before any of them.
// Declare it after the io_context, so it is destroyed first.
#pragma once

#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>

namespace fixpp::test_support {

using keep_running = asio::executor_work_guard<asio::io_context::executor_type>;

[[nodiscard]] inline keep_running keep_running_for(asio::io_context& ioc) {
    return asio::make_work_guard(ioc);
}

}  // namespace fixpp::test_support
