// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_536_shared_clock_liveness.cpp
//
// fixpp#536 (batch B28) — reproduction only, no fix.
//
// Claim under test: `Clock::cancel_sleeps()` is clock-wide, and sessions on one
// engine share that clock, so one session's close (or its LogoutSent
// confirmation) ends the liveness loop of every OTHER session on the clock.
//
// Shape (the issue's first acceptance item):
//   Two sessions, A and B, both Active with HeartBtInt = 1 s, both peers quiet.
//   A is closed; the cell then waits for B's Heartbeat(35=0) and TestRequest(35=1),
//   which are due one HeartBtInt after B's Logon.
//
// The clock is the real `system_clock_source`, never `mock_clock`: the issue is
// about which sleepers the real clock's sweep reaches, and a mock clock has its
// own sweep implementation.
//
// Cells, each with the trigger varied and everything else held fixed:
//   NoClose_SharedClock             control: no trigger; B must beat.
//   TerminalClose_OwnClock          control: A closes on its OWN clock; B must beat.
//   GracefulConfirmed_OwnClock      control: as above, graceful + peer 35=5.
//   TerminalClose_SharedClock       claim: close(terminal) → phase-2 sweep only.
//   GracefulConfirmed_SharedClock   claim: graceful close + peer 35=5 → the
//                                   LogoutSent sweep, then the phase-2 sweep.
// The two own-clock controls attribute any silence in B to the clock being
// SHARED, not to anything else A's close does on the shared io_context.
//
// Harness note: the sessions are standalone `Session` objects on one
// `EngineConfig`, not an `Engine`. `Session::open` resolves the clock from
// `EngineConfig::clock` (`effective_clock_` in src/session/session.cpp), which is
// the same object an Engine hands every session it owns.
//
// Precondition guarded in every cell: B has emitted no Heartbeat or TestRequest
// by the time A's close completes. If B's deadline fired before the trigger, the
// cell would observe nothing about the trigger, so it fails as a harness miss
// rather than reporting either verdict.
//
// Observation budget: derived from the event that competes with the predicate —
// B's unanswered-TestRequest disconnect, two HeartBtInt after its Logon — plus a
// margin, so a GREEN B is always seen beating before that budget runs out.

#include <gtest/gtest.h>

#include <array>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fixpp/core/clock.hpp>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/fix_time.hpp>
#include <fixpp/core/system_clock_source.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <future>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"
#include "support/pump_until_ready.hpp"

using namespace std::chrono_literals;

namespace fixpp::session::test {

namespace {

constexpr auto kHeartBtInt = std::chrono::seconds{1};
// Competing event: unanswered-TR disconnect at 2 × HeartBtInt after Logon.
constexpr auto kObserveBudget = 2 * kHeartBtInt + std::chrono::seconds{1};

// SendingTime(52) from the real clock, so the peer's frames pass the latency check.
std::string sending_time_now(fixpp::core::Clock& clock) {
    std::array<char, 32> buf{};
    auto r = fixpp::core::utc_time_to_fix_string(
        clock.now(), fixpp::core::fix_time_precision::millis, std::span<char>{buf});
    return r ? std::string{r->data(), r->size()} : std::string{};
}

std::vector<std::byte> make_frame(std::string_view msg_type, std::uint32_t seq,
                                  std::string_view sender, std::string_view target,
                                  std::string_view st52, std::string_view extra = {}) {
    std::string body;
    body += "35=" + std::string(msg_type) + "\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=" + std::string(sender) + "\x01";
    body += "52=" + std::string(st52) + "\x01";
    body += "56=" + std::string(target) + "\x01";
    body += std::string(extra);
    std::string full =
        "8=FIX.4.2\x01"
        "9=" +
        std::to_string(body.size()) + "\x01" + body;
    unsigned int cs = 0;
    for (unsigned char c : full) cs += c;
    char csbuf[8];
    std::snprintf(csbuf, sizeof(csbuf), "%03u", cs & 0xFFU);
    full += "10=" + std::string(csbuf) + "\x01";
    std::vector<std::byte> out;
    out.reserve(full.size());
    for (char c : full) out.push_back(static_cast<std::byte>(c));
    return out;
}

// Outbound frames of one session, as strings.
struct Capture {
    std::vector<std::string> frames;
    [[nodiscard]] int count(std::string_view msg_type) const {
        const std::string needle =
            "\x01"
            "35=" +
            std::string(msg_type) + "\x01";
        int n = 0;
        for (const auto& f : frames) {
            if (f.find(needle) != std::string::npos) ++n;
        }
        return n;
    }
};

enum class Trigger { none, terminal_close, graceful_confirmed };

struct Outcome {
    bool precondition_ok = false;  // B silent and Active when A's close completed
    bool beat = false;             // B emitted 35=0 AND 35=1 within the budget
    int heartbeats = 0;
    int test_requests = 0;
    fsm_state b_state = fsm_state::NotConnected;
};

class SharedClockLiveness : public ::testing::Test {
protected:
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::system_clock_source> shared_clock;
    std::shared_ptr<fixpp::core::system_clock_source> own_clock;
    fixpp::core::EngineConfig shared_engine;
    fixpp::core::EngineConfig own_engine;  // A's engine in the own-clock controls

