// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/transport/inflight_flag_guard.hpp — internal transport header.
//
// Sets a strand-confined in-flight boolean for the lifetime of the guard.
//
// WHY THIS EXISTS RATHER THAN THE LOCAL IDIOM (#342). async_read_some and
// async_write set and clear their flags by plain assignment:
//
//     read_in_flight_ = true;
//     ... single co_await ...
//     read_in_flight_ = false;
//
// That is safe THERE because those coroutines have exactly ONE suspension
// point and every exit after the set is a plain co_return below the clear.
// async_connect / async_handshake do not have that shape: between the set and
// the natural clear there are many co_return paths (resolve failure, connect
// refused, timeout, cancelled, handshake failure, pinset rejection, ...). A
// missed clear does not fail loudly — it wedges the Transport into permanent
// transport_already_connected, so every later connect/handshake is refused and
// the socket is never retried. Hence RAII: the destructor runs on every
// co_return AND on frame destruction (asio destroys a suspended awaitable
// frame under total cancellation, which runs in-scope locals' destructors).
//
// ⚠️ SCOPE, stated because the argument above proves more than this header
// fixes. Reason (2) -- a frame destroyed while suspended runs in-scope
// destructors but never resumes the body -- applies WORD FOR WORD to
// read_in_flight_ / write_in_flight_, which remain plain-assignment. A frame
// torn down while suspended in async_read_some / async_write therefore leaves
// its flag stuck true and wedges the Transport into permanent
// transport_read_in_progress / transport_write_in_progress, the exact symmetry
// of the 97 wedge this guard exists to prevent. Those two are NOT converted
// here: each has a single call site whose only cross-coroutine reader is
// close(), and converting them would ship with the same witness gap #339 hit
// trying to drive "frame destroyed while suspended" from the public surface.
// Known and deferred, not overlooked -- see the follow-up issue.
//
// The guard writes through the enclosing Transport's `this`. That is not a new
// lifetime hazard: the surrounding coroutine body already dereferences `this`
// (socket_, state_) at every step, so a frame resumed or destroyed after the
// Transport died is UB with or without this guard. The separately-owned
// timer_epoch_state block exists for the DIFFERENT case of a queued timer
// HANDLER outliving `this` (D-4.1); a coroutine frame is not that case.

#pragma once

namespace fixpp::transport::detail {

class inflight_flag_guard {
public:
    explicit inflight_flag_guard(bool& flag) noexcept : flag_(flag) { flag_ = true; }
    ~inflight_flag_guard() { flag_ = false; }

    inflight_flag_guard(inflight_flag_guard const&) = delete;
    inflight_flag_guard& operator=(inflight_flag_guard const&) = delete;

private:
    bool& flag_;
};

}  // namespace fixpp::transport::detail
