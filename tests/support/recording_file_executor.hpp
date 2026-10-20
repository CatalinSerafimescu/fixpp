// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/support/recording_file_executor.hpp — test-only.
//
// Two file executors for the cells of fixpp#598 (.specify/536-537-538-recovery-liveness.md
// §11.5, "The fixture" and "The window, by events"; §5, the paragraph above the table for
// §11). Each wraps a thread pool's executor, hands every job to the pool at once, and
// reorders nothing.
//
//   DelayingExecutor    numbers the jobs handed to it once its JobLog is armed, at its
//                       execute(), and holds the pool thread that takes the first of them
//                       before that thread starts it, until the cell lets it go. The log
//                       records when that thread was taken and held, when each job began
//                       and ended, and when the store's offloaded body began (the store's
//                       offload probe, scoped_body_probe). It offers the wait the release
//                       event needs: until every job other than the held one has ended.
//                       A body is the call's only when it begins inside a job the
//                       executor numbered: the executor records the job's number on the
//                       pool thread around its invocation of the job, and the probe, which
//                       runs inside that invocation, reads it there. A body that begins
//                       while the log is armed in a job handed over before arm() is
//                       recorded as an event and counted apart (unattributed_bodies), and
//                       never as a body of the call.
//   UnorderedExecutor   the second form, for the scene in which two jobs run on two pool
//                       threads let go together (§11.5, "The measurement under
//                       ThreadSanitizer"). It takes no lock, and its atomics are
//                       memory_order_relaxed, because a lock or an ordered atomic between
//                       a job's hand-over and the wrapper's invocation of it would order
//                       the two jobs for ThreadSanitizer. It carries the seeded pair of
//                       §11.5, "The known positive": a plain int of the cell's, written
//                       just before the second job of an armed call is invoked and read
//                       just before the first is invoked. The pair is off unless the cell
//                       sets UnorderedLog::seed. It does not attribute a body to a job: a
//                       cell that reads its count of bodies arms the log only when no job
//                       handed over before the arm can still begin a body.
//
// Where it came from. The job log, the executor that wraps a pool's executor and numbers its
// jobs, its mode delay_first_in_its_thread, and the cell's job that takes a pool thread and
// holds it are lifted from batch B41's scratch patch, the section "PH8E (scratch only): the
// own-write scenes" of the parent's research/probes/b41_563_564/ph8e_cell_edit.patch.
// Left behind, by name: PoolGate and its mode (it keeps the first job back and lets later
// jobs pass, which reorders them); B41's Fixture, CloseCell and LogoutRoute; its
// CountingPoolExecutor and StoreCallHold (tests/session/held_file_store_scene.hpp is this
// branch's hold); and the margin of time by which its cells kept two jobs apart. Rewritten:
// the waits that polled a flag with a sleep are blocking waits released by the cell, each
// with a bound that is recorded when it is reached; the log is read by assertions.
//
// The store's offload probe is one global function pointer (install_store_offload_probe).
// A cell that installs scoped_body_probe replaces whatever probe was installed for its
// scope, held_file_store_scene.hpp's hold included. Declare the probe and the log before the
// pool, and a let_go_on_exit after it, so that no pool is joined with a thread still held.
//
// The including target needs the compile definition FIXPP_TEST_HOOKS, which declares
// install_store_offload_probe.
#pragma once

#include <asio/any_io_executor.hpp>
#include <asio/execution.hpp>
#include <asio/post.hpp>
#include <asio/thread_pool.hpp>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <fixpp/session/file_store.hpp>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "support/pump_until_ready.hpp"

namespace fixpp::test_support::file_executor {

// A held thread is let go by the cell, never by time. This bound only frees a pool thread
// when a cell has missed its own release; reaching it is recorded, and a cell that reads
// held_timed_out() fails.
inline constexpr auto kHoldBound = 3 * kPumpBudget;

// The number of the job of an armed JobLog that this thread is running, or 0 outside one.
// Per thread, so it orders nothing between threads.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
inline thread_local int t_job_number = 0;

// Sets t_job_number for one invocation of a job, and restores it on every path out.
struct job_number_scope {
    explicit job_number_scope(int k) noexcept : outer_{t_job_number} { t_job_number = k; }
    ~job_number_scope() { t_job_number = outer_; }
    job_number_scope(job_number_scope const&) = delete;
    job_number_scope& operator=(job_number_scope const&) = delete;

private:
    int outer_;
};

// ── The first form ────────────────────────────────────────────────────────────────────

class JobLog {
public:
    // The next job handed over is job 1 of the call under test.
    void arm() {
        std::lock_guard lk{m_};
        armed_ = true;
        handed_ = 0;
        began_.clear();
        ended_.clear();
        held_ = false;
        let_go_ = false;
        bodies_ = 0;
        bodies_after_let_go_ = 0;
        unattributed_bodies_ = 0;
        holders_in_ = 0;
        timed_out_ = false;
        events_.clear();
    }