    void SetUp() override {
        shared_clock = std::make_shared<fixpp::core::system_clock_source>(ioc.get_executor());
        own_clock = std::make_shared<fixpp::core::system_clock_source>(ioc.get_executor());
        shared_engine.clock = shared_clock;
        shared_engine.executor = ioc.get_executor();
        own_engine.clock = own_clock;
        own_engine.executor = ioc.get_executor();
    }

    SessionConfig make_cfg(std::string_view sender, std::string_view target, Capture& cap) {
        SessionConfig cfg;
        cfg.sender_comp_id = std::string(sender);
        cfg.target_comp_id = std::string(target);
        cfg.begin_string = "FIX.4.2";
        cfg.heartbeat_interval = kHeartBtInt;
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override = ioc.get_executor();
        cfg.reset_seqnum_policy_field = reset_seqnum_policy::bilateral_lenient;
        cfg.transport_send = [&cap](std::span<const std::byte> f) {
            cap.frames.emplace_back(reinterpret_cast<const char*>(f.data()), f.size());
        };
        return cfg;
    }

    // `clock` is the clock of the session the coroutine belongs to, so a miss
    // drain releases the sleeps that frame can actually be parked on.
    template <class Coro>
    fixpp::core::expected_t<void> run_sync(Coro coro, fixpp::core::Clock& clock, const char* site) {
        auto fut = asio::co_spawn(ioc, std::move(coro), asio::use_future);
        if (!fixpp::test_support::pump_until_ready(ioc, fut, site)) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, clock, site);
            ADD_FAILURE() << fixpp::test_support::kPumpBudgetMiss << site;
            return std::unexpected(fixpp::test_support::kWindowMissSentinel);
        }
        return fut.get();
    }

    // Initiator open → LogonSent → peer Logon(35=A) → Active.
    bool drive_to_active(Session& s, std::string_view us, std::string_view peer,
                         fixpp::core::Clock& clock) {
        if (!run_sync(s.open(), clock, "SharedClockLiveness::open")) return false;
        const std::string extra =
            "98=0\x01"
            "108=" +
            std::to_string(kHeartBtInt.count()) + "\x01";
        auto logon = make_frame("A", 1, peer, us, sending_time_now(clock), extra);
        (void)run_sync(s.on_inbound_frame(logon), clock, "SharedClockLiveness::logon");
        return s.state() == fsm_state::Active;
    }

