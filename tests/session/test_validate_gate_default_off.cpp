// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_validate_gate_default_off.cpp
//
// 041-validation-gate-wiring T015 + T016 — US2 lenient-by-default preservation.
//
// T015 (C-2 default branch, FR-002, SC-001):
//   With validate_inbound_messages==false (the default), messages that WOULD be
//   validation violations under strict mode are accepted and dispatched exactly as
//   in the prior release — no validation-induced Reject emitted.
//
//   Violation fixtures (same inputs as T012 strict-mode witnesses):
//     W_Off1: header-out-of-order (would be reason=14 under strict) → accepted
//     W_Off2: undefined tag       (would be reason=2 under strict)  → accepted
//     W_Off3: required field missing (would be reason=1 under strict) → accepted
//
//   Positive-dispatch proof: each violation frame is fed at seq=2; a well-formed
//   frame is then fed at seq=3.  Accepting the seq=3 frame (no Reject, session
//   stays Active) proves the violation frame was PROCESSED (not silently dropped)
//   and that seqnum advanced — i.e. the session dispatched seq=2 normally.
//
// T016 (SC-005, FR-002, FR-009):
//   Structural proof that the default path constructs NO validator.  After open()
//   with validate_inbound_messages==false, FIXPP_TEST_HOOKS exposes
//   has_validator_for_test() (session.hpp) which checks validator_==nullptr.
//   The flag is the ONLY variable vs the T012 strict-mode fixture (dictionary is
//   still set), so a null validator is attributable to the flag, not a missing dict.
//
//   This is the DIRECT postcondition named in SC-005 ("no validator construction
//   or invocation"), not a behavioral proxy — per [feedback_witness_asserts_named_
//   postcondition_not_proxy].
//
// TDD note: T014 is already merged; these tests characterize already-correct
// default-off behavior.  They will be GREEN on first run.  The discrimination
// evidence (that the SAME inputs under flag=true produce Reject, per T012 W1-W3)
// is provided by the paired strict-mode tests in test_validate_gate_inbound.cpp.
//
// Anchors: spec.md US2 / FR-002 / FR-009 / SC-001 / SC-005;
//          contracts/validation-gate.md C-2 default branch;
//          tasks.md T015 / T016.

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

// ── #289: bounded pumps ──────────────────────────────────────────────────────
//
// The `run_for(W); restart(); get()` sites below call `run_window_then_ready` with a
// miss-branch drain (tests/support/pump_until_ready.hpp). The window is PRESERVED:
// the hazard #289 names is the UNCONDITIONAL `get()`, not the fixed window.
//
// The rationale and the teardown-shape rule -- why the drain runs on the miss branch
// and not in a fixture destructor -- are documented AT the primitive. Read it there.
// That text is deliberately NOT copied into this file: each earlier per-file copy
// acquired clauses that are false at its own sites (#324's FILE-SPECIFIC ADDENDA), a
// cost paid once per copy and avoided entirely by pointing.

using namespace std::chrono_literals;

