// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/alloc_guard/test_106_connection_bound.cpp — 106-logon-guards-write-bounds (tasks.md T004,
// T108).
//
// The connection bound's zero-allocation cell, alloc_guard_106_connection_bound_mallocnesia
// (research D9 row R2(a)): a real Engine session over system_clock_source, on the run-thread rig
// (tests/support/run_thread_engine_rig.hpp), so B-544-1's two conditions hold (the Engine's
// executor is io_context's own, and one thread stays inside io_context::run across messages).
//
//   Send half  HeartBtInt 30 s, longer than every window, so the liveness loop's own sleep never
//              re-arms in it; after warm-up, spaced Session::sends (one per inbound
//              NewOrderSingle, the next written after a pause longer than the limit), with
//              write_timeout_ms off and set: zero allocations in the window, and no cut (the
//              session is still Active with every send delivered; a cut ends it).
//   Emit half  HeartBtInt 1 s and a TestRequest threshold far beyond the run, the peer silent, so
//              each Heartbeat cycle is one emit and one loop sleep, and the loop's sleep takes
//              the per-call fallback (L-093-13) on every cycle. The window opens and closes
//              half a cycle after a Heartbeat reaches the peer, so it holds whole cycles only.
//              The reading is the allocation count over the window and the cycles in it, printed
//              and not asserted: the head's counts with the limit off and on and the base's
//              (FIXPP_106_ALLOC_BASE, read on the base tree under the same parameters) are
//              compared by hand, over repeated runs (research D9 row R2(a), tasks.md T108).
//
// TWO BINARIES FROM THIS SOURCE, 544's shape (test_544_run_thread_windows.cpp). mallocnesia.c
// refuses a process that defines the allocator names, so the interceptor runs the build without a
// counter (FIXPP_106_TU_COUNTER 0, the send half only), and FIXPP_106_TU_COUNTER 1 builds a
// counter of glibc's allocation entry points, the ones the interceptor hooks (malloc, calloc,
// realloc, aligned_alloc, memalign, posix_memalign), forwarding to glibc's __libc_* allocator as
// mallocnesia.c does. Global operator new reaches malloc and asio's recycler reaches
// aligned_alloc (asio/detail/memory.hpp, aligned_new), so the counter sees both. Counts are
// asserted on Release builds only, as the interceptor registrations are.
//
// With FIXPP_106_ALLOC_BASE set to 1 the TU builds only the emit half with the limit off and
// names no symbol 106 adds, so the base tree can build it (tasks.md T004).

#include <gtest/gtest.h>

#if !defined(FIXPP_106_ALLOC_BASE)
#error "FIXPP_106_ALLOC_BASE must be defined to 0 or 1 by tests/alloc_guard/CMakeLists.txt"
#endif
#if !defined(FIXPP_106_TU_COUNTER)
#error "FIXPP_106_TU_COUNTER must be defined to 0 or 1 by tests/alloc_guard/CMakeLists.txt"
#endif

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/system_clock_source.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include "support/alloc_guard_markers.hpp"
#include "support/run_thread_engine_rig.hpp"

// ── Sanitizer-detection guard (tests/alloc_guard/test_validate_gate_alloc_guard.cpp) ──
#ifdef __has_feature
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer) || \
    __has_feature(memory_sanitizer)
#define FIXPP_SANITIZER_REPLACES_NEW 1
#endif
#endif
#if !defined(FIXPP_SANITIZER_REPLACES_NEW) && \
    (defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__))
#define FIXPP_SANITIZER_REPLACES_NEW 1
#endif
#ifndef FIXPP_SANITIZER_REPLACES_NEW
#define FIXPP_SANITIZER_REPLACES_NEW 0
#endif

// The counter needs glibc's __libc_* exports (mallocnesia.c's condition) and a process whose
// allocator no sanitizer owns.
#if FIXPP_106_TU_COUNTER && !FIXPP_SANITIZER_REPLACES_NEW && defined(__GLIBC__)
#define FIXPP_106_COUNTS 1
#else
#define FIXPP_106_COUNTS 0
#endif

#if FIXPP_106_COUNTS
#include <malloc.h>

// Counts every call of the hooked entry points, from every thread, while armed.
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables) — the counter's state
static std::atomic<long> g_alloc_count{0};
static std::atomic<bool> g_counting{false};
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

