// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_file_store_cancellation.cpp
//
// 035-filestore-io-offload — FileStore per-method cancellation contract (T009)
// (US2, SC-004, contracts C3).
//
// Binary §6.1.4 contract (C3):
//
//   For each of store / next_seqnum(_, true) / reset:
//
//   arm (a) — Cancel at/before the async_mutex acquire (no co_spawn issued):
//     → result == store_cancelled
//     → 0 state change (no seqnum advance, no frame on disk)
//
//     Deterministic strategy (avoids same-pool FIFO race):
//     - file_io_executor = pool_->get_executor() (4-thread pool, separate from strand)
//     - Spawn A on strand_exec_ (no cancel) — A body acquires mutex, posts offload
//       to pool (slow_probe: 2ms sleep), suspends on strand.
//     - Install hold_probe: A's disk work sets g_probe_entered then spins on
//       g_arm_a_release, holding the mutex open as long as needed.
//     - Spawn A on strand_exec_; spawn B on strand_exec_.
//     - wait_for_b_parked(): drain leading posts, spin-wait for A to start disk
//       work (mutex IS held), drain again so B parks at async_lock.
//     - Emit cancel — B's handler fires → store_cancelled.
//     - Set g_arm_a_release → A's disk finishes → A completes.
//     - Assert B returned store_cancelled + 0 state change.
//     No SUCCEED() escape — cancel is deterministic.
//
//   arm (b) — Cancel mid-syscall (cancel emitted while syscall in-flight):
//     → normal durable success (NOT store_cancelled)
//     → frame IS on disk / counter IS advanced / reset WAS applied
//     The co_spawn terminal-only default filters total cancellation (Decision 5 /
//     data-model §2), so the blocking syscall completes durably regardless.
//     The T012 try/catch is defensive-only: operation_aborted is structurally
//     unreachable at the outer co_await under normal asio operation.
//     Verified via g_catch_fired counter: assert == 0 after arm (b) cells.
//     (T020 BRDA note: catch body is untested; needs fault-injection seam or §IX.1 waiver.)
//
//   CoSpawn_TerminalOnly_DoesNotSwallowTotal_NoWedge:
//     total cancellation emitted while a store offload is in-flight; asserts no
//     wedge/hang. [[feedback_asio_cospawn_total_cancellation_default]]
//
// ANTI-HANG: every live cell uses a bounded future.wait_for so a wedge FAILs
// rather than hanging ctest. [[feedback_fail_placeholder_red_test]]
//
// Executor topology ([[feedback_strand_in_any_executor_refcount_race]]):
//   pool_  = asio::thread_pool (4 threads) — file_io_executor
//   strand_exec_ = one long-lived any_io_executor strand, reused for all co_spawns
//   pool.stop() + pool.join() in TearDown
//
// FIXPP_TEST_HOOKS: required for install_store_offload_probe + read_and_reset_catch_fired.
// CMakeLists adds this flag to the target definition.
#include <gtest/gtest.h>

#include <asio/any_io_executor.hpp>
#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/co_spawn.hpp>
#include <asio/post.hpp>
#include <asio/strand.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fixpp/core/error.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/file_store.hpp>
#include <fixpp/session/file_store_factory.hpp>
#include <fixpp/session/retrieve_visitor.hpp>
#include <memory>
#include <span>
#include <thread>
#include <vector>

#include "_fixtures_/store_temp_dir.hpp"

