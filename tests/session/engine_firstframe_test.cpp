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
// THE BOUNDED WINDOW HAS TWO INDEPENDENT STAGES — this file witnesses BOTH
// (#228). The acceptor rejects a bad pre-session peer at one of two places:
//
//   Step 2  engine.cpp — tls_handshake_timeout (1500ms): a peer that never
//           completes the mTLS handshake (a raw-TCP probe is exactly that).
//   Step 3  engine.cpp — read_first_frame_bounded's kFirstFrameDeadline (5s)
//           and kFirstFrameMaxBytes (4096): a peer that DID complete the
//           handshake but then stalls or floods.
//
// The three raw-TCP probes below can only ever reach Step 2 — they never
// speak TLS, so the accept loop rejects them at the handshake. Their names and
// rationales used to claim the Step-3 bounds; #228 showed that was a proxy.
// The two post-handshake probes (real mTLS clients) are the Step-3 witnesses,
// and they assert an ELAPSED BAND, not merely "closed": a bare close is
// non-discriminating because every leg ends in a close. The band is what
// separates the 1500ms handshake timeout from the 5s first-frame deadline,
// and the immediate byte-budget rejection from the deadline backstop.
//
// Bounding: every test drives the io_context in slices until its probe reports
//   or a per-test cap elapses — no hang, and no burning the full cap once the
//   probe is done (the accept loop keeps outstanding work forever, so a bare
//   ioc.run_for(cap) would always cost the whole cap).
// IMPORTANT: stop() clears registry, frees sessions — capture state BEFORE stop().
// Anchors: tasks.md T010; spec.md SC-011 / FR-014; contracts C1/C5; data-model E-7;
//          issue #228 (Step-3 legs had no witness that could reach them).

#include <gtest/gtest.h>

#include <array>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/steady_timer.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>
#include <asio/write.hpp>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/session/engine.hpp>
#include <fixpp/transport/endpoint.hpp>
#include <fixpp/transport/tls_transport.hpp>
#include <fixpp/transport/transport.hpp>
#include <future>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "engine_loopback_harness.hpp"

using namespace std::chrono_literals;
using fixpp::test_support::EngineLoopbackHarness;

namespace {

// Drive `ioc` in slices until `done` flips or `cap` elapses. The accept loop
// holds an outstanding async_accept forever, so a bare ioc.run_for(cap) never
// returns early — it always costs the full cap. Slicing keeps a passing run at
// the cost of the behaviour under test rather than the cost of its bound.
static void run_until(asio::io_context& ioc, const std::atomic<bool>& done,
                      std::chrono::steady_clock::duration cap) {
    const auto limit = std::chrono::steady_clock::now() + cap;
    while (!done.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < limit) {
        ioc.run_for(50ms);
        ioc.restart();
    }
}

// Real behavioral probe (NO hardcoded result): connect TCP to the acceptor,
// optionally send `payload`, then read. The acceptor's bounded pre-session
// window (FR-014) MUST close the connection within its deadline → our read
// completes with eof/connection_reset → we record `closed = true`.
//
// This probe NEVER speaks TLS, so it can only ever be rejected at Step 2 (the
// 1500ms tls_handshake_timeout, or an immediate handshake parse failure) —
// never at the Step-3 first-frame bounds. Its own 2s self-deadline is below
// the 5s kFirstFrameDeadline, so a Step-3 close would be scored as `timed_out`
// and FAIL this test: Step 3 is not merely un-exercised here, it is
// unobservable. See probe_post_handshake() for the Step-3 witnesses. [#228]
//
// RED (stub): run_accept_loop never calls async_accept(); the connection is
//   never processed/closed → our read pends → the outer bound elapses with the
//   read still suspended → `closed` stays false. (Genuine RED — false measured
//   from real I/O, not a literal.)
// GREEN (T012): accept loop accepts, bounds the handshake, closes the non-TLS
//   peer → our read sees eof → `closed=true`.
static asio::awaitable<void> probe_closed_within_window(asio::io_context& ioc, uint16_t port,
                                                        std::string payload,
                                                        std::atomic<bool>& closed,
                                                        std::atomic<bool>& done) {
    asio::ip::tcp::socket s(ioc);
    asio::steady_timer self_deadline(ioc);
    bool connected = false;
    bool timed_out = false;
    try {
        co_await s.async_connect(asio::ip::tcp::endpoint{asio::ip::make_address("127.0.0.1"), port},
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
            if (!ec) {
                timed_out = true;
                s.close();
            }
        });

        std::array<char, 64> buf{};
        co_await s.async_read_some(asio::buffer(buf), asio::use_awaitable);
        // Reaching here means the server SENT data — not a pre-session close.
        self_deadline.cancel();
    } catch (const std::system_error&) {
        self_deadline.cancel();
        // eof / connection_reset AFTER connect, and NOT our own deadline-close,
        // == the acceptor closed the pre-session connection (the window fired).
        if (connected && !timed_out) closed.store(true, std::memory_order_release);
    } catch (...) {
        self_deadline.cancel();
    }
    done.store(true, std::memory_order_release);
    co_return;
}

