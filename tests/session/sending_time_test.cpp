// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/sending_time_test.cpp
//
// Seam #8 — SendingTime(52) MaxLatency enforcement (Q3).
// (005-session-establishment-fsm T051 / Phase 7 / US5)
//
// Scenarios (FR-013, SC-007, Clarification Q3, D-3/D-8):
//
//  1. check_sending_time: within MaxLatency → returns ok (expected_t<void>{}).
//
//  2. check_sending_time: exceeds MaxLatency (|delta| > 120 s default) →
//     returns unexpected(session_sending_time_accuracy) (slot 71).
//
//  3. check_sending_time: exact MaxLatency boundary → ok (≤ is allowed).
//
//  4. Q3 established-session path: a non-Logon message with stale SendingTime
//     arriving in Active state triggers:
//       - outbound Reject(35=3, RefTagID=52, SessionRejectReason=10)
//       - immediately followed by outbound Logout(35=5)
//       - FSM → Disconnected
//     The transport_send callback sees BOTH frames in order.
//
//  5. Q3 Logon-path special case (D-3, FR-013): a Logon(35=A) with a stale
//     SendingTime triggers a logout-with-error (outbound Logout only — NO
//     standalone Reject is emitted before the Logout on the Logon path).
//     The transport must see a Logout(35=5) as the outbound response, NOT a
//     Reject(35=3).
//
// Anchors: data-model.md E7/E8; research D-3/D-5/D-8; error slot 71;
// [FIX-SL §4.2.3]; spec FR-013; SC-007; tasks.md T051/T055/T056.
#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/sending_time.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <future>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"
#include "support/pump_until_ready.hpp"
#include "support/transport_double.hpp"

using namespace std::chrono_literals;

namespace fixpp::session::test {
namespace {

using fixpp::core::utc_time_point;

// ── check_sending_time unit tests (T055) ──────────────────────────────────────

// Test 1: within MaxLatency → ok
TEST(SendingTimeCheck, WithinMaxLatency) {
    utc_time_point epoch{};
    auto inbound = epoch + std::chrono::seconds{1000};
    auto now = epoch + std::chrono::seconds{1090};  // 90 s delta, < 120 s

    auto r = fixpp::session::check_sending_time(inbound, now, std::chrono::seconds{120});
    EXPECT_TRUE(r.has_value()) << "90 s delta with 120 s MaxLatency must return ok";
}

// Test 2: exceeds MaxLatency → session_sending_time_accuracy
TEST(SendingTimeCheck, ExceedsMaxLatency) {
    utc_time_point epoch{};
    auto inbound = epoch + std::chrono::seconds{1000};
    auto now = epoch + std::chrono::seconds{1200};  // 200 s delta, > 120 s

    auto r = fixpp::session::check_sending_time(inbound, now, std::chrono::seconds{120});
    ASSERT_FALSE(r.has_value()) << "200 s delta with 120 s MaxLatency must return error";
    EXPECT_EQ(r.error(), fixpp::core::error::session_sending_time_accuracy)
        << "Error must be session_sending_time_accuracy (slot 71)";
}

// Test 3: exactly at MaxLatency → ok (|delta| <= max_latency)
TEST(SendingTimeCheck, ExactBoundaryIsOk) {
    utc_time_point epoch{};
    auto inbound = epoch + std::chrono::seconds{1000};
    auto now = epoch + std::chrono::seconds{1120};  // exactly 120 s delta

    auto r = fixpp::session::check_sending_time(inbound, now, std::chrono::seconds{120});
    EXPECT_TRUE(r.has_value())
        << "Exactly 120 s delta with 120 s MaxLatency must return ok (boundary inclusive)";
}

// Test 3b: stale in the past (inbound < now - MaxLatency)
TEST(SendingTimeCheck, StaleInPast) {
    utc_time_point epoch{};
    auto inbound = epoch + std::chrono::seconds{1000};
    auto now = epoch + std::chrono::seconds{1200};  // inbound is 200s in the past

    auto r = fixpp::session::check_sending_time(inbound, now, std::chrono::seconds{120});
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), fixpp::core::error::session_sending_time_accuracy);
}

