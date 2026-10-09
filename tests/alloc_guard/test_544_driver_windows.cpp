// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/alloc_guard/test_544_driver_windows.cpp
//
// fixpp#544 (B35; `.specify/544-hot-path-zero-alloc.md` §3): the driver-form windows that
// need no session, in W-D's harness (tests/support/recycler_driver.hpp):
//
//   - W-D-R: `MemoryStore::store` with a stand-in pending read live, and its bracket;
//   - W-D's (b-L) and (b-S) pairs, planted directly in the driver;
//   - the (c-L), (c-P) and (c-H) single-edit arms at the filled depth, their production
//     twins, and the no-edit replica twins;
//   - W-D-W's stand-in bracket and its real-chain twin. Their nest sizes are N - e - 2 and
//     N - e - 1, and the headroom `static_assert` below requires N - e - 2 >= 2.
//
// W-D itself is `perf_store_alloc_guard`'s steady-state cell, and W-C is
// `session_refresh_on_logon`'s W8 cell; both instantiate the same template.
//
// Each cell is one gtest case and one mallocnesia registration, selected by
// GTEST_FILTER, because `alloc_guard_end()` exits the process on any interception. Every
// cell also asserts its functional post-condition (iteration and success counts), so a
// window that never ran its callee cannot pass. Without the interceptor the markers are
// null and every cell runs functionally.

#include <gtest/gtest.h>

#include <array>
#include <asio/co_spawn.hpp>
#include <asio/deferred.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/steady_timer.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/error.hpp>
#include <fixpp/core/sync/async_mutex.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/memory_store.hpp>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/seqnum_manager.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <memory>
#include <memory_resource>
#include <span>
#include <string_view>

#include "session/plain_engine_rig.hpp"
#include "support/blocking_write_transport.hpp"
#include "support/recycler_driver.hpp"
#include "support/run_thread_engine_rig.hpp"