    // At the executor's execute(): the job's number, or 0 when the log is not armed.
    [[nodiscard]] int hand_over() {
        std::lock_guard lk{m_};
        if (!armed_) return 0;
        ++handed_;
        began_.push_back(0);
        ended_.push_back(0);
        events_.push_back("job " + std::to_string(handed_) + " handed over");
        return handed_;
    }

    // Runs on the pool thread that has job 1, before the job starts, and waits there for the cell.
    void hold_taken_job() {
        std::unique_lock lk{m_};
        held_ = true;
        events_.emplace_back("job 1 taken by a pool thread, held before it starts");
        cv_.notify_all();
        if (!cv_.wait_for(lk, kHoldBound, [this] { return let_go_; })) timed_out_ = true;
    }

    void began(int k) {
        std::lock_guard lk{m_};
        began_.at(static_cast<std::size_t>(k - 1)) = 1;
        events_.push_back("job " + std::to_string(k) + " begins");
    }
    void ended(int k) {
        std::lock_guard lk{m_};
        ended_.at(static_cast<std::size_t>(k - 1)) = 1;
        events_.push_back("job " + std::to_string(k) + " ends");
        cv_.notify_all();
    }

    // The store's offload probe, on the pool thread, at the start of the offloaded body.
    // `job`: the number of the job it runs in, 0 when it runs in none.
    void body_began(int job) {
        std::lock_guard lk{m_};
        if (!armed_) return;
        if (job < 1) {
            ++unattributed_bodies_;
            events_.emplace_back(
                "the store's body begins in a job handed over before the arm (not counted)");
            return;
        }
        ++bodies_;
        if (let_go_) ++bodies_after_let_go_;
        events_.push_back("the store's body begins, in job " + std::to_string(job));
    }

    // A job of the cell that takes a thread of `pool` and holds it until let_go().
    void hold_a_thread(asio::thread_pool& pool) { hold_a_thread(pool.get_executor()); }
    void hold_a_thread(asio::thread_pool::executor_type const& pool) {
        asio::post(pool, [this] {
            std::unique_lock lk{m_};
            ++holders_in_;
            events_.emplace_back("a job of the cell holds a pool thread");
            cv_.notify_all();
            if (!cv_.wait_for(lk, kHoldBound, [this] { return let_go_; })) timed_out_ = true;
        });
    }

    void let_go() {
        {
            std::lock_guard lk{m_};
            let_go_ = true;
            events_.emplace_back("the cell lets the held thread go");
        }
        cv_.notify_all();
    }

    // The waits a cell takes. Each returns whether its condition held inside `bound`.
    [[nodiscard]] bool wait_held(std::chrono::steady_clock::duration bound = kPumpBudget) {
        std::unique_lock lk{m_};
        return cv_.wait_for(lk, bound, [this] { return held_; });
    }
    [[nodiscard]] bool wait_holders(int n,
                                    std::chrono::steady_clock::duration bound = kPumpBudget) {
        std::unique_lock lk{m_};
        return cv_.wait_for(lk, bound, [this, n] { return holders_in_ >= n; });
    }
    // The release event's last condition: every job of the call other than job 1 has ended.
    [[nodiscard]] bool wait_others_ended(std::chrono::steady_clock::duration bound = kPumpBudget) {
        std::unique_lock lk{m_};
        return cv_.wait_for(lk, bound, [this] {
            for (std::size_t i = 1; i < ended_.size(); ++i) {
                if (ended_[i] == 0) return false;
            }
            return true;
        });
    }