// Test 3c: from the future (inbound > now + MaxLatency)
TEST(SendingTimeCheck, FarFutureSendingTime) {
    utc_time_point epoch{};
    auto inbound = epoch + std::chrono::seconds{1300};  // 300s in the future
    auto now = epoch + std::chrono::seconds{1000};

    auto r = fixpp::session::check_sending_time(inbound, now, std::chrono::seconds{120});
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), fixpp::core::error::session_sending_time_accuracy);
}

// ── Frame builders for integration tests ─────────────────────────────────────

// Build a FIX frame with an explicit SendingTime value.
static std::vector<std::byte> make_frame_with_sending_time(
    std::string_view begin_string, std::string_view msg_type, std::uint32_t seq,
    std::string_view sender, std::string_view target, std::string_view sending_time,
    std::string_view extra_body = {}) {
    std::string body;
    body += "35=" + std::string(msg_type) + "\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=" + std::string(sender) + "\x01";
    body += "52=" + std::string(sending_time) + "\x01";
    body += "56=" + std::string(target) + "\x01";
    if (!extra_body.empty()) {
        body += extra_body;
    }

    std::string hdr;
    hdr += "8=" + std::string(begin_string) + "\x01";
    hdr += "9=" + std::to_string(body.size()) + "\x01";

    std::string full = hdr + body;
    unsigned int cs = 0;
    for (unsigned char c : full) {
        cs += c;
    }
    cs &= 0xFFU;
    char csbuf[4];
    snprintf(csbuf, sizeof(csbuf), "%03u", cs);
    full += "10=" + std::string(csbuf) + "\x01";

    std::vector<std::byte> frame;
    for (char c : full) {
        frame.push_back(static_cast<std::byte>(c));
    }
    return frame;
}

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

// ── Integration fixture ───────────────────────────────────────────────────────

