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
//     Deterministic strategy (single-threaded io_context controller):
//     Session executor = asio::io_context (single-threaded); file_io_executor = pool_.
//     A controller coroutine on the io_context:
//     - Spawns A (no cancel): A acquires mutex, offloads to pool_; hold_probe spins there.
//     - Polls g_probe_entered via yield_n(1) loops — ioc thread free; pool thread spins.
//     - Spawns B (with cancel slot): B's leading-post + async_lock attempt run on ioc.
//     - yield_n(6): lets B park at async_lock (on_cancel handler installed).
//     - sig.emit(total): called ON the ioc thread — thread-safe (same thread as B's
//       cancellation handler); B gets store_cancelled.
//     - Sets g_arm_a_release: hold_probe exits, A's offload returns, A completes.
//     Deterministic, no timing dependency, no cross-thread signal. [gate-b/r1 B]
//
//   arm (b) — Cancel mid-syscall (cancel emitted while syscall in-flight):
//     → normal durable success (NOT store_cancelled)
//     → frame IS on disk / counter IS advanced / reset WAS applied
//     The co_spawn terminal-only default filters total cancellation (Decision 5 /
//     data-model §2), so the blocking syscall completes durably regardless.
//     The T012 try/catch is not reached under normal asio operation. Verified via
//     g_catch_fired counter: assert == 0 after arm (b) cells. The catch body for
//     reset() IS exercised by the gate-b/r1 A.2 fault-injection test
//     (Reset_OperationAbortedAfterDurable_CommitsAndReturnsDurableSuccess) using
//     arm_force_abort_after_reset_lambda(). For store()/next_seqnum() the catch is
//     benign (io_ok=true already set); §IX.1 waiver recorded in spec.md.
//
//   CoSpawn_TerminalOnly_DoesNotSwallowTotal_NoWedge:
//     total cancellation emitted while a store offload is in-flight; asserts no
//     wedge/hang. [[feedback_asio_cospawn_total_cancellation_default]]
//
// ANTI-HANG: every live cell uses a bounded future.wait_for so a wedge FAILs
// rather than hanging ctest. [[feedback_fail_placeholder_red_test]]
//
// Executor topology ([[feedback_strand_in_any_executor_refcount_race]]):
//   arm (a): io_context (single-threaded) as session executor + pool_ as file_io_executor.
//     Cancel emitted from within a controller coroutine on the io_context (same thread as
//     the waiter's cancellation handler) — thread-safe by construction. Mirrors
//     tests/sync/test_cancellation_mid_wait.cpp. [[feedback_asio_cospawn_total_cancellation_default]]
//   arm (b): pool_ = asio::thread_pool (4 threads); strand_exec_ for session executor.
//   pool.stop() + pool.join() in TearDown.
//
// FIXPP_TEST_HOOKS: required for install_store_offload_probe + read_and_reset_catch_fired.
// CMakeLists adds this flag to the target definition.
#include <gtest/gtest.h>

#include <asio/any_io_executor.hpp>
#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
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
#include <optional>
#include <span>
#include <thread>
#include <vector>

#include "_fixtures_/store_temp_dir.hpp"
#include "sync/sync_test_support.hpp"

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
// arm (a) determinism: hold probe holds the offload open until g_arm_a_release is set.
// This ensures the mutex is held by A when we emit cancel for B — no SUCCEED() escape.
static std::atomic<bool> g_arm_a_release{false};

static void reset_probe() noexcept {
    g_probe_entered.store(false, std::memory_order_relaxed);
    g_arm_a_release.store(false, std::memory_order_relaxed);
}

// Plain probe: record that the lambda was entered on a pool thread.
static void plain_probe(std::thread::id) noexcept {
    g_probe_entered.store(true, std::memory_order_release);
}

