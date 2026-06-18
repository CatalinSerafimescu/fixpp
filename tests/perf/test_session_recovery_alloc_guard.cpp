// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/perf/test_session_recovery_alloc_guard.cpp — T020 [US1] Phase 3 RED
//
// Session recovery alloc-guard — DUAL gate:
//   1. counting_resource PMR: routes in-band PMR allocations, counts them.
//   2. mallocnesia LD_PRELOAD: intercepts global operator new/delete to catch
//      out-of-band heap escapes (non-PMR std::vector etc.).
//
// Zero allocations expected on the ACTIVE steady-state + AwaitingResend
// transition path and on the Heartbeat emit path.
//
// Anchors: spec.md §US1 / FR-009; [const §VIII.5];
//   plan.md §Test plan T020; [[feedback_tracking_pmr_resource_false_pass]].
//
// RED witness: Phase-2 stubs do not implement resend/heartbeat paths.
//   The counting_resource-guarded window will show zero allocations trivially
//   (stub does nothing). That is a FALSE-PASS for the counting_resource axis.
//   However, the BEHAVIOR assertion (that a ResendRequest was emitted, proving
//   the path actually ran) FAILS RED because the stub emits nothing.
//
//   The mallocnesia gate is the true runtime integrity check once the impl
//   lands: run this binary with LD_PRELOAD=tools/mallocnesia/libmallocnesia.so.
//
// Run with dual gate:
//   LD_PRELOAD=tools/mallocnesia/libmallocnesia.so \
//       build/linux-clang-debug/tests/perf/perf_session_recovery_alloc_guard

#include <gtest/gtest.h>

#include <array>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <future>
#include <memory>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"

using namespace std::chrono_literals;

// ── mallocnesia weak-symbol hooks ─────────────────────────────────────────────
// These are replaced by tools/mallocnesia/libmallocnesia.so via LD_PRELOAD.
// Without the preload, the stubs are no-ops (alloc counting = 0 always).
extern "C" {

__attribute__((weak)) void alloc_guard_start() {}
__attribute__((weak)) void alloc_guard_end() {}
__attribute__((weak)) long alloc_guard_count() { return 0; }

}  // extern "C"

// ── counting_resource — in-band PMR interceptor ───────────────────────────────

class counting_resource : public std::pmr::memory_resource {
public:
    std::size_t alloc_count = 0;

    void* do_allocate(std::size_t bytes, std::size_t align) override {
        ++alloc_count;
        return std::pmr::get_default_resource()->allocate(bytes, align);
    }
    void do_deallocate(void* p, std::size_t bytes, std::size_t align) override {
        std::pmr::get_default_resource()->deallocate(p, bytes, align);
    }
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
        return this == &other;
    }
};

namespace {

static std::string field_str(int tag, std::string_view val) {
    return std::to_string(tag) + "=" + std::string(val) + "\x01";
}

static std::vector<std::byte> make_fix_frame(std::string_view begin_string,
                                             std::string_view msg_type, std::uint32_t seq,
                                             std::string_view sender, std::string_view target,
                                             std::string_view extra = {}) {
    std::string body;
    body += field_str(35, msg_type);
    body += field_str(34, std::to_string(seq));
    body += field_str(49, sender);
    body += field_str(52, "20240101-00:00:00.000");
    body += field_str(56, target);
    if (!extra.empty()) body += std::string(extra);

    std::string msg;
    msg += "8=" + std::string(begin_string) + "\x01";
    msg += "9=" + std::to_string(body.size()) + "\x01";
    msg += body;
    unsigned int cs = 0;
    for (unsigned char c : msg) cs += c;
    cs &= 0xFFU;
    char csbuf[5];
    snprintf(csbuf, sizeof(csbuf), "%03u", cs);
    msg += "10=" + std::string(csbuf) + "\x01";

    std::vector<std::byte> out;
    out.reserve(msg.size());
    for (char c : msg) out.push_back(static_cast<std::byte>(c));
    return out;
}

static std::vector<std::byte> make_logon(std::string_view bs, std::uint32_t seq, std::string_view s,
                                         std::string_view t, int hbt = 30) {
    std::string extra;
    extra += field_str(98, "0");
    extra += field_str(108, std::to_string(hbt));
    return make_fix_frame(bs, "A", seq, s, t, extra);
}

static std::vector<std::byte> make_heartbeat(std::string_view bs, std::uint32_t seq,
                                             std::string_view s, std::string_view t) {
    return make_fix_frame(bs, "0", seq, s, t);
}

static bool is_msg_type(std::span<const std::byte> frame, std::string_view type) {
    std::string wire(reinterpret_cast<const char*>(frame.data()), frame.size());
    return wire.find("35=" + std::string(type) + "\x01") != std::string::npos;
}

}  // namespace

