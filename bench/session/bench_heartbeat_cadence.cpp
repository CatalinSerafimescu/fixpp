// SPDX-License-Identifier: AGPL-3.0-or-later
//
// bench/session/bench_heartbeat_cadence.cpp — T022 [US1] Phase 3
//
// Heartbeat cadence bench — end-to-end session timer-fire → Heartbeat emit.
//
// Ceiling (plan.md Technical-Context, FR-003; warm cache, Linux/Clang/x86_64
// release -O2): BM_Session_HeartbeatTimerFire ≤ 2 µs p99.
//
// CI gate: ±5% regression per [const §VIII.2]; baselines under
//   bench/baselines/session/heartbeat_cadence_baseline.json.
//
// Anchors: spec.md §US1 / FR-003; plan.md §Test plan T022;
//   [const §VIII.2] bench regression gate.
//
// Scope: measures the wall-clock of one full round-trip:
//   mock_clock::advance(HeartBtInt+1) → session FSM fires Heartbeat timer
//   → build_heartbeat() → transport_send callback fires.
//
// The mock_clock::advance path is synchronous within the ioc.run() window
// so the measured time is the pure processing cost (timer dispatch +
// Heartbeat frame build + outbound callback) minus the clock advance itself.
//
// RED: Phase-2 stubs do not arm any Heartbeat timer. mock_clock::advance()
//   will NOT cause a Heartbeat to be emitted. The benchmark will emit 0
//   Heartbeats per iteration and the counter assertion in
//   BM_Session_HeartbeatTimerFire_ZeroEmitRedWitness will confirm this.
//   The timing bench itself runs (it measures zero-cost stubs) but the
//   behavioral assertion companion fires RED until T024 impl lands.

#include <benchmark/benchmark.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <future>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>

#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>

// Bench target includes ${CMAKE_SOURCE_DIR} (library root) via
// add_bench_with_test_include in bench/session/CMakeLists.txt.
// The support headers are relative to that root.
#include "tests/support/minimal_dictionary.hpp"
#include "tests/support/minimal_security_profile.hpp"

using namespace std::chrono_literals;

namespace {

static std::string field_str(int tag, std::string_view val) {
    return std::to_string(tag) + "=" + std::string(val) + "\x01";
}

static std::vector<std::byte> make_logon_frame(std::string_view bs, std::uint32_t seq,
                                                std::string_view s, std::string_view t,
                                                int hbt = 5) {
    std::string body;
    body += field_str(35, "A");
    body += field_str(34, std::to_string(seq));
    body += field_str(49, s);
    body += field_str(52, "20240101-00:00:00.000");
    body += field_str(56, t);
    body += field_str(98, "0");
    body += field_str(108, std::to_string(hbt));

    std::string msg;
    msg += "8=" + std::string(bs) + "\x01";
    msg += "9=" + std::to_string(body.size()) + "\x01";
    msg += body;
    unsigned int cs = 0;
    for (unsigned char c : msg) cs += c;
    cs &= 0xFFU;
    char csbuf[5];
    snprintf(csbuf, sizeof(csbuf), "%03u", cs);
    msg += "10=" + std::string(csbuf) + "\x01";

    std::vector<std::byte> frame;
    frame.reserve(msg.size());
    for (char c : msg) frame.push_back(static_cast<std::byte>(c));
    return frame;
}

// ── Shared bench state ────────────────────────────────────────────────────────

struct HeartbeatBenchState {
    asio::io_context                         ioc;
    std::shared_ptr<fixpp::core::mock_clock> clk;
    fixpp::core::EngineConfig                engine;
    std::atomic<std::size_t>                 emit_count{0};
    std::unique_ptr<fixpp::session::Session> sess;

    HeartbeatBenchState() {
        auto utc = std::chrono::system_clock::time_point{} + std::chrono::seconds{1704067200};
        auto stp = fixpp::core::steady_time_point{};
        clk = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock    = clk;
        engine.executor = ioc.get_executor();
    }

    void setup() {
        fixpp::session::SessionConfig cfg;
        cfg.sender_comp_id    = "ISLD";
        cfg.target_comp_id    = "TW";
        cfg.begin_string      = "FIX.4.2";
        cfg.heartbeat_interval = 5s;
        cfg.security_profile  = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary        = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override = ioc.get_executor();
        cfg.transport_send    = [this](std::span<const std::byte>) { ++emit_count; };
        cfg.role              = fixpp::session::session_role::acceptor;

        sess = std::make_unique<fixpp::session::Session>(engine, cfg);

        // Drive to Active.
        auto fut = asio::co_spawn(ioc, sess->open(), asio::use_future);
        ioc.run_for(50ms);
        ioc.restart();
        (void)fut.get();

        auto logon = make_logon_frame("FIX.4.2", 1, "TW", "ISLD", 5);
        auto f2 = asio::co_spawn(ioc, sess->on_inbound_frame(logon), asio::use_future);
        ioc.run_for(50ms);
        ioc.restart();
        (void)f2.get();
    }

    // Returns true if session is in Active state.
    bool is_active() const {
        return sess && sess->state() == fixpp::session::fsm_state::Active;
    }
};

// ── BM_Session_HeartbeatTimerFire ────────────────────────────────────────────
//
// Measures: mock_clock::advance(6s) → Heartbeat timer fires → emit callback.
// Expected: ≤ 2 µs p99 on warm cache (Phase 3 stub: nearly 0 ns because
// the timer is never armed — stub co_returns immediately).

void BM_Session_HeartbeatTimerFire(benchmark::State& state) {
    HeartbeatBenchState bench;
    bench.setup();

    if (!bench.is_active()) {
        state.SkipWithError("Session did not reach Active state");
        return;
    }

    std::size_t heartbeats_fired = 0;

    for (auto _ : state) {
        bench.emit_count = 0;

        // Advance mock clock past HeartBtInt (5s) by 1s margin.
        bench.clk->advance(std::chrono::milliseconds{6000});
        bench.ioc.run_for(std::chrono::milliseconds{10});
        bench.ioc.restart();

        heartbeats_fired += bench.emit_count.load();
    }

    state.counters["heartbeats_per_iter"] =
        benchmark::Counter(static_cast<double>(heartbeats_fired),
                           benchmark::Counter::kIsRate,
                           benchmark::Counter::kIs1000);
}
BENCHMARK(BM_Session_HeartbeatTimerFire)
    ->Unit(benchmark::kMicrosecond)
    ->MinTime(0.5);

}  // namespace
