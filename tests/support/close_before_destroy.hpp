// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/support/close_before_destroy.hpp — test-only.
//
// 105-transport-liveness (contract C-9): a Session is not destroyed while its ending Logout's
// bound runs; ~Session asserts the bound's join counter is zero, and close() joins the bound.
// A cell that drives a Session directly and can leave an ending in progress declares one of
// these right after the Session, so it is destroyed first: it starts a terminal close (W-14:
// it ends the wait at once) and polls `ioc` until that close returns or `budget` passes.
// It reports nothing, since a destructor's report could throw under --gtest_throw_on_failure;
// a close that does not return leaves the Session's own assertion to report it.
#pragma once

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <fixpp/session/session.hpp>
#include <future>

#include "session_executor_of.hpp"

namespace fixpp::test_support {

class close_before_destroy {
public:
    close_before_destroy(fixpp::session::Session& s, asio::io_context& ioc,
                         std::chrono::milliseconds budget = std::chrono::seconds{30}) noexcept
        : s_{s}, ioc_{ioc}, budget_{budget} {}
    close_before_destroy(close_before_destroy const&) = delete;
    close_before_destroy& operator=(close_before_destroy const&) = delete;
    ~close_before_destroy() {
        try {
            auto fut =
                asio::co_spawn(session_executor_of(s_, ioc_),
                               s_.close(fixpp::session::close_mode::terminal), asio::use_future);
            auto const end = std::chrono::steady_clock::now() + budget_;
            while (fut.wait_for(std::chrono::seconds{0}) != std::future_status::ready &&
                   std::chrono::steady_clock::now() < end) {
                ioc_.restart();
                ioc_.poll();
            }
        } catch (...) {  // NOLINT(bugprone-empty-catch): a destructor reports nothing
        }
    }

private:
    fixpp::session::Session& s_;
    asio::io_context& ioc_;
    std::chrono::milliseconds budget_;
};

}  // namespace fixpp::test_support
