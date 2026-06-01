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
//
// STRING LIFETIME DISCIPLINE (013 T039 / data-model §E-5):
// session_event_tls_validation_failed carries three string_view fields
// (sub_reason, peer_endpoint, reason_string). These views MUST point into
// storage whose lifetime is ≥ the event's ring slot lifetime. ListenerEvents
// maintains three parallel owning-string rings (sub_reason_store_,
// peer_ep_store_, reason_str_store_) indexed by the same write_idx_ % capacity
// as the event ring. emit_with_strings() writes the owned copies BEFORE writing
// the event slot so the views are valid from the moment the event is readable.
// A slot's strings are overwritten when the slot is recycled (ring wrap) —
// exactly mirroring the event's own overwrite semantics.
//
// Capacity: kSessionEventRingCapacity = 16 (same constant as Session's ring,
// from session_event.hpp). [FR-035 v1.0 contract]
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>

#include "fixpp/core/error.hpp"             // fixpp::core::error (master enum)
#include "fixpp/session/session_event.hpp"  // SessionEvent + kSessionEventRingCapacity

namespace fixpp::transport {

// ListenerEvents — fixed-capacity ring buffer for SessionEvent values emitted
// by the per-listener acceptor path (pre-Session TLS validation events).
// NOT thread-safe: all emits and reads MUST occur on the listener strand
// (single-writer invariant per [const §XI.4] pattern). [data-model §E-6]
class ListenerEvents {
public:
    ListenerEvents() noexcept = default;

    ListenerEvents(ListenerEvents const&) = default;
    ListenerEvents& operator=(ListenerEvents const&) = default;
    ListenerEvents(ListenerEvents&&) = default;
    ListenerEvents& operator=(ListenerEvents&&) = default;
    ~ListenerEvents() = default;

    // Emit a generic SessionEvent into the ring. Overwrites the oldest entry
    // when full (ring-wrap). Called from the listener strand only.
    // NOTE: prefer emit_with_strings() for events carrying string_view fields
    // to ensure proper string lifetime. [data-model §E-5]
    void emit(session::SessionEvent ev) noexcept {
        events_[write_idx_++ % session::kSessionEventRingCapacity] = std::move(ev);
        if (count_ < session::kSessionEventRingCapacity) {
            ++count_;
        }
    }

    // Emit a session_event_tls_validation_failed with owning-string copies.
    //
    // The three string_view fields in session_event_tls_validation_failed point
    // into the parallel string arrays (sub_reason_store_, peer_ep_store_,
    // reason_str_store_). The strings are written BEFORE the event slot so the
    // views are valid from the instant the event is readable.
    //
    // Lifetime: the views remain valid until the slot is overwritten on the next
    // ring wrap — exactly mirroring the event-ring overwrite semantics.
    // [013 T039 / data-model §E-5 sub_reason capture semantics]
    void emit_with_strings(session::session_event_tls_validation_failed /*unused tag*/,
                           fixpp::core::error code, std::string const& sub_reason,
                           std::string const& peer_endpoint,
                           std::string const& reason_string) noexcept {
        const std::size_t slot = write_idx_ % session::kSessionEventRingCapacity;

        // Write owning copies first so the views are stable before the event
        // is inserted into the ring. noexcept: std::string assignment does not
        // throw (move from a temporary string literals; both lhs and rhs are
        // in a valid state after any exception — and the array pre-allocates
        // the default-initialised std::string objects).
        try {
            sub_reason_store_[slot] = sub_reason;
            peer_ep_store_[slot] = peer_endpoint;
            reason_str_store_[slot] = reason_string;
        } catch (...) {
            // Allocation failure: emit with empty strings rather than omitting
            // the event entirely (the code is still diagnostic). The views will
            // point into the now-cleared or partially-updated strings.
            sub_reason_store_[slot].clear();
            peer_ep_store_[slot].clear();
            reason_str_store_[slot].clear();
        }

        session::session_event_tls_validation_failed ev{};
        ev.code = code;
        ev.sub_reason = std::string_view{sub_reason_store_[slot]};
        ev.peer_endpoint = std::string_view{peer_ep_store_[slot]};
        ev.reason_string = std::string_view{reason_str_store_[slot]};

        events_[slot] = std::move(ev);
        ++write_idx_;
        if (count_ < session::kSessionEventRingCapacity) {
            ++count_;
        }
    }

    // Membership-witness view over the most recent ≤16 emitted events
    // (physical-buffer order; NOT chronologically meaningful). [FR-035]
    [[nodiscard]] std::span<const session::SessionEvent> recent_events() const noexcept {
        return std::span<const session::SessionEvent>{
            events_.data(), std::min(count_, session::kSessionEventRingCapacity)};
    }

private:
    // Event ring (capacity = kSessionEventRingCapacity = 16). [FR-035]
    std::array<session::SessionEvent, session::kSessionEventRingCapacity> events_{};

    // Parallel owning-string rings for session_event_tls_validation_failed
    // fields. Same index as events_ ring; same overwrite semantics. [data-model §E-5]
    std::array<std::string, session::kSessionEventRingCapacity> sub_reason_store_;
    std::array<std::string, session::kSessionEventRingCapacity> peer_ep_store_;
    std::array<std::string, session::kSessionEventRingCapacity> reason_str_store_;

    std::size_t count_ = 0;
    std::size_t write_idx_ = 0;
};

}  // namespace fixpp::transport