extern "C" {
void* __libc_malloc(std::size_t);                 // NOLINT(bugprone-reserved-identifier)
void* __libc_calloc(std::size_t, std::size_t);    // NOLINT(bugprone-reserved-identifier)
void* __libc_realloc(void*, std::size_t);         // NOLINT(bugprone-reserved-identifier)
void* __libc_memalign(std::size_t, std::size_t);  // NOLINT(bugprone-reserved-identifier)
}

namespace {
inline void note_alloc() noexcept {
    if (g_counting.load(std::memory_order_relaxed)) {
        g_alloc_count.fetch_add(1, std::memory_order_relaxed);
    }
}
}  // namespace

// NOLINTBEGIN(cppcoreguidelines-no-malloc,hicpp-no-malloc,cert-dcl58-cpp,readability-inconsistent-declaration-parameter-name)
extern "C" void* malloc(std::size_t n) noexcept {
    note_alloc();
    return __libc_malloc(n);
}
extern "C" void* calloc(std::size_t n, std::size_t m) noexcept {
    note_alloc();
    return __libc_calloc(n, m);
}
extern "C" void* realloc(void* p, std::size_t n) noexcept {
    note_alloc();
    return __libc_realloc(p, n);
}
// glibc has no __libc_ twin for aligned_alloc or posix_memalign; both validate as glibc does and
// forward to __libc_memalign (mallocnesia.c's hooks).
extern "C" void* aligned_alloc(std::size_t align, std::size_t n) noexcept {
    note_alloc();
    if (align == 0 || (align & (align - 1)) != 0) {
        errno = EINVAL;
        return nullptr;
    }
    return __libc_memalign(align, n);
}
extern "C" void* memalign(std::size_t align, std::size_t n) noexcept {
    note_alloc();
    return __libc_memalign(align, n);
}
extern "C" int posix_memalign(void** out, std::size_t align, std::size_t n) noexcept {
    note_alloc();
    if (align == 0 || align % sizeof(void*) != 0 ||
        ((align / sizeof(void*)) & (align / sizeof(void*) - 1)) != 0) {
        return EINVAL;
    }
    void* p = __libc_memalign(align, n);
    if (p == nullptr) return ENOMEM;
    *out = p;
    return 0;
}
// NOLINTEND(cppcoreguidelines-no-malloc,hicpp-no-malloc,cert-dcl58-cpp,readability-inconsistent-declaration-parameter-name)

#endif  // FIXPP_106_COUNTS

