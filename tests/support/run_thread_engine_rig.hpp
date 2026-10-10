// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/support/run_thread_engine_rig.hpp — test-only.
//
// fixpp#544 (B35; `.specify/544-hot-path-zero-alloc.md` §3, "Run thread (W-A, W-B)"): one
// plaintext Engine session over loopback TCP, driven by a raw peer socket, whose
// io_context is run by ONE dedicated thread for the whole test, from setup through
// teardown. It is T044's rig (tests/session/plain_engine_rig.hpp) re-shaped so that an
// allocation window can sit inside a single scheduler call: asio's frame recycler belongs
// to the scheduler call, so handlers that land in short `run_one_for` calls on another
// thread would each start with an empty cache.
//
// THE THREADS. The run thread is the only thread that drives the io_context. The test
// thread never calls `run*` / `poll*` (except in the `driven_by_test_thread` mode, which
// is arm (s)'s scope control). It talks to the run thread only through:
//   - std::atomic counters the test-owned Application increments on the session strand;
//   - raw `::send` on the peer socket's native handle, which asio does not see, so it is
//     never a concurrent operation on an asio object;
//   - posts made outside every window (connect, snapshot reads, teardown).
// Session state is read only through a read posted to the session strand.
//
// THE WINDOW. The test thread pre-builds every frame before the run thread starts. A
// window then arms, writes M+1 frames, each after the previous one's completion signal,
// waits for the (M+1)-th signal and disarms. The waiting thread spins on an atomic and
// allocates nothing; the peer's read is a callback over a fixed buffer, not a coroutine.
//
// TEARDOWN. `stop()` posts `Engine::stop()`, waits for it, closes the peer, then stops
// and joins the run thread. Assert after `stop()`, never between `start()` and it.
#pragma once

#include <array>
#include <asio/as_tuple.hpp>
#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/deferred.hpp>
#include <asio/detached.hpp>
#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/post.hpp>
#include <asio/prefer.hpp>
#include <asio/steady_timer.hpp>
#include <asio/use_future.hpp>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/fix_time.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/application.hpp>
#include <fixpp/session/engine.hpp>
#include <fixpp/session/security_profile.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fixpp/transport/endpoint.hpp>
#include <future>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <sys/socket.h>

#include <cerrno>
#endif

#include "session/plain_engine_rig.hpp"
#include "support/minimal_dictionary.hpp"
#include "support/recycler_driver.hpp"
#include "support/session_test_access.hpp"
#include "support/validation_test_dictionary.hpp"

namespace fixpp::test_support::run_thread_rig {

using native_socket = asio::ip::tcp::socket::native_handle_type;

// Writes all of `bytes` on a socket handle, bypassing asio. Retries a full send buffer.
inline bool raw_send_all(native_socket h, std::string_view bytes) {
    char const* p = bytes.data();
    std::size_t n = bytes.size();
    while (n != 0U) {
#ifdef _WIN32
        int const r = ::send(h, p, static_cast<int>(n), 0);
        if (r == SOCKET_ERROR) {
            if (::WSAGetLastError() == WSAEWOULDBLOCK) {
                std::this_thread::yield();
                continue;
            }
            return false;
        }
#else
        auto const r = ::send(h, p, n, MSG_NOSIGNAL);
        if (r < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                std::this_thread::yield();
                continue;
            }
            return false;
        }
#endif
        p += r;
        n -= static_cast<std::size_t>(r);
    }
    return true;
}

// What each test-owned callback does when its message arrives.
enum class hook : std::uint8_t {
    signal_from_admin,  // W-A: fromAdmin(Heartbeat) bumps the completion counter
    signal_from_app,    // W-B: fromApp(NewOrderSingle) bumps it
    wake_from_admin,    // W-A's (b) arms and twins: fromAdmin wakes the launcher
    wake_from_app,      // W-E: fromApp wakes the sender
};

// The test-owned Application. Every callback runs on the session strand.
class App final : public fixpp::session::Application {
public:
    explicit App(hook h) : mode_{h} {}

    std::atomic<std::uint64_t> logons{0};
    std::atomic<std::uint64_t> heartbeats{0};
    std::atomic<std::uint64_t> app_messages{0};
    std::atomic<std::uint64_t> completions{0};
    std::atomic<std::uint64_t> vetoed{0};
    // The timer a woken loop waits on; set before its first wake.
    std::atomic<asio::steady_timer*> wake{nullptr};
    // Sends `toApp` vetoes from now on; written only through a post to the strand.
    int veto_remaining = 0;

