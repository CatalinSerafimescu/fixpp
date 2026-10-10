// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_544_lock_cells.cpp
//
// fixpp#544 (B35; `.specify/544-hot-path-zero-alloc.md` §2.3, and §3 "Behaviour"): the
// cancellation cells for FIXPP_DETAIL_CO_AWAIT_LOCK (include/fixpp/core/sync/async_mutex.hpp)
// at the sites that use it. Each cell names the edit to the macro it exists to catch; a
// mutant of the macro that makes that edit turns the cell RED.
//
//   PreInitiationCancel_…      part 1 deleted (the cancelled() conjunct of the pre-check);
//   ThrowIfCancelledFalse_…    part 1's throw_if_cancelled() read dropped;
//   SeamSeventeen_…            part 4 deleted;
//   NonTerminalEntryFilter_…   part 4 restoring the caller's entry filter.
//
// E-5's drain cell is tests/sync/test_544_lock_macro_drain.cpp: it reads the waiter pool
// through FIXPP_ASYNC_MUTEX_TEST_SEAM, which a TU linking fixpp_session must not define.
//
// Every coroutine here is a named function taking references to the test body's locals,
// which outlive the io_context run that drives it.

#include <gtest/gtest.h>

#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>
#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/cancellation_state.hpp>
#include <asio/cancellation_type.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/error.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/system_error.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <cstddef>
#include <fixpp/core/error.hpp>
#include <fixpp/session/application.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/memory_store.hpp>
#include <fixpp/session/memory_store_factory.hpp>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/seqnum_manager.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "session/plain_engine_rig.hpp"
#include "support/blocking_write_transport.hpp"
#include "support/session_test_access.hpp"

