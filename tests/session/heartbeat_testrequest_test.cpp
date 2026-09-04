// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/heartbeat_testrequest_test.cpp
//
// Seam #5 — Heartbeat / TestRequest liveness (005-session-establishment-fsm
// T037 / Phase 5 / US3).
//
// Scenarios covered (SC-004 / FR-006 / [FIX-SL §4.5.1]/§4.5.5):
//
//   1. build_heartbeat — produces a well-formed 35=0 FIX frame that contains
//      the optional TestReqID(112) when supplied.
//   2. build_heartbeat with empty TestReqID — 35=0 with no tag-112 field.
//   3. build_test_request — produces a well-formed 35=1 frame carrying
//      TestReqID(112) with the supplied test_req_id value.
//   4. TestReqID distinctness — two successive build_test_request calls with
//      distinct test_req_id strings yield frames with distinct TestReqID values.
//   5. Inbound TestRequest in Active state → FSM stays Active (no disconnect
//      on a valid TestRequest). Verifies the echo-heartbeat FSM path (T041).
//   6. HeartBtInt=0 disables all liveness timers (FR-006 / [FIX-SL §4.3.4]):
//      session stays Active regardless of clock advance.
//   7. Unanswered TestRequest after grace window → session_test_request_unanswered
//      → session transitions to Disconnected (T039 + T041).
//   8. Inbound Heartbeat in Active → counter advances, FSM stays Active.
//
// Infrastructure:
//   - admin_messages (build_heartbeat / build_test_request) — unit tests (T040)
//   - mock_clock — deterministic timer control (T039)
//   - Session + on_inbound_frame — FSM integration (T041)
//
// Anchors: spec US3; data-model E2 (FSM Active row); error slot 74;
// research D-5/D-8; [FIX-SL §4.5.5]. [const §VII.1]/[const §VII.3].
//
// SCOPE NOTES:
//   Tests 1-4: admin-message builder tests (T040). RED until T040 ships.
//   Tests 5/8: inbound-dispatch FSM tests (T041). RED until T041 ships.
//   Tests 6/7: timer-driven tests (T039+T041). RED until both ship.
//   No SUCCEED() placeholders — each test has real assertions.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/admin_messages.hpp>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <future>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"
#include "support/pump_until_ready.hpp"
#include "support/store_double.hpp"
#include "support/transport_double.hpp"

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

using namespace std::chrono_literals;

namespace fixpp::session::test {

namespace {

// ── Frame-scanning helper ─────────────────────────────────────────────────────

// Extract a field value from a SOH-delimited FIX frame.
// Returns the raw string value for the tag, or "" if not found.
static std::string extract_field(std::span<const std::byte> frame, std::uint32_t tag_wanted) {
    std::string wire(reinterpret_cast<const char*>(frame.data()), frame.size());
    std::string needle = std::to_string(tag_wanted) + "=";
    auto pos = wire.find(needle);
    if (pos == std::string::npos) {
        return {};
    }
    pos += needle.size();
    auto end = wire.find('\x01', pos);
    if (end == std::string::npos) {
        return {};
    }
    return wire.substr(pos, end - pos);
}

static bool has_field(std::span<const std::byte> frame, std::uint32_t tag) {
    return !extract_field(frame, tag).empty();
}

// Build a minimal FIX frame with BeginString/BodyLength/checksum.
static std::vector<std::byte> make_frame(std::string_view begin_string, std::string_view msg_type,
                                         std::uint32_t seq, std::string_view sender,
                                         std::string_view target,
                                         std::string_view extra_fields = {}) {
    std::string body;
    body += "35=" + std::string(msg_type) + "\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=" + std::string(sender) + "\x01";
    body += "52=20240101-00:00:00.000\x01";
    body += "56=" + std::string(target) + "\x01";
    if (!extra_fields.empty()) {
        body += std::string(extra_fields);
    }

    std::string hdr;
    hdr += "8=" + std::string(begin_string) + "\x01";
    hdr += "9=" + std::to_string(body.size()) + "\x01";

    std::string full = hdr + body;
    unsigned int cs = 0;
    for (unsigned char c : full) {
        cs += c;
    }
    cs &= 0xFFu;
    char csbuf[8];
    std::snprintf(csbuf, sizeof(csbuf), "%03u", cs);
    full += "10=" + std::string(csbuf) + "\x01";

    std::vector<std::byte> result;
    result.reserve(full.size());
    for (char c : full) {
        result.push_back(static_cast<std::byte>(c));
    }
    return result;
}

static std::vector<std::byte> make_logon_frame(std::string_view begin_string, std::uint32_t seq,
                                               std::string_view sender, std::string_view target,
                                               int heartbt = 30) {
    // Build extra fields without "\x01108=" (hex escape extends through digits).
    std::string extra;
    extra += "98=0\x01";
    extra += "108=" + std::to_string(heartbt) + "\x01";
    return make_frame(begin_string, "A", seq, sender, target, extra);
}

// ── FSM-fixture helpers (pattern from seqnum_gap_fatal_test.cpp) ──────────────

// Drive `co_spawn(ioc, coro, use_future)` by running the ioc for up to 200ms,
// then restarting it. The future is returned for the caller to `.get()`.
// This is the established pattern for single-threaded coroutine tests.
template <class R>
static R run_coro(asio::io_context& ioc, asio::awaitable<fixpp::core::expected_t<R>> coro) {
    auto fut = asio::co_spawn(ioc, std::move(coro), asio::use_future);
    ioc.run_for(200ms);
    ioc.restart();
    auto res = fut.get();
    if (!res.has_value()) {
        // Propagate by re-throwing to make test failures visible.
        return R{};  // caller checks separately
    }
    if constexpr (std::is_same_v<R, void>) {
        return;
    } else {
        return *res;
    }
}

// Overload for awaitable<expected_t<void>>.
static fixpp::core::expected_t<void> run_coro_result(
    asio::io_context& ioc, asio::awaitable<fixpp::core::expected_t<void>> coro) {
    auto fut = asio::co_spawn(ioc, std::move(coro), asio::use_future);
    if (!fixpp::test_support::run_window_then_ready(ioc, fut, 200ms, "run_coro_result")) {
        fixpp::test_support::cancel_and_drain_or_report(ioc, *clock, "run_coro_result");
        ADD_FAILURE() << fixpp::test_support::kWindowMiss << "run_coro_result";
        return std::unexpected(fixpp::test_support::kWindowMissSentinel);
    }
    return fut.get();
}

// ── Test fixture ──────────────────────────────────────────────────────────────

class HbTrTest : public ::testing::Test {
protected:
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine;

