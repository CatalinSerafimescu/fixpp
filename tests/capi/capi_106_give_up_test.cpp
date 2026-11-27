// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/capi/capi_106_give_up_test.cpp — 106-logon-guards-write-bounds (contract C-6; spec FR-009):
// the C-ABI witness of two observers of the give-up with a write blocked, driven through the public
// C ABI only.
//
// One C-ABI initiator on the real-time clock, HeartBtInt H = 1 s, so test_request_threshold
// T = 1.2 s (B-105-5), and logout_disconnect_timeout_ms B = 2000 ms, the default, which no C setter
// changes. Its raw peer's receive buffer is set small before it listens; the peer answers the
// Logon, then never reads. A second thread calls fixpp_session_send with frames near the session's
// outbound ceiling until one call returns an error: once the peer's window and the engine's socket
// buffer are full, a call's write is blocked.
//
// While the flood fills the send path the peer writes a Heartbeat every kKeepalive, so the
// session's inbound is never silent for T and its liveness loop writes no TestRequest, however
// long the fill takes. Once one call has been in flight for kParkedFor, and the last Heartbeat was
// written kParkedFor ago, the peer writes one NewOrderSingle, the re-base: its Span (before the
// write, and after the receive callback saw it) brackets L, the session's last inbound instant,
// and the peer then writes nothing more. The parked call started before L, so the liveness loop's
// next emit after L queues behind its write, and the give-up instant is G = L + 3T + 1 ns; the
// close follows at G + B. A call still in flight for kParkedFor that then completes makes the cell
// re-base again, and one that completes after the cell has stopped watching is not the call that
// returns, which the identity clause reports.
//
// fixpp_session_is_established is sampled once, at lo + 2T, lo and hi the re-base's Span. The cell
// asserts:
//   - reach: the returning call is the one in flight at the re-base and started before lo, the
//     sample ended before lo + 3T, and it read true;
//   - the returning call's result was FIXPP_ERR_CANCELLED (B-105-8's value), no earlier than
//     lo + 3T + B and no later than hi + 3T + B + kMargin; with the reach clauses, the call was in
//     flight across the sample and up to the give-up, and the lower side shows the session read
//     the re-base (without it, L is the last keepalive, written kParkedFor or more before lo);
//   - fixpp_session_is_established read false after the ending.
// The instants are the C++ cells' (GiveUp106.* in ctest session_106_tests); the band's upper side
// here separates "returns" from "hangs". SendFlood's destructor disconnects the peer before the
// engine is destroyed, so a call still parked then fails on the reset connection rather than
// holding the destroy.
//
// CONDITIONS this cell depends on, checked by reading: the loopback peer's window closes once its
// receive buffer is full and the engine's send buffer then fills, so a write parks; a platform
// whose loopback buffering never parks a write fails the setup rather than passing. The session
// reads inbound frames while a write is parked. kParkedFor is under kKeepalive, and kKeepalive
// plus kParkedFor under T. kParkedFor is longer than one completing call on the slowest lane, or
// the cell re-bases more than once (a slower setup, not a weaker check).

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "fix/c_api/session.h"
#include "recovery_liveness_capi_support.hpp"

namespace {

namespace rl = fixpp::capi_test::rl;
using rl::steady;
using rl::time_point;

constexpr std::uint32_t kHeartbeatS = 1;
constexpr std::chrono::milliseconds kThreshold{1200};      // 1.2 x HeartBtInt (B-105-5)
constexpr std::chrono::milliseconds kLogoutTimeout{2000};  // logout_disconnect_timeout_ms's default
// Scheduling margin past G + B. Re-derive by reading returned_at - (hi + 3T + B) in this cell's
// failure message on the slowest lane it runs on (asan, tsan, MSVC debug) and keeping this above
// it.
constexpr std::chrono::milliseconds kMargin{3000};
// How often the peer writes a Heartbeat while it waits for the park.
constexpr std::chrono::milliseconds kKeepalive{400};
// How long one call stays in flight, with no Heartbeat written, before the cell takes it as parked
// and re-bases.
constexpr std::chrono::milliseconds kParkedFor{300};
// How long the cell waits for a call of the flood to park; longer than the fill on the slowest lane
// the cell runs on, which the setup failure's message reports by its count of returned calls.
constexpr std::chrono::milliseconds kParkBudget{20000};
// The peer's SO_RCVBUF.
constexpr int kPeerReceiveBuffer = 4096;
// The Text(58) padding of each flood frame, which keeps the frame under the session's outbound
// ceiling (Session::kSendFrameBytes).
constexpr std::size_t kPadding = 3500;

// fixpp_session_send on a second thread, called with one frame after another until a call returns
// an error or the flood is stopped.
class SendFlood {
public:
    SendFlood(rl::CSession& s, rl::PeerLine& line) : line_(line) {
        thread_ = std::thread([this, handle = s.handle] { run(handle); });
    }
    SendFlood(SendFlood const&) = delete;
    SendFlood& operator=(SendFlood const&) = delete;
    ~SendFlood() {
        stop_.store(true, std::memory_order_release);
        if (!done_.load(std::memory_order_acquire)) line_.sock.disconnect();
        thread_.join();
    }

