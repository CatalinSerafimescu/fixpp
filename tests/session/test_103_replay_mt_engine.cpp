// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_103_replay_mt_engine.cpp — test-only.
//
// Feature 103 (specs/103-resend-logon-reset-fidelity/), fixpp#588, R-588-h (research D5′;
// FR-014, SC-005; tasks T060): an acceptor session under an Engine whose io_context is run by
// several threads, on the plain transport, facing a raw peer socket. The peer asks for a
// resend; the session's store holds the answer at the read of slot 3; meanwhile several
// threads call Engine::send. Every frame numbered above the answer's range must reach the
// peer after the answer's last frame.
//
// Threads. The Engine runs on `ioc`, which kIoThreads threads run. The peer runs on an
// io_context of its own, on the test thread only, so its readings need no synchronisation.
// The store's hold (tests/support/recording_store.hpp) is armed before the Engine starts and
// released by a handler posted onto the session's own executor, so the hold's state is touched
// only there; every reading of the hold and of the replay gate is taken the same way.
//
// The hold is released only once every sender thread has a send waiting at the replay gate,
// read on the session's strand; a run in which they do not is a failure, not a sample. The
// readings share one budget, kPumpBudget, taken before the request that starts the hold and
// half the hold's own bound (kBodyHoldBound), so they end before the hold can end by time.
// How many repeats the race
// evidence takes is tasks T074's procedure; run it with --gtest_repeat=<n>.
#include <gtest/gtest.h>

#include <array>
#include <asio/co_spawn.hpp>
#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/post.hpp>
#include <asio/use_future.hpp>
#include <asio/write.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/fix_time.hpp>
#include <fixpp/core/system_clock_source.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/engine.hpp>
#include <fixpp/session/memory_store.hpp>
#include <fixpp/session/memory_store_factory.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/transport/endpoint.hpp>
#include <future>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include "session/plain_engine_rig.hpp"
#include "support/minimal_dictionary.hpp"
#include "support/pump_until_ready.hpp"
#include "support/recording_store.hpp"
#include "support/session_test_access.hpp"

namespace {

namespace plain_rig = fixpp::test_support::plain_rig;
using fixpp::session::direction_t;
using fixpp::test_support::RecordingStoreFactory;
using fixpp::test_support::store_op;
using fixpp::test_support::StoreCall;

constexpr int kIoThreads = 3;
constexpr int kSenderThreads = 3;
constexpr int kSendsPerThread = 5;
constexpr unsigned kStored = 3;  // application messages sent before the request, at 2 to 4
constexpr std::chrono::seconds kBudget{20};

std::string utc_now() {
    std::array<char, 32> buf{};
    auto const r = fixpp::core::utc_time_to_fix_string(std::chrono::system_clock::now(),
                                                       fixpp::core::fix_time_precision::millis,
                                                       std::span<char>{buf});
    return r ? std::string{r->data(), r->size()} : std::string{};
}

// A frame from the peer ("TW") to the session ("ISLD"), stamped with the real clock.
std::string peer_msg(std::string_view type, std::uint32_t seq, std::string_view extra = {}) {
    return plain_rig::message("FIX.4.2", type, seq, "TW", "ISLD", utc_now(), extra);
}

// The raw peer: one socket on an io_context of its own, every read bounded.
class Peer {
public:
    [[nodiscard]] bool connect(std::uint16_t port) {
        std::error_code ec;
        sock_.connect({asio::ip::make_address("127.0.0.1"), port}, ec);
        return !ec;
    }
    [[nodiscard]] bool write(std::string const& bytes) {
        std::error_code ec;
        asio::write(sock_, asio::buffer(bytes), ec);
        return !ec;
    }
    // Reads until `done()` holds over the frames received, or `budget` has passed.
    template <class Done>
    [[nodiscard]] bool read_until(Done done, std::chrono::steady_clock::duration budget = kBudget) {
        auto const until = std::chrono::steady_clock::now() + budget;
        while (!done(frames)) {
            if (std::chrono::steady_clock::now() >= until || eof_) return false;
            std::array<char, 8192> chunk{};
            bool got = false;
            std::size_t n = 0;
            std::error_code rec;
            sock_.async_read_some(asio::buffer(chunk), [&](std::error_code e, std::size_t k) {
                got = true;
                rec = e;
                n = k;
            });
            ioc_.restart();
            ioc_.run_until(until);
            if (!got) {
                std::error_code ec;
                sock_.cancel(ec);
                ioc_.restart();
                ioc_.run();
                return false;
            }
            if (rec) {
                eof_ = true;
                return false;
            }
            buf_.append(chunk.data(), n);
            split();
        }
        return true;
    }
    void close() {
        std::error_code ec;
        sock_.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
        sock_.close(ec);
    }

