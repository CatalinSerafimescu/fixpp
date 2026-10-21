// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/held_file_store_scene.hpp — test-only.
//
// A fixture for fixpp#538: cells that run on a real FileStore lock
// (.specify/536-537-538-recovery-liveness.md, D-538-4, "Why `store_cancelled` aborts"):
//
//   store_hold  a hold on one FileStore write: a one-shot latch inside FileStore's
//               offload hook (install_store_offload_probe), which runs on the file I/O
//               thread at the start of an offloaded write, while the store's lock is
//               taken. A cell arms it, starts the store call it wants held, and
//               releases it when it chooses; until then every other call of that
//               store queues at the lock.
//   Scene       an acceptor session under an Engine on the plain transport, facing a
//               raw peer, with the test thread running the I/O (plain_engine_rig.hpp).
//               Its store is a FileStore on a file I/O executor of its own, behind a
//               RecordingStore (tests/support/recording_store.hpp). A cell chooses how
//               many threads that executor has, and can open a second Scene on the
//               directory of one whose engine has stopped (`reuse_dir`).
//
// The including target needs the compile definition FIXPP_TEST_HOOKS, which declares
// install_store_offload_probe.
#pragma once

#include <asio/any_io_executor.hpp>
#include <asio/co_spawn.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_future.hpp>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fixpp/core/error.hpp>
#include <fixpp/session/application.hpp>
#include <fixpp/session/file_store.hpp>
#include <fixpp/session/session_config.hpp>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "session/plain_engine_rig.hpp"
#include "support/pump_until_ready.hpp"
#include "support/recording_store.hpp"
#include "support/temp_dir.hpp"

namespace fixpp::test_support::store_hold {

// The hold is released by the cell, never by time. This bound only frees the file I/O
// thread when a cell's own pumps have missed. It is three bounded-pump budgets, so a
// cell must take no more than two bounded pumps while a hold is in flight. A hold that
// reached the bound sets `timed_out`, and a cell that reads it fails.
inline constexpr auto kHoldBound = 3 * kPumpBudget;

struct State {
    std::mutex m;
    std::condition_variable cv;
    bool armed = false;     // guarded by m
    bool released = false;  // guarded by m
    std::atomic<bool> holding{false};
    std::atomic<bool> timed_out{false};
};
// The hook is a plain function pointer, so its state is a global.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
inline State g_state;

// Runs on the file I/O thread, at the start of an offloaded write.
inline void probe(std::thread::id /*pool_thread*/) noexcept {
    std::unique_lock lk{g_state.m};
    if (!g_state.armed) return;
    g_state.armed = false;
    g_state.holding.store(true, std::memory_order_release);
    if (!g_state.cv.wait_for(lk, kHoldBound, [] { return g_state.released; })) {
        g_state.timed_out.store(true, std::memory_order_release);
    }
    g_state.holding.store(false, std::memory_order_release);
}

// The next offloaded write holds.
inline void arm() {
    std::lock_guard lk{g_state.m};
    g_state.armed = true;
    g_state.released = false;
    g_state.timed_out.store(false, std::memory_order_release);
}

inline void release() {
    {
        std::lock_guard lk{g_state.m};
        g_state.released = true;
        g_state.armed = false;
    }
    g_state.cv.notify_all();
}

[[nodiscard]] inline bool holding() noexcept {
    return g_state.holding.load(std::memory_order_acquire);
}
[[nodiscard]] inline bool timed_out() noexcept {
    return g_state.timed_out.load(std::memory_order_acquire);
}

// Installs the hook for one scope. Declare it before the file I/O pool, so the pool is
// joined first and the hook outlives every offload.
struct scoped_hook {
    scoped_hook() noexcept { fixpp::session::install_store_offload_probe(&probe); }
    ~scoped_hook() noexcept { fixpp::session::install_store_offload_probe(nullptr); }
    scoped_hook(scoped_hook const&) = delete;
    scoped_hook& operator=(scoped_hook const&) = delete;
};

// Releases a hold still in flight when a scope is left by an exception. Declare it
// after the file I/O pool, so it runs before the pool's join, which would otherwise
// wait out kHoldBound.
struct release_on_exit {
    release_on_exit() = default;
    ~release_on_exit() {
        try {
            release();
        } catch (...) {  // NOLINT(bugprone-empty-catch): nothing to report to from here
        }
    }
    release_on_exit(release_on_exit const&) = delete;
    release_on_exit& operator=(release_on_exit const&) = delete;
};

// The "8=…|…|10=NNN|" frames in `wire`, in order.
[[nodiscard]] inline std::vector<std::string> split_frames(std::string_view wire) {
    std::vector<std::string> out;
    std::size_t pos = 0;
    while (true) {
        auto const start = wire.find("8=FIX", pos);
        if (start == std::string_view::npos) break;
        auto const cs = wire.find(
            "\x01"
            "10=",
            start);
        if (cs == std::string_view::npos) break;
        auto const end = wire.find('\x01', cs + 4);
        if (end == std::string_view::npos) break;
        out.emplace_back(wire.substr(start, end + 1 - start));
        pos = end + 1;
    }
    return out;
}

// `frames`, one per line, with SOH shown as '|': for a failure message.
[[nodiscard]] inline std::string printable(std::vector<std::string> const& frames) {
    std::string out;
    for (auto const& f : frames) {
        for (char const c : f) out += (c == '\x01') ? '|' : c;
        out += '\n';
    }
    return out.empty() ? std::string{"(no frame)\n"} : out;
}

}  // namespace fixpp::test_support::store_hold

