// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_memory_store_reset_during_retrieve.cpp
//
// S-P2-2 — MemoryStore retrieve() reset-during-traversal guard witness.
//
// Finding (phase-9 outbound-store-retention-review.md §P2-2): MemoryStore's
// retrieve() had NO equivalent of FileStore's generation_ epoch. For the
// bounded policy it resolved slab_ + e.slab_offset AFTER releasing the mutex
// and read it inside visitor.on_frame() ACROSS the co_await suspension.
// reset() does NOT deallocate the slab; new store() calls overwrite from slot
// 0. So a reset()+store() during a visitor suspension overwrote the slab slot
// a not-yet-visited Entry descriptor points at → the visitor copied the NEW
// frame's bytes under the OLD seqnum, silently. This violates the
// message_store.hpp:113-116 contract ("mid-traversal mutation is detected …
// without UB"). Bounds-safe (fixed slab) — a data-integrity hole, not a UAF.
//
// Fix (mirrors FileStore T015): a per-instance generation_ epoch, bumped under
// the mutex in reset(), snapshotted (g0) under the mutex in retrieve(), and
// re-checked at the top of the visitor loop with NO co_await between the check
// and the byte read. A mid-traversal reset makes generation_ != g0 → retrieve()
// fails closed with store_io_failure BEFORE reading the overwritten slot. The
// bounded path additionally materialises the slab bytes into a stable scratch
// buffer before on_frame() so a reset()+store() during the CURRENT frame's own
// suspension cannot corrupt the view the visitor is holding.
//
// DISCRIMINATING witness (both cells):
//   Pre-fix : retrieve() returns SUCCESS, frames_seen == 2, and (bounded) the
//             2nd frame carries the NEW store's bytes under the OLD seqnum.
//   Post-fix: retrieve() returns store_io_failure, frames_seen == 1 (the guard
//             fires before the 2nd frame is visited).
//
// Executor topology mirrors test_file_store_concurrent_tsan.cpp: ONE long-lived
// strand over a thread_pool, reused for every co_spawn (the simulated session
// strand). reset()/store() acquire the writer mutex freely because retrieve()
// released it before the walk (I-03). No FIXPP_TEST_HOOKS needed.
//
// ANTI-HANG: every cell uses std::future::wait_for(10s) so a wedge fails rather
// than hanging ctest. [[feedback_fail_placeholder_red_test]]

#include <gtest/gtest.h>

#include <asio/any_io_executor.hpp>
#include <asio/co_spawn.hpp>
#include <asio/strand.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <fixpp/core/error.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/memory_store.hpp>
#include <fixpp/session/retrieve_visitor.hpp>
#include <fixpp/session/seqnum.hpp>
#include <future>
#include <memory>
#include <span>
#include <vector>

namespace {

using fixpp::core::error;
using fixpp::session::capacity_policy;
using fixpp::session::direction_t;
using fixpp::session::MemoryStore;
using fixpp::session::retrieve_visitor;
using fixpp::session::seqnum_t;
using fixpp::session::visit_result;

// A payload of `len` bytes all equal to `fill` — recognisable content so the
// witness can tell the ORIGINAL frame from the reset()+store()'d NEW frame.
std::vector<std::byte> filled_frame(std::size_t len, std::uint8_t fill) {
    return std::vector<std::byte>(len, static_cast<std::byte>(fill));
}

// ── Fixture ───────────────────────────────────────────────────────────────────
class MemoryStoreResetDuringRetrieveTest : public ::testing::Test {
protected:
    void SetUp() override {
        pool_ = std::make_unique<asio::thread_pool>(4);
        // One long-lived strand — never recreated per co_spawn.
        // [[feedback_strand_in_any_executor_refcount_race]]
        strand_exec_ = asio::make_strand(pool_->get_executor());
    }

    void TearDown() override {
        if (pool_) {
            pool_->stop();
            pool_->join();
        }
    }

    static MemoryStore::Config make_config(capacity_policy policy) {
        MemoryStore::Config cfg;
        cfg.policy = policy;
        cfg.inbound_capacity = 16;
        cfg.outbound_capacity = 16;
        cfg.max_frame_bytes = 256;
        cfg.store_resource = nullptr;
        return cfg;
    }

