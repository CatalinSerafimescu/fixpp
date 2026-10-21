// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/support/calling_thread_readings.hpp — test-only.
//
// What a call left on the coroutine thread that awaited it
// (.specify/536-537-538-recovery-liveness.md §5, the rows "a directly driven call" and
// "the calling thread after the call, both timings"; D-CAN-3). A cell spawns
// await_and_read_thread() bound to a cancellation signal of its own, emits on that
// signal where the cell names, and reads the result afterwards.
//
// The coroutine catches whatever the call throws and reports it as a value, so a call
// that throws fails an assertion of the cell and does not end the process.
#pragma once

#include <asio/awaitable.hpp>
#include <asio/cancellation_state.hpp>
#include <asio/cancellation_type.hpp>
#include <asio/error.hpp>
#include <asio/post.hpp>
#include <asio/system_error.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <exception>
#include <fixpp/core/error.hpp>
#include <functional>
#include <optional>
#include <string>
#include <utility>

namespace fixpp::test_support {

struct CallingThreadReadings {
    bool completed = false;  // the reading coroutine ran to its end
    // The call itself: it returned (`error` holds its error, when it has one), or threw.
    bool returned = false;
    std::optional<fixpp::core::error> error;
    bool threw = false;
    bool threw_operation_aborted = false;
    std::string thrown;  // the exception's text, for a failure message
    // The calling thread, read after the call:
    bool cancellation_recorded = false;  // a cancellation is recorded on it
    bool next_await_threw = false;       // its next co_await of an asio operation threw

    [[nodiscard]] bool returned_success() const noexcept { return returned && !error; }
    [[nodiscard]] bool returned_error(fixpp::core::error e) const noexcept {
        return returned && error == e;
    }
    // "returned <code>", "returned success", "threw <text>" or "did not complete".
    [[nodiscard]] std::string call_text() const {
        if (threw) return "threw " + thrown;
        if (!returned) return "did not complete";
        return error ? "returned error " + std::to_string(static_cast<int>(*error))
                     : std::string{"returned success"};
    }
};

// Awaits `make_call()` on a thread that accepts every cancellation type, records what
// the call did, runs `after_call` (a cell samples the session there, before any other
// handler), and then takes the two readings of the thread. Spawn it on the executor
// the call needs, bound to the cell's cancellation slot.
inline asio::awaitable<void> await_and_read_thread(
    std::function<asio::awaitable<fixpp::core::expected_t<void>>()> make_call,
    CallingThreadReadings& out, std::function<void()> after_call = {}) {
    co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
    try {
        auto r = co_await make_call();
        out.returned = true;
        if (!r) out.error = r.error();
    } catch (const asio::system_error& e) {
        out.threw = true;
        out.threw_operation_aborted = e.code() == asio::error::operation_aborted;
        out.thrown =
            out.threw_operation_aborted ? std::string{"operation_aborted"} : e.code().message();
    } catch (const std::exception& e) {
        out.threw = true;
        out.thrown = e.what();
    } catch (...) {
        out.threw = true;
        out.thrown = "an exception that is not a std::exception";
    }
    if (after_call) after_call();
    out.cancellation_recorded =
        (co_await asio::this_coro::cancellation_state).cancelled() != asio::cancellation_type::none;
    try {
        co_await asio::post(co_await asio::this_coro::executor, asio::use_awaitable);
    } catch (...) {
        out.next_await_threw = true;
    }
    out.completed = true;
}

}  // namespace fixpp::test_support
