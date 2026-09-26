#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef FIXPP_TEST_HOOKS
#error \
    "capi_drain_support.hpp reads Session::is_drained_for_test, which exists only under FIXPP_TEST_HOOKS; include it only from a target that defines the macro (issue #511)"
#endif

#include <asio/co_spawn.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <future>
#include <memory>
#include <thread>

#include "capi_internal.hpp"
#include "fix/c_api/engine.h"
#include "fixpp/session/session.hpp"
#include "fixpp/session/session_config.hpp"

namespace fixpp::capi_test {

// Issue #151: poll the engine's RETAINED Session (a reaped session stays in lookup)
// until it reaches lifecycle::closed_drained — the deterministic signal that
// Session::close will return session_already_closed. is_established/onLogout fires on
// the Active→!Active edge BEFORE closed_drained, so a fixed sleep races the `closing`
// window; this waits on the real terminal state instead. Self-bounded by `deadline`.
//
// state_ is a plain enum, SINGLE-WRITER on the session strand (the worker mutates it
// in Session::close), so it must be READ ON THAT STRAND — an off-strand read is a data
// race. The read is hopped onto sess->executor() (the same strand the worker uses) and
// waited on via use_future no later than `deadline`; the test thread never touches
// state_. The posted read holds only a weak_ptr, so a read abandoned at the deadline
// does not keep the Session alive past its engine.
inline bool wait_for_acceptor_drained(
    fixpp_engine_t* engine, const fixpp::session::SessionId& id,
    std::chrono::milliseconds deadline = std::chrono::milliseconds{5000}) {
    auto* e = reinterpret_cast<fixpp_engine*>(engine);
    const auto until = std::chrono::steady_clock::now() + deadline;
    for (;;) {
        if (e->state_ != nullptr && e->state_->engine_.has_value()) {
            std::shared_ptr<fixpp::session::Session> sess = e->state_->engine_->lookup(id);
            if (sess != nullptr) {
                std::weak_ptr<fixpp::session::Session> const weak = sess;
                auto fut = asio::co_spawn(
                    sess->executor().underlying(),
                    [weak]() -> asio::awaitable<bool> {
                        if (auto s = weak.lock()) {
                            co_return s->is_drained_for_test();
                        }
                        co_return false;
                    },
                    asio::use_future);
                if (fut.wait_until(until) != std::future_status::ready) return false;
                if (fut.get()) return true;
            }
        }
        if (std::chrono::steady_clock::now() >= until) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }
}

}  // namespace fixpp::capi_test
