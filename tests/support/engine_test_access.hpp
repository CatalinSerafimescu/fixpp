#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/support/engine_test_access.hpp — TEST-ONLY Engine hook setters.
//
// fixpp#511: `Engine` declares `friend struct engine_test_access;`
// unconditionally. Define this struct only in this header; a differing
// definition of this struct elsewhere in the program is an ODR violation
// (ill-formed, no diagnostic required).
// Add new setters here, never as members of `Engine` gated behind a test macro,
// which would make a test TU's class differ from the library's. The hooks
// themselves are plain members that stay null unless a test installs one.
// Never installed; production targets must not include this header.

#include <fixpp/session/engine.hpp>
#include <functional>
#include <utility>

namespace fixpp::session {

struct engine_test_access {
    // V-12 seam (contracts C-6): run_accept_loop co_awaits this on the session
    // strand between step 7 (attach_accepted_transport) and step 7a
    // (publish_entry). Install before start().
    static void set_pre_publish_hook(Engine& e, std::function<asio::awaitable<void>()> hook) {
        e.test_hook_pre_publish_ = std::move(hook);
    }

    // Post-drain seam: stop() co_awaits this on the control strand after the
    // send_counter_ drain completes and before the registry clear, so a test can
    // start a late Engine::send() in that window.
    static void set_post_send_drain_hook(Engine& e, std::function<asio::awaitable<void>()> hook) {
        e.test_hook_post_send_drain_ = std::move(hook);
    }
};

}  // namespace fixpp::session
