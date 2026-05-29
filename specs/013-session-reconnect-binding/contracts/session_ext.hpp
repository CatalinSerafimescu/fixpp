// SPDX-License-Identifier: AGPL-3.0-or-later
//
// fixpp — 013-session-reconnect-binding
// Contract: Session method extensions (reload_credentials + logout(timeout)).
// Anchors: FR-008, FR-030..FR-033, D-11..D-13; [2j §3.12] IN-PROCESS variant;
// Clarifications Q4=A, Q5=A.
//
// NOTE: This is the Phase-1 contract documenting the Session DELTA. The
// shipped form lands at include/fixpp/session/session.hpp post-/speckit-implement
// — the methods below are APPENDED to 005/009/010's shipped Session class
// (preserves operator API + ABI).
//
// Acceptor-half rotation: BOTH initiator and acceptor halves route through
// the SAME `TransportFactory::reload_credentials(new_source)` call (per
// `[[feedback_half_restructure_symmetric_api]]`) — see contracts/data-model.md
// E-7 + include/fixpp/transport/transport_factory.hpp post-/speckit-implement.
// The acceptor side does NOT need a Listener::reload_credentials method;
// the abstract `Listener` pure-virtual cap stays at 1 (`async_accept` only).

#pragma once

#include <chrono>
#include <memory>
#include <span>

#include <asio/awaitable.hpp>

#include "fixpp/core/error.hpp"
#include "fixpp/core/expected.hpp"
#include "fixpp/session/session_event.hpp"
#include "fixpp/tls/cert_source.hpp"

namespace fixpp::session {

class Session {
public:
    // ... existing 005/009/010 methods ...
    // (open / send / close / fromApp / fromAdmin / etc. — see
    //  include/fixpp/session/session.hpp)
    //
    // 010 F-04 — UNCHANGED — complementary FSM-state observation accessor:
    //   std::span<const fsm_state> fsm_visit_history() const noexcept;

    // FR-030 / FR-031 / FR-032 / FR-033 / D-11 — operator-facing forwarder for
    // in-process credential rotation. Delegates to the held
    // TransportFactory*::reload_credentials(new_source) which performs the
    // atomic store on the factory-internal
    //   cert_source_slot_: std::atomic<std::shared_ptr<fixpp::tls::cert_source>>
    // — O(1), strand-free, race-free. Both initiator-side and acceptor-side
    // rotation route through the SAME factory call per
    // [[feedback_half_restructure_symmetric_api]] — the factory IS the symmetric
    // authority (the acceptor side does NOT need a separate Listener method).
    //
    // The in-flight handshake (if any) observes the OLD source because the FSM
    // captured a std::shared_ptr<cert_source> BY VALUE COPY via
    // factory_->cert_source_snapshot() before calling make(...); the captured
    // strong-ref keeps the OLD cert_source alive past the store. The NEXT
    // drive_reconnect_attempt reads the NEW snapshot. NO mid-handshake SSL_CTX
    // mutation (would invoke OpenSSL UB per OpenSSL 3.x SSL_CTX lifetime
    // documentation). Worst-case rotation latency = one in-flight HANDSHAKE
    // duration (~50–500 ms typical for TLS 1.3 1-RTT).
    //
    // Returns expected_t::ok() on swap-accepted. Rejects nullptr (returns
    // error::session_invalid_argument — slot 119 per contracts/session_errors.hpp;
    // renumbered from 121 to 119 per /speckit-analyze F1/D1 2026-05-28; added
    // 2026-05-28 per /speckit-analyze C3 resolution because no existing
    // session_invalid_* slot fits the runtime-API-argument-validation semantic).
    // Emits
    // SessionEvent::credentials_rotated{old_sha256, new_sha256} BEFORE the
    // first handshake on the rotated source (per D-12 / FR-032 — not at the
    // reload_credentials call-site).
    [[nodiscard]] expected_t<void>
    reload_credentials(std::shared_ptr<fixpp::tls::cert_source> new_source) noexcept;

    // FR-008 / US1 AC5 / D-13 — initiator-graceful Logout. Emits Logout(5),
    // awaits peer reply for `timeout`, closes Transport, transitions to
    // Disconnected. Surfaces error::session_logout_timeout (slot 73, 005-era
    // reused per F1/D1 2026-05-28) if elapsed before peer reply. Symmetric on
    // acceptor side (acceptor receives Logout from peer; same code path
    // completes with the same timeout).
    //
    // Default timeout = SessionConfig::logout_disconnect_timeout_ms (2000 ms
    // per Clarifications Q5=A).
    [[nodiscard]] asio::awaitable<expected_t<void>>
    logout(std::chrono::milliseconds timeout) noexcept;

    // Convenience overload — uses SessionConfig::logout_disconnect_timeout_ms.
    [[nodiscard]] asio::awaitable<expected_t<void>>
    logout() noexcept;

    // FR-035 — NEW 013-introduced ring-buffer accessor for SessionEvent
    // observability. Returns a std::span view over the underlying fixed-
    // capacity ring of SessionEvent values (capacity ≤ 16; physical-buffer
    // order; membership-witness semantics per 010 F-04 contract — NOT
    // chronologically ordered; consumers assert via Contains / std::find /
    // std::visit-then-match).
    //
    // DISTINCT from the 010 F-04 fsm_visit_history() accessor:
    //   - fsm_visit_history() -> std::span<const fsm_state>     (FSM-state observation)
    //   - recent_events()      -> std::span<const SessionEvent>  (variant events)
    //
    // Both accessors coexist; neither replaces the other.
    [[nodiscard]] std::span<const SessionEvent> recent_events() const noexcept;
};

}  // namespace fixpp::session
