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
// T2b USED TO rest on an inference rather than a barrier, and #237 closed that.
// As shipped through 2026-09-06, no positive barrier existed at engine scope
// (research.md D-6.13a, settled at Gate A round 4): the client completing its
// handshake and the server then closing it after stop() was a STATED, BOUNDED
// INFERENCE that the accept loop was somewhere between the handshake and
// Session-publish — not a proof it was inside read_first_frame_bounded
// specifically, so SC-015's non-vacuity rested on T2a and T6
// (first_frame_total_cancel_tls_test.cpp), which had real barriers. That gap was
// filed as residual T046 and closed by #237; the barrier the cells now carry is
// described ONCE, in the #237 note below — do not restate its mechanism here.
//
// ⚠️ KEEP, because it does not rot and it is why the barrier took the shape it
// did: a round-3 proposal to fake a positive barrier via an inverted
// test_hook_pre_publish_ was WITHDRAWN at round 4 — it is a NEGATIVE barrier,
// an absence, satisfied just as well by a coroutine that never ran, which is
// the same shape round 4 rejected for T6's round-2 form.
//
// Deterministic promptness construction (T045/SC-016): see kPromptHandlerBudget
// below — ONE canonical statement, and the cells point at it. Both cells must
// still drive the context to stop() completion: Engine::stop() has to be
// co_await'ed to completion before the Engine is destroyed (engine.hpp), or the
// Engine and this TU's stack locals are left in an unsafe state.
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
#include <fixpp/core/clock.hpp>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/system_clock_source.hpp>
#include <fixpp/session/engine.hpp>
#include <fixpp/transport/endpoint.hpp>
#include <fixpp/transport/tls_transport.hpp>
#include <fixpp/transport/transport.hpp>
#include <memory>
#include <span>

#include "engine_loopback_harness.hpp"
#include "support/pump_until_ready.hpp"

using namespace std::chrono_literals;
using fixpp::test_support::EngineLoopbackHarness;

namespace {

// ── #237: the POSITIVE near-side barrier, built from a seam that already exists ─
//
// WHAT #237 ASKED FOR. T2b asserted that stop() reclaims an accept slot held
// inside `read_first_frame_bounded`, but nothing observed that the accept loop
// had ever GOT there: stop() can land while the server is still inside
// `async_handshake`, and every rejection arm in `run_accept_loop` is the same
// `transport->close(); continue;` (src/session/engine.cpp), so the two are
// indistinguishable from outside. The cell's claim rested on a stated inference
// — the client's handshake completed and the peer then sent nothing, so the
// server is SOMEWHERE between the handshake and the publish. That is a real
// interval, and `read_first_frame_bounded` is only one point in it.
//
// ⚠️ #237 PRICED THIS AS A 7TH MECHANISM ON THE PRODUCTION ACCEPT PATH and
// declined to buy it: "instrument the accepted transport to record
// async_read_some INITIATION". That route really does need production code —
// the accepted transport is minted by an `asio_listener` that `run_accept_loop`
// CONSTRUCTS (engine.cpp, "Build the asio_listener"), with no injection seam;
// `TransportFactory` is the initiator's, not the acceptor's. But that is not
// the only route, and the issue did not consider this one: since #377 the
// first-frame deadline runs on `EngineConfig::clock`
// (`await_deadline(clock, abs_deadline)` → `clock.sleep_until(...)`), and that
// clock IS injectable — `EngineLoopbackHarness` only supplies one when the
// caller left it null. So the barrier costs a test-side decorator and ZERO
// production mechanisms.
//
// WHY THIS IS A POSITIVE BARRIER, not the inverted hook Gate A round 4 rejected.
// It counts a call that HAPPENS — the deadline arm of the join inside
// `read_first_frame_bounded` starting its sleep — rather than observing that
// something has not happened yet. `sleeps_observed() >= 1` is satisfiable only
// by the accept loop having entered the helper and armed the join. It is the
// Clock-side analogue of `mock_transport`'s "mechanism 4"
// (`reads_observed_` incremented at INITIATION, before the latency wait) and it
// is counted at the same moment, before the wrapped sleep is awaited.
//
// ⚠️ AND IT IS PROVEN ABLE TO REPORT ABSENCE, which is the half that makes a
// counter a barrier rather than a decoration. Two arms, both below:
//   * `sleeps_observed() == 0` immediately after start() and before any client
//     connects — nothing else the engine does on this path sleeps on the Clock,
//     so a non-zero here would mean the counter is not specific to the read.
//   * `sleeps_observed() == 0` in `StopIsPromptWhileAcceptedHandshakeIsInFlight`
//     at the moment stop() is called. That cell parks the accept loop inside
//     `async_handshake` — ONE STEP BEFORE the read, which is exactly the
//     ambiguity #237 names. A counter that could not tell those two cells apart
//     would report the same value in both.
// A count with no zero arm is the shape this repo keeps paying for; do not
// delete either arm to "simplify".
class counting_clock final : public fixpp::core::Clock {
public:
    explicit counting_clock(std::shared_ptr<fixpp::core::Clock> inner) : inner_(std::move(inner)) {}

