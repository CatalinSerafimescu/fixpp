// SPDX-License-Identifier: AGPL-3.0-or-later
//
// bench/session/on_inbound_frame_bench.cpp
//
// 092-garbled-frame-reject (research R-9, R-3, R-14) — `Session::on_inbound_frame`
// on CLEAN, in-sequence frames through an established (Active) session. It is the
// instrument that sees the per-frame disposition branch after the header scan and
// the in-sequence branch of `SeqnumManager::check_inbound`, which the scan-only
// bench (scan_frame_header_bench.cpp) cannot. Landed in a bench-only commit before
// any production edit, so the same source builds against the merge-base and the
// candidate for the paired comparison (`[const §VIII.2]`).
//
// Cases: an in-sequence Heartbeat (admin path) and an in-sequence NewOrderSingle
// (application path, delivered to a counting `Application::fromApp`).
//
// 093-inbound-frame-dispositions (quickstart §0.1): `..._InSequence_Validated` runs
// the same two cases with `SessionConfig::validate_inbound_messages = true`, so the
// paired comparison also covers the inbound validator's parse. Added in a
// bench-only commit before any production edit, for the same reason as above.
//
// Harness: the session test support used by tests/session/test_validate_gate_inbound.cpp
// (a mock clock on one io_context, `transport_send` captured, the real FIX44
// dictionary). Each iteration spawns one `on_inbound_frame` with a completion
// handler and polls the io_context until it completes; frames are prebuilt in
// batches outside the timed region.
//
// Reach: after the timed loop the bench requires that every frame completed with a
// value, the session is still Active, NOTHING was sent (a sequence gap or a Reject
// would send a frame), and — for the NewOrderSingle case — fromApp ran once per
// frame. Any miss is a `SkipWithError`, never a timing.

#include <benchmark/benchmark.h>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/application.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Bench target includes ${CMAKE_SOURCE_DIR} (library root); see
// bench/session/CMakeLists.txt.
#include "tests/support/fix44_dictionary.hpp"
#include "tests/support/minimal_security_profile.hpp"

using namespace std::chrono_literals;

namespace {

std::vector<std::byte> make_frame(std::string_view msg_type, std::uint32_t seq,
                                  std::string const& extra_body) {
    std::string body;
    body += "35=" + std::string(msg_type) + "\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=TW\x01";
    body += "52=20240101-00:00:00.000\x01";
    body += "56=ISLD\x01";
    body += extra_body;

    std::string msg = "8=FIX.4.4\x01";
    msg += "9=" + std::to_string(body.size()) + "\x01";
    msg += body;
    unsigned int cs = 0;
    for (unsigned char c : msg) {
        cs += c;
    }
    cs &= 0xFFU;
    char csbuf[5];
    std::snprintf(csbuf, sizeof(csbuf), "%03u", cs);
    msg += "10=" + std::string(csbuf) + "\x01";

    std::vector<std::byte> frame;
    frame.reserve(msg.size());
    for (char c : msg) {
        frame.push_back(static_cast<std::byte>(c));
    }
    return frame;
}

std::string const& nos_body() {
    static std::string const body =
        "11=ORD001\x01"
        "21=1\x01"
        "38=100\x01"
        "40=2\x01"
        "44=10.25\x01"
        "54=1\x01"
        "55=IBM\x01"
        "60=20240101-00:00:00.000\x01";
    return body;
}

class CountingApplication final : public fixpp::session::Application {
public:
    fixpp::core::expected_t<void> fromApp(
        const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& /*msg*/,
        const fixpp::session::SessionId& /*id*/) override {
        ++from_app;
        return {};
    }
    std::size_t from_app = 0;
};

enum class frame_kind : int { heartbeat = 0, nos = 1 };

// Upper bound on io_context polls for one on_inbound_frame to complete; a clean
// in-sequence frame never waits on a timer or on I/O, so reaching it is a miss.
constexpr int kMaxPolls = 10000;
constexpr std::size_t kBatch = 4096;

struct InboundBench {
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clk;
    fixpp::core::EngineConfig engine;
    std::shared_ptr<CountingApplication> app = std::make_shared<CountingApplication>();
    std::size_t sent = 0;
    std::unique_ptr<fixpp::session::Session> sess;
    bool done = false;
    bool ok = false;

    InboundBench() {
        auto utc = std::chrono::system_clock::time_point{} + std::chrono::seconds{1704067200};
        clk = std::make_shared<fixpp::core::mock_clock>(utc, fixpp::core::steady_time_point{},
                                                        ioc.get_executor());
        engine.clock = clk;
        engine.executor = ioc.get_executor();
        engine.application = app;
    }

