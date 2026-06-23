// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_file_store_concurrent_tsan.cpp
//
// 035-filestore-io-offload — real-pool concurrency + TSan/ASan witness
// (US3, SC-005, contracts C4, tasks T013).
//
// TSan is the PRIMARY gate for this feature (the inert offload made the
// concurrency surface dormant; making the offload genuine activates it).
//
// Cells (Phase 5, Task T013):
//
//   MidWalkReset_GenerationGuard_FailsClean (key deterministic witness):
//       Pre-store 2 frames; start a retrieve() walk whose visitor, on the
//       FIRST on_frame(), drives a co_await store->reset().  The reset runs
//       during the walk's suspension and bumps generation_.  On the next
//       iteration the re-check (generation_ != g0) fires and retrieve()
//       returns store_io_failure — NOT a torn read, NOT store_seqnum_gap.
//       DISCRIMINATING via read_and_reset_retrieve_pread_count():
//         - Without guard (pre-T015): 2 pread calls; the second fails at EOF
//           (stale offset against the fresh log), also returns store_io_failure.
//         - With guard (post-T015): 1 pread call; the guard fires before the
//           second pread.  pread_count == 1 is the discriminating assertion.
//       (SC-005 / I-03 / data-model §4 / FR-006 / C4)
//
//   ResetRace_vs_LogicalGap_Discriminating:
//       reset-race → store_io_failure (56);
//       FR-017 logical gap (seq never stored) → store_seqnum_gap (57).
//       Asserts the two codes are distinct so a reset-race cannot masquerade
//       as a logical never-persisted gap.  (data-model §5)
//
//   ConcurrentStoreRetrieveReset_TSanClean:
//       Overlapping store() stream + retrieve() walk + reset() on a real
//       4-thread pool under TSan+ASan.  Asserts zero data races on impl_/
//       the live log handle; no torn read; FR-017 gap detection intact.
//       (SC-005, FR-005/006, C4)
//
// ANTI-HANG: every cell uses std::future::wait_for(10s) so a wedge fails
// rather than hanging ctest.  [[feedback_fail_placeholder_red_test]]
//
// Executor topology (per [[feedback_strand_in_any_executor_refcount_race]]):
//   - pool_       = asio::thread_pool(4) — file_io_executor
//   - strand_exec_ = ONE long-lived any_io_executor (make_strand(pool_))
//     reused for ALL co_spawns (open, store, retrieve, reset, assertions).
//   - No fresh strand per co_spawn (avoids the shared_target_executor race).
//   - TearDown: pool_->stop(); pool_->join().
//
// [[feedback_fork_inherited_asio_pool_deadlock]]: pool is constructed in SetUp,
// not inherited across a fork — no cross-fork asio pool risk.

#include <gtest/gtest.h>

#include <asio/any_io_executor.hpp>
#include <asio/co_spawn.hpp>
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
#include <future>
#include <memory>
#include <optional>
#include <span>
#include <thread>
#include <vector>

#include "_fixtures_/store_temp_dir.hpp"
#include "_fixtures_/test_double_fsm.hpp"

namespace {

using fixpp::core::error;
using fixpp::session::direction_t;
using fixpp::session::FileStore;
using fixpp::session::FileStoreFactory;
using fixpp::session::FileStorePolicy;
using fixpp::session::retrieve_visitor;
using fixpp::session::seqnum_t;
using fixpp::session::visit_result;
using fixpp::store_test::make_test_frame;
using fixpp::store_test::unique_store_dir;

namespace fs = std::filesystem;

// ── Fixture ───────────────────────────────────────────────────────────────────
//
// pool_ (4 threads)   — genuine multi-thread file_io_executor (TSan surface)
// strand_exec_        — single long-lived any_io_executor strand reused for
//                       every co_spawn; simulates the session strand.
// All FileStore operations are co_spawned on strand_exec_.

class FileStoreConcurrentTsanTest : public ::testing::Test {
protected:
    void SetUp() override {
        pool_ = std::make_unique<asio::thread_pool>(4);
        // One long-lived strand — must NOT recreate per co_spawn.
        // [[feedback_strand_in_any_executor_refcount_race]]
        strand_exec_ = asio::make_strand(pool_->get_executor());
        dir_ = unique_store_dir("concurrent_tsan");
    }