// ── Step-3 witnesses (#228) ──────────────────────────────────────────────────
// A REAL mTLS client: connects, completes the handshake, and only then behaves
// badly. Because the handshake succeeded, Step 2's 1500ms bound is cancelled
// (asio_tls_transport.cpp: the handshake timer is a local, cancelled the moment
// async_handshake returns) and the accept loop is sitting in Step 3's
// read_first_frame_bounded — so whatever closes us now IS a Step-3 bound.
//
// `elapsed` is measured from the instant OUR handshake returns to the instant
// the acceptor's close is observed. It is the discriminator: a bare `closed`
// is true on every rejection leg and therefore proves nothing about WHICH one
// fired. The self-deadline is deliberately far above the 5s kFirstFrameDeadline
// (the old 2s probe bound is precisely what made that deadline unobservable).
struct PostHandshakeProbe {
    std::atomic<bool> closed{false};
    std::atomic<bool> done{false};
    std::chrono::milliseconds elapsed{0};  // written before `done` is released
};

static asio::awaitable<void> probe_post_handshake(
    asio::io_context& ioc, fixpp::transport::test::LoopbackTlsFixture& fixture, uint16_t port,
    std::string payload, std::chrono::milliseconds self_deadline_after, PostHandshakeProbe& out) {
    // make_client() throws when the fixture cannot mint a TLS client; catch it
    // here so a detached coroutine cannot terminate the process, and still
    // release `done` so the test reports a clean RED instead of hanging.
    try {
        auto client = fixture.make_client(ioc.get_executor());
        auto* tls = dynamic_cast<fixpp::transport::TlsTransport*>(client.get());
        if (tls != nullptr) {
            const fixpp::transport::Endpoint ep{"127.0.0.1", port};
            if ((co_await client->async_connect(ep)).has_value() &&
                (co_await tls->async_handshake(fixture.ssl_cfg())).has_value()) {
                const auto t0 = std::chrono::steady_clock::now();

                if (!payload.empty()) {
                    std::vector<std::byte> bytes;
                    bytes.reserve(payload.size());
                    for (char c : payload) bytes.push_back(static_cast<std::byte>(c));
                    (void)co_await client->async_write(std::span<const std::byte>{bytes});
                }

                // Probe-owned backstop: if the acceptor never closes us (the
                // mutation case for the deadline leg), WE must terminate the
                // read so the test reports a clean RED instead of hanging.
                bool self_timed_out = false;
                asio::steady_timer self_deadline(ioc);
                self_deadline.expires_after(self_deadline_after);
                self_deadline.async_wait([&](const std::error_code& ec) {
                    if (!ec) {
                        self_timed_out = true;
                        (void)client->cancel();
                    }
                });

                std::array<std::byte, 64> buf{};
                auto read_r = co_await client->async_read_some(std::span<std::byte>{buf});
                self_deadline.cancel();

                // A read ERROR that is not our own cancellation == the acceptor
                // closed the pre-session connection. A successful read would
                // mean the server sent us data, i.e. no pre-session close.
                if (!read_r.has_value() && !self_timed_out) {
                    out.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0);
                    out.closed.store(true, std::memory_order_release);
                }
            }
        }
        (void)client->close();
    } catch (...) {
    }
    out.done.store(true, std::memory_order_release);
    co_return;
}