// ── #289: the `run_for(W); restart(); fut.get()` migration ───────────────────
//
// The sites in this file use `run_window_then_ready`
// (tests/support/pump_until_ready.hpp). The window is PRESERVED: the hazard
// #289 names is the UNCONDITIONAL `get()`, not the fixed window. On a
// manually-driven io_context a `get()` the window did not satisfy blocks with
// nothing left to pump it -- a deadlock ctest reports as a timeout.
//
// Teardown is deliberately NOT a fixture-destructor drain, which is the shape
// PRs #301 and #307 used for fixtures that OWN their Session. Here every
// `Session` is a block-local declared AFTER the fixture, so it dies BEFORE a
// fixture destructor body could run -- and a drain is what RESUMES a suspended
// frame, so a destructor drain would resume it over the destroyed Session. The
// drain runs on the MISS branch instead, in the scope that still owns that
// storage, and it CANCELS THE MOCK CLOCK'S SLEEPS first: the first transition
// to Active co_spawns a detached `run_liveness_loop()` that parks on
// `sleep_until`, holding a work guard that `drain_or_report` cannot release
// (only a Clock can). `cancel_sleeps()` releases the waiters that exist WHEN IT
// RUNS and nothing more, so a miss whose drain itself performs the Active
// transition registers a NEW waiter afterwards. That WAS a documented limitation of
// the primitive, carried unchanged from PR #313; it is now FIXED. These sites call
// `cancel_and_drain_or_report` (`pump_until_ready.hpp`), which alternates the cancel
// with the drain and releases exactly that waiter.
//
// ── FILE-SPECIFIC ADDENDA (everything above is verbatim from the siblings) ───
//
// `open_to_active` and `feed` take the `Session&` from their caller, so their drains
// also run while the caller's `sess` is alive -- the same "scope that still owns that
// storage" rule, reached through a parameter rather than a block-local. The same holds
// for the frame each one spans into: `open_to_active`'s `logon` and the caller's
// `stale_hb` / `frame` / `peer_logon` are named locals that outlive the call whose
// drain resumes the coroutine holding a span into them.
//
// `cfg.transport_send` here is a synchronous `std::function` and cannot park a
// coroutine; `TransportDouble` is not a `fixpp::transport::Transport` at all, only an
// in-memory frame recorder the callback appends to. So no `transport` is in play and
// the class-4 teardown gap does not apply to this file.
//
// A miss returns rather than falling through to `fut.get()`: on the false path the
// awaited coroutine is still SUSPENDED, and `get()` would block on a future nothing
// will complete.
//
// THE QUOTED `sleep_until` CLAUSE HOLDS HERE, unlike at some siblings, and it is worth
// stating because the reader's habit by now is to expect an exemption. `make_cfg()`
// sets `cfg.heartbeat_interval = 30s`, so `Session::run_liveness_loop()` resolves a
// NON-zero `heartbt_int`, passes its `HeartBtInt=0` early `co_return`, and reaches
// `co_await effective_clock_->sleep_until(deadline)` -- a real registered waiter that
// only a Clock can release. `cancel_and_drain_or_report` is load-bearing here, not the
// harmless superset it is in a fixture whose heartbeat is zero.
//
// No site in this file puts a `clock->advance()` between its window and its `get()`,
// so none of these windows is a STAGING window of the kind
// `cancellation_two_phase_test.cpp` / `tc_liveness_test.cpp` must preserve unmigrated.
//
// THE WINDOWS STAY AT 200 ms, which is also what keeps `tc_logout_test.cpp`'s
// close-grace hazard out of this file: that hazard is a pump budget GROWN past a real
// `asio::steady_timer` the mock clock does not govern, and nothing here grows one.
// `run_window_then_ready` adds one `kPumpSlice` of grace and no more.
//
// THIS IS ANOTHER COPY OF THE QUOTED SPAN, and which files share it is a MEASUREMENT,
// not a fact to cache here. An earlier revision of this addendum stated the population
// as a list of file names; a later PR amended one listed member's span in place and the
// list did not notice. It was then briefly replaced by a shell pipeline pasted into this
// comment -- which is the same defect one level up: a pipeline nothing ever runs is an
// untested instrument, and pasting the drift DETECTOR into every file that needs it
// drifts exactly as the file list did (the two pasted copies had already diverged in
// wording inside the commit that wrote them). Derive it instead:
//
//     .specify/decisions/289-data/audit-copy-span.sh
//
// Self-testing, with a look-alike in its control set and a non-vacuous demonstration
// that its one-shot extractor is load-bearing; it refuses to print a grouping it cannot
// stand behind. Files sharing a hash are the population an audit may `diff` against; a
// file that carries the heading but hashes differently has diverged DELIBERATELY, and
// that divergence is the thing to read, not to normalise away.
//
// The audit only works if this copy stays VERBATIM, not paraphrased -- an earlier
// revision elsewhere in the series paraphrased it and silently dropped a precondition.
// Keep it verbatim; put anything file-specific under this addenda heading instead.

struct SendingTimeFixture {
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine;
    TransportDouble transport;

    // Fixed "now" in the mock_clock: 2024-01-01 00:00:00 UTC
    // epoch seconds from unix = 1704067200
    static constexpr std::int64_t kNowSec = 1704067200;

    SendingTimeFixture() {
        using namespace std::chrono;
        auto utc = system_clock::time_point{} + seconds{kNowSec};
        auto stp = fixpp::core::steady_time_point{} + seconds{0};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock = clock;
        engine.executor = ioc.get_executor();
    }

    SessionConfig make_cfg(std::string_view begin_string = "FIX.4.2") {
        SessionConfig cfg;
        cfg.sender_comp_id = "ISLD";
        cfg.target_comp_id = "TW";
        cfg.begin_string = std::string(begin_string);
        cfg.heartbeat_interval = 30s;
        // Use default MaxLatency (120 s) — no sending_time_threshold override.
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override = ioc.get_executor();
        cfg.transport_send = [this](std::span<const std::byte> frame) {
            transport.capture_outbound(frame);
        };
        // RC#C (gate-b/r1): bilateral_lenient — tests here don't exercise reset semantics.
        cfg.reset_seqnum_policy_field = reset_seqnum_policy::bilateral_lenient;
        return cfg;
    }

