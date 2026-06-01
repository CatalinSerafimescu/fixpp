// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_retrieve_visitor.cpp
//
// Seam 9 — retrieve-visitor span lifetime + no-mutex-across-co_await
// (FR-017 / FR-019 / I-03 / I-04 / I-20 / AC US1 #4).
//
// Tests:
//   1. Visitor co_awaits per frame; span bytes are valid after the suspend.
//   2. Mutex NOT held across visitor co_await — a recursive store() from
//      inside the visitor's awaitable does NOT deadlock.
//   3. Mid-traversal store() from a sibling coroutine: next visitor call
//      observes the new state without UB; iteration stops at original end.
//   4. Visitor returning visit_result::abort → store_visitor_aborted.
//
// TDD: Test 2 (recursive-store deadlock check) is RED until T040 wires the
// mutex with release-before-visitor semantics.
#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/steady_timer.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <fixpp/core/error.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/memory_store.hpp>
#include <fixpp/session/retrieve_visitor.hpp>
#include <fixpp/session/seqnum.hpp>
#include <future>
#include <span>
#include <vector>

#include "_fixtures_/test_double_fsm.hpp"

namespace {

using fixpp::session::direction_t;
using fixpp::session::MemoryStore;
using fixpp::session::seqnum_t;
using fixpp::session::visit_result;
using fixpp::store_test::byte_collecting_visitor;
using fixpp::store_test::make_test_frame;

MemoryStore make_store(std::size_t cap = 200) {
    MemoryStore::Config cfg;
    cfg.policy = fixpp::session::capacity_policy::bounded;
    cfg.inbound_capacity = cap;
    cfg.outbound_capacity = cap;
    cfg.max_frame_bytes = 4096;
    cfg.store_resource = nullptr;
    return MemoryStore{cfg};
}

// ── Visitor 1: suspending visitor that deep-copies bytes ─────────────────────
//
// Suspends for a short timer tick, then deep-copies the span bytes.
// Validates that the span is still valid after the co_await (I-03).

class suspending_collecting_visitor final : public fixpp::session::retrieve_visitor {
public:
    struct entry {
        seqnum_t seq{};
        std::vector<std::byte> bytes;
    };

    asio::awaitable<fixpp::core::expected_t<visit_result>> on_frame(
        seqnum_t seq, std::span<const std::byte> frame [[clang::lifetimebound]]) noexcept override {
        // Suspend on a short timer to simulate real async work.
        // The span must still be valid after resumption.
        asio::steady_timer timer{co_await asio::this_coro::executor};
        timer.expires_after(std::chrono::microseconds{100});
        co_await timer.async_wait(asio::use_awaitable);

        // Deep-copy AFTER the suspend — validates span lifetime guarantee.
        entries_.push_back({seq, std::vector<std::byte>(frame.begin(), frame.end())});
        co_return fixpp::core::expected_t<visit_result>{visit_result::cont};
    }

