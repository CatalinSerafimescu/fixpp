// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/support/recycler_driver.hpp — test-only.
//
// fixpp#544 (B35; `.specify/544-hot-path-zero-alloc.md` §3, "The window rule" and
// "One template provides the driver and the wrapper"): the driver-and-wrapper template
// every driver-form allocation window instantiates, and the planted frames their arms
// and twins await.
//
// WHY ONE SCHEDULER CALL. asio serves coroutine frames from a recycling cache that a
// scheduler call declares on its own stack (`asio/detail/impl/scheduler.ipp`,
// `thread_info this_thread;`), not one that belongs to the thread. A window that spans
// several `run*` / `poll*` calls starts each with an empty cache. So the driver is
// spawned before the window opens, it runs the warm-up and the windowed iterations
// itself, all under one `ioc.run()`, and nothing in the window spawns a coroutine or
// waits on a future (`use_future`'s handler allocates its promise).
//
// DEPTH. `padder<D, F>::run(f)` puts exactly D named wrapper coroutines between the
// driver and the callee `f()`; at D = 0 it is a non-coroutine forwarder. No lambda
// coroutine is on the path, because a lambda coroutine is a cycled frame nobody chose.
// The callee is a NON-coroutine callable that returns the awaitable, or the deferred
// operation, to await: a lambda that only forwards a call is not a frame.
//
// THE MACRO IS READ AFTER asio DEFINES ITS DEFAULT. `thread_info_base.hpp` defines
// `ASIO_RECYCLING_ALLOCATOR_CACHE_SIZE` when no one else has, so it is included before
// the macro is read here.
#pragma once

#include <asio/as_tuple.hpp>
#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/deferred.hpp>
#include <asio/detached.hpp>
#include <asio/detail/thread_info_base.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/steady_timer.hpp>
#include <asio/this_coro.hpp>
#include <climits>
#include <cstddef>
#include <type_traits>
#include <utility>

#include "support/alloc_guard_markers.hpp"

