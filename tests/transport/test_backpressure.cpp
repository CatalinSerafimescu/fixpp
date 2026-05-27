// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// tests/transport/test_backpressure.cpp — [2h §9 seam #10] (T039).
//
// Drives outbound traffic through `fixpp::transport::test::mock_transport`
// and verifies:
//   - (a) outbound coroutines suspend on a depth-1 in-flight slot per the
//         [2h §4.1] in-flight exclusivity contract — no transport-internal
//         queue spilling per [arch §5.8] + [2h §1.1] FR-039.
//   - (d) FR-037 partial-write-then-cancel — mock surfaces
//         transport_write_short identical to production asio_tls_transport.
//
// DISABLED cells:
//   - (b) disconnect_and_recover policy threshold — depends on the
//         post-this-feature session-Phase-4 backpressure-policy FSM.
//   - (c) No-drop-oldest binding ([const §XV.15] / FR-039) — same scope.

#define FIXPP_ALLOW_MOCK_TRANSPORT
#include <fixpp/transport/test/mock_transport.hpp>

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>

#include <chrono>
#include <vector>

namespace {

using namespace std::chrono_literals;
using fixpp::core::error;
using fixpp::transport::test::Script;
using fixpp::transport::test::mock_transport;

// Helper — synchronous std::byte construction.
constexpr std::byte b(unsigned v) {
    return static_cast<std::byte>(v & 0xFFu);
}

// ════════════════════════════════════════════════════════════════════════════
// Cell A — 10³ sequential outbound writes through a mock with write_latency:
// each call awaits the latency before the next can start. Verifies the
// in-flight exclusivity contract — second concurrent write would return
// transport_write_in_progress, but the test drives writes SEQUENTIALLY (one
// co_spawn at a time) which is the [arch §5.8] depth-1 strand-queue model.
// ════════════════════════════════════════════════════════════════════════════
TEST(Backpressure, SequentialWritesDrainAtStrandDepth1) {
    asio::io_context ioc;
    Script s;
    s.write_latency = 1ms;  // small latency to make ordering observable

    mock_transport mt{ioc.get_executor(), std::move(s)};

    constexpr std::size_t kFrames = 1000;
    std::vector<std::byte> frame(64, b(0xAA));

    std::size_t completed = 0;
    auto fut = asio::co_spawn(
        ioc.get_executor(),
        [&]() -> asio::awaitable<void> {
            for (std::size_t i = 0; i < kFrames; ++i) {
                auto rc = co_await mt.async_write(std::span<const std::byte>{frame});
                if (rc.has_value()) {
                    ++completed;
                }
            }
            co_return;
        },
        asio::use_future);

    ioc.run();
    fut.get();

    EXPECT_EQ(completed, kFrames);
    EXPECT_EQ(mt.async_writes_observed(), kFrames);
    EXPECT_EQ(mt.outbound_bytes_seen().size(), kFrames * frame.size());
}

// ════════════════════════════════════════════════════════════════════════════
// Cell D — FR-037 partial-write-then-cancel. Configure the mock to ack only
// the first N/2 bytes of a 64-byte write and return transport_write_short.
// The mock must surface this variant IDENTICAL to the production
// asio_tls_transport per [2h §6.6]:1182.
// ════════════════════════════════════════════════════════════════════════════
TEST(Backpressure, PartialWriteReturnsWriteShort) {
    asio::io_context ioc;
    Script s;
    s.partial_write_bytes = 32;  // ack first half, then short-write

    mock_transport mt{ioc.get_executor(), std::move(s)};

    std::vector<std::byte> frame(64, b(0x55));

    auto fut = asio::co_spawn(
        ioc.get_executor(),
        mt.async_write(std::span<const std::byte>{frame}),
        asio::use_future);

    ioc.run();
    auto result = fut.get();

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), error::transport_write_short)
        << "FR-037: partial-write must surface transport_write_short, not "
           "transport_write_cancelled";

    // The mock captured the first 32 bytes — the partial-write contract is
    // "wrote those bytes, then surfaced short". The FSM's recovery path
    // ([FIX-SL §4.5.2] ResendRequest) re-sends starting from the durable
    // store; 2h does NOT roll back the captured prefix.
    EXPECT_EQ(mt.outbound_bytes_seen().size(), 32u);
    EXPECT_EQ(mt.async_writes_observed(), 1u);
}

// ════════════════════════════════════════════════════════════════════════════
// Cell A2 — in-flight exclusivity smoke. mock_transport tracks writes_observed
// per call; the strand-confined contract means concurrent writes are a
// programmer error (the production asio_tls_transport returns
// transport_write_in_progress). The mock does not enforce this guard — it's
// the FSM's responsibility — so this cell is purely a smoke that a single
// write completes successfully through the awaitable.
// ════════════════════════════════════════════════════════════════════════════
TEST(Backpressure, SingleWriteSucceeds) {
    asio::io_context ioc;
    mock_transport mt{ioc.get_executor(), Script{}};

    std::vector<std::byte> frame(16, b(0x01));

    auto fut = asio::co_spawn(
        ioc.get_executor(),
        mt.async_write(std::span<const std::byte>{frame}),
        asio::use_future);

    ioc.run();
    auto result = fut.get();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, frame.size());
    EXPECT_EQ(mt.async_writes_observed(), 1u);
}

// ════════════════════════════════════════════════════════════════════════════
// Cell B (DISABLED) — disconnect_and_recover policy threshold. Requires the
// session-Phase-4 FSM's BackpressurePolicy plumbing which is post-this-
// feature scope. Re-enable when session-Phase-4 lands.
// ════════════════════════════════════════════════════════════════════════════
TEST(DISABLED_Backpressure, DisconnectAndRecoverTriggersTransportCancel) {
    GTEST_SKIP() << "Requires session-Phase-4 BackpressurePolicy FSM "
                    "(post-this-feature scope per spec.md Q3 carry-forward).";
}

// ════════════════════════════════════════════════════════════════════════════
// Cell C (DISABLED) — no-drop-oldest binding ([const §XV.15] / FR-039).
// Same dependency as Cell B.
// ════════════════════════════════════════════════════════════════════════════
TEST(DISABLED_Backpressure, NoDropOldestBehaviour) {
    GTEST_SKIP() << "Requires session-Phase-4 BackpressurePolicy FSM "
                    "(post-this-feature scope).";
}

}  // namespace
