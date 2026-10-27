// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_624_offload_completion_lifetime.cpp
//
// fixpp#624, D-624-1 (.specify/624-offload-completion-lifetime.md): a FileStore call hands
// its file I/O to the file I/O executor and its completion back to the caller's executor.
// The hand-back runs on the file I/O thread, and that thread touches the caller's
// executor, and through it the caller's execution context, after the completion is
// already runnable on the caller's side. D-624-1 makes the caller wait, before the store
// call returns, until the file I/O job has made its last touch.
//
// The host order the hazard cells drive is legal: FileStore::Config::file_io_executor's
// precondition (include/fixpp/session/file_store.hpp) asks only that the file I/O pool
// keep running jobs until every store call has returned. It does not ask that pool to
// be joined before the caller's own execution context is destroyed; one file I/O pool
// can outlive many callers. Each hazard cell therefore owns the order itself, on
// heap-held contexts:
//
//   1. a store() call is awaited to completion on the caller's context;
//   2. the caller's execution context is destroyed, the file I/O pool still alive;
//   3. the hold below is released, if the caller's wait has not released it already;
//   4. the file I/O pool is joined and destroyed, then the FileStore.
//
// The hold. The caller's executor is wrapped in `holding_executor`, which forwards every
// operation to the executor it wraps. A call to its execute() from the file I/O thread
// is the completion hand-off: asio's dispatch and post both end in the target
// executor's execute(), whichever of the two the offload uses. That call hands the
// completion to the wrapped executor and then waits, still inside execute() and so
// still holding the caller's executor and whatever the hand-off path holds around it,
// until it is released. Two events release it, whichever comes first:
//   - the offload's caller-wait probe, which fires on the caller's thread when the
//     caller starts waiting for the job's last touch (installed through
//     install_store_offload_caller_wait_probe);
//   - the test thread at step 3, after the caller's execution context is gone.
// The hold reorders nothing: it only keeps open a window the file I/O thread already
// has. It is bounded, and a cell fails unless the hold engaged, the caller started its
// wait while the hold was engaged, and the hold was released within its bound.
//
//   CallerPool    the caller is a one-thread pool's own executor. A copy carrying
//                 outstanding work, and the pool's scheduler signalling its waiters,
//                 use the pool's scheduler.
//   CallerStrand  the caller is a strand over such a pool, as under the default
//                 per_session_strand (src/session/session_strand.cpp). The last strand
//                 copy, with or without outstanding work, destroys the strand
//                 implementation, whose destructor uses the pool's strand service. A
//                 change that only drops work tracking from the hand-off can therefore
//                 not make this cell pass.
//   ThrowingJob   CallerPool, with the offloaded job throwing (the one-shot seam
//                 arm_force_store_offload_throw_once): the exception reaches the caller
//                 through the same hand-off.
//
// The deadlock cells pass the caller's own executor as the file I/O executor: a
// single-thread pool, an io_context and a strand. The offload's job then runs on the
// caller's thread. A hand-back that could run the completion inline, inside the job,
// would resume the caller before the job's last touch, and the caller's wait would
// never end; each cell bounds the call and fails instead of hanging.

#include <gtest/gtest.h>

#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/execution.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/prefer.hpp>
#include <asio/query.hpp>
#include <asio/require.hpp>
#include <asio/strand.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_future.hpp>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/file_store.hpp>
#include <fixpp/session/file_store_factory.hpp>
#include <fixpp/session/message_store.hpp>
#include <future>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

#include "_fixtures_/test_double_fsm.hpp"
#include "support/temp_dir.hpp"

namespace {

using fixpp::session::direction_t;
using fixpp::session::FileStore;
using fixpp::session::FileStoreFactory;
using fixpp::session::MessageStore;
using fixpp::store_test::make_test_frame;

// Long enough that a hold which is released on time never reaches it, short enough that
// a hold which is never released fails the cell well inside the ctest timeout.
constexpr std::chrono::seconds kBound{10};

// The hold's flags and its latch, shared by every wrapper copy. Declared before every
// context in a cell, so it outlives every copy.
struct hold_state {
    std::thread::id file_io_thread;
    std::atomic<int> held{0};
    std::atomic<int> caller_waits{0};
    std::atomic<bool> timed_out{false};