    void onLogon(fixpp::session::SessionId const& /*id*/) override {
        logons.fetch_add(1, std::memory_order_release);
    }

    fixpp::core::expected_t<void> fromAdmin(
        fixpp::wire::MessageView<fixpp::wire::access_mode::Index> const& msg,
        fixpp::session::SessionId const& /*id*/) override {
        if (msg.msg_type() != "0") return {};
        heartbeats.fetch_add(1, std::memory_order_relaxed);
        if (mode_ == hook::signal_from_admin) {
            completions.fetch_add(1, std::memory_order_release);
        } else if (mode_ == hook::wake_from_admin) {
            wake_loop();
        }
        return {};
    }

    fixpp::core::expected_t<void> fromApp(
        fixpp::wire::MessageView<fixpp::wire::access_mode::Index> const& /*msg*/,
        fixpp::session::SessionId const& /*id*/) override {
        app_messages.fetch_add(1, std::memory_order_relaxed);
        if (mode_ == hook::signal_from_app) {
            completions.fetch_add(1, std::memory_order_release);
        } else if (mode_ == hook::wake_from_app) {
            wake_loop();
        }
        return {};
    }

    fixpp::core::expected_t<void> toApp(
        fixpp::wire::MessageView<fixpp::wire::access_mode::Index> const& /*msg*/,
        fixpp::session::SessionId const& /*id*/) override {
        if (veto_remaining > 0) {
            --veto_remaining;
            vetoed.fetch_add(1, std::memory_order_relaxed);
            return std::unexpected(fixpp::core::error::app_do_not_send);
        }
        return {};
    }

private:
    void wake_loop() {  // NOLINT(readability-make-member-function-const): it cancels a timer
        // The cancel's completion is posted, so the woken loop runs after the inbound arm
        // returns, beside it rather than on top of it (§3, W-E).
        if (auto* t = wake.load(std::memory_order_acquire)) t->cancel();
    }

    hook mode_;
};

// The peer's reader: a callback over a fixed buffer, re-armed on every completion. It
// counts the bytes it receives and the application messages (`<SOH>35=D<SOH>`) among them.
class PeerReader {
public:
    explicit PeerReader(asio::ip::tcp::socket& sock) : sock_{&sock} {}

    std::atomic<std::size_t> bytes{0};
    std::atomic<std::size_t> app_frames{0};
    std::atomic<bool> ended{false};

    void start() {
        sock_->async_read_some(asio::buffer(buf_), [this](std::error_code const& ec,
                                                          std::size_t n) { on_read(ec, n); });
    }

private:
    void on_read(std::error_code const& ec, std::size_t n) {
        if (ec) {
            ended.store(true, std::memory_order_release);
            return;
        }
        static constexpr std::string_view kPattern{
            "\x01"
            "35=D\x01"};
        std::size_t found = 0;
        for (std::size_t i = 0; i < n; ++i) {
            char const c = buf_[i];
            if (c == kPattern[match_]) {
                if (++match_ == kPattern.size()) {
                    ++found;
                    match_ = 1;  // the closing SOH opens the next match
                }
            } else {
                match_ = c == kPattern[0] ? 1 : 0;
            }
        }
        app_frames.fetch_add(found, std::memory_order_relaxed);
        bytes.fetch_add(n, std::memory_order_release);
        start();
    }

    asio::ip::tcp::socket* sock_;
    std::array<char, 4096> buf_{};
    std::size_t match_ = 0;  // run thread only
};

// A long-lived coroutine on the session strand, woken by `wake`'s cancel. Each wake runs
// `body()` once and then bumps `completions`. `body` is a non-coroutine callable returning
// the awaitable to await, so it adds no frame of its own.
template <class Body>
asio::awaitable<void> woken_loop(asio::steady_timer& wake, std::atomic<bool> const& stop,
                                 Body& body, std::atomic<std::uint64_t>& completions,
                                 std::atomic<std::uint64_t>& body_ok, std::atomic<bool>& exited) {
    for (;;) {
        (void)co_await wake.async_wait(asio::as_tuple(asio::deferred));
        if (stop.load(std::memory_order_acquire)) break;
        using R = recycler::awaited_t<decltype(body())>;
        if constexpr (std::is_void_v<R>) {
            co_await body();
            body_ok.fetch_add(1, std::memory_order_relaxed);
        } else {
            auto r = co_await body();
            if (recycler::result_ok(r)) body_ok.fetch_add(1, std::memory_order_relaxed);
        }
        completions.fetch_add(1, std::memory_order_release);
    }
    exited.store(true, std::memory_order_release);
}