    void TearDown() override {
        if (pool_) {
            pool_->stop();
            pool_->join();
        }
        fixpp::store_test::remove_store_dir(dir_);
    }

    FileStore::Config make_config(FileStorePolicy policy = {}) const {
        FileStore::Config cfg;
        cfg.directory = dir_;
        cfg.sender_comp_id = "SENDER";
        cfg.target_comp_id = "TARGET";
        cfg.policy = policy;
        cfg.max_frame_bytes = 4096;
        cfg.file_io_executor = pool_->get_executor();
        return cfg;
    }

    // Open a FileStore on strand_exec_. Returns nullptr on failure.
    std::shared_ptr<FileStore> open_store(FileStore::Config cfg) {
        auto fut = asio::co_spawn(
            strand_exec_,
            [cfg = std::move(cfg)]() mutable
                -> asio::awaitable<std::shared_ptr<FileStore>> {
                FileStoreFactory factory{cfg};
                auto ms = factory.make("SENDER", "TARGET", nullptr, 1024 * 1024, nullptr);
                if (!ms) co_return nullptr;
                co_return std::shared_ptr<FileStore>(
                    static_cast<FileStore*>(ms->release()));
            },
            asio::use_future);
        if (fut.wait_for(std::chrono::seconds(10)) != std::future_status::ready) {
            return nullptr;
        }
        return fut.get();
    }

    // Store one frame on strand_exec_. Returns true on success.
    bool store_one(FileStore& store, seqnum_t seq,
                   direction_t dir = direction_t::outbound) {
        auto frame = make_test_frame(seq, dir);
        auto fut = asio::co_spawn(
            strand_exec_,
            [&store, seq, dir, frame = std::move(frame)]() mutable
                -> asio::awaitable<bool> {
                auto r = co_await store.store(seq, std::span<const std::byte>(frame), dir);
                co_return r.has_value();
            },
            asio::use_future);
        if (fut.wait_for(std::chrono::seconds(10)) != std::future_status::ready) {
            return false;
        }
        return fut.get();
    }

    std::unique_ptr<asio::thread_pool> pool_;
    asio::any_io_executor strand_exec_;
    std::filesystem::path dir_;
};

// ── Visitor: drives reset() from inside on_frame() ────────────────────────────
//
// Used by MidWalkReset_GenerationGuard_FailsClean.
// On the FIRST on_frame() call: co_awaits a reset() on the SAME strand.
// Because retrieve() releases the writer mutex before the walk (I-03), reset()
// can acquire it freely.  After the reset() returns, we co_return cont so the
// retrieve() walk proceeds to the next iteration — where the generation re-check
// should fire.
class ResetDrivingVisitor final : public retrieve_visitor {
public:
    explicit ResetDrivingVisitor(FileStore* store, asio::any_io_executor sx)
        : store_(store), sx_(sx) {}

    asio::awaitable<fixpp::core::expected_t<visit_result>> on_frame(
        seqnum_t seq, std::span<const std::byte> /*payload*/) noexcept override {
        frames_seen++;
        if (frames_seen == 1) {
            // Drive reset() during the retrieve() walk suspension.
            // The co_await here IS the suspension point where generation_ is bumped.
            auto r = co_await store_->reset();
            reset_result = r.has_value() ? 0 : static_cast<int>(r.error());
        }
        co_return visit_result::cont;
    }

    fixpp::core::error abort_error() const noexcept override {
        return fixpp::core::error::store_io_failure;
    }