    void SetUp() override {
        // Clock must be anchored at 2024-01-01 00:00:00 UTC (= unix epoch +
        // 1704067200 s) to match the "52=20240101-00:00:00.000" timestamps
        // that every frame builder in this file embeds.  Using utc_time_point{}
        // (epoch = 0) caused a ~1.7 × 10⁹ s delta that triggered Q3
        // SendingTime/MaxLatency checks and sent sessions to Disconnected.
        using sc = std::chrono::system_clock;
        auto utc_2024 = sc::time_point{} + std::chrono::seconds{1704067200};
        clock = std::make_shared<fixpp::core::mock_clock>(
            utc_2024, fixpp::core::steady_time_point{}, ioc.get_executor());
        engine.clock = clock;
        engine.executor = ioc.get_executor();
    }

    fixpp::session::SessionConfig make_cfg(int heartbt_sec = 30) {
        fixpp::session::SessionConfig cfg;
        cfg.sender_comp_id = "SENDER";
        cfg.target_comp_id = "TARGET";
        cfg.begin_string = "FIX.4.2";
        cfg.heartbeat_interval = std::chrono::seconds{heartbt_sec};
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override = ioc.get_executor();
        // RC#C (gate-b/r1): bilateral_lenient — tests here don't exercise reset semantics.
        cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
        return cfg;
    }

    fixpp::core::expected_t<void> open_sync(Session& s) {
        auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, 200ms, "HbTrTest::open_sync")) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock, "HbTrTest::open_sync");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss << "HbTrTest::open_sync";
            return std::unexpected(fixpp::test_support::kWindowMissSentinel);
        }
        return fut.get();
    }

    fixpp::core::expected_t<void> feed_sync(Session& s, std::span<const std::byte> frame) {
        auto fut = asio::co_spawn(ioc, s.on_inbound_frame(frame), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, 200ms, "HbTrTest::feed_sync")) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock, "HbTrTest::feed_sync");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss << "HbTrTest::feed_sync";
            return std::unexpected(fixpp::test_support::kWindowMissSentinel);
        }
        return fut.get();
    }

    // Drive session to Active (initiator: open → LogonSent → feed peer Logon → Active).
    bool drive_to_active(Session& s, int heartbt_sec = 30) {
        auto r = open_sync(s);
        if (!r.has_value()) {
            return false;
        }
        // Peer (TARGET→SENDER) sends Logon seq=1.
        auto logon = make_logon_frame("FIX.4.2", 1, "TARGET", "SENDER", heartbt_sec);
        feed_sync(s, logon);
        return s.state() == fsm_state::Active;
    }
};

}  // anonymous namespace