// `Session::send` over one pre-built payload (§3, W-E): never `Engine::send`, which
// allocates on every call.
struct send_body {
    fixpp::session::Session* session = nullptr;
    std::span<const std::byte> payload;
    auto operator()() const { return session->send(payload); }
};

struct options {
    hook mode = hook::signal_from_admin;
    // W-B, W-E: validation on, over a dictionary that defines NewOrderSingle, so the path
    // runs parse -> validate -> dispatch.
    bool validating = false;
    // Arm (a): the Engine's executor is a work-tracked io_context executor, whose target
    // type is not `io_context::executor_type`.
    bool tracked_executor = false;
    // Arm (s): no run thread; the test thread drives the io_context with `run_one_for`.
    bool driven_by_test_thread = false;
    // A PMR arena for the session's message_arena, when a cell counts it.
    std::pmr::memory_resource* message_arena = nullptr;
};

// A snapshot of session state, read on the session strand.
struct session_snapshot {
    bool read = false;
    fixpp::session::fsm_state state = fixpp::session::fsm_state::NotConnected;
    fixpp::session::seqnum_t next_inbound = 0;
    bool has_validator = false;
};

inline constexpr std::chrono::seconds kWaitBudget{10};

class Rig {
public:
    // The io_context is the first member: it outlives the engine, the peer and the timer.
    asio::io_context ioc;
    options opt;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    std::shared_ptr<App> app;
    asio::any_io_executor engine_exec;
    std::unique_ptr<fixpp::session::Engine> engine;
    fixpp::session::SessionId id;
    std::string begin_string = "FIX.4.2";
    asio::ip::tcp::socket peer{ioc};
    // Seeded from the unopened `peer` because the IOCP `native_handle_type` has no default
    // constructor; `start_and_logon()` re-reads it once `peer` is open.
    native_socket peer_handle = peer.native_handle();
    PeerReader reader{peer};

    // A woken loop's state (W-A's launcher, W-E's sender).
    std::optional<asio::steady_timer> wake;
    std::atomic<bool> loop_stop{false};
    std::atomic<bool> loop_exited{false};
    std::atomic<std::uint64_t> loop_ok{0};

    explicit Rig(options o) : opt{o}, app{std::make_shared<App>(o.mode)} {
        using namespace std::chrono;
        clock = std::make_shared<fixpp::core::mock_clock>(
            system_clock::time_point{} + seconds{1704067200}, fixpp::core::steady_time_point{},
            ioc.get_executor());
        // NOLINTBEGIN(readability-static-accessed-through-instance): asio's spelling
        engine_exec = opt.tracked_executor
                          ? asio::any_io_executor{asio::prefer(
                                ioc.get_executor(), asio::execution::outstanding_work.tracked)}
                          : asio::any_io_executor{ioc.get_executor()};
        // NOLINTEND(readability-static-accessed-through-instance)
    }

    Rig(Rig const&) = delete;
    Rig& operator=(Rig const&) = delete;

    ~Rig() {
        if (runner_.joinable()) {
            work_.reset();
            ioc.stop();
            runner_.join();
        }
        if (engine && !engine_stopped_) {
            // Engine::stop() did not complete: ~Engine would abort on its stopped_
            // precondition, so the engine is leaked rather than destroyed. The cell has
            // already failed on stop()'s result.
            (void)engine.release();
        }
        std::error_code ec;
        peer.close(ec);
    }

    // SendingTime(52) for every frame, read once from the mock clock, which never moves.
    [[nodiscard]] std::string sending_time() const {
        std::array<char, 32> buf{};
        auto const r = fixpp::core::utc_time_to_fix_string(
            clock->now(), fixpp::core::fix_time_precision::millis, std::span<char>{buf});
        return r ? std::string{r->data(), r->size()} : std::string{};
    }