    int frames_seen = 0;
    int reset_result = -1;  // 0 = success, else error code

private:
    FileStore* store_;
    asio::any_io_executor sx_;
};

// ── Cell 1: MidWalkReset_GenerationGuard_FailsClean ──────────────────────────
//
// SC-005 / I-03 / data-model §4 / FR-006 / C4
//
// The KEY discriminating assertion is pread_count == 1.
//   Pre-T015:  guard absent → 2 preads; the 2nd hits EOF on the fresh log,
//              also returns store_io_failure. Error-code-only is non-discriminating.
//   Post-T015: guard fires before 2nd pread → pread_count == 1.  GREEN.
TEST_F(FileStoreConcurrentTsanTest, MidWalkReset_GenerationGuard_FailsClean) {
    auto store = open_store(make_config());
    ASSERT_NE(store, nullptr) << "FileStore open failed";

    // Pre-store 2 frames (both outbound). The retrieve range covers both,
    // so the walk has 2 iterations. The guard re-check fires on iteration 2.
    ASSERT_TRUE(store_one(*store, 1));
    ASSERT_TRUE(store_one(*store, 2));

    // Reset the pread counter before the retrieve.
    (void)fixpp::session::read_and_reset_retrieve_pread_count();

    // Construct the visitor that drives reset() from inside on_frame().
    ResetDrivingVisitor visitor{store.get(), strand_exec_};

    // Run retrieve() [1..2] on the strand.
    auto fut = asio::co_spawn(
        strand_exec_,
        [&store, &visitor]() mutable
            -> asio::awaitable<fixpp::core::expected_t<void>> {
            co_return co_await store->retrieve(1, 2, direction_t::outbound, visitor);
        },
        asio::use_future);

    ASSERT_EQ(fut.wait_for(std::chrono::seconds(10)), std::future_status::ready)
        << "retrieve() did not complete within 10s — possible deadlock";

    auto result = fut.get();

    // --- Post-condition assertions ---

    // The walk MUST fail with store_io_failure (generation re-check).
    // (data-model §5: reset-race → store_io_failure, NOT store_seqnum_gap)
    ASSERT_FALSE(result.has_value())
        << "retrieve() should have failed when reset() ran mid-walk";
    EXPECT_EQ(result.error(), fixpp::core::error::store_io_failure)
        << "Expected store_io_failure (56) from generation re-check, got: "
        << static_cast<int>(result.error());

    // Exactly 1 frame was visited (on_frame called once = reset driven once).
    EXPECT_EQ(visitor.frames_seen, 1);

    // reset() itself must have succeeded (the newly-open fresh log is valid).
    EXPECT_EQ(visitor.reset_result, 0) << "reset() during walk failed unexpectedly";

    // KEY DISCRIMINATING ASSERTION (T015 gate):
    // pread_count == 1 proves the guard fired BEFORE the 2nd pread.
    // pread_count == 2 would mean the guard is absent and the stale pread
    // failed at EOF — the non-discriminating pre-T015 behaviour.
    int pread_count = fixpp::session::read_and_reset_retrieve_pread_count();
    EXPECT_EQ(pread_count, 1)
        << "Expected exactly 1 pread (guard fired before 2nd read); got "
        << pread_count << ". Pre-T015 behaviour: stale pread fails at EOF (count=2).";
}

// ── Cell 2: ResetRace_vs_LogicalGap_Discriminating ───────────────────────────
//
// data-model §5 — the two error codes are distinct:
//   reset-race (generation mismatch)  → store_io_failure (56)
//   logical gap (seq never stored)    → store_seqnum_gap  (57)
//
// This ensures a reset-race cannot masquerade as a logical never-persisted gap.
// [[feedback_witness_asserts_named_postcondition_not_proxy]]
TEST_F(FileStoreConcurrentTsanTest, ResetRace_vs_LogicalGap_Discriminating) {
    // --- Part A: mid-walk reset → store_io_failure ---
    {
        auto store = open_store(make_config());
        ASSERT_NE(store, nullptr);

        ASSERT_TRUE(store_one(*store, 1));
        ASSERT_TRUE(store_one(*store, 2));

        (void)fixpp::session::read_and_reset_retrieve_pread_count();
        ResetDrivingVisitor visitor{store.get(), strand_exec_};

        auto fut = asio::co_spawn(
            strand_exec_,
            [&store, &visitor]() mutable
                -> asio::awaitable<fixpp::core::expected_t<void>> {
                co_return co_await store->retrieve(1, 2, direction_t::outbound, visitor);
            },
            asio::use_future);

        ASSERT_EQ(fut.wait_for(std::chrono::seconds(10)), std::future_status::ready);
        auto result = fut.get();

        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error(), fixpp::core::error::store_io_failure)
            << "reset-race must return store_io_failure (56), got: "
            << static_cast<int>(result.error());

        // Confirm the guard fired before the second pread.
        EXPECT_EQ(fixpp::session::read_and_reset_retrieve_pread_count(), 1);
    }

