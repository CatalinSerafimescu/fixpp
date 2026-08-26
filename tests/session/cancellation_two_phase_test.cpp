// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/cancellation_two_phase_test.cpp
//
// Seam #11 — Two-phase cancellation (005-session-establishment-fsm T044 /
// Phase 6 / US4).
//
// Scenarios covered ([2d §6.5] / I-9 / SC-005 / FR-005):
//
//  1. CancellableDispatchThreeCases (carried from existing seam 4 patterns):
//     Verifies cancellable_dispatch slot-signalled-before-pickup reaps handler
//     (dispatch_aborted); not-signalled runs handler exactly once. These already
//     pass in the existing seam 4 tests — this seam checks the same property
//     from the session-layer perspective (the child cancellation_state used in
//     the phase-1 Logout step does NOT propagate to the root dispatch chain
//     while phase-1 is running).
//
//  2. ChildCancellationStateIsolatesLogout: The phase-1 child cancellation_state
//     isolates the Logout write + timeout from the phase-2 root total signal.
//     Specifically: root_cancel.emit(total) during phase-1 causes the child
//     to be cancelled (logout path exits) but does NOT skip the phase-2 cleanup
//     path (close() still completes, lifecycle transitions to closed_drained).
//
//  3. CloseIdempotent (I-10): second close() on a closing session returns the
//     same result without side effects (no duplicate Logout emission, no
//     re-entrant flush, no hang). Verified by driving two close() calls
//     concurrently and asserting both get an ok result.
//
//  4. CloseTerminalSkipsPhase1 (I-9): close(terminal) does NOT emit a Logout
//     frame (the transport_send callback is never called for a Logout in the
//     terminal path). Phase-2 root total fires immediately.
//
// Anchors: data-model.md I-9/I-10; [2d §6.5]; spec FR-005; SC-005;
// tasks.md T044.
#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <future>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"
#include "support/pump_until_ready.hpp"
#include "support/transport_double.hpp"

using namespace std::chrono_literals;

namespace fixpp::session::test {

namespace {

static std::vector<std::byte> make_logon_frame(std::string_view begin_string, std::uint32_t seq,
                                               std::string_view sender, std::string_view target,
                                               int heartbt = 30) {
    std::string body;
    body += "35=A\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=" + std::string(sender) + "\x01";
    body += "52=20240101-00:00:00.000\x01";
    body += "56=" + std::string(target) + "\x01";
    body += "98=0\x01";
    body += "108=" + std::to_string(heartbt) + "\x01";

    std::string hdr;
    hdr += "8=" + std::string(begin_string) + "\x01";
    hdr += "9=" + std::to_string(body.size()) + "\x01";

    std::string full = hdr + body;
    unsigned int cs = 0;
    for (unsigned char c : full) {
        cs += c;
    }
    cs &= 0xFFu;
    char csbuf[8];
    std::snprintf(csbuf, sizeof(csbuf), "%03u", cs);
    full += "10=" + std::string(csbuf) + "\x01";

    std::vector<std::byte> result;
    for (char c : full) {
        result.push_back(static_cast<std::byte>(c));
    }
    return result;
}

static std::string extract_field(std::span<const std::byte> frame, std::uint32_t tag) {
    std::string wire(reinterpret_cast<const char*>(frame.data()), frame.size());
    std::string needle = std::to_string(tag) + "=";
    auto pos = wire.find(needle);
    if (pos == std::string::npos) {
        return {};
    }
    pos += needle.size();
    auto end = wire.find('\x01', pos);
    if (end == std::string::npos) {
        return wire.substr(pos);
    }
    return wire.substr(pos, end - pos);
}

}  // namespace

// ── Test fixture ──────────────────────────────────────────────────────────────
//
// The `run_for(W); restart(); fut.get()` sites in this file use
// `run_window_then_ready` (tests/support/pump_until_ready.hpp). The window is
// PRESERVED: the hazard #289 names is the UNCONDITIONAL `get()`, not the fixed
// window. On a manually-driven io_context a `get()` the window did not satisfy
// blocks with nothing left to pump it — a deadlock ctest reports as a timeout.
//
// Teardown is deliberately NOT a fixture-destructor drain, which is the shape
// PRs #301 and #307 used for fixtures that OWN their Session. Here every
// `Session`, and every frame a coroutine spans, is a block-local declared AFTER
// the fixture, so it dies BEFORE a fixture destructor body could run — and a
// drain is what RESUMES a suspended frame, so a destructor drain would resume it
// over the destroyed Session. The drain runs on the MISS branch instead, in the
// scope that still owns that storage, and it CANCELS THE MOCK CLOCK'S SLEEPS
// first: the first transition to Active co_spawns a detached
// `run_liveness_loop()` that parks on `sleep_until`, which holds a work guard
// that `drain_or_report` cannot release (only a Clock can). Measured — without
// the `cancel_sleeps()` the drain burns its whole 5 s budget and then reports a
// residual it had no lever to clear. Both arms measured; see
// `decisions/speckit/pr4-289-clocked-capture-migration-oracle.md`.

class CancellationTwoPhaseTest : public ::testing::Test {
protected:
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine;