    std::vector<std::string> frames;

private:
    void split() {
        for (;;) {
            auto const at = buf_.find(
                "\x01"
                "10=");
            if (at == std::string::npos || buf_.size() < at + 8) return;
            auto const end = at + 8;  // "<SOH>10=ddd<SOH>"
            frames.push_back(buf_.substr(0, end));
            buf_.erase(0, end);
        }
    }

    asio::io_context ioc_;
    asio::ip::tcp::socket sock_{ioc_};
    std::string buf_;
    bool eof_ = false;
};

bool is_answer_frame(std::string const& f) {
    return plain_rig::field_value(f, "43") == "Y" || plain_rig::field_value(f, "123") == "Y";
}
std::size_t count_live(std::vector<std::string> const& frames) {
    std::size_t n = 0;
    for (auto const& f : frames) {
        n += static_cast<std::size_t>(plain_rig::field_value(f, "35") == "D" &&
                                      plain_rig::field_value(f, "43") != "Y" &&
                                      plain_rig::field_value(f, "11").starts_with("LIVE-"));
    }
    return n;
}
bool has_replay(std::vector<std::string> const& frames, std::string const& seq) {
    for (auto const& f : frames) {
        if (plain_rig::field_value(f, "35") == "D" && plain_rig::field_value(f, "43") == "Y" &&
            plain_rig::field_value(f, "34") == seq) {
            return true;
        }
    }
    return false;
}

// `read()`, run on the session's strand and waited for until `until`; nullopt when the strand
// has not run it by then. The handler captures what it reads by value, since a reading that
// misses may run after the caller has moved on.
template <class Read>
auto on_strand(fixpp::session::Session& s, Read read,
               std::chrono::steady_clock::time_point const until)
    -> std::optional<decltype(read())> {
    using T = decltype(read());
    auto p = std::make_shared<std::promise<T>>();
    auto fut = p->get_future();
    asio::post(s.executor().underlying(), [p, read] { p->set_value(read()); });
    if (fut.wait_until(until) != std::future_status::ready) return std::nullopt;
    return fut.get();
}

std::string printable(std::vector<std::string> const& frames) {
    std::string out;
    for (auto const& f : frames) {
        out += "35=" + plain_rig::field_value(f, "35") + "/34=" + plain_rig::field_value(f, "34");
        if (plain_rig::field_value(f, "43") == "Y") out += "(43=Y)";
        out += ' ';
    }
    return out;
}

}  // namespace

