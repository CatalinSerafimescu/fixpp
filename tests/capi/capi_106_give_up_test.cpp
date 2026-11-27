// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/capi/capi_106_give_up_test.cpp — 106-logon-guards-write-bounds (contract C-6; spec FR-009):
// the C-ABI witness of two observers of the give-up with a write blocked, driven through the public
// C ABI only.
//
// One C-ABI initiator on the real-time clock, HeartBtInt H = 1 s, so test_request_threshold
// T = 1.2 s (B-105-5), and logout_disconnect_timeout_ms B = 2000 ms, the default, which no C setter
// changes. Its raw peer's receive buffer is set small before it listens; the peer answers the
// Logon, then neither reads nor writes. A second thread calls fixpp_session_send with frames near
// the session's outbound ceiling until one call returns an error: once the peer's window and the
// engine's socket buffer are full, a call's write is blocked. With the liveness loop's next emit
// queued behind it, the give-up instant is G = L + 3T + 1 ns (L the session's last inbound instant,
// inside the Span answer_logon returns), and the close follows at G + B.
//
// The parked call is the flood's returning call, the one that returned an error; the cell reads its
// start once the flood has stopped, so a call that was slow but completed is never the one judged.
// fixpp_session_is_established is sampled once, at lo + 2T. The cell asserts:
//   - reach: the returning call started before lo + T (so the liveness loop's first emit after L
//     queues behind its write and the give-up instant is L + 3T + 1 ns) and before the sample, the
//     sample ended before lo + 3T, and it read true;
//   - the returning call's result was FIXPP_ERR_CANCELLED (B-105-8's value), no earlier than
//     lo + 3T + B and no later than hi + 3T + B + kMargin, lo and hi the Span's ends; with the
//     reach clauses, the call was in flight across the sample and up to the give-up;
//   - fixpp_session_is_established read false after the ending.
// The instants are the C++ cells' (GiveUp106.* in ctest session_106_tests); the band's upper side
// here separates "returns" from "hangs". SendFlood's destructor disconnects the peer before the
// engine is destroyed, so a call still parked then fails on the reset connection rather than
// holding the destroy.
//
// CONDITIONS this cell depends on, checked by reading: the loopback peer's window closes once its
// receive buffer is full and the engine's send buffer then fills, so a write parks; a platform
// whose loopback buffering never parks a write fails the reach assertion rather than passing.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
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

private:
    void run(fixpp_session_t* handle) {
        for (std::uint64_t n = 0; !stop_.load(std::memory_order_acquire); ++n) {
            std::string const p =
                fixpp::capi_test::ifd::order_payload("F106-" + std::to_string(n)) +
                "58=" + std::string(kPadding, 'x') + "\x01";
            auto const start = steady::now();
            fixpp_error_t const rc =
                fixpp_session_send(handle, reinterpret_cast<const uint8_t*>(p.data()), p.size());
            auto const at = steady::now();
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
    fixpp_error_t rc_ = FIXPP_ERR_OK;
    time_point started_at_{};
    time_point returned_at_{};
    std::uint64_t last_call_ = 0;
};

[[nodiscard]] long long ms_of(steady::duration d) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(d).count();
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
    auto const at = rl::answer_logon(s, line, kHeartbeatS);
    ASSERT_TRUE(at.has_value()) << "setup: the session did not establish";
    auto const give_up_lo = at->lo + 3 * kThreshold;
    auto const close_lo = give_up_lo + kLogoutTimeout;
    auto const close_hi = at->hi + 3 * kThreshold + kLogoutTimeout;

    SendFlood flood{s, line};
    // One sample of fixpp_session_is_established, between lo + T and lo + 3T, bracketed by two
    // readings of the clock; the reach clauses below place it inside the returning call.
    std::this_thread::sleep_until(at->lo + 2 * kThreshold);
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
                  << " started at lo + " << ms_of(flood.started_at() - at->lo)
                  << " ms; returned at hi + 3T + B + " << ms_of(flood.returned_at() - close_hi)
                  << " ms\n";
        EXPECT_LT(flood.started_at(), at->lo + kThreshold)
            << "setup: the returning call started " << ms_of(flood.started_at() - at->lo)
            << " ms after lo, not before lo + T: the loop's TestRequest may have been written";
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
