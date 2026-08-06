// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/first_frame_stop_test.cpp — T2b (SC-015 accept-slot leg),
// 088-firstframe-budget-timer-lifetime, Phase 4 (US2), tasks.md T022.
//
// Engine::stop()'s accept-slot leg: a peer that completes the mTLS handshake
// and then sends NOTHING holds a pre-session accept slot inside the accept
// loop's read_first_frame_bounded call. stop() must (a) return PROMPTLY, not
// merely eventually, and (b) reclaim the accept slot — the peer's connection
// must observe a server-initiated close.
//
// Why this exercises the SAME mutant as T2a (research.md D-6.1/D-6.12b): the
// accept loop (src/session/engine.cpp run_accept_loop) is spawned with
// asio::bind_cancellation_slot(entry.session_cancel.slot(), ...); stop()'s
// Step 1 (engine.cpp ~1182-1216) sets stopped_=true THEN emits
// cancellation_type::total on entry.session_cancel, which propagates through
// the coroutine's (already-reset-to-total, engine.cpp run_accept_loop) state
// into the currently-suspended read_first_frame_bounded join. stop()'s Step 3
// (engine.cpp:1263-1270) JOINS on outstanding_counter_ — it does not return
// until run_accept_loop itself has co_return'd, which requires
// read_first_frame_bounded to have returned. Under the bare-deadline-arm
// mutant (await_deadline replaced by a raw timer.async_wait(use_awaitable) —
// same mutant as T2a), the deadline arm's cancel is silently dropped, so
// read_first_frame_bounded cannot return until the FULL kFirstFrameDeadline
// (5000ms, engine.cpp:772) elapses — and neither can stop().
//
// T2b does NOT discharge SC-015's non-vacuity clause (research.md D-6.13a,
// settled at Gate A round 4): no positive barrier exists at engine scope. The
// client completing its handshake and the server then closing it after
// stop() is a STATED, BOUNDED INFERENCE that the accept loop was somewhere
// between the handshake and Session-publish — not a proof it was inside
// read_first_frame_bounded specifically. SC-015's non-vacuity rests on T2a
// and T6 (first_frame_total_cancel_tls_test.cpp), which have real barriers.
// A round-3 proposal to fake a positive barrier via an inverted
// test_hook_pre_publish_ was WITHDRAWN at round 4 (it is a negative barrier —
// an absence — the same shape round 4 rejected for T6's round-2 form); filed
// as residual T046, not bought here.
//
// Deterministic promptness construction (T045/SC-016 — NOT a wall-clock
// threshold): an intermediate timer the TEST owns is armed at 500ms against
// kFirstFrameDeadline's 5000ms — D-6.12's own T2b figure, a one-sided 10x
// margin. The assertion is an ORDERING between two test-controlled events
// (did the timer's handler run before stop() completed?), not a comparison
// of elapsed wall-clock against a constant. The context is always drained to
// stop() completion regardless of which fires first — Engine::stop() must be
// co_await'ed to completion before the Engine is destroyed (engine.hpp), so
// an early return mid-teardown would leave the Engine (and this TU's stack
// locals it references) in an unsafe state.
//
// Anchors: spec.md SC-015/FR-015; research.md D-6.1/D-6.12/D-6.12b/D-6.13a;
//          tasks.md T022; engine_firstframe_test.cpp (probe pattern, #228).

#include <gtest/gtest.h>

#include <array>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/steady_timer.hpp>
#include <asio/use_awaitable.hpp>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <exception>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/session/engine.hpp>
#include <fixpp/transport/endpoint.hpp>
#include <fixpp/transport/tls_transport.hpp>
#include <fixpp/transport/transport.hpp>
#include <memory>
#include <span>

#include "engine_loopback_harness.hpp"

using namespace std::chrono_literals;
using fixpp::test_support::EngineLoopbackHarness;