// A payload the Framer CARRIES rather than rejects: a well-formed
// "8=…\x019=<huge>\x01" header whose BodyLength far exceeds what we send, so
// parse_frame() classifies it partial (src/wire/framer.cpp: body_off +
// body_length > bytes.size() → make_partial()). This matters for
// mutation-proofness — a payload the Framer rejected would route the close
// through the framing-error arm and pin the wrong branch.
//
// SIZE IS LOAD-BEARING: the caller sends only just OVER kFirstFrameMaxBytes,
// not a large multiple. A payload of 2x the budget would still trip a budget
// widened to 2x and so would only witness gross removals. At budget+ε, ANY
// widening leaves the frame incomplete, the read blocks, and the close falls
// through to kFirstFrameDeadline at ~5s — which the elapsed band catches.
static std::string make_carried_over_budget_payload(std::size_t total_bytes) {
    // Split literal: "\x01" is a maximal-munch hex escape, so "…\x019=…" would
    // be read as the single byte 0x19 followed by '='.
    std::string p =
        "8=FIX.4.2\x01"
        "9=200000\x01";
    p.append(total_bytes - p.size(), 'X');
    return p;
}

}  // namespace

// Renamed at #228: this probe is raw TCP, so the bound it actually pins is the
// Step-2 tls_handshake_timeout, not the Step-3 first-frame deadline the old
// name ("SilentPeerClosedWithinDeadline") claimed.
TEST(EngineFirstFrameTest, NonTlsSilentPeerClosedByHandshakeBound) {
    asio::io_context ioc;
    fixpp::core::EngineConfig eng_cfg;
    eng_cfg.executor = ioc.get_executor();
    auto harness = EngineLoopbackHarness::build(ioc.get_executor(), std::move(eng_cfg));
    if (!harness) {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }

    ASSERT_TRUE(harness->engine().start().has_value()) << "engine.start() failed";
    // Run briefly to let the accept loop bind the listener (OS port assignment).
    ioc.run_for(std::chrono::milliseconds{50});
    ioc.restart();
    uint16_t port = harness->server_endpoint().port;
    if (port == 0) {
        GTEST_SKIP() << "acceptor listener did not bind";
    }

    std::atomic<bool> closed{false};
    std::atomic<bool> done{false};
    asio::co_spawn(ioc, probe_closed_within_window(ioc, port, /*payload=*/"", closed, done),
                   asio::detached);

    // BOUNDED: 3s cap. The probe's read pends on the stub; the cap ends the run.
    run_until(ioc, done, 3s);

    const bool measured_closed = closed.load(std::memory_order_acquire);

    auto stop_fut = asio::co_spawn(ioc, harness->engine().stop(), asio::use_future);
    ioc.run();
    stop_fut.get();

    EXPECT_TRUE(measured_closed)
        << "SC-011 (FR-014): a silent NON-TLS peer must be closed by the bounded "
        << "pre-session window — here the 1500ms tls_handshake_timeout, since the "
        << "probe never speaks TLS. Measured from real I/O — the peer's read never "
        << "saw eof (server never closed it). RED: stub never accepts/arms the "
        << "bound. GREEN after T011/T012/T013: bounded window closes + reclaims.";
}