TEST(ReplayMtEngine588, R588h_SendsFromSeveralThreadsDuringAHeldAnswer_AllAfterIt) {
    // Declared first, destroyed last: the Engine's executor is on it.
    asio::io_context ioc;
    auto work = asio::make_work_guard(ioc);
    std::vector<std::thread> io_threads;
    auto factory = std::make_shared<RecordingStoreFactory>(
        std::make_shared<fixpp::session::MemoryStoreFactory>(fixpp::session::MemoryStore::Config{
            .policy = fixpp::session::capacity_policy::unbounded}));
    auto const log = factory->log;
    // A call is recorded, and so can be held, only once the log is armed.
    log->arm();
    log->hold_in = [](StoreCall const& c) {
        return c.op == store_op::retrieve && c.dir == direction_t::outbound && c.first == 3;
    };
    std::unique_ptr<fixpp::session::Engine> engine;
    Peer peer;
    std::vector<std::thread> senders;

    fixpp::session::SessionConfig cfg;
    cfg.sender_comp_id = "ISLD";
    cfg.target_comp_id = "TW";
    cfg.begin_string = "FIX.4.2";
    cfg.role = fixpp::session::session_role::acceptor;
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    cfg.security_profile =
        fixpp::session::SecurityProfile{fixpp::session::SecurityProfile::kind::insecure_plain_tcp};
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
    cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
    cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
    cfg.heartbeat_interval = std::chrono::seconds{30};
    cfg.reconnect_endpoint = fixpp::transport::Endpoint{"127.0.0.1", 0};
    cfg.store_factory = factory;
    auto const id = fixpp::session::SessionId::from_config(cfg);

    fixpp::core::EngineConfig ec;
    ec.executor = ioc.get_executor();
    ec.clock = std::make_shared<fixpp::core::system_clock_source>(ioc.get_executor());
    engine = std::make_unique<fixpp::session::Engine>(ioc.get_executor(), std::move(ec));
    ASSERT_TRUE(engine->register_session(std::move(cfg)).has_value());
    ASSERT_TRUE(engine->start().has_value());
    for (int i = 0; i < kIoThreads; ++i) io_threads.emplace_back([&ioc] { ioc.run(); });

    // Every ending below runs this: the Engine stopped, the threads joined.
    auto const teardown = [&] {
        for (auto& t : senders) t.join();
        senders.clear();
        peer.close();
        if (engine) {
            auto stop = asio::co_spawn(ioc, engine->stop(), asio::use_future);
            if (stop.wait_for(kBudget) != std::future_status::ready) {
                ADD_FAILURE() << "Engine::stop did not complete";
            }
        }
        work.reset();
        for (auto& t : io_threads) t.join();
        engine.reset();
    };

    bool ok = true;
    auto const deadline = std::chrono::steady_clock::now() + kBudget;
    while (engine->acceptor_bound_endpoint(id).port == 0 &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    ok = ok && peer.connect(engine->acceptor_bound_endpoint(id).port);
    ok = ok && peer.write(peer_msg("A", 1,
                                   "98=0\x01"
                                   "108=30\x01"));
    ok = ok && peer.read_until([](auto const& fs) {
        return !fs.empty() && plain_rig::field_value(fs.back(), "35") == "A";
    });
    EXPECT_TRUE(ok) << "setup: no Logon reply";

    // The acceptor emits its Logon reply in LogonReceived and records Active only after the
    // emit has completed, so the peer can read the reply before the session is Active, and a
    // send that reaches the session's strand first is refused. The setup sends wait for
    // Active, read on the session's strand. The handler holds no lookup() handle (~Engine
    // requires every one released); the Session outlives every handler, which runs before
    // teardown joins the io threads.
    if (ok) {
        std::optional<bool> active;
        while (!active.value_or(false) && std::chrono::steady_clock::now() < deadline) {
            if (auto const s = engine->lookup(id)) {
                active = on_strand(
                    *s, [p = s.get()] { return p->state() == fixpp::session::fsm_state::Active; },
                    deadline);
            }
            if (!active.value_or(false)) std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        ok = active.value_or(false);
        EXPECT_TRUE(ok) << "setup: the session did not reach Active";
    }

    // The messages the answer replays, stored at 2 to 4.
    std::vector<std::string> const setup_payloads{
        "35=D\x01"
        "11=SETUP-0\x01"
        "55=X\x01",
        "35=D\x01"
        "11=SETUP-1\x01"
        "55=X\x01",
        "35=D\x01"
        "11=SETUP-2\x01"
        "55=X\x01"};
    for (unsigned k = 0; ok && k < kStored; ++k) {
        auto const& p = setup_payloads[k];
        auto fut = asio::co_spawn(
            ioc,
            engine->send(
                id,
                std::span<const std::byte>{reinterpret_cast<std::byte const*>(p.data()), p.size()}),
            asio::use_future);
        ok = fut.wait_for(kBudget) == std::future_status::ready && fut.get().has_value();
    }
    EXPECT_TRUE(ok) << "setup: a setup send failed";
    ok = ok && peer.read_until([](auto const& fs) {
        std::size_t n = 0;
        for (auto const& f : fs)
            n += static_cast<std::size_t>(plain_rig::field_value(f, "35") == "D");
        return n >= kStored;
    });

    // The request; the answer reaches the hold at slot 3 once replay 2 is on the wire.
    auto const until = std::chrono::steady_clock::now() + fixpp::test_support::kPumpBudget;
    ok = ok && peer.write(peer_msg("2", 2,
                                   "7=1\x01"
                                   "16=0\x01"));
    ok = ok && peer.read_until([](auto const& fs) { return has_replay(fs, "2"); });
    EXPECT_TRUE(ok) << "setup: the answer did not reach the held read: " << printable(peer.frames);
    if (!ok) {
        teardown();
        return;
    }

    auto session = engine->lookup(id);
    if (session == nullptr) {
        ADD_FAILURE() << "Engine::lookup found no session";
        teardown();
        return;
    }
    // The readings' one budget, taken before the request: the hold cannot begin before the
    // request is written.
    auto const release_and_teardown = [&] {
        asio::post(session->executor().underlying(), [log] { log->release_hold(); });
        session.reset();
        teardown();
    };
    std::optional<bool> held;
    while (!held.value_or(false) && std::chrono::steady_clock::now() < until) {
        held = on_strand(*session, [log] { return log->holding(); }, until);
        if (!held.value_or(false)) std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    if (!held.value_or(false)) {
        ADD_FAILURE() << "setup: the hold is not engaged (reading "
                      << (held ? (*held ? "true" : "false") : "none") << ")";
        release_and_teardown();
        return;
    }

    std::vector<std::vector<std::string>> payloads(kSenderThreads);
    std::vector<int> failed(kSenderThreads, 0);
    for (int t = 0; t < kSenderThreads; ++t) {
        for (int k = 0; k < kSendsPerThread; ++k) {
            payloads[t].push_back(
                "35=D\x01"
                "11=LIVE-" +
                std::to_string(t) + "-" + std::to_string(k) +
                "\x01"
                "55=X\x01");
        }
    }
    for (int t = 0; t < kSenderThreads; ++t) {
        senders.emplace_back([&, t] {
            for (auto const& p : payloads[t]) {
                auto fut = asio::co_spawn(
                    ioc,
                    engine->send(
                        id, std::span<const std::byte>{reinterpret_cast<std::byte const*>(p.data()),
                                                       p.size()}),
                    asio::use_future);
                if (fut.wait_for(kBudget) != std::future_status::ready || !fut.get().has_value()) {
                    ++failed[t];
                }
            }
        });
    }
    // The contention witness: every sender thread has a send waiting at the replay gate. The
    // reading holds no lookup() handle (~Engine requires every one released); the Session
    // outlives every handler, which runs before teardown joins the io threads.
    auto const gate_waiters = [s = session.get()] {
        return fixpp::session::session_test_access::replay_gate(*s).waiters;
    };
    std::optional<std::uint32_t> waiting;
    while (waiting.value_or(0) != static_cast<std::uint32_t>(kSenderThreads) &&
           std::chrono::steady_clock::now() < until) {
        waiting = on_strand(*session, gate_waiters, until);
        if (waiting.value_or(0) != static_cast<std::uint32_t>(kSenderThreads)) {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
    }
    if (waiting.value_or(0) != static_cast<std::uint32_t>(kSenderThreads)) {
        ADD_FAILURE() << "not every sender reached the replay gate while the answer was held: "
                      << "waiters " << (waiting ? std::to_string(*waiting) : std::string{"none"})
                      << " of " << kSenderThreads;
        release_and_teardown();
        return;
    }

    asio::post(session->executor().underlying(), [log] { log->release_hold(); });
    for (auto& s : senders) s.join();
    senders.clear();
    auto const timed_out = on_strand(
        *session, [log] { return log->hold_timed_out(); },
        std::chrono::steady_clock::now() + fixpp::test_support::kPumpBudget);
    EXPECT_EQ(timed_out, std::optional<bool>{false})
        << "the hold ended by its time bound, not by the release";
    std::size_t const want_live = static_cast<std::size_t>(kSenderThreads * kSendsPerThread);
    bool const all = peer.read_until([&](auto const& fs) {
        return has_replay(fs, "3") && has_replay(fs, "4") && count_live(fs) >= want_live;
    });
    EXPECT_TRUE(all) << "not every frame reached the peer: " << printable(peer.frames);
    for (int t = 0; t < kSenderThreads; ++t) EXPECT_EQ(failed[t], 0) << "sender thread " << t;

    auto const& fs = peer.frames;
    std::size_t last_answer = fs.size();
    unsigned long end = 0;
    for (std::size_t i = 0; i < fs.size(); ++i) {
        if (!is_answer_frame(fs[i])) continue;
        last_answer = i;
        if (plain_rig::field_value(fs[i], "123") == "Y") {
            end = std::max(end, std::stoul(plain_rig::field_value(fs[i], "36")) - 1UL);
        } else {
            end = std::max(end, std::stoul(plain_rig::field_value(fs[i], "34")));
        }
    }
    EXPECT_LT(last_answer, fs.size()) << "no answer frame reached the peer";
    for (std::size_t i = 0; i < fs.size(); ++i) {
        if (is_answer_frame(fs[i])) continue;
        auto const n = std::stoul(plain_rig::field_value(fs[i], "34"));
        if (n > end) {
            EXPECT_GT(i, last_answer)
                << "frame " << i << " (34=" << n
                << ") above the answer's range precedes its last frame: " << printable(fs);
        }
    }
    session.reset();
    teardown();
}