namespace fixpp::session::test {
namespace {

// ── Frame builder helpers (identical to test_validate_gate_inbound.cpp) ────────

static std::vector<std::byte> make_raw_frame(std::string_view begin_string,
                                             std::string_view msg_type, std::uint32_t seq,
                                             std::string_view sender, std::string_view target,
                                             std::string extra_body = {}) {
    std::string body;
    body += "35=" + std::string(msg_type) + "\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=" + std::string(sender) + "\x01";
    body += "52=20240101-00:00:00.000\x01";
    body += "56=" + std::string(target) + "\x01";
    if (!extra_body.empty()) {
        body += extra_body;
    }

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

static std::vector<std::byte> make_logon_frame(std::string_view begin_string = "FIX.4.2",
                                               std::uint32_t seq = 1,
                                               std::string_view sender = "TW",
                                               std::string_view target = "ISLD", int heartbt = 30) {
    std::string extra;
    extra += "98=0\x01";
    extra += "108=" + std::to_string(heartbt) + "\x01";
    return make_raw_frame(begin_string, "A", seq, sender, target, extra);
}

static std::vector<std::byte> make_heartbeat_frame(std::uint32_t seq = 2) {
    return make_raw_frame("FIX.4.2", "0", seq, "TW", "ISLD");
}

// ── Fixture ────────────────────────────────────────────────────────────────────

struct ValidateDefaultOffFixture {
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine;
    TransportDouble transport;

    ValidateDefaultOffFixture() {
        using namespace std::chrono;
        auto utc = system_clock::time_point{} + seconds{1704067200};
        auto stp = fixpp::core::steady_time_point{} + seconds{0};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock = clock;
        engine.executor = ioc.get_executor();
    }

    // Default config: validate_inbound_messages NOT set (defaults to false).
    // Dictionary IS set so that, if T016 sees validator_==nullptr, it is the FLAG
    // that is responsible — not a missing dictionary.
    SessionConfig make_cfg_default() {
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
        // validate_inbound_messages deliberately NOT set — remains false (default).
        return cfg;
    }

    // Open + drive to Active (same path as the strict-mode fixture).
    void open_to_active(Session& sess) {
        transport.reset();
        auto fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, 200ms)) {
            fixpp::test_support::cancel_and_drain_or_report(
                ioc, *clock, "ValidateDefaultOffFixture::open_to_active/open");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss
                          << "ValidateDefaultOffFixture::open_to_active/open";
            return;
        }
        ASSERT_TRUE(fut.get().has_value()) << "open() failed";

        // In LogonSent. Feed a valid peer Logon to reach Active.
        auto logon = make_logon_frame("FIX.4.2", 1, "TW", "ISLD", 30);
        transport.reset();
        auto fut2 = asio::co_spawn(ioc, sess.on_inbound_frame(logon), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut2, 200ms)) {
            fixpp::test_support::cancel_and_drain_or_report(
                ioc, *clock, "ValidateDefaultOffFixture::open_to_active/logon");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss
                          << "ValidateDefaultOffFixture::open_to_active/logon";
            return;
        }
        ASSERT_TRUE(fut2.get().has_value());
        ASSERT_EQ(sess.state(), fsm_state::Active);
    }

    void feed(Session& sess, std::span<const std::byte> frame) {
        transport.reset();
        auto fut = asio::co_spawn(ioc, sess.on_inbound_frame(frame), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, 200ms)) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock,
                                                            "ValidateDefaultOffFixture::feed");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss << "ValidateDefaultOffFixture::feed";
            return;
        }
        (void)fut.get();
    }

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

    bool has_any_reject() const {
        for (auto const& f : transport.sent_frames()) {
            if (extract_field(f, 35) == "3") return true;
        }
        return false;
    }
};

// ── T016 — SC-005 structural proof ───────────────────────────────────────────
//
// After open() with validate_inbound_messages==false (dictionary IS set),
// has_validator_for_test() returns false — the validator was NOT constructed.
// This is the named postcondition of SC-005 ("no validator construction or
// invocation") asserted directly via the FIXPP_TEST_HOOKS accessor, not a proxy.
//
// Discrimination: open() with validate_inbound_messages==true (see T012 fixture)
// DOES set validator_!=null.  The flag is the controlling variable; keeping the
// dictionary set in BOTH configurations isolates the flag as the sole cause.
TEST(ValidateGateDefaultOff, T016_ValidatorNotConstructed_SC005) {
    ValidateDefaultOffFixture fix;
    auto cfg = fix.make_cfg_default();
    Session sess{fix.engine, cfg};

    // open() triggers the validator-construction guard (session.cpp:1103).
    // With flag=false the guard is not entered → validator_ stays null.
    auto fut = asio::co_spawn(fix.ioc, sess.open(), asio::use_future);
    if (!fixpp::test_support::run_window_then_ready(fix.ioc, fut, 200ms)) {
        fixpp::test_support::cancel_and_drain_or_report(fix.ioc, *fix.clock,
                                                        "T016_ValidatorNotConstructed/open");
        ADD_FAILURE() << fixpp::test_support::kWindowMiss << "T016_ValidatorNotConstructed/open";
        return;
    }
    ASSERT_TRUE(fut.get().has_value()) << "open() must succeed";

    // Direct structural assertion: no validator constructed.
    // [SC-005; FR-002; 041 T016; session.cpp:1103; session.hpp FIXPP_TEST_HOOKS]
    EXPECT_FALSE(sess.has_validator_for_test())
        << "T016/SC-005: validator_ must be null when validate_inbound_messages==false "
           "(flag gates construction at session.cpp:1103; dict IS set in this config, "
           "so null is caused by the flag, not a missing dict)";
}

