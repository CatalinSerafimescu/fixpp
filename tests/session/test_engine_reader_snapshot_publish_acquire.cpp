// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_engine_reader_snapshot_publish_acquire.cpp
// T008 (046-atomic-shared-ptr, NFR-017) — consumer publish/acquire witness for
// Engine::reader_snapshot_
// (fixpp::sync::atomic_shared_ptr<const ReaderSnapshot>).
//
// Design anchor: .specify/046-atomic-shared-ptr.md (plan row 6-consumers)
// Consumer: Engine (include/fixpp/session/engine.hpp §302)
//
// Pattern: the Engine's control strand publishes a new ReaderSnapshot whenever
// the registry changes (register_session, start, stop).  Engine::lookup() is the
// any-thread-safe reader — it acquire-loads reader_snapshot_ and searches it.
//
// This test runs the Engine's io_context on one background thread while the main
// thread calls lookup() concurrently in a loop bounded by the ioc-done flag,
// exercising the release-store (control strand) vs acquire-load (any thread)
// ordering through the fixpp::sync::atomic_shared_ptr primitive.
//
// Design of the publish trigger:
//   - The Engine registers one insecure_plain_tcp INITIATOR session pointing at a
//     raw TCP acceptor on loopback (port=0).
//   - The raw acceptor (a standalone asio::ip::tcp::acceptor coroutine) accepts the
//     connection and holds it open for the test window.
//   - The Engine's connect loop (run_connect_loop) connects, emits the initiator
//     Logon, and then calls publish_entry — making the session visible via lookup().
//   - No TLS fixtures required; no FIX Logon ACK required for publication.
//
// Witnessed transition, not a fixed-window guarantee, and not a start-barrier
// alone: a dedicated reader THREAD is started first and loops on lookup() while
// the engine's io_context is not yet being driven — Engine::start() only spawns
// coroutines onto ioc; nothing executes them until ioc is pumped — so its first
// reads are null. The main thread then waits (bounded) until that LIVE reader has
// actually recorded a null read before letting the io_context run at all, so
// publication cannot precede the reader's first observation. The reader keeps
// running, concurrently with the io_context, until ioc_done — spanning the whole
// transition — so null_reads>0 and nonnull_reads>0 together prove a live
// concurrent reader observed both the pre-publish and post-publish states, not
// just that a read could theoretically have landed on either side. A single
// EXPECT_GT(nonnull_reads,0) alone is NOT sufficient: the io_thread could publish
// before the reader's first read, in which case every observed read is the
// terminal non-null state and no null→non-null transition is actually witnessed.
//
// Scope: no TLS fixtures required (insecure_plain_tcp + raw loopback peer).
// SecurityProfile::kind::insecure_plain_tcp — suppress the [[deprecated]] friction
// because this test file is legitimately exercising the plain-TCP path.
//
// Anchors: data-model E-7/INV-9/D-SNAP; [2h D-SNAP]; engine.hpp §302-307;
//          NFR-017; [[feedback_single_threaded_harness_masks_strand_races]];
//          engine.cpp run_connect_loop step 4 (publish_entry at line ~1079).

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/redirect_error.hpp>
#include <asio/steady_timer.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/system_clock_source.hpp>
#include <fixpp/session/engine.hpp>
#include <fixpp/session/security_profile.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/transport/endpoint.hpp>
#include <fixpp/transport/reconnect_policy.hpp>
#include <fixpp/transport/transport.hpp>
#include <fixpp/transport/transport_factory.hpp>
#include <future>
#include <memory>
#include <thread>

#include "support/minimal_dictionary.hpp"
#include "support/pump_until_ready.hpp"

using namespace std::chrono_literals;
using fixpp::session::Engine;
using fixpp::session::SessionId;
using fixpp::session::SessionConfig;