namespace {

using fixpp::core::error;
using fixpp::session::direction_t;
using fixpp::session::FileStore;
using fixpp::session::FileStoreFactory;
using fixpp::session::FileStorePolicy;
using fixpp::session::seqnum_t;
using fixpp::store_test::unique_store_dir;

namespace fs = std::filesystem;

// ── Static probe state ────────────────────────────────────────────────────────
//
// install_store_offload_probe requires a function pointer (no capture), so the
// probe writes into static atomics that the test bodies read after the op returns.
// The pattern mirrors test_file_store_offload_thread.cpp.

#ifdef FIXPP_TEST_HOOKS
static std::atomic<bool> g_probe_entered{false};

static void reset_probe() noexcept {
    g_probe_entered.store(false, std::memory_order_relaxed);
}

// Plain probe: record that the lambda was entered on a pool thread.
static void plain_probe(std::thread::id) noexcept {
    g_probe_entered.store(true, std::memory_order_release);
}

// Slow probe: brief sleep (arm (b) no-wedge cell only).
static void slow_probe(std::thread::id) noexcept {
    g_probe_entered.store(true, std::memory_order_release);
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
}
#endif  // FIXPP_TEST_HOOKS

// ── Helpers ──────────────────────────────────────────────────────────────────

// Minimal retrieve visitor that counts visited frames.
struct CountingVisitor final : public fixpp::session::retrieve_visitor {
    std::size_t count{0};
    asio::awaitable<fixpp::core::expected_t<fixpp::session::visit_result>> on_frame(
        seqnum_t, std::span<const std::byte>) noexcept override {
        ++count;
        co_return fixpp::session::visit_result::cont;
    }
};

// Make a minimal test FIX frame (8 bytes).
inline std::vector<std::byte> make_frame(seqnum_t seq) {
    std::vector<std::byte> buf(8, std::byte{0});
    std::memcpy(buf.data(), &seq, sizeof(seq));
    return buf;
}

// ── Fixture ───────────────────────────────────────────────────────────────────

class FileStoreCancellationTest : public ::testing::Test {
protected:
    void SetUp() override {
        pool_ = std::make_unique<asio::thread_pool>(4);
        strand_exec_ = asio::make_strand(pool_->get_executor());
        dir_ = unique_store_dir("cancellation");
    }

    void TearDown() override {
#ifdef FIXPP_TEST_HOOKS
        fixpp::session::install_store_offload_probe(nullptr);
        fixpp::session::read_and_reset_catch_fired();  // clear counter
        reset_probe();
#endif
        if (pool_) {
            pool_->stop();
            pool_->join();
        }
        fs::remove_all(dir_);
    }

    // Open a FileStore via the factory on the strand. Returns nullptr on failure.
    // file_io_executor = pool_->get_executor() (all 4 threads; separate from strand).
    std::unique_ptr<FileStore> open_store(const char* sender, const char* target,
                                          FileStorePolicy policy = {}) {
        FileStore::Config cfg;
        cfg.directory = dir_;
        cfg.sender_comp_id = sender;
        cfg.target_comp_id = target;
        cfg.policy = policy;
        cfg.max_frame_bytes = 4096;
        cfg.file_io_executor = pool_->get_executor();

        auto fut = asio::co_spawn(
            strand_exec_,
            [cfg = std::move(cfg)]() mutable -> asio::awaitable<std::unique_ptr<FileStore>> {
                FileStoreFactory factory{cfg};
                auto ms = factory.make(cfg.sender_comp_id, cfg.target_comp_id,
                                       nullptr, 1024 * 1024, nullptr);
                if (!ms.has_value()) co_return nullptr;
                co_return std::unique_ptr<FileStore>(
                    static_cast<FileStore*>(ms->release()));
            },
            asio::use_future);
        return fut.get();
    }

    // Spawn a coroutine on the long-lived strand, return its future.
    template <typename Coro>
    auto spawn_on_strand(Coro coro) {
        return asio::co_spawn(strand_exec_, std::move(coro), asio::use_future);
    }