    Outcome run_cell(Trigger trigger, bool a_on_own_clock) {
        Capture cap_a;
        Capture cap_b;
        auto& a_engine = a_on_own_clock ? own_engine : shared_engine;
        auto& a_clock = a_on_own_clock ? *own_clock : *shared_clock;
        auto cfg_a = make_cfg("A_US", "A_PEER", cap_a);
        auto cfg_b = make_cfg("B_US", "B_PEER", cap_b);
        Session a{a_engine, cfg_a};
        Session b{shared_engine, cfg_b};

        Outcome out;
        if (!drive_to_active(a, "A_US", "A_PEER", a_clock) ||
            !drive_to_active(b, "B_US", "B_PEER", *shared_clock)) {
            ADD_FAILURE() << "setup: a session did not reach Active";
            return out;
        }
        cap_b.frames.clear();  // drop B's Logon

        switch (trigger) {
            case Trigger::none:
                break;
            case Trigger::terminal_close:
                (void)run_sync(a.close(close_mode::terminal), a_clock,
                               "SharedClockLiveness::close_a_T");
                break;
            case Trigger::graceful_confirmed: {
                auto fut = asio::co_spawn(ioc, a.close(close_mode::graceful), asio::use_future);
                // Wait for A to send its Logout, then confirm it as the peer.
                if (!fixpp::test_support::pump_until(
                        ioc, [&] { return a.state() == fsm_state::LogoutSent; },
                        "SharedClockLiveness::await_logout_sent")) {
                    ADD_FAILURE() << "setup: A never reached LogoutSent";
                }
                auto confirm = make_frame("5", 2, "A_PEER", "A_US", sending_time_now(a_clock));
                (void)run_sync(a.on_inbound_frame(confirm), a_clock,
                               "SharedClockLiveness::confirm_a");
                if (!fixpp::test_support::pump_until_ready(ioc, fut,
                                                           "SharedClockLiveness::close_a_G")) {
                    // The close frame is still suspended on `a`, which dies before `ioc`.
                    fixpp::test_support::cancel_and_drain_or_report(
                        ioc, a_clock, "SharedClockLiveness::close_a_G");
                    ADD_FAILURE() << "setup: A's graceful close did not complete";
                } else {
                    (void)fut.get();
                }
                break;
            }
        }

        out.precondition_ok =
            cap_b.count("0") == 0 && cap_b.count("1") == 0 && b.state() == fsm_state::Active;

        out.beat = fixpp::test_support::pump_until(
            ioc, [&] { return cap_b.count("0") > 0 && cap_b.count("1") > 0; }, kObserveBudget,
            fixpp::test_support::kPumpSlice, "SharedClockLiveness::observe_b");
        out.heartbeats = cap_b.count("0");
        out.test_requests = cap_b.count("1");
        out.b_state = b.state();

        // Teardown: close whatever is still open so no liveness frame outlives its Session.
        if (trigger == Trigger::none) {
            (void)run_sync(a.close(close_mode::terminal), a_clock,
                           "SharedClockLiveness::teardown_a");
        }
        (void)run_sync(b.close(close_mode::terminal), *shared_clock,
                       "SharedClockLiveness::teardown_b");
        return out;
    }
};

void expect_b_beats(const Outcome& o, const char* cell) {
    ASSERT_TRUE(o.precondition_ok)
        << cell
        << ": harness miss — B had already beaten (or left Active) before A's close "
           "completed, so this run says nothing about the trigger";
    EXPECT_TRUE(o.beat)
        << cell << ": session B sent no Heartbeat and/or TestRequest within "
        << std::chrono::duration_cast<std::chrono::milliseconds>(kObserveBudget).count()
        << " ms of A's close (HeartBtInt = " << kHeartBtInt.count()
        << " s, peer quiet). heartbeats=" << o.heartbeats << " test_requests=" << o.test_requests
        << " B.state=" << static_cast<int>(o.b_state) << " — B's liveness loop no longer runs.";
}

}  // namespace

TEST_F(SharedClockLiveness, NoClose_SharedClock) {
    expect_b_beats(run_cell(Trigger::none, /*a_on_own_clock=*/false), "NoClose_SharedClock");
}

TEST_F(SharedClockLiveness, TerminalClose_OwnClock) {
    expect_b_beats(run_cell(Trigger::terminal_close, /*a_on_own_clock=*/true),
                   "TerminalClose_OwnClock");
}

TEST_F(SharedClockLiveness, GracefulConfirmed_OwnClock) {
    expect_b_beats(run_cell(Trigger::graceful_confirmed, /*a_on_own_clock=*/true),
                   "GracefulConfirmed_OwnClock");
}

TEST_F(SharedClockLiveness, TerminalClose_SharedClock) {
    expect_b_beats(run_cell(Trigger::terminal_close, /*a_on_own_clock=*/false),
                   "TerminalClose_SharedClock");
}

TEST_F(SharedClockLiveness, GracefulConfirmed_SharedClock) {
    expect_b_beats(run_cell(Trigger::graceful_confirmed, /*a_on_own_clock=*/false),
                   "GracefulConfirmed_SharedClock");
}

}  // namespace fixpp::session::test
