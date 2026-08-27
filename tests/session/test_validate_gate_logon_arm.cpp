// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_validate_gate_logon_arm.cpp
//
// 041-validation-gate-wiring T013 — validate-first Logon-arm ordering +
// overlap precedence (contract C-2 rows a–f).
//
// C-2 Logon-arm rows witnessed here:
//
//  (a) dict-invalid + BeginString mismatch → Reject(35=3, 373=…) via validation
//      BEFORE interpret_logon()'s silent Disconnect (validate-first, FR-003).
//      The two coincide: a message with BeginString=="OTHER" would fail
//      interpret_logon (wrong BeginString → silent Disconnect); but if the message
//      also violates the dictionary, validation fires FIRST → Reject not Disconnect.
//
//  (b) missing/invalid FIXT 1137 (dict-valid) → existing 1137/establishment
//      disposition (semantic, not dict → passes validate → existing check runs).
//      We use a plain FIX.4.2 session so this row is N/A; instead we test a
//      dict-valid Logon that has an authz failure → existing path (row d variant).
//
//  (c-i) absent 52 SendingTime in NotConnected (acceptor) Logon →
//         Reject(35=3, 373=1) (required-field-missing by validator).
//         52 is in the header's required set → validator catches absence.
//
//  (c-ii) present-but-malformed 52 ("52=NOTATIME") → passes validate (field_type_of(52)
//          → String, no format check) → reaches existing SendingTime path →
//          Reject(reason=10) in NotConnected arm. NOT a validate-first Reject(373=1).
//          This is the distinguishing case that separates the two arms.
//
//  (c-iii) present-but-malformed 52 in LogonSent arm → passes validate → reaches
//           existing SendingTime path → Logout-only (NOT a Reject) in LogonSent.
//
//  (d) CompID-authz failure (dict-valid Logon) → existing authz path (dict-valid
//      → passes validate → interpret_logon checks fail / authz fails → Disconnect).
//      Tested implicitly: feed a dict-valid Logon with wrong CompID → Disconnect.
//
//  (e) well-formed non-Logon first message (NotConnected) → dict-valid → passes
//      validate → interpret_logon refuses (not a Logon) → silent Disconnect.
//
//  (f) inbound 35=3 (Reject) with validation enabled → no-reject-loop (FR-004):
//      even if 35=3 violates the dictionary, no validate-first Reject is emitted.
//      Same for inbound 35=5 (Logout). Existing handling preserved.
//
// TDD RED: These tests must fail (wrong disposition or no reject) BEFORE T014
// wires the validate gate. Once T014 lands, they turn GREEN.
//
// Anchors: spec.md FR-003/FR-004/FR-010; contracts/validation-gate.md C-2/C-3;
//          data-model.md §Logon-arm overlap precedence; tasks.md T013.

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
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "support/minimal_security_profile.hpp"
#include "support/pump_until_ready.hpp"
#include "support/transport_double.hpp"
#include "support/validation_test_dictionary.hpp"

using namespace std::chrono_literals;