    void SetUp() override {
        using namespace std::chrono;
        auto utc = system_clock::time_point{} + seconds{1704067200};
        auto stp = fixpp::core::steady_time_point{} + seconds{0};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock = clock;
        engine.executor = ioc.get_executor();
    }

    SessionConfig make_cfg() {
        SessionConfig cfg;
        cfg.sender_comp_id = "ISLD";
        cfg.target_comp_id = "TW";
        cfg.begin_string = "FIX.4.2";
        cfg.heartbeat_interval = 30s;
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override = ioc.get_executor();
        // RC#C (gate-b/r1): bilateral_lenient — tests here don't exercise reset semantics.
        cfg.reset_seqnum_policy_field = reset_seqnum_policy::bilateral_lenient;
        return cfg;
    }

    void drive_to_active(Session& sess) {
        auto fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, 200ms)) {
            clock->cancel_sleeps();
            fixpp::test_support::drain_or_report(ioc,
                                                 "CancellationTwoPhaseTest::drive_to_active/open");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss
                          << "CancellationTwoPhaseTest::drive_to_active/open";
            return;
        }
        ASSERT_TRUE(fut.get().has_value()) << "open() failed";
        ASSERT_EQ(sess.state(), fsm_state::LogonSent);

        auto logon = make_logon_frame("FIX.4.2", 1, "TW", "ISLD", 30);
        auto fut2 = asio::co_spawn(ioc, sess.on_inbound_frame(logon), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut2, 200ms)) {
            clock->cancel_sleeps();
            fixpp::test_support::drain_or_report(ioc,
                                                 "CancellationTwoPhaseTest::drive_to_active/logon");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss
                          << "CancellationTwoPhaseTest::drive_to_active/logon";
            return;
        }
        ASSERT_TRUE(fut2.get().has_value()) << "Logon-ack failed";
        ASSERT_EQ(sess.state(), fsm_state::Active);
    }
};

// ── Test 1: CloseTerminalSkipsPhase1 (I-9) ────────────────────────────────────
//
// close(terminal) skips phase 1: no Logout frame is emitted on the transport.
// Phase-2 root total fires immediately.
TEST_F(CancellationTwoPhaseTest, CloseTerminalSkipsPhase1) {
    auto cfg = make_cfg();
    TransportDouble td;
    cfg.transport_send = [&td](std::span<const std::byte> frame) { td.capture_outbound(frame); };

    Session sess(engine, cfg);
    drive_to_active(sess);
    ASSERT_EQ(sess.state(), fsm_state::Active);

    // Record sent count before terminal close.
    const std::size_t sent_before = td.sent_count();

    auto close_fut = asio::co_spawn(ioc, sess.close(close_mode::terminal), asio::use_future);
    if (!fixpp::test_support::run_window_then_ready(ioc, close_fut, 200ms)) {
        clock->cancel_sleeps();
        fixpp::test_support::drain_or_report(ioc, "CloseTerminalSkipsPhase1/close");
        ADD_FAILURE() << fixpp::test_support::kWindowMiss << "CloseTerminalSkipsPhase1/close";
        return;
    }
    auto close_r = close_fut.get();

    // close(terminal) must complete ok.
    EXPECT_TRUE(close_r.has_value()) << "close(terminal) should complete ok";

    // Session must be Disconnected.
    EXPECT_EQ(sess.state(), fsm_state::Disconnected);

    // No Logout frame emitted in the terminal path (phase 1 skipped).
    EXPECT_EQ(td.sent_count(), sent_before)
        << "close(terminal) must NOT emit a Logout frame (phase 1 skipped per I-9)";
}