    std::mutex m;
    std::condition_variable cv;
    bool released = false;
    bool caller_waited_while_held = false;  // under m

    void release() {
        {
            std::scoped_lock const lock{m};
            released = true;
        }
        cv.notify_all();
    }

    // Called from the wrapper's execute() on the file I/O thread, before the wrapped
    // executor is handed the completion: the caller cannot resume before this is counted.
    void engage() noexcept {
        held.fetch_add(1);
        // Printed before the wait, so a run that a sanitizer ends still shows the hold.
        // NOLINTNEXTLINE(modernize-use-std-print): libstdc++ 13 has no <print>
        std::fprintf(stderr, "[fixpp#624] hold engaged on the file I/O thread\n");
    }

    // Called from the wrapper's execute() on the file I/O thread, after the hand-off.
    void hold() noexcept {
        std::unique_lock lock{m};
        if (!cv.wait_for(lock, kBound, [this] { return released; })) timed_out.store(true);
    }

    // The caller-wait probe: on the caller's thread, as the caller starts waiting for the
    // job's last touch.
    void caller_starts_waiting() noexcept {
        caller_waits.fetch_add(1);
        {
            std::scoped_lock const lock{m};
            if (held.load() > 0 && !released) caller_waited_while_held = true;
        }
        // NOLINTNEXTLINE(modernize-use-std-print): libstdc++ 13 has no <print>
        std::fprintf(stderr, "[fixpp#624] the caller starts waiting for the job's last touch\n");
        release();
    }
};

// The probe is a plain function pointer, so it reaches the cell's state through this.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables): the probe's only route
std::atomic<hold_state*> g_hold{nullptr};

void on_caller_wait() noexcept {
    if (hold_state* const st = g_hold.load()) st->caller_starts_waiting();
}

// Installs the caller-wait probe for one cell. Uninstalled by the destructor, which runs
// after the cell has joined both pools.
struct probe_scope {
    explicit probe_scope(hold_state* st) {
        g_hold.store(st);
        fixpp::session::install_store_offload_caller_wait_probe(&on_caller_wait);
    }
    ~probe_scope() {
        fixpp::session::install_store_offload_caller_wait_probe(nullptr);
        g_hold.store(nullptr);
    }
    probe_scope(probe_scope const&) = delete;
    probe_scope& operator=(probe_scope const&) = delete;
};

template <class Inner>
class holding_executor {
public:
    holding_executor(Inner inner, hold_state* st) noexcept : inner_(std::move(inner)), st_(st) {}

    template <class F>
    void execute(F&& f) const {
        if (std::this_thread::get_id() != st_->file_io_thread) {
            inner_.execute(std::forward<F>(f));
            return;
        }
        st_->engage();
        inner_.execute(std::forward<F>(f));
        st_->hold();
    }

    [[nodiscard]] asio::execution_context& query(
        asio::execution::context_t /*prop*/) const noexcept {
        return asio::query(inner_, asio::execution::context);
    }

    // any_io_executor probes context_as<execution_context&>, which the constrained
    // template below does not admit for a strand (as in session_executor.hpp).
    template <class U>
    [[nodiscard]] U query(asio::execution::context_as_t<U> /*prop*/) const noexcept {
        return asio::query(inner_, asio::execution::context);
    }

    template <class Property>
        requires asio::can_query_v<Inner const&, Property>
    [[nodiscard]] auto query(Property const& p) const
        noexcept(asio::is_nothrow_query_v<Inner const&, Property>) {
        return asio::query(inner_, p);
    }

    template <class Property>
        requires asio::can_require_v<Inner const&, Property>
    [[nodiscard]] auto require(Property const& p) const {
        using next = std::decay_t<asio::require_result_t<Inner const&, Property>>;
        return holding_executor<next>{asio::require(inner_, p), st_};
    }

    template <class Property>
        requires asio::can_prefer_v<Inner const&, Property>
    [[nodiscard]] auto prefer(Property const& p) const {
        using next = std::decay_t<asio::prefer_result_t<Inner const&, Property>>;
        return holding_executor<next>{asio::prefer(inner_, p), st_};
    }

    bool operator==(holding_executor const& other) const noexcept {
        return inner_ == other.inner_ && st_ == other.st_;
    }
    bool operator!=(holding_executor const& other) const noexcept { return !(*this == other); }

private:
    template <class>
    friend class holding_executor;