    // Store one frame on the strand. Returns true on success.
    bool store_one(MemoryStore& store, seqnum_t seq, std::span<const std::byte> frame,
                   direction_t dir = direction_t::outbound) {
        auto fut = asio::co_spawn(
            strand_exec_,
            [&store, seq, frame, dir]() mutable -> asio::awaitable<bool> {
                auto r = co_await store.store(seq, frame, dir);
                co_return r.has_value();
            },
            asio::use_future);
        if (fut.wait_for(std::chrono::seconds(10)) != std::future_status::ready) {
            return false;
        }
        return fut.get();
    }

    // Run retrieve(1,2,outbound) with a reset-driving visitor. Defined below the
    // visitor class (see run_mid_traversal_reset_impl).
    void run_mid_traversal_reset(capacity_policy policy);

    std::unique_ptr<asio::thread_pool> pool_;
    asio::any_io_executor strand_exec_;
};

// ── Visitor: drives reset()+store()+store() from inside the FIRST on_frame() ───
//
// On the FIRST on_frame() call it records the bytes it was handed, then
// co_awaits reset() followed by two store()s that re-fill slots 0 and 1 with
// NEW content on the SAME strand. Because retrieve() released the writer mutex
// before the walk (I-03), these acquire the mutex freely. The reset() bump is
// what the generation re-check must observe on the SECOND iteration.
class ResetDrivingVisitor final : public retrieve_visitor {
public:
    ResetDrivingVisitor(MemoryStore* store, std::vector<std::byte> new1,
                        std::vector<std::byte> new2)
        : store_(store), new1_(std::move(new1)), new2_(std::move(new2)) {}

    asio::awaitable<fixpp::core::expected_t<visit_result>> on_frame(
        seqnum_t seq, std::span<const std::byte> payload) noexcept override {
        ++frames_seen;
        // Deep-copy what we were handed for THIS frame (span lifetime bound).
        std::vector<std::byte> seen(payload.begin(), payload.end());
        if (frames_seen == 1) {
            first_seq = seq;
            first_bytes = seen;  // read BEFORE the suspension
            // Drive reset() + two stores during the retrieve() walk suspension.
            auto rr = co_await store_->reset();
            reset_result = rr.has_value() ? 0 : static_cast<int>(rr.error());
            auto s1 = co_await store_->store(
                1, std::span<const std::byte>(new1_), direction_t::outbound);
            auto s2 = co_await store_->store(
                2, std::span<const std::byte>(new2_), direction_t::outbound);
            stores_ok = s1.has_value() && s2.has_value();
            // Read the SAME span AFTER the suspension. This is what witnesses the
            // materialisation: with the fix, `payload` points at retrieve()'s
            // private stable buffer (frame_scratch / unbounded_payload_copy) and
            // still reads the ORIGINAL bytes; WITHOUT materialisation the bounded
            // span aliases the slab, which store(1) above just overwrote → the
            // visitor would read the NEW bytes for its OWN frame. retrieve_visitor
            // is a public interface; a user visitor may legitimately read post-suspend.
            first_bytes_after_suspend.assign(payload.begin(), payload.end());
        } else if (frames_seen == 2) {
            second_bytes = seen;
        }
        co_return visit_result::cont;
    }

    fixpp::core::error abort_error() const noexcept override {
        return fixpp::core::error::store_io_failure;
    }

