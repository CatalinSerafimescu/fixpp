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
// ⚠️ THE FLAG LIVES IN timer_epoch_state, NOT IN THE TRANSPORT — and that is
// load-bearing, not tidiness. The first version of this guard bound a
// `bool&` to a Transport member and this comment argued the point away:
// "that is not a new lifetime hazard: the surrounding coroutine body already
// dereferences `this` at every step, so a frame resumed or destroyed after the
// Transport died is UB with or without this guard."
//
// That argument is FALSE, and ASan says so. It conflates RESUME with DESTROY.
// A resumed frame does dereference `this`. A frame merely DESTROYED at a
// suspension point runs only its in-scope destructors — and before this guard
// existed there were none that touched `this`: `resolver` and `steady_timer`
// hold executor copies, not the Transport. This destructor was the first, and
// under D-4.0 destroy-with-no-drain (the Transport destroyed synchronously on
// the failure arm, its frame destroyed later) it produced a measured
// heap-use-after-free — WRITE of size 1, reported at this destructor.
//
// That is the SAME hazard timer_epoch_state was built for, one step over: the
// timer handlers there capture a COPY of the shared_ptr precisely so a stranded
// handler never reads through a dangling `this`. Holding a copy of that block
// here makes the clear safe whether or not the Transport is still alive.

#pragma once

#include <memory>
#include <utility>

#include "timer_epoch_state.hpp"

namespace fixpp::transport::detail {

class inflight_flag_guard {
public:
    // Takes a COPY of the shared block, so the block outlives the Transport and
    // the clear in ~inflight_flag_guard is safe even when the frame is
    // destroyed after the Transport (D-4.0). Never bind this to a Transport
    // member -- that is the bug this shape exists to prevent, and it was a
    // measured heap-use-after-free, not a theoretical one.
    inflight_flag_guard(std::shared_ptr<timer_epoch_state> block,
                        bool timer_epoch_state::* field) noexcept
        : block_(std::move(block)), field_(field) {
        (*block_).*field_ = true;
    }
    ~inflight_flag_guard() { (*block_).*field_ = false; }

    inflight_flag_guard(inflight_flag_guard const&) = delete;
    inflight_flag_guard& operator=(inflight_flag_guard const&) = delete;

private:
    std::shared_ptr<timer_epoch_state> block_;
    bool timer_epoch_state::* field_;
};

}  // namespace fixpp::transport::detail
