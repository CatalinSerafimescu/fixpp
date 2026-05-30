// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// tests/session/engine_firstframe_test.cpp — T010 [P] [US1] Phase 3
//
// TDD RED: bounded pre-session window (FR-014).
//
// Scenario (SC-011 / C1 steps 2-3 / C5 accept-scope domain):
//   Inbound connections that stall, send nothing, or send over-budget data
//   MUST be closed within the configured deadline. Transport + accept slot
//   MUST be reclaimed. Other peers MUST be unaffected.
//
// RED (stub): run_accept_loop exits after open() without calling
//   listener.async_accept(). Accept-scope deadline never armed.
//   Assertion "connection closed within deadline" FAILS.
//
// GREEN (T011/T012/T013): bounded first-frame window wired; bad peers closed.
//
// Bounding: ioc.run_for(3s) — no hang on stub path.
// IMPORTANT: stop() clears registry, frees sessions — capture state BEFORE stop().
// Anchors: tasks.md T010; spec.md SC-011 / FR-014; contracts C1/C5; data-model E-7.

#include <array>
#include <atomic>
#include <chrono>
#include <future>
#include <string>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/steady_timer.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>
#include <asio/write.hpp>
#include <asio/detached.hpp>

#include <gtest/gtest.h>

#include <fixpp/core/engine_config.hpp>
#include <fixpp/session/engine.hpp>

#include "engine_loopback_harness.hpp"

using namespace std::chrono_literals;
using fixpp::test_support::EngineLoopbackHarness;

namespace {

// Real behavioral probe (NO hardcoded result): connect TCP to the acceptor,
// optionally send `payload`, then read. The acceptor's bounded pre-session
// window (FR-014) MUST close the connection within its deadline → our read
// completes with eof/connection_reset → we record `closed = true`.
//
// RED (stub): run_accept_loop never calls async_accept(); the connection is
//   never processed/closed → our read pends → the outer run_for bound elapses
//   with the read still suspended → `closed` stays false. (Genuine RED — false
//   measured from real I/O, not a literal.)
// GREEN (T012): accept loop accepts, arms the first-frame deadline/byte-budget,
//   times-out/over-budget-closes the bad peer → our read sees eof → `closed=true`.
static asio::awaitable<void>
probe_closed_within_window(asio::io_context& ioc, uint16_t port,
                           std::string payload, std::atomic<bool>& closed) {
    asio::ip::tcp::socket s(ioc);
    asio::steady_timer self_deadline(ioc);
    bool connected = false;
    bool timed_out = false;
    try {
        co_await s.async_connect(
            asio::ip::tcp::endpoint{asio::ip::make_address("127.0.0.1"), port},
            asio::use_awaitable);
        connected = true;
        if (!payload.empty())
            co_await asio::async_write(s, asio::buffer(payload), asio::use_awaitable);

        // Probe-owned bound (< the test's run_for): on the stub the read pends
        // forever, so WE must terminate it — otherwise the final ioc.run() in
        // the test would hang on this dangling read. On expiry we close our own
        // socket and mark timed_out so the resulting abort is NOT miscounted as
        // a server-side close.
        self_deadline.expires_after(2s);
        self_deadline.async_wait([&](const std::error_code& ec) {
            if (!ec) { timed_out = true; s.close(); }
        });

        std::array<char, 64> buf{};
        co_await s.async_read_some(asio::buffer(buf), asio::use_awaitable);
        // Reaching here means the server SENT data — not a pre-session close.
        self_deadline.cancel();
    } catch (const std::system_error&) {
        self_deadline.cancel();
        // eof / connection_reset AFTER connect, and NOT our own deadline-close,
        // == the acceptor closed the pre-session connection (the window fired).
        if (connected && !timed_out)
            closed.store(true, std::memory_order_release);
    } catch (...) { self_deadline.cancel(); }
    co_return;
}

}  // namespace