    int frames_seen = 0;
    int reset_result = -1;  // 0 = success, else error code
    bool stores_ok = false;
    seqnum_t first_seq = 0;
    std::vector<std::byte> first_bytes;                // frame 1 bytes, read pre-suspend
    std::vector<std::byte> first_bytes_after_suspend;  // frame 1 bytes, read post-suspend
    std::vector<std::byte> second_bytes;

private:
    MemoryStore* store_;
    std::vector<std::byte> new1_;
    std::vector<std::byte> new2_;
};

// Run retrieve(1,2,outbound) with the reset-driving visitor and assert the
// discriminating post-conditions. `policy` selects bounded vs unbounded.
void MemoryStoreResetDuringRetrieveTest::run_mid_traversal_reset(capacity_policy policy) {
    auto& t = *this;
    auto store = std::make_shared<MemoryStore>(make_config(policy));

    constexpr std::size_t kLen = 64;
    const auto orig1 = filled_frame(kLen, 0xA1);  // original seq 1
    const auto orig2 = filled_frame(kLen, 0xB2);  // original seq 2
    auto new1 = filled_frame(kLen, 0xC3);         // reset()+store()'d seq 1
    auto new2 = filled_frame(kLen, 0xD4);         // reset()+store()'d seq 2

    ASSERT_TRUE(t.store_one(*store, 1, orig1));
    ASSERT_TRUE(t.store_one(*store, 2, orig2));

    ResetDrivingVisitor visitor{store.get(), new1, new2};

    auto fut = asio::co_spawn(
        t.strand_exec_,
        [&store, &visitor]() mutable -> asio::awaitable<fixpp::core::expected_t<void>> {
            co_return co_await store->retrieve(1, 2, direction_t::outbound, visitor);
        },
        asio::use_future);

    ASSERT_EQ(fut.wait_for(std::chrono::seconds(10)), std::future_status::ready)
        << "retrieve() did not complete within 10s — possible deadlock";
    auto result = fut.get();

    // The mid-traversal reset() itself must have succeeded and re-populated the
    // slots (so the pre-fix path had NEW bytes to mis-replay).
    EXPECT_EQ(visitor.reset_result, 0) << "reset() during walk failed unexpectedly";
    EXPECT_TRUE(visitor.stores_ok) << "post-reset store()s failed unexpectedly";

    // Frame 1 was visited before the reset — it must carry the ORIGINAL bytes.
    ASSERT_GE(visitor.frames_seen, 1);
    EXPECT_EQ(visitor.first_seq, 1u);
    EXPECT_EQ(visitor.first_bytes, orig1)
        << "frame 1 (visited before reset) must carry its original bytes";

    // Materialisation witness: the span handed to on_frame must stay stable across
    // the visitor's OWN suspension. Post-fix the bounded path copies into a private
    // scratch buffer (like unbounded/FileStore), so a store(1) during the suspension
    // cannot corrupt frame 1's view. Pre-fix the bounded span aliased the slab that
    // store(1) overwrote, so this read would return the NEW bytes.
    EXPECT_EQ(visitor.first_bytes_after_suspend, orig1)
        << "frame 1's span must remain the ORIGINAL bytes when read after the "
           "visitor's own suspension (materialisation into a stable buffer)";

    // ── The discriminating post-conditions ──────────────────────────────────
    // Post-fix: the generation re-check fires before the 2nd frame is read.
    ASSERT_FALSE(result.has_value())
        << "retrieve() must fail closed when reset() runs mid-walk (got success — "
           "pre-fix behaviour: the 2nd frame was replayed with the NEW store's "
           "bytes under the OLD seqnum)";
    EXPECT_EQ(result.error(), fixpp::core::error::store_io_failure)
        << "Expected store_io_failure from the generation re-check, got: "
        << static_cast<int>(result.error());
    EXPECT_EQ(visitor.frames_seen, 1)
        << "the guard must fire before the 2nd frame is visited (got frames_seen="
        << visitor.frames_seen
        << " — pre-fix: 2, i.e. the corrupted 2nd frame WAS handed to the visitor)";

    // Make the pre-fix corruption explicit: if the buggy path DID visit frame 2,
    // it handed over new2 (0xD4) under seqnum 2 rather than the original 0xB2.
    if (visitor.frames_seen >= 2) {
        EXPECT_NE(visitor.second_bytes, new2)
            << "SILENT CORRUPTION: frame 2 was replayed with the reset()+store()'d "
               "NEW bytes under the OLD seqnum";
    }
}

// ── Cell 1: bounded policy (the finding's target) ────────────────────────────
TEST_F(MemoryStoreResetDuringRetrieveTest, Bounded_MidWalkReset_FailsClosed) {
    run_mid_traversal_reset(capacity_policy::bounded);
}

// ── Cell 2: unbounded policy (shared generation guard covers it too) ──────────
TEST_F(MemoryStoreResetDuringRetrieveTest, Unbounded_MidWalkReset_FailsClosed) {
    run_mid_traversal_reset(capacity_policy::unbounded);
}

}  // namespace