namespace {

// How long we let the engine run before calling stop().
// Must be large enough for:
//   (1) The ioc_thread to start and pick up tasks.
//   (2) The raw acceptor to bind and begin listening.
//   (3) The connect loop to connect, emit Logon, and call publish_entry.
// On a loopback the connect+logon-emit path takes <10ms; 500ms gives plenty of margin.
constexpr auto kRunWindow = 500ms;

// ── Raw TCP acceptor coroutine ────────────────────────────────────────────────
// Accepts exactly one connection and holds it open for the run window, then
// exits.  This gives the initiator something to connect to without needing any
// FIX protocol implementation or TLS on the peer side.
// The port is passed by reference and set before the coroutine suspends so the
// main thread can read it after binding.
static asio::awaitable<void> run_raw_acceptor(
    asio::io_context& ioc,
    uint16_t& bound_port,
    std::atomic<bool>& port_ready,
    std::chrono::milliseconds hold_window)
{
    asio::ip::tcp::acceptor acceptor{ioc};
    asio::ip::tcp::endpoint ep{asio::ip::make_address("127.0.0.1"), 0};
    acceptor.open(ep.protocol());
    acceptor.set_option(asio::ip::tcp::acceptor::reuse_address{true});
    acceptor.bind(ep);
    acceptor.listen(1);
    bound_port = acceptor.local_endpoint().port();
    port_ready.store(true, std::memory_order_release);

    asio::error_code ec;
    auto sock = co_await acceptor.async_accept(asio::redirect_error(asio::use_awaitable, ec));
    if (!ec) {
        // Hold the socket open for the test window so the initiator's read-pump
        // stays alive (not EOF-terminated) and the session remains published.
        asio::steady_timer timer{ioc};
        timer.expires_after(hold_window);
        co_await timer.async_wait(asio::redirect_error(asio::use_awaitable, ec));
        sock.close(ec);
    }
}

}  // namespace

// ── EngineReaderSnapshotPublishAcquire ───────────────────────────────────────

