// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/support/identity_injecting_transport.hpp
//
// 015 T021 [US4] — live-identity injection for CompID binding-logic tests.
//
// Replaces the removed SessionConfig per-config peer-identity test seam
// (SC-006 / FR-009): instead of injecting a peer_identity through config, these
// helpers drive the identity through the PRODUCTION live-handshake path —
// Session::attach_accepted_transport stores it as live_peer_id_, exactly as the
// engine accept loop does after a real TLS handshake (data-model §E-2/E-4).
//
// Usage (AFTER Session::open() has bound the session executor):
//     fixpp::test_support::inject_live_identity(sess, make_peer_identity(dn));
//     // ... then feed the peer Logon / Logon-ack ...
//
// The authorize() gate is role-symmetric (the same arm-(1-live) fires for
// acceptor and initiator), so this single primitive serves both roles' cells.
#pragma once

#include <asio/awaitable.hpp>
#include <cstddef>
#include <fixpp/core/error.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/tls/peer_identity.hpp>
#include <fixpp/transport/endpoint.hpp>
#include <fixpp/transport/tls_transport.hpp>  // handshake_result
#include <fixpp/transport/transport.hpp>
#include <memory>
#include <span>
#include <utility>

namespace fixpp::test_support {

// Minimal owned Transport for attach_accepted_transport: async_write swallows
// bytes (the acceptor reply-Logon sink), async_read_some reports EOF, the rest
// are no-ops. Never actually connects — attach only needs an owned Transport
// object to take ownership of and rebind transport_send_ against.
class NullSinkTransport final : public fixpp::transport::Transport {
public:
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<fixpp::transport::ConnectInfo>>
    async_connect(fixpp::transport::Endpoint const& ep) override {
        fixpp::transport::ConnectInfo info;
        info.remote = ep;
        info.local = fixpp::transport::Endpoint{"127.0.0.1", 0};
        info.family = 2;
        co_return info;
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<std::size_t>> async_read_some(
        std::span<std::byte> buf [[clang::lifetimebound]]) override {
        (void)buf;
        co_return std::unexpected{fixpp::core::error::transport_read_eof};
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<std::size_t>> async_write(
        std::span<const std::byte> buf [[clang::lifetimebound]]) override {
        co_return buf.size();
    }

    [[nodiscard]] fixpp::core::expected_t<void> cancel() noexcept override { return {}; }
    [[nodiscard]] fixpp::core::expected_t<void> close() noexcept override { return {}; }
};

// Inject `pid` as the session's live handshake identity through the production
// acceptor attach primitive (sets live_peer_id_ + rebinds transport_send_; does
// NOT advance the FSM). PRECONDITION: Session::open() already called (exec_
// bound). Mirrors the engine accept loop's attach-before-first-frame
// happens-before (Gate A New-1 / E-4); the gate consumes live_peer_id_ one-shot
// when the next inbound Logon / Logon-ack is processed.
inline void inject_live_identity(fixpp::session::Session& s, fixpp::tls::peer_identity pid) {
    fixpp::transport::handshake_result hr{};
    hr.peer_id = std::move(pid);
    s.attach_accepted_transport(std::make_unique<NullSinkTransport>(), std::move(hr));
}

}  // namespace fixpp::test_support