    // Drive session to Active via initiator path.
    // Uses the mock clock's "now" (2024-01-01-00:00:00) as the valid SendingTime.
    void open_to_active(Session& sess, std::string_view begin_string = "FIX.4.2") {
        auto fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, 200ms)) {
            fixpp::test_support::cancel_and_drain_or_report(
                ioc, *clock, "SendingTimeFixture::open_to_active/open");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss
                          << "SendingTimeFixture::open_to_active/open";
            return;
        }
        ASSERT_TRUE(fut.get().has_value()) << "open() failed";

        // Feed a valid Logon with SendingTime matching mock clock now.
        // "now" = 2024-01-01 00:00:00 UTC → "20240101-00:00:00.000"
        auto logon = make_frame_with_sending_time(begin_string, "A", 1, "TW", "ISLD",
                                                  "20240101-00:00:00.000",
                                                  "98=0\x01"
                                                  "108=30\x01");
        auto fut2 = asio::co_spawn(ioc, sess.on_inbound_frame(logon), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut2, 200ms)) {
            fixpp::test_support::cancel_and_drain_or_report(
                ioc, *clock, "SendingTimeFixture::open_to_active/logon");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss
                          << "SendingTimeFixture::open_to_active/logon";
            return;
        }
        ASSERT_TRUE(fut2.get().has_value()) << "Logon-ack failed";
        ASSERT_EQ(sess.state(), fsm_state::Active);
    }

    void feed(Session& sess, std::span<const std::byte> frame) {
        auto fut = asio::co_spawn(ioc, sess.on_inbound_frame(frame), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, 200ms)) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock,
                                                            "SendingTimeFixture::feed");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss << "SendingTimeFixture::feed";
            return;
        }
        (void)fut.get();
    }
};

// ── Test 4: Q3 established-session — stale SendingTime → Reject+Logout+Disconnect
TEST(SendingTimeIntegration, StaleSendingTimeInActiveTriggersRejectThenLogout) {
    SendingTimeFixture f;
    auto cfg = f.make_cfg("FIX.4.2");
    Session sess(f.engine, cfg);
    f.open_to_active(sess);
    ASSERT_EQ(sess.state(), fsm_state::Active);

    const std::size_t before = f.transport.sent_count();

    // Feed a Heartbeat(35=0) with SendingTime 300 s in the past
    // (well beyond the 120 s MaxLatency).
    // "20231231-23:55:00.000" = 2024-01-01 00:00:00 − 300 s
    auto stale_hb =
        make_frame_with_sending_time("FIX.4.2", "0", 2, "TW", "ISLD", "20231231-23:55:00.000");
    f.feed(sess, stale_hb);

    // Must emit a Reject(35=3) with RefTagID=52, SessionRejectReason=10.
    bool found_reject = false;
    bool found_logout = false;
    for (std::size_t i = before; i < f.transport.sent_count(); ++i) {
        auto mt = extract_field(f.transport.sent(i), 35);
        if (mt == "3") {
            found_reject = true;
            EXPECT_EQ(extract_field(f.transport.sent(i), 371), "52")
                << "RefTagID(371) must be 52 (SendingTime tag)";
            EXPECT_EQ(extract_field(f.transport.sent(i), 373), "10")
                << "SessionRejectReason(373) must be 10 (SendingTime accuracy)";
        } else if (mt == "5") {
            found_logout = true;
        }
    }
    EXPECT_TRUE(found_reject)
        << "Q3: stale SendingTime in Active must emit Reject(35=3, reason=10, refTag=52)";
    EXPECT_TRUE(found_logout)
        << "Q3: stale SendingTime in Active must emit Logout(35=5) after Reject";

    // Session must be Disconnected after Reject+Logout.
    EXPECT_EQ(sess.state(), fsm_state::Disconnected)
        << "Q3: session must be Disconnected after stale-SendingTime Reject+Logout";
}

