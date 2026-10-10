// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/alloc_guard/test_544_run_thread_windows.cpp
//
// fixpp#544 (B35; `.specify/544-hot-path-zero-alloc.md` §3): the windows that run the real
// Engine read pump, on the run-thread rig (tests/support/run_thread_engine_rig.hpp):
//
//   W-A    an Active inbound Heartbeat;
//   W-B    an Active inbound application message through parse -> validate -> dispatch;
//   W-E    W-B interleaved with `Session::send`s from a long-lived sender, and the
//          veto-then-zero cell (a vetoed send before the window);
//   (e)    W-E with the send slot's flag set by the test before the warm-up and left set,
//          so every send takes the fallback leaf, whose frame is over the recycler's limit;
//          its twin is W-E;
//   (a)    W-A with the Engine on a work-tracked executor, which takes the strand's
//          fallback path; its twin is W-A;
//   (b-L)  W-A with a planted frame over, and under, the recycler's size limit;
//   (b-S)  W-A with a planted nest that, with the pending read's r frames, is one over,
//          or exactly, the slot count;
//   (s)    W-A driven by a `run_one_for` loop on the test thread, T044's shape; it must
//          allocate whatever the tree, because every handler starts a scheduler call.
//
// TWO BINARIES FROM THIS SOURCE. `mallocnesia.c` refuses a process that defines the
// allocator names, so the interceptor runs a build without a counter, and the build with
// FIXPP_B35_TU_COUNTER replaces global operator new and counts it (T044's counter, the
// platform-portable half). The interceptor registrations are Release-only, one gtest case
// each (`alloc_guard_end()` exits on any interception).
//
// Every cell asserts its post-conditions after `Rig::stop()`: the session reached Active,
// every frame of the warm-up and of the window was processed, the session is still Active
// with the expected NextNumIn, and the cell's own hook ran once per frame. A window that
// never reached the pump cannot pass.

#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <memory>
#include <new>
#include <span>
#include <string>
#include <vector>

#include "session/session_strand.hpp"  // fixpp#544 §2.1: the target-type oracle
#include "support/recycler_driver.hpp"
#include "support/run_thread_engine_rig.hpp"
#include "support/session_test_access.hpp"

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

#if defined(FIXPP_B35_TU_COUNTER) && !FIXPP_SANITIZER_REPLACES_NEW
#define FIXPP_B35_COUNTS_NEW 1
#else
#define FIXPP_B35_COUNTS_NEW 0
#endif

#if FIXPP_B35_COUNTS_NEW

// Counts global operator new calls, from every thread, while armed.
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables) — the counter's state
static std::atomic<std::size_t> g_new_count{0};
static std::atomic<bool> g_arming{false};
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

// NOLINTBEGIN(cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,hicpp-no-malloc)
// NOLINTNEXTLINE(cert-dcl58-cpp) — replacing global operator new/delete is intentional
void* operator new(std::size_t n) {
    if (g_arming.load(std::memory_order_relaxed)) {
        g_new_count.fetch_add(1, std::memory_order_relaxed);
    }
    void* p = std::malloc(n == 0 ? 1 : n);
    if (!p) throw std::bad_alloc{};
    return p;
}

