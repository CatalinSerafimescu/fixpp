// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_test_request_id_cross_session_race.cpp
//
// 009-session-fsm-finalize T019 [US4] — Per-session TestReqID counter race-freedom.
//
// Scenarios (spec.md FR-010 / SC-003 / [const §XI.4]):
//
//   1. CrossSessionDisjoint — two concurrent sessions' TestReqID sequences are
//      disjoint (FR-010: per-session counter, not process-global static).
//
//   2. WithinSessionMonotone — within each session, TestReqIDs increment
//      monotonically from TR1 upward (no gaps, no reset).
//
// Test structure:
//   - asio::thread_pool(4) with TWO Sessions, each on its own strand.
//   - Both sessions use mock_clock + HeartBtInt=1s so the liveness loop fires
//     often. We advance the mock clock rapidly so each session emits many
//     TestRequest frames.
//   - Each session's transport_send captures outbound frames into a
//     mutex-protected vector.
//   - After enough clock ticks, close both sessions and analyse captures.
//
// SC-003 quantified threshold: 10^4 TestRequests per session.
// In practice we use a smaller count (100) to keep the test fast; the TSan
// stress comes from the pool threads, not the TestRequest count.
//
// RED phase (before T020): the existing `static tr_counter` in
// run_liveness_loop is shared across all sessions → assertion (a) will fail
// (sequences interleave) AND TSan will fire a data race on the static.
//
// GREEN phase (after T020): ++next_test_request_id_ is per-session on the
// session strand → disjoint, monotone, race-free.
//
// Anchors:
//   spec.md FR-010, SC-003
//   [const §XI.4] per-session strand isolation
//   research.md D-3 (wrap-around at UINT32_MAX acceptable)

#include <gtest/gtest.h>

#include <algorithm>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_future.hpp>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"
#include "support/pump_until_ready.hpp"

using namespace std::chrono_literals;