    [[nodiscard]] std::string msg(std::string_view msg_type, std::uint32_t seq,
                                  std::string_view extra = {}) const {
        return plain_rig::message(begin_string, msg_type, seq, "TW", "ISLD", sending_time(), extra);
    }
    [[nodiscard]] std::string logon() const {
        return msg("A", 1,
                   "98=0\x01"
                   "108=30\x01");
    }
    [[nodiscard]] std::string heartbeat(std::uint32_t seq) const { return msg("0", seq); }
    // A NewOrderSingle the validation dictionary accepts: ClOrdID, Side and TransactTime.
    [[nodiscard]] std::string new_order(std::uint32_t seq) const {
        return msg(
            "D", seq,
            "11=ORD" + std::to_string(seq) + "\x01" + "54=1\x01" + "60=" + sending_time() + "\x01");
    }
    // The application payload a W-E send carries.
    [[nodiscard]] std::vector<std::byte> send_payload() const {
        return plain_rig::to_bytes(
            "35=D\x01"
            "11=OUT1\x01"
            "54=2\x01"
            "60=" +
            sending_time() + "\x01");
    }

    [[nodiscard]] fixpp::session::SessionConfig cfg() const {
        fixpp::session::SessionConfig c;
        c.sender_comp_id = "ISLD";
        c.target_comp_id = "TW";
        c.begin_string = begin_string;
        c.role = fixpp::session::session_role::acceptor;
        c.executor_override = engine_exec;
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#elif defined(_MSC_VER)  // NOLINT(readability-use-concise-preprocessor-directives): C++23 form
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
        c.security_profile = fixpp::session::SecurityProfile{
            fixpp::session::SecurityProfile::kind::insecure_plain_tcp};
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)  // NOLINT(readability-use-concise-preprocessor-directives): C++23 form
#pragma warning(pop)
#endif
        if (opt.validating) {
            c.dictionary = fixpp::test_support::make_validation_test_dictionary();
            c.validate_inbound_messages = true;
        } else {
            c.dictionary = fixpp::test_support::make_minimal_dictionary();
        }
        c.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
        c.heartbeat_interval = std::chrono::seconds{30};
        c.logout_disconnect_timeout_ms = 500;
        c.reconnect_endpoint = fixpp::transport::Endpoint{"127.0.0.1", 0};
        c.transport_send = [](std::span<const std::byte>) {};
        if (opt.message_arena != nullptr) c.message_arena = opt.message_arena;
        return c;
    }

    // Registers the session, starts the engine and the run thread, connects the peer and
    // logs on. True once the session has reported onLogon.
    [[nodiscard]] bool start_and_logon() {
        std::string const logon_frame = logon();
        fixpp::core::EngineConfig ec;
        ec.executor = engine_exec;
        ec.clock = clock;
        ec.application = app;
        engine = std::make_unique<fixpp::session::Engine>(engine_exec, std::move(ec));
        auto c = cfg();
        id = fixpp::session::SessionId::from_config(c);
        if (!engine->register_session(std::move(c)).has_value()) return false;
        if (!engine->start().has_value()) return false;
        engine_started_ = true;

        std::error_code ec_open;
        peer.open(asio::ip::tcp::v4(), ec_open);
        if (ec_open) return false;
        peer_handle = peer.native_handle();

        if (!opt.driven_by_test_thread) {
            work_.emplace(asio::make_work_guard(ioc));
            runner_ = std::thread{[this] { ioc.run(); }};
        }

        if (!wait_until([this] { return engine->acceptor_bound_endpoint(id).port != 0; })) {
            return false;
        }
        auto const port = engine->acceptor_bound_endpoint(id).port;
        asio::post(ioc, [this, port] {
            peer.async_connect(asio::ip::tcp::endpoint{asio::ip::make_address("127.0.0.1"), port},
                               [this](std::error_code const& e) {
                                   connect_ec_ = e;
                                   if (!e) reader.start();
                                   connected_.store(true, std::memory_order_release);
                               });
        });
        if (!wait_until([this] { return connected_.load(std::memory_order_acquire); })) {
            return false;
        }
        if (connect_ec_) return false;
        if (!raw_send_all(peer_handle, logon_frame)) return false;
        return wait_until([this] { return app->logons.load(std::memory_order_acquire) != 0U; });
    }

    [[nodiscard]] std::shared_ptr<fixpp::session::Session> session() const {
        return engine ? engine->lookup(id) : nullptr;
    }

    // Writes one pre-built frame and waits until `counter` reaches `target`.
    [[nodiscard]] bool write_and_wait(std::string_view frame, std::atomic<std::uint64_t>& counter,
                                      std::uint64_t target) {
        if (!raw_send_all(peer_handle, frame)) return false;
        return wait_until([&] { return counter.load(std::memory_order_acquire) >= target; });
    }

    // Waits for `pred`: spinning without allocating while the run thread drives the
    // io_context, or driving it one handler at a time in arm (s)'s mode.
    template <class Pred>
    [[nodiscard]] bool wait_until(Pred pred,
                                  std::chrono::steady_clock::duration budget = kWaitBudget) {
        auto const limit = std::chrono::steady_clock::now() + budget;
        while (!pred()) {
            if (std::chrono::steady_clock::now() >= limit) return false;
            if (opt.driven_by_test_thread) {
                ioc.run_one_for(std::chrono::milliseconds{1});
            } else {
                std::this_thread::yield();
            }
        }
        return true;
    }

    // Starts a woken loop on the session strand over `body`. The loop and `body` live
    // until stop().
    template <class Body>
    [[nodiscard]] bool start_woken_loop(Body& body) {
        auto const s = session();
        if (!s) return false;
        auto const strand = s->executor().underlying();
        wake.emplace(strand, asio::steady_timer::time_point::max());
        app->wake.store(&*wake, std::memory_order_release);
        asio::co_spawn(strand,
                       woken_loop(*wake, loop_stop, body, app->completions, loop_ok, loop_exited),
                       asio::detached);
        return true;
    }

    // Sets how many sends `toApp` vetoes, on the session strand.
    [[nodiscard]] bool set_veto(int n) {
        auto const s = session();
        if (!s) return false;
        auto done = std::make_shared<std::atomic<bool>>(false);
        asio::post(s->executor().underlying(), [this, n, done] {
            app->veto_remaining = n;
            done->store(true, std::memory_order_release);
        });
        return wait_until([&] { return done->load(std::memory_order_acquire); });
    }

    // Runs `f()` on the session strand and waits for it. For reads of state the run thread
    // writes; it allocates, so it is never called inside a window.
    template <class F>
    [[nodiscard]] bool on_strand(F f) {
        auto const s = session();
        if (!s) return false;
        auto done = std::make_shared<std::atomic<bool>>(false);
        asio::post(s->executor().underlying(), [f = std::move(f), done]() mutable {
            f();
            done->store(true, std::memory_order_release);
        });
        return wait_until([&] { return done->load(std::memory_order_acquire); });
    }

    // Reads session state on the session strand.
    [[nodiscard]] session_snapshot observe() {
        auto const s = session();
        if (!s) return {};
        struct shared_snapshot {
            std::atomic<bool> done{false};
            session_snapshot snap;
        };
        auto box = std::make_shared<shared_snapshot>();
        asio::post(s->executor().underlying(), [s, box] {
            box->snap.read = true;
            box->snap.state = s->state();
            box->snap.next_inbound =
                fixpp::session::session_test_access::seqnum_mgr(*s).next_inbound_unsafe();
            box->snap.has_validator = fixpp::session::session_test_access::has_validator(*s);
            box->done.store(true, std::memory_order_release);
        });
        if (!wait_until([&] { return box->done.load(std::memory_order_acquire); })) return {};
        return box->snap;
    }

    // Stops a woken loop, the engine and the run thread. True iff Engine::stop()
    // completed. Assert only after this returns.
    [[nodiscard]] bool stop() {
        bool loop_ok_to_stop = true;
        if (wake) {
            auto const s = session();
            if (s) {
                asio::post(s->executor().underlying(), [this] {
                    loop_stop.store(true, std::memory_order_release);
                    wake->cancel();
                });
                loop_ok_to_stop =
                    wait_until([this] { return loop_exited.load(std::memory_order_acquire); });
            }
            app->wake.store(nullptr, std::memory_order_release);
        }
        if (engine_started_) {
            auto fut = asio::co_spawn(ioc, engine->stop(), asio::use_future);
            bool const ready = wait_until(
                [&] { return fut.wait_for(std::chrono::seconds{0}) == std::future_status::ready; });
            if (ready) {
                fut.get();
                engine_stopped_ = true;
            }
        } else {
            engine_stopped_ = true;
        }
        asio::post(ioc, [this] {
            std::error_code ec;
            peer.close(ec);
        });
        if (opt.driven_by_test_thread) {
            ioc.run_for(std::chrono::milliseconds{50});
        }
        if (runner_.joinable()) {
            work_.reset();
            ioc.stop();
            runner_.join();
        }
        return engine_stopped_ && loop_ok_to_stop;
    }

private:
    std::optional<asio::executor_work_guard<asio::io_context::executor_type>> work_;
    std::thread runner_;
    std::atomic<bool> connected_{false};
    std::error_code connect_ec_;
    bool engine_started_ = false;
    bool engine_stopped_ = false;
};

}  // namespace fixpp::test_support::run_thread_rig