namespace {

namespace rc = fixpp::test_support::recycler;
using fixpp::session::direction_t;
using fixpp::session::MemoryStore;
using fixpp::session::seqnum_t;

constexpr int kWarm = 20;
constexpr int kMeasured = 1000;

// One opaque frame, stored under every seq: the store copies it and does not parse it.
constexpr std::string_view kFrameText =
    "8=FIX.4.4\x01"
    "9=5\x01"
    "35=0\x01"
    "10=000\x01";

std::span<const std::byte> frame_bytes() {
    return std::as_bytes(std::span<const char>{kFrameText.data(), kFrameText.size()});
}

MemoryStore::Config bounded_store_config(std::size_t capacity) {
    MemoryStore::Config cfg;
    cfg.policy = fixpp::session::capacity_policy::bounded;
    cfg.inbound_capacity = 0;
    cfg.outbound_capacity = capacity;
    cfg.max_frame_bytes = 256;
    cfg.store_resource = std::pmr::new_delete_resource();
    return cfg;
}

// The production `MemoryStore::store`, one seq per call.
struct store_call {
    MemoryStore* store = nullptr;
    seqnum_t next = 1;
    auto operator()() { return store->store(next++, frame_bytes(), direction_t::outbound); }
};

// The production `SeqnumManager::hydrate`.
struct hydrate_call {
    fixpp::session::SeqnumManager* mgr = nullptr;
    auto operator()() const {
        return mgr->hydrate(static_cast<seqnum_t>(5), static_cast<seqnum_t>(7));
    }
};

// ── (c) replicas ─────────────────────────────────────────────────────────────
// The replica condition: a replica awaits the same operations, in the same order, as its
// production callee, except the one reverted edit. Compare with
//   grep -n "co_await" include/fixpp/session/memory_store.hpp src/session/seqnum_manager.cpp
// read inside `MemoryStore::store` and `SeqnumManager::hydrate`.

// (c-L): `store()` with the leading post in its `deferred` form and the lock reverted to
// the public `async_mutex::async_lock`.
struct store_replica_public_lock {
    static asio::awaitable<fixpp::core::expected_t<void>> run(fixpp::sync::async_mutex& m,
                                                              seqnum_t& counter) {
        co_await asio::post(co_await asio::this_coro::executor, asio::deferred);
        auto guard = co_await m.async_lock();
        if (!guard) co_return std::unexpected(fixpp::core::error::store_cancelled);
        ++counter;
        co_return fixpp::core::expected_t<void>{};
    }
};
struct store_replica_call {
    fixpp::sync::async_mutex* m = nullptr;
    seqnum_t* counter = nullptr;
    auto operator()() const { return store_replica_public_lock::run(*m, *counter); }
};

// (c-H): `hydrate()` on the public `async_mutex::async_lock`.
struct hydrate_replica_public_lock {
    static asio::awaitable<fixpp::core::expected_t<void>> run(fixpp::sync::async_mutex& m,
                                                              seqnum_t& in, seqnum_t& out) {
        auto guard = co_await m.async_lock();
        if (!guard) co_return std::unexpected(fixpp::core::error::session_already_closed);
        in = 5;
        out = 7;
        co_return fixpp::core::expected_t<void>{};
    }
};
struct hydrate_replica_call {
    fixpp::sync::async_mutex* m = nullptr;
    seqnum_t* in = nullptr;
    seqnum_t* out = nullptr;
    auto operator()() const { return hydrate_replica_public_lock::run(*m, *in, *out); }
};

// (c-P): `store()` with the lock in its macro form and the leading post reverted to
// `use_awaitable`.
struct store_replica_use_awaitable_post {
    static asio::awaitable<fixpp::core::expected_t<void>> run(fixpp::sync::async_mutex& m,
                                                              seqnum_t& counter) {
        co_await asio::post(co_await asio::this_coro::executor, asio::use_awaitable);
        FIXPP_DETAIL_CO_AWAIT_LOCK(lock_storage, guard, m, nullptr);
        if (!guard) co_return std::unexpected(fixpp::core::error::store_cancelled);
        ++counter;
        co_return fixpp::core::expected_t<void>{};
    }
};
struct store_replica_ua_post_call {
    fixpp::sync::async_mutex* m = nullptr;
    seqnum_t* counter = nullptr;
    auto operator()() const { return store_replica_use_awaitable_post::run(*m, *counter); }
};

// The no-edit replicas: each (c) replica with no edit reverted, so that an allocation in
// the replica's own code cannot satisfy its arm. (c-L)'s and (c-P)'s coincide: both are
// `store()`'s shape with the `deferred` post and the macro.
struct store_replica_no_edit {
    static asio::awaitable<fixpp::core::expected_t<void>> run(fixpp::sync::async_mutex& m,
                                                              seqnum_t& counter) {
        co_await asio::post(co_await asio::this_coro::executor, asio::deferred);
        FIXPP_DETAIL_CO_AWAIT_LOCK(lock_storage, guard, m, nullptr);
        if (!guard) co_return std::unexpected(fixpp::core::error::store_cancelled);
        ++counter;
        co_return fixpp::core::expected_t<void>{};
    }
};
struct store_replica_no_edit_call {
    fixpp::sync::async_mutex* m = nullptr;
    seqnum_t* counter = nullptr;
    auto operator()() const { return store_replica_no_edit::run(*m, *counter); }
};

struct hydrate_replica_no_edit {
    static asio::awaitable<fixpp::core::expected_t<void>> run(fixpp::sync::async_mutex& m,
                                                              seqnum_t& in, seqnum_t& out) {
        FIXPP_DETAIL_CO_AWAIT_LOCK(lock_storage, guard, m, nullptr);
        if (!guard) co_return std::unexpected(fixpp::core::error::session_already_closed);
        in = 5;
        out = 7;
        co_return fixpp::core::expected_t<void>{};
    }
};
struct hydrate_replica_no_edit_call {
    fixpp::sync::async_mutex* m = nullptr;
    seqnum_t* in = nullptr;
    seqnum_t* out = nullptr;
    auto operator()() const { return hydrate_replica_no_edit::run(*m, *in, *out); }
};

// ── Window runners ───────────────────────────────────────────────────────────

// Each runner owns its io_context and builds the callee with `make(ex)`, so a plant posts
// to the executor the driver runs on.

// Runs the callee at wrapper depth D, alone on its scheduler call.
template <int D, class Make>
rc::window_outcome run_alone(Make make) {
    asio::io_context ioc;
    auto f = make(asio::any_io_executor{ioc.get_executor()});
    return rc::run_driver_window<D>(ioc, ioc.get_executor(), f,
                                    rc::window_spec{.warm = kWarm, .measured = kMeasured});
}

// Runs `f` at wrapper depth D while a long-lived chain of C cycled frames, parked on a
// test-owned timer, completes and re-arms before every iteration (§3, W-D-R: q5's shape at
// production depth).
template <int D, int C, class Make>
rc::window_outcome run_with_parked_chain(Make make) {
    asio::io_context ioc;
    auto f = make(asio::any_io_executor{ioc.get_executor()});
    asio::steady_timer timer{ioc, asio::steady_timer::time_point::max()};
    bool stop = false;
    asio::co_spawn(ioc, rc::parked_chain_loop<C>(timer, stop), asio::detached);
    rc::cancel_then_yield_twice hook;
    hook.timer = &timer;
    auto out = rc::run_driver_window<D>(
        ioc, ioc.get_executor(), f,
        rc::window_spec{.warm = kWarm, .measured = kMeasured, .stop_after = &ioc}, hook);
    stop = true;
    timer.cancel();
    ioc.run();
    return out;
}

void expect_every_iteration_ok(rc::window_outcome const& out) {
    EXPECT_TRUE(out.closed) << "the driver never reached alloc_guard_end()";
    EXPECT_EQ(out.iterations, kWarm + kMeasured);
    EXPECT_EQ(out.ok, kWarm + kMeasured) << "a callee reported failure inside the window";
}

// ── W-D-R: the store with a pending read live ────────────────────────────────

TEST(B35DriverWindows, WDR_StoreWithLiveRead) {
    MemoryStore store{bounded_store_config(kWarm + kMeasured)};
    auto out = run_with_parked_chain<1, rc::kPendingReadCycledFrames>(
        [&](asio::any_io_executor) { return store_call{.store = &store}; });
    expect_every_iteration_ok(out);
}

// The bracket: with the stand-in read live, a nest of N - r fills the slots exactly and must
// read 0; one frame more must allocate. The arm proves the stand-in holds a slot.
TEST(B35DriverWindows, WDR_BracketTwin_NestFillsSlots) {
    constexpr int kNest = rc::kCacheSize - rc::kPendingReadCycledFrames;
    auto out = run_with_parked_chain<0, rc::kPendingReadCycledFrames>(
        [](asio::any_io_executor ex) { return rc::nest_plant<kNest>{.ex = ex}; });
    expect_every_iteration_ok(out);
}

TEST(B35DriverWindows, WDR_BracketArm_NestOneOverSlots) {
    constexpr int kNest = rc::kCacheSize - rc::kPendingReadCycledFrames + 1;
    auto out = run_with_parked_chain<0, rc::kPendingReadCycledFrames>(
        [](asio::any_io_executor ex) { return rc::nest_plant<kNest>{.ex = ex}; });
    expect_every_iteration_ok(out);
}

// ── W-D's (b-L) pair: the recycler's size limit ──────────────────────────────

// The arm's frame request exceeds the limit on its buffer alone.
inline constexpr std::size_t kOverLimitPlantBytes = 2 * rc::kRecycleLimit;
// The twin's buffer leaves room under the limit for the frame's overhead. It is a
// parameter: re-derive it per compiler from the arm's log, where each interception prints
// `aligned_alloc(<align>, <size>)` and asio requests `chunks * chunk_size + 1` bytes.
inline constexpr std::size_t kInLimitPlantBytes = rc::kRecycleLimit / 4;

TEST(B35DriverWindows, WD_BL_Arm_OverLimitFrame) {
    auto out = run_alone<0>(
        [](asio::any_io_executor ex) { return rc::buffer_plant<kOverLimitPlantBytes>{.ex = ex}; });
    expect_every_iteration_ok(out);
}

TEST(B35DriverWindows, WD_BL_Twin_InLimitFrame) {
    auto out = run_alone<0>(
        [](asio::any_io_executor ex) { return rc::buffer_plant<kInLimitPlantBytes>{.ex = ex}; });
    expect_every_iteration_ok(out);
}

// ── W-D's (b-S) pair: the slot count ─────────────────────────────────────────

TEST(B35DriverWindows, WD_BS_Arm_NestOneOverSlots) {
    auto out = run_alone<0>(
        [](asio::any_io_executor ex) { return rc::nest_plant<rc::kCacheSize + 1>{.ex = ex}; });
    expect_every_iteration_ok(out);
}

TEST(B35DriverWindows, WD_BS_Twin_NestFillsSlots) {
    auto out = run_alone<0>(
        [](asio::any_io_executor ex) { return rc::nest_plant<rc::kCacheSize>{.ex = ex}; });
    expect_every_iteration_ok(out);
}

// ── (c-L): store()'s lock reverted, at the filled depth ──────────────────────

TEST(B35DriverWindows, CL_Arm_StoreReplicaOnPublicLock) {
    fixpp::sync::async_mutex m;
    seqnum_t counter = 0;
    auto out = run_alone<rc::kFilledDepth>(
        [&](asio::any_io_executor) { return store_replica_call{.m = &m, .counter = &counter}; });
    expect_every_iteration_ok(out);
    EXPECT_EQ(counter, static_cast<seqnum_t>(kWarm + kMeasured));
}

TEST(B35DriverWindows, CL_ProductionTwin_MemoryStoreStore) {
    MemoryStore store{bounded_store_config(kWarm + kMeasured)};
    auto out = run_alone<rc::kFilledDepth>(
        [&](asio::any_io_executor) { return store_call{.store = &store}; });
    expect_every_iteration_ok(out);
}

// ── (c-H): hydrate()'s lock reverted, at the filled depth ────────────────────

TEST(B35DriverWindows, CH_Arm_HydrateReplicaOnPublicLock) {
    fixpp::sync::async_mutex m;
    seqnum_t in = 1;
    seqnum_t out_seq = 1;
    auto out = run_alone<rc::kFilledDepth>([&](asio::any_io_executor) {
        return hydrate_replica_call{.m = &m, .in = &in, .out = &out_seq};
    });
    expect_every_iteration_ok(out);
    EXPECT_EQ(in, static_cast<seqnum_t>(5));
    EXPECT_EQ(out_seq, static_cast<seqnum_t>(7));
}

TEST(B35DriverWindows, CH_ProductionTwin_SeqnumManagerHydrate) {
    fixpp::session::SeqnumManager mgr;
    auto out = run_alone<rc::kFilledDepth>(
        [&](asio::any_io_executor) { return hydrate_call{.mgr = &mgr}; });
    expect_every_iteration_ok(out);
    EXPECT_EQ(mgr.next_inbound_unsafe(), static_cast<seqnum_t>(5));
}

// ── (c-P): store()'s leading post reverted, at the filled depth ──────────────
// Its production twin is CL_ProductionTwin_MemoryStoreStore: the same callee at the same
// depth.

TEST(B35DriverWindows, CP_Arm_StoreReplicaOnUseAwaitablePost) {
    fixpp::sync::async_mutex m;
    seqnum_t counter = 0;
    auto out = run_alone<rc::kFilledDepth>([&](asio::any_io_executor) {
        return store_replica_ua_post_call{.m = &m, .counter = &counter};
    });
    expect_every_iteration_ok(out);
    EXPECT_EQ(counter, static_cast<seqnum_t>(kWarm + kMeasured));
}

// ── The no-edit replica twins, at the filled depth ───────────────────────────

TEST(B35DriverWindows, CLP_NoEditReplicaTwin_Store) {
    fixpp::sync::async_mutex m;
    seqnum_t counter = 0;
    auto out = run_alone<rc::kFilledDepth>([&](asio::any_io_executor) {
        return store_replica_no_edit_call{.m = &m, .counter = &counter};
    });
    expect_every_iteration_ok(out);
    EXPECT_EQ(counter, static_cast<seqnum_t>(kWarm + kMeasured));
}

TEST(B35DriverWindows, CH_NoEditReplicaTwin_Hydrate) {
    fixpp::sync::async_mutex m;
    seqnum_t in = 1;
    seqnum_t out_seq = 1;
    auto out = run_alone<rc::kFilledDepth>([&](asio::any_io_executor) {
        return hydrate_replica_no_edit_call{.m = &m, .in = &in, .out = &out_seq};
    });
    expect_every_iteration_ok(out);
    EXPECT_EQ(in, static_cast<seqnum_t>(5));
    EXPECT_EQ(out_seq, static_cast<seqnum_t>(7));
}

// ── W-D-W's stand-in bracket (§3, W-D-W) ─────────────────────────────────────
//
// A long-lived emitter loops over e cycled frames parked on a timer, standing in for an
// application send suspended in its write. Each iteration awaits an Active-depth replica
// pair through the template at D = 1 (the wrapper stands in for `on_inbound_active_`, the
// callee for `check_inbound`), whose callee awaits a nest of k frames:
//   k = 0          the window;
//   k = N - e - 2  the twin, which fills the slots exactly and must read 0;
//   k = N - e - 1  the arm, which must allocate.
// It pins the recycler's arithmetic for the derived e, not the production chain's depth.

template <int K>
struct check_inbound_shaped {
    static asio::awaitable<void> run(asio::any_io_executor ex) { co_await rc::nest<K>::run(ex); }
};
template <int K>
struct replica_pair_call {
    asio::any_io_executor ex;
    auto operator()() const { return check_inbound_shaped<K>::run(ex); }
};

inline constexpr int kWdwTwinNest = rc::kCacheSize - rc::kSendInWriteCycledFrames - 2;
inline constexpr int kWdwArmNest = rc::kCacheSize - rc::kSendInWriteCycledFrames - 1;

// The headroom condition (§2.4, "Headroom, as a condition"; §3, W-D-W): at the instant N is
// sized on, the inbound pair requesting blocks while a send is held in its write, the slots
// leave room for at least one more nested `co_await` on each of the two chains. It reads N
// from the macro and e from the harness, so it is as current as e: re-derive e by its recipe
// (tests/support/recycler_driver.hpp) whenever the send chain gains a `co_await`.
static_assert(rc::kCacheSize - rc::kSendInWriteCycledFrames - 2 >= 2,
              "W-D-W headroom: ASIO_RECYCLING_ALLOCATOR_CACHE_SIZE - e - 2 must be at least 2");

template <int K>
void run_wdw_stand_in() {
    auto out = run_with_parked_chain<1, rc::kSendInWriteCycledFrames>(
        [](asio::any_io_executor ex) { return replica_pair_call<K>{.ex = ex}; });
    expect_every_iteration_ok(out);
}

TEST(B35DriverWindows, WDW_StandIn_Window) { run_wdw_stand_in<0>(); }

TEST(B35DriverWindows, WDW_StandIn_Twin) { run_wdw_stand_in<kWdwTwinNest>(); }

TEST(B35DriverWindows, WDW_StandIn_Arm) { run_wdw_stand_in<kWdwArmNest>(); }

// ── W-D-W's real-chain twin (§3, I2R2 C-1) ───────────────────────────────────
//
// The same bracket, with the stand-in emitter replaced by the real `Session::send` chain
// held in its write by tests/support/blocking_write_transport.hpp. If the real chain holds
// more than the derived e, the twin at k = N - e - 2 reads above zero; if fewer, the arm at
// k = N - e - 1 reads zero and fails its `--expect-violation`. So e is a checked input.
//
// Each iteration, in the driver's frame: arm the block and wake the long-lived sender;
// yield until its send is parked in the write; await the replica pair and the nest of k;
// release the write; yield until the send has completed. All of it runs under one
// `ioc.run()`, and release() runs on that thread.

struct park_send_then_release : rc::no_hook {
    static constexpr int yields_after = 2;
    fixpp::session::test::blocking_write_state* write = nullptr;
    asio::steady_timer* sender_wake = nullptr;
    std::atomic<std::uint64_t>* sends_done = nullptr;
    std::uint64_t* sends_issued = nullptr;
    void before() const {
        write->arm_block();
        ++*sends_issued;
        sender_wake->cancel();
    }
    [[nodiscard]] bool ready() const { return write->parked; }
    void after() const { write->release(); }
    [[nodiscard]] bool settled() const {
        return sends_done->load(std::memory_order_acquire) >= *sends_issued;
    }
};

struct real_chain_outcome {
    bool up = false;
    bool sender_exited = false;
    std::size_t writes = 0;
    std::uint64_t sends_ok = 0;
    rc::window_outcome window;
};

template <int K>
real_chain_outcome run_wdw_real_chain() {
    namespace pr = fixpp::test_support::plain_rig;
    namespace rt = fixpp::test_support::run_thread_rig;
    real_chain_outcome r;
    pr::Rig rig{std::make_shared<fixpp::session::Application>()};
    auto stream = std::make_shared<fixpp::session::test::ScriptedStream>();
    stream->hold_open = true;
    auto write = std::make_shared<fixpp::session::test::blocking_write_state>();
    auto cfg = rig.cfg(fixpp::session::session_role::initiator);
    cfg.transport_factory_override =
        std::make_shared<fixpp::session::test::BlockingWriteTransportFactory>(stream, write);
    r.up = rig.start(std::move(cfg)) && rig.run_until([&] {
        return rig.state() == fixpp::session::fsm_state::LogonSent && stream->waiting != nullptr;
    });
    if (r.up) {
        stream->push(pr::to_bytes(rig.logon()));
        r.up = rig.run_until([&] { return rig.state() == fixpp::session::fsm_state::Active; });
    }
    auto const sess = rig.session();
    if (r.up && sess) {
        auto const strand = sess->executor().underlying();
        auto const payload = pr::to_bytes(
            "35=D\x01"
            "11=OUT1\x01"
            "54=2\x01");
        asio::steady_timer wake{strand, asio::steady_timer::time_point::max()};
        std::atomic<bool> stop{false};
        std::atomic<bool> exited{false};
        std::atomic<std::uint64_t> sends_done{0};
        std::atomic<std::uint64_t> sends_ok{0};
        rt::send_body body{.session = sess.get(), .payload = std::span<const std::byte>{payload}};
        asio::co_spawn(strand, rt::woken_loop(wake, stop, body, sends_done, sends_ok, exited),
                       asio::detached);

        std::uint64_t sends_issued = 0;
        park_send_then_release hook;
        hook.write = write.get();
        hook.sender_wake = &wake;
        hook.sends_done = &sends_done;
        hook.sends_issued = &sends_issued;
        auto pair = replica_pair_call<K>{.ex = strand};
        r.window = rc::run_driver_window<1>(
            rig.ioc, strand, pair,
            rc::window_spec{.warm = kWarm, .measured = kMeasured, .stop_after = &rig.ioc}, hook);

        asio::post(strand, [&] {
            stop.store(true, std::memory_order_release);
            wake.cancel();
        });
        r.sender_exited = rig.run_until([&] { return exited.load(std::memory_order_acquire); });
        r.sends_ok = sends_ok.load();
    }
    r.writes = write->writes;
    rig.stop();
    return r;
}

template <int K>
void check_wdw_real_chain() {
    auto const r = run_wdw_real_chain<K>();
    ASSERT_TRUE(r.up) << "the initiator did not reach Active";
    expect_every_iteration_ok(r.window);
    EXPECT_TRUE(r.sender_exited);
    EXPECT_EQ(r.sends_ok, static_cast<std::uint64_t>(kWarm + kMeasured))
        << "a send parked in its write did not complete";
}

TEST(B35DriverWindows, WDW_RealChain_Twin) { check_wdw_real_chain<kWdwTwinNest>(); }

TEST(B35DriverWindows, WDW_RealChain_Arm) { check_wdw_real_chain<kWdwArmNest>(); }

}  // namespace