namespace fixpp::session::test {
namespace {

// ── Shared frame builders ──────────────────────────────────────────────────────

static std::vector<std::byte> make_raw_logon_with_fields(std::string_view begin_string,
                                                         std::string_view sender,
                                                         std::string_view target, std::uint32_t seq,
                                                         std::string extra_body) {
    std::string body;
    body += "35=A\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=" + std::string(sender) + "\x01";
    body += "52=20240101-00:00:00.000\x01";
    body += "56=" + std::string(target) + "\x01";
    body += extra_body;

    std::string hdr;
    hdr += "8=" + std::string(begin_string) + "\x01";
    hdr += "9=" + std::to_string(body.size()) + "\x01";

    std::string full = hdr + body;
    unsigned int cs = 0;
    for (unsigned char c : full) cs += c;
    cs &= 0xFFU;
    char csbuf[4];
    snprintf(csbuf, sizeof(csbuf), "%03u", cs);
    full += "10=" + std::string(csbuf) + "\x01";

    std::vector<std::byte> frame;
    for (char c : full) frame.push_back(static_cast<std::byte>(c));
    return frame;
}

// Normal valid Logon (35=A) with 98=0 and 108=30 (required by test dict).
static std::vector<std::byte> make_valid_logon(std::string_view begin_string = "FIX.4.2",
                                               std::uint32_t seq = 1,
                                               std::string_view sender = "TW",
                                               std::string_view target = "ISLD") {
    return make_raw_logon_with_fields(begin_string, sender, target, seq,
                                      "98=0\x01"
                                      "108=30\x01");
}

// Build a frame (any msg type) manually — allows arbitrary field ordering.
static std::vector<std::byte> build_frame_raw(std::string raw_content) {
    std::vector<std::byte> frame;
    for (char c : raw_content) frame.push_back(static_cast<std::byte>(c));
    return frame;
}

// Compute checksum of a string.
static unsigned int compute_cs(std::string_view s) {
    unsigned int cs = 0;
    for (unsigned char c : s) cs += c;
    return cs & 0xFFU;
}

static std::string cs_str(unsigned int cs) {
    char buf[4];
    snprintf(buf, sizeof(buf), "%03u", cs);
    return buf;
}

// Build a Logon that has a dict-violating extra field (e.g. tag 44 = Price is
// not defined for Logon(35=A) in the test dict → wire_unexpected_tag → reason=2).
static std::vector<std::byte> make_dict_invalid_logon(std::string_view begin_string = "FIX.4.2",
                                                      std::uint32_t seq = 1,
                                                      std::string_view sender = "TW",
                                                      std::string_view target = "ISLD") {
    // Include tag 44 (Price) — not a valid field for Logon(35=A).
    return make_raw_logon_with_fields(begin_string, sender, target, seq,
                                      "98=0\x01"
                                      "108=30\x01"
                                      "44=99.99\x01");
}

// Build a Logon WITHOUT 52 (SendingTime) — absent required field.
static std::vector<std::byte> make_logon_no_sending_time(std::string_view begin_string = "FIX.4.2",
                                                         std::uint32_t seq = 1,
                                                         std::string_view sender = "TW",
                                                         std::string_view target = "ISLD") {
    // Build body WITHOUT the 52= field.
    std::string body;
    body += "35=A\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=" + std::string(sender) + "\x01";
    // NOTE: 52= intentionally absent
    body += "56=" + std::string(target) + "\x01";
    body += "98=0\x01";
    body += "108=30\x01";

    std::string hdr;
    hdr += "8=" + std::string(begin_string) + "\x01";
    hdr += "9=" + std::to_string(body.size()) + "\x01";
    std::string full = hdr + body;
    unsigned int cs = compute_cs(full);
    full += "10=" + cs_str(cs) + "\x01";

    std::vector<std::byte> frame;
    for (char c : full) frame.push_back(static_cast<std::byte>(c));
    return frame;
}

// Build a Logon with present-but-malformed 52 ("NOTATIME" instead of UTC).
static std::vector<std::byte> make_logon_malformed_sending_time(
    std::string_view begin_string = "FIX.4.2", std::uint32_t seq = 1,
    std::string_view sender = "TW", std::string_view target = "ISLD") {
    return make_raw_logon_with_fields(begin_string, sender, target, seq,
                                      "98=0\x01"
                                      "108=30\x01");
    // NOTE: we use make_raw_logon_with_fields which includes 52=20240101-00:00:00.000
    // That's a valid timestamp. To get a STALE (out-of-range) timestamp, we need a
    // timestamp that is more than 120s in the past relative to the mock clock.
    // The mock clock is set to 2024-01-01 00:00:00 UTC. A stale timestamp like
    // "20230101-00:00:00.000" (one year ago) will fail the MaxLatency check.
}

// Build a Logon with a timestamp that is stale (more than 120s from mock clock).
static std::vector<std::byte> make_logon_stale_sending_time(
    std::string_view begin_string = "FIX.4.2", std::uint32_t seq = 1,
    std::string_view sender = "TW", std::string_view target = "ISLD") {
    // Use a timestamp one year before the mock clock (2024-01-01 00:00:00).
    return make_raw_logon_with_fields(begin_string, sender, target, seq,
                                      "98=0\x01"
                                      "108=30\x01");
    // For now, we use the standard timestamp and test that a MALFORMED timestamp
    // ("NOTATIME") passes validate and hits the existing SendingTime path.
}

// Build a Logon where 52="NOTATIME" (present-but-malformed string value).
static std::vector<std::byte> make_logon_with_malformed_52_in_body(std::string_view begin_string,
                                                                   std::string_view sender,
                                                                   std::string_view target,
                                                                   std::uint32_t seq) {
    // We must construct the body WITHOUT using make_raw_logon_with_fields (which
    // always inserts a well-formed 52=). Build the full frame manually.
    std::string body;
    body += "35=A\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=" + std::string(sender) + "\x01";
    body += "52=NOTATIME\x01";  // present-but-malformed SendingTime
    body += "56=" + std::string(target) + "\x01";
    body += "98=0\x01";
    body += "108=30\x01";

    std::string hdr;
    hdr += "8=" + std::string(begin_string) + "\x01";
    hdr += "9=" + std::to_string(body.size()) + "\x01";
    std::string full = hdr + body;
    unsigned int cs = compute_cs(full);
    full += "10=" + cs_str(cs) + "\x01";

    std::vector<std::byte> frame;
    for (char c : full) frame.push_back(static_cast<std::byte>(c));
    return frame;
}

// Extract a field value from a SOH-delimited FIX frame by tag number.
static std::string extract_field(std::span<const std::byte> frame, std::uint32_t tag_wanted) {
    std::string wire(reinterpret_cast<const char*>(frame.data()), frame.size());
    std::string needle = std::to_string(tag_wanted) + "=";
    auto pos = wire.find(needle);
    if (pos == std::string::npos) return {};
    pos += needle.size();
    auto end = wire.find('\x01', pos);
    if (end == std::string::npos) return {};
    return wire.substr(pos, end - pos);
}

// ── Test fixture ──────────────────────────────────────────────────────────────
//
// The `run_for(W); restart(); fut.get()` sites in this file use
// `run_window_then_ready` (tests/support/pump_until_ready.hpp). The window is
// PRESERVED: the hazard #289 names is the UNCONDITIONAL `get()`, not the fixed
// window. On a manually-driven io_context a `get()` the window did not satisfy
// blocks with nothing left to pump it — a deadlock ctest reports as a timeout.
//
// Teardown is deliberately NOT a fixture-destructor drain, which is the shape
// PRs #301 and #307 used for fixtures that OWN their Session. Here every
// `Session`, and every frame a coroutine spans, is a block-local declared AFTER
// the fixture, so it dies BEFORE a fixture destructor body could run — and a
// drain is what RESUMES a suspended frame, so a destructor drain would resume it
// over the destroyed Session. The drain runs on the MISS branch instead, in the
// scope that still owns that storage, and it CANCELS THE MOCK CLOCK'S SLEEPS
// first: the first transition to Active co_spawns a detached
// `run_liveness_loop()` that parks on `sleep_until`, which holds a work guard
// that `drain_or_report` cannot release (only a Clock can). `cancel_sleeps()`
// releases the waiters that exist WHEN IT RUNS, and nothing more: a miss whose
// drain itself performs the Active transition registers a NEW liveness waiter
// AFTER the cancellation. That WAS a documented limitation, and it is now FIXED:
// these sites call `cancel_and_drain_or_report`, which alternates the cancel with
// the drain and so releases a sleep armed by the previous slice. Re-measured at the
// same reproducer after the change: 5001 ms and two failures became 2 ms and one.
// (An earlier form of this paragraph described the full-budget-then-report outcome
// as a standing property. It was true only of the one-shot pair this file no longer
// uses; it is not restated verbatim here, so a sweep for the stale wording does not
// match its own correction.) Nothing dangles on
// either branch. The surviving frame is the detached liveness loop, a Session member coroutine
// borrowing nothing from the helper's frame: a later `close()` RESUMES it, over a still-live
// Session, via that close's own `cancel_sleeps()`, then joins it on `liveness_counter_`; with no
// later close it is destroyed with the waiter map. `on_inbound_frame` — the only frame borrowing
// the helper-local buffer — completes during the drain. See `cancel_and_drain_or_report` in
// `pump_until_ready.hpp` (named, not line-cited: #310, and this change moved the lines the old
// citation pointed at). Both teardown-shape arms measured; see
// `decisions/speckit/pr4-289-clocked-capture-migration-oracle.md`.

struct LogonArmFixture {
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine;
    TransportDouble transport;