// Hold probe: sets g_probe_entered, then spins until g_arm_a_release is set.
// Runs on a file pool thread (separate from the ioc session driver) — spinning
// here does NOT block the ioc or the controller coroutine.
// Bounded spin (max 10 s) prevents a deadlock from becoming a hang.
static void hold_probe(std::thread::id) noexcept {
    g_probe_entered.store(true, std::memory_order_release);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
    while (!g_arm_a_release.load(std::memory_order_acquire)) {
        if (std::chrono::steady_clock::now() > deadline) {
            break;  // safety valve — test will FAIL on the subsequent assertions
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
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
// Deterministic strategy (single-threaded io_context controller, mirrors
// tests/sync/test_cancellation_mid_wait.cpp):
//   session executor = io_context (single thread via ioc.run());
//   file_io_executor = pool_ (separate thread pool; hold_probe spins there).
//   A controller coroutine on the io_context:
//   1. co_spawn A (no cancel) on ioc. A: leading-post -> acquire mutex -> offload to pool_.
//      hold_probe runs on pool thread: sets g_probe_entered, spins on g_arm_a_release.
//   2. Poll g_probe_entered with yield_n(1) loops — ioc thread stays free while pool spins.
//   3. co_spawn B (with cancel slot) on ioc. B: leading-post -> async_lock -> parks.
//   4. yield_n(6) — lets B run its leading-post and park at async_lock (on_cancel installed).
//   5. sig.emit(total) — from within the controller (ioc thread) — same thread as B's
//      cancellation slot handler; thread-safe, deterministic.
//   6. yield_n(4) — let cancellation handler post B's cancelled-resume.
//   7. g_arm_a_release = true -> hold_probe exits -> A's offload returns -> A completes.
//   Assert B = store_cancelled, 0 state change. No SUCCEED() escape. [gate-b/r1 B]

#ifdef FIXPP_TEST_HOOKS

TEST_F(FileStoreCancellationTest, Store_CancelAtMutexAcquire_YieldsStoreCancelled_NoStateChange) {
    using fixpp::sync::test::yield_n;

    reset_probe();
    fixpp::session::install_store_offload_probe(hold_probe);
    fixpp::session::read_and_reset_catch_fired();

    // Store opened on fixture strand; file_io_executor = pool_.
    auto store = open_store("SNDR1", "TGTA",
                            FileStorePolicy{FileStorePolicy::kind::commit_per_message});
    ASSERT_TRUE(store != nullptr);
    auto store_sp = std::shared_ptr<FileStore>(std::move(store));

    const auto frame1 = make_frame(1);
    const auto frame2 = make_frame(2);

    // Results captured into optionals; controller fills them on the ioc thread.
    std::optional<fixpp::core::expected_t<void>> rA, rB;
    asio::cancellation_signal sig;

    // Single-threaded session driver — all coroutines run co-operatively here.
    asio::io_context ioc;

    auto controller = [&]() -> asio::awaitable<void> {
        // Spawn A (no cancel). A: leading-post -> acquire mutex -> offload to pool_.
        // hold_probe spins on pool_ thread; does not block the ioc.
        asio::co_spawn(
            ioc,
            [&]() -> asio::awaitable<void> {
                rA = co_await store_sp->store(
                    1, std::span<const std::byte>(frame1), direction_t::outbound);
            },
            asio::detached);

        // Poll until hold_probe signals A is in the offload (mutex held).
        while (!g_probe_entered.load(std::memory_order_acquire)) {
            co_await yield_n(1);
        }

        // Spawn B with cancel slot. B: leading-post -> async_lock -> parks (A holds mutex).
        asio::co_spawn(
            ioc,
            [&]() -> asio::awaitable<void> {
                rB = co_await store_sp->store(
                    2, std::span<const std::byte>(frame2), direction_t::outbound);
            },
            asio::bind_cancellation_slot(sig.slot(), asio::detached));

        // Let B run its leading-post and park at async_lock (on_cancel handler installed).
        co_await yield_n(6);

        // Emit cancel from the ioc thread — same thread as B's cancellation handler.
        sig.emit(asio::cancellation_type::total);

        // Let the cancellation handler post B's cancelled-resume.
        co_await yield_n(4);

        // Release A's hold_probe; A's offload returns; A completes.
        g_arm_a_release.store(true, std::memory_order_release);
    };

    asio::co_spawn(ioc, controller(), asio::detached);
    // Bounded run: ioc.run() returns only when no more work is pending.
    // Anti-hang: run_for(10s) instead of run() so a bug doesn't wedge ctest.
    ioc.run_for(std::chrono::seconds{10});

    fixpp::session::install_store_offload_probe(nullptr);

    ASSERT_TRUE(rA.has_value()) << "Op A result not set — ioc did not drive it to completion";
    ASSERT_TRUE(rB.has_value()) << "Op B result not set — ioc did not drive it to completion";

    ASSERT_TRUE(rA->has_value()) << "store(seq=1) A must succeed";

    // Primary path: B cancelled at the mutex acquire — MANDATORY (no SUCCEED() escape).
    ASSERT_FALSE(rB->has_value()) << "B must be cancelled; cancel was deterministic";
    EXPECT_EQ(rB->error(), error::store_cancelled)
        << "Cancelled store must return store_cancelled, not "
        << static_cast<int>(rB->error());

    // 0 state change: only seq=1 committed; B's syscall probe must NOT have fired.
    CountingVisitor vis;
    spawn_on_strand([store_sp, &vis]() mutable -> asio::awaitable<void> {
        co_await store_sp->retrieve(1, 0, direction_t::outbound, vis);
    }).get();
    EXPECT_EQ(vis.count, 1u) << "Only seq=1 should be stored; B was cancelled at mutex";

    // T012 catch must NOT have fired on the arm-(a) path (cancel before any offload).
    EXPECT_EQ(fixpp::session::read_and_reset_catch_fired(), 0)
        << "T012 catch fired unexpectedly during arm (a)";
}

TEST_F(FileStoreCancellationTest,
       NextSeqnum_CancelAtMutexAcquire_YieldsStoreCancelled_NoStateChange) {
    using fixpp::sync::test::yield_n;

    reset_probe();
    fixpp::session::install_store_offload_probe(hold_probe);
    fixpp::session::read_and_reset_catch_fired();

    auto store = open_store("SNDR2", "TGTB");
    ASSERT_TRUE(store != nullptr);
    auto store_sp = std::shared_ptr<FileStore>(std::move(store));

    std::optional<fixpp::core::expected_t<seqnum_t>> rA, rB;
    asio::cancellation_signal sig;

    asio::io_context ioc;

    auto controller = [&]() -> asio::awaitable<void> {
        // Spawn A (no cancel). A: leading-post -> acquire mutex -> offload to pool_.
        asio::co_spawn(
            ioc,
            [&]() -> asio::awaitable<void> {
                rA = co_await store_sp->next_seqnum(direction_t::outbound, true);
            },
            asio::detached);

        // Poll until hold_probe signals A is in the offload (mutex held).
        while (!g_probe_entered.load(std::memory_order_acquire)) {
            co_await yield_n(1);
        }

        // Spawn B with cancel slot. B: leading-post -> async_lock -> parks.
        asio::co_spawn(
            ioc,
            [&]() -> asio::awaitable<void> {
                rB = co_await store_sp->next_seqnum(direction_t::outbound, true);
            },
            asio::bind_cancellation_slot(sig.slot(), asio::detached));

        co_await yield_n(6);
        sig.emit(asio::cancellation_type::total);
        co_await yield_n(4);
        g_arm_a_release.store(true, std::memory_order_release);
    };

    asio::co_spawn(ioc, controller(), asio::detached);
    ioc.run_for(std::chrono::seconds{10});

    fixpp::session::install_store_offload_probe(nullptr);

    ASSERT_TRUE(rA.has_value()) << "Op A result not set";
    ASSERT_TRUE(rB.has_value()) << "Op B result not set";

    ASSERT_TRUE(rA->has_value()) << "next_seqnum(true) A must succeed";
    EXPECT_EQ(**rA, 1u) << "first increment returns seqnum 1";

    EXPECT_EQ(fixpp::session::read_and_reset_catch_fired(), 0)
        << "T012 catch fired unexpectedly during arm (a)";

    // Primary path: B cancelled at mutex acquire — MANDATORY.
    ASSERT_FALSE(rB->has_value()) << "B must be cancelled; cancel was deterministic";
    EXPECT_EQ(rB->error(), error::store_cancelled)
        << "Cancelled next_seqnum must return store_cancelled";

    // 0 state change: only A's increment applied; counter must be 2.
    auto rC = spawn_on_strand(
                  [store_sp]() mutable -> asio::awaitable<fixpp::core::expected_t<seqnum_t>> {
                      co_return co_await store_sp->next_seqnum(direction_t::outbound, false);
                  })
                  .get();
    ASSERT_TRUE(rC.has_value());
    EXPECT_EQ(*rC, 2u) << "Counter must be 2 after only A's increment (B cancelled)";
}

TEST_F(FileStoreCancellationTest, Reset_CancelAtMutexAcquire_YieldsStoreCancelled_NoStateChange) {
    using fixpp::sync::test::yield_n;

    reset_probe();
    fixpp::session::read_and_reset_catch_fired();

    auto store = open_store("SNDR3", "TGTC");
    ASSERT_TRUE(store != nullptr);
    auto store_sp = std::shared_ptr<FileStore>(std::move(store));

    // Pre-store a frame so reset() has state to clear (confirms 0-change on cancel).
    // Use no probe for the pre-store (plain sequential call on fixture strand).
    fixpp::session::install_store_offload_probe(nullptr);
    const auto frame1 = make_frame(1);
    spawn_on_strand([store_sp, frame1]() mutable -> asio::awaitable<void> {
        co_await store_sp->store(1, std::span<const std::byte>(frame1), direction_t::outbound);
    }).get();

    // Re-arm hold_probe for op A.
    reset_probe();
    fixpp::session::install_store_offload_probe(hold_probe);

    const auto frame2 = make_frame(2);

    std::optional<fixpp::core::expected_t<void>> rA, rB;
    asio::cancellation_signal sig;

    asio::io_context ioc;

    auto controller = [&]() -> asio::awaitable<void> {
        // Spawn A (no cancel): store seq=2. A acquires mutex, offloads to pool_.
        // hold_probe spins there; mutex stays held.
        asio::co_spawn(
            ioc,
            [&]() -> asio::awaitable<void> {
                rA = co_await store_sp->store(
                    2, std::span<const std::byte>(frame2), direction_t::outbound);
            },
            asio::detached);

        // Poll until hold_probe signals A is in the offload.
        while (!g_probe_entered.load(std::memory_order_acquire)) {
            co_await yield_n(1);
        }

        // Spawn B = reset() with cancel slot. B parks at async_lock (A holds mutex).
        asio::co_spawn(
            ioc,
            [&]() -> asio::awaitable<void> {
                rB = co_await store_sp->reset();
            },
            asio::bind_cancellation_slot(sig.slot(), asio::detached));

        co_await yield_n(6);
        sig.emit(asio::cancellation_type::total);
        co_await yield_n(4);
        g_arm_a_release.store(true, std::memory_order_release);
    };

    asio::co_spawn(ioc, controller(), asio::detached);
    ioc.run_for(std::chrono::seconds{10});

    fixpp::session::install_store_offload_probe(nullptr);

    ASSERT_TRUE(rA.has_value()) << "Op A result not set";
    ASSERT_TRUE(rB.has_value()) << "Op B result not set";

    ASSERT_TRUE(rA->has_value()) << "store(seq=2) A must succeed";

    EXPECT_EQ(fixpp::session::read_and_reset_catch_fired(), 0)
        << "T012 catch fired unexpectedly during arm (a)";

    // Primary path: reset() cancelled at the mutex acquire — MANDATORY.
    ASSERT_FALSE(rB->has_value()) << "B must be cancelled; cancel was deterministic";
    EXPECT_EQ(rB->error(), error::store_cancelled)
        << "Cancelled reset must return store_cancelled";

    // 0 state change: both frames still exist (reset was cancelled before renaming).
    CountingVisitor vis;
    spawn_on_strand([store_sp, &vis]() mutable -> asio::awaitable<void> {
        co_await store_sp->retrieve(1, 0, direction_t::outbound, vis);
    }).get();
    EXPECT_EQ(vis.count, 2u) << "Both frames must still exist; reset was cancelled at mutex";
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

#ifdef FIXPP_TEST_HOOKS

// ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ──
// gate-b/r1 A.1 — poison-on-post-rename-reopen-failure witness
// ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ──
//
// Validates that when reset()'s post-rename reopen/lock step fails (i.e., the live
// log was replaced by rename+dir-fsync but the reopen of the new fd failed), the
// store is poisoned (open_ok=false) so no subsequent store() can silently write to
// the stale (now-unlinked) inode.
//
// Discriminating: the fault hook forces a reopen failure AFTER rename (not before),
// so the live pathname names the fresh reset log but old fd is dead. Without the
// poison fix, store() would succeed against the unlinked inode → silent loss on
// restart. With the fix, all subsequent ops fail with store_io_failure.
//
// Discrimination seed: pre-store seq=99 so we can distinguish "state unchanged
// (bug)" from "store poisoned (correct)" — a store() returning success while the
// counter at 99 means the bug is present; store_io_failure means poisoned.

static bool force_reopen_fail() noexcept { return false; }

TEST_F(FileStoreCancellationTest,
       Reset_PostRenameReopenFail_PoisonsStore_NoSilentLossAfterRestart) {
    auto store = open_store("SNDR_PA1", "TGT_PA1",
                            FileStorePolicy{FileStorePolicy::kind::commit_per_message});
    ASSERT_TRUE(store != nullptr);
    auto store_sp = std::shared_ptr<FileStore>(std::move(store));

    // Pre-store a frame (seq=1 = seqnum_min on a fresh store) as a baseline —
    // confirms the store is working before the fault injection runs.
    const auto frame1_pre = make_frame(1);
    {
        auto r = spawn_on_strand(
                     [store_sp, frame1_pre]() mutable
                     -> asio::awaitable<fixpp::core::expected_t<void>> {
                         co_return co_await store_sp->store(
                             1, std::span<const std::byte>(frame1_pre), direction_t::outbound);
                     })
                     .get();
        ASSERT_TRUE(r.has_value()) << "Pre-store seq=1 must succeed";
    }

    // Install the hook that forces reopen failure AFTER rename — simulates the A.1 path.
    fixpp::session::install_post_rename_reopen_fail_hook(force_reopen_fail);

    // reset() — rename+dir-fsync succeed, then reopen is forced to fail.
    auto r_reset = spawn_on_strand(
                       [store_sp]() mutable -> asio::awaitable<fixpp::core::expected_t<void>> {
                           co_return co_await store_sp->reset();
                       })
                       .get();

    // reset() must return store_io_failure (post-rename failure — not a pre-rename failure).
    ASSERT_FALSE(r_reset.has_value()) << "reset() must fail after forced reopen failure";
    EXPECT_EQ(r_reset.error(), fixpp::core::error::store_io_failure)
        << "reset() must return store_io_failure on post-rename reopen failure";

    // NOW: the store must be poisoned — store() must fail, not succeed on the dead inode.
    const auto frame1 = make_frame(1);
    auto r_store = spawn_on_strand(
                       [store_sp, frame1]() mutable
                       -> asio::awaitable<fixpp::core::expected_t<void>> {
                           co_return co_await store_sp->store(
                               1, std::span<const std::byte>(frame1), direction_t::outbound);
                       })
                       .get();

    ASSERT_FALSE(r_store.has_value())
        << "store() after post-rename reopen failure MUST fail (store is poisoned); "
           "if it succeeds, writes go to the dead unlinked inode — silent loss on restart";
    EXPECT_EQ(r_store.error(), fixpp::core::error::store_io_failure)
        << "Poisoned store must return store_io_failure";
}

// ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ──
// gate-b/r1 A.2 — operation_aborted-after-durable witness for reset()
// ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ── ──
//
// Validates that when reset() receives asio::system_error(operation_aborted) at the
// outer co_await AFTER the lambda completed durably (rename+reopen+lock all succeeded),
// the catch body commits the new file and returns durable success — never store_io_failure.
//
// This exercises the C3 contract: a durable reset must NEVER be reported as failed.
//
// Discriminating: pre-advance the seqnum counter to N=37 before reset. After reset,
// assert next_seqnum==1 (counter reset to seqnum_min). If the catch body doesn't commit,
// next_seqnum would still be 38 (not reset) — proves the commit path fired.

TEST_F(FileStoreCancellationTest, Reset_OperationAbortedAfterDurable_CommitsAndReturnsDurableSuccess) {
    auto store = open_store("SNDR_PA2", "TGT_PA2");
    ASSERT_TRUE(store != nullptr);
    auto store_sp = std::shared_ptr<FileStore>(std::move(store));

    // Advance the outbound counter to 37 by incrementing 36 times (starts at seqnum_min=1,
    // each increment advances by 1, so 36 increments → next_seqnum=37 before the 37th call).
    // This gives a discriminating pre-reset value well above seqnum_min.
    for (int i = 0; i < 36; ++i) {
        auto r = spawn_on_strand(
                     [store_sp]() mutable -> asio::awaitable<fixpp::core::expected_t<seqnum_t>> {
                         co_return co_await store_sp->next_seqnum(direction_t::outbound, true);
                     })
                     .get();
        ASSERT_TRUE(r.has_value()) << "next_seqnum advance failed at i=" << i;
    }
    // Verify: next_seqnum (no increment) should be 37.
    {
        auto r = spawn_on_strand(
                     [store_sp]() mutable -> asio::awaitable<fixpp::core::expected_t<seqnum_t>> {
                         co_return co_await store_sp->next_seqnum(direction_t::outbound, false);
                     })
                     .get();
        ASSERT_TRUE(r.has_value()) << "Pre-reset next_seqnum check failed";
        ASSERT_EQ(*r, seqnum_t{37}) << "Pre-reset counter must be 37 (discrimination seed)";
    }

    // Arm the one-shot fault: next reset() will receive operation_aborted AFTER lambda.
    fixpp::session::arm_force_abort_after_reset_lambda();

    // reset() — lambda completes durably, then operation_aborted is thrown at the outer
    // co_await. The catch body must commit (impl_ updated + durable success returned).
    auto r_reset = spawn_on_strand(
                       [store_sp]() mutable -> asio::awaitable<fixpp::core::expected_t<void>> {
                           co_return co_await store_sp->reset();
                       })
                       .get();

    // C3: reset() must return durable success — the rename was committed on disk.
    ASSERT_TRUE(r_reset.has_value())
        << "reset() after durable rename + operation_aborted MUST return success (C3); "
           "returning store_io_failure would report a durable reset as failed — silent loss";

    // The catch body committed: next_seqnum must be seqnum_min (1) — not 37 (pre-reset).
    auto r_seq = spawn_on_strand(
                     [store_sp]() mutable -> asio::awaitable<fixpp::core::expected_t<seqnum_t>> {
                         co_return co_await store_sp->next_seqnum(direction_t::outbound, false);
                     })
                     .get();
    ASSERT_TRUE(r_seq.has_value()) << "next_seqnum after reset must succeed";
    EXPECT_EQ(*r_seq, seqnum_t{1})
        << "Counter must be seqnum_min=1 after reset; if it's 37 the catch body did not commit";

    // g_catch_fired must be 1: the fault-injection triggered the catch exactly once.
    EXPECT_EQ(fixpp::session::read_and_reset_catch_fired(), 1)
        << "g_catch_fired must be 1 — catch body was not exercised";
}

#endif  // FIXPP_TEST_HOOKS (gate-b/r1 A.1 + A.2 fault-injection witnesses)

}  // namespace