    [[nodiscard]] int handed() const {
        std::lock_guard lk{m_};
        return handed_;
    }
    [[nodiscard]] bool held() const {
        std::lock_guard lk{m_};
        return held_;
    }
    [[nodiscard]] bool job_began(int k) const {
        std::lock_guard lk{m_};
        return k >= 1 && static_cast<std::size_t>(k) <= began_.size() &&
               began_[static_cast<std::size_t>(k - 1)] != 0;
    }
    [[nodiscard]] bool job_ended(int k) const {
        std::lock_guard lk{m_};
        return k >= 1 && static_cast<std::size_t>(k) <= ended_.size() &&
               ended_[static_cast<std::size_t>(k - 1)] != 0;
    }
    [[nodiscard]] int bodies() const {
        std::lock_guard lk{m_};
        return bodies_;
    }
    [[nodiscard]] int bodies_after_let_go() const {
        std::lock_guard lk{m_};
        return bodies_after_let_go_;
    }
    [[nodiscard]] int unattributed_bodies() const {
        std::lock_guard lk{m_};
        return unattributed_bodies_;
    }
    [[nodiscard]] bool held_timed_out() const {
        std::lock_guard lk{m_};
        return timed_out_;
    }
    // The events in order, one per line, for a failure message.
    [[nodiscard]] std::string events() const {
        std::lock_guard lk{m_};
        std::string out;
        for (auto const& e : events_) out += "  " + e + "\n";
        return out.empty() ? std::string{"  (no event)\n"} : out;
    }

private:
    mutable std::mutex m_;
    std::condition_variable cv_;
    bool armed_ = false;
    int handed_ = 0;
    std::vector<char> began_;
    std::vector<char> ended_;
    bool held_ = false;
    bool let_go_ = false;
    int bodies_ = 0;
    int bodies_after_let_go_ = 0;
    int unattributed_bodies_ = 0;
    int holders_in_ = 0;
    bool timed_out_ = false;
    std::vector<std::string> events_;
};

struct DelayingExecutor {
    asio::thread_pool::executor_type inner;
    std::shared_ptr<JobLog> log;
    // Off: the executor numbers and records jobs and holds none (the one-thread controls,
    // where the cell's own job holds the pool's thread).
    bool delay_first_in_its_thread = true;

    asio::execution_context& query(asio::execution::context_t /*unused*/) const noexcept {
        return asio::query(inner, asio::execution::context);
    }
    static constexpr asio::execution::blocking_t query(
        asio::execution::blocking_t /*unused*/) noexcept {
        return asio::execution::blocking.never;
    }
    DelayingExecutor require(asio::execution::blocking_t::never_t /*unused*/) const {
        return *this;
    }
    template <class F>
    void execute(F&& f) const {
        auto const never = asio::require(inner, asio::execution::blocking.never);
        int const k = log->hand_over();
        if (k == 0) {
            never.execute(std::forward<F>(f));
            return;
        }
        bool const delay = delay_first_in_its_thread && k == 1;
        never.execute([l = log, k, delay, job = std::decay_t<F>(std::forward<F>(f))]() mutable {
            if (delay) l->hold_taken_job();
            l->began(k);
            {
                job_number_scope const in_job{k};
                std::move(job)();
            }
            l->ended(k);
        });
    }
    friend bool operator==(DelayingExecutor const& a, DelayingExecutor const& b) noexcept {
        return a.inner == b.inner && a.log == b.log &&
               a.delay_first_in_its_thread == b.delay_first_in_its_thread;
    }
};

// The store's offload probe writes into the log through this pointer: the probe is a plain
// function pointer.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
inline std::atomic<JobLog*> g_body_log{nullptr};

inline void body_probe(std::thread::id /*pool_thread*/) noexcept {
    if (JobLog* const l = g_body_log.load(std::memory_order_acquire)) l->body_began(t_job_number);
}

struct scoped_body_probe {
    explicit scoped_body_probe(JobLog& log) noexcept {
        g_body_log.store(&log, std::memory_order_release);
        fixpp::session::install_store_offload_probe(&body_probe);
    }
    ~scoped_body_probe() noexcept {
        fixpp::session::install_store_offload_probe(nullptr);
        g_body_log.store(nullptr, std::memory_order_release);
    }
    scoped_body_probe(scoped_body_probe const&) = delete;
    scoped_body_probe& operator=(scoped_body_probe const&) = delete;
};

// Lets every thread the log holds go when a scope is left, by any path. Declare it after
// the pool.
struct let_go_on_exit {
    std::shared_ptr<JobLog> log;
    ~let_go_on_exit() {
        try {
            if (log) log->let_go();
        } catch (...) {  // NOLINT(bugprone-empty-catch): nothing to report to from here
        }
    }
};

// ── The second form ───────────────────────────────────────────────────────────────────
//
// Every member is read and written by the test thread and by pool threads with
// memory_order_relaxed: none of them may order one pool thread's job against another's.

struct UnorderedLog {
    std::atomic<bool> armed{false};
    std::atomic<int> handed{0};
    std::atomic<int> bodies{0};
    std::atomic<int> holders_in{0};
    std::atomic<int> holders_out{0};
    std::atomic<bool> go{false};
    std::atomic<bool> holder_timed_out{false};
    // The seeded pair. Set by the cell before it arms the log, and left alone while a job of
    // an armed call can run.
    int* seed = nullptr;
    std::atomic<int> seed_sink{0};