// Renamed at #228: the 8KiB payload is not a valid TLS record, so OpenSSL
// rejects it during the handshake — kFirstFrameMaxBytes is never reached here.
// The old name ("OverBudgetPayloadClosedWithinDeadline") and its
// `wire_frame_too_large` rationale were both proxies. The byte budget is
// witnessed by PostHandshakeOverBudgetClosedByByteBudget below.
TEST(EngineFirstFrameTest, NonTlsPeerWithPayloadClosedByHandshakeBound) {
    asio::io_context ioc;
    fixpp::core::EngineConfig eng_cfg;
    eng_cfg.executor = ioc.get_executor();
    auto harness = EngineLoopbackHarness::build(ioc.get_executor(), std::move(eng_cfg));
    if (!harness) {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }

    ASSERT_TRUE(harness->engine().start().has_value()) << "engine.start() failed";
    ioc.run_for(std::chrono::milliseconds{50});
    ioc.restart();
    uint16_t port = harness->server_endpoint().port;
    if (port == 0) {
        GTEST_SKIP() << "acceptor listener did not bind";
    }

    std::atomic<bool> closed{false};
    std::atomic<bool> done{false};
    asio::co_spawn(
        ioc,
        probe_closed_within_window(ioc, port, /*payload=*/std::string(8192, 'X'), closed, done),
        asio::detached);

    // BOUNDED: 3s cap.
    run_until(ioc, done, 3s);

    const bool measured_closed = closed.load(std::memory_order_acquire);

    auto stop_fut = asio::co_spawn(ioc, harness->engine().stop(), asio::use_future);
    ioc.run();
    stop_fut.get();

    EXPECT_TRUE(measured_closed)
        << "SC-011 (FR-014): a peer that dumps 8KiB of non-TLS bytes must be closed "
        << "by the bounded pre-session window and its accept slot reclaimed. The "
        << "bytes are not a valid TLS record, so the rejection is at the handshake. "
        << "Measured from real I/O — the peer's read never saw eof. "
        << "GREEN after T011/T012/T013: bounded window closes + reclaims.";
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
    if (!harness) {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }

    ASSERT_TRUE(harness->engine().start().has_value()) << "engine.start() failed";
    ioc.run_for(std::chrono::milliseconds{50});
    ioc.restart();
    uint16_t port = harness->server_endpoint().port;
    if (port == 0) {
        GTEST_SKIP() << "acceptor listener did not bind";
    }

    std::atomic<bool> first_closed{false};
    std::atomic<bool> first_done{false};
    asio::co_spawn(ioc, probe_closed_within_window(ioc, port, "", first_closed, first_done),
                   asio::detached);
    run_until(ioc, first_done, 3s);

    std::atomic<bool> second_closed{false};
    std::atomic<bool> second_done{false};
    asio::co_spawn(ioc, probe_closed_within_window(ioc, port, "", second_closed, second_done),
                   asio::detached);
    run_until(ioc, second_done, 3s);

    const bool both_closed = first_closed.load(std::memory_order_acquire) &&
                             second_closed.load(std::memory_order_acquire);

    auto stop_fut = asio::co_spawn(ioc, harness->engine().stop(), asio::use_future);
    ioc.run();
    stop_fut.get();

    EXPECT_TRUE(both_closed)
        << "SC-011 (C5): the accept loop must re-spin and close a SECOND silent "
        << "peer, not just the first. Measured from real I/O on two sequential "
        << "peers. RED: stub closes neither. GREEN after T011/T012: loop re-spins.";
}

// ── #228: Step-3 witnesses — kFirstFrameDeadline and kFirstFrameMaxBytes ─────

// FR-014 / engine.cpp kFirstFrameDeadline (5s). A peer that completes the mTLS
// handshake and then sends NOTHING holds a pre-session accept slot; the
// first-frame deadline must reclaim it.
//
// The LOWER bound is the load-bearing assertion. `closed == true` alone is
// satisfied by every rejection leg, including the 1500ms handshake bound that
// the raw-TCP probes above already pin — asserting only that is precisely the
// proxy #228 reports. Requiring elapsed >= 4000ms is what makes this witness
// reachable ONLY through the 5s deadline. Widen or delete kFirstFrameDeadline
// and this test goes RED; the byte-budget witness below stays GREEN.
//
// Upper bound is deliberately loose (timers fire late under load, never early);
// the probe's own self-deadline caps the run.
TEST(EngineFirstFrameTest, PostHandshakeStallClosedByFirstFrameDeadline) {
    asio::io_context ioc;
    fixpp::core::EngineConfig eng_cfg;
    eng_cfg.executor = ioc.get_executor();
    auto harness = EngineLoopbackHarness::build(ioc.get_executor(), std::move(eng_cfg));
    if (!harness) {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }

    ASSERT_TRUE(harness->engine().start().has_value()) << "engine.start() failed";
    ioc.run_for(std::chrono::milliseconds{50});
    ioc.restart();
    uint16_t port = harness->server_endpoint().port;
    if (port == 0) {
        GTEST_SKIP() << "acceptor listener did not bind";
    }

    PostHandshakeProbe probe;
    asio::co_spawn(ioc,
                   probe_post_handshake(ioc, harness->transport_fixture(), port, /*payload=*/"",
                                        /*self_deadline_after=*/12s, probe),
                   asio::detached);

    // Cap > the probe's self-deadline so the probe always reports; a passing
    // run costs ~5s (the deadline), not the cap.
    run_until(ioc, probe.done, 15s);

    const bool measured_closed = probe.closed.load(std::memory_order_acquire);
    const auto measured_ms = probe.elapsed.count();

    auto stop_fut = asio::co_spawn(ioc, harness->engine().stop(), asio::use_future);
    ioc.run();
    stop_fut.get();

    ASSERT_TRUE(measured_closed)
        << "SC-011 (FR-014): a peer that completed the mTLS handshake and then "
        << "stalled must be closed by kFirstFrameDeadline. Measured from real I/O "
        << "— the peer's post-handshake read never saw eof within 12s.";
    EXPECT_GE(measured_ms, 4000)
        << "SC-011 (FR-014) [#228]: the close arrived after " << measured_ms
        << "ms, i.e. BEFORE the 5s kFirstFrameDeadline could fire. Something "
        << "earlier closed this connection, so this test would not detect the "
        << "removal of the first-frame deadline — exactly the proxy-assertion "
        << "defect this witness exists to rule out.";
}