    // Polls until the flood's last call returned an error, or until `until`.
    [[nodiscard]] bool wait_done(time_point until) const {
        for (;;) {
            if (done_.load(std::memory_order_acquire)) return true;
            if (steady::now() >= until) return false;
            std::this_thread::sleep_for(rl::kSlice);
        }
    }

    // Valid once done: the last call's result, when it started and returned, and its index (calls
    // returned before it).
    [[nodiscard]] fixpp_error_t rc() const { return rc_; }
    [[nodiscard]] time_point started_at() const { return started_at_; }
    [[nodiscard]] time_point returned_at() const { return returned_at_; }
    [[nodiscard]] std::uint64_t last_call() const { return last_call_; }
    [[nodiscard]] std::uint64_t calls() const { return calls_.load(std::memory_order_acquire); }
    [[nodiscard]] bool done() const { return done_.load(std::memory_order_acquire); }

    // The call in flight now: its index and when it started.
    struct InFlight {
        std::uint64_t call = 0;
        time_point started{};
    };
    [[nodiscard]] std::optional<InFlight> in_flight() const {
        std::scoped_lock const lock{mu_};
        return in_flight_;
    }

private:
    void run(fixpp_session_t* handle) {
        for (std::uint64_t n = 0; !stop_.load(std::memory_order_acquire); ++n) {
            std::string const p =
                fixpp::capi_test::ifd::order_payload("F106-" + std::to_string(n)) +
                "58=" + std::string(kPadding, 'x') + "\x01";
            auto const start = steady::now();
            {
                std::scoped_lock const lock{mu_};
                in_flight_ = InFlight{n, start};
            }
            fixpp_error_t const rc =
                fixpp_session_send(handle, reinterpret_cast<const uint8_t*>(p.data()), p.size());
            auto const at = steady::now();
            {
                std::scoped_lock const lock{mu_};
                in_flight_.reset();
            }
            if (rc != FIXPP_ERR_OK) {
                rc_ = rc;
                started_at_ = start;
                returned_at_ = at;
                last_call_ = n;
                done_.store(true, std::memory_order_release);
                return;
            }
            calls_.store(n + 1, std::memory_order_release);
        }
        done_.store(true, std::memory_order_release);
    }

    rl::PeerLine& line_;
    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> done_{false};
    std::atomic<std::uint64_t> calls_{0};
    mutable std::mutex mu_;
    std::optional<InFlight> in_flight_;
    fixpp_error_t rc_ = FIXPP_ERR_OK;
    time_point started_at_{};
    time_point returned_at_{};
    std::uint64_t last_call_ = 0;
};

[[nodiscard]] long long ms_of(steady::duration d) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(d).count();
}

// The re-base: the peer's last frame, written once one call of the flood has been in flight for
// kParkedFor, and that call's index.
struct Rebase {
    rl::Span at;
    std::uint64_t call = 0;
};

// Writes the peer's keepalive Heartbeats until one call of the flood has been in flight for
// kParkedFor, and the last Heartbeat was written kParkedFor ago, then the re-base; returns it once
// the same call is still in flight kParkedFor after the re-base was received. A call that completes
// in the meantime makes it re-base again. Nullopt when the flood ends, a write or the re-base's
// receipt fails, or no park holds within kParkBudget.
std::optional<Rebase> rebase_once_parked(rl::CSession& s, rl::PeerLine& line,
                                         SendFlood const& flood) {
    auto const until = steady::now() + kParkBudget;
    auto keepalive_at = steady::now() - kKeepalive;
    std::optional<Rebase> rebase;
    for (std::uint64_t k = 0;;) {
        auto const now = steady::now();
        if (flood.done() || now >= until) return std::nullopt;
        auto const call = flood.in_flight();
        if (rebase) {
            if (call && call->call == rebase->call) {
                if (now - rebase->at.hi >= kParkedFor) return rebase;
            } else {
                rebase.reset();
            }
        }
        if (!rebase && call && now - call->started >= kParkedFor &&
            now - keepalive_at >= kParkedFor) {
            std::string const id = "R106-" + std::to_string(k++);
            Rebase next{{steady::now(), {}}, call->call};
            if (!line.write(line.wire.order(line.next_seq++, id))) return std::nullopt;
            while (!s.rec.received_id(id)) {
                if (steady::now() >= until) return std::nullopt;
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
            }
            next.at.hi = steady::now();
            rebase = next;
            continue;
        }
        if (!rebase && now - keepalive_at >= kKeepalive) {
            if (!line.write(line.wire.heartbeat(line.next_seq++))) return std::nullopt;
            keepalive_at = now;
        }
        std::this_thread::sleep_for(rl::kSlice);
    }
}