namespace {

using fixpp::core::error;
using fixpp::session::direction_t;
using fixpp::session::MemoryStore;
using fixpp::session::seqnum_t;
using fixpp::session::SeqnumManager;

constexpr std::string_view kFrameText =
    "8=FIX.4.2\x01"
    "9=5\x01"
    "35=0\x01"
    "10=000\x01";

std::span<const std::byte> frame_bytes() {
    return std::as_bytes(std::span<const char>{kFrameText.data(), kFrameText.size()});
}

MemoryStore::Config bounded_store_config() {
    MemoryStore::Config cfg;
    cfg.policy = fixpp::session::capacity_policy::bounded;
    cfg.inbound_capacity = 0;
    cfg.outbound_capacity = 8;
    cfg.max_frame_bytes = 256;
    cfg.store_resource = std::pmr::new_delete_resource();
    return cfg;
}

// Queues a terminal emission on `sig`. Posted before a converted site's own leading post,
// it runs while that post is suspended: the strand runs its handlers in order.
void post_terminal_emission(asio::any_io_executor const& ex, asio::cancellation_signal& sig) {
    asio::post(ex, [&sig] { sig.emit(asio::cancellation_type::terminal); });
}

// ── Pre-initiation cancellation (§2.3, "Why part 1 exists"; §3, O-2) ──────────────
//
// A terminal cancellation on the session's cancel slot lands while MemoryStore::store is
// suspended in its leading post. The macro's part 1 throws operation_aborted, and
// store_then_emit's `catch (const asio::system_error&)` returns dispatch_aborted, with
// nothing stored and nothing transmitted. Without part 1, part 2 erases the cancellation:
// the store commits and the frame is written. The cell runs with throw_if_cancelled()
// left at its default, true, so it witnesses part 1's cancelled() conjunct.

struct pre_initiation_outcome {
    bool done = false;
    bool memory_store = false;
    std::optional<seqnum_t> next_before;
    std::optional<seqnum_t> next_after;
    std::optional<fixpp::core::expected_t<void>> result;
};

asio::awaitable<void> cancel_while_store_posts(fixpp::session::Session& s,
                                               asio::cancellation_signal& sig,
                                               pre_initiation_outcome& out) {
    auto* store = fixpp::session::session_test_access::store(s);
    out.memory_store = dynamic_cast<MemoryStore*>(store) != nullptr;
    if (store == nullptr) {
        out.done = true;
        co_return;
    }
    auto before = co_await store->next_seqnum(direction_t::outbound, false);
    if (before) out.next_before = *before;
    post_terminal_emission(co_await asio::this_coro::executor, sig);
    out.result = co_await fixpp::session::session_test_access::store_then_emit(
        s, out.next_before.value_or(1), frame_bytes());
    // The cancellation is still recorded in this coroutine's state; clear it, so the read
    // below runs.
    co_await asio::this_coro::reset_cancellation_state();
    auto after = co_await store->next_seqnum(direction_t::outbound, false);
    if (after) out.next_after = *after;
    out.done = true;
}

TEST(B35LockCells, PreInitiationCancel_StoreThenEmitReturnsDispatchAbortedAndTransmitsNothing) {
    namespace pr = fixpp::test_support::plain_rig;
    pr::Rig rig{std::make_shared<fixpp::session::Application>()};
    auto stream = std::make_shared<fixpp::session::test::ScriptedStream>();
    stream->hold_open = true;
    auto write = std::make_shared<fixpp::session::test::blocking_write_state>();
    auto cfg = rig.cfg(fixpp::session::session_role::initiator);
    cfg.transport_factory_override =
        std::make_shared<fixpp::session::test::BlockingWriteTransportFactory>(stream, write);
    MemoryStore::Config store_cfg;
    store_cfg.inbound_capacity = 64;
    store_cfg.outbound_capacity = 64;
    store_cfg.max_frame_bytes = 1024;
    cfg.store_factory = std::make_shared<fixpp::session::MemoryStoreFactory>(store_cfg);

    // Nothing fatal between start() and stop(): ~Engine requires a completed stop().
    bool up = rig.start(std::move(cfg)) && rig.run_until([&] {
        return rig.state() == fixpp::session::fsm_state::LogonSent && stream->waiting != nullptr;
    });
    if (up) {
        stream->push(pr::to_bytes(rig.logon()));
        up = rig.run_until([&] { return rig.state() == fixpp::session::fsm_state::Active; });
    }
    pre_initiation_outcome out;
    // At function scope: the frame's bound slot points into `sig`, and `rig.stop()` below can
    // resume or cancel that frame, so `sig` must outlive it.
    asio::cancellation_signal sig;
    std::size_t writes_before = 0;
    std::size_t writes_after = 0;
    bool finished = false;
    auto const sess = rig.session();
    if (up && sess) {
        writes_before = write->writes;
        asio::co_spawn(sess->executor().underlying(), cancel_while_store_posts(*sess, sig, out),
                       asio::bind_cancellation_slot(sig.slot(), asio::detached));
        finished = rig.run_until([&] { return out.done; });
        writes_after = write->writes;
    }
    rig.stop();

    ASSERT_TRUE(up) << "the initiator did not reach Active";
    ASSERT_TRUE(finished) << "the cell's coroutine did not finish";
    EXPECT_TRUE(out.memory_store) << "the session's store is not a MemoryStore";
    ASSERT_TRUE(out.result.has_value());
    // NOLINTBEGIN(bugprone-unchecked-optional-access): each follows its ASSERT_TRUE(has_value())
    ASSERT_FALSE(out.result->has_value())
        << "store_then_emit succeeded: the cancellation that landed in store()'s leading "
           "post was erased before the lock";
    EXPECT_EQ(out.result->error(), error::dispatch_aborted);
    EXPECT_EQ(writes_after, writes_before) << "a frame was transmitted";
    ASSERT_TRUE(out.next_before.has_value());
    ASSERT_TRUE(out.next_after.has_value());
    EXPECT_EQ(*out.next_after, *out.next_before) << "the store committed the frame";
    // NOLINTEND(bugprone-unchecked-optional-access)
}

// ── throw_if_cancelled(false) (§3, C2-4 window 1) ────────────────────────────────
//
// A coroutine that sets throw_if_cancelled(false) calls MemoryStore::store, and a terminal
// cancellation lands in store()'s leading post. store() completes as a co_await of
// async_lock() does at that point: no exception, and the record stored. The default arm,
// throw_if_cancelled() true, is the positive control: the same cancellation, in the same
// place, makes store() throw, so the emission did land during the post.

struct store_outcome {
    bool done = false;
    bool threw_aborted = false;
    bool stored = false;
    std::optional<seqnum_t> next_after;
};

asio::awaitable<void> store_under_cancellation(MemoryStore& store, bool throw_if_cancelled,
                                               asio::cancellation_signal& sig, store_outcome& out) {
    co_await asio::this_coro::throw_if_cancelled(throw_if_cancelled);
    post_terminal_emission(co_await asio::this_coro::executor, sig);
    try {
        auto r = co_await store.store(1, frame_bytes(), direction_t::outbound);
        out.stored = r.has_value();
    } catch (asio::system_error const& e) {
        out.threw_aborted = e.code() == asio::error::operation_aborted;
    }
    co_await asio::this_coro::reset_cancellation_state();
    auto n = co_await store.next_seqnum(direction_t::outbound, false);
    if (n) out.next_after = *n;
    out.done = true;
}

store_outcome run_store_under_cancellation(bool throw_if_cancelled) {
    asio::io_context ioc;
    MemoryStore store{bounded_store_config()};
    asio::cancellation_signal sig;
    store_outcome out;
    asio::co_spawn(ioc, store_under_cancellation(store, throw_if_cancelled, sig, out),
                   asio::bind_cancellation_slot(sig.slot(), asio::detached));
    ioc.run();
    return out;
}

TEST(B35LockCells, ThrowIfCancelledFalse_StoreCompletesAndRecordsTheFrame) {
    auto const off = run_store_under_cancellation(false);
    ASSERT_TRUE(off.done);
    EXPECT_FALSE(off.threw_aborted)
        << "store() threw with throw_if_cancelled(false): the pre-check ignored the flag";
    EXPECT_TRUE(off.stored);
    ASSERT_TRUE(off.next_after.has_value());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access): ASSERT_TRUE above
    EXPECT_EQ(*off.next_after, static_cast<seqnum_t>(2)) << "the record was not stored";