TEST(EngineReaderSnapshotPublishAcquire, LookupNeverSeesTornPointer) {
    // [gate-b/r2 F2] declared BEFORE `ioc` (destructed LAST) so that on the
    // bounded-pump timeout path below, `ioc` (and any coroutine frame it
    // still holds — e.g. a suspended engine->stop()) is destroyed first,
    // before `engine`. See the timeout branch below for the other half of
    // this fix: on that path `engine` must also be deliberately leaked, or
    // ~Engine's `stopped_` assert aborts instead of reporting the failure.
    std::unique_ptr<Engine> engine;
    asio::io_context ioc;

    // ── Bind the raw loopback acceptor.  ────────────────────────────────────
    // We need the port before creating the SessionConfig, but the acceptor must
    // bind using the same ioc.  We bind synchronously here (no co_await needed).
    asio::ip::tcp::acceptor raw_acc{ioc};
    {
        asio::ip::tcp::endpoint ep{asio::ip::make_address("127.0.0.1"), 0};
        raw_acc.open(ep.protocol());
        raw_acc.set_option(asio::ip::tcp::acceptor::reuse_address{true});
        raw_acc.bind(ep);
        raw_acc.listen(1);
    }
    uint16_t bound_port = raw_acc.local_endpoint().port();

    // ── Build an insecure plain-TCP factory (no TLS fixtures required). ─────
    // insecure_plain_tcp is [[deprecated]]; the pragma at the top suppresses it.
    fixpp::transport::Transport::Config tcfg;
    tcfg.connect_timeout = 200ms;  // short connect timeout; connect is instant on loopback
    auto factory_r = fixpp::transport::make_asio_plain_transport_factory(tcfg);
    ASSERT_TRUE(factory_r.has_value()) << "make_asio_plain_transport_factory failed";

    // ── Build Engine. ────────────────────────────────────────────────────────
    fixpp::core::EngineConfig eng_cfg;
    eng_cfg.executor = ioc.get_executor();
    eng_cfg.clock = std::make_shared<fixpp::core::system_clock_source>(ioc.get_executor());
    eng_cfg.default_transport_factory = std::move(*factory_r);

    engine = std::make_unique<Engine>(ioc.get_executor(), std::move(eng_cfg));

    // ── Register one initiator session targeting the loopback port. ──────────
    SessionConfig sc;
    sc.dictionary = fixpp::test_support::make_minimal_dictionary();
    sc.begin_string = "FIX.4.2";
    sc.sender_comp_id = "SNAP_SENDER";
    sc.target_comp_id = "SNAP_TARGET";
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    sc.security_profile =
        fixpp::session::SecurityProfile{fixpp::session::SecurityProfile::kind::insecure_plain_tcp};
#pragma clang diagnostic pop
    sc.reconnect_endpoint = fixpp::transport::Endpoint{"127.0.0.1", bound_port};
    // Unlimited reconnect attempts so the loop stays alive for the whole window.
    fixpp::transport::ReconnectPolicy policy;
    policy.max_attempts = std::numeric_limits<unsigned>::max();
    sc.reconnect_policy = policy;
    sc.heartbeat_interval = 30s;

    auto reg_r = engine->register_session(sc);
    ASSERT_TRUE(reg_r.has_value()) << "register_session failed";

    auto sid = SessionId::from_config(sc);

    // ── Start the engine — spawns the connect loop on the ioc executor. ──────
    auto start_r = engine->start();
    ASSERT_TRUE(start_r.has_value()) << "start() failed";

    // ── Concurrency pattern: ─────────────────────────────────────────────────
    //   background ioc_thread: runs the engine's event loop (connect loop on session
    //     strand, control strand for publish_entry, raw acceptor).
    //   main thread: calls lookup() in a loop until the ioc_thread finishes.
    //
    // The background thread runs:
    //   - The raw acceptor coroutine (accepts the initiator's connection).
    //   - The Engine's connect loop (connects, emits Logon, calls publish_entry → snapshot
    //     release-store of reader_snapshot_).
    // The main thread runs:
    //   - lookup() → acquire-load of reader_snapshot_.
    //
    // A torn pointer from reader_snapshot_ would either crash (ASan: invalid deref)
    // or be flagged by TSan (refcount race on the shared_ptr control block).
    // We do NOT call session->state() — that is session-strand-only (data race).

    std::atomic<bool> ioc_done{false};
    std::atomic<bool> reader_saw_null{false};
    int null_reads = 0;
    int nonnull_reads = 0;

    // ── Reader thread: live across the whole null→non-null transition ───────
    // Started BEFORE the io_context is ever pumped and kept running until
    // ioc_done, so it is a single continuously-reading witness spanning both
    // the pre-publish and post-publish states — not two disjoint loops. Its
    // first observation is guaranteed null (nothing executes on `ioc` until
    // it is pumped below), but the ASSERTION below does not rely on that
    // guarantee alone: the main thread holds the io_context back (see the
    // wait on reader_saw_null) until this thread has ACTUALLY observed and
    // recorded a null read, so the null read is witnessed by a live
    // concurrent reader, not merely presumed reachable. An overlap witness on
    // a start barrier alone is probabilistic — the sibling lesson recorded in
    // [[feedback_overlap_witness_needs_stimulus_held_until_witnessed]]; this
    // applies the same fix to the null-side of the transition.
    std::thread reader_thread([&] {
        while (!ioc_done.load(std::memory_order_acquire)) {
            auto session = engine->lookup(sid);
            if (session == nullptr) {
                // Valid: session not yet published (publish_entry not yet called).
                ++null_reads;
                reader_saw_null.store(true, std::memory_order_release);
            } else {
                // Session published.  We hold a strong-ref via shared_ptr<Session>.
                // The refcount increment is the primary correctness witness under TSan:
                // a torn acquire-load of reader_snapshot_ would produce an invalid
                // shared_ptr whose refcount operations race → TSan fires.
                ++nonnull_reads;
            }
            // Prevent the optimizer from eliding the loads.
            (void)session.get();
        }
    });

    // Wait (bounded) until the live reader has actually observed null before
    // letting the io_context run at all — publication cannot happen before
    // this point, so it cannot precede the reader's first observation. This
    // wait is a scheduling nicety, not a correctness gate: it has no fatal
    // assertion (a std::thread must not be joinable when one fires), and if
    // it times out the reader is very likely to have already recorded a null
    // read anyway (nothing publishes until below), so the final assertions
    // below remain the honest oracle either way.
    {
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        while (!reader_saw_null.load(std::memory_order_acquire) &&
               std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(1ms);
        }
    }

    // ── Let the io_context run, witnessed concurrently by the reader ────────
    // Spawn the raw acceptor coroutine.  Hold window = kRunWindow so the socket
    // stays alive for the whole test.
    asio::co_spawn(ioc, [&]() -> asio::awaitable<void> {
        asio::error_code ec;
        // Accept one connection and hold for the window.
        auto sock = co_await raw_acc.async_accept(asio::redirect_error(asio::use_awaitable, ec));
        if (!ec) {
            asio::steady_timer timer{ioc};
            timer.expires_after(kRunWindow);
            co_await timer.async_wait(asio::redirect_error(asio::use_awaitable, ec));
            sock.close(ec);
        }
    }, asio::detached);

    std::thread ioc_thread([&] {
        ioc.run_for(kRunWindow);
        ioc_done.store(true, std::memory_order_release);
    });

    // Wait for both threads. No fatal assertion runs before this join —
    // a std::thread must not be joinable when one fires (it would call
    // std::terminate on unwind).
    ioc_thread.join();
    reader_thread.join();

    // If the initiator never connected within the window (e.g. a slow or
    // failed loopback connect), run_raw_acceptor's async_accept() (or the
    // co_spawned accept lambda above) is STILL SUSPENDED here — the
    // unconditional ioc.run() this block used to call would then wait on it
    // forever instead of returning once stop() completes, turning a
    // diagnosable failure (nonnull_reads==0 below) into a 30s CTest timeout.
    // Force it to unblock before draining. [gate-b/r1 P2-6]
    asio::error_code raw_acc_close_ec;
    raw_acc.cancel(raw_acc_close_ec);
    raw_acc.close(raw_acc_close_ec);

    // Stop the engine cleanly: restart the ioc and drive stop() to completion,
    // bounded so a stuck stop() (or a still-pending cancellation completion)
    // fails loudly instead of hanging the ctest run.
    ioc.restart();
    auto stop_fut = asio::co_spawn(ioc, engine->stop(), asio::use_future);
    if (!fixpp::test_support::pump_until_ready(ioc, stop_fut)) {
        // [gate-b/r2 F2] Already-failing path: the stop() frame is still
        // suspended in `ioc` and holds Engine&. ~Engine would trip its
        // stopped_ assert and abort, replacing this diagnosable failure.
        // Leak deliberately.
        (void)engine.release();
        ADD_FAILURE() << "engine->stop() did not complete within the bounded pump budget";
        return;
    }
    stop_fut.get();

    // Primary oracle: TSan must report no race on reader_snapshot_ or the shared_ptr
    // refcount. The functional oracle is the WITNESSED null→non-null transition:
    // null_reads>0 proves a nonterminal pre-publish observation actually happened
    // (not just that the loop could theoretically have seen one), and
    // nonnull_reads>0 proves publish_entry ran and was observed. Neither alone is
    // sufficient — see the file header and w-Q1/w-Q2 above.
    EXPECT_GT(null_reads, 0)
        << "The reader never observed a null lookup() result before the engine's "
           "io_context was driven — cannot witness the null-to-non-null transition "
           "this test names. This mutation is harness scheduling, not a production "
           "defect: production has no synchronous eager-publication seam to force it.";

    EXPECT_GT(nonnull_reads, 0)
        << "Expected at least one non-null lookup() result — the connect loop should have "
           "called publish_entry (reader_snapshot_ release-store) before the 500ms window "
           "expired.  If this fails, the test window is too short or the loopback connect "
           "is failing.  null_reads=" << null_reads
        << " nonnull_reads=" << nonnull_reads;

    // Destroy Engine after stop() — strict assert(stopped()) is satisfied.
    engine.reset();
}

