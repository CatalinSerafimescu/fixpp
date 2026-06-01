// tests/transport/transport_smoke_test.cpp
// Phase 2 compile-only smoke test — verifies all foundational headers parse
// cleanly and the 22 error::transport_* enum variants occupy contiguous
// slots 94..115 per T006 + data-model E-13 + [2h §6.6]:1167-1204.

#include <gtest/gtest.h>

#include <fixpp/core/error.hpp>
#include <fixpp/transport/endpoint.hpp>
#include <fixpp/transport/tls_transport.hpp>
#include <fixpp/transport/transport.hpp>
#include <fixpp/transport/transport_errors.hpp>
#include <fixpp/transport/transport_factory.hpp>

// ── Slot-range assertions: 22 variants occupy exactly 94..115 ─────────────────
static_assert(static_cast<std::uint8_t>(fixpp::core::error::transport_resolve_failed) == 94);
static_assert(static_cast<std::uint8_t>(fixpp::core::error::transport_connect_refused) == 95);
static_assert(static_cast<std::uint8_t>(fixpp::core::error::transport_connect_timeout) == 96);
static_assert(static_cast<std::uint8_t>(fixpp::core::error::transport_already_connected) == 97);
static_assert(static_cast<std::uint8_t>(fixpp::core::error::transport_already_closed) == 98);
static_assert(static_cast<std::uint8_t>(fixpp::core::error::transport_read_in_progress) == 99);
static_assert(static_cast<std::uint8_t>(fixpp::core::error::transport_write_in_progress) == 100);
static_assert(static_cast<std::uint8_t>(fixpp::core::error::transport_reconnect_limit_exceeded) ==
              101);
static_assert(static_cast<std::uint8_t>(fixpp::core::error::transport_read_eof) == 102);
static_assert(static_cast<std::uint8_t>(fixpp::core::error::transport_read_truncated) == 103);
static_assert(static_cast<std::uint8_t>(fixpp::core::error::transport_read_error) == 104);
static_assert(static_cast<std::uint8_t>(fixpp::core::error::transport_write_short) == 105);
static_assert(static_cast<std::uint8_t>(fixpp::core::error::transport_write_error) == 106);
static_assert(static_cast<std::uint8_t>(fixpp::core::error::transport_handshake_failed) == 107);
static_assert(static_cast<std::uint8_t>(fixpp::core::error::transport_handshake_timeout) == 108);
static_assert(static_cast<std::uint8_t>(fixpp::core::error::transport_factory_failed) == 109);
static_assert(static_cast<std::uint8_t>(fixpp::core::error::transport_psk_unsupported) == 110);
static_assert(static_cast<std::uint8_t>(fixpp::core::error::transport_connect_cancelled) == 111);
static_assert(static_cast<std::uint8_t>(fixpp::core::error::transport_read_cancelled) == 112);
static_assert(static_cast<std::uint8_t>(fixpp::core::error::transport_write_cancelled) == 113);
static_assert(static_cast<std::uint8_t>(fixpp::core::error::transport_handshake_cancelled) == 114);
static_assert(static_cast<std::uint8_t>(fixpp::core::error::transport_accept_cancelled) == 115);

// ── Boundary guard: 011's tls_load_cancelled must remain at 93 ───────────────
static_assert(static_cast<std::uint8_t>(fixpp::core::error::tls_load_cancelled) == 93,
              "011 boundary shifted — re-check 012 slot assignment");

// ── Transport::Config default-value spot checks ───────────────────────────────
static_assert(fixpp::transport::Transport::Config{}.tcp_nodelay == true,
              "Clarifications Q5=A: tcp_nodelay must default to true (Nagle OFF)");
static_assert(fixpp::transport::Transport::Config{}.so_linger_enabled == false,
              "Clarifications Q5=A: so_linger_enabled must default to false");
static_assert(fixpp::transport::Transport::Config{}.connect_timeout.count() == 30'000,
              "connect_timeout must default to 30 s (30000 ms)");

// ── Endpoint default-value spot checks ───────────────────────────────────────
static_assert(fixpp::transport::Endpoint{}.port == 0);
static_assert(fixpp::transport::Endpoint{}.backlog == 128);

// ── namespace alias re-exports are consistent ─────────────────────────────────
static_assert(fixpp::transport::errors::transport_resolve_failed ==
              fixpp::core::error::transport_resolve_failed);
static_assert(fixpp::transport::errors::transport_accept_cancelled ==
              fixpp::core::error::transport_accept_cancelled);

TEST(TransportFoundational, StaticAssertionsPass) {
    // All load-bearing assertions are static_asserts above; this test body is
    // a compile-time-only witness. A runtime assertion is included to produce
    // a green ctest row.
    EXPECT_EQ(static_cast<std::uint8_t>(fixpp::core::error::transport_resolve_failed), 94u);
    EXPECT_EQ(static_cast<std::uint8_t>(fixpp::core::error::transport_accept_cancelled), 115u);
}