TEST(EngineFirstFrameTest, SilentPeerClosedWithinDeadline) {
    asio::io_context ioc;
    fixpp::core::EngineConfig eng_cfg;
    eng_cfg.executor = ioc.get_executor();
    auto harness = EngineLoopbackHarness::build(ioc.get_executor(), std::move(eng_cfg));
    if (!harness) { GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set"; }

    harness->engine().start();
    uint16_t port = harness->server_endpoint().port;

    std::atomic<bool> closed{false};
    asio::co_spawn(ioc, probe_closed_within_window(ioc, port, /*payload=*/"", closed),
                   asio::detached);

    // BOUNDED: 3s. The probe's read pends on the stub; run_for returns at the bound.
    ioc.run_for(3s);
    ioc.restart();

    const bool measured_closed = closed.load(std::memory_order_acquire);

    auto stop_fut = asio::co_spawn(ioc, harness->engine().stop(), asio::use_future);
    ioc.run(); stop_fut.get();

    EXPECT_TRUE(measured_closed)
        << "SC-011 (FR-014): a silent peer must be closed within the 3s deadline. "
        << "Measured from real I/O — the peer's read never saw eof (server never "
        << "closed it). RED: stub never accepts/arms the deadline. "
        << "GREEN after T011/T012/T013: bounded first-frame window closes + reclaims.";
}

TEST(EngineFirstFrameTest, OverBudgetPayloadClosedWithinDeadline) {
    asio::io_context ioc;
    fixpp::core::EngineConfig eng_cfg;
    eng_cfg.executor = ioc.get_executor();
    auto harness = EngineLoopbackHarness::build(ioc.get_executor(), std::move(eng_cfg));
    if (!harness) { GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set"; }

    harness->engine().start();
    uint16_t port = harness->server_endpoint().port;

    std::atomic<bool> closed{false};
    asio::co_spawn(ioc,
        probe_closed_within_window(ioc, port, /*payload=*/std::string(8192, 'X'), closed),
        asio::detached);

    // BOUNDED: 3s.
    ioc.run_for(3s);
    ioc.restart();

    const bool measured_closed = closed.load(std::memory_order_acquire);

    auto stop_fut = asio::co_spawn(ioc, harness->engine().stop(), asio::use_future);
    ioc.run(); stop_fut.get();

    EXPECT_TRUE(measured_closed)
        << "SC-011 (FR-014): an over-budget peer (8KiB before a valid Logon) must "
        << "be closed within the 3s deadline and its accept slot reclaimed. "
        << "Measured from real I/O — the peer's read never saw eof. "
        << "GREEN after T011/T012/T013: over-budget → wire_frame_too_large → close.";
}

TEST(EngineFirstFrameTest, AcceptLoopRunsContinuously) {
    // SC-011 (C5): the accept loop must re-spin after each connection so bad
    // peers don't starve good ones. Measured via TWO sequential silent peers:
    // both must be closed by the pre-session window. A non-looping accept would
    // close only the first; the stub closes neither.
    asio::io_context ioc;
    fixpp::core::EngineConfig eng_cfg;
    eng_cfg.executor = ioc.get_executor();
    auto harness = EngineLoopbackHarness::build(ioc.get_executor(), std::move(eng_cfg));
    if (!harness) { GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set"; }

    harness->engine().start();
    uint16_t port = harness->server_endpoint().port;

    std::atomic<bool> first_closed{false};
    asio::co_spawn(ioc, probe_closed_within_window(ioc, port, "", first_closed),
                   asio::detached);
    ioc.run_for(3s);
    ioc.restart();

    std::atomic<bool> second_closed{false};
    asio::co_spawn(ioc, probe_closed_within_window(ioc, port, "", second_closed),
                   asio::detached);
    ioc.run_for(3s);
    ioc.restart();

    const bool both_closed =
        first_closed.load(std::memory_order_acquire) &&
        second_closed.load(std::memory_order_acquire);

    auto stop_fut = asio::co_spawn(ioc, harness->engine().stop(), asio::use_future);
    ioc.run(); stop_fut.get();

    EXPECT_TRUE(both_closed)
        << "SC-011 (C5): the accept loop must re-spin and close a SECOND silent "
        << "peer, not just the first. Measured from real I/O on two sequential "
        << "peers. RED: stub closes neither. GREEN after T011/T012: loop re-spins.";
}