// ── T015 W_Off1 — header-out-of-order → accepted (not rejected) ──────────────
//
// Same violation stimulus as T012 W1 (header out of order: 49= before 35=) but
// with validate_inbound_messages==false.
// The message is accepted and dispatched; no Reject is emitted.
// Positive dispatch proof: after feeding the violation at seq=2, feed a well-formed
// Heartbeat at seq=3; it is accepted too (session stays Active, no new Reject) —
// which proves seq=2 was processed (seqnum advanced to expect seq=3).
TEST(ValidateGateDefaultOff, T015_HeaderOutOfOrder_Accepted) {
    ValidateDefaultOffFixture fix;
    auto cfg = fix.make_cfg_default();
    Session sess{fix.engine, cfg};
    fix.open_to_active(sess);

    // Violation stimulus: 49= before 35= (header out of order).
    std::string body;
    body += "49=TW\x01";  // sender before MsgType — out of order
    body += "35=0\x01";   // MsgType after sender
    body += "34=2\x01";
    body += "52=20240101-00:00:00.000\x01";
    body += "56=ISLD\x01";

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
    std::vector<std::byte> violation_frame;
    for (char c : full) violation_frame.push_back(static_cast<std::byte>(c));

    // Feed the violation at seq=2.
    fix.feed(sess, violation_frame);

    // No Reject emitted — validation gate did not fire.
    EXPECT_FALSE(fix.has_any_reject())
        << "W_Off1: header-out-of-order must NOT produce a Reject when validation is off";

    // Positive dispatch proof: feed a well-formed Heartbeat at seq=3.
    // Acceptance proves seq=2 was processed (seqnum advanced; the session expects seq=3).
    auto next_frame = make_heartbeat_frame(3);
    fix.feed(sess, next_frame);

    EXPECT_FALSE(fix.has_any_reject())
        << "W_Off1 dispatch proof: seq=3 Heartbeat must be accepted (session advanced past seq=2)";
    EXPECT_EQ(sess.state(), fsm_state::Active)
        << "W_Off1 dispatch proof: session must remain Active";
}

// ── T015 W_Off2 — undefined tag → accepted (not rejected) ────────────────────
//
// Same violation stimulus as T012 W2 (tag 44 = Price not defined for Heartbeat)
// but with validate_inbound_messages==false.
// Positive dispatch proof: same seq-advance pattern as W_Off1.
TEST(ValidateGateDefaultOff, T015_UndefinedTag_Accepted) {
    ValidateDefaultOffFixture fix;
    auto cfg = fix.make_cfg_default();
    Session sess{fix.engine, cfg};
    fix.open_to_active(sess);

    // Violation: Heartbeat(35=0) with extra tag 44 (Price, not in Heartbeat).
    auto violation_frame = make_raw_frame("FIX.4.2", "0", 2, "TW", "ISLD", "44=99.99\x01");

    fix.feed(sess, violation_frame);

    EXPECT_FALSE(fix.has_any_reject())
        << "W_Off2: undefined-tag must NOT produce a Reject when validation is off";

    // Positive dispatch proof.
    auto next_frame = make_heartbeat_frame(3);
    fix.feed(sess, next_frame);

    EXPECT_FALSE(fix.has_any_reject())
        << "W_Off2 dispatch proof: seq=3 Heartbeat must be accepted (session advanced past seq=2)";
    EXPECT_EQ(sess.state(), fsm_state::Active)
        << "W_Off2 dispatch proof: session must remain Active";
}

