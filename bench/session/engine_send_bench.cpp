// SPDX-License-Identifier: AGPL-3.0-or-later
//
// bench/session/engine_send_bench.cpp
//
// fixpp#563, fixpp#564 (B41; `.specify/563-564-callback-send.md` §3 "Cost (OD-5)"): the
// route of `Engine::send`, from the call to the point where `Session::send` has the
// payload, on one Active engine-managed session. It is the instrument for the paired
// base-vs-candidate comparison of that route (`[const §VIII.2]`, `[const §VIII.3]`). It
// uses the public `Engine` API and test-support headers only, so the same source builds at
// the merge-base.
//
// Harness: one `Engine` on one `io_context` that the bench thread polls, on a mock clock.
// One registered initiator whose `transport_factory_override` is
// `ScriptedReadTransportFactory` (tests/support/transport_double.hpp) over a
// `ScriptedStream` that scripts the peer's Logon and has `hold_open` set, so the session is
// published and Active with no socket, no TLS and no file, and its read pump waits.
//
// What is timed: each iteration spawns one `Engine::send` on the `io_context` with a
// completion handler and polls until it completes. The payload is a NewOrderSingle body
// whose first field is not MsgType(35). `Session::build_send_frame_` refuses it with
// `app_payload_malformed` before the sequence number is read, the frame is built or any
// callback runs, so an iteration holds `Engine::send`'s own path and `Session::send` up to
// that refusal, and nothing of the send path after it: no assignment, no store, no write.
//
// One thread: the send is started from the bench thread, outside any strand's handler. The
// bench does not time the wake-up of an io thread by a send started on another thread.
//
// A cost from outside the send path is inside each iteration. The scripted read waits on an
// `asio::steady_timer`, so the `io_context` has a reactor, and asio's scheduler runs the
// reactor with a zero timeout each time its queue reaches it: a kernel poll between the
// handlers of one send. The bench's pump cannot leave it out; removing it needs a transport
// double whose read waits without a timer. Re-derive the count per send with
// `strace -c -e trace=epoll_wait,epoll_pwait` over a run with a fixed iteration count.
//
// Reach: before the timed loop one well-formed send must complete with a value and reach
// the stream as one write. After the loop, every timed send must have returned exactly
// `app_payload_malformed`, the session must still be Active, and the stream must hold no
// write after that first send's. On this path only `build_send_frame_` returns that error
// (`grep -n "app_payload_malformed" src/session/*.cpp`), so a send refused earlier, for an
// unknown id, a session that is not Active or a stopped Engine, is a miss. Any miss is a
// `SkipWithError`, never a timing.

#include <benchmark/benchmark.h>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <fixpp/core/clock.hpp>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/engine.hpp>
#include <fixpp/session/security_profile.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fixpp/transport/endpoint.hpp>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Bench target includes ${CMAKE_SOURCE_DIR} (library root); see
// bench/session/CMakeLists.txt.
#include "tests/support/fix44_dictionary.hpp"
#include "tests/support/transport_double.hpp"

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

