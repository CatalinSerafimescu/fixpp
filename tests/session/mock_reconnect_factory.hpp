// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/mock_reconnect_factory.hpp — test-only.
//
// MockReconnectFactory: the TransportFactory that lets a directly driven initiator's
// Session::drive_reconnect() reach its Logon emission with no socket. It was written in
// tests/session/test_refresh_on_logon.cpp (025) and is here so that a second binary can
// drive that entry point (.specify/536-537-538-recovery-liveness.md §5, the rows
// "D-OUT-5, the Logon root" and "D-CAN-1, the hydration flag").
//
// The including translation unit defines FIXPP_ALLOW_MOCK_TRANSPORT, in its source or
// through its target, before this header: mock_transport.hpp refuses to compile without
// it.
#pragma once

#include <asio/any_io_executor.hpp>
#include <atomic>
#include <fixpp/core/error.hpp>
#include <fixpp/transport/test/mock_transport.hpp>
#include <fixpp/transport/transport_factory.hpp>
#include <memory>
#include <memory_resource>
#include <utility>

namespace fixpp::test_support {

// ── MockReconnectFactory: TransportFactory returning mock_transport ────────────
//
// Used to drive session->drive_reconnect() in unit tests.
// make() returns a mock_transport with handshake_succeeds=true and empty inbound
// (so async_read_some immediately returns EOF after the 2nd Logon is emitted).
// cert_source_snapshot() returns nullptr (no cert rotation; step 2 safe with snap=null).
//
// last_transport: raw pointer to the most-recently created mock_transport. Remains
// valid as long as the session holds the transport (session outlives the test scope).
// Used by W5a to read outbound_bytes_seen() from the reconnect transport.

class MockReconnectFactory final : public fixpp::transport::TransportFactory {
public:
    std::atomic<int> make_call_count{0};
    // Raw pointer to the last transport created. Owned by the session.
    fixpp::transport::test::mock_transport* last_transport{nullptr};

    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<fixpp::transport::Transport>> make(
        asio::any_io_executor exec, fixpp::tls::SslCtxConfig /*ssl_cfg*/,
        std::pmr::memory_resource* /*mr*/) noexcept override {
        ++make_call_count;
        fixpp::transport::test::Script script;
        script.handshake_succeeds = true;
        // Empty inbound → immediate EOF after connect+handshake.
        auto t = std::make_unique<fixpp::transport::test::mock_transport>(std::move(exec),
                                                                          std::move(script));
        last_transport = t.get();
        return t;
    }

    [[nodiscard]] fixpp::core::expected_t<void> reload_credentials(
        std::shared_ptr<fixpp::tls::cert_source> /*new_source*/) noexcept override {
        return {};
    }

    [[nodiscard]] std::shared_ptr<fixpp::tls::cert_source> cert_source_snapshot()
        const noexcept override {
        return nullptr;
    }
};

}  // namespace fixpp::test_support
