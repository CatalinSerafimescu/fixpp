// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/perf/test_session_recovery_alloc_guard.cpp — T020 [US1] Phase 3 RED
//
// Session recovery alloc-guard. Checked: the counting_resource PMR half (in-band PMR
// allocations, counted) and the behaviour of each path.
//
// The global-heap half. [const §VIII.5] asks the ACTIVE steady-state and AwaitingResend
// transition paths for zero global-heap allocation. The Heartbeat steady-state cell is
// fixpp#544's W-A window, and its interceptor registration is Release-only
// (tests/perf/CMakeLists.txt). ⚠️ The AwaitingResend cell's global-heap half is NOT
// CHECKED: its window is resend/gap mixed traffic, which fixpp#544 discloses rather than
// gates (L-497-1). Its alloc_guard markers stay so the window can be run by hand, as
// tests/perf/CMakeLists.txt shows at this binary's registration.
//
// Anchors: spec.md §US1 / FR-009; [const §VIII.5];
//   plan.md §Test plan T020; [[feedback_tracking_pmr_resource_false_pass]].
//
// RED witness: Phase-2 stubs do not implement resend/heartbeat paths.
//   The counting_resource-guarded window will show zero allocations trivially
//   (stub does nothing). That is a FALSE-PASS for the counting_resource axis.
//   However, the BEHAVIOR assertion (that a ResendRequest was emitted, proving
//   the path actually ran) FAILS RED because the stub emits nothing.

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
// Defined by the interceptor when it is preloaded; no-ops otherwise.
#include "support/alloc_guard_markers.hpp"
#include "support/pump_until_ready.hpp"
#include "support/run_thread_engine_rig.hpp"

// ── #289: bounded pumps ──────────────────────────────────────────────────────
//
// Where a site in this file is migrated it uses `run_window_then_ready` plus a
// miss-branch drain (tests/support/pump_until_ready.hpp). The window is PRESERVED:
// the hazard #289 names is the UNCONDITIONAL `get()`, not the fixed window.
//
// The site label passed to `run_window_then_ready` is the FORCING SEAM: exporting
// FIXPP_FORCE_WINDOW_MISS=<label> makes exactly that site take its miss branch, with
// no source edit and no rebuild. It is a WEAKER witness than textual mutation and
// does not replace it -- see the primitive.
//
// Rationale and the teardown-shape rule live at the primitive, not duplicated here
// (#324).

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
    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
        return this == &other;
    }
};