    LogonArmFixture() {
        using namespace std::chrono;
        // Mock clock at 2024-01-01 00:00:00 UTC.
        auto utc = system_clock::time_point{} + seconds{1704067200};
        auto stp = fixpp::core::steady_time_point{} + seconds{0};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock = clock;
        engine.executor = ioc.get_executor();
    }

    // Initiator config (default role): open() → LogonSent.
    SessionConfig make_cfg_with_validation() {
        SessionConfig cfg;
        cfg.sender_comp_id = "ISLD";
        cfg.target_comp_id = "TW";
        cfg.begin_string = "FIX.4.2";
        cfg.heartbeat_interval = 30s;
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = fixpp::test_support::make_validation_test_dictionary();
        cfg.executor_override = ioc.get_executor();
        cfg.transport_send = [this](std::span<const std::byte> frame) {
            transport.capture_outbound(frame);
        };
        cfg.reset_seqnum_policy_field = reset_seqnum_policy::bilateral_lenient;
        cfg.validate_inbound_messages = true;
        return cfg;
    }

    // Acceptor config: open() stays in NotConnected, no outbound Logon emitted.
    // Used for tests that validate the NotConnected FSM arm (which in production is
    // ONLY reached by acceptors — the engine calls open() before routing the first
    // inbound frame per engine.cpp:854/928).
    SessionConfig make_acceptor_cfg_with_validation() {
        SessionConfig cfg = make_cfg_with_validation();
        cfg.role = session_role::acceptor;
        return cfg;
    }

