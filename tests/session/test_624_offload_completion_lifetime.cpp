// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_624_offload_completion_lifetime.cpp
//
// fixpp#624: a FileStore call hands its file I/O to the file I/O executor and its
// completion back to the caller's executor. The hand-back runs on the file I/O thread,
// and that thread may go on holding copies of the caller's executor after the
// completion has been handed off. A copy that carries outstanding work, or the last
// copy of a strand, touches the caller's execution context when it is destroyed.
//
// The host order these cells drive is legal: FileStore::Config::file_io_executor's
// precondition (include/fixpp/session/file_store.hpp) asks only that the file I/O pool
// keep running jobs until every store call has returned. It does not ask that pool to
// be joined before the caller's own execution context is destroyed; one file I/O pool
// can outlive many callers. Each cell therefore owns the order itself, on heap-held
// contexts:
//
//   1. a store() call is awaited to completion on the caller's context;
//   2. the caller's execution context is destroyed, the file I/O pool still alive;
//   3. the hold below is released;
//   4. the file I/O pool is joined and destroyed, then the FileStore.
//
// The hold. The caller's executor is wrapped in `holding_executor`, which forwards every
// operation to the executor it wraps. A call to its execute() from the file I/O thread
// is the completion hand-off. Every wrapper copy destroyed on the file I/O thread after
// that hand-off waits, in its destructor body and so before the wrapped executor is
// destroyed, until the test thread has done step 2. The hold reorders nothing: it only
// keeps open a window the file I/O thread already has. It is bounded, and a cell fails
// unless the hold is released within its bound, a hand-off is seen and a copy is held.
// A copy without outstanding work is held too: holding a harmless copy changes no
// verdict, and it keeps "the hold engaged" a property of the hand-off path rather
// than of how that path tracks work.
//
//   CallerPool    the caller is a one-thread pool's own executor. A copy carrying
//                 outstanding work calls the pool's scheduler when destroyed.
//   CallerStrand  the caller is a strand over such a pool, as under the default
//                 per_session_strand (src/session/session_strand.cpp). The last strand
//                 copy, with or without outstanding work, destroys the strand
//                 implementation, whose destructor uses the pool's strand service. A
//                 change that only drops work tracking from the hand-off can therefore
//                 not make this cell pass.
//
// Re-derive the copies the file I/O thread destroys from asio's co_spawn_entry_point
// (asio/impl/co_spawn.hpp): the `(dispatch)(s.handler_work.get_executor(), ...)` call
// and the coroutine frame that owns `s`.

#include <gtest/gtest.h>

#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/execution.hpp>
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
#include <cstdio>
#include <filesystem>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/file_store.hpp>
#include <fixpp/session/file_store_factory.hpp>
#include <fixpp/session/message_store.hpp>
#include <future>
#include <memory>
#include <mutex>
#include <span>
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
    std::atomic<bool> handed_off{false};
    std::atomic<int> held{0};
    std::atomic<int> held_tracked{0};
    std::atomic<bool> timed_out{false};

    std::mutex m;
    std::condition_variable cv;
    bool released = false;

    void release() {
        {
            std::lock_guard lock{m};
            released = true;
        }
        cv.notify_all();
    }

    // Called from a wrapper copy's destructor on the file I/O thread, after the hand-off.
    void hold(bool const tracked) noexcept {
        held.fetch_add(1);
        if (tracked) held_tracked.fetch_add(1);
        // Printed before the wait, so a run that a sanitizer ends still shows the hold.
        std::fprintf(stderr, "[fixpp#624] hold engaged on the file I/O thread (%s copy)\n",
                     tracked ? "work-tracking" : "untracked");
        std::unique_lock lock{m};
        if (!cv.wait_for(lock, kBound, [this] { return released; })) timed_out.store(true);
    }
};