// ── Test 5: Q3 Logon-path — stale Logon SendingTime → Logout ONLY (no Reject)
//
// Per D-3/FR-013: if the offending message IS the Logon itself, there is no
// standalone Reject. The response is a logout-with-error (Logout only).
// The transport must see a Logout(35=5) but NO Reject(35=3).
//
// The session is in LogonSent state (initiator just called open()).
// The peer's Logon reply arrives with a stale SendingTime.
TEST(SendingTimeIntegration, StaleLogonSendingTimeTriggersLogoutOnlyNoReject) {
    SendingTimeFixture f;
    auto cfg = f.make_cfg("FIX.4.2");
    Session sess(f.engine, cfg);

    // Call open() — session goes to LogonSent.
    auto fut = asio::co_spawn(f.ioc, sess.open(), asio::use_future);
    if (!fixpp::test_support::run_window_then_ready(f.ioc, fut, 200ms)) {
        fixpp::test_support::cancel_and_drain_or_report(
            f.ioc, *f.clock, "StaleLogonSendingTimeTriggersLogoutOnlyNoReject/open");
        ADD_FAILURE() << fixpp::test_support::kWindowMiss
                      << "StaleLogonSendingTimeTriggersLogoutOnlyNoReject/open";
        return;
    }
    ASSERT_TRUE(fut.get().has_value()) << "open() failed";
    ASSERT_EQ(sess.state(), fsm_state::LogonSent);

    const std::size_t before = f.transport.sent_count();

    // Feed a Logon with SendingTime 300 s in the past.
    // "20231231-23:55:00.000" = now − 300 s (well beyond 120 s MaxLatency).
    auto stale_logon =
        make_frame_with_sending_time("FIX.4.2", "A", 1, "TW", "ISLD", "20231231-23:55:00.000",
                                     "98=0\x01"
                                     "108=30\x01");
    f.feed(sess, stale_logon);

    // Must emit a Logout(35=5) as the error response.
    bool found_reject = false;
    bool found_logout = false;
    for (std::size_t i = before; i < f.transport.sent_count(); ++i) {
        auto mt = extract_field(f.transport.sent(i), 35);
        if (mt == "3") {
            found_reject = true;
        }
        if (mt == "5") {
            found_logout = true;
        }
    }

    EXPECT_FALSE(found_reject)
        << "Q3 Logon-path: stale Logon SendingTime must NOT emit Reject(35=3) (D-3)";
    EXPECT_TRUE(found_logout)
        << "Q3 Logon-path: stale Logon SendingTime must emit Logout(35=5) (logout-with-error)";

    // Session must be Disconnected.
    EXPECT_EQ(sess.state(), fsm_state::Disconnected)
        << "Q3 Logon-path: session must be Disconnected after stale-Logon logout-with-error";
}

// ── T015 [US3] — FR-007/FR-008: missing/malformed SendingTime in Active/LogonReceived ──
//
// Each test: feed an inbound frame with tag 52 absent (missing) or malformed.
// Expected path: outbound Reject(35=3, 371=52, 373=10) then outbound Logout(35=5)
// then session → Disconnected.
//
// Assertions use extract_field(frame, 371) == "52" and extract_field(frame, 373) == "10"
// per the anti-pattern guard: "371=52" in FIX wire grammar means the VALUE of tag 371
// is the string "52" (the SendingTime tag number), NOT "tag 371 equals tag 52".
//
// Anchors: spec.md FR-007 + FR-008; opus_pr81_1_triage.md RC#5; tasks.md T015.

// Build a FIX frame with tag 52 ABSENT (no SendingTime field at all).
static std::vector<std::byte> make_frame_missing_sending_time(
    std::string_view begin_string, std::string_view msg_type, std::uint32_t seq,
    std::string_view sender, std::string_view target, std::string_view extra_body = {}) {
    std::string body;
    body += "35=" + std::string(msg_type) + "\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=" + std::string(sender) + "\x01";
    // Tag 52 deliberately omitted.
    body += "56=" + std::string(target) + "\x01";
    if (!extra_body.empty()) {
        body += extra_body;
    }

    std::string hdr;
    hdr += "8=" + std::string(begin_string) + "\x01";
    hdr += "9=" + std::to_string(body.size()) + "\x01";

    std::string full = hdr + body;
    unsigned int cs = 0;
    for (unsigned char c : full) {
        cs += c;
    }
    cs &= 0xFFU;
    char csbuf[4];
    snprintf(csbuf, sizeof(csbuf), "%03u", cs);
    full += "10=" + std::string(csbuf) + "\x01";

    std::vector<std::byte> frame;
    for (char c : full) {
        frame.push_back(static_cast<std::byte>(c));
    }
    return frame;
}