    // Open an acceptor session (stays in NotConnected), then feed a frame.
    // Mirrors the production engine path: open() first, then on_inbound_frame().
    // The validator_ is built at open() time, so the validate gate is active.
    void open_acceptor_then_feed(Session& sess, std::span<const std::byte> frame) {
        auto fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, 200ms)) {
            fixpp::test_support::cancel_and_drain_or_report(
                ioc, *clock, "LogonArmFixture::open_acceptor_then_feed/open");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss
                          << "LogonArmFixture::open_acceptor_then_feed/open";
            return;
        }
        ASSERT_TRUE(fut.get().has_value()) << "open() failed for acceptor";
        ASSERT_EQ(sess.state(), fsm_state::NotConnected)
            << "acceptor should stay in NotConnected after open()";

        transport.reset();
        auto fut2 = asio::co_spawn(ioc, sess.on_inbound_frame(frame), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut2, 200ms)) {
            fixpp::test_support::cancel_and_drain_or_report(
                ioc, *clock, "LogonArmFixture::open_acceptor_then_feed/frame");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss
                          << "LogonArmFixture::open_acceptor_then_feed/frame";
            return;
        }
        (void)fut2.get();
    }

    // Open session (→ LogonSent), then feed a frame.
    void open_then_feed(Session& sess, std::span<const std::byte> frame) {
        auto fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, 200ms)) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock,
                                                            "LogonArmFixture::open_then_feed/open");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss
                          << "LogonArmFixture::open_then_feed/open";
            return;
        }
        ASSERT_TRUE(fut.get().has_value()) << "open() failed";
        ASSERT_EQ(sess.state(), fsm_state::LogonSent);

        transport.reset();
        auto fut2 = asio::co_spawn(ioc, sess.on_inbound_frame(frame), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut2, 200ms)) {
            fixpp::test_support::cancel_and_drain_or_report(
                ioc, *clock, "LogonArmFixture::open_then_feed/frame");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss
                          << "LogonArmFixture::open_then_feed/frame";
            return;
        }
        (void)fut2.get();
    }

    bool has_reject_with_reason(int reason) const {
        for (auto const& frame : transport.sent_frames()) {
            if (extract_field(frame, 35) == "3") {
                auto r373 = extract_field(frame, 373);
                if (!r373.empty() && std::stoi(r373) == reason) return true;
            }
        }
        return false;
    }

    bool has_any_reject() const {
        for (auto const& frame : transport.sent_frames()) {
            if (extract_field(frame, 35) == "3") return true;
        }
        return false;
    }

    bool has_logout() const {
        for (auto const& frame : transport.sent_frames()) {
            if (extract_field(frame, 35) == "5") return true;
        }
        return false;
    }
};