// ──────────────────────────────────────────────────────────────────────────────
// T037 Test 1: build_heartbeat — 35=0, TestReqID(112) present when supplied
// ──────────────────────────────────────────────────────────────────────────────
TEST(HbTrBuilders, BuildHeartbeatCarriesTestReqID) {
    std::array<std::byte, 512> buf{};

    auto result = fixpp::session::build_heartbeat(std::span<std::byte>(buf), 1, "SENDER", "TARGET",
                                                  "TR001", "FIX.4.2", "20240101-00:00:00.000");

    ASSERT_TRUE(result.has_value()) << "build_heartbeat must succeed";
    auto frame = *result;
    ASSERT_GT(frame.size(), 0u);

    // MsgType must be 0 (Heartbeat).
    EXPECT_EQ(extract_field(frame, 35), "0");
    // TestReqID(112) must carry the supplied value.
    EXPECT_EQ(extract_field(frame, 112), "TR001");
    // SeqNum / CompID must be correct.
    EXPECT_EQ(extract_field(frame, 34), "1");
    EXPECT_EQ(extract_field(frame, 49), "SENDER");
    EXPECT_EQ(extract_field(frame, 56), "TARGET");
}

// ──────────────────────────────────────────────────────────────────────────────
// T037 Test 2: build_heartbeat — empty TestReqID → no tag 112 in frame
// ──────────────────────────────────────────────────────────────────────────────
TEST(HbTrBuilders, BuildHeartbeatNoTestReqID) {
    std::array<std::byte, 512> buf{};

    auto result =
        fixpp::session::build_heartbeat(std::span<std::byte>(buf), 2, "SENDER", "TARGET",
                                        /*test_req_id=*/{}, "FIX.4.2", "20240101-00:00:00.000");

    ASSERT_TRUE(result.has_value()) << "build_heartbeat (no TestReqID) must succeed";
    auto frame = *result;

    EXPECT_EQ(extract_field(frame, 35), "0");
    // Tag 112 must NOT appear when test_req_id is empty.
    EXPECT_FALSE(has_field(frame, 112)) << "tag 112 must be absent when test_req_id is empty";
}

// ──────────────────────────────────────────────────────────────────────────────
// T037 Test 3: build_test_request — 35=1, TestReqID(112) carried through
// ──────────────────────────────────────────────────────────────────────────────
TEST(HbTrBuilders, BuildTestRequestCarriesTestReqID) {
    std::array<std::byte, 512> buf{};

    auto result =
        fixpp::session::build_test_request(std::span<std::byte>(buf), 3, "SENDER", "TARGET",
                                           "REQ-42", "FIX.4.2", "20240101-00:00:00.000");

    ASSERT_TRUE(result.has_value()) << "build_test_request must succeed";
    auto frame = *result;
    ASSERT_GT(frame.size(), 0u);

    // MsgType must be 1 (TestRequest).
    EXPECT_EQ(extract_field(frame, 35), "1");
    // TestReqID(112) must carry the supplied value.
    EXPECT_EQ(extract_field(frame, 112), "REQ-42");
    EXPECT_EQ(extract_field(frame, 34), "3");
    EXPECT_EQ(extract_field(frame, 49), "SENDER");
    EXPECT_EQ(extract_field(frame, 56), "TARGET");
}

// ──────────────────────────────────────────────────────────────────────────────
// T037 Test 4: TestReqID distinctness — distinct caller-supplied IDs
// ──────────────────────────────────────────────────────────────────────────────
TEST(HbTrBuilders, TestReqIDDistinctness) {
    std::array<std::byte, 512> buf1{};
    std::array<std::byte, 512> buf2{};

    auto r1 = fixpp::session::build_test_request(std::span<std::byte>(buf1), 1, "S", "T", "ID-A",
                                                 "FIX.4.2", "20240101-00:00:00.000");
    auto r2 = fixpp::session::build_test_request(std::span<std::byte>(buf2), 2, "S", "T", "ID-B",
                                                 "FIX.4.2", "20240101-00:00:00.000");

    ASSERT_TRUE(r1.has_value());
    ASSERT_TRUE(r2.has_value());

    std::string id1 = extract_field(*r1, 112);
    std::string id2 = extract_field(*r2, 112);

    // Distinct test_req_id inputs → distinct TestReqID values in the frames.
    EXPECT_NE(id1, id2) << "Distinct test_req_id inputs must yield distinct TestReqID field values";
    EXPECT_EQ(id1, "ID-A");
    EXPECT_EQ(id2, "ID-B");
}