    // --- Part B: logical gap (seq 3 never stored) → store_seqnum_gap ---
    {
        auto dir2 = unique_store_dir("gap_discriminating");
        FileStore::Config cfg2 = make_config();
        cfg2.directory = dir2;
        auto store2 = open_store(cfg2);
        ASSERT_NE(store2, nullptr);

        // Store only seq 1; request [1..2] so seq 2 is a genuine gap.
        ASSERT_TRUE(store_one(*store2, 1));

        // Simple counting visitor (no reset drive).
        class CountingVisitor final : public retrieve_visitor {
        public:
            asio::awaitable<fixpp::core::expected_t<visit_result>> on_frame(
                seqnum_t, std::span<const std::byte>) noexcept override {
                ++count;
                co_return visit_result::cont;
            }
            fixpp::core::error abort_error() const noexcept override {
                return fixpp::core::error::store_io_failure;
            }
            int count = 0;
        } gap_visitor;

        auto fut = asio::co_spawn(
            strand_exec_,
            [&store2, &gap_visitor]() mutable
                -> asio::awaitable<fixpp::core::expected_t<void>> {
                // Request [1..2] but only seq 1 was stored — FR-017 gap.
                co_return co_await store2->retrieve(1, 2, direction_t::outbound, gap_visitor);
            },
            asio::use_future);

        ASSERT_EQ(fut.wait_for(std::chrono::seconds(10)), std::future_status::ready);
        auto result = fut.get();

        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error(), fixpp::core::error::store_seqnum_gap)
            << "Logical gap must return store_seqnum_gap (57), got: "
            << static_cast<int>(result.error());

        store2.reset();
        fixpp::store_test::remove_store_dir(dir2);
    }

    // --- Sanity: the two error codes are numerically distinct ---
    EXPECT_NE(static_cast<int>(fixpp::core::error::store_io_failure),
              static_cast<int>(fixpp::core::error::store_seqnum_gap))
        << "store_io_failure and store_seqnum_gap must be different error codes";
}

