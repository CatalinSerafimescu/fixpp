// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_store_pmr_poison_retrieve.cpp
//
// Seam 16 — PMR poison on retrieve recovery path (I-21 / [2a §4.2]).
//
// A visitor's caller-supplied memory_resource throws on allocation during the
// recovery path; the throw routes through fixpp::core::detail::trap_throw
// (no terminate); the awaitable completes with
// expected_t::unexpected{store_visitor_aborted}.
//
// Implementation note: trap_throw catches std::bad_alloc and maps it to
// error::out_of_memory, NOT store_visitor_aborted. The visitor boundary
// in retrieve() must catch any exception from the visitor's on_frame call
// (which may throw due to PMR allocation failure inside the visitor) and
// return store_visitor_aborted. T043 wires this catch block.
//
// TDD: RED until T043 (trap_throw wiring on retrieve boundary) ships.
#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_future.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/memory_store.hpp>
#include <fixpp/session/retrieve_visitor.hpp>
#include <fixpp/session/seqnum.hpp>
#include <memory_resource>
#include <new>
#include <span>
#include <vector>

#include "_fixtures_/test_double_fsm.hpp"

namespace {

using fixpp::session::direction_t;
using fixpp::session::MemoryStore;
using fixpp::session::seqnum_t;
using fixpp::session::visit_result;
using fixpp::store_test::make_test_frame;

MemoryStore make_store() {
    MemoryStore::Config cfg;
    cfg.policy = fixpp::session::capacity_policy::bounded;
    cfg.inbound_capacity = 100;
    cfg.outbound_capacity = 100;
    cfg.max_frame_bytes = 4096;
    cfg.store_resource = nullptr;
    return MemoryStore{cfg};
}

// ── Poison resource ───────────────────────────────────────────────────────────
//
// A PMR resource that always throws std::bad_alloc on allocate.

class poison_memory_resource final : public std::pmr::memory_resource {
protected:
    void* do_allocate(std::size_t, std::size_t) override { throw std::bad_alloc{}; }

    void do_deallocate(void*, std::size_t, std::size_t) noexcept override {}

    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
        return this == &other;
    }
};

// ── PMR-throwing visitor ──────────────────────────────────────────────────────
//
// A visitor that allocates from a poisoned resource on the first call,
// causing std::bad_alloc to propagate from on_frame.
//
// Note: The visitor itself is NOT noexcept (the on_frame signature is noexcept
// but we want the PMR throw to escape from inside the coroutine's operator()
// and be caught by retrieve's try/catch at the boundary per T043).

class pmr_poisoned_visitor final : public fixpp::session::retrieve_visitor {
public:
    explicit pmr_poisoned_visitor(std::pmr::memory_resource* mr) : mr_{mr} {}

    asio::awaitable<fixpp::core::expected_t<visit_result>> on_frame(
        seqnum_t, std::span<const std::byte>) noexcept override {
        // Attempt to allocate from the poisoned resource — throws bad_alloc.
        // The noexcept on on_frame means if this throw escapes the coroutine
        // frame it would call terminate(). T043's catch block in retrieve()
        // must catch it BEFORE the coroutine frame boundary.
        //
        // In practice: the PMR allocation happens inside the visitor BEFORE
        // co_return, so the throw propagates up through the coroutine machinery.
        // T043 wraps the visitor call in try/catch(...) at retrieve()'s level.
        try {
            mr_->allocate(1024, 8);  // throws
        } catch (std::bad_alloc const&) {
            // Re-throw to let retrieve()'s T043 catch block handle it.
            // This simulates the visitor code throwing on PMR failure.
            throw;
        }
        // Unreachable
        co_return fixpp::core::expected_t<visit_result>{visit_result::cont};
    }

private:
    std::pmr::memory_resource* mr_;
};

// ── Test 1: PMR poison → store_visitor_aborted (no terminate) ─────────────────

TEST(StorePmrPoisonRetrieve, PmrThrowRoutedThroughTrapThrow) {
    asio::thread_pool pool{1};
    auto store = make_store();

    auto fut = asio::co_spawn(
        pool.get_executor(),
        [&store]() -> asio::awaitable<void> {
            // Store one frame
            auto frame = make_test_frame(1, direction_t::outbound);
            auto sr =
                co_await store.store(1, std::span<const std::byte>(frame), direction_t::outbound);
            EXPECT_TRUE(sr.has_value());

            // Retrieve with a PMR-poisoned visitor
            poison_memory_resource poison;
            pmr_poisoned_visitor vis{&poison};
            auto r = co_await store.retrieve(1, 1, direction_t::outbound, vis);

            // Must NOT terminate — must return store_visitor_aborted
            EXPECT_FALSE(r.has_value()) << "Expected error from PMR poison";
            if (!r.has_value()) {
                // Under T043: the catch block in retrieve maps bad_alloc to
                // store_visitor_aborted. Under US1 (no catch block) the
                // noexcept on on_frame + the throw propagating = terminate().
                // The test being here at all (not terminated) validates T043.
                EXPECT_EQ(r.error(), fixpp::core::error::store_visitor_aborted)
                    << "PMR throw must route to store_visitor_aborted via trap_throw";
            }
        },
        asio::use_future);
    // If T043 is not wired: std::terminate() is called before fut.get() returns.
    // The test framework will report a crash, not a test failure.
    fut.get();
}

// ── Test 2: PMR poison from second frame — first frame was processed ──────────

class pmr_poisoned_on_second_visitor final : public fixpp::session::retrieve_visitor {
public:
    explicit pmr_poisoned_on_second_visitor(std::pmr::memory_resource* mr) : mr_{mr} {}

    asio::awaitable<fixpp::core::expected_t<visit_result>> on_frame(
        seqnum_t, std::span<const std::byte>) noexcept override {
        ++call_count_;
        if (call_count_ >= 2) {
            // Second call: trigger PMR poison
            try {
                mr_->allocate(1024, 8);
            } catch (std::bad_alloc const&) {
                throw;
            }
        }
        co_return fixpp::core::expected_t<visit_result>{visit_result::cont};
    }

    int call_count() const noexcept { return call_count_; }

private:
    std::pmr::memory_resource* mr_;
    int call_count_{0};
};

TEST(StorePmrPoisonRetrieve, PmrPoisonOnSecondFrameReturnsStoreVisitorAborted) {
    asio::thread_pool pool{1};
    auto store = make_store();

    auto fut = asio::co_spawn(
        pool.get_executor(),
        [&store]() -> asio::awaitable<void> {
            // Store 3 frames
            for (int i = 1; i <= 3; ++i) {
                auto frame = make_test_frame(static_cast<seqnum_t>(i), direction_t::inbound);
                co_await store.store(static_cast<seqnum_t>(i), std::span<const std::byte>(frame),
                                     direction_t::inbound);
            }

            poison_memory_resource poison;
            pmr_poisoned_on_second_visitor vis{&poison};
            auto r = co_await store.retrieve(1, 0, direction_t::inbound, vis);

            EXPECT_FALSE(r.has_value());
            if (!r.has_value()) {
                EXPECT_EQ(r.error(), fixpp::core::error::store_visitor_aborted)
                    << "PMR throw on second frame must route to store_visitor_aborted";
            }
            // First frame was processed successfully (call_count >= 1)
            EXPECT_GE(vis.call_count(), 1);
        },
        asio::use_future);
    fut.get();
}

}  // namespace