    void arm() {
        handed.store(0, std::memory_order_relaxed);
        bodies.store(0, std::memory_order_relaxed);
        armed.store(true, std::memory_order_relaxed);
    }
    void disarm() { armed.store(false, std::memory_order_relaxed); }

    // Two jobs of the cell that take both threads of a pool of two and spin until go. A
    // spin, not a wait on a lock: a lock taken by both holders would order the two threads.
    void hold_both(asio::thread_pool& pool) {
        go.store(false, std::memory_order_relaxed);
        holders_in.store(0, std::memory_order_relaxed);
        holders_out.store(0, std::memory_order_relaxed);
        for (int i = 0; i < 2; ++i) {
            asio::post(pool, [this] {
                holders_in.fetch_add(1, std::memory_order_relaxed);
                auto const until = std::chrono::steady_clock::now() + kHoldBound;
                while (!go.load(std::memory_order_relaxed)) {
                    if (std::chrono::steady_clock::now() >= until) {
                        holder_timed_out.store(true, std::memory_order_relaxed);
                        break;
                    }
                    std::this_thread::yield();
                }
                holders_out.fetch_add(1, std::memory_order_relaxed);
            });
        }
    }
};

struct UnorderedExecutor {
    asio::thread_pool::executor_type inner;
    UnorderedLog* log = nullptr;

    asio::execution_context& query(asio::execution::context_t /*unused*/) const noexcept {
        return asio::query(inner, asio::execution::context);
    }
    static constexpr asio::execution::blocking_t query(
        asio::execution::blocking_t /*unused*/) noexcept {
        return asio::execution::blocking.never;
    }
    UnorderedExecutor require(asio::execution::blocking_t::never_t /*unused*/) const {
        return *this;
    }
    template <class F>
    void execute(F&& f) const {
        auto const never = asio::require(inner, asio::execution::blocking.never);
        if (!log->armed.load(std::memory_order_relaxed)) {
            never.execute(std::forward<F>(f));
            return;
        }
        int const k = log->handed.fetch_add(1, std::memory_order_relaxed) + 1;
        never.execute([l = log, k, job = std::decay_t<F>(std::forward<F>(f))]() mutable {
            if (l->seed != nullptr) {
                if (k == 2) {
                    *l->seed = k;
                } else if (k == 1) {
                    l->seed_sink.store(*l->seed, std::memory_order_relaxed);
                }
            }
            std::move(job)();
        });
    }
    friend bool operator==(UnorderedExecutor const& a, UnorderedExecutor const& b) noexcept {
        return a.inner == b.inner && a.log == b.log;
    }
};

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
inline std::atomic<UnorderedLog*> g_unordered_body_log{nullptr};

inline void unordered_body_probe(std::thread::id /*pool_thread*/) noexcept {
    if (UnorderedLog* const l = g_unordered_body_log.load(std::memory_order_relaxed)) {
        if (l->armed.load(std::memory_order_relaxed)) {
            l->bodies.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

struct scoped_unordered_body_probe {
    explicit scoped_unordered_body_probe(UnorderedLog& log) noexcept {
        g_unordered_body_log.store(&log, std::memory_order_relaxed);
        fixpp::session::install_store_offload_probe(&unordered_body_probe);
    }
    ~scoped_unordered_body_probe() noexcept {
        fixpp::session::install_store_offload_probe(nullptr);
        g_unordered_body_log.store(nullptr, std::memory_order_relaxed);
    }
    scoped_unordered_body_probe(scoped_unordered_body_probe const&) = delete;
    scoped_unordered_body_probe& operator=(scoped_unordered_body_probe const&) = delete;
};

}  // namespace fixpp::test_support::file_executor
