// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/perf/test_099_fixt_inbound_alloc_guard.cpp
//
// 099-fixt-appl-version-validation, contracts/witnesses.md W-16 (NFR-002, `[const §VIII.5]`): shape
// T, validated and not, an inbound NewOrderSingle on a non-own slot, through a real Session to
// fromApp, draws nothing from the global heap once the session has warmed up.
//
// Modelled on tests/perf/test_session_recovery_alloc_guard.cpp's window and on
// tests/alloc_guard/test_544_run_thread_windows.cpp's two binaries: the run-thread rig, since a
// window across several scheduler calls cannot read zero, and every frame built before the rig
// starts. Under the interceptor, tools/check_alloc.py requires the interceptor's own witness, and
// alloc_guard_end() exits on any interception. The counter build, FIXPP_099_TU_COUNTER, is the
// reading a sanitizer-free build of any type can make without the preload.
//
// Every cell asserts after Rig::stop(): the session reached Active, each frame reached fromApp, and
// NextNumIn shows each processed, so a window that never ran the route cannot pass.

#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/version_profile.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <memory>
#include <memory_resource>
#include <new>
#include <string>
#include <utility>
#include <vector>

#include "support/alloc_guard_markers.hpp"
#include "support/run_thread_engine_rig.hpp"

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

#if defined(FIXPP_099_TU_COUNTER) && !FIXPP_SANITIZER_REPLACES_NEW
#define FIXPP_099_COUNTS_NEW 1
#else
#define FIXPP_099_COUNTS_NEW 0
#endif

#if FIXPP_099_COUNTS_NEW

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

#endif  // FIXPP_099_COUNTS_NEW

namespace {

namespace rt = fixpp::test_support::run_thread_rig;
namespace dd = fixpp::dict;

using DictPtr = std::shared_ptr<const dd::Dictionary>;

constexpr int kWarmFrames = 8;
constexpr int kMeasuredFrames = 20;

// A vendored file, loaded once per process and never freed: the session and the set read it after
// the rig's teardown has begun.
DictPtr load_vendored(char const* name) {
    static std::pmr::monotonic_buffer_resource mr;
    return std::make_shared<const dd::Dictionary>(
        dd::XmlLoader{}.load(std::filesystem::path{FIXPP_DICT_DATA_DIR} / name, &mr));
}

struct window_result {
    bool up = false;
    bool warmed = false;
    bool window_done = false;
    bool stopped = false;
    std::uint64_t completions = 0;
    std::uint64_t app_messages = 0;
    rt::session_snapshot snap;
    std::size_t new_calls = 0;
};

// S is FIXT11 and the slot FIX 5.0 SP2's, so the route's table is not S's, and a validated frame
// passes only on that route: S's table answers tag 8.
window_result run_window(bool validating) {
    static DictPtr const fixt11 = load_vendored("FIXT11.xml");
    static DictPtr const fix50sp2 = load_vendored("FIX50SP2.xml");

    rt::options opt{.mode = rt::hook::signal_from_app};
    opt.logon_extra = "1137=9\x01";
    opt.shape_engine = [](fixpp::core::EngineConfig& e) { e.dictionaries = {fixt11, fix50sp2}; };
    opt.shape_session = [validating](fixpp::session::SessionConfig& c) {
        c.dictionary = fixt11;
        c.default_appl_ver_id = dd::application_version::v50sp2;
        c.validate_inbound_messages = validating;
    };

    window_result r;
    rt::Rig rig{std::move(opt)};
    rig.begin_string = "FIXT.1.1";

    int const total = kWarmFrames + kMeasuredFrames + 1;
    std::vector<std::string> frames;
    frames.reserve(static_cast<std::size_t>(total));
    for (int i = 0; i < total; ++i) {
        auto const seq = static_cast<std::uint32_t>(2 + i);
        frames.push_back(rig.msg("D", seq,
                                 "1128=9\x01"
                                 "11=C" +
                                     std::to_string(seq) +
                                     "\x01"
                                     "21=1\x01"
                                     "55=X\x01"
                                     "54=1\x01"
                                     "60=20240101-00:00:00\x01"
                                     "40=1\x01"
                                     "38=1\x01"));
    }

    auto& done = rig.app->completions;
    r.up = rig.start_and_logon();
    int next = 0;
    if (r.up) {
        r.warmed = true;
        for (int i = 0; i < kWarmFrames && r.warmed; ++i, ++next) {
            r.warmed = rig.write_and_wait(frames[next], done, static_cast<std::uint64_t>(next + 1));
        }
    }
    if (r.warmed) {
        bool ok = true;
        if (alloc_guard_start) alloc_guard_start();
#if FIXPP_099_COUNTS_NEW
        g_new_count.store(0, std::memory_order_relaxed);
        g_arming.store(true, std::memory_order_relaxed);
#endif
        for (int j = 0; j <= kMeasuredFrames && ok; ++j, ++next) {
            ok = rig.write_and_wait(frames[next], done, static_cast<std::uint64_t>(next + 1));
        }
#if FIXPP_099_COUNTS_NEW
        g_arming.store(false, std::memory_order_relaxed);
        r.new_calls = g_new_count.load(std::memory_order_relaxed);
#endif
        if (alloc_guard_end) alloc_guard_end();
        r.window_done = ok;
    }
    r.snap = rig.observe();
    r.stopped = rig.stop();
    r.completions = done.load();
    r.app_messages = rig.app->app_messages.load();
    return r;
}

void expect_window(window_result const& r, bool validating) {
    ASSERT_TRUE(r.up) << "the session did not reach Active";
    ASSERT_TRUE(r.warmed) << "a warm-up frame did not reach fromApp";
    EXPECT_TRUE(r.window_done) << "a frame written inside the window did not reach fromApp";
    EXPECT_TRUE(r.stopped) << "Engine::stop() did not complete";
    auto const frames = static_cast<std::uint64_t>(kWarmFrames + kMeasuredFrames + 1);
    EXPECT_EQ(r.completions, frames);
    EXPECT_EQ(r.app_messages, frames) << "a NewOrderSingle was not delivered";
    ASSERT_TRUE(r.snap.read) << "the strand-side state read did not run";
    EXPECT_EQ(r.snap.state, fixpp::session::fsm_state::Active);
    EXPECT_EQ(r.snap.next_inbound, static_cast<fixpp::session::seqnum_t>(2 + frames));
    EXPECT_EQ(r.snap.has_validator, validating);
#if FIXPP_099_COUNTS_NEW
    EXPECT_EQ(r.new_calls, 0U) << "global operator new calls in the marked region";
#endif
}

TEST(W16NonOwnSlot, Validated) { expect_window(run_window(/*validating=*/true), true); }

TEST(W16NonOwnSlot, NotValidated) { expect_window(run_window(/*validating=*/false), false); }

}  // namespace