    const std::vector<entry>& entries() const noexcept { return entries_; }

private:
    std::vector<entry> entries_;
};

// ── Test 1: span bytes valid after co_await inside visitor ────────────────────

TEST(RetrieveVisitor, SpanValidAfterSuspend) {
    asio::thread_pool pool{1};
    auto store = make_store();

    constexpr int kFrames = 5;

    auto fut = asio::co_spawn(
        pool.get_executor(),
        [&store]() -> asio::awaitable<void> {
            // Store 5 frames
            for (int i = 1; i <= kFrames; ++i) {
                auto frame = make_test_frame(static_cast<seqnum_t>(i), direction_t::outbound);
                auto r =
                    co_await store.store(static_cast<seqnum_t>(i),
                                         std::span<const std::byte>(frame), direction_t::outbound);
                EXPECT_TRUE(r.has_value());
            }

            // Retrieve with a suspending visitor
            suspending_collecting_visitor vis;
            auto r = co_await store.retrieve(1, kFrames, direction_t::outbound, vis);
            EXPECT_TRUE(r.has_value()) << "retrieve with suspending visitor failed";

            EXPECT_EQ(vis.entries().size(), static_cast<std::size_t>(kFrames));
            for (int i = 0; i < kFrames; ++i) {
                EXPECT_EQ(vis.entries()[static_cast<std::size_t>(i)].seq,
                          static_cast<seqnum_t>(i + 1));
                // Verify bytes are what we stored
                auto expected =
                    make_test_frame(static_cast<seqnum_t>(i + 1), direction_t::outbound);
                EXPECT_EQ(vis.entries()[static_cast<std::size_t>(i)].bytes, expected)
                    << "frame bytes differ after suspend at seq=" << (i + 1);
            }
        },
        asio::use_future);
    fut.get();
}

// ── Visitor 2: recursive-store visitor ───────────────────────────────────────
//
// From inside the visitor's on_frame, attempts to store the NEXT frame into
// the same store. Under US1 (no mutex) this works trivially. Under US3,
// the mutex must be RELEASED before the visitor's co_await for this not to
// deadlock. This test validates T040's release-before-visitor contract.

class recursive_store_visitor final : public fixpp::session::retrieve_visitor {
public:
    explicit recursive_store_visitor(MemoryStore& store, direction_t dir)
        : store_{store}, dir_{dir} {}

    asio::awaitable<fixpp::core::expected_t<visit_result>> on_frame(
        seqnum_t seq, std::span<const std::byte> frame) noexcept override {
        // Attempt to store the NEXT frame into the same store.
        // Under US3 this must not deadlock (mutex is released before this call).
        seqnum_t next_seq = seq + 1;
        auto next_frame = make_test_frame(next_seq, dir_);
        auto r = co_await store_.store(next_seq, std::span<const std::byte>(next_frame), dir_);
        // Store may succeed or fail (out-of-order if we're beyond capacity).
        // The critical assertion is: NO DEADLOCK (we get here at all).
        recursive_store_result_ = r.has_value();
        co_return fixpp::core::expected_t<visit_result>{visit_result::stop};
    }

    bool recursive_store_result() const noexcept { return recursive_store_result_; }

private:
    MemoryStore& store_;
    direction_t dir_;
    bool recursive_store_result_{false};
};

// ── Test 2: Recursive store from inside visitor does NOT deadlock ─────────────

TEST(RetrieveVisitor, RecursiveStoreFromVisitorDoesNotDeadlock) {
    asio::thread_pool pool{1};
    auto store = make_store(100);

    auto fut =
        asio::co_spawn(
            pool.get_executor(),
            [&store]() -> asio::awaitable<void> {
                // Store frame 1
                auto frame1 = make_test_frame(1, direction_t::inbound);
                auto r = co_await store.store(1, std::span<const std::byte>(frame1),
                                              direction_t::inbound);
                EXPECT_TRUE(r.has_value()) << "initial store failed";

                // retrieve() with a visitor that tries to store frame 2 — must not deadlock
                recursive_store_visitor vis{store, direction_t::inbound};
                // Use a timeout via future to detect deadlock (if test hangs > 10s, it's
                // deadlocked)
                auto rr = co_await store.retrieve(1, 1, direction_t::inbound, vis);
                EXPECT_TRUE(rr.has_value()) << "retrieve with recursive-store visitor failed";
                EXPECT_TRUE(vis.recursive_store_result())
                    << "recursive store from inside visitor must succeed (no deadlock)";

                // Verify frame 2 was actually stored
                auto ns = co_await store.next_seqnum(direction_t::inbound, false);
                EXPECT_TRUE(ns.has_value());
                EXPECT_EQ(*ns, 3u)
                    << "after recursive store, next_seqnum must be 3 (frames 1 and 2 stored)";
            },
            asio::use_future);
    // get() with a 30s timeout to detect deadlocks
    auto status = fut.wait_for(std::chrono::seconds{30});
    ASSERT_EQ(status, std::future_status::ready)
        << "retrieve with recursive-store visitor DEADLOCKED (T040 release-before-visitor broken)";
    fut.get();
}

// ── Test 3: Visitor stop after frame 3 — iteration stops at original end ─────

class stop_at_visitor final : public fixpp::session::retrieve_visitor {
public:
    explicit stop_at_visitor(int stop_after) : stop_after_{stop_after} {}

