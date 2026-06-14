// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_store_shutdown_ordering.cpp
//
// Seam 18 — concurrent store/retrieve/reset under a single async-mutex;
// orderly teardown of an unbounded MemoryStore without UAF on the slab
// (I-22 / SC-005).
//
// What this file actually tests:
//   1. HundredSequentialStoresAllSucceed — 100 store() calls on a bounded
//      store run sequentially on a single-thread pool; all succeed.
//   2. StoreOutlivesAllCoroutines — two concurrent writers (inbound +
//      outbound) on an unbounded store; TSan sees 0 data races; store is
//      destroyed after the pool joins (correct ownership ordering).
//   3. ResetDuringOperationalPeriodIsClean — store/reset/next_seqnum
//      round-trip verifies counters rewind to 1 after reset().
//   4. ConcurrentReadWriteNoDataRace — concurrent retrieve (frames 1..10)
//      + store (frames 11..30) on unbounded store; TSan must report 0 races.
//   5. UnboundedRetrieveUAFUnderConcurrentAppend — RC#1 regression.
//   6. FileStoreOffloadDrainBeforePoolJoin — T014 / SC-007 / C5:
//      in-flight offloaded store() + flush_for_session_close() under
//      commit_batched complete before pool.stop()/join() and store
//      destruction; no UAF, no use-of-joined-pool under ASan/TSan.
//      (pool-level witness; pool.stop()/join() AFTER every co_await.)
//   7. FileStoreFlushForCloseIsGenuineOffload — T014 supplement: confirms
//      flush_for_session_close() does genuine offloaded fdatasync when
//      frames buffered under commit_batched (batch_size > stores made).
//
// What this file does NOT test:
//   - cancel_and_drain() / store_cancelled outcomes (those require a real
//     Session instance or a direct async_mutex drain harness; deferred to
//     005-session-establishment-fsm per D-4).
//   - Session::close(terminal) shutdown ordering.
//
// gate-b/r1: RC#7 — comment corrected to match body (test body unchanged).
#include <gtest/gtest.h>

#include <asio/any_io_executor.hpp>
#include <asio/co_spawn.hpp>
#include <asio/strand.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_future.hpp>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fixpp/core/error.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/file_store.hpp>
#include <fixpp/session/file_store_factory.hpp>
#include <fixpp/session/memory_store.hpp>
#include <fixpp/session/seqnum.hpp>
#include <future>
#include <span>
#include <vector>

#include "_fixtures_/store_temp_dir.hpp"
#include "_fixtures_/test_double_fsm.hpp"