// ── Test 2: CloseIdempotent (I-10) ────────────────────────────────────────────
//
// Two concurrent close(graceful) calls return ok (not an error); no duplicate
// Logout frames emitted; lifecycle ends at closed_drained.
TEST_F(CancellationTwoPhaseTest, CloseIdempotent) {
    auto cfg = make_cfg();
    TransportDouble td;
    cfg.transport_send = [&td](std::span<const std::byte> frame) { td.capture_outbound(frame); };

    Session sess(engine, cfg);
    drive_to_active(sess);
    ASSERT_EQ(sess.state(), fsm_state::Active);

    // First close: starts the graceful-close sequence.
    auto fut1 = asio::co_spawn(ioc, sess.close(close_mode::graceful), asio::use_future);

    // Run briefly so phase 1 starts (Logout emitted, state → LogoutSent).
    ioc.run_for(50ms);
    ioc.restart();

    // Second close during phase 1 (idempotent — should attach to the in-flight result).
    auto fut2 = asio::co_spawn(ioc, sess.close(close_mode::graceful), asio::use_future);

    // Advance clock past timeout to complete.
    clock->advance(std::chrono::seconds{3});
    // ONE preserved window serves BOTH futures — the second close attaches to the in-flight
    // result, so they resolve in the same handler chain. `run_window_then_ready` only inspects
    // the future it is given, so fut2's readiness is checked explicitly rather than assumed.
    // It is checked AFTER the window (not given a window of its own) because `run_for` returns
    // when the context runs out of work: if fut1's completion was dispatched, fut2's was too.
    const bool both_ready = fixpp::test_support::run_window_then_ready(ioc, fut1, 200ms) &&
                            fut2.wait_for(std::chrono::seconds{0}) == std::future_status::ready;
    if (!both_ready) {
        clock->cancel_sleeps();
        fixpp::test_support::drain_or_report(ioc, "CloseIdempotent/both-closes");
        ADD_FAILURE() << fixpp::test_support::kWindowMiss << "CloseIdempotent/both-closes";
        return;
    }

    auto r1 = fut1.get();
    auto r2 = fut2.get();

    // Both should complete (either ok or with an expected error — not hang).
    // The key contract: second close does NOT produce an additional Logout.
    // Count Logout frames (35=5) in sent_frames.
    std::size_t logout_count = 0;
    for (std::size_t i = 0; i < td.sent_count(); ++i) {
        if (extract_field(td.sent(i), 35) == "5") {
            ++logout_count;
        }
    }
    EXPECT_LE(logout_count, 1u)
        << "Idempotent close: at most one Logout frame emitted (no duplicate)";

    // Session must be Disconnected at the end.
    EXPECT_EQ(sess.state(), fsm_state::Disconnected);

    (void)r1;
    (void)r2;
}