// Helper: assert Reject(35=3, 371=52, 373=10) + Logout(35=5) in outbound frames
// and session state == Disconnected.
// Verifies the ordering guarantee: Reject is emitted before Logout.
static void assert_reject_then_logout_then_disconnected(const TransportDouble& transport,
                                                        std::size_t before,
                                                        const fixpp::session::Session& sess,
                                                        const char* context) {
    bool found_reject = false;
    bool found_logout = false;
    int reject_pos = -1;
    int logout_pos = -1;

    for (std::size_t i = before; i < transport.sent_count(); ++i) {
        auto mt = extract_field(transport.sent(i), 35);
        if (mt == "3" && !found_reject) {
            found_reject = true;
            reject_pos = static_cast<int>(i);
            EXPECT_EQ(extract_field(transport.sent(i), 371), "52")
                << context << ": RefTagID(371) value must be \"52\" (SendingTime tag number)";
            EXPECT_EQ(extract_field(transport.sent(i), 373), "10")
                << context
                << ": SessionRejectReason(373) value must be \"10\" (SendingTime accuracy)";
        } else if (mt == "5" && !found_logout) {
            found_logout = true;
            logout_pos = static_cast<int>(i);
        }
    }

    EXPECT_TRUE(found_reject)
        << context << ": must emit Reject(35=3, 373=10, 371=52) for missing/malformed SendingTime";
    EXPECT_TRUE(found_logout) << context << ": must emit Logout(35=5) after Reject";

    if (found_reject && found_logout) {
        EXPECT_LT(reject_pos, logout_pos) << context << ": Reject must be emitted before Logout";
    }

    EXPECT_EQ(sess.state(), fsm_state::Disconnected)
        << context << ": session must be Disconnected after Reject+Logout path";
}

// T015 test 1: MissingSendingTimeInActiveRejects
// Active state + inbound Heartbeat with tag 52 absent → Reject+Logout+Disconnected.
// Anchors: spec.md FR-007.
TEST(SendingTimeIntegration, MissingSendingTimeInActiveRejects) {
    SendingTimeFixture f;
    auto cfg = f.make_cfg("FIX.4.2");
    Session sess(f.engine, cfg);
    f.open_to_active(sess);
    ASSERT_EQ(sess.state(), fsm_state::Active);

    const std::size_t before = f.transport.sent_count();

    // Heartbeat (35=0) seq=2, tag 52 absent.
    auto frame = make_frame_missing_sending_time("FIX.4.2", "0", 2, "TW", "ISLD");
    f.feed(sess, frame);

    assert_reject_then_logout_then_disconnected(f.transport, before, sess,
                                                "MissingSendingTimeInActiveRejects");
}

// T015 test 2: MalformedSendingTimeInActiveRejects
// Active state + inbound Heartbeat with tag 52=abc (non-parseable) → Reject+Logout+Disconnected.
// Anchors: spec.md FR-008.
TEST(SendingTimeIntegration, MalformedSendingTimeInActiveRejects) {
    SendingTimeFixture f;
    auto cfg = f.make_cfg("FIX.4.2");
    Session sess(f.engine, cfg);
    f.open_to_active(sess);
    ASSERT_EQ(sess.state(), fsm_state::Active);

    const std::size_t before = f.transport.sent_count();

    // Heartbeat (35=0) seq=2, tag 52=abc (not a valid FIX timestamp).
    auto frame = make_frame_with_sending_time("FIX.4.2", "0", 2, "TW", "ISLD", "abc");
    f.feed(sess, frame);

    assert_reject_then_logout_then_disconnected(f.transport, before, sess,
                                                "MalformedSendingTimeInActiveRejects");
}