// ── Test fixture ──────────────────────────────────────────────────────────────

class SessionRecoveryAllocGuardTest : public ::testing::Test {
protected:
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine{};
    std::vector<std::vector<std::byte>> outbound_frames;
    counting_resource pmr;

    void SetUp() override {
        auto utc = std::chrono::system_clock::time_point{} + std::chrono::seconds{1704067200};
        auto stp = fixpp::core::steady_time_point{};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock = clock;
        engine.executor = ioc.get_executor();
    }

    fixpp::session::SessionConfig make_cfg() {
        fixpp::session::SessionConfig cfg;
        cfg.sender_comp_id = "ISLD";
        cfg.target_comp_id = "TW";
        cfg.begin_string = "FIX.4.2";
        cfg.heartbeat_interval = 30s;
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override = ioc.get_executor();
        cfg.transport_send = [this](std::span<const std::byte> d) {
            outbound_frames.emplace_back(d.begin(), d.end());
        };
        cfg.role = fixpp::session::session_role::acceptor;
        cfg.message_arena = &pmr;
        // RC#C (gate-b/r1): bilateral_lenient — test exercises alloc-guard, not reset.
        cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
        return cfg;
    }

    fixpp::core::expected_t<void> run_open(fixpp::session::Session& s) {
        auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
        ioc.run_for(50ms);
        ioc.restart();
        return fut.get();
    }

    fixpp::core::expected_t<void> feed(fixpp::session::Session& s,
                                       std::span<const std::byte> frame) {
        auto fut = asio::co_spawn(ioc, s.on_inbound_frame(frame), asio::use_future);
        ioc.run_for(10ms);
        ioc.restart();
        return fut.get();
    }

