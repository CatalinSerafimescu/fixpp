// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/transport/inflight_flag_guard.hpp — internal transport header.
//
// Sets a strand-confined in-flight boolean for the lifetime of the guard.
//
// WHY RAII RATHER THAN PLAIN ASSIGNMENT (#342, extended to read/write in #346).
//
// A set/clear pair written as two statements around a co_await:
//
//     flag = true;
//     ... co_await ...
//     flag = false;
//
// has TWO ways to leave the flag stuck true. (1) Any co_return between the set
// and the clear skips it -- async_connect / async_handshake have many (resolve
// failure, connect refused, timeout, cancelled, handshake failure, pinset
// rejection, ...). (2) A frame DESTROYED while suspended runs its in-scope
// destructors but never resumes the body, so no statement below the co_await
// ever executes; asio destroys a suspended awaitable frame under total
// cancellation. Reason (2) does not care how many suspension points or exit
// paths a coroutine has, which is why async_read_some / async_write are guarded
// here too even though each has exactly one of each.
//
// ⚠️ BE PRECISE ABOUT WHEN (2) HAPPENS -- an earlier draft of this header said
// "asio destroys a suspended awaitable frame under total cancellation", and that
// is FALSE. Cancellation RESUMES the frame with operation_aborted, on which a
// plain assignment below the co_await runs perfectly well. A frame is destroyed
// mid-body only when its handler chain is destroyed unrun (~awaitable_thread,
// i.e. io_context teardown). No caller-reachable path is known that destroys a
// read/write frame while the Transport SURVIVES to exhibit the stuck flag, so
// reason (2) is why this shape is correct, not evidence of a live wedge --
// see the #346 note in tests/transport/test_inflight_exclusivity.cpp.
//
// A stuck flag does not fail loudly. It wedges the Transport permanently:
// transport_already_connected (97) for connect/handshake,
// transport_read_in_progress / transport_write_in_progress (99/100) for
// read/write. Every later operation is refused and the socket is never retried.
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