// FR-014 / engine.cpp kFirstFrameMaxBytes (4096). A peer that completes the
// handshake and then floods bytes without ever completing a frame must be cut
// off by the byte budget, not merely by the deadline backstop.
//
// The UPPER bound is the load-bearing assertion here, for the mirror-image
// reason: if kFirstFrameMaxBytes is widened or removed, read_first_frame_bounded
// keeps carrying the (deliberately incomplete) frame and the connection is
// still closed — 5s later, by kFirstFrameDeadline. Only the elapsed band
// separates "rejected on budget" from "rejected on deadline".
//
// The payload is budget+104 bytes, so the witness is sensitive to ANY widening
// of the budget, not just a large one — see make_carried_over_budget_payload().
TEST(EngineFirstFrameTest, PostHandshakeOverBudgetClosedByByteBudget) {
    asio::io_context ioc;
    fixpp::core::EngineConfig eng_cfg;
    eng_cfg.executor = ioc.get_executor();
    auto harness = EngineLoopbackHarness::build(ioc.get_executor(), std::move(eng_cfg));
    if (!harness) {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }

    ASSERT_TRUE(harness->engine().start().has_value()) << "engine.start() failed";
    ioc.run_for(std::chrono::milliseconds{50});
    ioc.restart();
    uint16_t port = harness->server_endpoint().port;
    if (port == 0) {
        GTEST_SKIP() << "acceptor listener did not bind";
    }

    PostHandshakeProbe probe;
    asio::co_spawn(ioc,
                   probe_post_handshake(ioc, harness->transport_fixture(), port,
                                        // 4200 = kFirstFrameMaxBytes (4096) + epsilon.
                                        make_carried_over_budget_payload(4200),
                                        /*self_deadline_after=*/9s, probe),
                   asio::detached);

    // Cap > the 5s deadline so a budget-less build still REPORTS its close (at
    // ~5s) and fails on the band, rather than failing ambiguously on "never
    // closed".
    run_until(ioc, probe.done, 12s);

    const bool measured_closed = probe.closed.load(std::memory_order_acquire);
    const auto measured_ms = probe.elapsed.count();

    auto stop_fut = asio::co_spawn(ioc, harness->engine().stop(), asio::use_future);
    ioc.run();
    stop_fut.get();

    ASSERT_TRUE(measured_closed)
        << "SC-011 (FR-014): a peer that completed the mTLS handshake and then "
        << "sent 8KiB without completing a frame must be closed and its accept "
        << "slot reclaimed. Measured from real I/O — no close within 9s.";
    EXPECT_LT(measured_ms, 2000)
        << "SC-011 (FR-014) [#228]: the close arrived after " << measured_ms
        << "ms. The 4096-byte kFirstFrameMaxBytes budget is exceeded on the first "
        << "read, so a live budget rejects this peer immediately; a close near "
        << "5000ms means the budget did NOT fire and the connection was reclaimed "
        << "by kFirstFrameDeadline instead.";
}