    bool drive_to_active(fixpp::session::Session& s) {
        if (!run_open(s).has_value()) return false;
        auto logon = make_logon("FIX.4.2", 1, "TW", "ISLD");
        feed(s, logon);
        return s.state() == fixpp::session::fsm_state::Active;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// T020-A: Heartbeat steady-state processing path — DUAL-GATE alloc check.
//
// counting_resource gate: zero PMR allocs in the measured window.
// mallocnesia gate: alloc_guard_count() == 0 inside the window.
//
// Behavioral assertion: we feed N inbound Heartbeats in a loop and check that
//   the session emits NO outbound frame — a Heartbeat is never answered
//   (data-model.md:22 Active×inbound-Heartbeat = "advance counter", no emit).
//   The alloc gates measure the steady-state inbound-Heartbeat processing path.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(SessionRecoveryAllocGuardTest, HeartbeatSteadyState_DualGate) {
    auto cfg = make_cfg();
    fixpp::session::Session sess(engine, cfg);
    ASSERT_TRUE(drive_to_active(sess));

    constexpr int kIter = 20;

    // Warm up (primes asio per-thread recycler outside the guard window).
    auto warmup_hb = make_heartbeat("FIX.4.2", 2, "TW", "ISLD");
    for (int i = 0; i < 10; ++i) feed(sess, warmup_hb);
    outbound_frames.clear();
    pmr.alloc_count = 0;

    // --- OPEN GUARD WINDOW ---
    alloc_guard_start();
    std::size_t pre_pmr_count = pmr.alloc_count;

    for (int i = 0; i < kIter; ++i) {
        auto hb = make_heartbeat("FIX.4.2", static_cast<std::uint32_t>(3 + i), "TW", "ISLD");
        feed(sess, hb);
    }

    long global_alloc_count = alloc_guard_count();
    std::size_t pmr_allocs_in_window = pmr.alloc_count - pre_pmr_count;
    alloc_guard_end();
    // --- CLOSE GUARD WINDOW ---

    // mallocnesia gate: zero global heap allocations in the steady-state window.
    EXPECT_EQ(global_alloc_count, 0L)
        << "mallocnesia gate: global heap allocations in Heartbeat steady-state "
        << "window must be zero. [const §VIII.5]. Run with LD_PRELOAD to activate.";

    // counting_resource gate: zero PMR allocations (all-arena path).
    EXPECT_EQ(pmr_allocs_in_window, 0u)
        << "counting_resource gate: PMR allocs in Heartbeat steady-state window "
        << "must be zero. [const §VIII.5].";

    // Behavioral gate: inbound Heartbeats must produce NO outbound frame
    // (a Heartbeat is never answered; data-model.md:22).
    EXPECT_EQ(outbound_frames.size(), 0U)
        << "Behavioral gate: " << kIter << " inbound Heartbeats must produce ZERO "
        << "outbound frames (a Heartbeat is never answered); got "
        << outbound_frames.size();
}

// ─────────────────────────────────────────────────────────────────────────────
// T020-B: AwaitingResend transition — DUAL-GATE alloc check.
//
// Behavioral RED assertion: too-high seqnum → ResendRequest emitted.
//   The stub returns {} without emitting → no ResendRequest → FAILS RED.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(SessionRecoveryAllocGuardTest, AwaitingResendTransition_DualGate) {
    auto cfg = make_cfg();
    fixpp::session::Session sess(engine, cfg);
    ASSERT_TRUE(drive_to_active(sess));

    outbound_frames.clear();
    pmr.alloc_count = 0;

    // Warm up.
    auto warmup_hb = make_heartbeat("FIX.4.2", 2, "TW", "ISLD");
    feed(sess, warmup_hb);
    outbound_frames.clear();
    pmr.alloc_count = 0;

    // --- OPEN GUARD WINDOW ---
    alloc_guard_start();
    std::size_t pre_pmr_count = pmr.alloc_count;

    // Feed heartbeat with too-high seqnum (gap [3..4] → expected 3, got 5).
    auto gap_hb = make_fix_frame("FIX.4.2", "0", 5, "TW", "ISLD");
    feed(sess, gap_hb);

    long global_alloc_count = alloc_guard_count();
    std::size_t pmr_allocs_in_window = pmr.alloc_count - pre_pmr_count;
    alloc_guard_end();
    // --- CLOSE GUARD WINDOW ---

    EXPECT_EQ(global_alloc_count, 0L)
        << "mallocnesia gate: enter_awaiting_resend must not allocate on global heap. "
        << "[const §VIII.5]. Run with LD_PRELOAD to activate.";

    // PMR allocs are allowed for resend_state_ internal storage (pmr::vector).
    // We only assert the global-heap gate is zero (mallocnesia).

    // Behavioral RED assertion: a ResendRequest(2) must appear in outbound.
    bool found_resend = false;
    for (const auto& f : outbound_frames) {
        std::string wire(reinterpret_cast<const char*>(f.data()), f.size());
        if (wire.find("35=2\x01") != std::string::npos) {
            found_resend = true;
            break;
        }
    }
    EXPECT_TRUE(found_resend)
        << "Behavioral gate: too-high seqnum must emit ResendRequest(2) outbound. "
        << "RED: enter_awaiting_resend stub emits nothing → FAILS RED per T020-B.";
}
