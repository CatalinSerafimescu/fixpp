// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/transport/timer_epoch_state.hpp — internal transport header.
//
// The shared timer-epoch counter block used by both concrete transports to
// close the late-timer-handler defect class (088-firstframe-budget-timer-
// lifetime, FR-014 / FR-009; research.md D-4.1).
//
// The defect: a timer whose handler cancels a socket can already be QUEUED AND
// READY when the operation it guarded has succeeded. `timer.cancel()` cannot
// un-queue an already-completed handler, so a late cancel lands on a socket
// that worked — and, if the transport itself is gone, through a dangling
// `this`.
//
// The mechanism: each arm site takes an epoch (`++`), and the handler captures
// a COPY OF THE shared_ptr by value and compares. A retired epoch (bumped at
// the in-function retire point, and again in the destructor BODY) makes the
// handler a no-op. The state must live in a separately-owned block precisely
// so it outlives the transport — a plain member would be read through the
// dangling `this` that is the defect being fixed.
//
// ONE type, not one per transport. The plain transport uses `connect` only and
// carries 8 unused bytes; that is what data-model.md §4 and research.md D-4.1
// specify ("handshake — TLS only; unused on the plain transport"). Two
// same-named-but-differently-shaped nested types were delivered first, then
// unified here at /simplify: the namespace-scope collision that justified
// nesting existed ONLY because the type had been split, and both headers are
// included together in transport_factory.cpp / engine.cpp / asio_listener.cpp.
//
// TWO COUNTERS, not one — and this is the part D-4.1 actually argues, about
// FIELDS rather than types. `async_connect` and `async_handshake` are strictly
// ordered by every current caller (reconnect_fsm.cpp arms connect then
// handshake; the accept path calls handshake only), and reconnect_fsm mints a
// fresh transport per attempt — so a single shared counter WOULD be correct
// against today's call graph. It is still split, because that correctness
// rests on a sequencing property of the *callers* which nothing in the
// transport enforces: a future interleaving, or a reconnect path that reused a
// transport, would silently reintroduce the exact stale-handler-cancels-a-
// live-op defect this closes. Eight bytes buys removing an argument rather
// than adding one.
//
// Internal header: NOT under include/fixpp/ (not part of the public API), so
// SC-010/SC-017 hold by construction — `src/` is not an installed include
// root. Do NOT include from public headers or library consumers.
//
// Anchors: research.md D-4.0/D-4.1; data-model.md §4; spec.md FR-009/FR-014.
#pragma once

#include <cstdint>

namespace fixpp::transport {

// Strand-confined plain integers, no atomics: every arm and retire runs on the
// transport's own executor, and the accessor is read only after the context has
// been driven to completion. Should a future consumer arm or retire from a
// second thread, these must become atomics at that point.
struct timer_epoch_state {
    std::uint64_t connect{0};
    std::uint64_t handshake{0};  // TLS only; unused on the plain transport.

    // ── In-flight flags (#342), here for the SAME lifetime reason as the
    // epochs, established by MEASUREMENT not by argument. They were first
    // written as plain Transport members cleared by an RAII guard in
    // async_connect / async_handshake; ASan then reported a
    // heap-use-after-free, WRITE of size 1, in that guard's destructor under
    // D-4.0 destroy-with-no-drain. A suspended coroutine frame is destroyed
    // AFTER the Transport on that path, and a destructor that writes through
    // `this` is exactly what the rest of this file exists to avoid — the timer
    // handlers capture a COPY of the shared_ptr for the same reason. Living
    // here, the clear is safe whether or not the Transport is still alive.
    bool connect_in_flight{false};
    bool handshake_in_flight{false};  // TLS only.
};

}  // namespace fixpp::transport
