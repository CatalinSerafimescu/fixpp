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
// asio_listener::reload_credentials (the acceptor-half symmetric method per
// FR-030 + [[feedback_half_restructure_symmetric_api]]) is documented at
// include/fixpp/transport/listener.hpp shipped form post-/speckit-implement.

#pragma once

#include <chrono>
#include <memory>

#include <asio/awaitable.hpp>

#include "fixpp/core/error.hpp"
#include "fixpp/core/expected.hpp"
#include "fixpp/tls/cert_source.hpp"

namespace fixpp::session {

class Session {
public:
    // ... existing 005/009/010 methods ...
    // (open / send / close / fromApp / fromAdmin / etc. — see
    //  include/fixpp/session/session.hpp)

    // FR-030 / FR-031 / FR-032 / FR-033 / D-11 — in-process credential rotation.
    // Atomically swaps the underlying cert_source slot on the held
    // TransportFactory via std::atomic<std::shared_ptr<cert_source>>::store(...)
    // — O(1), strand-free, race-free. The in-flight handshake (if any) observes
    // the OLD source; the NEXT transport_factory::make(...) call observes the
    // NEW. NO mid-handshake SSL_CTX mutation (would invoke OpenSSL UB per OpenSSL
    // 3.x SSL_CTX lifetime documentation). Worst-case rotation latency = one
    // handshake (~50–500 ms typical for TLS 1.3 1-RTT).
    //
    // Returns expected_t::ok() on swap-accepted. Rejects nullptr (returns
    // error::session_invalid_argument — uses existing slot, NOT in 013's 5-new-slot
    // budget). Emits SessionEvent::credentials_rotated{old_sha256, new_sha256}
    // BEFORE the first handshake on the rotated source (per D-12 / FR-032 — not
    // at the reload_credentials call-site).
    //
    // Symmetric with asio_listener::reload_credentials per FR-030 acceptor half
    // + [[feedback_half_restructure_symmetric_api]].
    [[nodiscard]] expected_t<void>
    reload_credentials(std::shared_ptr<fixpp::tls::cert_source> new_source) noexcept;

    // FR-008 / US1 AC5 / D-13 — initiator-graceful Logout. Emits Logout(5),
    // awaits peer reply for `timeout`, closes Transport, transitions to
    // Disconnected. Surfaces error::session_logout_disconnect_timeout (slot 118)
    // if elapsed before peer reply. Symmetric on acceptor side (acceptor
    // receives Logout from peer; same code path completes with the same
    // timeout).
    //
    // Default timeout = SessionConfig::logout_disconnect_timeout_ms (2000 ms
    // per Clarifications Q5=A).
    [[nodiscard]] asio::awaitable<expected_t<void>>
    logout(std::chrono::milliseconds timeout) noexcept;

    // Convenience overload — uses SessionConfig::logout_disconnect_timeout_ms.
    [[nodiscard]] asio::awaitable<expected_t<void>>
    logout() noexcept;
};

}  // namespace fixpp::session