// T015 test 3: MissingSendingTimeInLogonReceivedRejects
// LogonReceived state (acceptor, received peer Logon, not yet Active) + inbound
// Heartbeat with tag 52 absent → Reject+Logout+Disconnected.
// Anchors: spec.md FR-007 (applies to both Active and LogonReceived rows per §US3 AC).
TEST(SendingTimeIntegration, MissingSendingTimeInLogonReceivedRejects) {
    SendingTimeFixture f;
    // Use acceptor role to reach LogonReceived without completing handshake.
    auto cfg = f.make_cfg("FIX.4.2");
    cfg.role = fixpp::session::session_role::acceptor;
    Session sess(f.engine, cfg);

    // open() as acceptor → NotConnected (stays).
    auto fut_open = asio::co_spawn(f.ioc, sess.open(), asio::use_future);
    if (!fixpp::test_support::run_window_then_ready(f.ioc, fut_open, 200ms)) {
        fixpp::test_support::cancel_and_drain_or_report(
            f.ioc, *f.clock, "MissingSendingTimeInLogonReceivedRejects/open");
        ADD_FAILURE() << fixpp::test_support::kWindowMiss
                      << "MissingSendingTimeInLogonReceivedRejects/open";
        return;
    }
    ASSERT_TRUE(fut_open.get().has_value()) << "open() failed";
    ASSERT_EQ(sess.state(), fsm_state::NotConnected);

    // Feed valid peer Logon → NotConnected → (LogonReceived) → Active.
    // F1 (Round-A drift fix): acceptor now emits reply Logon and transitions to Active
    // within on_inbound_frame; LogonReceived is internal/transient. After feed_sync
    // the state is Active (spec.md FR-005 §US2 AC2).
    auto peer_logon =
        make_frame_with_sending_time("FIX.4.2", "A", 1, "TW", "ISLD", "20240101-00:00:00.000",
                                     "98=0\x01"
                                     "108=30\x01");
    f.feed(sess, peer_logon);
    ASSERT_EQ(sess.state(), fsm_state::Active)
        << "acceptor must be in Active after valid peer Logon (F1 drift fix: LogonReceived is "
           "transient)";

    const std::size_t before = f.transport.sent_count();

    // Heartbeat (35=0) seq=2 with tag 52 absent.
    auto frame = make_frame_missing_sending_time("FIX.4.2", "0", 2, "TW", "ISLD");
    f.feed(sess, frame);

    assert_reject_then_logout_then_disconnected(f.transport, before, sess,
                                                "MissingSendingTimeInLogonReceivedRejects");
}

// ── T017 [US3] — FR-009: missing/malformed SendingTime on inbound Logon (LogonSent) ──
//
// D-3 LogonSent-special path: NO standalone Reject before establishment.
// Response is Logout(58=<error text>) ONLY. No Reject(35=3) must be emitted.
// Session → Disconnected.
//
// Anchors: spec.md FR-009; research.md D-3; [FIX-SL §4.3]; tasks.md T017.

// T017 test 1: MissingSendingTimeOnLogonEmitsLogoutOnly
// LogonSent state + inbound Logon with tag 52 absent → Logout only (no Reject) + Disconnected.
TEST(SendingTimeIntegration, MissingSendingTimeOnLogonEmitsLogoutOnly) {
    SendingTimeFixture f;
    auto cfg = f.make_cfg("FIX.4.2");
    Session sess(f.engine, cfg);

    // open() as initiator → LogonSent.
    auto fut = asio::co_spawn(f.ioc, sess.open(), asio::use_future);
    if (!fixpp::test_support::run_window_then_ready(f.ioc, fut, 200ms)) {
        fixpp::test_support::cancel_and_drain_or_report(
            f.ioc, *f.clock, "MissingSendingTimeOnLogonEmitsLogoutOnly/open");
        ADD_FAILURE() << fixpp::test_support::kWindowMiss
                      << "MissingSendingTimeOnLogonEmitsLogoutOnly/open";
        return;
    }
    ASSERT_TRUE(fut.get().has_value()) << "open() failed";
    ASSERT_EQ(sess.state(), fsm_state::LogonSent);

    const std::size_t before = f.transport.sent_count();

    // Peer Logon with tag 52 absent (no SendingTime field).
    auto stale_logon = make_frame_missing_sending_time("FIX.4.2", "A", 1, "TW", "ISLD",
                                                       "98=0\x01"
                                                       "108=30\x01");
    f.feed(sess, stale_logon);

    // Must NOT emit Reject(35=3). MUST emit Logout(35=5). Session → Disconnected.
    bool found_reject = false;
    bool found_logout = false;
    bool found_logout_text = false;
    for (std::size_t i = before; i < f.transport.sent_count(); ++i) {
        auto mt = extract_field(f.transport.sent(i), 35);
        if (mt == "3") {
            found_reject = true;
        }
        if (mt == "5") {
            found_logout = true;
            // Tag 58 (Text) must be present with an error description.
            auto text = extract_field(f.transport.sent(i), 58);
            if (!text.empty()) {
                found_logout_text = true;
            }
        }
    }

    EXPECT_FALSE(found_reject)
        << "MissingSendingTimeOnLogonEmitsLogoutOnly: D-3 LogonSent-special: "
           "NO standalone Reject(35=3) before establishment";
    EXPECT_TRUE(found_logout) << "MissingSendingTimeOnLogonEmitsLogoutOnly: must emit Logout(35=5) "
                                 "as logout-with-error response";
    EXPECT_TRUE(found_logout_text)
        << "MissingSendingTimeOnLogonEmitsLogoutOnly: Logout must carry Text(58) error";
    EXPECT_EQ(sess.state(), fsm_state::Disconnected)
        << "MissingSendingTimeOnLogonEmitsLogoutOnly: session must be Disconnected";
}