    Inner inner_;
    hold_state* st_;
};

static_assert(asio::execution::is_executor_v<holding_executor<asio::thread_pool::executor_type>>);
static_assert(asio::execution::is_executor_v<
              holding_executor<asio::strand<asio::thread_pool::executor_type>>>);

// The FileStore's directory, removed after the store over it is destroyed.
struct temp_dir {
    std::filesystem::path path = fixpp::test_support::unique_temp_dir("fixpp624_offload");
    temp_dir() = default;
    ~temp_dir() { (void)fixpp::test_support::try_remove_temp_dir(path); }
    temp_dir(temp_dir const&) = delete;
    temp_dir& operator=(temp_dir const&) = delete;
};

std::unique_ptr<MessageStore> make_file_store(std::filesystem::path const& dir,
                                              asio::any_io_executor const& file_io) {
    FileStore::Config cfg;
    cfg.directory = dir;
    cfg.sender_comp_id = "SENDER";
    cfg.target_comp_id = "TARGET";
    cfg.max_frame_bytes = 4096;
    cfg.file_io_executor = file_io;
    auto made =
        FileStoreFactory{cfg}.make("SENDER", "TARGET", nullptr, std::size_t{1} << 30, file_io);
    return made ? std::move(*made) : nullptr;
}

// What one store(1) call ended as, seen from the awaiting coroutine.
struct store_outcome {
    bool returned = false;
    bool stored = false;
    bool threw = false;
    std::string what;
};

asio::awaitable<void> store_one(MessageStore& store, store_outcome& out) {
    auto const frame = make_test_frame(1, direction_t::outbound);
    try {
        auto const r =
            co_await store.store(1, std::span<const std::byte>{frame}, direction_t::outbound);
        out.returned = true;
        out.stored = r.has_value();
    } catch (std::runtime_error const& e) {
        out.threw = true;
        out.what = e.what();
    }
}

enum class job_kind : std::uint8_t { returns, throws };

// One hazard cell. `make_caller` builds the caller's executor over the caller's pool.
template <class MakeCaller>
void run_cell(MakeCaller make_caller, job_kind const kind) {
    hold_state st;  // first: outlives every wrapper copy
    probe_scope const probe{&st};
    temp_dir dir;
    auto file_io = std::make_unique<asio::thread_pool>(1);
    {
        std::promise<std::thread::id> id;
        asio::post(*file_io, [&id] { id.set_value(std::this_thread::get_id()); });
        st.file_io_thread = id.get_future().get();
    }
    auto store = make_file_store(dir.path, file_io->get_executor());
    ASSERT_NE(store, nullptr) << "setup: the FileStore";

    auto caller = std::make_unique<asio::thread_pool>(1);
    store_outcome out;
    if (kind == job_kind::throws) fixpp::session::arm_force_store_offload_throw_once();
    {
        auto const ex = make_caller(*caller, &st);
        // Step 1, the direct caller's obligation: the store call is awaited to
        // completion before anything is joined or destroyed.
        auto done = asio::co_spawn(
            ex, [&]() -> asio::awaitable<void> { co_await store_one(*store, out); },
            asio::use_future);
        if (done.wait_for(kBound) != std::future_status::ready) {
            ADD_FAILURE() << "the store call did not complete while the hold was closed";
            st.release();
        }
        done.get();
    }  // the test thread's copies of the caller's executor are gone

    caller.reset();   // step 2: the caller's execution context is destroyed
    st.release();     // step 3
    file_io.reset();  // step 4
    store = nullptr;

    if (kind == job_kind::returns) {
        EXPECT_TRUE(out.returned && out.stored && !out.threw)
            << "store(1) must be accepted; threw: " << out.what;
    } else {
        EXPECT_TRUE(out.threw && !out.returned)
            << "the offloaded job's exception must reach the store call's awaiter";
        EXPECT_EQ(out.what, "fixpp test seam: the offloaded job threw");
    }
    EXPECT_GE(st.held.load(), 1)
        << "the hold never engaged: no completion was handed to the caller's executor from "
           "the file I/O thread";
    {
        std::scoped_lock const lock{st.m};
        EXPECT_TRUE(st.caller_waited_while_held)
            << "the store call's caller did not wait for the file I/O job while the file I/O "
               "thread was still inside the hand-off (caller-wait probe fired "
            << st.caller_waits.load() << " time(s))";
    }
    EXPECT_FALSE(st.timed_out.load()) << "the hold was not released within its bound";
    // NOLINTNEXTLINE(modernize-use-std-print): libstdc++ 13 has no <print>
    std::printf("[fixpp#624] hand-offs held: %d; caller waits: %d\n", st.held.load(),
                st.caller_waits.load());
}

auto bare_caller() {
    return [](asio::thread_pool& pool, hold_state* st) {
        return holding_executor<asio::thread_pool::executor_type>{pool.get_executor(), st};
    };
}

auto strand_caller() {
    return [](asio::thread_pool& pool, hold_state* st) {
        return holding_executor<asio::strand<asio::thread_pool::executor_type>>{
            asio::make_strand(pool.get_executor()), st};
    };
}

TEST(OffloadCompletionLifetime, CallerPool_DestroyedBeforeTheFileIoPool) {
    run_cell(bare_caller(), job_kind::returns);
}

TEST(OffloadCompletionLifetime, CallerStrand_DestroyedBeforeTheFileIoPool) {
    run_cell(strand_caller(), job_kind::returns);
}

TEST(OffloadCompletionLifetime, ThrowingJob_CallerPool_DestroyedBeforeTheFileIoPool) {
    run_cell(bare_caller(), job_kind::throws);
}

// ── The file I/O executor is the caller's own ────────────────────────────────────────

// A store call that does not complete leaves a thread spinning in the caller's wait, which
// no teardown can join, so the process ends here with the failure on stderr.
void complete_or_exit(std::future<void>& done, char const* cell) {
    if (done.wait_for(kBound) == std::future_status::ready) return;
    // NOLINTNEXTLINE(modernize-use-std-print): libstdc++ 13 has no <print>
    std::fprintf(stderr, "[fixpp#624] %s: the store call did not complete; exiting\n", cell);
    std::fflush(stderr);
    std::_Exit(1);
}

TEST(OffloadCompletionLifetime, SameExecutor_SingleThreadPool_Completes) {
    temp_dir dir;
    asio::thread_pool pool{1};
    auto store = make_file_store(dir.path, pool.get_executor());
    ASSERT_NE(store, nullptr) << "setup: the FileStore";
    store_outcome out;
    auto done = asio::co_spawn(
        pool, [&]() -> asio::awaitable<void> { co_await store_one(*store, out); },
        asio::use_future);
    complete_or_exit(done, "SingleThreadPool");
    done.get();
    pool.join();
    EXPECT_TRUE(out.returned && out.stored) << "store(1) must be accepted; threw: " << out.what;
}

TEST(OffloadCompletionLifetime, SameExecutor_IoContext_Completes) {
    temp_dir dir;
    asio::io_context ioc;
    auto store = make_file_store(dir.path, ioc.get_executor());
    ASSERT_NE(store, nullptr) << "setup: the FileStore";
    store_outcome out;
    auto done = asio::co_spawn(
        ioc, [&]() -> asio::awaitable<void> { co_await store_one(*store, out); }, asio::use_future);
    std::thread runner{[&ioc] { ioc.run(); }};
    complete_or_exit(done, "IoContext");
    done.get();
    runner.join();
    EXPECT_TRUE(out.returned && out.stored) << "store(1) must be accepted; threw: " << out.what;
}

TEST(OffloadCompletionLifetime, SameExecutor_Strand_Completes) {
    temp_dir dir;
    asio::thread_pool pool{1};
    asio::any_io_executor const sx{asio::make_strand(pool.get_executor())};
    auto store = make_file_store(dir.path, sx);
    ASSERT_NE(store, nullptr) << "setup: the FileStore";
    store_outcome out;
    auto done = asio::co_spawn(
        sx, [&]() -> asio::awaitable<void> { co_await store_one(*store, out); }, asio::use_future);
    complete_or_exit(done, "Strand");
    done.get();
    pool.join();
    EXPECT_TRUE(out.returned && out.stored) << "store(1) must be accepted; threw: " << out.what;
}

}  // namespace