template <class Inner>
class holding_executor {
public:
    holding_executor(Inner inner, hold_state* st) noexcept : inner_(std::move(inner)), st_(st) {}
    holding_executor(holding_executor const&) noexcept = default;
    holding_executor(holding_executor&&) noexcept = default;
    holding_executor& operator=(holding_executor const&) noexcept = default;
    holding_executor& operator=(holding_executor&&) noexcept = default;

    // The body runs before `inner_` is destroyed, so the hold sits in front of whatever
    // the wrapped executor's destructor does to its context.
    ~holding_executor() {
        if (st_ != nullptr && st_->handed_off.load() &&
            std::this_thread::get_id() == st_->file_io_thread) {
            st_->hold(asio::query(inner_, asio::execution::outstanding_work) ==
                      asio::execution::outstanding_work.tracked);
        }
    }

    template <class F>
    void execute(F&& f) const {
        if (std::this_thread::get_id() == st_->file_io_thread) st_->handed_off.store(true);
        inner_.execute(std::forward<F>(f));
    }

    asio::execution_context& query(asio::execution::context_t /*prop*/) const noexcept {
        return asio::query(inner_, asio::execution::context);
    }

    // any_io_executor probes context_as<execution_context&>, which the constrained
    // template below does not admit for a strand (as in session_executor.hpp).
    template <class U>
    U query(asio::execution::context_as_t<U> /*prop*/) const noexcept {
        return asio::query(inner_, asio::execution::context);
    }

    template <class Property>
        requires asio::can_query_v<Inner const&, Property>
    auto query(Property const& p) const noexcept(asio::is_nothrow_query_v<Inner const&, Property>) {
        return asio::query(inner_, p);
    }

    template <class Property>
        requires asio::can_require_v<Inner const&, Property>
    auto require(Property const& p) const {
        using next = std::decay_t<asio::require_result_t<Inner const&, Property>>;
        return holding_executor<next>{asio::require(inner_, p), st_};
    }

    template <class Property>
        requires asio::can_prefer_v<Inner const&, Property>
    auto prefer(Property const& p) const {
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

// One cell. `make_caller` builds the caller's executor over the caller's pool.
template <class MakeCaller>
void run_cell(MakeCaller make_caller) {
    hold_state st;  // first: outlives every wrapper copy
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
    bool stored = false;
    {
        auto const ex = make_caller(*caller, &st);
        // Step 1, the direct caller's obligation: the store call is awaited to
        // completion before anything is joined or destroyed.
        auto done = asio::co_spawn(
            ex,
            [&]() -> asio::awaitable<void> {
                auto const frame = make_test_frame(1, direction_t::outbound);
                auto const r = co_await store->store(1, std::span<const std::byte>{frame},
                                                     direction_t::outbound);
                stored = r.has_value();
            },
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
    store.reset();

    EXPECT_TRUE(stored) << "store(1) must be accepted";
    EXPECT_TRUE(st.handed_off.load())
        << "the path was not exercised: no completion was handed to the caller's executor "
           "from the file I/O thread";
    EXPECT_GE(st.held.load(), 1)
        << "the hold never engaged: no copy of the caller's executor was destroyed on the "
           "file I/O thread after the hand-off";
    EXPECT_FALSE(st.timed_out.load()) << "the hold was not released within its bound";
    std::printf(
        "[fixpp#624] copies held on the file I/O thread after the hand-off: %d "
        "(with outstanding work: %d)\n",
        st.held.load(), st.held_tracked.load());
}

TEST(OffloadCompletionLifetime, CallerPool_DestroyedBeforeTheFileIoPool) {
    run_cell([](asio::thread_pool& pool, hold_state* st) {
        return holding_executor<asio::thread_pool::executor_type>{pool.get_executor(), st};
    });
}

TEST(OffloadCompletionLifetime, CallerStrand_DestroyedBeforeTheFileIoPool) {
    run_cell([](asio::thread_pool& pool, hold_state* st) {
        return holding_executor<asio::strand<asio::thread_pool::executor_type>>{
            asio::make_strand(pool.get_executor()), st};
    });
}

}  // namespace