namespace fixpp::test_support {

// One session and its peer. A cell never asserts fatally while a Scene's engine runs:
// ~Engine requires a completed stop. It calls finish() on every path and asserts on what
// it recorded afterwards.
class HeldFileStoreScene {
    // Declared first, destroyed last: the hook outlives the pool's offloads, the pool
    // outlives the engine and its FileStore, and the directory is removed after both.
    store_hold::scoped_hook hook_;
    asio::thread_pool file_pool_;  // the FileStore's file I/O executor
    store_hold::release_on_exit release_guard_;
    struct temp_dir {
        std::filesystem::path path = unique_temp_dir("b28_538_scene");
        temp_dir() = default;
        ~temp_dir() { (void)try_remove_temp_dir(path); }
        temp_dir(temp_dir const&) = delete;
        temp_dir& operator=(temp_dir const&) = delete;
    } dir_;
    std::vector<std::byte> payload_ = plain_rig::to_bytes(
        "35=D\x01"
        "11=ORD001\x01"
        "54=1\x01"
        "55=AAPL\x01");

public:
    plain_rig::Rig rig;
    std::shared_ptr<StoreCallLog> log;
    std::string setup_miss;  // empty when setup() completed
    unsigned first = 0;      // MsgSeqNum(34) of the first application message of setup()
    unsigned last = 0;       // and of the last
    bool stop_completed = false;
    // Adjusts the session's configuration after setup() has built it and before the
    // session starts.
    std::function<void(fixpp::session::SessionConfig&)> tweak_cfg;
    // The FileStore's configuration, as setup() built it. A cell opens a fresh FileStore
    // from it once the engine has stopped.
    fixpp::session::FileStore::Config file_cfg;
    // Set before setup(): the session's store is opened in this directory, another
    // Scene's, in place of this Scene's own. The other Scene must outlive this one, and
    // its engine must have stopped.
    std::optional<std::filesystem::path> reuse_dir;
    // Set before setup(): the FileStore's file I/O executor is what this returns for the
    // Scene's own pool's executor, in place of that executor itself.
    std::function<asio::any_io_executor(asio::thread_pool::executor_type)> wrap_file_executor;

    // `file_io_threads`: the threads of the FileStore's file I/O executor. With one, an
    // offload of the store waits behind a held write; close(graceful)'s flush is such an
    // offload and takes no lock, so a cell that closes gracefully during a hold needs two.
    explicit HeldFileStoreScene(std::size_t file_io_threads = 1) : file_pool_{file_io_threads} {}
    // The same, with the engine's Application.
    HeldFileStoreScene(std::size_t file_io_threads,
                       std::shared_ptr<fixpp::session::Application> application)
        : file_pool_{file_io_threads}, rig{std::move(application)} {}
    HeldFileStoreScene(HeldFileStoreScene const&) = delete;
    HeldFileStoreScene& operator=(HeldFileStoreScene const&) = delete;
    ~HeldFileStoreScene() = default;

    // This Scene's own store directory.
    [[nodiscard]] std::filesystem::path const& dir() const noexcept { return dir_.path; }

    template <class Fut>
    [[nodiscard]] static bool ready(Fut const& fut) {
        return fut.wait_for(std::chrono::seconds{0}) == std::future_status::ready;
    }