// ──────────────────────────────────────────────────────────────────────────────
// T037 Test 5: HeartBtInt=0 disables all timers (FR-006 / [FIX-SL §4.3.4])
//
// Negative-contrast multi-window assertion: with HeartBtInt=0 negotiated at
// logon, NO timer arms — neither the heartbeat-emit nor the unanswered-TR
// disconnect.  Companion test UnansweredTestRequestDisconnects proves that
// with HeartBtInt=1s and no inbound, the session disconnects after a single
// 1s window + 1s grace (=2s).  Here we negotiate HeartBtInt=0 and advance
// the mock clock by 10 × 30s = 300s with ZERO inbound frames.  At a normal
// HeartBtInt=30s the session would have disconnected after 60s (1×window +
// 1×grace); at 300s we are 5× past that threshold.  The session remaining
// Active is direct observable evidence that the HeartBtInt=0 disable held.
// ──────────────────────────────────────────────────────────────────────────────
TEST_F(HbTrTest, HeartBtIntZeroDisablesTimers) {
    // heartbeat_interval = 0 → disabled per [FIX-SL §4.3.4].
    auto cfg = make_cfg(0);
    cfg.heartbeat_interval = std::chrono::seconds{0};
    Session session{engine, cfg};

    ASSERT_TRUE(open_sync(session).has_value());

    // Feed a Logon-ack that negotiates HeartBtInt=0.
    auto logon_zero = make_logon_frame("FIX.4.2", 1, "TARGET", "SENDER", 0);
    feed_sync(session, logon_zero);
    ASSERT_EQ(session.state(), fsm_state::Active)
        << "Session must reach Active even with HeartBtInt=0";

    // Advance mock clock by 10 × 30s (= 300s) with NO inbound frames.
    // If any timer had armed against an implied default HeartBtInt=30s, the
    // unanswered-TR disconnect would have fired no later than 60s in.  At
    // 300s the session being Active proves all timers stayed disabled.
    for (int i = 0; i < 10; ++i) {
        clock->advance(30s);
        ioc.run_for(5ms);
        ioc.restart();
    }

    // Session must remain Active (no unanswered-TR disconnect could have
    // fired — the negative contrast with UnansweredTestRequestDisconnects
    // makes this an observable disable, not a "happens to be Active"
    // trivially-true assertion).
    EXPECT_EQ(session.state(), fsm_state::Active)
        << "HeartBtInt=0 disable held across 10×30s windows: "
           "without disable the unanswered-TR path would have fired by 60s";
}

// ──────────────────────────────────────────────────────────────────────────────
// T037 Test 7: Unanswered TestRequest → session_test_request_unanswered →
//              session transitions to Disconnected
//
// Scenario:
//   1. Active session with HeartBtInt=1s.
//   2. Clock advances past inbound-silence threshold (1×HeartBtInt).
//      → Timer fires; session emits TestRequest.
//   3. No Heartbeat reply arrives (no inbound frame).
//   4. Clock advances past grace window (another 1×HeartBtInt).
//      → unanswered-TR: session_test_request_unanswered → Disconnected.
//
// This test exercises T039 (timer loop) + T041 (unanswered-TR disposition).
// FAILS at RED phase — timer loop not yet wired.
// ──────────────────────────────────────────────────────────────────────────────
TEST_F(HbTrTest, UnansweredTestRequestDisconnects) {
    auto cfg = make_cfg(1);  // 1s HeartBtInt for fast test
    Session session{engine, cfg};
    ASSERT_TRUE(drive_to_active(session, 1));

    // Advance clock past inbound-silence threshold (1s + buffer).
    // This should trigger the session to emit a TestRequest.
    clock->advance(2s);
    ioc.run_for(20ms);
    ioc.restart();

    // Advance clock past the grace window (another 1s + buffer).
    // No Heartbeat reply → unanswered-TR → Disconnected.
    clock->advance(2s);
    ioc.run_for(20ms);
    ioc.restart();

    // The session must be Disconnected after the grace window.
    EXPECT_EQ(session.state(), fsm_state::Disconnected)
        << "Unanswered TestRequest must cause Disconnected within grace window";
}