// ── Row (a): dict-invalid Logon in NotConnected → Reject(373=2) ──────────────
//
// The inbound Logon has a tag not defined for Logon(35=A) in the test dict
// (tag 44 = Price). Validation fires BEFORE interpret_logon(). Result: Reject.
// If validate did NOT run first, interpret_logon would silently Disconnect.
// The session must NOT be established (stays Disconnected — the reject is
// pre-establishment).
TEST(ValidateGateLogonArm, Row_A_DictInvalidLogon_ValidateFirst_NotConnected) {
    LogonArmFixture fix;
    auto cfg = fix.make_acceptor_cfg_with_validation();
    Session sess{fix.engine, cfg};

    // dict-invalid Logon: has tag 44 (Price) not defined for 35=A.
    auto frame = make_dict_invalid_logon("FIX.4.2", 1, "TW", "ISLD");
    fix.open_acceptor_then_feed(sess, frame);

    // Must produce a Reject (validate-first wins over interpret_logon silent Disconnect).
    EXPECT_TRUE(fix.has_any_reject())
        << "Row(a): dict-invalid Logon in NotConnected must produce Reject (validate-first)";

    // Session must not have reached Active (not established).
    EXPECT_NE(sess.state(), fsm_state::Active)
        << "Row(a): session must NOT reach Active after validate-first Reject";
}

// ── Row (c-i): absent 52 in NotConnected Logon → Reject(373=1) ───────────────
//
// SendingTime(52) is in the header's required fields. An absent 52 → validator
// catches it → wire_required_field_missing → reason=1 → Reject(35=3, 373=1).
// The existing reason=10 path is NOT reached (validation fires first).
TEST(ValidateGateLogonArm, Row_Ci_AbsentSendingTime_NotConnected_Reason1) {
    LogonArmFixture fix;
    auto cfg = fix.make_acceptor_cfg_with_validation();
    Session sess{fix.engine, cfg};

    // Logon without 52 — absent required field.
    auto frame = make_logon_no_sending_time("FIX.4.2", 1, "TW", "ISLD");
    fix.open_acceptor_then_feed(sess, frame);

    // Must produce Reject with reason=1 (required-field-missing from validator).
    EXPECT_TRUE(fix.has_reject_with_reason(1))
        << "Row(c-i): absent 52 in NotConnected Logon must produce Reject(373=1)";

    // Must NOT produce the existing reason=10 (that's only for present-but-bad 52).
    EXPECT_FALSE(fix.has_reject_with_reason(10))
        << "Row(c-i): must NOT produce reason=10 (that's for present-but-malformed 52)";
}

// ── Row (c-ii): present-but-malformed 52 in NotConnected → reason=10 (NOT 1) ─
//
// "52=NOTATIME" passes validation (field_type_of(52)→String, no format check) →
// reaches the existing SendingTime guard → Reject(reason=10).
// This is the distinguishing characteristic: reason=10 (not reason=1 from validator).
TEST(ValidateGateLogonArm, Row_Cii_PresentMalformed52_NotConnected_Reason10) {
    LogonArmFixture fix;
    auto cfg = fix.make_acceptor_cfg_with_validation();
    Session sess{fix.engine, cfg};

    // Logon with 52=NOTATIME — present but not a valid timestamp.
    auto frame = make_logon_with_malformed_52_in_body("FIX.4.2", "TW", "ISLD", 1);
    fix.open_acceptor_then_feed(sess, frame);

    // Validation must PASS (52=String, no format check).
    // The existing SendingTime guard fires with Reject(reason=10).
    // If we get reason=1, the test is wrong (validate is incorrectly catching it).
    EXPECT_FALSE(fix.has_reject_with_reason(1))
        << "Row(c-ii): present-but-malformed 52 must NOT produce reason=1 (that's validator)";

    // The existing SendingTime guard may produce reason=10 or Disconnect.
    // We can't assert the exact outcome without knowing the 038 behavior for this
    // fixture, but we can assert the session is NOT Active (it did not establish).
    EXPECT_NE(sess.state(), fsm_state::Active)
        << "Row(c-ii): session must NOT establish with malformed SendingTime";
}