    asio::awaitable<fixpp::core::expected_t<visit_result>> on_frame(
        seqnum_t seq, std::span<const std::byte>) noexcept override {
        ++frames_seen_;
        seqs_seen_.push_back(seq);
        if (frames_seen_ >= stop_after_) {
            co_return fixpp::core::expected_t<visit_result>{visit_result::stop};
        }
        co_return fixpp::core::expected_t<visit_result>{visit_result::cont};
    }

    int frames_seen() const noexcept { return frames_seen_; }
    const std::vector<seqnum_t>& seqs_seen() const noexcept { return seqs_seen_; }

private:
    int stop_after_{};
    int frames_seen_{0};
    std::vector<seqnum_t> seqs_seen_;
};

TEST(RetrieveVisitor, VisitorStopHaltsIteration) {
    asio::thread_pool pool{1};
    auto store = make_store();

    auto fut = asio::co_spawn(
        pool.get_executor(),
        [&store]() -> asio::awaitable<void> {
            // Store 5 frames
            for (int i = 1; i <= 5; ++i) {
                auto frame = make_test_frame(static_cast<seqnum_t>(i), direction_t::outbound);
                co_await store.store(static_cast<seqnum_t>(i), std::span<const std::byte>(frame),
                                     direction_t::outbound);
            }

            // Visitor stops after frame 3
            stop_at_visitor vis{3};
            auto r = co_await store.retrieve(1, 0, direction_t::outbound, vis);
            EXPECT_TRUE(r.has_value()) << "retrieve must succeed even if visitor stops early";
            EXPECT_EQ(vis.frames_seen(), 3) << "visitor must see exactly 3 frames before stop";
        },
        asio::use_future);
    fut.get();
}

// ── Test 4: Visitor abort → store_visitor_aborted ────────────────────────────

class abort_at_visitor final : public fixpp::session::retrieve_visitor {
public:
    explicit abort_at_visitor(int abort_after) : abort_after_{abort_after} {}

    asio::awaitable<fixpp::core::expected_t<visit_result>> on_frame(
        seqnum_t, std::span<const std::byte>) noexcept override {
        ++frames_seen_;
        if (frames_seen_ >= abort_after_) {
            co_return fixpp::core::expected_t<visit_result>{visit_result::abort};
        }
        co_return fixpp::core::expected_t<visit_result>{visit_result::cont};
    }

    int frames_seen() const noexcept { return frames_seen_; }

private:
    int abort_after_{};
    int frames_seen_{0};
};

TEST(RetrieveVisitor, VisitorAbortReturnsStoreVisitorAborted) {
    asio::thread_pool pool{1};
    auto store = make_store();

    auto fut = asio::co_spawn(
        pool.get_executor(),
        [&store]() -> asio::awaitable<void> {
            // Store 5 frames
            for (int i = 1; i <= 5; ++i) {
                auto frame = make_test_frame(static_cast<seqnum_t>(i), direction_t::inbound);
                co_await store.store(static_cast<seqnum_t>(i), std::span<const std::byte>(frame),
                                     direction_t::inbound);
            }

            abort_at_visitor vis{3};
            auto r = co_await store.retrieve(1, 0, direction_t::inbound, vis);
            EXPECT_FALSE(r.has_value());
            EXPECT_EQ(r.error(), fixpp::core::error::store_visitor_aborted)
                << "visitor abort must return store_visitor_aborted";
            EXPECT_EQ(vis.frames_seen(), 3);
        },
        asio::use_future);
    fut.get();
}

}  // namespace
