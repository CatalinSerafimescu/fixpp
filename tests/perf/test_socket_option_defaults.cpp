// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// tests/perf/test_socket_option_defaults.cpp
// T019 — FR-029 / FR-029a — socket option defaults on initiator + acceptor sockets.
//
// Verifies that after async_connect completes:
//   - TCP_NODELAY == 1  (Nagle OFF per Clarifications Q5=A).
//   - SO_LINGER  disabled (linger disabled per Clarifications Q5=A).
//
// Split per T019 description:
//   Cell 1 (initiator leg) — asio_tls_transport over loopback. Wired by T029.
//   Cell 2 (acceptor leg)  — asio_listener-accepted socket. Depends on US3 T037;
//                            wired by T038. Marked TODO here.
//
// Run:
//   ctest --test-dir build/linux-clang-debug -R perf_socket_option_defaults -V
//
// Phase 3a — DISABLED cells require asio_tls_transport (T026). Compile-time
// default-value assertions run NOW (from Transport::Config).

#include <gtest/gtest.h>

#include <fixpp/transport/transport.hpp>
#include <fixpp/transport/transport_errors.hpp>

namespace {

using namespace fixpp::transport;

// ─────────────────────────────────────────────────────────────────────────────
// Phase 3a: compile-time default-value assertions (Clarifications Q5=A).
// ─────────────────────────────────────────────────────────────────────────────

TEST(SocketOptionDefaults, ConfigDefaultsTcpNodelayTrue) {
    Transport::Config cfg{};
    EXPECT_TRUE(cfg.tcp_nodelay)
        << "Clarifications Q5=A: tcp_nodelay must default to true (Nagle OFF)";
}

TEST(SocketOptionDefaults, ConfigDefaultsSoLingerDisabled) {
    Transport::Config cfg{};
    EXPECT_FALSE(cfg.so_linger_enabled)
        << "Clarifications Q5=A: so_linger_enabled must default to false";
    EXPECT_EQ(cfg.so_linger_seconds, 0)
        << "so_linger_seconds must default to 0 when linger is disabled";
}

TEST(SocketOptionDefaults, ConfigDefaultsTcpKeepaliveOff) {
    Transport::Config cfg{};
    // FIX-level Heartbeat is the primary keep-alive; TCP keepalive is opt-in.
    EXPECT_FALSE(cfg.tcp_keepalive)
        << "tcp_keepalive must default to false (opt-in)";
}

// ─────────────────────────────────────────────────────────────────────────────
// DISABLED integration cells — require asio_tls_transport (T026/T037).
// ─────────────────────────────────────────────────────────────────────────────

// Cell 1: initiator leg — verify socket options on the connected socket.
// Construct asio_tls_transport via make_asio_tls_transport; open loopback TCP;
// query socket options via ASIO get_option.
TEST(DISABLED_SocketOptionDefaults, InitiatorLegTcpNodelay) {
    // Flow:
    //   1. make_asio_tls_transport(exec, Config{}, ssl_cfg, nullptr).
    //   2. async_connect(Endpoint{"127.0.0.1", loopback_port}).
    //   3. socket.get_option(asio::ip::tcp::no_delay) → ASSERT value == true.
    //   4. socket.get_option(asio::socket_base::linger) → ASSERT enabled == false.
    GTEST_SKIP() << "Requires asio_tls_transport (T026) — wired by T029";
}

// Cell 2: acceptor leg — verify socket options on the accepted socket.
// NOTE: This cell depends on US3 T037 (asio_listener). Wired by T038.
// TODO (T038): wire this cell once asio_listener ships.
TEST(DISABLED_SocketOptionDefaults, AcceptorLegTcpNodelay) {
    // Flow:
    //   1. Stand up asio_listener on 127.0.0.1:0 (OS port).
    //   2. Connect stub client.
    //   3. asio_listener::async_accept → Transport*.
    //   4. Query accepted socket options via the Transport interface:
    //      ASSERT TCP_NODELAY=1, SO_LINGER disabled.
    //      (The accepted_transport_config pass-through per Appendix D §D.8
    //       ensures asio_listener copies cfg_.accepted_transport_config to the
    //       accepted transport at mint time.)
    GTEST_SKIP() << "Requires asio_listener (US3 T037) — wired by T038";
}

}  // namespace