// T017 test 2: MalformedSendingTimeOnLogonEmitsLogoutOnly
// LogonSent state + inbound Logon with tag 52=abc (malformed) → Logout only + Disconnected.
TEST(SendingTimeIntegration, MalformedSendingTimeOnLogonEmitsLogoutOnly) {
    SendingTimeFixture f;
    auto cfg = f.make_cfg("FIX.4.2");
    Session sess(f.engine, cfg);

    // open() as initiator → LogonSent.
    auto fut = asio::co_spawn(f.ioc, sess.open(), asio::use_future);
    if (!fixpp::test_support::run_window_then_ready(f.ioc, fut, 200ms)) {
        fixpp::test_support::cancel_and_drain_or_report(
            f.ioc, *f.clock, "MalformedSendingTimeOnLogonEmitsLogoutOnly/open");
        ADD_FAILURE() << fixpp::test_support::kWindowMiss
                      << "MalformedSendingTimeOnLogonEmitsLogoutOnly/open";
        return;
    }
    ASSERT_TRUE(fut.get().has_value()) << "open() failed";
    ASSERT_EQ(sess.state(), fsm_state::LogonSent);

    const std::size_t before = f.transport.sent_count();

    // Peer Logon with tag 52=abc (not a valid FIX timestamp).
    auto malformed_logon = make_frame_with_sending_time("FIX.4.2", "A", 1, "TW", "ISLD",
                                                        "abc",  // malformed timestamp
                                                        "98=0\x01"
                                                        "108=30\x01");
    f.feed(sess, malformed_logon);

    // Must NOT emit Reject(35=3). MUST emit Logout(35=5). Session → Disconnected.
    bool found_reject = false;
    bool found_logout = false;
    bool found_logout_text = false;
    for (std::size_t i = before; i < f.transport.sent_count(); ++i) {
        auto mt = extract_field(f.transport.sent(i), 35);
        if (mt == "3") {
            found_reject = true;
        }
        if (mt == "5") {
            found_logout = true;
            auto text = extract_field(f.transport.sent(i), 58);
            if (!text.empty()) {
                found_logout_text = true;
            }
        }
    }

    EXPECT_FALSE(found_reject)
        << "MalformedSendingTimeOnLogonEmitsLogoutOnly: D-3 LogonSent-special: "
           "NO standalone Reject(35=3) before establishment";
    EXPECT_TRUE(found_logout)
        << "MalformedSendingTimeOnLogonEmitsLogoutOnly: must emit Logout(35=5) "
           "as logout-with-error response";
    EXPECT_TRUE(found_logout_text)
        << "MalformedSendingTimeOnLogonEmitsLogoutOnly: Logout must carry Text(58) error";
    EXPECT_EQ(sess.state(), fsm_state::Disconnected)
        << "MalformedSendingTimeOnLogonEmitsLogoutOnly: session must be Disconnected";
}

}  // namespace
}  // namespace fixpp::session::test