namespace {

// Drive `ioc` in slices until `done` flips or `cap` elapses. Same pattern as
// engine_firstframe_test.cpp's run_until — copied rather than shared because
// this is a distinct translation unit/executable (precedent:
// first_frame_total_cancel_tls_test.cpp duplicates EstablishedPair rather
// than including engine_firstframe_test.cpp's anonymous-namespace helpers).
void run_until(asio::io_context& ioc, std::atomic<bool> const& done,
                std::chrono::steady_clock::duration cap) {
    auto const limit = std::chrono::steady_clock::now() + cap;
    while (!done.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < limit) {
        ioc.run_for(50ms);
        ioc.restart();
    }
}

struct PostHandshakeProbe {
    std::atomic<bool> closed{false};
    std::atomic<bool> done{false};
};

// Real mTLS client: connects, completes the handshake, then goes SILENT — no
// payload, no close. `self_deadline_after` is a probe-owned backstop (NOT the
// mechanism under test): if the server never closes us, WE must terminate the
// read so the test reports a clean failure instead of hanging.
asio::awaitable<void> probe_post_handshake_silent(asio::io_context& ioc,
                                                    fixpp::transport::test::LoopbackTlsFixture& fixture,
                                                    std::uint16_t port,
                                                    std::chrono::milliseconds self_deadline_after,
                                                    PostHandshakeProbe& out) {
    try {
        std::shared_ptr<fixpp::transport::Transport> client = fixture.make_client(ioc.get_executor());
        auto* tls = dynamic_cast<fixpp::transport::TlsTransport*>(client.get());
        if (tls != nullptr) {
            fixpp::transport::Endpoint const ep{"127.0.0.1", port};
            if ((co_await client->async_connect(ep)).has_value() &&
                (co_await tls->async_handshake(fixture.ssl_cfg())).has_value()) {
                auto self_timed_out = std::make_shared<bool>(false);
                asio::steady_timer self_deadline(ioc);
                self_deadline.expires_after(self_deadline_after);
                self_deadline.async_wait([client, self_timed_out](std::error_code const& ec) {
                    if (!ec) {
                        *self_timed_out = true;
                        (void)client->cancel();
                    }
                });

                std::array<std::byte, 64> buf{};
                auto read_r = co_await client->async_read_some(std::span<std::byte>{buf});
                self_deadline.cancel();

                // A read ERROR that is not our own self-deadline cancellation ==
                // the acceptor closed the pre-session connection (accept slot
                // reclaimed).
                if (!read_r.has_value() && !*self_timed_out) {
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

}  // namespace

TEST(FirstFrameStop, StopReturnsPromptlyAndReclaimsAcceptSlot) {
    asio::io_context ioc;
    fixpp::core::EngineConfig eng_cfg;
    eng_cfg.executor = ioc.get_executor();
    auto harness = EngineLoopbackHarness::build(ioc.get_executor(), std::move(eng_cfg));
    if (!harness) {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }

    ASSERT_TRUE(harness->engine().start().has_value()) << "engine.start() failed";
    ioc.run_for(50ms);
    ioc.restart();
    std::uint16_t const port = harness->server_endpoint().port;
    if (port == 0) {
        GTEST_SKIP() << "acceptor listener did not bind";
    }

    PostHandshakeProbe probe;
    asio::co_spawn(ioc,
                    probe_post_handshake_silent(ioc, harness->transport_fixture(), port,
                                                 /*self_deadline_after=*/10s, probe),
                    asio::detached);

    // Let the client complete its handshake and the server-side accept loop
    // (as far as this test can tell — see the non-vacuity note above the
    // fixture) settle into read_first_frame_bounded before calling stop().
    ioc.run_for(200ms);
    ioc.restart();

    // Deterministic promptness (D-6.12b): `timer_fired_before_stop` is set
    // inside the intermediate timer's OWN handler ONLY if `stop_done` is
    // still false at that instant — an ordering check between two
    // test-controlled events, not a wall-clock comparison. The context is
    // ALWAYS driven to stop_done regardless of which fires first (safe
    // teardown on both the GREEN and RED paths — Engine::stop() must run to
    // completion before the Engine is destroyed).
    bool stop_done = false;
    bool timer_fired_before_stop = false;
    asio::steady_timer intermediate{ioc.get_executor()};
    intermediate.expires_after(500ms);
    intermediate.async_wait([&](std::error_code ec) {
        if (!ec && !stop_done) timer_fired_before_stop = true;
    });

    asio::co_spawn(ioc, harness->engine().stop(), [&](std::exception_ptr ep) {
        EXPECT_FALSE(ep) << "T2b: Engine::stop() threw.";
        stop_done = true;
    });

    while (!stop_done) {
        ASSERT_GT(ioc.run_one(), 0u)
            << "T2b: io_context ran out of work before Engine::stop() completed — a "
            << "broken cell (mis-wired timer/harness), not a RED proof.";
    }
    intermediate.cancel();
    ioc.poll();

    // THE promptness assertion — kills the bare-deadline-arm mutant (same
    // mutant T2a kills; D-6.12b), applied at engine scope.
    EXPECT_FALSE(timer_fired_before_stop)
        << "T2b (SC-015 accept-slot leg): the intermediate 500ms timer fired BEFORE "
        << "Engine::stop() completed, against a 5000ms kFirstFrameDeadline (10x margin). "
        << "Under the bare-deadline-arm mutant the deadline arm's own cancel is silently "
        << "dropped, so the accept loop's read_first_frame_bounded call — and therefore "
        << "stop()'s Step-3 join on outstanding_counter_ — cannot retire before the FULL "
        << "5000ms deadline elapses (D-6.12b): a bounded stall equal to the pre-fix tail, "
        << "not the unbounded hang FR-018/T029 fixes.";

    // Accept-slot reclaim: the peer's post-handshake read must observe a
    // server-initiated close once stop() has run.
    run_until(ioc, probe.done, 5s);
    EXPECT_TRUE(probe.closed.load(std::memory_order_acquire))
        << "T2b (SC-015 accept-slot leg): the accept slot was not reclaimed — the peer's "
        << "post-handshake read never observed a close after Engine::stop().";
}