    // Runs the io_context until the spawned operation completes or kMaxPolls is hit.
    bool pump() {
        for (int i = 0; i < kMaxPolls && !done; ++i) {
            if (ioc.stopped()) {
                ioc.restart();
            }
            ioc.poll();
        }
        return done;
    }

    bool feed(std::span<const std::byte> frame) {
        done = false;
        ok = false;
        asio::co_spawn(ioc, sess->on_inbound_frame(frame),
                       [this](std::exception_ptr e, fixpp::core::expected_t<void> r) {
                           ok = !e && r.has_value();
                           done = true;
                       });
        return pump() && ok;
    }

    // Initiator path: open() sends a Logon, the peer's Logon reply makes it Active.
    bool setup(bool validate) {
        fixpp::session::SessionConfig cfg;
        cfg.sender_comp_id = "ISLD";
        cfg.target_comp_id = "TW";
        cfg.begin_string = "FIX.4.4";
        cfg.heartbeat_interval = 30s;
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = fixpp::test_support::make_fix44_dictionary();
        cfg.executor_override = ioc.get_executor();
        cfg.transport_send = [this](std::span<const std::byte>) { ++sent; };
        // The peer Logon below carries no ResetSeqNumFlag(141).
        cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
        cfg.validate_inbound_messages = validate;
        sess = std::make_unique<fixpp::session::Session>(engine, cfg);

        done = false;
        ok = false;
        asio::co_spawn(ioc, sess->open(),
                       [this](std::exception_ptr e, fixpp::core::expected_t<void> r) {
                           ok = !e && r.has_value();
                           done = true;
                       });
        if (!pump() || !ok) {
            return false;
        }
        auto const logon = make_frame("A", 1,
                                      "98=0\x01"
                                      "108=30\x01");
        if (!feed(logon)) {
            return false;
        }
        sent = 0;
        return sess->state() == fixpp::session::fsm_state::Active;
    }
};

void fill(std::vector<std::vector<std::byte>>& batch, frame_kind kind, std::uint32_t& next_seq) {
    batch.clear();
    for (std::size_t i = 0; i < kBatch; ++i) {
        batch.push_back(kind == frame_kind::heartbeat ? make_frame("0", next_seq, {})
                                                      : make_frame("D", next_seq, nos_body()));
        ++next_seq;
    }
}

void run_in_sequence(benchmark::State& state, bool validate) {
    auto const kind = static_cast<frame_kind>(state.range(0));
    InboundBench b;
    if (!b.setup(validate)) {
        state.SkipWithError("session did not reach Active");
        return;
    }

    std::uint32_t next_seq = 2;
    std::vector<std::vector<std::byte>> batch;
    batch.reserve(kBatch);
    fill(batch, kind, next_seq);
    std::size_t idx = 0;
    std::size_t fed = 0;
    bool all_ok = true;

    // `_` is the google-benchmark loop idiom: the loop runs for the iteration, not the value.
    // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores)
    for (auto _ : state) {
        if (idx == batch.size()) {
            state.PauseTiming();
            fill(batch, kind, next_seq);
            idx = 0;
            state.ResumeTiming();
        }
        all_ok = b.feed(batch[idx]) && all_ok;
        ++idx;
        ++fed;
    }

    if (!all_ok) {
        state.SkipWithError("an on_inbound_frame did not complete with a value");
    } else if (b.sess->state() != fixpp::session::fsm_state::Active) {
        state.SkipWithError("session left Active");
    } else if (b.sent != 0) {
        state.SkipWithError("session sent a frame (sequence gap or Reject): not the clean path");
    } else if (kind == frame_kind::nos && b.app->from_app != fed) {
        state.SkipWithError("fromApp did not run once per NewOrderSingle");
    }
}

void BM_Session_OnInboundFrame_InSequence(benchmark::State& state) {
    run_in_sequence(state, /*validate=*/false);
}
BENCHMARK(BM_Session_OnInboundFrame_InSequence)
    ->ArgName("frame")
    ->Arg(static_cast<int>(frame_kind::heartbeat))
    ->Arg(static_cast<int>(frame_kind::nos));

void BM_Session_OnInboundFrame_InSequence_Validated(benchmark::State& state) {
    run_in_sequence(state, /*validate=*/true);
}
BENCHMARK(BM_Session_OnInboundFrame_InSequence_Validated)
    ->ArgName("frame")
    ->Arg(static_cast<int>(frame_kind::heartbeat))
    ->Arg(static_cast<int>(frame_kind::nos));

}  // namespace