// ── Counter-test for P2-6 (gate-b/r1) ────────────────────────────────────────
// Isolates the exact mechanism the fix above depends on, without the full
// Engine/session machinery: if nothing ever connects, a raw acceptor's
// async_accept() is still suspended when the driving thread's bounded
// run_for() window closes. Before the fix, an unconditional ioc.run() after
// that point would wait on it forever. This proves cancel()+close() on the
// acceptor, followed by a bounded pump, reaches completion promptly instead
// of hanging — i.e. the disable-the-connect-trigger case reaches its
// diagnostic rather than timing out.
TEST(EngineReaderSnapshotPublishAcquire, PendingAcceptDoesNotWedgeBoundedDrain) {
    asio::io_context ioc;
    asio::ip::tcp::acceptor raw_acc{ioc};
    asio::ip::tcp::endpoint ep{asio::ip::make_address("127.0.0.1"), 0};
    raw_acc.open(ep.protocol());
    raw_acc.set_option(asio::ip::tcp::acceptor::reuse_address{true});
    raw_acc.bind(ep);
    raw_acc.listen(1);

    // Nobody ever connects: this stays suspended past the run_for() window
    // below, exactly like the connect-disabled scenario this test names.
    std::atomic<bool> accept_completed{false};
    asio::co_spawn(
        ioc,
        [&]() -> asio::awaitable<void> {
            asio::error_code ec;
            (void)co_await raw_acc.async_accept(asio::redirect_error(asio::use_awaitable, ec));
            accept_completed.store(true, std::memory_order_release);
        },
        asio::detached);

    ioc.run_for(50ms);
    ioc.restart();
    ASSERT_FALSE(accept_completed.load(std::memory_order_acquire))
        << "the accept must still be pending for this counter-test to be meaningful";

    // The fix under test: cancel+close the acceptor before the bounded drain.
    asio::error_code close_ec;
    raw_acc.cancel(close_ec);
    raw_acc.close(close_ec);

    ASSERT_TRUE(fixpp::test_support::pump_until(
        ioc, [&] { return accept_completed.load(std::memory_order_acquire); }, 2s))
        << "cancel()+close() on a pending acceptor must unblock its async_accept() "
        << "promptly instead of leaving it suspended forever";
}

#pragma clang diagnostic pop
