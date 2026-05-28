// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/transport/listener_events.hpp
//
// fixpp::transport::ListenerEvents — per-listener SessionEvent ring for
// pre-Session TLS events (FR-026..FR-029). 013 T013a part (b).
// Anchors: FR-026, FR-027, FR-028, FR-035; data-model §E-5/§E-6;
// contracts/session_event.hpp.
//
// LIFETIME INVARIANT: ListenerEvents MUST outlive every transient pre-Session
// `verify_peer` callback. In production: owned by `asio_listener::Config`
// (parallel to `accepted_transport_config`), whose lifetime spans the Listener.
// The public address is published to the OpenSSL verify_peer_trampoline via
// SSL_set_ex_data at `async_accept`-time — but the wiring lives in Phase 5
// T039; for Phase 2 only the type + CMake registration is in scope.
//
// Capacity: kSessionEventRingCapacity = 16 (same constant as Session's ring,
// from session_event.hpp). [FR-035 v1.0 contract]
#pragma once

#include <algorithm>
#include <cstddef>
#include <span>

#include "fixpp/session/session_event.hpp"  // SessionEvent + kSessionEventRingCapacity

namespace fixpp::transport {

// ListenerEvents — fixed-capacity ring buffer for SessionEvent values emitted
// by the per-listener acceptor path (pre-Session TLS validation events).
// NOT thread-safe: all emits and reads MUST occur on the listener strand
// (single-writer invariant per [const §XI.4] pattern). [data-model §E-6]
class ListenerEvents {
public:
    ListenerEvents() noexcept = default;

    ListenerEvents(ListenerEvents const&)            = default;
    ListenerEvents& operator=(ListenerEvents const&) = default;
    ListenerEvents(ListenerEvents&&)                 = default;
    ListenerEvents& operator=(ListenerEvents&&)      = default;
    ~ListenerEvents()                                = default;

    // Emit a SessionEvent into the ring. Overwrites the oldest entry when
    // full (ring-wrap). Called from the listener strand only.
    void emit(session::SessionEvent ev) noexcept {
        events_[write_idx_++ % session::kSessionEventRingCapacity] = std::move(ev);
        if (count_ < session::kSessionEventRingCapacity) {
            ++count_;
        }
    }

    // Membership-witness view over the most recent ≤16 emitted events
    // (physical-buffer order; NOT chronologically meaningful). [FR-035]
    [[nodiscard]] std::span<const session::SessionEvent>
    recent_events() const noexcept {
        return std::span<const session::SessionEvent>{
            events_.data(),
            std::min(count_, session::kSessionEventRingCapacity)};
    }

private:
    std::array<session::SessionEvent, session::kSessionEventRingCapacity> events_{};
    std::size_t count_     = 0;
    std::size_t write_idx_ = 0;
};

}  // namespace fixpp::transport
