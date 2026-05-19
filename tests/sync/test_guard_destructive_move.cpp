// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/sync/test_guard_destructive_move.cpp — Seam #20
//
// async_lock_guard destructive move-assign:
//  - Moving into an engaged guard unlocks the previously-owned mutex first.
//  - Self-assignment is a no-op.
//  - uses the async_mutex_awaiter friend ctor for the engaged guard.
//
// Oracle: [2f §9 #20] — "async_lock_guard destructive move-assign".
//         [2f §4.4] — RC#1 / N-P1-3 destructive move semantics.

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>

#include <fixpp/core/sync/async_mutex.hpp>

namespace {

using fixpp::sync::async_mutex;
using fixpp::sync::async_lock_guard;

TEST(SeamGuardDestructiveMove, DefaultConstructedGuardIsDisengaged) {
    async_lock_guard g;
    EXPECT_FALSE(g.owns_lock());
    EXPECT_EQ(g.release(), nullptr);
}

TEST(SeamGuardDestructiveMove, MoveConstructFromEngagedDisengagesSource) {
    // Acquire, then move-construct a second guard from the first.
    asio::io_context ioc;
    bool second_owns = false;
    bool first_owns_after = false;

    auto f = asio::co_spawn(ioc, [&]() -> asio::awaitable<void> {
        async_mutex mtx;
        auto r = co_await mtx.async_lock();
        EXPECT_TRUE(r.has_value());
        if (!r.has_value()) co_return;
        async_lock_guard g1 = std::move(*r);
        EXPECT_TRUE(g1.owns_lock());

        async_lock_guard g2 = std::move(g1);
        second_owns = g2.owns_lock();
        first_owns_after = g1.owns_lock();
        // g2 destructor releases the lock
    }, asio::use_future);
    ioc.run();
    f.get();

    EXPECT_TRUE(second_owns);
    EXPECT_FALSE(first_owns_after);
}

TEST(SeamGuardDestructiveMove, MoveAssignToEngagedUnlocksFirst) {
    // Moving into an already-engaged guard must unlock the previously-held
    // mutex FIRST before taking ownership of the new one.
    asio::io_context ioc;
    int unlock_sequence = 0;

    auto f = asio::co_spawn(ioc, [&]() -> asio::awaitable<void> {
        async_mutex mtx1;
        async_mutex mtx2;

        auto r1 = co_await mtx1.async_lock();
        EXPECT_TRUE(r1.has_value());
        if (!r1.has_value()) co_return;
        async_lock_guard g1 = std::move(*r1);
        EXPECT_TRUE(g1.owns_lock());

        auto r2 = co_await mtx2.async_lock();
        EXPECT_TRUE(r2.has_value());
        if (!r2.has_value()) co_return;
        async_lock_guard g2 = std::move(*r2);
        EXPECT_TRUE(g2.owns_lock());

        // Destructive move: g1 = std::move(g2) should:
        //  1. Unlock mtx1 (previously owned by g1).
        //  2. Take ownership of mtx2 (previously owned by g2).
        g1 = std::move(g2);

        EXPECT_TRUE(g1.owns_lock());   // now owns mtx2
        EXPECT_FALSE(g2.owns_lock()); // g2 disengaged

        // mtx1 must now be free (acquirable again).
        auto r_retry = co_await mtx1.async_lock();
        EXPECT_TRUE(r_retry.has_value())
            << "mtx1 must have been released by destructive move-assign";
        if (r_retry.has_value()) ++unlock_sequence;
    }, asio::use_future);
    ioc.run();
    f.get();

    EXPECT_EQ(unlock_sequence, 1);
}

TEST(SeamGuardDestructiveMove, SelfAssignmentIsNoOp) {
    asio::io_context ioc;
    bool still_owns = false;

    auto f = asio::co_spawn(ioc, [&]() -> asio::awaitable<void> {
        async_mutex mtx;
        auto r = co_await mtx.async_lock();
        EXPECT_TRUE(r.has_value());
        if (!r.has_value()) co_return;
        async_lock_guard g = std::move(*r);

        // Self-assignment must be a no-op (not unlock + re-engage).
        auto& ref = g;
        g = std::move(ref);  // self-move

        still_owns = g.owns_lock();
    }, asio::use_future);
    ioc.run();
    f.get();

    EXPECT_TRUE(still_owns);
}

TEST(SeamGuardDestructiveMove, ReleaseDisengagesGuard) {
    asio::io_context ioc;

    auto f = asio::co_spawn(ioc, [&]() -> asio::awaitable<void> {
        async_mutex mtx;
        auto r = co_await mtx.async_lock();
        EXPECT_TRUE(r.has_value());
        if (!r.has_value()) co_return;
        async_lock_guard g = std::move(*r);
        EXPECT_TRUE(g.owns_lock());

        auto* mx = g.release();
        EXPECT_NE(mx, nullptr);
        EXPECT_FALSE(g.owns_lock());

        // Manually unlock so destructor check passes.
        mx->unlock();
    }, asio::use_future);
    ioc.run();
    f.get();
}

}  // namespace