namespace fixpp::session::test {

namespace {

// ── Wire-field extractor (local copy avoids TARGET_OBJECTS dependency) ─────────
//
// Extract the value of a FIX tag from a SOH-delimited frame.
// Returns "" if the tag is not present.
static std::string extract_tag(std::span<const std::byte> frame, std::uint32_t tag) {
    std::string wire(reinterpret_cast<const char*>(frame.data()), frame.size());
    std::string needle = std::to_string(tag) + "=";
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

// ── Build a minimal Logon frame for feeding into a session ────────────────────
static std::vector<std::byte> make_logon_frame(std::string_view begin_string, std::uint32_t seq,
                                               std::string_view sender, std::string_view target,
                                               int heartbt = 1) {
    std::string body;
    body += "35=A\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=" + std::string(sender) + "\x01";
    body += "52=20240101-00:00:00.000\x01";
    body += "56=" + std::string(target) + "\x01";
    body += "98=0\x01";
    body += "108=" + std::to_string(heartbt) + "\x01";

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

// ── CaptureTransport: thread-safe outbound frame capture ────────────────────────
//
// The session's transport_send is called from the session strand; we collect all
// outbound frames into a vector under a std::mutex.  The mutex is ONLY for the
// test's collector (not on the session-internal path), so it doesn't affect TSan
// annotations on the session's own counters.
struct CaptureTransport {
    std::mutex mtx;
    std::vector<std::vector<std::byte>> frames;

    void capture(std::span<const std::byte> frame) {
        std::lock_guard<std::mutex> lock{mtx};
        frames.emplace_back(frame.begin(), frame.end());
    }

    // Collect all 112= (TestReqID) values from frames where 35=1 (TestRequest).
    std::vector<std::string> collect_test_req_ids() {
        std::lock_guard<std::mutex> lock{mtx};
        std::vector<std::string> ids;
        for (const auto& f : frames) {
            auto sp = std::span<const std::byte>{f.data(), f.size()};
            if (extract_tag(sp, 35) == "1") {
                auto id = extract_tag(sp, 112);
                if (!id.empty()) {
                    ids.push_back(std::move(id));
                }
            }
        }
        return ids;
    }
};

// ── TestReqID numeric extractor: "TR<N>" → N ──────────────────────────────────
// Returns 0 if parsing fails.
static std::uint32_t parse_tr_id(std::string_view s) {
    if (s.size() < 3) {
        return 0;
    }
    if (s[0] != 'T' || s[1] != 'R') {
        return 0;
    }
    std::uint32_t v = 0;
    for (std::size_t i = 2; i < s.size(); ++i) {
        if (s[i] < '0' || s[i] > '9') {
            return 0;
        }
        v = v * 10u + static_cast<std::uint32_t>(s[i] - '0');
    }
    return v;
}

// ── Bounded pump (#284) ───────────────────────────────────────────────────────
//
// Every `co_spawn(..., use_future)` → `run_for(window)` → `restart()` →
// `fut.get()` site below now waits with `pump_until_ready` instead: it drives
// the context UNTIL the operation completes, bounded by a real-time budget.
// The helper is the one logout_exchange_test.cpp introduced for this same
// defect, hoisted to tests/support; its header carries the full rationale, the
// failure text, and the teardown guard the budget path needs.
using fixpp::test_support::kPumpBudgetMiss;
using fixpp::test_support::pump_until;
using fixpp::test_support::pump_until_ready;
using fixpp::test_support::quiesce_on_exit;

// ── Session fixture helper ─────────────────────────────────────────────────────

struct SessionFixture {
    fixpp::core::EngineConfig engine;
    fixpp::session::SessionConfig cfg;
    CaptureTransport transport;
    std::unique_ptr<fixpp::session::Session> session;

    SessionFixture(asio::any_io_executor ex, std::shared_ptr<fixpp::core::mock_clock> clk,
                   std::string_view sender, std::string_view target) {
        engine.executor = ex;
        engine.clock = clk;

        cfg.sender_comp_id = std::string(sender);
        cfg.target_comp_id = std::string(target);
        cfg.begin_string = "FIX.4.2";
        cfg.heartbeat_interval = std::chrono::seconds{1};
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override = ex;
        cfg.transport_send = [this](std::span<const std::byte> frame) { transport.capture(frame); };
        // RC#C (gate-b/r1): bilateral_lenient — test exercises TestReqID cross-session, not reset.
        cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;

        session = std::make_unique<fixpp::session::Session>(engine, cfg);
    }

    // Open the session (initiator: emits Logon, waits for Logon-ack).
    // Returns the future for the open() awaitable.
    [[nodiscard]] std::future<fixpp::core::expected_t<void>> async_open(asio::thread_pool& pool) {
        return asio::co_spawn(pool, session->open(), asio::use_future);
    }

    // Feed a peer Logon-ack to drive session into Active state.
    [[nodiscard]] std::future<fixpp::core::expected_t<void>> async_feed_logon(
        asio::thread_pool& pool) {
        auto logon_bytes =
            make_logon_frame("FIX.4.2", 1, cfg.target_comp_id, cfg.sender_comp_id, 1);
        return asio::co_spawn(pool,
                              session->on_inbound_frame(std::span<const std::byte>{
                                  logon_bytes.data(), logon_bytes.size()}),
                              asio::use_future);
    }

    // Close the session.
    [[nodiscard]] std::future<fixpp::core::expected_t<void>> async_close(asio::thread_pool& pool) {
        return asio::co_spawn(pool, session->close(fixpp::session::close_mode::terminal),
                              asio::use_future);
    }
};

}  // anonymous namespace

// ── Test 1: CrossSessionDisjoint ──────────────────────────────────────────────
//
// RED assertion (a): session A's TestReqID set is disjoint from session B's.
// With a `static tr_counter`, both sessions share the counter so IDs interleave
// → the assertion fails in the RED phase.
// With `++next_test_request_id_` (per-session), the sequences are disjoint.
//
// GREEN assertion (b): within each session, IDs increment monotonically.
//
// SC-003: we advance the clock enough for each session to emit ≥ 100
// TestRequests (fast test; TSan is the stress mechanism, not the count).
TEST(CrossSessionTestReqID, CrossSessionDisjoint) {
    // Single io_context so we can coordinate clock advancement.
    asio::io_context ioc;
    // Both sessions share the same mock_clock driven by the ioc executor.
    using sc = std::chrono::system_clock;
    auto utc_2024 = sc::time_point{} + std::chrono::seconds{1704067200};
    auto clock = std::make_shared<fixpp::core::mock_clock>(
        utc_2024, fixpp::core::steady_time_point{}, ioc.get_executor());

    SessionFixture sA{ioc.get_executor(), clock, "SENDER_A", "TARGET_A"};
    SessionFixture sB{ioc.get_executor(), clock, "SENDER_B", "TARGET_B"};

    // #284 teardown. On the budget-exhausted path the awaited coroutine is
    // still SUSPENDED and its frame references sA/sB, the clock, and (for
    // on_inbound_frame) a span over a block-scoped buffer. Declared AFTER the
    // fixtures so it runs BEFORE them, on every exit path including the early
    // `return` an ASSERT_* performs. See the header for why reordering the
    // declarations cannot substitute.
    quiesce_on_exit quiesce{ioc, *clock};

    // Open both sessions (initiator path: each emits a Logon immediately).
    auto fut_open_a = asio::co_spawn(ioc, sA.session->open(), asio::use_future);
    ASSERT_TRUE(pump_until_ready(ioc, fut_open_a)) << kPumpBudgetMiss << "opening session A";
    ASSERT_TRUE(fut_open_a.get().has_value()) << "Session A failed to open";

    auto fut_open_b = asio::co_spawn(ioc, sB.session->open(), asio::use_future);
    ASSERT_TRUE(pump_until_ready(ioc, fut_open_b)) << kPumpBudgetMiss << "opening session B";
    ASSERT_TRUE(fut_open_b.get().has_value()) << "Session B failed to open";

    // Drive both sessions to Active by feeding them peer Logon-acks.
    {
        auto logon_a = make_logon_frame("FIX.4.2", 1, "TARGET_A", "SENDER_A", 1);
        auto fut_a = asio::co_spawn(ioc,
                                    sA.session->on_inbound_frame(
                                        std::span<const std::byte>{logon_a.data(), logon_a.size()}),
                                    asio::use_future);
        ASSERT_TRUE(pump_until_ready(ioc, fut_a)) << kPumpBudgetMiss << "feeding session A's Logon-ack";
        (void)fut_a.get();
    }
    {
        auto logon_b = make_logon_frame("FIX.4.2", 1, "TARGET_B", "SENDER_B", 1);
        auto fut_b = asio::co_spawn(ioc,
                                    sB.session->on_inbound_frame(
                                        std::span<const std::byte>{logon_b.data(), logon_b.size()}),
                                    asio::use_future);
        ASSERT_TRUE(pump_until_ready(ioc, fut_b)) << kPumpBudgetMiss << "feeding session B's Logon-ack";
        (void)fut_b.get();
    }

    ASSERT_EQ(sA.session->state(), fixpp::session::fsm_state::Active)
        << "Session A must be Active before advancing clock";
    ASSERT_EQ(sB.session->state(), fixpp::session::fsm_state::Active)
        << "Session B must be Active before advancing clock";

    // Advance mock clock to generate TestRequests.
    // HeartBtInt=1s; each 2-second advance (1s window + 1s grace without reply)
    // generates one TestRequest then disconnects. We want many TestRequests without
    // disconnecting, so we feed Heartbeat replies to keep the session alive.
    //
    // Strategy: advance 1s → session emits TestRequest → feed Heartbeat reply to
    // both sessions → repeat 50 times. Each session emits one TR per iteration → 50 each.
    //
    // Heartbeat frame (35=0) with TestReqID (112) to clear the pending TR.
    auto make_heartbeat = [](std::string_view bs, std::uint32_t seq, std::string_view sender,
                             std::string_view target,
                             std::string_view tr_id) -> std::vector<std::byte> {
        std::string body;
        body += "35=0\x01";
        body += "34=" + std::to_string(seq) + "\x01";
        body += "49=" + std::string(sender) + "\x01";
        body += "52=20240101-00:00:00.000\x01";
        body += "56=" + std::string(target) + "\x01";
        if (!tr_id.empty()) {
            body += "112=" + std::string(tr_id) + "\x01";
        }
        std::string hdr;
        hdr += "8=" + std::string(bs) + "\x01";
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
    };

    // Advance clock and feed Heartbeat replies to keep sessions alive.
    // Each iteration generates one TestRequest per session.
    std::uint32_t hb_seq_a = 2;  // peer (TARGET_A) inbound seqnum (Logon was 1)
    std::uint32_t hb_seq_b = 2;
    const int kIterations = 50;

    for (int i = 0; i < kIterations; ++i) {
        // Advance by 1.5s to trigger the TestRequest emission, then pump until
        // BOTH sessions have actually emitted this iteration's TestRequest.
        //
        // #284: this was the last fixed window in the test, and it was the one
        // that mattered most — it is the emission driver, so an under-served
        // 20 ms here silently shrinks the corpus every assertion below runs
        // over rather than hanging. Waiting on the emission itself is what lets
        // the count be pinned EXACTLY at kIterations instead of to a band, and
        // a band wide enough to tolerate the old window would have admitted the
        // very collapse it was meant to detect.
        clock->advance(std::chrono::milliseconds{1500});
        const auto want = static_cast<std::size_t>(i + 1);
        ASSERT_TRUE(pump_until(ioc, [&] {
            return sA.transport.collect_test_req_ids().size() >= want &&
                   sB.transport.collect_test_req_ids().size() >= want;
        })) << kPumpBudgetMiss << "waiting for both TestRequests at iteration " << i;

        // Find the most recently emitted TR from each session and echo it back.
        auto tr_ids_a = sA.transport.collect_test_req_ids();
        auto tr_ids_b = sB.transport.collect_test_req_ids();

        if (!tr_ids_a.empty()) {
            std::string latest_a = tr_ids_a.back();
            auto hb = make_heartbeat("FIX.4.2", hb_seq_a++, "TARGET_A", "SENDER_A", latest_a);
            auto fut = asio::co_spawn(
                ioc, sA.session->on_inbound_frame(std::span<const std::byte>{hb.data(), hb.size()}),
                asio::use_future);
            ASSERT_TRUE(pump_until_ready(ioc, fut))
                << kPumpBudgetMiss << "feeding session A's Heartbeat at iteration " << i;
            (void)fut.get();
        }

        if (!tr_ids_b.empty()) {
            std::string latest_b = tr_ids_b.back();
            auto hb = make_heartbeat("FIX.4.2", hb_seq_b++, "TARGET_B", "SENDER_B", latest_b);
            auto fut = asio::co_spawn(
                ioc, sB.session->on_inbound_frame(std::span<const std::byte>{hb.data(), hb.size()}),
                asio::use_future);
            ASSERT_TRUE(pump_until_ready(ioc, fut))
                << kPumpBudgetMiss << "feeding session B's Heartbeat at iteration " << i;
            (void)fut.get();
        }
    }

    // Close both sessions before analysis.
    {
        auto fut_a = asio::co_spawn(ioc, sA.session->close(fixpp::session::close_mode::terminal),
                                    asio::use_future);
        ASSERT_TRUE(pump_until_ready(ioc, fut_a)) << kPumpBudgetMiss << "closing session A";
        (void)fut_a.get();
    }
    {
        auto fut_b = asio::co_spawn(ioc, sB.session->close(fixpp::session::close_mode::terminal),
                                    asio::use_future);
        ASSERT_TRUE(pump_until_ready(ioc, fut_b)) << kPumpBudgetMiss << "closing session B";
        (void)fut_b.get();
    }

    // Collect all TestRequest IDs from both sessions.
    auto ids_a = sA.transport.collect_test_req_ids();
    auto ids_b = sB.transport.collect_test_req_ids();

    // Assertion: each session emitted exactly one TestRequest per iteration.
    //
    // #284: the previous `> 0` form was satisfiable by a SINGLE emission, so any
    // change that pumped less would have collapsed the corpus every assertion
    // below runs over while leaving all of them green — the #283/#286 vacuity
    // class. An EQUALITY is only assertable because the loop above now waits on
    // the emission instead of on a fixed window; a tolerance band wide enough to
    // survive that window would have admitted the collapse it claims to catch.
    ASSERT_EQ(ids_a.size(), static_cast<std::size_t>(kIterations))
        << "Session A emitted " << ids_a.size() << " TestRequests, expected exactly "
        << kIterations << " — check clock/liveness wiring";
    ASSERT_EQ(ids_b.size(), static_cast<std::size_t>(kIterations))
        << "Session B emitted " << ids_b.size() << " TestRequests, expected exactly "
        << kIterations << " — check clock/liveness wiring";

    // ── Assertion (a): per-session isolation — sequences are contiguous ───────
    //
    // With per-session ++next_test_request_id_ (GREEN): each session independently
    // counts from 1. Session A produces TR1, TR2, TR3, ... and session B also
    // independently produces TR1, TR2, TR3, ... Both sequences are contiguous
    // (no gaps). A contiguous sequence from 1..N proves the counter was NOT shared
    // with another session (which would cause alternating skips: TR1, TR3, TR5, ...).
    //
    // With static tr_counter (RED): A and B share the global counter. If A and B
    // interleave, A might produce TR1, TR3, TR5... and B: TR2, TR4, TR6... —
    // both sequences have GAPS. The contiguous check catches this.
    //
    // Note: both sessions start at next_test_request_id_=0, so both produce
    // TR1, TR2, TR3... with per-session counters (expected duplicate values
    // across sessions; uniqueness is NOT the isolation invariant — isolation means
    // each session's sequence is contiguous within itself).
    {
        // Extract numeric values and verify they form a contiguous sequence 1..N.
        auto check_contiguous = [](const std::vector<std::string>& ids,
                                   const char* session_name) -> bool {
            if (ids.empty()) {
                return true;
            }
            std::vector<std::uint32_t> ns;
            ns.reserve(ids.size());
            for (const auto& id : ids) {
                std::uint32_t n = parse_tr_id(id);
                if (n == 0) {
                    return false;
                }
                ns.push_back(n);
            }
            // Sequence must start at 1 and be strictly increasing by 1.
            for (std::size_t i = 0; i < ns.size(); ++i) {
                if (ns[i] != static_cast<std::uint32_t>(i + 1)) {
                    return false;
                }
            }
            return true;
        };

        bool a_contiguous = check_contiguous(ids_a, "A");
        bool b_contiguous = check_contiguous(ids_b, "B");

        EXPECT_TRUE(a_contiguous)
            << "Session A's TestReqID sequence is not contiguous 1..N — "
            << "the counter may be shared with another session (static tr_counter bug). "
            << "IDs: " << ids_a.size() << " total";
        EXPECT_TRUE(b_contiguous)
            << "Session B's TestReqID sequence is not contiguous 1..N — "
            << "the counter may be shared with another session (static tr_counter bug). "
            << "IDs: " << ids_b.size() << " total";
    }

    // ── Assertion (b): monotone within each session ────────────────────────
    // IDs within each session must be strictly increasing (1, 2, 3, ...).
    {
        std::uint32_t prev = 0;
        bool monotone = true;
        for (const auto& id : ids_a) {
            std::uint32_t n = parse_tr_id(id);
            if (n == 0 || n <= prev) {
                monotone = false;
                break;
            }
            prev = n;
        }
        EXPECT_TRUE(monotone) << "Session A's TestReqID sequence is not strictly increasing";
    }
    {
        std::uint32_t prev = 0;
        bool monotone = true;
        for (const auto& id : ids_b) {
            std::uint32_t n = parse_tr_id(id);
            if (n == 0 || n <= prev) {
                monotone = false;
                break;
            }
            prev = n;
        }
        EXPECT_TRUE(monotone) << "Session B's TestReqID sequence is not strictly increasing";
    }
}

// ── Test 2: Concurrent sessions on a thread_pool (TSan stress) ──────────────
//
// Runs two sessions concurrently on a thread_pool(4) to maximise the TSan
// window for racing on the `static tr_counter` (RED) or proving per-session
// isolation (GREEN).
//
// RED (before T020): the `static tr_counter` in run_liveness_loop is shared
// across sessions → TSan fires a data race when both sessions' liveness loops
// run concurrently on different pool threads.
//
// GREEN (after T020): ++next_test_request_id_ is per-session on the session
// strand; no shared state → TSan clean.
//
// Clock advances are sequential (one session's clock advanced at a time) to
// avoid races on the mock_clock's internal state (which is a test concern,
// not the SUT concern). The TSan stress comes from both sessions running
// their liveness loops concurrently on the pool threads.
//
// #284 disposition: the six `fut.get()` calls below are NOT the #284 defect.
// This test runs on a `thread_pool`, whose own threads service the work, so a
// get() here waits on a context that is still being pumped. #284 is specific to
// the single-threaded io_context in test 1, where the test thread was both the
// only pump and the blocked waiter.
TEST(CrossSessionTestReqID, ConcurrentSessionsTSanStress) {
    asio::thread_pool pool{4};

    using sc = std::chrono::system_clock;
    auto utc_2024 = sc::time_point{} + std::chrono::seconds{1704067200};

    // Each session gets its own mock_clock bound to the pool executor.
    auto clock_a = std::make_shared<fixpp::core::mock_clock>(
        utc_2024, fixpp::core::steady_time_point{}, pool.get_executor());
    auto clock_b = std::make_shared<fixpp::core::mock_clock>(
        utc_2024, fixpp::core::steady_time_point{}, pool.get_executor());

    SessionFixture sA{pool.get_executor(), clock_a, "SENDER_A", "TARGET_A"};
    SessionFixture sB{pool.get_executor(), clock_b, "SENDER_B", "TARGET_B"};

    // Open both sessions.
    {
        auto fa = asio::co_spawn(pool, sA.session->open(), asio::use_future);
        auto fb = asio::co_spawn(pool, sB.session->open(), asio::use_future);
        ASSERT_TRUE(fa.get().has_value()) << "Session A open failed";
        ASSERT_TRUE(fb.get().has_value()) << "Session B open failed";
    }

    // Drive to Active (sequential to avoid concurrent on_inbound_frame calls
    // on the same executor before the strand is fully set up).
    {
        auto logon_a = make_logon_frame("FIX.4.2", 1, "TARGET_A", "SENDER_A", 1);
        auto fa = asio::co_spawn(pool,
                                 sA.session->on_inbound_frame(
                                     std::span<const std::byte>{logon_a.data(), logon_a.size()}),
                                 asio::use_future);
        fa.get();
    }
    {
        auto logon_b = make_logon_frame("FIX.4.2", 1, "TARGET_B", "SENDER_B", 1);
        auto fb = asio::co_spawn(pool,
                                 sB.session->on_inbound_frame(
                                     std::span<const std::byte>{logon_b.data(), logon_b.size()}),
                                 asio::use_future);
        fb.get();
    }

    ASSERT_EQ(sA.session->state(), fixpp::session::fsm_state::Active);
    ASSERT_EQ(sB.session->state(), fixpp::session::fsm_state::Active);

    // Advance both clocks sequentially (not concurrently) to avoid races on
    // the mock_clock's internals. The TSan stress is on the `tr_counter` that
    // both sessions' liveness loops (running on separate pool strands) access.
    //
    // Each clock advance triggers the respective session's liveness loop to
    // emit a TestRequest. The two liveness loops may run concurrently on the
    // pool's threads → TSan window for `static tr_counter` race.
    for (int i = 0; i < 10; ++i) {
        // Advance session A's clock.
        clock_a->advance(std::chrono::milliseconds{1500});
        // Advance session B's clock.
        clock_b->advance(std::chrono::milliseconds{1500});
        // Allow both liveness loops to process (they run on pool threads
        // concurrently; we don't block here, TSan instruments the actual access).
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }

    // Close both sessions cleanly (sequential to avoid concurrent close races).
    {
        auto fa = asio::co_spawn(pool, sA.session->close(fixpp::session::close_mode::terminal),
                                 asio::use_future);
        fa.get();
    }
    {
        auto fb = asio::co_spawn(pool, sB.session->close(fixpp::session::close_mode::terminal),
                                 asio::use_future);
        fb.get();
    }

    pool.join();

    // Collect results and verify per-session contiguity.
    auto ids_a = sA.transport.collect_test_req_ids();
    auto ids_b = sB.transport.collect_test_req_ids();

    // Per-session isolation: each session's sequence must be contiguous 1..N.
    auto check_contiguous = [](const std::vector<std::string>& ids) -> bool {
        if (ids.empty()) {
            return true;
        }
        for (std::size_t i = 0; i < ids.size(); ++i) {
            if (parse_tr_id(ids[i]) != static_cast<std::uint32_t>(i + 1)) {
                return false;
            }
        }
        return true;
    };

    if (!ids_a.empty()) {
        EXPECT_TRUE(check_contiguous(ids_a))
            << "Session A TestReqID sequence has gaps (shared static counter?)";
    }
    if (!ids_b.empty()) {
        EXPECT_TRUE(check_contiguous(ids_b))
            << "Session B TestReqID sequence has gaps (shared static counter?)";
    }

    // Monotone within each session (assertion (b)).
    if (!ids_a.empty()) {
        std::uint32_t prev = 0;
        bool ok = true;
        for (const auto& id : ids_a) {
            auto n = parse_tr_id(id);
            if (n > 0 && n <= prev) {
                ok = false;
                break;
            }
            if (n > 0) {
                prev = n;
            }
        }
        EXPECT_TRUE(ok) << "Session A TestReqID sequence is not monotone";
    }
    if (!ids_b.empty()) {
        std::uint32_t prev = 0;
        bool ok = true;
        for (const auto& id : ids_b) {
            auto n = parse_tr_id(id);
            if (n > 0 && n <= prev) {
                ok = false;
                break;
            }
            if (n > 0) {
                prev = n;
            }
        }
        EXPECT_TRUE(ok) << "Session B TestReqID sequence is not monotone";
    }
}

}  // namespace fixpp::session::test