namespace {

namespace rt = fixpp::test_support::run_thread_rig;
using fixpp::session::fsm_state;
using namespace std::chrono_literals;

void open_window() {
    if (alloc_guard_start) alloc_guard_start();
#if FIXPP_106_COUNTS
    g_alloc_count.store(0, std::memory_order_relaxed);
    g_counting.store(true, std::memory_order_relaxed);
#endif
}

// The counter's reading, or -1 in a build without the counter.
long close_window() {
    long n = -1;
#if FIXPP_106_COUNTS
    g_counting.store(false, std::memory_order_relaxed);
    n = g_alloc_count.load(std::memory_order_relaxed);
#endif
    if (alloc_guard_end) alloc_guard_end();
    return n;
}

// The rig on the shipping clock. The frames' SendingTime(52) is read from the rig's mock clock,
// which never moves, so that clock is re-seeded at the real time: an inbound SendingTime must be
// within MaxLatency of the session's effective clock, which here is system_clock_source.
struct ShippingRig {
    explicit ShippingRig(rt::options o) : rig{std::move(o)} {
        rig.clock = std::make_shared<fixpp::core::mock_clock>(
            std::chrono::floor<std::chrono::milliseconds>(std::chrono::system_clock::now()),
            fixpp::core::steady_time_point{}, rig.ioc.get_executor());
        sys = std::make_shared<fixpp::core::system_clock_source>(rig.ioc.get_executor());
        auto const shape = rig.opt.shape_engine;
        rig.opt.shape_engine = [shape, c = sys](fixpp::core::EngineConfig& ec) {
            if (shape) shape(ec);
            ec.clock = c;
        };
    }
    rt::Rig rig;
    std::shared_ptr<fixpp::core::system_clock_source> sys;
};

#if !FIXPP_106_ALLOC_BASE

// ── The send half ───────────────────────────────────────────────────────────

constexpr int kSendWarm = 8;       // spaced sends before the window
constexpr int kSendMeasured = 20;  // spaced sends inside it
constexpr auto kSendSpacing = 100ms;
constexpr std::uint32_t kSendLimitMs = 50;  // below the spacing, so each wait ends in the gap

struct send_result {
    bool up = false;
    bool warmed = false;
    bool window_done = false;
    bool stopped = false;
    long count = -1;
    std::uint64_t completions = 0;
    std::uint64_t loop_ok = 0;
    std::size_t peer_app_frames = 0;
    bool peer_ended = false;
    rt::session_snapshot snap;
};

constexpr auto make_sender = [](fixpp::session::Session* s, std::span<const std::byte> payload) {
    return rt::send_body{.session = s, .payload = payload};
};

send_result run_send_half(std::uint32_t write_ms) {
    send_result r;
    ShippingRig sr{rt::options{.mode = rt::hook::wake_from_app,
                               .validating = true,
                               .shape_session = [write_ms](fixpp::session::SessionConfig& c) {
                                   c.heartbeat_interval = std::chrono::seconds{30};
                                   c.write_timeout_ms = write_ms;
                               }}};
    auto& rig = sr.rig;
    int const total = kSendWarm + kSendMeasured;
    std::vector<std::string> frames;
    frames.reserve(static_cast<std::size_t>(total));
    for (int i = 0; i < total; ++i)
        frames.push_back(rig.new_order(static_cast<std::uint32_t>(2 + i)));
    std::vector<std::byte> const payload = rig.send_payload();

    auto& done = rig.app->completions;
    r.up = rig.start_and_logon();
    auto sess = rig.session();
    auto body = make_sender(sess.get(), std::span<const std::byte>{payload});
    if (r.up && sess) r.up = rig.start_woken_loop(body);

    int next = 0;
    if (r.up) {
        r.warmed = true;
        for (int i = 0; i < kSendWarm && r.warmed; ++i, ++next) {
            r.warmed = rig.write_and_wait(frames[next], done, static_cast<std::uint64_t>(next + 1));
            std::this_thread::sleep_for(kSendSpacing);
        }
    }
    if (r.warmed) {
        bool ok = true;
        open_window();
        for (int j = 0; j < kSendMeasured && ok; ++j, ++next) {
            ok = rig.write_and_wait(frames[next], done, static_cast<std::uint64_t>(next + 1));
            std::this_thread::sleep_for(kSendSpacing);
        }
        r.count = close_window();
        r.window_done = ok;
    }
    auto const want = static_cast<std::size_t>(rig.loop_ok.load());
    (void)rig.wait_until([&] { return rig.reader.app_frames.load() >= want; });
    r.snap = rig.observe();
    r.peer_ended = rig.reader.ended.load();
    r.stopped = rig.stop();
    r.completions = done.load();
    r.loop_ok = rig.loop_ok.load();
    r.peer_app_frames = rig.reader.app_frames.load();
    return r;
}

void expect_send_half(send_result const& r, char const* cell) {
    ASSERT_TRUE(r.up) << cell << ": the session did not reach Active";
    ASSERT_TRUE(r.warmed) << cell << ": a warm-up send did not complete";
    EXPECT_TRUE(r.window_done) << cell << ": a send inside the window did not complete";
    EXPECT_TRUE(r.stopped) << cell << ": Engine::stop() did not complete";
    auto const frames = static_cast<std::uint64_t>(kSendWarm + kSendMeasured);
    EXPECT_EQ(r.completions, frames) << cell << ": the completion signal did not fire per frame";
    EXPECT_EQ(r.loop_ok, frames) << cell << ": a send did not succeed";
    EXPECT_GE(r.peer_app_frames, r.loop_ok) << cell << ": a successful send did not reach the peer";
    // A cut ends the session (data-model E-5): Disconnected, the close, the record. Active after
    // the window with the peer's socket open is therefore the absence of every cut in it.
    EXPECT_FALSE(r.peer_ended) << cell << ": the connection was closed";
    ASSERT_TRUE(r.snap.read) << cell << ": the strand-side state read did not run";
    EXPECT_EQ(r.snap.state, fsm_state::Active) << cell;
    EXPECT_EQ(r.snap.next_inbound, static_cast<fixpp::session::seqnum_t>(2 + frames))
        << cell << ": NextNumIn does not show every frame processed";
#if FIXPP_106_COUNTS
    // NOLINTNEXTLINE(modernize-use-std-print): std::println is C++23
    std::printf("[106] %s: allocations in the window = %ld (sends = %d)\n", cell, r.count,
                kSendMeasured);
#ifdef NDEBUG
    EXPECT_EQ(r.count, 0L) << cell << ": allocations in the window";
#endif
#endif
}

TEST(Alloc106ConnectionBound, SendHalf_LimitOff) {
    expect_send_half(run_send_half(0), "send half, limit off");
}

TEST(Alloc106ConnectionBound, SendHalf_LimitOn) {
    expect_send_half(run_send_half(kSendLimitMs), "send half, limit on");
}

#endif  // !FIXPP_106_ALLOC_BASE

// ── The emit half ───────────────────────────────────────────────────────────

#if FIXPP_106_TU_COUNTER

constexpr int kEmitWarm = 3;      // Heartbeats before the window
constexpr int kEmitMeasured = 5;  // Heartbeat cycles inside it
constexpr auto kEmitHeartBt = std::chrono::seconds{1};
constexpr auto kEmitHalfCycle = 500ms;

struct emit_result {
    bool up = false;
    bool warmed = false;
    bool window_done = false;
    bool stopped = false;
    long count = -1;
    rt::session_snapshot snap;
};

// One Heartbeat reaches the peer: its bytes counter moves. The peer sends nothing, so the only
// frames after the Logon acknowledgement are the session's Heartbeats, one per HeartBtInt, each
// read on its own completion.
[[nodiscard]] bool next_heartbeat(rt::Rig& rig) {
    auto const before = rig.reader.bytes.load(std::memory_order_acquire);
    return rig.wait_until(
        [&] { return rig.reader.bytes.load(std::memory_order_acquire) != before; }, 5s);
}

// The write limit, on the head only: the base has no knob.
void set_write_limit(fixpp::session::SessionConfig& c, std::uint32_t ms) {
#if FIXPP_106_ALLOC_BASE
    (void)c;
    (void)ms;
#else
    c.write_timeout_ms = ms;
#endif
}

emit_result run_emit_half(std::uint32_t write_ms) {
    emit_result r;
    ShippingRig sr{rt::options{.mode = rt::hook::signal_from_admin,
                               .shape_session = [write_ms](fixpp::session::SessionConfig& c) {
                                   c.heartbeat_interval = kEmitHeartBt;
                                   c.test_request_threshold = std::chrono::milliseconds{3600000};
                                   set_write_limit(c, write_ms);
                               }}};
    auto& rig = sr.rig;
    r.up = rig.start_and_logon();
    if (r.up) {
        r.warmed = true;
        for (int i = 0; i < kEmitWarm && r.warmed; ++i) r.warmed = next_heartbeat(rig);
    }
    if (r.warmed) r.warmed = next_heartbeat(rig);
    if (r.warmed) {
        std::this_thread::sleep_for(kEmitHalfCycle);
        bool ok = true;
        open_window();
        for (int j = 0; j < kEmitMeasured && ok; ++j) ok = next_heartbeat(rig);
        std::this_thread::sleep_for(kEmitHalfCycle);
        r.count = close_window();
        r.window_done = ok;
    }
    r.snap = rig.observe();
    r.stopped = rig.stop();
    return r;
}

void expect_emit_ran(emit_result const& r, char const* cell) {
    ASSERT_TRUE(r.up) << cell << ": the session did not reach Active";
    ASSERT_TRUE(r.warmed) << cell << ": a warm-up Heartbeat did not reach the peer";
    EXPECT_TRUE(r.window_done) << cell << ": a Heartbeat inside the window did not reach the peer";
    EXPECT_TRUE(r.stopped) << cell << ": Engine::stop() did not complete";
    ASSERT_TRUE(r.snap.read) << cell << ": the strand-side state read did not run";
    EXPECT_EQ(r.snap.state, fsm_state::Active) << cell;
#if FIXPP_106_COUNTS
    // NOLINTNEXTLINE(modernize-use-std-print): std::println is C++23
    std::printf("[106] %s: allocations in the window = %ld (Heartbeat cycles = %d)\n", cell,
                r.count, kEmitMeasured);
#endif
}

TEST(Alloc106ConnectionBound, EmitHalf_PerHeartbeatCycle) {
    auto const off = run_emit_half(0);
    expect_emit_ran(off, "emit half, limit off");
#if !FIXPP_106_ALLOC_BASE
    auto const on = run_emit_half(kSendLimitMs);
    expect_emit_ran(on, "emit half, limit on");
#endif
}

#endif  // FIXPP_106_TU_COUNTER

}  // namespace