    // One application message through Engine::send. `token` completes it: use_future,
    // or use_future bound to a cancellation slot.
    template <class Token>
    [[nodiscard]] auto send_app(Token&& token) {
        return asio::co_spawn(rig.ioc,
                              rig.engine->send(rig.id, std::span<const std::byte>{payload_}),
                              std::forward<Token>(token));
    }
    [[nodiscard]] std::future<fixpp::core::expected_t<void>> send_app() {
        return send_app(asio::use_future);
    }

    // Starts the engine, logs the peer on, arms the store log, and sends `app_messages`
    // application messages, one at a time, until the peer has read them. On a miss it
    // names the step in `setup_miss`, calls finish() and returns false.
    [[nodiscard]] bool setup(int app_messages) {
        auto miss = [this](std::string what) {
            setup_miss = std::move(what);
            finish();
            return false;
        };
        auto c = rig.cfg(fixpp::session::session_role::acceptor);
        file_cfg.directory = reuse_dir ? *reuse_dir : dir_.path;
        file_cfg.sender_comp_id = c.sender_comp_id;
        file_cfg.target_comp_id = c.target_comp_id;
        file_cfg.file_io_executor = wrap_file_executor
                                        ? wrap_file_executor(file_pool_.get_executor())
                                        : asio::any_io_executor{file_pool_.get_executor()};
        auto const factory = std::make_shared<RecordingStoreFactory>(file_cfg);
        log = factory->log;
        c.store_factory = factory;
        if (tweak_cfg) tweak_cfg(c);

        if (!rig.start(c)) return miss("rig.start");
        if (!rig.to_active()) return miss("rig.to_active");
        if (!rig.run_until(
                [this] { return !plain_rig::frames_of_type(rig.peer.received, "A").empty(); })) {
            return miss("the peer did not read the Logon reply");
        }
        // Armed after Active: the session's own calls of its store while it opens and
        // logs on are not a cell's subject.
        log->arm();
        for (int i = 0; i < app_messages; ++i) {
            auto fut = send_app();
            if (!rig.run_until([&] { return ready(fut); }) || !fut.get().has_value()) {
                return miss("an Engine::send of setup did not complete");
            }
        }
        auto const want = static_cast<std::size_t>(app_messages);
        if (!rig.run_until(
                [&] { return plain_rig::frames_of_type(rig.peer.received, "D").size() >= want; })) {
            return miss("the peer did not read the application messages of setup");
        }
        auto const apps = plain_rig::frames_of_type(rig.peer.received, "D");
        if (!apps.empty()) {
            first = static_cast<unsigned>(std::stoul(plain_rig::field_value(apps.front(), "34")));
            last = static_cast<unsigned>(std::stoul(plain_rig::field_value(apps.back(), "34")));
        }
        return true;
    }

    // Spawns Engine::stop without waiting for it. A cell that must act while the stop
    // is under way uses this, and finish() then completes it: Rig::stop() spawns and
    // waits in one call.
    void spawn_stop() { stop_fut_ = asio::co_spawn(rig.ioc, rig.engine->stop(), asio::use_future); }
    [[nodiscard]] bool stop_spawned() const noexcept { return stop_fut_.valid(); }
    // The stop spawn_stop() began has completed and finish() has not yet collected it.
    [[nodiscard]] bool spawned_stop_ready() const { return stop_fut_.valid() && ready(stop_fut_); }

    // Releases a hold in flight and completes the stop: the one spawn_stop() began, or
    // one of its own. Idempotent.
    void finish() {
        store_hold::release();
        if (!rig.engine || stop_completed) return;
        if (stop_fut_.valid()) {
            stop_completed = rig.run_until([this] { return ready(stop_fut_); });
            if (stop_completed) stop_fut_.get();
            return;
        }
        rig.stop();
        stop_completed = rig.engine->stopped();
    }

    // The peer's ResendRequest for [begin, end], as its MsgSeqNum(34) `seq`.
    void peer_resend_request(std::uint32_t seq, unsigned begin, unsigned end) {
        rig.peer.send(
            rig.msg("2", seq,
                    "7=" + std::to_string(begin) + "\x01" + "16=" + std::to_string(end) + "\x01"));
    }

    // Every frame the peer has read from byte `from` of its input on.
    [[nodiscard]] std::vector<std::string> peer_frames_from(std::size_t from) const {
        return store_hold::split_frames(std::string_view{rig.peer.received}.substr(from));
    }

private:
    std::future<void> stop_fut_;
};

}  // namespace fixpp::test_support