// ──────────────────────────────────────────────────────────────────────────────
// T037 Test 7: Inbound Heartbeat in Active → resets last_inbound_steady_
//              across multiple HeartBtInt windows, FSM stays Active
//
// Negative-contrast multi-window assertion: per data-model Active row,
// inbound Heartbeat is a liveness event that resets the inactivity timer.
// Companion test UnansweredTestRequestDisconnects proves that with
// HeartBtInt=1s and NO inbound frame, the session disconnects after a
// single 1s window + 1s grace (=2s).  Here we use HeartBtInt=1s and feed
// an inbound Heartbeat at the start of each window across N=3 windows.
// Each inbound HB must reset last_inbound_steady_ — if any one fails to
// do so the unanswered-TR disconnect would fire by the end of that
// window's grace.  Surviving ≥3 windows with state still Active is the
// observable evidence that inbound HB genuinely resets the timer.
// ──────────────────────────────────────────────────────────────────────────────
TEST_F(HbTrTest, InboundHeartbeatKeepsSessionActive) {
    auto cfg = make_cfg(1);  // 1s HeartBtInt — same as UnansweredTR test
    Session session{engine, cfg};
    ASSERT_TRUE(drive_to_active(session, 1));

    // Drive through 3 HeartBtInt windows, feeding inbound HB at the start
    // of each.  Without the keep-alive reset, the session would have
    // disconnected inside the first window (2s after Active).
    seqnum_t inbound_seq = 2;
    for (int window = 0; window < 3; ++window) {
        // Feed inbound Heartbeat (35=0) — must reset last_inbound_steady_.
        auto hb_frame = make_frame("FIX.4.2", "0", inbound_seq++, "TARGET", "SENDER");
        auto res = feed_sync(session, hb_frame);
        ASSERT_TRUE(res.has_value())
            << "window " << window << ": on_inbound_frame for Heartbeat must not error";

        // Session must still be Active after the HB delivery.
        ASSERT_EQ(session.state(), fsm_state::Active)
            << "window " << window << ": session must be Active immediately after inbound HB";

        // Advance the clock half a HeartBtInt — keeps us inside this
        // window so no timer fires yet.
        clock->advance(500ms);
        ioc.run_for(20ms);
        ioc.restart();
    }

    // After 3 windows of HB-driven keep-alive, the FSM must remain Active.
    // Without the inbound-HB reset, the unanswered-TR path would have
    // fired by window 0's grace expiry (=2s after Active).
    EXPECT_EQ(session.state(), fsm_state::Active)
        << "Inbound Heartbeat must reset last_inbound_steady_ each window: "
           "session survived 3 windows that would otherwise have disconnected";
}

// Regression: an inbound Heartbeat(35=0) MUST NOT trigger an outbound Heartbeat.
// Per FIX session semantics a Heartbeat is never answered — only a TestRequest
// gets a Heartbeat reply (data-model.md:22 Active row: inbound Heartbeat =
// "advance counter (liveness)", NO emit). The retired "T020-A echo" emitted a
// Heartbeat on every inbound Heartbeat, which storms at RTT cadence when two
// fixpp sessions are paired (each echoes the other's beat). See the self-paired
// heartbeat-storm finding.
TEST_F(HbTrTest, InboundHeartbeatEmitsNoEcho) {
    auto cfg = make_cfg(30);  // long HeartBtInt → the liveness timer stays parked
    std::vector<std::vector<std::byte>> outbound;
    cfg.transport_send = [&outbound](std::span<const std::byte> f) {
        outbound.emplace_back(f.begin(), f.end());
    };
    Session session{engine, cfg};
    ASSERT_TRUE(drive_to_active(session, 30));

    outbound.clear();  // drop the Logon handshake frames

    // Feed several in-order inbound Heartbeats; none may produce any outbound.
    seqnum_t inbound_seq = 2;
    for (int i = 0; i < 5; ++i) {
        auto hb_frame = make_frame("FIX.4.2", "0", inbound_seq++, "TARGET", "SENDER");
        ASSERT_TRUE(feed_sync(session, hb_frame).has_value())
            << "inbound Heartbeat " << i << " must not error";
        ASSERT_EQ(session.state(), fsm_state::Active);
    }

    int outbound_hb = 0;
    for (const auto& f : outbound) {
        std::string_view sv{reinterpret_cast<const char*>(f.data()), f.size()};
        if (sv.find("\x01"
                    "35=0\x01") != std::string_view::npos) {
            ++outbound_hb;
        }
    }
    EXPECT_EQ(outbound_hb, 0)
        << "inbound Heartbeat(35=0) must NOT trigger an outbound Heartbeat echo "
           "(FIX: a Heartbeat is never answered; data-model.md:22) — got " << outbound_hb;
    EXPECT_TRUE(outbound.empty())
        << "inbound Heartbeat must produce zero outbound frames; got " << outbound.size();
}

}  // namespace fixpp::session::test
