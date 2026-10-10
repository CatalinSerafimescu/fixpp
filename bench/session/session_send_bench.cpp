// SPDX-License-Identifier: AGPL-3.0-or-later
//
// bench/session/session_send_bench.cpp
//
// fixpp#544 (B35; `.specify/544-hot-path-zero-alloc.md` §2.5; owner ruling R-11):
// sequential `Session::send` calls on one Active session over a bounded MemoryStore. It is
// the send path's instrument for the paired base-vs-candidate comparison
// (`[const §VIII.2]`, `[const §VIII.3]`). Soft/advisory: it sets no ceiling. It uses only the
// public `Session` API, so the same source builds at the merge-base.
//
// Cases: no Application, and a counting `Application::toApp`, which adds the outbound parse
// that `toApp` receives.
//
// Harness: on_inbound_frame_bench.cpp's (a mock clock on one io_context, `transport_send`
// counted). Each iteration spawns one `Session::send` with a completion handler and polls
// the io_context until it completes. The MemoryStore keeps every outbound frame, so every
// kBatch sends the session is rebuilt outside the timed region, before the store fills.
//
// Reach: after the timed loop the bench requires that every send completed with a value,
// that each one reached `transport_send`, that the session is still Active, and, with an
// Application, that toApp ran once per send. Any miss is a `SkipWithError`, never a timing.

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
#include <fixpp/session/memory_store.hpp>
#include <fixpp/session/memory_store_factory.hpp>
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

std::vector<std::byte> to_bytes(std::string_view s) {
    std::vector<std::byte> v;
    v.reserve(s.size());
    for (char c : s) {
        v.push_back(static_cast<std::byte>(c));
    }
    return v;
}

std::vector<std::byte> make_logon(std::uint32_t seq) {
    std::string body;
    body += "35=A\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=TW\x01";
    body += "52=20240101-00:00:00.000\x01";
    body += "56=ISLD\x01";
    body += "98=0\x01";
    body += "108=30\x01";

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
    return to_bytes(msg);
}

// A NewOrderSingle body, as an application hands it to Session::send.
std::vector<std::byte> const& nos_payload() {
    static std::vector<std::byte> const p = to_bytes(
        "35=D\x01"
        "11=ORD001\x01"
        "21=1\x01"
        "38=100\x01"
        "40=2\x01"
        "44=10.25\x01"
        "54=1\x01"
        "55=IBM\x01"
        "60=20240101-00:00:00.000\x01");
    return p;
}

class CountingApplication final : public fixpp::session::Application {
public:
    fixpp::core::expected_t<void> toApp(
        const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& /*msg*/,
        const fixpp::session::SessionId& /*id*/) override {
        ++to_app;
        return {};
    }
    std::size_t to_app = 0;
};

enum class app_kind : int { none = 0, to_app = 1 };

// Upper bound on io_context polls for one operation to complete; a send over a MemoryStore
// and a counting transport_send never waits on a timer or on I/O, so reaching it is a miss.
constexpr int kMaxPolls = 10000;
// Sends per session. It must not exceed the store's outbound capacity.
constexpr std::size_t kBatch = 4096;

struct SendBench {
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clk;
    fixpp::core::EngineConfig engine;
    std::shared_ptr<CountingApplication> app;
    std::size_t sent = 0;
    std::unique_ptr<fixpp::session::Session> sess;
    bool done = false;
    bool ok = false;

    explicit SendBench(app_kind kind) {
        auto utc = std::chrono::system_clock::time_point{} + std::chrono::seconds{1704067200};
        clk = std::make_shared<fixpp::core::mock_clock>(utc, fixpp::core::steady_time_point{},
                                                        ioc.get_executor());
        engine.clock = clk;
        engine.executor = ioc.get_executor();
        if (kind == app_kind::to_app) {
            app = std::make_shared<CountingApplication>();
            engine.application = app;
        }
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

    template <class Awaitable>
    bool run(Awaitable a) {
        done = false;
        ok = false;
        asio::co_spawn(ioc, std::move(a),
                       [this](std::exception_ptr e, fixpp::core::expected_t<void> r) {
                           ok = !e && r.has_value();
                           done = true;
                       });
        return pump() && ok;
    }

    bool send(std::span<const std::byte> payload) { return run(sess->send(payload)); }

    // Initiator path: open() sends a Logon, the peer's Logon reply makes it Active. A
    // previous session, if any, is destroyed first, with its store.
    bool setup() {
        sess.reset();
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
        fixpp::session::MemoryStore::Config store_cfg;
        store_cfg.inbound_capacity = 16;
        store_cfg.outbound_capacity = kBatch + 16;
        store_cfg.max_frame_bytes = 1024;
        cfg.store_factory = std::make_shared<fixpp::session::MemoryStoreFactory>(store_cfg);
        sess = std::make_unique<fixpp::session::Session>(engine, cfg);

        if (!run(sess->open())) {
            return false;
        }
        auto const logon = make_logon(1);
        if (!run(sess->on_inbound_frame(logon))) {
            return false;
        }
        sent = 0;
        if (app) {
            app->to_app = 0;
        }
        return sess->state() == fixpp::session::fsm_state::Active;
    }
};

void BM_Session_Send_Sequential(benchmark::State& state) {
    auto const kind = static_cast<app_kind>(state.range(0));
    SendBench b{kind};
    if (!b.setup()) {
        state.SkipWithError("session did not reach Active");
        return;
    }
    std::span<const std::byte> const payload{nos_payload()};

    std::size_t in_batch = 0;
    std::size_t sends = 0;
    std::size_t transmitted = 0;
    std::size_t to_app_calls = 0;
    bool all_ok = true;
    bool active = true;

    // `_` is the google-benchmark loop idiom: the loop runs for the iteration, not the value.
    // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores)
    for (auto _ : state) {
        if (in_batch == kBatch) {
            state.PauseTiming();
            transmitted += b.sent;
            to_app_calls += b.app ? b.app->to_app : 0;
            active = active && b.sess->state() == fixpp::session::fsm_state::Active;
            all_ok = b.setup() && all_ok;
            in_batch = 0;
            state.ResumeTiming();
        }
        all_ok = b.send(payload) && all_ok;
        ++in_batch;
        ++sends;
    }
    transmitted += b.sent;
    to_app_calls += b.app ? b.app->to_app : 0;
    active = active && b.sess->state() == fixpp::session::fsm_state::Active;

    if (!all_ok) {
        state.SkipWithError("a send or a session rebuild did not complete with a value");
    } else if (!active) {
        state.SkipWithError("session left Active");
    } else if (transmitted != sends) {
        state.SkipWithError("a send did not reach transport_send");
    } else if (kind == app_kind::to_app && to_app_calls != sends) {
        state.SkipWithError("toApp did not run once per send");
    }
}
BENCHMARK(BM_Session_Send_Sequential)
    ->ArgName("app")
    ->Arg(static_cast<int>(app_kind::none))
    ->Arg(static_cast<int>(app_kind::to_app));

}  // namespace