// Contract C-6: fixpp_session_send's 1.19 clause (a send blocked at the give-up's close returns
// FIXPP_ERR_CANCELLED) and fixpp_session_is_established's (false at the give-up's ending with a
// write blocked).
TEST(Capi106GiveUp, SendBlockedAtGiveUp_ReturnsCancelled_IsEstablishedFalse) {
    rl::Dictionary dict;
    ASSERT_NE(dict.handle, nullptr) << "setup: FIX44.xml did not load";
    rl::PeerLine line{rl::Wire{"INI-106", "ACC-106"}, kPeerReceiveBuffer};
    rl::CEngine engine{dict, {line.spec(kHeartbeatS)}};
    ASSERT_TRUE(engine.start()) << "setup: the engine did not start";
    rl::CSession& s = engine.session(0);
    ASSERT_TRUE(rl::answer_logon(s, line, kHeartbeatS).has_value())
        << "setup: the session did not establish";

    SendFlood flood{s, line};
    auto const rebase = rebase_once_parked(s, line, flood);
    if (!rebase) {
        ADD_FAILURE() << "setup: no call of the flood parked within the budget, or the re-base "
                         "failed; calls returned "
                      << flood.calls() << ", flood ended " << flood.done();
        return;
    }
    auto const& at = rebase->at;
    auto const give_up_lo = at.lo + 3 * kThreshold;
    auto const close_lo = give_up_lo + kLogoutTimeout;
    auto const close_hi = at.hi + 3 * kThreshold + kLogoutTimeout;

    // One sample of fixpp_session_is_established, between lo + T and lo + 3T, bracketed by two
    // readings of the clock; the reach clauses below place it inside the returning call.
    std::this_thread::sleep_until(at.lo + 2 * kThreshold);
    auto const sample_from = steady::now();
    bool const established_while_parked = s.established();
    auto const sample_to = steady::now();

    EXPECT_LT(sample_to, give_up_lo) << "setup: the sample ended after lo + 3T";
    EXPECT_TRUE(established_while_parked)
        << "fixpp_session_is_established read false while the send was parked, before L + 3T";

    bool const returned = flood.wait_done(close_hi + kMargin);
    EXPECT_TRUE(returned)
        << "the parked send did not return by hi + 3T + B + margin; calls returned "
        << flood.calls();
    if (returned) {
        // The readings kMargin's recipe and the reach condition are re-derived from, on every run.
        std::cout << "[capi_106_give_up] returning call " << flood.last_call()
                  << " (in flight at the re-base: call " << rebase->call << ") started at lo - "
                  << ms_of(at.lo - flood.started_at()) << " ms; returned at hi + 3T + B + "
                  << ms_of(flood.returned_at() - close_hi) << " ms\n";
        EXPECT_EQ(flood.last_call(), rebase->call)
            << "setup: the returning call is not the one in flight at the re-base";
        EXPECT_LT(flood.started_at(), at.lo)
            << "setup: the returning call started after the re-base";
        EXPECT_LT(flood.started_at(), sample_from)
            << "setup: the returning call started after fixpp_session_is_established was sampled";
        EXPECT_EQ(flood.rc(), FIXPP_ERR_CANCELLED) << "the parked send's result";
        EXPECT_GE(flood.returned_at(), close_lo - rl::kSlice)
            << "returned " << ms_of(close_lo - flood.returned_at()) << " ms before lo + 3T + B";
        EXPECT_LE(flood.returned_at(), close_hi + kMargin)
            << "returned " << ms_of(flood.returned_at() - close_hi) << " ms after hi + 3T + B";
    }
    rl::EstablishedWatch watch{s};
    watch.watch_until(steady::now() + rl::kStepBudget);
    EXPECT_TRUE(watch.first_false.has_value())
        << "fixpp_session_is_established still read true after the ending";
}

}  // namespace