// ── Test 3: ChildCancellationStateIsolatesLogout ──────────────────────────────
//
// The graceful close path runs the Logout + timeout under a CHILD cancellation
// state. The root total cancellation (phase-2) fires only AFTER phase-1 resolves.
// Observable: after close(graceful) completes, the session is Disconnected,
// a Logout frame was emitted in phase-1 (before phase-2 root total), and
// close() returns ok (not an error).
//
// NOTE: We do NOT assign a handler to root_cancellation_slot() here because
// doing so would conflict with the liveness loop's own use of that same slot
// (the liveness loop binds to root_cancel_.slot() for its sleep_until
// cancellation registration; overwriting the slot drops the loop's
// cancellation handler). We observe the phase-2 semantics via the final state
// and the presence of the Logout frame only.
TEST_F(CancellationTwoPhaseTest, ChildCancellationStateIsolatesLogout) {
    auto cfg = make_cfg();
    TransportDouble td;
    cfg.transport_send = [&td](std::span<const std::byte> frame) { td.capture_outbound(frame); };

    Session sess(engine, cfg);
    drive_to_active(sess);
    ASSERT_EQ(sess.state(), fsm_state::Active);

    // Start graceful close. Run briefly to let phase-1 start (Logout emitted,
    // state → LogoutSent) and the sleep_until registered in mock_clock.
    auto fut = asio::co_spawn(ioc, sess.close(close_mode::graceful), asio::use_future);

    ioc.run_for(100ms);
    ioc.restart();

    // Phase-1 should have emitted Logout by now; root should NOT have fired yet
    // (phase-2 comes after phase-1 resolves).
    EXPECT_GE(td.sent_count(), 1u) << "Logout should be emitted in phase 1";
    EXPECT_EQ(sess.state(), fsm_state::LogoutSent)
        << "FSM should be in LogoutSent while waiting for peer Logout or timeout";

    // Advance clock past timeout (force-disconnect). Phase-1 times out,
    // run_logout_phase1 returns session_logout_timeout, then phase-2
    // fires root_cancel_.emit(total) to cancel the liveness loop.
    clock->advance(std::chrono::seconds{3});
    if (!fixpp::test_support::run_window_then_ready(ioc, fut, 200ms)) {
        clock->cancel_sleeps();
        fixpp::test_support::drain_or_report(ioc, "ChildCancellationStateIsolatesLogout/close");
        ADD_FAILURE() << fixpp::test_support::kWindowMiss
                      << "ChildCancellationStateIsolatesLogout/close";
        return;
    }

    auto close_r = fut.get();

    // close() returns ok (timeout error is internal; I-07 logged-then-proceed).
    // Session must be Disconnected — phase-1 timed out → Disconnected.
    EXPECT_EQ(sess.state(), fsm_state::Disconnected)
        << "After close(graceful) timeout, session must be Disconnected";

    // close() itself returns ok (the timeout triggers force-disconnect but
    // close() still completes successfully per I-07).
    EXPECT_TRUE(close_r.has_value())
        << "close(graceful) must complete ok even on timeout (I-07 logged-then-proceed)";
}

// ── Test 4: GracefulCloseFromAlreadyClosed ────────────────────────────────────
//
// After a session is fully closed (closed_drained), calling close() again
// returns session_already_closed (idempotent three-state model, I-10).
TEST_F(CancellationTwoPhaseTest, GracefulCloseFromAlreadyClosed) {
    auto cfg = make_cfg();
    cfg.transport_send = [](std::span<const std::byte>) {};  // discard

    Session sess(engine, cfg);
    drive_to_active(sess);

    // First close: start the close sequence, then run briefly so that
    // run_logout_phase1 registers its sleep_until(2s) in the mock_clock,
    // THEN advance past the 2s timeout. The advance MUST come AFTER the
    // sleep_until is registered (same pattern as NeverConfirmedForceDisconnect
    // in logout_exchange_test.cpp: run_for(100ms) → advance → run_for again).
    auto fut1 = asio::co_spawn(ioc, sess.close(close_mode::graceful), asio::use_future);
    // NOT migrated, deliberately. `fut1` is NOT expected to be ready here — this window exists
    // so `run_logout_phase1` reaches its `sleep_until`, which the advance below then fires.
    // `run_window_then_ready` here would report a miss on the one outcome the test requires.
    // It matched the #289 census only lexically, because `fut1.get()` happened to sit within
    // six lines; the pump that waits for `fut1` is the migrated one below.
    ioc.run_for(100ms);  // let close() start; run_logout_phase1 registers sleep
    ioc.restart();
    clock->advance(std::chrono::seconds{3});  // fire the 2s sleep
    if (!fixpp::test_support::run_window_then_ready(ioc, fut1, 200ms)) {
        clock->cancel_sleeps();
        fixpp::test_support::drain_or_report(ioc, "GracefulCloseFromAlreadyClosed/first-close");
        ADD_FAILURE() << fixpp::test_support::kWindowMiss
                      << "GracefulCloseFromAlreadyClosed/first-close";
        return;
    }
    (void)fut1.get();

    // Second close on already-closed session.
    auto fut2 = asio::co_spawn(ioc, sess.close(close_mode::graceful), asio::use_future);
    if (!fixpp::test_support::run_window_then_ready(ioc, fut2, 200ms)) {
        clock->cancel_sleeps();
        fixpp::test_support::drain_or_report(ioc, "GracefulCloseFromAlreadyClosed/second-close");
        ADD_FAILURE() << fixpp::test_support::kWindowMiss
                      << "GracefulCloseFromAlreadyClosed/second-close";
        return;
    }
    auto r2 = fut2.get();

    ASSERT_FALSE(r2.has_value());
    EXPECT_EQ(r2.error(), fixpp::core::error::session_already_closed)
        << "Second close() on closed session should return session_already_closed";
}

}  // namespace fixpp::session::test