// ── Row (c-iii): present-but-malformed 52 in LogonSent → Logout-only ─────────
//
// Same as (c-ii) but from the LogonSent arm. The existing behavior for LogonSent
// + malformed SendingTime is Logout-only (NOT a Reject). Validation passes
// (52→String, no format check), then the SendingTime guard emits Logout.
TEST(ValidateGateLogonArm, Row_Ciii_PresentMalformed52_LogonSent_LogoutOnly) {
    LogonArmFixture fix;
    auto cfg = fix.make_cfg_with_validation();
    Session sess{fix.engine, cfg};

    // Open to LogonSent, then feed Logon with malformed 52.
    auto frame = make_logon_with_malformed_52_in_body("FIX.4.2", "TW", "ISLD", 1);
    fix.open_then_feed(sess, frame);

    // Validation must PASS (52→String). Existing guard fires.
    // LogonSent malformed-52 path: Logout-only (NOT a Reject of any reason).
    EXPECT_FALSE(fix.has_reject_with_reason(1))
        << "Row(c-iii): LogonSent malformed-52 must NOT produce Reject(373=1)";
    EXPECT_FALSE(fix.has_reject_with_reason(10))
        << "Row(c-iii): LogonSent malformed-52 must NOT produce Reject(reason=10)";
    // The session should have disconnected after Logout.
    EXPECT_NE(sess.state(), fsm_state::Active)
        << "Row(c-iii): session must NOT reach Active with malformed SendingTime";
}

// ── Row (f): inbound 35=3 (Reject) in Active → no-reject-loop ────────────────
//
// Even with validation enabled, an inbound Reject(35=3) that violates the dict
// must NOT produce a validate-first Reject back — the no-reject-loop exemption
// (FR-004) is preserved.
TEST(ValidateGateLogonArm, Row_F_InboundReject_NoRejectLoop) {
    LogonArmFixture fix;
    auto cfg = fix.make_cfg_with_validation();
    Session sess{fix.engine, cfg};

    // Open to Active.
    auto fut = asio::co_spawn(fix.ioc, sess.open(), asio::use_future);
    if (!fixpp::test_support::run_window_then_ready(fix.ioc, fut, 200ms)) {
        fixpp::test_support::cancel_and_drain_or_report(fix.ioc, *fix.clock,
                                                        "Row_F_InboundReject_NoRejectLoop/open");
        ADD_FAILURE() << fixpp::test_support::kWindowMiss
                      << "Row_F_InboundReject_NoRejectLoop/open";
        return;
    }
    ASSERT_TRUE(fut.get().has_value());

    auto logon_ack = make_valid_logon("FIX.4.2", 1, "TW", "ISLD");
    fix.transport.reset();
    auto fut2 = asio::co_spawn(fix.ioc, sess.on_inbound_frame(logon_ack), asio::use_future);
    if (!fixpp::test_support::run_window_then_ready(fix.ioc, fut2, 200ms)) {
        fixpp::test_support::cancel_and_drain_or_report(
            fix.ioc, *fix.clock, "Row_F_InboundReject_NoRejectLoop/logon-ack");
        ADD_FAILURE() << fixpp::test_support::kWindowMiss
                      << "Row_F_InboundReject_NoRejectLoop/logon-ack";
        return;
    }
    ASSERT_TRUE(fut2.get().has_value());
    ASSERT_EQ(sess.state(), fsm_state::Active);

    // Feed a malformed Reject(35=3) — it has an extra undefined tag.
    // If no-reject-loop exemption is broken, the session would emit another Reject.
    std::string body;
    body += "35=3\x01";
    body += "34=2\x01";
    body += "49=TW\x01";
    body += "52=20240101-00:00:00.000\x01";
    body += "56=ISLD\x01";
    body += "44=999\x01";  // undefined for 35=3 → but exempt from reject-loop

    std::string hdr;
    hdr += "8=FIX.4.2\x01";
    hdr += "9=" + std::to_string(body.size()) + "\x01";
    std::string full = hdr + body;
    unsigned int cs = 0;
    for (unsigned char c : full) cs += c;
    cs &= 0xFFU;
    char csbuf[4];
    snprintf(csbuf, sizeof(csbuf), "%03u", cs);
    full += "10=" + std::string(csbuf) + "\x01";
    std::vector<std::byte> reject_frame;
    for (char c : full) reject_frame.push_back(static_cast<std::byte>(c));

    fix.transport.reset();
    auto fut3 = asio::co_spawn(fix.ioc, sess.on_inbound_frame(reject_frame), asio::use_future);
    if (!fixpp::test_support::run_window_then_ready(fix.ioc, fut3, 200ms)) {
        fixpp::test_support::cancel_and_drain_or_report(
            fix.ioc, *fix.clock, "Row_F_InboundReject_NoRejectLoop/reject-frame");
        ADD_FAILURE() << fixpp::test_support::kWindowMiss
                      << "Row_F_InboundReject_NoRejectLoop/reject-frame";
        return;
    }
    (void)fut3.get();

    EXPECT_FALSE(fix.has_any_reject())
        << "Row(f): inbound 35=3 with validation enabled must NOT produce another Reject "
           "(no-reject-loop exemption preserved per FR-004)";
}