namespace fixpp::test_support::recycler {

// The recycler's slot count per purpose, as the test TU sees it.
inline constexpr int kCacheSize = ASIO_RECYCLING_ALLOCATOR_CACHE_SIZE;

// `chunk_size` is a private enumerator of `asio::detail::thread_info_base`, so it cannot
// be named here. This mirrors its definition under the same condition. Re-check with
//   grep -n "chunk_size = " <asio include>/asio/detail/thread_info_base.hpp
#ifdef ASIO_HAS_IO_URING
inline constexpr std::size_t kChunkSize = 8;
#else
inline constexpr std::size_t kChunkSize = 4;
#endif

// The largest block the recycler caches (`thread_info_base::deallocate`): a frame
// request above it is allocated on every use and evicts a cached block.
inline constexpr std::size_t kRecycleLimit = kChunkSize * UCHAR_MAX;

// The (b-L) pairs' plant buffers.
inline constexpr std::size_t kOverLimitPlantBytes = 2 * kRecycleLimit;
// The twin's buffer leaves room under the limit for the frame's overhead. It is a
// parameter: re-derive it per compiler from the arm's log, where each interception prints
// `aligned_alloc(<align>, <size>)` and asio requests `chunks * chunk_size + 1` bytes.
inline constexpr std::size_t kInLimitPlantBytes = kRecycleLimit / 4;

// ── Production-chain parameters (harness inputs, not assertions) ─────────────
//
// Each is a count of cycled coroutine frames on a production chain, derived by the
// note's §2.4 recipe (a `use_awaitable` adapter counts as one frame). A later phase
// that changes the chain re-derives the value here; the cells that use it follow.
//
// r: the pending read's own cycled frames. Walk from the read pump's Active-branch
// `read_r = co_await transport.async_read_some(read_span)` (src/session/engine.cpp)
// into `asio_plain_transport::async_read_some` (src/transport/asio_plain_transport.cpp),
// counting each coroutine and each `use_awaitable` adapter; an operation awaited with
// `asio::deferred` adds no frame.
inline constexpr int kPendingReadCycledFrames = 1;

// e: the cycled frames of an application send suspended in its write. Walk from
// `Session::send` (src/session/session.cpp) through `send_impl`, `store_then_emit` and
// `live_write_serialized_` into the plain transport's `async_write` and its
// `use_awaitable` adapter. A send waiting at the write gate holds the public
// `async_lock` frame there instead, which is fewer.
inline constexpr int kSendInWriteCycledFrames = 6;

// The production callee's own cycled frames at the filled depth (§3, "One template"):
// the depth D at which D plus this count is exactly the slot count, for
// `MemoryStore::store` and `SeqnumManager::hydrate`. Re-derive it by walking the
// callee's `co_await`s, read inside each callee in the output of
//   grep -n "co_await\|FIXPP_DETAIL_CO_AWAIT_LOCK" include/fixpp/session/memory_store.hpp
//   grep -n "co_await\|FIXPP_DETAIL_CO_AWAIT_LOCK" src/session/seqnum_manager.cpp
// A `deferred` post and FIXPP_DETAIL_CO_AWAIT_LOCK add no frame; a public
// `async_mutex::async_lock` adds one.
inline constexpr int kFilledDepthCalleeFrames = 1;
inline constexpr int kFilledDepth = kCacheSize - kFilledDepthCalleeFrames;

// ── awaited_t: what `co_await` of a callee's result yields ──────────────────
template <class A>
struct awaited {
    using type = void;  // a deferred operation with a void() signature
};
template <class T, class E>
struct awaited<asio::awaitable<T, E>> {
    using type = T;
};
template <class A>
// NOLINTNEXTLINE(readability-redundant-typename): the pre-C++20 spelling
using awaited_t = typename awaited<std::remove_cvref_t<A>>::type;

// ── padder<D, F>: exactly D cycled wrapper frames above the callee ──────────
template <int D, class F>
struct padder {
    static_assert(D > 0);
    using result_type = awaited_t<std::invoke_result_t<F&>>;
    static asio::awaitable<result_type> run(F& f) {
        if constexpr (std::is_void_v<result_type>) {
            co_await padder<D - 1, F>::run(f);
        } else {
            co_return co_await padder<D - 1, F>::run(f);
        }
    }
};
template <class F>
struct padder<0, F> {
    // A non-coroutine forwarder: no frame.
    static decltype(auto) run(F& f) { return f(); }
};

// ── Per-iteration hooks ──────────────────────────────────────────────────────
// Every iteration runs, in the driver's frame: `before()`; `yields` posts; more posts
// until `ready()`; the callee; `after()`; `yields_after` posts; more posts until
// `settled()`. The posts are `asio::deferred`, so they add no frame, and the driver's own
// frame lives for the whole window and is not cycled. A hook overrides only what it needs.
struct no_hook {
    static constexpr int yields = 0;
    static constexpr int yields_after = 0;
    void before() const noexcept {}
    [[nodiscard]] bool ready() const noexcept { return true; }
    void after() const noexcept {}
    [[nodiscard]] bool settled() const noexcept { return true; }
};

// Cancels a test-owned timer, then yields twice, so that a long-lived chain parked on
// that timer completes and re-arms before the iteration's callee runs (§3, W-D-R).
struct cancel_then_yield_twice : no_hook {
    static constexpr int yields = 2;
    asio::steady_timer* timer = nullptr;
    // NOLINTNEXTLINE(bugprone-derived-method-shadowing-base-method): replaces no_hook's
    void before() const { timer->cancel(); }
};

// The most posts a `ready()` or `settled()` wait makes before the driver gives the
// iteration up, so that a hook that never becomes true fails the cell instead of hanging.
inline constexpr int kMaxHookYields = 10000;

struct window_spec {
    int warm = 0;      // K: iterations before alloc_guard_start()
    int measured = 0;  // M: iterations inside the window
    // Stopped after the window closes, when other work would keep `run()` from
    // returning (a live session's timers, a long-lived chain).
    asio::io_context* stop_after = nullptr;
};

struct window_outcome {
    int iterations = 0;   // callee awaits completed, warm-up included
    int ok = 0;           // of those, the ones whose result reported success
    bool closed = false;  // the driver reached alloc_guard_end()
};

template <class R>
bool result_ok(R const& r) {
    if constexpr (requires { r.has_value(); }) {
        return r.has_value();
    } else {
        return true;
    }
}

// The driver: K warm-up iterations, alloc_guard_start(), M iterations,
// alloc_guard_end(). Spawn it before the window with `spawn_driver` and run the
// io_context once.
template <int D, class F, class Hook = no_hook>
asio::awaitable<void> driver(F& f, window_spec spec, window_outcome& out, Hook hook = {}) {
    auto ex = co_await asio::this_coro::executor;
    for (int i = 0; i < spec.warm + spec.measured; ++i) {
        if (i == spec.warm && alloc_guard_start) alloc_guard_start();
        hook.before();
        for (int y = 0; y < Hook::yields; ++y) co_await asio::post(ex, asio::deferred);
        bool hook_ok = true;
        for (int y = 0; !hook.ready(); ++y) {
            if (y == kMaxHookYields) {
                hook_ok = false;
                break;
            }
            co_await asio::post(ex, asio::deferred);
        }
        using R = awaited_t<decltype(padder<D, F>::run(f))>;
        if constexpr (std::is_void_v<R>) {
            co_await padder<D, F>::run(f);
        } else {
            auto r = co_await padder<D, F>::run(f);
            if (!result_ok(r)) hook_ok = false;
        }
        hook.after();
        for (int y = 0; y < Hook::yields_after; ++y) co_await asio::post(ex, asio::deferred);
        for (int y = 0; !hook.settled(); ++y) {
            if (y == kMaxHookYields) {
                hook_ok = false;
                break;
            }
            co_await asio::post(ex, asio::deferred);
        }
        if (hook_ok) ++out.ok;
        ++out.iterations;
    }
    if (spec.measured == 0 && alloc_guard_start) alloc_guard_start();
    if (alloc_guard_end) alloc_guard_end();
    out.closed = true;
    if (spec.stop_after != nullptr) spec.stop_after->stop();
}

// Spawns the driver on `ex` and runs `ioc` once. The driver lives for the whole window.
template <int D, class F, class Hook = no_hook>
window_outcome run_driver_window(asio::io_context& ioc, asio::any_io_executor ex, F& f,
                                 window_spec spec, Hook hook = {}) {
    window_outcome out;
    asio::co_spawn(ex, driver<D, F, Hook>(f, spec, out, hook), asio::detached);
    ioc.restart();
    ioc.run();
    ioc.restart();
    return out;
}

// ── Planted frames ───────────────────────────────────────────────────────────

// nest<K>: exactly K trivial cycled frames; the innermost awaits a deferred post.
// nest<0> is the post alone, with no frame. (§3, arm (b-S).)
template <int K>
struct nest {
    static_assert(K > 1);
    static asio::awaitable<void> run(asio::any_io_executor ex) { co_await nest<K - 1>::run(ex); }
};
template <>
struct nest<1> {
    static asio::awaitable<void> run(asio::any_io_executor ex) {
        co_await asio::post(ex, asio::deferred);
    }
};
template <>
struct nest<0> {
    static auto run(asio::any_io_executor ex) { return asio::post(ex, asio::deferred); }
};

template <int K>
struct nest_plant {
    static_assert(K >= 0, "a nest cannot hold a negative number of frames");
    asio::any_io_executor ex;
    auto operator()() const { return nest<K>::run(ex); }
};

// Written through, so a planted buffer's address escapes and the compiler must keep the
// buffer in the coroutine frame across the suspension.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables): the sink
inline std::byte* volatile g_plant_sink = nullptr;

// A frame holding `Bytes` across one deferred post (§3, arm (b-L)). Its frame request
// is `Bytes` plus the compiler's frame overhead.
template <std::size_t Bytes>
struct hold_across_post {
    static asio::awaitable<void> run(asio::any_io_executor ex) {
        std::byte buf[Bytes];
        buf[0] = std::byte{1};
        g_plant_sink = buf;
        co_await asio::post(ex, asio::deferred);
        buf[Bytes - 1] = std::byte{2};
        g_plant_sink = buf;
    }
};

template <std::size_t Bytes>
struct buffer_plant {
    asio::any_io_executor ex;
    auto operator()() const { return hold_across_post<Bytes>::run(ex); }
};

// ── Long-lived chains parked on a timer (§3, W-D-R and W-D-W) ───────────────
//
// timer_chain<C>: exactly C cycled frames, the innermost awaiting `timer`. A long-lived
// loop over it stands in for a pending transport read (C = r) or for an application
// send suspended in its write (C = e). Cancelling the timer completes the chain; the
// loop then re-arms it, so its frames are cycled inside the window.
template <int C>
struct timer_chain {
    static_assert(C > 1);
    static asio::awaitable<void> run(asio::steady_timer& timer) {
        co_await timer_chain<C - 1>::run(timer);
    }
};
template <>
struct timer_chain<1> {
    static asio::awaitable<void> run(asio::steady_timer& timer) {
        (void)co_await timer.async_wait(asio::as_tuple(asio::deferred));
    }
};

template <int C>
asio::awaitable<void> parked_chain_loop(asio::steady_timer& timer, bool const& stop) {
    while (!stop) co_await timer_chain<C>::run(timer);
}

}  // namespace fixpp::test_support::recycler
