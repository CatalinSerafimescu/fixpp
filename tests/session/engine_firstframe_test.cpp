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
// non-discriminating because every leg ends in a close. [gate-b/r1 nit] Step 2
// is already excluded by CONSTRUCTION, not by the elapsed band: each probe's
// `t0` is taken only after its own mTLS handshake has returned, by which
// point the server-side 1500ms handshake timer has already been cancelled
// (asio_tls_transport.cpp). The elapsed band's job is narrower — it separates
// the two Step-3 outcomes from EACH OTHER: the immediate byte-budget
// rejection from the ~5s first-frame-deadline backstop.
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
    // [gate-b/r1 FQ-1] s and timed_out are shared-owned and captured BY VALUE
    // in the timer handler below (not by reference to a frame local). Once
    // self_deadline.async_wait() has queued a handler for the io_context's
    // ready queue, cancel() cannot un-queue it — if the read completion is
    // dequeued first, this coroutine resumes and returns, and the still-queued
    // timer handler would then write through references into a destroyed
    // frame. Shared ownership means the handler always has a live target;
    // asio_tls_transport::cancel()-equivalent close() on an already-closed
    // socket is a documented no-op, so a late-firing handler is harmless.
    auto s = std::make_shared<asio::ip::tcp::socket>(ioc);
    asio::steady_timer self_deadline(ioc);
    bool connected = false;
    auto timed_out = std::make_shared<bool>(false);
    try {
        co_await s->async_connect(
            asio::ip::tcp::endpoint{asio::ip::make_address("127.0.0.1"), port},
            asio::use_awaitable);
        connected = true;
        if (!payload.empty())
            co_await asio::async_write(*s, asio::buffer(payload), asio::use_awaitable);

        // Probe-owned bound (< the test's run_for): on the stub the read pends
        // forever, so WE must terminate it — otherwise the final ioc.run() in
        // the test would hang on this dangling read. On expiry we close our own
        // socket and mark timed_out so the resulting abort is NOT miscounted as
        // a server-side close.
        self_deadline.expires_after(2s);
        self_deadline.async_wait([s, timed_out](const std::error_code& ec) {
            if (!ec) {
                *timed_out = true;
                s->close();
            }
        });

        std::array<char, 64> buf{};
        co_await s->async_read_some(asio::buffer(buf), asio::use_awaitable);
        // Reaching here means the server SENT data — not a pre-session close.
        self_deadline.cancel();
    } catch (const std::system_error&) {
        self_deadline.cancel();
        // eof / connection_reset AFTER connect, and NOT our own deadline-close,
        // == the acceptor closed the pre-session connection (the window fired).
        if (connected && !*timed_out) closed.store(true, std::memory_order_release);
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
        // [gate-b/r1 FQ-1] client and self_timed_out are shared-owned and
        // captured BY VALUE in the timer handler below — see the identical
        // rationale on probe_closed_within_window() above. `make_client()`
        // returns a unique_ptr<Transport>, which converts implicitly.
        std::shared_ptr<fixpp::transport::Transport> client =
            fixture.make_client(ioc.get_executor());
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
                auto self_timed_out = std::make_shared<bool>(false);
                asio::steady_timer self_deadline(ioc);
                self_deadline.expires_after(self_deadline_after);
                self_deadline.async_wait([client, self_timed_out](const std::error_code& ec) {
                    if (!ec) {
                        *self_timed_out = true;
                        (void)client->cancel();
                    }
                });

                std::array<std::byte, 64> buf{};
                auto read_r = co_await client->async_read_some(std::span<std::byte>{buf});
                self_deadline.cancel();

                // A read ERROR that is not our own cancellation == the acceptor
                // closed the pre-session connection. A successful read would
                // mean the server sent us data, i.e. no pre-session close.
                if (!read_r.has_value() && !*self_timed_out) {
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
// widened to 2x and so would only witness gross removals. [gate-b/r1 FQ-2]
// budget + 1. Any widening to >= 4098 leaves the frame incomplete, the read
// blocks, and the close falls through to kFirstFrameDeadline at ~5s — which
// the elapsed band catches. The single 4096->4097 mutant survives; that is
// the deliberate price of a payload that is agnostic to whether production
// rejects at >= or at > (see issue #233 / production budget-vs-FR-014
// "exceeds" mismatch), so this test needs no change when that is corrected.
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
// proxy #228 reports. Requiring elapsed >= 4700ms is what makes this witness
// reachable ONLY through something close to the 5s deadline, not the 1500ms
// handshake bound. [gate-b/r1 FQ-3a/FQ-3b] Honest claim: this test pins the
// Step-3 close into a measured band [4700, 7000)ms — a deadline moved to
// <= 4500ms goes RED (FQ-3c, the EXPECT_GE below), and a deadline widened to
// >= 8000ms (measured ~8000ms) also goes RED (FQ-3b, the EXPECT_LT below;
// see that assertion's comment for the measurement provenance). It still
// CANNOT distinguish 5000ms from, say, 6000ms — a wall-clock witness has
// that limit. Closing THAT gap would need a production clock/timer seam on
// read_first_frame_bounded, which is a behaviour change triggering a real
// Gate A (Article XVII §1 — concurrency/cancellation) and is rejected here
// as out of scope for a tests-only remediation; see issue #233.
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
    // [gate-b/r1 FQ-3c] 4700, not 4000: kills the `kFirstFrameDeadline: 5000 ->
    // 4500` mutant (measured ~4560ms would pass a 4000ms floor). Safe on
    // physics, not a projection: the only route to elapsed < 4700ms is the
    // server arming kFirstFrameDeadline more than 300ms BEFORE our own t0
    // (taken after the client handshake returns, above), which loopback
    // handshake ordering forbids — timers fire late, never early. Figures
    // already in evidence put real elapsed at 5068ms (linux-clang-debug) and
    // 5124ms (MSVC), so 4700 leaves >= 368ms of headroom under either; a
    // sanitizer lane can only push elapsed further UP, never down, so this
    // floor cannot false-fail under TSan/ASan overhead.
    EXPECT_GE(measured_ms, 4700)
        << "SC-011 (FR-014) [#228]: the close arrived after " << measured_ms
        << "ms, i.e. BEFORE the 5s kFirstFrameDeadline could plausibly fire. "
        << "Something earlier closed this connection, so this test would not "
        << "detect the removal of the first-frame deadline — exactly the "
        << "proxy-assertion defect this witness exists to rule out.";
    // [gate-b/r1 FQ-3b] Upper band, measurement-gated per the triage: measured
    // 5077ms on linux-clang-tsan (ctest -R, i.e. this test running ALONE),
    // 5070ms on linux-clang-debug, 5124ms on MSVC debug — ~1.9s of headroom
    // under 7000. Limitation: the TSan figure is from a filtered single-test
    // run; linux-clang-tsan is the one lane that runs ctest with
    // execution.jobs: 2 (CMakePresets.json, PR #227), so a real concurrent CI
    // run could measure later than 5077ms. 7000ms is sized to absorb that
    // un-measured lateness — it is not a claim that 5077ms is the worst case.
    // 7000 is also the largest value that still kills the `kFirstFrameDeadline:
    // 5000 -> 8000` mutant, which is the entire point of this leg; it must not
    // drift upward past this without re-deriving from a fresh measurement.
    EXPECT_LT(measured_ms, 7000)
        << "SC-011 (FR-014) [#228]: the close arrived after " << measured_ms
        << "ms. kFirstFrameDeadline is configured at 5s; a close this late "
        << "means the deadline is no longer ~5s — i.e. it has been widened — "
        << "which the lower bound above cannot see on its own.";
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
// [gate-b/r1 FQ-2] The payload is budget+1 (4097) bytes. Any widening to
// >= 4098 leaves the frame incomplete, the read blocks, and the close falls
// through to kFirstFrameDeadline at ~5s, which the elapsed band below
// catches. The single 4096->4097 mutant survives GREEN — that is the
// deliberate price of a payload that is agnostic to whether production
// rejects at >= or at > (issue #233), so this test needs no change if that
// boundary is later corrected. See make_carried_over_budget_payload().
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
                                        // 4097 = kFirstFrameMaxBytes (4096) + 1.
                                        make_carried_over_budget_payload(4097),
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
        << "sent 4097 bytes without completing a frame must be closed and its "
        << "accept slot reclaimed. Measured from real I/O — no close within 9s.";
    // [gate-b/r1 FQ-2 / F-7] "on the first read" is not exact: async_read_some
    // may return short, in which case each valid prefix is fed to the Framer
    // and classified PARTIAL (framer.cpp) before the NEXT read pushes the
    // cumulative buffer to the budget. Either way the close is on the budget
    // arm, not the deadline.
    EXPECT_LT(measured_ms, 2000)
        << "SC-011 (FR-014) [#228]: the close arrived after " << measured_ms
        << "ms. This peer sends 4097 bytes against a 4096-byte "
        << "kFirstFrameMaxBytes, so it reaches the rejection boundary under "
        << "BOTH the current >= implementation and the intended > semantics "
        << "(issue #233) — possibly over more than one read, before the next "
        << "Framer feed. A live budget therefore rejects this peer "
        << "immediately; a close near 5000ms means the budget did NOT fire "
        << "and the connection was reclaimed by kFirstFrameDeadline instead.";
}

// [gate-b/r1 FQ-4] Neither Step-3 witness above proves the accept loop
// RE-SPINS after a Step-3 rejection: each opens exactly one connection and
// returns. Mutating engine.cpp's Step-3 failure arm from
// `transport->close(); continue;` to `transport->close(); co_return;` leaves
// both witnesses above green — and `AcceptLoopRunsContinuously` too, since
// its raw-TCP probes never speak TLS and so never reach Step 3 at all — while
// the engine permanently stops accepting: an SC-011 "accept slot reclaimed …
// other peers unaffected" violation. This test drives TWO SEQUENTIAL
// post-handshake over-budget peers; the second must ALSO be rejected by the
// byte budget (not merely "eventually closed" or "never accepted"), which is
// only possible if the accept loop re-issued async_accept after the first
// peer's Step-3 rejection.
TEST(EngineFirstFrameTest, PostHandshakeRejectionDoesNotStopTheAcceptLoop) {
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

    // First peer: over-budget mTLS client, same 4097 payload as
    // PostHandshakeOverBudgetClosedByByteBudget. A short 3s self-deadline is
    // ample against the ~105ms budget-rejection close.
    PostHandshakeProbe first;
    asio::co_spawn(ioc,
                   probe_post_handshake(ioc, harness->transport_fixture(), port,
                                        make_carried_over_budget_payload(4097),
                                        /*self_deadline_after=*/3s, first),
                   asio::detached);
    run_until(ioc, first.done, 5s);

    const bool first_closed = first.closed.load(std::memory_order_acquire);
    const auto first_ms = first.elapsed.count();

    // Second peer, started only after the first has fully reported. If the
    // accept loop did not re-spin (the mutation case), this connect's TCP
    // handshake still succeeds (the listener socket itself is untouched —
    // only async_accept was never reissued), but no async_accept is pending
    // to complete the mTLS handshake server-side, so this probe's own read
    // never sees a server-side close and `second.done` never flips within the
    // bound below: `run_until` exhausts its cap with `second.closed == false`.
    PostHandshakeProbe second;
    asio::co_spawn(ioc,
                   probe_post_handshake(ioc, harness->transport_fixture(), port,
                                        make_carried_over_budget_payload(4097),
                                        /*self_deadline_after=*/3s, second),
                   asio::detached);
    run_until(ioc, second.done, 5s);

    const bool second_closed = second.closed.load(std::memory_order_acquire);
    const auto second_ms = second.elapsed.count();

    auto stop_fut = asio::co_spawn(ioc, harness->engine().stop(), asio::use_future);
    ioc.run();
    stop_fut.get();

    ASSERT_TRUE(first_closed) << "SC-011 (FR-014): the first over-budget peer must be "
                              << "closed and its accept slot reclaimed.";
    EXPECT_LT(first_ms, 2000) << "first peer: the close arrived after " << first_ms
                              << "ms — expected an immediate byte-budget rejection, "
                              << "not a deadline-backstop close.";
    ASSERT_TRUE(second_closed)
        << "SC-011 (FR-014) / [#228 FQ-4]: the SECOND over-budget peer, connected "
        << "after the first was rejected, must ALSO be closed. If it is not, the "
        << "accept loop failed to re-issue async_accept after reclaiming the first "
        << "peer's slot — Step 3's failure arm stopped the loop instead of "
        << "continuing it, exactly what `transport->close(); continue;` -> "
        << "`transport->close(); co_return;` at engine.cpp's Step-3 rejection "
        << "would do, and exactly what AcceptLoopRunsContinuously (raw-TCP, "
        << "Step-2-only) cannot see.";
    EXPECT_LT(second_ms, 2000)
        << "second peer: the close arrived after " << second_ms
        << "ms — expected an immediate byte-budget rejection (proving the accept "
        << "loop re-spun AND Step 3 is live again), not a deadline-backstop close "
        << "or a stall.";
}