namespace {

std::string field_str(int tag, std::string_view val) {
    return std::to_string(tag) + "=" + std::string(val) + "\x01";
}

std::vector<std::byte> make_fix_frame(std::string_view begin_string, std::string_view msg_type,
                                      std::uint32_t seq, std::string_view sender,
                                      std::string_view target, std::string_view extra = {}) {
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

std::vector<std::byte> make_logon(std::string_view bs, std::uint32_t seq, std::string_view s,
                                  std::string_view t, int hbt = 30) {
    std::string extra;
    extra += field_str(98, "0");
    extra += field_str(108, std::to_string(hbt));
    return make_fix_frame(bs, "A", seq, s, t, extra);
}

std::vector<std::byte> make_heartbeat(std::string_view bs, std::uint32_t seq, std::string_view s,
                                      std::string_view t) {
    return make_fix_frame(bs, "0", seq, s, t);
}

[[maybe_unused]] bool is_msg_type(std::span<const std::byte> frame, std::string_view type) {
    std::string wire(reinterpret_cast<const char*>(frame.data()), frame.size());
    return wire.contains("35=" + std::string(type) + "\x01");
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
        if (!fixpp::test_support::run_window_then_ready(
                ioc, fut, 50ms, "SessionRecoveryAllocGuardTest::run_open")) {
            fixpp::test_support::cancel_and_drain_or_report(
                ioc, *clock, "SessionRecoveryAllocGuardTest::run_open");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss
                          << "SessionRecoveryAllocGuardTest::run_open";
            return std::unexpected(fixpp::test_support::kWindowMissSentinel);
        }
        return fut.get();
    }

    fixpp::core::expected_t<void> feed(fixpp::session::Session& s,
                                       std::span<const std::byte> frame) {
        auto fut = asio::co_spawn(ioc, s.on_inbound_frame(frame), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, 10ms,
                                                        "SessionRecoveryAllocGuardTest::feed")) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock,
                                                            "SessionRecoveryAllocGuardTest::feed");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss
                          << "SessionRecoveryAllocGuardTest::feed";
            return std::unexpected(fixpp::test_support::kWindowMissSentinel);
        }
        return fut.get();
    }

    bool drive_to_active(fixpp::session::Session& s) {
        if (!run_open(s).has_value()) return false;
        auto logon = make_logon("FIX.4.2", 1, "TW", "ISLD");
        if (!feed(s, logon).has_value()) return false;
        return s.state() == fixpp::session::fsm_state::Active;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// T020-A: Heartbeat steady-state processing path — DUAL-GATE alloc check.
//
// counting_resource gate: zero PMR allocs in the window.
//
// Behavioral assertion: we feed inbound Heartbeats and check that the session emits NO
//   outbound frame — a Heartbeat is never answered
//   (specs/005-session-establishment-fsm/data-model.md's FSM table, Active×inbound-Heartbeat =
//   "advance counter", no emit). The alloc gates measure the steady-state inbound-Heartbeat
//   processing path.
//
// fixpp#544 (B35; `.specify/544-hot-path-zero-alloc.md` §3): this window is W-A's, and it
// runs on the run-thread rig (tests/support/run_thread_engine_rig.hpp): the real Engine read
// pump over loopback TCP, one dedicated thread running the io_context for the whole test,
// and every Heartbeat written by the peer after the previous one's fromAdmin. A window
// driven with `use_future` and `run_for` slices cannot read zero: each slice is a fresh
// scheduler call with an empty frame cache. Its global-heap half is registered under the
// interceptor, Release-only (tests/perf/CMakeLists.txt). The fixture's own session is not
// used by this cell.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(SessionRecoveryAllocGuardTest, HeartbeatSteadyState_DualGate) {
    namespace rt = fixpp::test_support::run_thread_rig;
    constexpr int kWarm = 10;
    constexpr int kIter = 20;

    rt::Rig rig{rt::options{.mode = rt::hook::signal_from_admin, .message_arena = &pmr}};
    std::vector<std::string> frames;
    for (int i = 0; i < kWarm + kIter; ++i) {
        frames.push_back(rig.heartbeat(static_cast<std::uint32_t>(2 + i)));
    }
    auto& done = rig.app->completions;

    bool const up = rig.start_and_logon();
    bool warmed = up;
    for (int i = 0; i < kWarm && warmed; ++i) {
        warmed = rig.write_and_wait(frames[i], done, static_cast<std::uint64_t>(i + 1));
    }

    // The arena is counted on the run thread, so its count is read on the session strand.
    std::size_t pmr_before = 0;
    std::size_t pmr_after = 0;
    std::size_t peer_bytes_before = 0;
    bool window_done = false;
    if (warmed && rig.on_strand([&] { pmr_before = pmr.alloc_count; })) {
        peer_bytes_before = rig.reader.bytes.load(std::memory_order_acquire);
        // --- OPEN GUARD WINDOW ---
        if (alloc_guard_start) alloc_guard_start();
        bool ok = true;
        for (int i = kWarm; i < kWarm + kIter && ok; ++i) {
            ok = rig.write_and_wait(frames[i], done, static_cast<std::uint64_t>(i + 1));
        }
        if (alloc_guard_end) alloc_guard_end();
        // --- CLOSE GUARD WINDOW ---
        window_done = ok && rig.on_strand([&] { pmr_after = pmr.alloc_count; });
    }
    std::size_t const peer_bytes_after = rig.reader.bytes.load(std::memory_order_acquire);
    auto const snap = rig.observe();
    bool const stopped = rig.stop();

    ASSERT_TRUE(up) << "the session did not reach Active";
    ASSERT_TRUE(warmed) << "a warm-up Heartbeat was not processed";
    ASSERT_TRUE(window_done) << "a Heartbeat written inside the window was not processed";
    EXPECT_TRUE(stopped) << "Engine::stop() did not complete";
    EXPECT_EQ(done.load(), static_cast<std::uint64_t>(kWarm + kIter));
    EXPECT_EQ(snap.state, fixpp::session::fsm_state::Active);
    EXPECT_EQ(snap.next_inbound, static_cast<fixpp::session::seqnum_t>(2 + kWarm + kIter));

    // counting_resource gate: zero PMR allocations (all-arena path).
    EXPECT_EQ(pmr_after - pmr_before, 0U)
        << "counting_resource gate: PMR allocs in Heartbeat steady-state window "
        << "must be zero. [const §VIII.5].";

    // Behavioral gate: inbound Heartbeats must produce NO outbound frame
    // (a Heartbeat is never answered; specs/005-session-establishment-fsm/data-model.md's FSM
    // table).
    EXPECT_EQ(peer_bytes_after, peer_bytes_before)
        << "Behavioral gate: " << kIter << " inbound Heartbeats must produce ZERO "
        << "outbound bytes (a Heartbeat is never answered)";
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
    (void)feed(sess, warmup_hb);  // priming only; not measured or asserted
    outbound_frames.clear();
    pmr.alloc_count = 0;

    // --- OPEN GUARD WINDOW ---
    if (alloc_guard_start) alloc_guard_start();
    std::size_t pre_pmr_count = pmr.alloc_count;

    // Feed heartbeat with too-high seqnum (gap [3..4] → expected 3, got 5).
    auto gap_hb = make_fix_frame("FIX.4.2", "0", 5, "TW", "ISLD");
    // Inside the alloc-measured window; outcome is checked below via the
    // emitted ResendRequest, not via feed's own result.
    (void)feed(sess, gap_hb);

    [[maybe_unused]] std::size_t pmr_allocs_in_window = pmr.alloc_count - pre_pmr_count;
    if (alloc_guard_end) alloc_guard_end();
    // --- CLOSE GUARD WINDOW ---

    // PMR allocs are allowed for resend_state_ internal storage (pmr::vector).

    // Behavioral RED assertion: a ResendRequest(2) must appear in outbound.
    bool found_resend = false;
    for (const auto& f : outbound_frames) {
        std::string wire(reinterpret_cast<const char*>(f.data()), f.size());
        if (wire.contains("35=2\x01")) {
            found_resend = true;
            break;
        }
    }
    EXPECT_TRUE(found_resend)
        << "Behavioral gate: too-high seqnum must emit ResendRequest(2) outbound. "
        << "RED: enter_awaiting_resend stub emits nothing → FAILS RED per T020-B.";
}