    auto const on = run_store_under_cancellation(true);
    ASSERT_TRUE(on.done);
    EXPECT_TRUE(on.threw_aborted)
        << "control: with throw_if_cancelled() true the cancellation must abort store()";
    EXPECT_FALSE(on.stored);
    ASSERT_TRUE(on.next_after.has_value());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access): ASSERT_TRUE above
    EXPECT_EQ(*on.next_after, static_cast<seqnum_t>(1));
}

// ── After the grant: the filter the macro leaves (§3; seam #17 and the entry filter) ──
//
// Each converted site runs its lock and returns. Then the coroutine emits on its own slot
// and awaits once more. A total emission must not abort that await, because part 4 leaves
// the filter terminal-only; a terminal emission still does, which is the positive control
// that the emission reaches the coroutine's cancellation state.

enum class site { check_inbound, memory_store };

struct after_grant_outcome {
    bool done = false;
    bool site_ok = false;
    bool next_await_aborted = false;
};

asio::awaitable<void> lock_then_emit(site where, bool enter_with_total, bool call_site,
                                     asio::cancellation_type emitted,
                                     asio::cancellation_signal& sig, after_grant_outcome& out) {
    SeqnumManager mgr;
    MemoryStore store{bounded_store_config()};
    if (enter_with_total) {
        co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation{});
    }
    if (!call_site) {
        out.site_ok = true;
    } else if (where == site::check_inbound) {
        auto r = co_await mgr.check_inbound(1);
        out.site_ok = r.has_value() && mgr.next_inbound_unsafe() == 2;
    } else {
        auto r = co_await store.store(1, frame_bytes(), direction_t::outbound);
        out.site_ok = r.has_value();
    }
    sig.emit(emitted);
    try {
        co_await asio::post(co_await asio::this_coro::executor, asio::use_awaitable);
    } catch (asio::system_error const& e) {
        out.next_await_aborted = e.code() == asio::error::operation_aborted;
    }
    out.done = true;
}

after_grant_outcome run_lock_then_emit(site where, bool enter_with_total, bool call_site,
                                       asio::cancellation_type emitted) {
    asio::io_context ioc;
    asio::cancellation_signal sig;
    after_grant_outcome out;
    asio::co_spawn(ioc, lock_then_emit(where, enter_with_total, call_site, emitted, sig, out),
                   asio::bind_cancellation_slot(sig.slot(), asio::detached));
    ioc.run();
    return out;
}

char const* site_name(site where) {
    return where == site::check_inbound ? "SeqnumManager::check_inbound" : "MemoryStore::store";
}

// Seam #17, LateSignal/GrantOrCancel: the caller enters with the default terminal filter.
TEST(B35LockCells, SeamSeventeen_TotalAfterGrantDoesNotAbortTheNextAwait) {
    for (auto const where : {site::check_inbound, site::memory_store}) {
        SCOPED_TRACE(site_name(where));
        auto const total = run_lock_then_emit(where, false, true, asio::cancellation_type::total);
        ASSERT_TRUE(total.done);
        EXPECT_TRUE(total.site_ok);
        EXPECT_FALSE(total.next_await_aborted)
            << "a total emission after the grant aborted the caller's next co_await";

        auto const terminal =
            run_lock_then_emit(where, false, true, asio::cancellation_type::terminal);
        ASSERT_TRUE(terminal.done);
        EXPECT_TRUE(terminal.site_ok);
        EXPECT_TRUE(terminal.next_await_aborted)
            << "control: a terminal emission after the grant must abort the next co_await";
    }
}

// The caller enters with a total filter; the macro still leaves it terminal-only, as
// async_lock() does. The control arm skips the site: there the entry filter is still in
// force, and the same total emission aborts the next await.
TEST(B35LockCells, NonTerminalEntryFilter_ConvertedSiteLeavesTheCallerTerminalOnly) {
    for (auto const where : {site::check_inbound, site::memory_store}) {
        SCOPED_TRACE(site_name(where));
        auto const locked = run_lock_then_emit(where, true, true, asio::cancellation_type::total);
        ASSERT_TRUE(locked.done);
        EXPECT_TRUE(locked.site_ok);
        EXPECT_FALSE(locked.next_await_aborted)
            << "the caller's total entry filter survived the lock";

        auto const control = run_lock_then_emit(where, true, false, asio::cancellation_type::total);
        ASSERT_TRUE(control.done);
        EXPECT_TRUE(control.next_await_aborted)
            << "control: without the lock, the total entry filter must admit a total emission";
    }
}

}  // namespace
