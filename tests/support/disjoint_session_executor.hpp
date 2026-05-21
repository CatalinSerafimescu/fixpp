// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/support/disjoint_session_executor.hpp
//
// Test-only helper: build a `fixpp::core::session_executor` over a SEPARATE
// single-thread asio::io_context (RAII-managed), bound to the same Session*
// as the test's primary session_executor. Posting to the returned wrapper
// resumes a coroutine on a worker thread that is, by std::thread identity,
// disjoint from the primary executor's pool. Use this when a test needs to
// assert "trace_context (or any session-local state) survives a cross-thread
// coroutine resume" deterministically — without depending on asio's
// scheduling to deliver an observable thread hop.
//
// This is the Pattern B from the `fixpp::current_trace_context()` /
// `fixpp::core::make_session_executor()` docstrings, packaged as a
// test_support primitive. See book/concurrency_model.md §4 for the full
// production usage; this header is its test-author mirror.
//
// Lifetime: the side io_context owns a worker thread. Construct the helper
// AFTER the Session is open; destroy it BEFORE main-scope destructors
// (stop()+join() in the destructor) so asio's any_io_executor type-erasure
// shared-target ref-counts tear down on a quiescent thread. Pattern lifted
// from `tests/core/test_trace_context_resume.cpp`.
//
// Motivating issue: https://github.com/CatalinSerafimescu/fixpp/issues/79
#pragma once

#include <thread>
#include <utility>

#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>

#include <fixpp/core/error.hpp>
#include <fixpp/core/session_executor.hpp>
#include <fixpp/session/session.hpp>

namespace fixpp::test_support {

// RAII-owned disjoint session_executor over a private single-thread
// io_context. After successful construction:
//   • `executor()` returns the session_executor wrapper — co_await
//     asio::post(disjoint.executor(), asio::use_awaitable) to hop.
//   • The wrapper's underlying thread is `thread_id()` (for explicit
//     identity comparisons; never equal to any thread of the primary
//     session_executor).
//   • valid() returns true iff make_session_executor accepted the inputs.
class disjoint_session_executor {
public:
    explicit disjoint_session_executor(fixpp::session::Session& sess)
        : work_(asio::make_work_guard(ioc_)) {
        thread_ = std::thread([this] { ioc_.run(); });
        // direct_executor + attested: a single-thread io_context is
        // intrinsically serialized — the attestation path is the right
        // discriminator for the [2d §4.8] enforcement point. Multi-thread
        // foreign pools must use per_session_strand instead; see
        // book/concurrency_model.md §4.
        auto r = fixpp::core::make_session_executor(
            ioc_.get_executor(),
            fixpp::session::threading_mode::direct_executor,
            /*already_serialized_executor=*/true,
            &sess);
        if (r.has_value()) {
            se_    = *r;
            valid_ = true;
        }
    }

    disjoint_session_executor(const disjoint_session_executor&) = delete;
    disjoint_session_executor& operator=(const disjoint_session_executor&) = delete;
    disjoint_session_executor(disjoint_session_executor&&) = delete;
    disjoint_session_executor& operator=(disjoint_session_executor&&) = delete;

    ~disjoint_session_executor() {
        work_.reset();
        ioc_.stop();
        if (thread_.joinable()) thread_.join();
    }

    [[nodiscard]] const fixpp::core::session_executor& executor() const noexcept {
        return se_;
    }

    [[nodiscard]] std::thread::id thread_id() const noexcept {
        return thread_.get_id();
    }

    [[nodiscard]] bool valid() const noexcept { return valid_; }

private:
    asio::io_context                                  ioc_;
    asio::executor_work_guard<asio::io_context::executor_type> work_;
    std::thread                                       thread_;
    fixpp::core::session_executor                     se_{};
    bool                                              valid_{false};
};

}  // namespace fixpp::test_support