    [[nodiscard]] fixpp::core::utc_time_point now() const noexcept override {
        return inner_->now();
    }
    [[nodiscard]] fixpp::core::steady_time_point steady_now() const noexcept override {
        return inner_->steady_now();
    }
    // ⚠️ COUNT BEFORE THE co_await, not after. After is a COMPLETION count, and
    // the deadline arm of a healthy first-frame read never completes — the read
    // wins and the join cancels it — so an after-count would read 0 on exactly
    // the path this barrier exists to witness.
    [[nodiscard]] asio::awaitable<void> sleep_until(
        fixpp::core::steady_time_point deadline) override {
        sleeps_.fetch_add(1, std::memory_order_release);
        co_await inner_->sleep_until(deadline);
    }
    void cancel_sleeps() noexcept override { inner_->cancel_sleeps(); }

    [[nodiscard]] std::size_t sleeps_observed() const noexcept {
        return sleeps_.load(std::memory_order_acquire);
    }

private:
    std::shared_ptr<fixpp::core::Clock> inner_;
    std::atomic<std::size_t> sleeps_{0};
};

// Build the EngineConfig both cells use, with the counting clock installed.
// The harness injects a `system_clock_source` only when `clock` is null
// (engine_loopback_harness.hpp), so supplying one here is the whole seam.
std::shared_ptr<counting_clock> make_counting_clock(asio::any_io_executor const& exec) {
    return std::make_shared<counting_clock>(
        std::make_shared<fixpp::core::system_clock_source>(exec));
}

// ── #357: the promptness barrier — HANDLERS, not wall clock ──────────────────
//
// ⚠️ THE OLD CONSTRUCTION WAS A WALL-CLOCK COMPARISON WEARING AN ORDERING'S
// CLOTHES, and it produced a false failure in CI. It armed a real 500 ms
// `asio::steady_timer` and set a flag inside the timer's own handler if
// `stop_done` was still false. That reads as an ordering between two
// test-controlled events, but the events are a REAL TIMER and a chain of POSTED
// HANDLERS. A process-level stall of >= 500 ms landing after the arm makes the
// reactor find the timer expired and enqueue its handler AHEAD of stop()'s
// remaining strand hops — so the flag is set although stop() did no extra work.
// That is #357: the failing job ran 56 tests 0.9-1.6 s slower than a passing
// re-run of the same SHA, most of them touching no socket at all.
//
// COUNT HANDLERS INSTEAD. A stall pauses the process; it does not create work,
// so the count cannot be inflated by a slow runner. That much is immune by
// construction rather than by a wider margin.
//
// ⚠️ BUT THIS IS NOT A PROMPTNESS MEASUREMENT, and calling it one would be the
// same overclaim in a new form. What it detects is exactly one thing: a stop path
// that BUSY-SPINS a join. It works here because Engine::stop()'s step-2 and
// step-3 joins spin zero-length steady_timers while their counters are non-zero,
// so a stop that has to wait out a timeout racks up handlers while it waits.
//
// A stop that BLOCKS instead of spinning is INVISIBLE to it: `run_one()` sleeps
// on the timer, the count stays low, and the cell passes. That is measured, not
// hypothetical — the D-6.12b mutant stalls this file's accept-slot cell for
// 259 ms at 10 handlers and it goes GREEN. Do not copy this barrier into a cell
// whose stall shape is a block rather than a spin.
//
// ⚠️ AND THE COUNT IS A STEP FUNCTION, not a smooth quantity: single digits while
// no join spins, hundreds-to-thousands the moment one does. The healthy 9-10 here
// means no join is currently spinning in these cells. A future change that makes
// step 2 join a real close_async in them could push a HEALTHY run into the
// hundreds with no defect at all — so the budget has less headroom than the
// 9-vs-110,000 separation suggests. Re-measure the healthy value before trusting
// it after any change to stop()'s join structure.
//
// WHY IT DISCRIMINATES SO SHARPLY. Engine::stop()'s step-2 and step-3 joins are
// zero-length `steady_timer` waits in a loop (engine.cpp), i.e. a busy spin that
// keeps the queue non-empty. On the healthy path stop() retires after a short,
// bounded chain of handlers. When something on the stop path cannot be
// cancelled, that spin runs for the WHOLE of whatever timeout does eventually
// release it, turning a bounded chain into a five-orders-of-magnitude one.
//
// ⚠️ Two shapes that look right and are NOT, both measured here before this one
// was adopted — do not "simplify" back into either:
//   * `while (!stop_done && ioc.poll() > 0)`: `poll()` runs until the queue is
//     EMPTY, and the join spin keeps re-arming it, so a single poll() call spins
//     for the entire timeout. The RED arm PASSED with `rounds == 1`.
//   * any wall-clock deadline: that is the defect above.
//
// RE-DERIVING THE BUDGET — and the two cells are NOT in the same position.
//
// StopIsPromptWhileAcceptedHandshakeIsInFlight has a MEASURED pair: revert
// async_handshake's OUT filter in asio_tls_transport.cpp and it goes 9 -> ~110,000
// handlers. That cell's promptness leg is witnessed.
//
// ⚠️ StopReturnsPromptlyAndReclaimsAcceptSlot's PROMPTNESS LEG IS UNWITNESSED,
// and an earlier version of this note implied otherwise by naming
// async_read_some's filter as its mutant. That is FALSE and was measured false:
// reverting it left the cell green at 259 ms, and so did the D-6.12b mutant the
// cell used to claim it killed. No mutation is currently known to drive that
// cell's handler count above the budget. What still carries it is its OTHER
// assertion — the accept-slot reclaim, which observes the peer seeing a close.
// Anyone adding a witness for the promptness leg must MEASURE the pair, not
// reason about it. #359 tracked the same gap for D-6.12b and is CLOSED: its
// item 1 (T2a's own promptness construction) is done — T2a no longer arms a
// real intermediate timer, it runs on a frozen clock and asserts that the join
// retires at all, measured lethal against the bare-arm mutant. Its item 2 was
// the question this comment answers, and the answer taken was the one recorded
// here: the D-6.12b claim is DROPPED at engine scope rather than propped up
// with a new assertion. SC-015's non-vacuity rests on T2a + T6 and on the
// sibling cell below, whose mutant IS proven lethal at engine scope. Adding a
// real positive barrier here remains possible and remains unfiled — it needs a
// peer that cannot make progress, which is what that sibling cell already does.
constexpr std::size_t kPromptHandlerBudget = 1000;

struct PostHandshakeProbe {
    std::atomic<bool> closed{false};
    std::atomic<bool> done{false};
};

// Real mTLS client: connects, completes the handshake, then goes SILENT — no
// payload, no close. `self_deadline_after` is a probe-owned backstop (NOT the
// mechanism under test): if the server never closes us, WE must terminate the
// read so the test reports a clean failure instead of hanging.
asio::awaitable<void> probe_post_handshake_silent(
    asio::io_context& ioc, fixpp::transport::test::LoopbackTlsFixture& fixture, std::uint16_t port,
    std::chrono::milliseconds self_deadline_after, PostHandshakeProbe& out) {
    try {
        std::shared_ptr<fixpp::transport::Transport> client =
            fixture.make_client(ioc.get_executor());
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
    auto clock = make_counting_clock(ioc.get_executor());  // #237 — see the barrier note
    eng_cfg.clock = clock;
    auto harness = EngineLoopbackHarness::build(ioc.get_executor(), std::move(eng_cfg));
    if (!harness) {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }

    ASSERT_TRUE(harness->engine().start().has_value()) << "engine.start() failed";
    fixpp::test_support::engine_stop_guard stop_guard{*harness, ioc};  // #323
    ioc.run_for(50ms);
    ioc.restart();

    // #237 ZERO ARM — the barrier must be able to report absence. The engine is
    // started and its accept loop is parked in async_accept; no client has
    // connected. Nothing on this path sleeps on the Clock, so a non-zero here
    // would mean the counter is not specific to the first-frame deadline and
    // the >= 1 wait below would be satisfiable without the read.
    ASSERT_EQ(clock->sleeps_observed(), 0u)
        << "T2b [#237 barrier, zero arm]: the engine slept on its Clock before any client "
           "connected, so `sleeps_observed() >= 1` no longer means the accept loop reached "
           "read_first_frame_bounded. Find the other sleeper before trusting the barrier.";
    std::uint16_t const port = harness->server_endpoint().port;
    if (port == 0) {
        GTEST_SKIP() << "acceptor listener did not bind";
    }

    PostHandshakeProbe probe;
    asio::co_spawn(ioc,
                   probe_post_handshake_silent(ioc, harness->transport_fixture(), port,
                                               /*self_deadline_after=*/10s, probe),
                   asio::detached);

    // #237 POSITIVE ARM — wait for the accept loop to be OBSERVED inside
    // read_first_frame_bounded, rather than assuming it after a fixed window.
    //
    // ⚠️ WHAT THIS REPLACED: `ioc.run_for(200ms); ioc.restart();`, whose own
    // comment conceded the gap ("as far as this test can tell"). Two defects in
    // one line. It was an INFERENCE — 200 ms is not an observation, and stop()
    // could land with the loop still in async_handshake, which every arm of
    // run_accept_loop then closes identically. And it was a WALL-CLOCK FIGURE,
    // the same class that took T1/B6 red on windows-msvc-asan (run
    // 34074957982): on a runner slow enough, 200 ms does not cover a loopback
    // TLS handshake and the cell goes quietly vacuous. Staging on the observable
    // fixes both, and cannot go vacuous in silence — a barrier that is never
    // reached FAILS here instead of passing weaker.
    {
        auto const limit = std::chrono::steady_clock::now() + 10s;
        while (clock->sleeps_observed() == 0 && std::chrono::steady_clock::now() < limit) {
            ioc.run_for(5ms);
            ioc.restart();
        }
    }
    ASSERT_GE(clock->sleeps_observed(), 1u)
        << "T2b [#237 barrier, positive arm]: the accept loop never armed the first-frame "
           "deadline within the budget, so it never entered read_first_frame_bounded and "
           "everything below would be asserted about a slot held somewhere else — the exact "
           "inference #237 was filed to remove. Not a stop() defect: look at whether the "
           "client's handshake completed at all.";

    // Deterministic promptness (D-6.12b) — see the kPromptHandlerBudget note at
    // the top of this file. ⚠️ THIS USED TO ARM A REAL 500 ms steady_timer and
    // assert it had not fired before stop() completed, described in its own
    // comment as "an ordering check ... not a wall-clock comparison". It was
    // both: the ordering was between a real timer and a chain of posted
    // handlers, so a process stall set the flag with stop() doing no extra work.
    // THAT IS #357 — this cell is the one that false-failed. Counting handlers
    // measures the work stop() needed and cannot be inflated by a stalled runner.
    bool stop_done = false;
    asio::co_spawn(ioc, harness->engine().stop(), [&](std::exception_ptr ep) {
        EXPECT_FALSE(ep) << "T2b: Engine::stop() threw.";
        stop_done = true;
    });

    std::size_t handlers = 0;
    while (!stop_done) {
        ASSERT_GT(ioc.run_one(), 0u)
            << "T2b: io_context ran out of work before Engine::stop() completed — a "
            << "broken cell (mis-wired harness), not a RED proof.";
        ++handlers;
    }

    // THE promptness assertion, at engine scope.
    //
    // ⚠️ THIS CELL DOES *NOT* KILL THE BARE-DEADLINE-ARM MUTANT. The claim that
    // it did — carried here since 088 as "kills the bare-deadline-arm mutant
    // (same mutant T2a kills; D-6.12b)" — is DELETED rather than restated,
    // because it was measured false while rewriting this assertion for #357.
    //
    // ⚠️ THE MUTANT IS REAL AND IS LETHAL — JUST NOT HERE, which is why the old
    // claim was believable. Run against the FULL D-6.12b mutant (await_deadline
    // reduced to a bare `co_await timer.async_wait(use_awaitable)`, dropping
    // BOTH the total-cancel reset and redirect_error):
    //
    //     ReadFirstFrameBounded.T2a  (direct helper) FAILED at 500 ms  — kills it
    //     this cell                  (engine scope)  passed at 259 ms  — does not
    //
    // So D-6.12b is witnessed at HELPER scope and unwitnessed at ENGINE scope.
    // Tracked as a follow-up; see the #357 close-out.
    //
    // ⚠️ A PARTIAL MUTANT IS NOT THE MUTANT — recorded because it cost a wrong
    // conclusion here first. Removing only the reset (keeping redirect_error)
    // also left this cell green, and was briefly written up as "the mutant
    // survives"; the faithful mutant is the bare arm, both changes together, and
    // it is what T2a actually kills. When re-deriving, copy the mutation from
    // T2a's own header rather than paraphrasing it.
    //
    // Mutation liveness was proven, not assumed: an fprintf probe in
    // `await_deadline` printed, so the arm is compiled with the mutant AND
    // entered on this cell's path. (A `strings` check for the mutant's comment
    // was also run and returned 0 — worthless, since comments never reach a
    // binary. Named here so nobody repeats it.)
    //
    // What this cell does witness is below and in the reclaim assertion: that
    // stop() retires on a bounded chain of handlers, and that the accept slot is
    // reclaimed. The sibling cell StopIsPromptWhileAcceptedHandshakeIsInFlight
    // has a mutant that IS proven lethal at engine scope (9 handlers healthy vs
    // ~110,000 under a reverted async_handshake OUT filter).
    EXPECT_LT(handlers, kPromptHandlerBudget)
        << "T2b (SC-015 accept-slot leg): Engine::stop() needed " << handlers
        << " handlers to complete, against a budget of " << kPromptHandlerBudget << ". "
        << "That means stop() waited for a real timer to expire while its joins spun on "
        << "their zero-length ones. ⚠️ Do NOT read this as the bare-deadline-arm mutant: "
        << "the note above records that mutant MEASURED as not killing this cell (it "
        << "passed at 259 ms / 10 handlers). What this cell actually detects is any stop "
        << "path that has to wait out a timeout instead of retiring on ready work — "
        << "start by finding which timeout, not by assuming D-6.12b.";

    // Accept-slot reclaim: the peer's post-handshake read must observe a
    // server-initiated close once stop() has run.
    // (#289) The shared seam, not a seventh local spelling. This cell used to
    // carry a private `run_until` whose keep-alive answer was an in-loop
    // restart() with no work guard — one of the three divergent answers #289
    // exists to collapse. Its own comment cited engine_firstframe_test.cpp as
    // the original it was "copied rather than shared" from; that file had since
    // collapsed onto pump_until, so the copy outlived both its original and the
    // census entry that would have counted it.
    //
    // The result is DISCARDED deliberately: the assertion below is on
    // probe.closed, not probe.done. Asserting the pump's own return here would
    // add a claim this cell never made.
    (void)fixpp::test_support::pump_until(
        ioc, [&probe] { return probe.done.load(std::memory_order_acquire); }, 5s,
        fixpp::test_support::kPumpSlice, "first_frame_stop/accept_slot_reclaim");
    EXPECT_TRUE(probe.closed.load(std::memory_order_acquire))
        << "T2b (SC-015 accept-slot leg): the accept slot was not reclaimed — the peer's "
        << "post-handshake read never observed a close after Engine::stop().";
}

// ── #357: stop() promptness while an ACCEPTED TLS HANDSHAKE is in flight ─────
//
// Sibling of the cell above, one stage earlier in the accept loop. That one
// holds the accept slot in Step 3 (read_first_frame_bounded, post-handshake);
// this one holds it in Step 2 (async_handshake), which is a DIFFERENT
// cancellation path and was the one that did not honour stop()'s total.
//
// THE DEFECT THIS KILLS. asio's SSL composed operation (ssl::detail::io_op)
// builds its cancellation_state through base_from_cancellation_state's
// no-filter constructor, i.e. TERMINAL-ONLY. asio_tls_transport::async_handshake
// used to install a ONE-argument reset_cancellation_state, which sets the same
// filter as both in and out — so Engine::stop()'s cancellation_type::total was
// forwarded to the SSL op unchanged and then dropped by that terminal-only
// state. The handshake did not abort, and stop()'s Step-3 join on
// outstanding_counter_ could not retire until the accepted transport's
// tls_handshake_timeout expired. async_connect and async_read_some already
// carried the two-argument OUT filter that fixes this; handshake and write did
// not. [#357]
//
// WHY A RAW TCP PEER RATHER THAN AN mTLS ONE. The condition under test is "the
// server is SUSPENDED inside async_handshake when stop() runs". A real mTLS
// client makes that a race — it is exactly the race the sibling cell's header
// says it cannot resolve, and #357 was observed only on a runner slow enough to
// lose it. A peer that completes the TCP connect and then never sends a
// ClientHello makes it a CERTAINTY: the server's async_handshake cannot
// complete, by construction, for as long as the test cares to look. No barrier,
// no inference, no scheduler dependence.
//
// THE ASSERTION counts HANDLERS — see kPromptHandlerBudget at the top of this
// file for the mechanism, the measured separation, and the two constructions
// that were tried first and are vacuous. This cell adds nothing to that account
// and deliberately does not restate it.
//
// ⚠️ NON-VACUITY IS THE RED ARM, and it is the only thing that proves this cell
// can report non-zero. Revert either OUT filter in asio_tls_transport.cpp and
// this cell FAILS — stop() then waits out the whole handshake budget. A green
// result here without that revert having been run once is a claim about
// compilation, not about detection.
TEST(FirstFrameStop, StopIsPromptWhileAcceptedHandshakeIsInFlight) {
    asio::io_context ioc;
    fixpp::core::EngineConfig eng_cfg;
    eng_cfg.executor = ioc.get_executor();
    auto clock = make_counting_clock(ioc.get_executor());  // #237 — the absence arm
    eng_cfg.clock = clock;
    auto harness = EngineLoopbackHarness::build(ioc.get_executor(), std::move(eng_cfg));
    if (!harness) {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }

    ASSERT_TRUE(harness->engine().start().has_value()) << "engine.start() failed";
    fixpp::test_support::engine_stop_guard stop_guard{*harness, ioc};  // #323
    ioc.run_for(50ms);
    ioc.restart();
    std::uint16_t const port = harness->server_endpoint().port;
    if (port == 0) {
        GTEST_SKIP() << "acceptor listener did not bind";
    }

    // A peer that completes the TCP connect and then says NOTHING. The server
    // accepts it, enters async_handshake, and blocks there awaiting a
    // ClientHello that never comes. Held open for the whole cell — closing it
    // would let the handshake fail on EOF and dissolve the very condition under
    // test.
    asio::ip::tcp::socket silent_peer{ioc};
    {
        asio::error_code connect_ec;
        silent_peer.connect(asio::ip::tcp::endpoint{asio::ip::make_address("127.0.0.1"), port},
                            connect_ec);
        ASSERT_FALSE(connect_ec) << "raw TCP connect to the acceptor failed: "
                                 << connect_ec.message();
    }

    // Let the accept loop pick the connection up and reach async_handshake.
    ioc.run_for(200ms);
    ioc.restart();

    // #237 ABSENCE ARM — this is the run where the read is NEVER reached, and
    // the barrier the sibling cell relies on must read zero here.
    //
    // ⚠️ READ THE SCOPE, not more than it says. This does NOT positively locate
    // the accept loop in `async_handshake` — nothing here observes that, and the
    // peer holding its silence only makes the handshake unable to COMPLETE. What
    // it does establish is the half the sibling's `>= 1` needs: on a run that
    // never enters `read_first_frame_bounded`, the counter is 0. Without this,
    // `sleeps_observed() >= 1` over there could be satisfied by anything the
    // engine happens to sleep on, and would be a decoration rather than a
    // barrier. That is why the arm lives in THIS cell — it is the only one in
    // the file whose peer, by construction, can never produce a first frame.
    EXPECT_EQ(clock->sleeps_observed(), 0u)
        << "[#237 barrier, absence arm]: the Clock was slept on during a run whose peer never "
           "sent a ClientHello, so the accept loop cannot have reached read_first_frame_bounded "
           "— something ELSE sleeps on the engine Clock, and the sibling cell's "
           "`sleeps_observed() >= 1` no longer proves what it claims. Fix the barrier there "
           "before trusting either cell.";

    bool stop_done = false;
    asio::co_spawn(ioc, harness->engine().stop(), [&](std::exception_ptr ep) {
        EXPECT_FALSE(ep) << "#357: Engine::stop() threw.";
        stop_done = true;
    });

    // THE BARRIER — count HANDLERS. See kPromptHandlerBudget at the top of this
    // file for why, for the measured healthy-vs-mutant separation, and for the two
    // constructions that were tried first and are vacuous.
    //
    // ⚠️ AN EARLIER VERSION OF THIS COMMENT DESCRIBED A `poll()` LOOP and is gone
    // rather than trimmed. It did not merely fail to match the code below — it
    // recommended the one construction that was MEASURED not to work: `poll()`
    // runs until the queue is EMPTY, and stop()'s joins keep re-arming
    // zero-length timers, so a single poll() call spins for the whole timeout and
    // the RED arm PASSED with rounds == 1. A stale comment that re-proposes a
    // refuted mechanism is worse than none.
    std::size_t handlers = 0;
    while (!stop_done) {
        ASSERT_GT(ioc.run_one(), 0u)
            << "#357: io_context ran out of work before Engine::stop() completed — a "
            << "broken cell (mis-wired harness), not a RED proof.";
        ++handlers;
    }

    EXPECT_LT(handlers, kPromptHandlerBudget)
        << "#357: Engine::stop() needed " << handlers << " handlers to complete, against a "
        << "budget of " << kPromptHandlerBudget << ". The accept loop was suspended in "
        << "asio_tls_transport::async_handshake and stop()'s cancellation_type::total did "
        << "not abort it, so stop()'s Step-3 join on outstanding_counter_ spun on its "
        << "zero-length timers until the accepted transport's tls_handshake_timeout "
        << "released it. Re-check that async_handshake still installs the TWO-argument "
        << "reset_cancellation_state whose OUT filter maps any accepted cancellation to "
        << "`terminal` for the child SSL op.";

    asio::error_code ignored;
    silent_peer.close(ignored);
}