// Helper: check if a specific Reject reason (373=N) was emitted.
static std::string extract_field_from_transport(std::span<const std::byte> frame,
                                                std::uint32_t tag_wanted) {
    std::string wire(reinterpret_cast<const char*>(frame.data()), frame.size());
    std::string needle = std::to_string(tag_wanted) + "=";
    auto pos = wire.find(needle);
    if (pos == std::string::npos) return {};
    pos += needle.size();
    auto end = wire.find('\x01', pos);
    if (end == std::string::npos) return {};
    return wire.substr(pos, end - pos);
}

// ── T015 W_Off3 — required field missing → no VALIDATION-induced Reject ──────
//
// Same violation stimulus as T012 W3 (NOS without required ClOrdID(11))
// but with validate_inbound_messages==false.
//
// Pre-existing session behavior (prior release): a NewOrderSingle(35=D) in Active
// state with no Application registered triggers the session's existing
// "unsupported message type for state" path → Reject with SessionRejectReason=3.
// This is NOT a validation-induced Reject (reason=1); it is the unchanged
// prior-release behavior for app messages when no Application is registered.
//
// The test asserts: no Reject with reason=1 (which is what the validation gate
// would emit for required-field-missing).  A Reject with reason=3 is allowed
// because that is the prior-release behavior — the test proves the VALIDATION
// gate did NOT fire (no additional reason=1 Reject).
//
// This is the T015 assertion for "missing required field" — validation off means
// the message follows exactly the prior-release path (Reject(373=3)), not the
// validation path (Reject(373=1)).  "Accepted and dispatched" = "prior-release
// behavior preserved" = "no validation-induced Reject with reason=1".
TEST(ValidateGateDefaultOff, T015_RequiredFieldMissing_NoValidationReject) {
    ValidateDefaultOffFixture fix;
    auto cfg = fix.make_cfg_default();
    Session sess{fix.engine, cfg};
    fix.open_to_active(sess);

    // Violation: NewOrderSingle without required ClOrdID(11).
    std::string nos_body;
    // Deliberately omit 11=ClOrdID
    nos_body += "54=1\x01";
    nos_body += "60=20240101-00:00:00.000\x01";
    auto violation_frame = make_raw_frame("FIX.4.2", "D", 2, "TW", "ISLD", nos_body);

    fix.feed(sess, violation_frame);

    // The validation gate must NOT have fired (no reason=1 Reject).
    // A reason=3 Reject from the pre-existing "no-Application" path is acceptable
    // (it is the prior-release behavior — unchanged by this feature).
    bool has_validation_reject = false;
    for (auto const& f : fix.transport.sent_frames()) {
        if (extract_field_from_transport(f, 35) == "3") {
            auto r = extract_field_from_transport(f, 373);
            if (!r.empty() && std::stoi(r) == 1) {
                has_validation_reject = true;
            }
        }
    }
    EXPECT_FALSE(has_validation_reject)
        << "W_Off3: validation must NOT emit Reject(373=1) when flag is off; "
           "a pre-existing Reject(373=3) from the no-Application path is acceptable";

    // The session should not have disconnected (the pre-existing Reject(373=3)
    // is non-terminal — session stays Active per prior-release behavior).
    EXPECT_EQ(sess.state(), fsm_state::Active)
        << "W_Off3: session must remain Active after a non-terminal Reject";
}

}  // namespace
}  // namespace fixpp::session::test