// The peer's Logon. SendingTime(52) is the mock clock's initial UTC.
std::vector<std::byte> make_peer_logon() {
    std::string body;
    body += "35=A\x01";
    body += "34=1\x01";
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

// A NewOrderSingle body, as an application hands it to Engine::send.
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

// The same fields with ClOrdID(11) ahead of MsgType(35): build_send_frame_ refuses a
// payload that does not begin with "35=".
std::vector<std::byte> const& refused_payload() {
    static std::vector<std::byte> const p = to_bytes(
        "11=ORD001\x01"
        "35=D\x01"
        "21=1\x01"
        "38=100\x01"
        "40=2\x01"
        "44=10.25\x01"
        "54=1\x01"
        "55=IBM\x01"
        "60=20240101-00:00:00.000\x01");
    return p;
}

// True when `frame` holds the field MsgType(35)=D.
bool is_new_order_single(std::span<const std::byte> frame) {
    constexpr std::string_view kField =
        "\x01"
        "35=D\x01";
    std::string_view const text{reinterpret_cast<const char*>(frame.data()), frame.size()};
    return text.contains(kField);
}

// Upper bound on io_context polls for one operation to complete. Nothing in this harness
// waits on time or on I/O before the Engine is stopped, so reaching it is a miss.
constexpr int kMaxPolls = 10000;
// Wall-clock bound on Engine::stop(), which joins its loops through timer waits.
constexpr auto kStopBudget = 10s;

struct EngineSendBench {
    // First member: it outlives the engine and every frame that holds a strand on it.
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clk;
    std::shared_ptr<fixpp::session::test::ScriptedStream> stream;
    std::unique_ptr<fixpp::session::Engine> engine;
    fixpp::session::SessionId id;
    bool done = false;
    bool threw = false;
    fixpp::core::expected_t<void> result;

    EngineSendBench() {
        auto utc = std::chrono::system_clock::time_point{} + std::chrono::seconds{1704067200};
        clk = std::make_shared<fixpp::core::mock_clock>(utc, fixpp::core::steady_time_point{},
                                                        ioc.get_executor());
        stream = std::make_shared<fixpp::session::test::ScriptedStream>();
        stream->hold_open = true;
        stream->chunks.push_back(make_peer_logon());
    }

    EngineSendBench(EngineSendBench const&) = delete;
    EngineSendBench& operator=(EngineSendBench const&) = delete;

    ~EngineSendBench() { (void)stop(); }

    // Polls the io_context until the spawned operation completes or kMaxPolls is hit.
    bool pump() {
        for (int i = 0; i < kMaxPolls && !done; ++i) {
            if (ioc.stopped()) {
                ioc.restart();
            }
            ioc.poll();
        }
        return done;
    }

    // Starts one Engine::send from this thread and polls until its handler has run. False
    // when it did not complete; `threw` and `result` hold its outcome otherwise.
    bool send(std::span<const std::byte> payload) {
        done = false;
        asio::co_spawn(ioc, engine->send(id, payload),
                       [this](std::exception_ptr e, fixpp::core::expected_t<void> r) {
                           threw = e != nullptr;
                           result = r;
                           done = true;
                       });
        return pump();
    }

    [[nodiscard]] bool active() const {
        auto const s = engine->lookup(id);
        return s && s->state() == fixpp::session::fsm_state::Active;
    }

    // Registers the initiator and starts the engine; true once the session is published
    // and Active and its read pump waits at the end of the script.
    bool setup() {
        fixpp::core::EngineConfig ec;
        ec.executor = ioc.get_executor();
        ec.clock = clk;
        engine = std::make_unique<fixpp::session::Engine>(ioc.get_executor(), std::move(ec));

        fixpp::session::SessionConfig cfg;
        cfg.sender_comp_id = "ISLD";
        cfg.target_comp_id = "TW";
        cfg.begin_string = "FIX.4.4";
        cfg.role = fixpp::session::session_role::initiator;
        cfg.heartbeat_interval = 30s;
        cfg.executor_override = ioc.get_executor();
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#elif defined(_MSC_VER)  // NOLINT(readability-use-concise-preprocessor-directives): C++23 form
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
        // The plaintext profile ScriptedReadTransportFactory is a factory for.
        cfg.security_profile = fixpp::session::SecurityProfile{
            fixpp::session::SecurityProfile::kind::insecure_plain_tcp};
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)  // NOLINT(readability-use-concise-preprocessor-directives): C++23 form
#pragma warning(pop)
#endif
        cfg.dictionary = fixpp::test_support::make_fix44_dictionary();
        // The peer Logon carries no ResetSeqNumFlag(141).
        cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
        // Never dialled: the scripted transport's connect completes at once.
        cfg.reconnect_endpoint = fixpp::transport::Endpoint{"127.0.0.1", 1};
        cfg.transport_send = [](std::span<const std::byte>) {};
        cfg.transport_factory_override =
            std::make_shared<fixpp::session::test::ScriptedReadTransportFactory>(stream);

        id = fixpp::session::SessionId::from_config(cfg);
        if (!engine->register_session(std::move(cfg)).has_value()) {
            return false;
        }
        if (!engine->start().has_value()) {
            return false;
        }
        for (int i = 0; i < kMaxPolls; ++i) {
            if (active() && stream->waiting != nullptr) {
                return true;
            }
            if (ioc.stopped()) {
                ioc.restart();
            }
            ioc.poll();
        }
        return false;
    }

    // Engine::stop(), polled to completion. On a miss the engine is leaked, so the frames
    // the io_context destroys later still refer to a live Engine.
    bool stop() {
        if (!engine) {
            return true;
        }
        bool stopped = false;
        asio::co_spawn(ioc, engine->stop(), [&stopped](std::exception_ptr) { stopped = true; });
        auto const deadline = std::chrono::steady_clock::now() + kStopBudget;
        while (!stopped && std::chrono::steady_clock::now() < deadline) {
            if (ioc.stopped()) {
                ioc.restart();
            }
            ioc.poll();
        }
        if (!stopped) {
            (void)engine.release();
            return false;
        }
        engine.reset();
        return true;
    }
};

void BM_Engine_Send_Route(benchmark::State& state) {
    using fixpp::core::error;

    EngineSendBench b;
    if (!b.setup()) {
        state.SkipWithError("session did not reach Active with its read pump waiting");
        return;
    }

    // Reach, before the timed loop: one well-formed send is transmitted.
    std::size_t const writes_before = b.stream->written.size();
    if (!b.send(nos_payload()) || b.threw || !b.result.has_value()) {
        state.SkipWithError("the well-formed send did not complete with a value");
        return;
    }
    if (b.stream->written.size() != writes_before + 1 ||
        !is_new_order_single(b.stream->written.back())) {
        state.SkipWithError("the well-formed send did not reach the stream as one write");
        return;
    }
    std::size_t const writes_after_first = b.stream->written.size();

    std::span<const std::byte> const payload{refused_payload()};
    std::size_t sends = 0;
    std::size_t refused = 0;

    // `_` is the google-benchmark loop idiom: the loop runs for the iteration, not the value.
    // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores)
    for (auto _ : state) {
        if (b.send(payload) && !b.threw && !b.result.has_value() &&
            b.result.error() == error::app_payload_malformed) {
            ++refused;
        }
        ++sends;
    }

    // Reach, after the timed loop. Each condition is tested on its own, and every one that
    // does not hold is named.
    std::string misses;
    if (refused != sends) {
        misses += " [a timed send did not return app_payload_malformed]";
    }
    if (!b.active()) {
        misses += " [session left Active]";
    }
    if (b.stream->written.size() != writes_after_first) {
        misses += " [the stream holds a write after the first send's]";
    }
    if (!b.stop()) {
        misses += " [Engine::stop did not complete]";
    }
    if (!misses.empty()) {
        state.SkipWithError("reach:" + misses);
    }
}
BENCHMARK(BM_Engine_Send_Route);

}  // namespace