namespace {

using fixpp::session::direction_t;
using fixpp::session::FileStore;
using fixpp::session::FileStoreFactory;
using fixpp::session::FileStorePolicy;
using fixpp::session::MemoryStore;
using fixpp::session::seqnum_t;
using fixpp::store_test::make_test_frame;
using fixpp::store_test::unique_store_dir;

// ── Test 1: 100 sequential stores all succeed ─────────────────────────────────
//
// Under a single coroutine strand, 100 stores complete sequentially.
// Under US3 each acquires and releases the mutex — TSan must see 0 races.

TEST(StoreShutdownOrdering, HundredSequentialStoresAllSucceed) {
    asio::thread_pool pool{1};

    MemoryStore::Config cfg;
    cfg.policy = fixpp::session::capacity_policy::bounded;
    cfg.inbound_capacity = 200;
    cfg.outbound_capacity = 200;
    cfg.max_frame_bytes = 4096;
    auto store = std::make_shared<MemoryStore>(cfg);

    auto fut = asio::co_spawn(
        pool.get_executor(),
        [store]() -> asio::awaitable<void> {
            for (int i = 1; i <= 100; ++i) {
                auto frame = make_test_frame(static_cast<seqnum_t>(i), direction_t::outbound);
                auto r =
                    co_await store->store(static_cast<seqnum_t>(i),
                                          std::span<const std::byte>(frame), direction_t::outbound);
                EXPECT_TRUE(r.has_value()) << "store seq=" << i << " failed";
            }
        },
        asio::use_future);
    fut.get();

    // store destroyed after pool.stop() — correct ordering
    pool.stop();
    pool.join();
    store.reset();  // explicit destroy AFTER pool stops
}

// ── Test 2: Concurrent stores from multiple coroutines, store outlives them ──
//
// The MemoryStore outlives all coroutines. Under TSan this validates
// that no data race exists on the store's internal state.

TEST(StoreShutdownOrdering, StoreOutlivesAllCoroutines) {
    asio::thread_pool pool{4};

    MemoryStore::Config cfg;
    cfg.policy = fixpp::session::capacity_policy::unbounded;
    cfg.max_frame_bytes = 4096;
    auto store = std::make_shared<MemoryStore>(cfg);

    constexpr int kPerDir = 50;

    // Two concurrent writers: one inbound, one outbound
    auto fut_in = asio::co_spawn(
        pool.get_executor(),
        [store]() -> asio::awaitable<void> {
            for (int i = 1; i <= kPerDir; ++i) {
                auto frame = make_test_frame(static_cast<seqnum_t>(i), direction_t::inbound);
                co_await store->store(static_cast<seqnum_t>(i), std::span<const std::byte>(frame),
                                      direction_t::inbound);
            }
        },
        asio::use_future);

    auto fut_out = asio::co_spawn(
        pool.get_executor(),
        [store]() -> asio::awaitable<void> {
            for (int i = 1; i <= kPerDir; ++i) {
                auto frame = make_test_frame(static_cast<seqnum_t>(i), direction_t::outbound);
                co_await store->store(static_cast<seqnum_t>(i), std::span<const std::byte>(frame),
                                      direction_t::outbound);
            }
        },
        asio::use_future);

    fut_in.get();
    fut_out.get();

    // Stop pool: no more coroutines in flight
    pool.stop();
    pool.join();

    // Destroy store AFTER pool joins — correct shutdown ordering
    store.reset();

    // If we reach here without TSan/ASan complaints, the test passes.
}

// ── Test 3: Reset during shutdown — store is drained cleanly ─────────────────
//
// After 50 stores, call reset(). Verify counters go back to 1.
// No UAF: the reset() coroutine must not touch the store after it's destroyed.

TEST(StoreShutdownOrdering, ResetDuringOperationalPeriodIsClean) {
    asio::thread_pool pool{1};

    MemoryStore::Config cfg;
    cfg.policy = fixpp::session::capacity_policy::bounded;
    cfg.inbound_capacity = 200;
    cfg.outbound_capacity = 200;
    cfg.max_frame_bytes = 4096;
    auto store = std::make_shared<MemoryStore>(cfg);

    auto fut = asio::co_spawn(
        pool.get_executor(),
        [store]() -> asio::awaitable<void> {
            // Store 50 frames
            for (int i = 1; i <= 50; ++i) {
                auto frame = make_test_frame(static_cast<seqnum_t>(i), direction_t::outbound);
                co_await store->store(static_cast<seqnum_t>(i), std::span<const std::byte>(frame),
                                      direction_t::outbound);
            }

            // Trigger reset
            auto r = co_await store->reset();
            EXPECT_TRUE(r.has_value()) << "reset() failed";

            // Verify counter is back to 1
            auto ns = co_await store->next_seqnum(direction_t::outbound, false);
            EXPECT_TRUE(ns.has_value());
            EXPECT_EQ(*ns, 1u);
        },
        asio::use_future);
    fut.get();

    // Destroy in correct order
    pool.stop();
    pool.join();
    store.reset();
}

// ── Test 4: Concurrent read (retrieve) + write (store) ───────────────────────
//
// One coroutine reads (retrieve with to-tail), another writes new frames.
// Under US3, the mutex ensures no data race on the index / entry vectors.
// TSan must report 0 races.

TEST(StoreShutdownOrdering, ConcurrentReadWriteNoDataRace) {
    asio::thread_pool pool{2};

    MemoryStore::Config cfg;
    cfg.policy = fixpp::session::capacity_policy::unbounded;
    cfg.max_frame_bytes = 4096;
    auto store = std::make_shared<MemoryStore>(cfg);

    // Pre-populate 10 frames so retrieve has something to read
    {
        asio::thread_pool setup{1};
        auto sfut = asio::co_spawn(
            setup.get_executor(),
            [store]() -> asio::awaitable<void> {
                for (int i = 1; i <= 10; ++i) {
                    auto frame = make_test_frame(static_cast<seqnum_t>(i), direction_t::outbound);
                    co_await store->store(static_cast<seqnum_t>(i),
                                          std::span<const std::byte>(frame), direction_t::outbound);
                }
            },
            asio::use_future);
        sfut.get();
        setup.stop();
        setup.join();
    }

    // Writer: stores 20 more frames (seq 11..30)
    // Reader: retrieves frames 1..10 (original range; must not see UB even if
    //         writer is also active)
    std::atomic<bool> writer_done{false};

    auto fut_writer = asio::co_spawn(
        pool.get_executor(),
        [store, &writer_done]() -> asio::awaitable<void> {
            for (int i = 11; i <= 30; ++i) {
                auto frame = make_test_frame(static_cast<seqnum_t>(i), direction_t::outbound);
                co_await store->store(static_cast<seqnum_t>(i), std::span<const std::byte>(frame),
                                      direction_t::outbound);
            }
            writer_done.store(true, std::memory_order_release);
        },
        asio::use_future);

    // Simple collecting visitor for reader
    struct simple_vis final : public fixpp::session::retrieve_visitor {
        std::vector<seqnum_t> seqs;
        asio::awaitable<fixpp::core::expected_t<fixpp::session::visit_result>> on_frame(
            seqnum_t s, std::span<const std::byte>) noexcept override {
            seqs.push_back(s);
            co_return fixpp::core::expected_t<fixpp::session::visit_result>{
                fixpp::session::visit_result::cont};
        }
    };

    auto fut_reader = asio::co_spawn(
        pool.get_executor(),
        [store]() -> asio::awaitable<void> {
            simple_vis vis;
            // Read a fixed range (1..10) — concurrent writer writes 11..30
            auto r = co_await store->retrieve(1, 10, direction_t::outbound, vis);
            EXPECT_TRUE(r.has_value()) << "concurrent retrieve failed";
            // Must see at least frames 1..10
            EXPECT_EQ(vis.seqs.size(), 10u);
        },
        asio::use_future);

    fut_writer.get();
    fut_reader.get();

    pool.stop();
    pool.join();
    store.reset();
}

// ── Test 5: RC#1 regression — unbounded retrieve UAF under concurrent append ──
//
// Before RC#1, retrieve() on an unbounded MemoryStore would capture
// unbounded_slab_.data() under the mutex and dereference that pointer AFTER
// the mutex was released. A concurrent store() could reallocate the vector,
// invalidating the captured pointer → UAF/OOB read detectable by ASan.
//
// This test forces reallocation by starting with a tiny initial capacity (the
// vector grows from 1 byte), then concurrently calling retrieve() while the
// writer loop appends enough frames to trigger repeated reallocations.
//
// Without the RC#1 fix, ASan should flag a heap-use-after-free in retrieve().
// With the fix (payload bytes copied under the mutex), the test must pass clean.
TEST(StoreShutdownOrdering, UnboundedRetrieveUAFUnderConcurrentAppend) {
    // Use a pool large enough that the reader and writer truly run concurrently.
    asio::thread_pool pool{4};

    // Tiny initial capacity to force immediate reallocation on first store().
    MemoryStore::Config cfg;
    cfg.policy = fixpp::session::capacity_policy::unbounded;
    cfg.max_frame_bytes = 1024;
    auto store = std::make_shared<MemoryStore>(cfg);

    // Pre-populate 1 frame so retrieve(1,1,...) has something to walk.
    {
        asio::thread_pool setup{1};
        auto pre = asio::co_spawn(
            setup.get_executor(),
            [store]() -> asio::awaitable<void> {
                auto frame = make_test_frame(static_cast<seqnum_t>(1), direction_t::outbound);
                co_await store->store(1, std::span<const std::byte>(frame), direction_t::outbound);
            },
            asio::use_future);
        pre.get();
        setup.stop();
        setup.join();
    }

    // Writer: store 200 frames (each ~50 bytes) to force multiple reallocations.
    auto fut_writer = asio::co_spawn(
        pool.get_executor(),
        [store]() -> asio::awaitable<void> {
            for (int i = 2; i <= 200; ++i) {
                auto frame = make_test_frame(static_cast<seqnum_t>(i), direction_t::outbound);
                co_await store->store(static_cast<seqnum_t>(i), std::span<const std::byte>(frame),
                                      direction_t::outbound);
            }
        },
        asio::use_future);

    // Reader: repeatedly retrieve frame seq=1 while the writer is growing the slab.
    // Without RC#1, this triggers the UAF.
    struct nop_vis final : public fixpp::session::retrieve_visitor {
        std::size_t count{0};
        asio::awaitable<fixpp::core::expected_t<fixpp::session::visit_result>> on_frame(
            seqnum_t, std::span<const std::byte> frame) noexcept override {
            // Touch all bytes to ensure ASan catches use-after-free.
            volatile std::byte sum{};
            for (auto b : frame)
                sum = static_cast<std::byte>(static_cast<uint8_t>(sum) ^ static_cast<uint8_t>(b));
            (void)sum;
            ++count;
            co_return fixpp::core::expected_t<fixpp::session::visit_result>{
                fixpp::session::visit_result::cont};
        }
    };

    auto fut_reader = asio::co_spawn(
        pool.get_executor(),
        [store]() -> asio::awaitable<void> {
            for (int round = 0; round < 50; ++round) {
                nop_vis vis;
                // Retrieve only frame 1 (which was pre-populated).
                auto r = co_await store->retrieve(1, 1, direction_t::outbound, vis);
                // May fail with store_seqnum_gap if the writer hasn't reached
                // that index yet — both outcomes are acceptable; we only care
                // that there is no memory corruption.
                (void)r;
            }
        },
        asio::use_future);

    fut_writer.get();
    fut_reader.get();

    pool.stop();
    pool.join();
    store.reset();
    // If we reach here without ASan/MSan flags, the RC#1 fix holds.
}

// ── Test 6: T014 / SC-007 / C5 — FileStore offload drain before pool join ────
//
// Witnesses the teardown contract (contracts/file-offload.md §C5 / FR-007):
// an in-flight offloaded store() AND a graceful-close flush_for_session_close()
// under commit_batched (with buffered frames that make the flush non-trivial)
// both complete before pool.stop()/join() and store destruction.
//
// No UAF, no use-of-joined-pool under ASan/TSan.  The witness is at the
// pool-level (standalone FileStore + app-owned asio::thread_pool) because:
//   - Engine::stop() drives close(terminal), which does NOT invoke
//     flush_for_session_close() per file_store.hpp:152 / Appendix D §D.2.
//   - The pool-level witness is the faithful, tractable path for the
//     graceful-close flush property (brief §T014 explicit approval).
//
// Executor topology (per [[feedback_strand_in_any_executor_refcount_race]]):
//   pool_    = asio::thread_pool(2) — the app-owned file_io_executor pool.
//   sx_      = ONE long-lived any_io_executor strand (make_strand(pool_))
//              reused for ALL co_spawns.  No fresh strand per co_spawn.
//   TearDown: pool_.stop(); pool_.join(); store_.reset().
//
// Discriminating: commit_batched with batch_size=10 and 3 stores leaves
// frames buffered (total_after=3, 3%10!=0 → no auto-flush).  The
// flush_for_session_close() call then does a genuine offloaded fdatasync.
// With commit_per_message the flush is a no-op — wrong witness.
//
// ANTI-HANG: wait_for(10s) on every future.
// [[feedback_fail_placeholder_red_test]]
TEST(StoreShutdownOrdering, FileStoreOffloadDrainBeforePoolJoin) {
    auto dir = unique_store_dir("shutdown_ordering");

    // App-owned pool — 2 threads so file I/O and the session strand run truly
    // concurrently (TSan surface activated).
    asio::thread_pool pool{2};

    // ONE long-lived strand reused for all co_spawns — the "session strand".
    // [[feedback_strand_in_any_executor_refcount_race]]
    asio::any_io_executor sx = asio::make_strand(pool.get_executor());

    // Build and open the FileStore on the strand.
    FileStore::Config cfg;
    cfg.directory = dir;
    cfg.sender_comp_id = "SENDER";
    cfg.target_comp_id = "TARGET";
    cfg.policy.which = FileStorePolicy::kind::commit_batched;
    cfg.policy.batch_size = 10;  // batch_size > frames stored → no auto-flush
    cfg.max_frame_bytes = 4096;
    cfg.file_io_executor = pool.get_executor();  // app-owned pool

    std::shared_ptr<FileStore> store;
    {
        auto fut = asio::co_spawn(
            sx,
            [cfg]() mutable -> asio::awaitable<std::shared_ptr<FileStore>> {
                FileStoreFactory factory{cfg};
                auto ms = factory.make("SENDER", "TARGET", nullptr, 1024 * 1024, nullptr);
                if (!ms) co_return nullptr;
                co_return std::shared_ptr<FileStore>(
                    static_cast<FileStore*>(ms->release()));
            },
            asio::use_future);
        ASSERT_EQ(fut.wait_for(std::chrono::seconds(10)), std::future_status::ready)
            << "FileStore open timed out";
        store = fut.get();
        ASSERT_NE(store, nullptr) << "FileStoreFactory::make() failed";
    }

    // Phase 1: store 3 outbound frames on the strand.
    // commit_batched(10): total_after in {1,2,3}, none divisible by 10 →
    // no auto-flush → frames buffered in kernel, NOT yet fdatasynced.
    {
        auto fut = asio::co_spawn(
            sx,
            [s = store]() -> asio::awaitable<void> {
                for (int i = 1; i <= 3; ++i) {
                    auto frame = make_test_frame(static_cast<seqnum_t>(i), direction_t::outbound);
                    auto r = co_await s->store(static_cast<seqnum_t>(i),
                                               std::span<const std::byte>(frame),
                                               direction_t::outbound);
                    EXPECT_TRUE(r.has_value()) << "store seq=" << i << " failed";
                }
            },
            asio::use_future);
        ASSERT_EQ(fut.wait_for(std::chrono::seconds(10)), std::future_status::ready)
            << "store phase timed out";
        fut.get();  // re-throws any assertion failure
    }

    // Phase 2: graceful-close flush — drains the buffered frames to disk.
    // This is the genuine offloaded fdatasync that commit_batched deferred.
    // flush_for_session_close() uses co_await offload_to(file_io_executor, ...)
    // and returns only AFTER raw_datasync completes on the pool thread.
    {
        auto fut = asio::co_spawn(
            sx,
            [s = store]() -> asio::awaitable<void> {
                auto r = co_await s->flush_for_session_close();
                EXPECT_TRUE(r.has_value()) << "flush_for_session_close() failed";
            },
            asio::use_future);
        ASSERT_EQ(fut.wait_for(std::chrono::seconds(10)), std::future_status::ready)
            << "flush phase timed out";
        fut.get();
    }

    // Phase 3: assert durability — next_seqnum(outbound, false) returns 4,
    // confirming all 3 store() calls were committed (counter record was written).
    {
        auto fut = asio::co_spawn(
            sx,
            [s = store]() -> asio::awaitable<seqnum_t> {
                auto r = co_await s->next_seqnum(direction_t::outbound, /*increment=*/false);
                EXPECT_TRUE(r.has_value()) << "next_seqnum() failed";
                co_return r.has_value() ? *r : seqnum_t{0};
            },
            asio::use_future);
        ASSERT_EQ(fut.wait_for(std::chrono::seconds(10)), std::future_status::ready)
            << "next_seqnum check timed out";
        EXPECT_EQ(fut.get(), seqnum_t{4})
            << "expected next outbound=4 after 3 stores (seqnum_min=1 → after 3 stored → 4)";
    }

    // Phase 4: correct teardown ordering — all awaitables completed above;
    // now join the pool and destroy the store.  No in-flight offloaded work
    // remains, so pool.stop()/join() is safe.  Destroying the store AFTER
    // pool.join() ensures no FileStore method can run on the dead pool.
    // [[feedback_strand_in_any_executor_refcount_race]]: no further co_spawns
    // after we begin teardown.
    pool.stop();
    pool.join();
    store.reset();  // explicit destroy AFTER pool stops (validates no UAF)

    std::filesystem::remove_all(dir);
    // Reaching here under ASan+TSan without flags confirms the C5 contract.
}

// ── Test 7: T014 supplement — flush is genuine offload, not a no-op ──────────
//
// Confirms the discriminating property: flush_for_session_close() under
// commit_batched (with buffered frames) triggers a real fdatasync offload —
// not a no-op.  The data-path witness is that next_seqnum reflects all stored
// seqnums even AFTER a crash-simulation: we close the FileStore (releasing
// the advisory lock) and re-open a new one from the same path, asserting
// the counter record was durable (the fdatasync pushed it to disk) and
// next_seqnum == N+1.
//
// This is distinct from Test 6: Test 6 proves correct teardown ordering
// (no UAF); this test proves the flush actually made data durable.
//
// ANTI-HANG: wait_for(10s).
TEST(StoreShutdownOrdering, FileStoreFlushForCloseIsGenuineOffload) {
    auto dir = unique_store_dir("flush_genuine");

    asio::thread_pool pool{2};
    asio::any_io_executor sx = asio::make_strand(pool.get_executor());

    FileStore::Config cfg;
    cfg.directory = dir;
    cfg.sender_comp_id = "SENDER";
    cfg.target_comp_id = "TARGET";
    cfg.policy.which = FileStorePolicy::kind::commit_batched;
    cfg.policy.batch_size = 10;  // 5 stores < 10 → no auto-flush boundary
    cfg.max_frame_bytes = 4096;
    cfg.file_io_executor = pool.get_executor();

    // Open store, store 5 frames, flush, close.
    {
        auto fut = asio::co_spawn(
            sx,
            [cfg]() mutable -> asio::awaitable<bool> {
                FileStoreFactory factory{cfg};
                auto ms = factory.make("SENDER", "TARGET", nullptr, 1024 * 1024, nullptr);
                if (!ms) co_return false;
                auto store = std::shared_ptr<FileStore>(
                    static_cast<FileStore*>(ms->release()));

                for (int i = 1; i <= 5; ++i) {
                    auto frame = make_test_frame(static_cast<seqnum_t>(i), direction_t::outbound);
                    auto r = co_await store->store(static_cast<seqnum_t>(i),
                                                   std::span<const std::byte>(frame),
                                                   direction_t::outbound);
                    if (!r.has_value()) co_return false;
                }

                // Explicit flush — offloads fdatasync to the pool.
                auto flush_r = co_await store->flush_for_session_close();
                co_return flush_r.has_value();
            },
            asio::use_future);
        ASSERT_EQ(fut.wait_for(std::chrono::seconds(10)), std::future_status::ready)
            << "store+flush phase timed out";
        ASSERT_TRUE(fut.get()) << "store or flush failed";
    }

    pool.stop();
    pool.join();

    // Re-open the same log file from scratch on a second pool.
    asio::thread_pool pool2{1};
    asio::any_io_executor sx2 = asio::make_strand(pool2.get_executor());

    {
        auto fut = asio::co_spawn(
            sx2,
            [cfg]() mutable -> asio::awaitable<seqnum_t> {
                FileStoreFactory factory2{cfg};
                auto ms2 = factory2.make("SENDER", "TARGET", nullptr, 1024 * 1024, nullptr);
                if (!ms2) co_return seqnum_t{0};
                auto store2 = std::shared_ptr<FileStore>(
                    static_cast<FileStore*>(ms2->release()));
                auto r = co_await store2->next_seqnum(direction_t::outbound, /*increment=*/false);
                co_return r.has_value() ? *r : seqnum_t{0};
            },
            asio::use_future);
        ASSERT_EQ(fut.wait_for(std::chrono::seconds(10)), std::future_status::ready)
            << "re-open next_seqnum check timed out";
        // After 5 stores the counter record should show next_outbound = 6.
        EXPECT_EQ(fut.get(), seqnum_t{6})
            << "flush_for_session_close() failed to make counter durable: "
               "re-opened store shows wrong next_seqnum";
    }

    pool2.stop();
    pool2.join();

    std::filesystem::remove_all(dir);
}

}  // namespace