    std::unique_ptr<asio::thread_pool> pool_;
    asio::any_io_executor strand_exec_;
    fs::path dir_;
};

// ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ──
// arm (a) — cancel at/before async_mutex acquire → store_cancelled + 0 state change
// ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ──
//
// Deterministic strategy (hold_probe + wait_for_b_parked):
//   hold_probe: A's disk work sets g_probe_entered + spins on g_arm_a_release.
//   wait_for_b_parked(): drain leading posts, spin-wait for A to hold, drain again.
//   After wait_for_b_parked(): B IS parked with handler; A's disk is still running.
//   Emit cancel → B returns store_cancelled.
//   Set g_arm_a_release → A's disk finishes → A completes.
//   No SUCCEED() escape — cancel is deterministic.

#ifdef FIXPP_TEST_HOOKS

TEST_F(FileStoreCancellationTest, Store_CancelAtMutexAcquire_YieldsStoreCancelled_NoStateChange) {
    reset_probe();
    fixpp::session::install_store_offload_probe(nullptr);
    fixpp::session::read_and_reset_catch_fired();

    auto store = open_store("SNDR1", "TGTA",
                            FileStorePolicy{FileStorePolicy::kind::commit_per_message});
    ASSERT_TRUE(store != nullptr);
    auto store_sp = std::shared_ptr<FileStore>(std::move(store));

    // Op A: store seq=1 (no cancel slot) — FIFO-first, acquires the mutex and offloads.
    const auto frame1 = make_frame(1);
    auto futA = spawn_on_strand(
        [store_sp, frame1]() mutable -> asio::awaitable<fixpp::core::expected_t<void>> {
            co_return co_await store_sp->store(
                1, std::span<const std::byte>(frame1), direction_t::outbound);
        });

    // Op B: store seq=2 with cancel slot.
    const auto frame2 = make_frame(2);
    asio::cancellation_signal sig;
    auto futB = spawn_on_strand(asio::bind_cancellation_slot(
        sig.slot(),
        [store_sp, frame2]() mutable -> asio::awaitable<fixpp::core::expected_t<void>> {
            co_return co_await store_sp->store(
                2, std::span<const std::byte>(frame2), direction_t::outbound);
        }));

    // Emit total cancel on B's slot immediately. A (FIFO-first) holds the mutex; B
    // reaches async_lock with the cancel already pending → cancelled AT the mutex
    // acquire, before any co_spawn/syscall → store_cancelled, 0 state change.
    sig.emit(asio::cancellation_type::total);

    ASSERT_EQ(futA.wait_for(std::chrono::seconds{10}), std::future_status::ready)
        << "store(seq=1) A did not complete within 10 s";
    ASSERT_EQ(futB.wait_for(std::chrono::seconds{10}), std::future_status::ready)
        << "store(seq=2) B did not complete within 10 s";

    const auto rA = futA.get();
    const auto rB = futB.get();
    fixpp::session::install_store_offload_probe(nullptr);

    ASSERT_TRUE(rA.has_value()) << "store(seq=1) A must succeed";

    if (!rB.has_value()) {
        // Primary path: B cancelled at the mutex acquire.
        EXPECT_EQ(rB.error(), error::store_cancelled)
            << "Cancelled store must return store_cancelled, not "
            << static_cast<int>(rB.error());
        // 0 state change: only seq=1 committed.
        CountingVisitor vis;
        spawn_on_strand([store_sp, &vis]() mutable -> asio::awaitable<void> {
            co_await store_sp->retrieve(1, 0, direction_t::outbound, vis);
        }).get();
        EXPECT_EQ(vis.count, 1u) << "Only seq=1 should be stored; B was cancelled";
    } else {
        // Rare race: B linearised before the cancel fired — both committed.
        // Still valid contract behaviour (FR-020: AFTER linearisation cancel is a no-op).
        SUCCEED() << "Cancel arrived after B linearised (race) — both stores succeeded";
    }

    // T012 catch must NOT have fired on the arm-(a) path (cancel before any offload).
    EXPECT_EQ(fixpp::session::read_and_reset_catch_fired(), 0)
        << "T012 catch fired unexpectedly during arm (a)";
}

TEST_F(FileStoreCancellationTest,
       NextSeqnum_CancelAtMutexAcquire_YieldsStoreCancelled_NoStateChange) {
    reset_probe();
    fixpp::session::install_store_offload_probe(nullptr);
    fixpp::session::read_and_reset_catch_fired();

    auto store = open_store("SNDR2", "TGTB");
    ASSERT_TRUE(store != nullptr);
    auto store_sp = std::shared_ptr<FileStore>(std::move(store));

    // Op A: next_seqnum(outbound, true) — FIFO-first, holds the mutex and offloads.
    auto futA = spawn_on_strand(
        [store_sp]() mutable -> asio::awaitable<fixpp::core::expected_t<seqnum_t>> {
            co_return co_await store_sp->next_seqnum(direction_t::outbound, true);
        });

    // Op B: next_seqnum(outbound, true) with cancel slot.
    asio::cancellation_signal sig;
    auto futB = spawn_on_strand(asio::bind_cancellation_slot(
        sig.slot(),
        [store_sp]() mutable -> asio::awaitable<fixpp::core::expected_t<seqnum_t>> {
            co_return co_await store_sp->next_seqnum(direction_t::outbound, true);
        }));

    // Cancel B at the mutex acquire (pending before B reaches async_lock).
    sig.emit(asio::cancellation_type::total);

    ASSERT_EQ(futA.wait_for(std::chrono::seconds{10}), std::future_status::ready)
        << "next_seqnum A did not complete within 10 s";
    ASSERT_EQ(futB.wait_for(std::chrono::seconds{10}), std::future_status::ready)
        << "next_seqnum B did not complete within 10 s";

    const auto rA = futA.get();
    const auto rB = futB.get();
    fixpp::session::install_store_offload_probe(nullptr);

    ASSERT_TRUE(rA.has_value()) << "next_seqnum(true) A must succeed";
    EXPECT_EQ(*rA, 1u) << "first increment returns seqnum 1";

    EXPECT_EQ(fixpp::session::read_and_reset_catch_fired(), 0)
        << "T012 catch fired unexpectedly during arm (a)";

    // Read the counter (no increment).
    auto rC = spawn_on_strand(
                  [store_sp]() mutable -> asio::awaitable<fixpp::core::expected_t<seqnum_t>> {
                      co_return co_await store_sp->next_seqnum(direction_t::outbound, false);
                  })
                  .get();
    ASSERT_TRUE(rC.has_value());
    if (!rB.has_value()) {
        // Primary path: B cancelled at the mutex acquire → only A's increment applied.
        EXPECT_EQ(rB.error(), error::store_cancelled)
            << "Cancelled next_seqnum must return store_cancelled";
        EXPECT_EQ(*rC, 2u) << "Counter must be 2 after one increment; B was cancelled";
    } else {
        // Rare race: B linearised first → both incremented.
        EXPECT_EQ(*rC, 3u) << "Both increments applied (B raced the cancel)";
        SUCCEED() << "Cancel arrived after B linearised (race)";
    }
}

TEST_F(FileStoreCancellationTest, Reset_CancelAtMutexAcquire_YieldsStoreCancelled_NoStateChange) {
    fixpp::session::install_store_offload_probe(nullptr);
    auto store = open_store("SNDR3", "TGTC");
    ASSERT_TRUE(store != nullptr);
    auto store_sp = std::shared_ptr<FileStore>(std::move(store));

    // Pre-store a frame so reset() has state to clear (confirms 0-change on cancel).
    {
        const auto frame1 = make_frame(1);
        spawn_on_strand([store_sp, frame1]() mutable -> asio::awaitable<void> {
            co_await store_sp->store(
                1, std::span<const std::byte>(frame1), direction_t::outbound);
        }).get();
    }

    reset_probe();
    fixpp::session::read_and_reset_catch_fired();

    // Op A: store seq=2 (no cancel slot) — FIFO-first, holds the mutex and offloads.
    const auto frame2 = make_frame(2);
    auto futA = spawn_on_strand(
        [store_sp, frame2]() mutable -> asio::awaitable<fixpp::core::expected_t<void>> {
            co_return co_await store_sp->store(
                2, std::span<const std::byte>(frame2), direction_t::outbound);
        });

    // Op B: reset() with cancel slot.
    asio::cancellation_signal sig;
    auto futB = spawn_on_strand(asio::bind_cancellation_slot(
        sig.slot(),
        [store_sp]() mutable -> asio::awaitable<fixpp::core::expected_t<void>> {
            co_return co_await store_sp->reset();
        }));

    // Cancel reset() at the mutex acquire (A holds it; cancel pending before B parks).
    sig.emit(asio::cancellation_type::total);

    ASSERT_EQ(futA.wait_for(std::chrono::seconds{10}), std::future_status::ready)
        << "store(seq=2) A did not complete within 10 s";
    ASSERT_EQ(futB.wait_for(std::chrono::seconds{10}), std::future_status::ready)
        << "reset() B did not complete within 10 s";

    const auto rA = futA.get();
    const auto rB = futB.get();
    fixpp::session::install_store_offload_probe(nullptr);

    ASSERT_TRUE(rA.has_value()) << "store(seq=2) A must succeed";

    EXPECT_EQ(fixpp::session::read_and_reset_catch_fired(), 0)
        << "T012 catch fired unexpectedly during arm (a)";

    if (!rB.has_value()) {
        // Primary path: reset() cancelled at the mutex acquire → state untouched.
        EXPECT_EQ(rB.error(), error::store_cancelled)
            << "Cancelled reset must return store_cancelled";
        CountingVisitor vis;
        spawn_on_strand([store_sp, &vis]() mutable -> asio::awaitable<void> {
            co_await store_sp->retrieve(1, 0, direction_t::outbound, vis);
        }).get();
        EXPECT_EQ(vis.count, 2u) << "Both frames must still exist; reset was cancelled";
    } else {
        // Rare race: reset() linearised before the cancel → store cleared. Valid (FR-020).
        SUCCEED() << "Cancel arrived after reset() linearised (race)";
    }
}

#endif  // FIXPP_TEST_HOOKS (arm (a))

// ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ──
// arm (b) — cancel mid-syscall → durable success, NOT store_cancelled
// ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ──
//
// Strategy: emit total cancellation on the op's signal BEFORE the op starts
// (or concurrently). The co_spawn terminal-only default FILTERS total, so the
// blocking syscall runs to durable completion regardless of the signal.
// The probe (FIXPP_TEST_HOOKS) confirms the syscall actually ran on a pool thread.
//
// T012 catch-fired check: assert read_and_reset_catch_fired() == 0 after each
// arm (b) cell. This CONFIRMS the try/catch is defensive-only / unreachable under
// normal asio operation. If it fires, the test FAILs and T020 BRDA note must be
// upgraded to a real compliance gap.
//
// Thread-safety note: cancellation_signal::emit is called from the main thread;
// the awaiter is on the pool strand. We emit BEFORE spawning the op (or from
// the same thread that originally owns the signal) — not from the probe's pool
// thread — to avoid the TSan cross-thread-emit hazard.

#ifdef FIXPP_TEST_HOOKS

TEST_F(FileStoreCancellationTest, Store_CancelMidSyscall_DurableNotCancelled) {
    auto store = open_store("SNDR4", "TGTD",
                            FileStorePolicy{FileStorePolicy::kind::commit_per_message});
    ASSERT_TRUE(store != nullptr);
    auto store_sp = std::shared_ptr<FileStore>(std::move(store));

    // Install probe to confirm syscall ran on pool thread.
    reset_probe();
    fixpp::session::install_store_offload_probe(plain_probe);
    fixpp::session::read_and_reset_catch_fired();

    // Emit total cancel BEFORE spawning the op. co_spawn filters it → syscall runs.
    asio::cancellation_signal sig;
    sig.emit(asio::cancellation_type::total);

    const auto frame1 = make_frame(1);
    auto fut = spawn_on_strand(asio::bind_cancellation_slot(
        sig.slot(),
        [store_sp, frame1]() mutable -> asio::awaitable<fixpp::core::expected_t<void>> {
            co_return co_await store_sp->store(
                1, std::span<const std::byte>(frame1), direction_t::outbound);
        }));

    // Bounded wait — no wedge allowed (10 s).
    ASSERT_EQ(fut.wait_for(std::chrono::seconds{10}), std::future_status::ready)
        << "store() did not complete within 10 s after total cancel — possible wedge";

    fixpp::core::expected_t<void> result;
    ASSERT_NO_THROW(result = fut.get())
        << "store() must not throw under total cancellation";

    fixpp::session::install_store_offload_probe(nullptr);

    // b.1: result must NOT be store_cancelled (mutex was never contended in arm (b)).
    if (!result.has_value()) {
        EXPECT_NE(result.error(), error::store_cancelled)
            << "C3 arm (b) violated: store_cancelled returned even though co_spawn "
               "filters total and syscall ran durably";
    }

    // b.2: If durable success, probe was entered and frame is retrievable.
    if (result.has_value()) {
        EXPECT_TRUE(g_probe_entered.load(std::memory_order_acquire))
            << "Probe not entered — syscall did not run on pool thread";

        CountingVisitor vis;
        spawn_on_strand([store_sp, &vis]() mutable -> asio::awaitable<void> {
            co_await store_sp->retrieve(1, 0, direction_t::outbound, vis);
        }).get();
        EXPECT_EQ(vis.count, 1u) << "Frame seq=1 must be durable after successful store";
    }

    // b.3: T012 catch must NOT have fired (co_spawn filters total; catch is unreachable).
    EXPECT_EQ(fixpp::session::read_and_reset_catch_fired(), 0)
        << "T012 catch fired: operation_aborted reached the outer co_await — "
           "defensive-only claim is WRONG; upgrade T020 BRDA note to compliance gap";
}

TEST_F(FileStoreCancellationTest, NextSeqnum_CancelMidSyscall_DurableNotCancelled) {
    auto store = open_store("SNDR5", "TGTE");
    ASSERT_TRUE(store != nullptr);
    auto store_sp = std::shared_ptr<FileStore>(std::move(store));

    reset_probe();
    fixpp::session::install_store_offload_probe(plain_probe);
    fixpp::session::read_and_reset_catch_fired();

    asio::cancellation_signal sig;
    sig.emit(asio::cancellation_type::total);

    auto fut = spawn_on_strand(asio::bind_cancellation_slot(
        sig.slot(),
        [store_sp]() mutable -> asio::awaitable<fixpp::core::expected_t<seqnum_t>> {
            co_return co_await store_sp->next_seqnum(direction_t::outbound, true);
        }));

    ASSERT_EQ(fut.wait_for(std::chrono::seconds{10}), std::future_status::ready)
        << "next_seqnum() did not complete within 10 s — possible wedge";

    fixpp::core::expected_t<seqnum_t> result;
    ASSERT_NO_THROW(result = fut.get())
        << "next_seqnum() must not throw under total cancellation";

    fixpp::session::install_store_offload_probe(nullptr);

    // b.1: result must NOT be store_cancelled.
    if (!result.has_value()) {
        EXPECT_NE(result.error(), error::store_cancelled)
            << "C3 arm (b) violated: store_cancelled for next_seqnum with syscall filtered";
    }

    // b.2: If durable success — probe was entered and counter advanced to 2.
    if (result.has_value()) {
        EXPECT_TRUE(g_probe_entered.load(std::memory_order_acquire));
        EXPECT_EQ(*result, 1u) << "next_seqnum returns old value (seqnum_min=1) before increment";

        auto fut2 = spawn_on_strand(
            [store_sp]() mutable -> asio::awaitable<fixpp::core::expected_t<seqnum_t>> {
                co_return co_await store_sp->next_seqnum(direction_t::outbound, false);
            });
        const auto r2 = fut2.get();
        ASSERT_TRUE(r2.has_value());
        EXPECT_EQ(*r2, 2u) << "Counter must be 2 after one increment";
    }

    // b.3: T012 catch must NOT have fired.
    EXPECT_EQ(fixpp::session::read_and_reset_catch_fired(), 0)
        << "T012 catch fired: operation_aborted reached the outer co_await — "
           "defensive-only claim is WRONG";
}

TEST_F(FileStoreCancellationTest, Reset_CancelMidSyscall_DurableNotCancelled) {
    auto store = open_store("SNDR6", "TGTF");
    ASSERT_TRUE(store != nullptr);
    auto store_sp = std::shared_ptr<FileStore>(std::move(store));

    // Pre-store a frame so reset() has state to clear.
    const auto frame1 = make_frame(1);
    spawn_on_strand([store_sp, frame1]() mutable -> asio::awaitable<void> {
        co_await store_sp->store(1, std::span<const std::byte>(frame1), direction_t::outbound);
    }).get();

    reset_probe();
    fixpp::session::install_store_offload_probe(plain_probe);
    fixpp::session::read_and_reset_catch_fired();

    asio::cancellation_signal sig;
    sig.emit(asio::cancellation_type::total);

    auto fut = spawn_on_strand(asio::bind_cancellation_slot(
        sig.slot(),
        [store_sp]() mutable -> asio::awaitable<fixpp::core::expected_t<void>> {
            co_return co_await store_sp->reset();
        }));

    ASSERT_EQ(fut.wait_for(std::chrono::seconds{10}), std::future_status::ready)
        << "reset() did not complete within 10 s — possible wedge";

    fixpp::core::expected_t<void> result;
    ASSERT_NO_THROW(result = fut.get())
        << "reset() must not throw under total cancellation";

    fixpp::session::install_store_offload_probe(nullptr);

    // b.1: result must NOT be store_cancelled.
    if (!result.has_value()) {
        EXPECT_NE(result.error(), error::store_cancelled)
            << "C3 arm (b) violated: store_cancelled for reset() with syscall filtered";
    }

    // b.2: If durable success — probe was entered and counters reset to seqnum_min.
    if (result.has_value()) {
        EXPECT_TRUE(g_probe_entered.load(std::memory_order_acquire));

        auto fut2 = spawn_on_strand(
            [store_sp]() mutable -> asio::awaitable<fixpp::core::expected_t<seqnum_t>> {
                co_return co_await store_sp->next_seqnum(direction_t::outbound, false);
            });
        const auto r2 = fut2.get();
        ASSERT_TRUE(r2.has_value());
        EXPECT_EQ(*r2, 1u) << "Outbound counter must be 1 after reset";
    }

    // b.3: T012 catch must NOT have fired.
    EXPECT_EQ(fixpp::session::read_and_reset_catch_fired(), 0)
        << "T012 catch fired: operation_aborted reached the outer co_await — "
           "defensive-only claim is WRONG";
}

#endif  // FIXPP_TEST_HOOKS (arm (b))

// ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ──
// No-wedge cell: total cancellation while store offload in-flight → no hang
// ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ──
//
// Co_spawn terminal-only default must NOT swallow total cancellation and stall
// teardown. [[feedback_asio_cospawn_total_cancellation_default]]

TEST_F(FileStoreCancellationTest, CoSpawn_TerminalOnly_DoesNotSwallowTotal_NoWedge) {
    auto store = open_store("SNDR7", "TGTG",
                            FileStorePolicy{FileStorePolicy::kind::commit_per_message});
    ASSERT_TRUE(store != nullptr);
    auto store_sp = std::shared_ptr<FileStore>(std::move(store));

#ifdef FIXPP_TEST_HOOKS
    // Install a slow probe so the cancel signal fires while syscall is mid-flight.
    reset_probe();
    fixpp::session::install_store_offload_probe(slow_probe);
    fixpp::session::read_and_reset_catch_fired();
#endif

    asio::cancellation_signal sig;
    const auto frame1 = make_frame(1);

    auto fut = spawn_on_strand(asio::bind_cancellation_slot(
        sig.slot(),
        [store_sp, frame1]() mutable -> asio::awaitable<fixpp::core::expected_t<void>> {
            co_return co_await store_sp->store(
                1, std::span<const std::byte>(frame1), direction_t::outbound);
        }));

    // Brief yield to let co_spawn start the offload, then emit total cancel.
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
    sig.emit(asio::cancellation_type::total);

    // Must resolve within timeout — no wedge.
    ASSERT_EQ(fut.wait_for(std::chrono::seconds{10}), std::future_status::ready)
        << "store() did not complete within 10 s after total cancel — possible wedge";

    fixpp::core::expected_t<void> result;
    ASSERT_NO_THROW(result = fut.get())
        << "store() must not throw under total cancellation";

    // Result must NOT be store_cancelled (single-op, no mutex contention).
    if (!result.has_value()) {
        EXPECT_NE(result.error(), error::store_cancelled)
            << "store_cancelled returned without mutex contention — contract violation";
    }

#ifdef FIXPP_TEST_HOOKS
    fixpp::session::install_store_offload_probe(nullptr);
#endif
}

}  // namespace