// NOLINTNEXTLINE(cert-dcl58-cpp)
void* operator new[](std::size_t n) {
    if (g_arming.load(std::memory_order_relaxed)) {
        g_new_count.fetch_add(1, std::memory_order_relaxed);
    }
    void* p = std::malloc(n == 0 ? 1 : n);
    if (!p) throw std::bad_alloc{};
    return p;
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif
// NOLINTNEXTLINE(cert-dcl58-cpp)
void operator delete(void* p) noexcept { std::free(p); }
// NOLINTNEXTLINE(cert-dcl58-cpp)
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
// NOLINTNEXTLINE(cert-dcl58-cpp)
void operator delete[](void* p) noexcept { std::free(p); }
// NOLINTNEXTLINE(cert-dcl58-cpp)
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
// NOLINTEND(cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,hicpp-no-malloc)

#endif  // FIXPP_B35_COUNTS_NEW

namespace {

namespace rt = fixpp::test_support::run_thread_rig;
namespace rc = fixpp::test_support::recycler;
using fixpp::session::fsm_state;

// K: frames processed before the window opens. M: frames whose read cycle lies wholly
// inside the window; the window writes M + 1.
constexpr int kWarmFrames = 8;
constexpr int kMeasuredFrames = 20;

// A reading run may set FIXPP_B35_MEASURED to change M, so that a per-frame rate can be
// told from a fixed cost by two readings.
int measured_frames() {
#ifndef _WIN32
    if (char const* v = std::getenv("FIXPP_B35_MEASURED")) {
        int const n = std::atoi(v);
        if (n > 0) return n;
    }
#endif
    return kMeasuredFrames;
}

void open_window() {
    if (alloc_guard_start) alloc_guard_start();
#if FIXPP_B35_COUNTS_NEW
    g_new_count.store(0, std::memory_order_relaxed);
    g_arming.store(true, std::memory_order_relaxed);
#endif
}

// Returns the operator new count, or 0 in a build without the counter.
std::size_t close_window() {
    std::size_t n = 0;
#if FIXPP_B35_COUNTS_NEW
    g_arming.store(false, std::memory_order_relaxed);
    n = g_new_count.load(std::memory_order_relaxed);
#endif
    if (alloc_guard_end) alloc_guard_end();
    return n;
}

// What the operator new count of a cell must show. `zero` is a window, which makes no
// operator new call; `allocates` is an arm, which allocates on every tree. Debug builds
// print the count and assert nothing (R-4: zero is gated on Release only).
enum class tu_expect : std::uint8_t { print_only, zero, allocates };

void check_tu_count([[maybe_unused]] std::size_t n, [[maybe_unused]] tu_expect e,
                    [[maybe_unused]] char const* cell) {
#if FIXPP_B35_COUNTS_NEW
    std::printf("[b35] %s: global operator new calls in the window = %zu\n", cell, n);
#if defined(NDEBUG)
    if (e == tu_expect::zero) {
        EXPECT_EQ(n, 0U) << cell << ": global operator new calls in the window";
    } else if (e == tu_expect::allocates) {
        EXPECT_GT(n, 0U) << cell << ": the counter saw no operator new call in the window";
    }
#endif
#endif
}

struct window_result {
    bool up = false;
    bool warmed = false;
    bool window_done = false;
    bool stopped = false;
    std::uint64_t completions = 0;
    std::uint64_t heartbeats = 0;
    std::uint64_t app_messages = 0;
    std::uint64_t vetoed = 0;
    std::uint64_t loop_ok = 0;
    std::size_t peer_app_frames = 0;
    rt::session_snapshot snap;
    std::size_t tu_count = 0;
    int warm = 0;
    int measured = 0;
    // The target-type oracle (§2.1), read on the session strand after the window: the
    // session executor's target is the fast-path strand, or the fallback strand. Each is
    // -1 when the read did not run.
    int fast_path_strand = -1;
    int fallback_strand = -1;
};

enum class inbound : std::uint8_t { heartbeat, new_order };

// A body a woken loop runs: a plant, or a send.
enum class loop_kind : std::uint8_t { none, plant, sender };

struct cell_spec {
    rt::options opt;
    inbound frames = inbound::heartbeat;
    loop_kind loop = loop_kind::none;
    bool veto_one_before_window = false;
    // Arm (e): the test holds the send slot, on the session strand, before the warm-up.
    bool hold_send_slot = false;
};

// Runs one window: setup, K warm-up frames, the window over M + 1 frames, a strand-side
// state read, teardown. `make_body(rig)` builds the woken loop's body, when the cell has
// one; it lives on this frame until the rig has stopped.
template <class MakeBody>
window_result run_window(cell_spec const& spec, MakeBody make_body) {
    window_result r;
    r.warm = kWarmFrames;
    r.measured = measured_frames();
    rt::Rig rig{spec.opt};

    // Every frame is built before the run thread starts. Seq 1 is the Logon.
    int const veto_frames = spec.veto_one_before_window ? 1 : 0;
    int const total = r.warm + veto_frames + r.measured + 1;
    std::vector<std::string> frames;
    frames.reserve(static_cast<std::size_t>(total));
    for (int i = 0; i < total; ++i) {
        auto const seq = static_cast<std::uint32_t>(2 + i);
        frames.push_back(spec.frames == inbound::heartbeat ? rig.heartbeat(seq)
                                                           : rig.new_order(seq));
    }
    std::vector<std::byte> const payload = rig.send_payload();

    auto& done = rig.app->completions;
    r.up = rig.start_and_logon();
    auto sess = rig.session();
    auto body = make_body(rig, sess.get(), std::span<const std::byte>{payload});
    if (r.up && sess && spec.loop != loop_kind::none) r.up = rig.start_woken_loop(body);
    if (r.up && sess && spec.hold_send_slot) {
        r.up = rig.on_strand(
            [sess] { fixpp::session::session_test_access::send_slot_in_use(*sess) = true; });
    }

    int next = 0;
    if (r.up) {
        r.warmed = true;
        for (int i = 0; i < r.warm && r.warmed; ++i, ++next) {
            r.warmed = rig.write_and_wait(frames[next], done, static_cast<std::uint64_t>(next + 1));
        }
    }
    if (r.warmed && spec.veto_one_before_window) {
        r.warmed = rig.set_veto(1) &&
                   rig.write_and_wait(frames[next], done, static_cast<std::uint64_t>(next + 1));
        ++next;
    }
    if (r.warmed) {
        bool ok = true;
        open_window();
        for (int j = 0; j <= r.measured && ok; ++j, ++next) {
            ok = rig.write_and_wait(frames[next], done, static_cast<std::uint64_t>(next + 1));
        }
        r.tu_count = close_window();
        r.window_done = ok;
    }
    if (spec.loop == loop_kind::sender) {
        // The sends' bytes reach the peer after their writes complete; wait for them,
        // outside the window, before reading the count.
        auto const want = static_cast<std::size_t>(rig.loop_ok.load());
        (void)rig.wait_until([&] { return rig.reader.app_frames.load() >= want; });
    }
    r.snap = rig.observe();
    if (sess) {
        // Shared, so a handler still queued after a missed wait writes into live state.
        auto oracle = std::make_shared<std::pair<std::atomic<int>, std::atomic<int>>>(-1, -1);
        (void)rig.on_strand([oracle, sess] {
            asio::any_io_executor const ex = sess->executor().underlying();
            using fast_t = asio::strand<fixpp::session::detail::session_inner_executor_t>;
            oracle->first = ex.target<fast_t>() != nullptr ? 1 : 0;
            oracle->second = ex.target<asio::strand<asio::any_io_executor>>() != nullptr ? 1 : 0;
        });
        r.fast_path_strand = oracle->first.load();
        r.fallback_strand = oracle->second.load();
    }
    r.stopped = rig.stop();
    r.completions = done.load();
    r.heartbeats = rig.app->heartbeats.load();
    r.app_messages = rig.app->app_messages.load();
    r.vetoed = rig.app->vetoed.load();
    r.loop_ok = rig.loop_ok.load();
    r.peer_app_frames = rig.reader.app_frames.load();
    return r;
}

window_result run_window(cell_spec const& spec) {
    // No woken loop: the body is never started, but it must be a valid one.
    return run_window(spec, [](rt::Rig&, fixpp::session::Session*, std::span<const std::byte>) {
        return rc::nest_plant<0>{};
    });
}

// The post-conditions every cell shares.
void expect_window_ran(window_result const& r, int extra_frames = 0) {
    ASSERT_TRUE(r.up) << "the session did not reach Active";
    ASSERT_TRUE(r.warmed) << "a warm-up frame was not processed";
    EXPECT_TRUE(r.window_done) << "a frame written inside the window was not processed";
    EXPECT_TRUE(r.stopped) << "Engine::stop() did not complete";
    auto const frames = static_cast<std::uint64_t>(r.warm + extra_frames + r.measured + 1);
    EXPECT_EQ(r.completions, frames) << "the completion signal did not fire once per frame";
    ASSERT_TRUE(r.snap.read) << "the strand-side state read did not run";
    EXPECT_EQ(r.snap.state, fsm_state::Active);
    EXPECT_EQ(r.snap.next_inbound, static_cast<fixpp::session::seqnum_t>(2 + frames))
        << "NextNumIn does not show every frame processed";
}

// ── W-A ──────────────────────────────────────────────────────────────────────

TEST(B35RunThreadWindows, WA_ActiveHeartbeat) {
    auto const r = run_window(cell_spec{.opt = {.mode = rt::hook::signal_from_admin}});
    expect_window_ran(r);
    EXPECT_EQ(r.heartbeats, r.completions);
    EXPECT_EQ(r.fast_path_strand, 1) << "the session strand is not the fast-path strand";
    check_tu_count(r.tu_count, tu_expect::zero, "W-A");
}

// ── W-B ──────────────────────────────────────────────────────────────────────

TEST(B35RunThreadWindows, WB_ActiveAppMessage) {
    auto const r =
        run_window(cell_spec{.opt = {.mode = rt::hook::signal_from_app, .validating = true},
                             .frames = inbound::new_order});
    expect_window_ran(r);
    EXPECT_TRUE(r.snap.has_validator) << "validation is off, so the window never validated";
    EXPECT_EQ(r.app_messages, r.completions) << "a NewOrderSingle did not reach fromApp";
    EXPECT_EQ(r.fast_path_strand, 1) << "the session strand is not the fast-path strand";
    check_tu_count(r.tu_count, tu_expect::zero, "W-B");
}

// ── W-E and the veto-then-zero cell ──────────────────────────────────────────

auto make_sender = [](rt::Rig&, fixpp::session::Session* s, std::span<const std::byte> payload) {
    return rt::send_body{.session = s, .payload = payload};
};

cell_spec sender_spec(bool veto) {
    return cell_spec{.opt = {.mode = rt::hook::wake_from_app, .validating = true},
                     .frames = inbound::new_order,
                     .loop = loop_kind::sender,
                     .veto_one_before_window = veto};
}

TEST(B35RunThreadWindows, WE_InboundPairInterleavedWithSends) {
    auto const r = run_window(sender_spec(false), make_sender);
    expect_window_ran(r);
    EXPECT_EQ(r.app_messages, r.completions);
    EXPECT_EQ(r.loop_ok, r.completions) << "a send in the window did not succeed";
    EXPECT_GE(r.peer_app_frames, r.loop_ok) << "a successful send did not reach the peer";
    check_tu_count(r.tu_count, tu_expect::print_only, "W-E");
}

TEST(B35RunThreadWindows, VetoThenZero_SendsAfterAVetoedSend) {
    auto const r = run_window(sender_spec(true), make_sender);
    expect_window_ran(r, /*extra_frames=*/1);
    EXPECT_EQ(r.vetoed, 1U) << "toApp did not veto the send before the window";
    EXPECT_EQ(r.loop_ok, r.completions - 1) << "a send after the vetoed one did not succeed";
    EXPECT_GE(r.peer_app_frames, r.loop_ok);
    check_tu_count(r.tu_count, tu_expect::print_only, "veto-then-zero");
}

// ── Arm (e): every send takes the fallback leaf ──────────────────────────────

TEST(B35RunThreadWindows, ArmE_SlotHeldSendsTakeTheLeaf) {
    cell_spec spec = sender_spec(false);
    spec.hold_send_slot = true;
    auto const r = run_window(spec, make_sender);
    expect_window_ran(r);
    EXPECT_EQ(r.app_messages, r.completions);
    EXPECT_EQ(r.loop_ok, r.completions) << "a send in the window did not succeed";
    EXPECT_GE(r.peer_app_frames, r.loop_ok) << "a successful send did not reach the peer";
    check_tu_count(r.tu_count, tu_expect::print_only, "arm (e)");
}

// ── Arm (a): the strand's fallback executor ──────────────────────────────────

// The entry is the allocator the arm's class reaches (design §1): class (a) through global
// operator new (`new impl<E>` in asio's shared_target_executor), which reaches `malloc`; the
// recycler classes through its fall-through, which reaches `aligned_alloc`.
TEST(B35RunThreadWindows, ArmA_TrackedExecutorFallback) {
    auto const r = run_window(
        cell_spec{.opt = {.mode = rt::hook::signal_from_admin, .tracked_executor = true}});
    expect_window_ran(r);
    // The oracle half (T1.7): a work-tracked executor's target type is not
    // io_context::executor_type, so make_session_strand takes the fallback.
    EXPECT_EQ(r.fast_path_strand, 0) << "a tracked executor reached the fast-path strand";
    EXPECT_EQ(r.fallback_strand, 1) << "the session strand is not the fallback strand";
    check_tu_count(r.tu_count, tu_expect::allocates, "arm (a)");
}

// ── W-A's (b-L) and (b-S) pairs, planted through a launcher woken by fromAdmin ──

inline constexpr std::size_t kOverLimitPlantBytes = 2 * rc::kRecycleLimit;
// A parameter: re-derive it per compiler from the arm's log, as in W-D's harness.
inline constexpr std::size_t kInLimitPlantBytes = rc::kRecycleLimit / 4;

template <class Plant>
window_result run_planted() {
    return run_window(
        cell_spec{.opt = {.mode = rt::hook::wake_from_admin}, .loop = loop_kind::plant},
        [](rt::Rig&, fixpp::session::Session* s, std::span<const std::byte>) {
            return Plant{.ex = s != nullptr ? s->executor().underlying() : asio::any_io_executor{}};
        });
}

void expect_planted_ran(window_result const& r) {
    expect_window_ran(r);
    EXPECT_EQ(r.loop_ok, r.completions) << "a plant iteration did not complete";
    EXPECT_EQ(r.heartbeats, r.completions);
}

TEST(B35RunThreadWindows, WA_BL_Arm_OverLimitPlant) {
    expect_planted_ran(run_planted<rc::buffer_plant<kOverLimitPlantBytes>>());
}

TEST(B35RunThreadWindows, WA_BL_Twin_InLimitPlant) {
    expect_planted_ran(run_planted<rc::buffer_plant<kInLimitPlantBytes>>());
}

// Shifted by r: the pending read holds r cycled frames while the plant runs, so a nest of
// N - r fills the slots exactly and one frame more is over them.
inline constexpr int kShiftedTwinNest = rc::kCacheSize - rc::kPendingReadCycledFrames;

TEST(B35RunThreadWindows, WA_BS_Arm_NestOneOverSlots) {
    expect_planted_ran(run_planted<rc::nest_plant<kShiftedTwinNest + 1>>());
}

TEST(B35RunThreadWindows, WA_BS_Twin_NestFillsSlots) {
    expect_planted_ran(run_planted<rc::nest_plant<kShiftedTwinNest>>());
}

// ── Arm (s): the scope control ───────────────────────────────────────────────

TEST(B35RunThreadWindows, ArmS_RunOneForScope) {
    auto const r = run_window(
        cell_spec{.opt = {.mode = rt::hook::signal_from_admin, .driven_by_test_thread = true}});
    expect_window_ran(r);
    check_tu_count(r.tu_count, tu_expect::print_only, "arm (s)");
}

}  // namespace