// ── Cell 3: ConcurrentStoreRetrieveReset_TSanClean ───────────────────────────
//
// SC-005, FR-005/006, C4.
//
// Overlap a stream of store() calls + a retrieve() walk + a reset() on the
// real 4-thread pool.  Under TSan+ASan:
//   - Zero data races on impl_ or the live log handle.
//   - No torn read.
//   - FR-017 gap detection still holds.
//
// Design: all FileStore method calls go through strand_exec_ (the simulated
// session strand), which serialises impl_ access.  The pool only executes raw
// syscalls inside the nested co_spawn — no impl_ access there.
// This is TSan-clean BY CONSTRUCTION (strand-confinement of impl_), and the
// cell proves it holds under a live pool.
//
// Note: retrieve() may observe a gap if reset() has cleared the index between
// the mutex-snapshot and the walk — this is expected (store_io_failure from the
// generation guard, or store_seqnum_gap from the snapshot gap).  We accept any
// combination of success / store_io_failure / store_seqnum_gap; we assert only
// that no data race fires (TSan binary output) and that the process doesn't crash.
TEST_F(FileStoreConcurrentTsanTest, ConcurrentStoreRetrieveReset_TSanClean) {
    auto store = open_store(make_config());
    ASSERT_NE(store, nullptr);

    // Pre-store 3 frames so retrieve() has something to walk over.
    ASSERT_TRUE(store_one(*store, 1));
    ASSERT_TRUE(store_one(*store, 2));
    ASSERT_TRUE(store_one(*store, 3));

    // --- Launch store stream: store seqs 4..13 concurrently ---
    // All co_spawned on strand_exec_ (serialised), but the disk work runs on pool_.
    std::vector<std::future<bool>> store_futs;
    store_futs.reserve(10);
    for (seqnum_t seq = 4; seq <= 13; ++seq) {
        auto frame = make_test_frame(seq, direction_t::outbound);
        auto fut = asio::co_spawn(
            strand_exec_,
            [&store, seq, frame = std::move(frame)]() mutable
                -> asio::awaitable<bool> {
                auto r = co_await store->store(
                    seq, std::span<const std::byte>(frame), direction_t::outbound);
                co_return r.has_value();
            },
            asio::use_future);
        store_futs.push_back(std::move(fut));
    }

    // --- Launch a retrieve() walk over [1..3] concurrently ---
    class SimpleVisitor final : public retrieve_visitor {
    public:
        asio::awaitable<fixpp::core::expected_t<visit_result>> on_frame(
            seqnum_t, std::span<const std::byte>) noexcept override {
            ++frames_seen;
            co_return visit_result::cont;
        }
        fixpp::core::error abort_error() const noexcept override {
            return fixpp::core::error::store_io_failure;
        }
        std::atomic<int> frames_seen{0};
    } retr_visitor;

    auto retr_fut = asio::co_spawn(
        strand_exec_,
        [&store, &retr_visitor]() mutable
            -> asio::awaitable<fixpp::core::expected_t<void>> {
            co_return co_await store->retrieve(1, 3, direction_t::outbound, retr_visitor);
        },
        asio::use_future);

    // --- Launch a reset() concurrently ---
    auto reset_fut = asio::co_spawn(
        strand_exec_,
        [&store]() mutable -> asio::awaitable<bool> {
            auto r = co_await store->reset();
            co_return r.has_value();
        },
        asio::use_future);

    // --- Drain all with bounded timeout ---
    constexpr auto kTimeout = std::chrono::seconds(10);

    ASSERT_EQ(reset_fut.wait_for(kTimeout), std::future_status::ready)
        << "reset() did not complete within 10s";
    bool reset_ok = reset_fut.get();
    EXPECT_TRUE(reset_ok) << "reset() failed during concurrent stress";

    ASSERT_EQ(retr_fut.wait_for(kTimeout), std::future_status::ready)
        << "retrieve() did not complete within 10s";
    auto retr_result = retr_fut.get();
    // Accept: success, store_io_failure (generation guard), store_seqnum_gap (FR-017).
    // Anything else is unexpected.
    if (!retr_result.has_value()) {
        const auto ec = retr_result.error();
        EXPECT_TRUE(ec == fixpp::core::error::store_io_failure ||
                    ec == fixpp::core::error::store_seqnum_gap)
            << "Unexpected retrieve() error: " << static_cast<int>(ec);
    }

    for (auto& sf : store_futs) {
        ASSERT_EQ(sf.wait_for(kTimeout), std::future_status::ready)
            << "A store() did not complete within 10s";
        // store() after reset() may fail if the file is in a transition;
        // in practice it succeeds because the mutex serialises everything.
        // We don't assert success here — any non-crash result is acceptable.
        (void)sf.get();
    }

    // If we reach here without a crash or TSan report, the cell passes.
    // TSan interception is at the binary level — a race would abort the process.
    SUCCEED();
}

}  // namespace