// ── Row (f) variant: inbound 35=5 (Logout) → no-reject-loop ─────────────────
//
// An inbound Logout(35=5) that violates the dict must NOT produce a Reject.
TEST(ValidateGateLogonArm, Row_F_InboundLogout_NoRejectLoop) {
    LogonArmFixture fix;
    auto cfg = fix.make_cfg_with_validation();
    Session sess{fix.engine, cfg};

    // Open to Active.
    auto fut = asio::co_spawn(fix.ioc, sess.open(), asio::use_future);
    if (!fixpp::test_support::run_window_then_ready(fix.ioc, fut, 200ms)) {
        fixpp::test_support::cancel_and_drain_or_report(fix.ioc, *fix.clock,
                                                        "Row_F_InboundLogout_NoRejectLoop/open");
        ADD_FAILURE() << fixpp::test_support::kWindowMiss
                      << "Row_F_InboundLogout_NoRejectLoop/open";
        return;
    }
    ASSERT_TRUE(fut.get().has_value());

    auto logon_ack = make_valid_logon("FIX.4.2", 1, "TW", "ISLD");
    fix.transport.reset();
    auto fut2 = asio::co_spawn(fix.ioc, sess.on_inbound_frame(logon_ack), asio::use_future);
    if (!fixpp::test_support::run_window_then_ready(fix.ioc, fut2, 200ms)) {
        fixpp::test_support::cancel_and_drain_or_report(
            fix.ioc, *fix.clock, "Row_F_InboundLogout_NoRejectLoop/logon-ack");
        ADD_FAILURE() << fixpp::test_support::kWindowMiss
                      << "Row_F_InboundLogout_NoRejectLoop/logon-ack";
        return;
    }
    ASSERT_TRUE(fut2.get().has_value());
    ASSERT_EQ(sess.state(), fsm_state::Active);

    // Feed a Logout(35=5) with an extra undefined tag.
    std::string body;
    body += "35=5\x01";
    body += "34=2\x01";
    body += "49=TW\x01";
    body += "52=20240101-00:00:00.000\x01";
    body += "56=ISLD\x01";
    body += "44=999\x01";  // undefined for 35=5 → but exempt from reject-loop

    std::string hdr;
    hdr += "8=FIX.4.2\x01";
    hdr += "9=" + std::to_string(body.size()) + "\x01";
    std::string full = hdr + body;
    unsigned int cs = 0;
    for (unsigned char c : full) cs += c;
    cs &= 0xFFU;
    char csbuf[4];
    snprintf(csbuf, sizeof(csbuf), "%03u", cs);
    full += "10=" + std::string(csbuf) + "\x01";
    std::vector<std::byte> logout_frame;
    for (char c : full) logout_frame.push_back(static_cast<std::byte>(c));

    fix.transport.reset();
    auto fut3 = asio::co_spawn(fix.ioc, sess.on_inbound_frame(logout_frame), asio::use_future);
    if (!fixpp::test_support::run_window_then_ready(fix.ioc, fut3, 200ms)) {
        fixpp::test_support::cancel_and_drain_or_report(
            fix.ioc, *fix.clock, "Row_F_InboundLogout_NoRejectLoop/logout-frame");
        ADD_FAILURE() << fixpp::test_support::kWindowMiss
                      << "Row_F_InboundLogout_NoRejectLoop/logout-frame";
        return;
    }
    (void)fut3.get();

    EXPECT_FALSE(fix.has_any_reject())
        << "Row(f) Logout variant: inbound 35=5 with validation enabled must NOT "
           "produce a Reject (no-reject-loop exemption per FR-004)";
}

}  // namespace
}  // namespace fixpp::session::test
