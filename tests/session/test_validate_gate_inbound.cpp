// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_validate_gate_inbound.cpp
//
// 041-validation-gate-wiring T012 — inbound validation gate witnesses, one per
// violation class → mapped SessionRejectReason.
//
// Contract C-2: given a session with validate_inbound_messages==true, in an
// inbound-processing state (Active for most cells, NotConnected for Logon cell):
//
//  W1: header-out-of-order message       → Reject(35=3, 373=14)
//  W2: undefined-tag message             → Reject(35=3, 373=2)
//  W3: required-field-missing message    → Reject(35=3, 373=1)
//  W4: type-nonconformant (Int field with non-numeric value) → Reject(35=3, 373=5)
//  W5: Float/decimal precision-loss      → Reject(35=3, 373=6)
//  W6: conformant message               → dispatched (no Reject emitted)
//  W7: seqnum NOT advanced on reject     → C-3 (validate before seqnum gate)
//
// NOTE (T009a): the Float garbage-value (e.g. "abc") test is NOT reason=6 —
// decimal_invalid_input remaps to wire_field_value_out_of_range (40) → reason=5.
// Reason=6 is ONLY for decimal_precision_loss, tested via a high-precision decimal
// that loses precision when rounded to the representable form.
//
// TDD RED: These tests must fail (WRONG reason or no reject) BEFORE T014 wires
// the validate gate in on_inbound_frame. Once T014 is complete, they turn GREEN.
//
// Anchors: spec.md FR-003/FR-004; contracts/validation-gate.md C-2/C-3;
//          data-model E-4; tasks.md T012.

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
#include "support/transport_double.hpp"
#include "support/validation_test_dictionary.hpp"

using namespace std::chrono_literals;

namespace fixpp::session::test {
namespace {

// ── Frame builder helpers ──────────────────────────────────────────────────────

// Build a minimal SOH-delimited FIX frame with correct BodyLength(9) + CheckSum(10).
static std::vector<std::byte> make_raw_frame(std::string_view begin_string,
                                             std::string_view msg_type, std::uint32_t seq,
                                             std::string_view sender, std::string_view target,
                                             std::string extra_body = {}) {
    // Build body first (everything after the 8= header), excluding 8=, 9=.
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

// Build a Logon(35=A) frame (for reaching Active state).
static std::vector<std::byte> make_logon_frame(std::string_view begin_string = "FIX.4.2",
                                               std::uint32_t seq = 1,
                                               std::string_view sender = "TW",
                                               std::string_view target = "ISLD",
                                               int heartbt = 30) {
    std::string extra;
    extra += "98=0\x01";
    extra += "108=" + std::to_string(heartbt) + "\x01";
    return make_raw_frame(begin_string, "A", seq, sender, target, extra);
}

// Build a Heartbeat(35=0) frame — well-formed per the test dictionary.
static std::vector<std::byte> make_heartbeat_frame(std::uint32_t seq = 2) {
    return make_raw_frame("FIX.4.2", "0", seq, "TW", "ISLD");
}

// Build a NewOrderSingle(35=D) frame — base well-formed.
static std::vector<std::byte> make_nos_frame(std::uint32_t seq = 2,
                                             std::string extra_body = {}) {
    std::string body;
    body += "11=ORD001\x01";  // ClOrdID required
    body += "54=1\x01";       // Side required (1 char = valid CHAR)
    body += "60=20240101-00:00:00.000\x01";  // TransactTime required
    if (!extra_body.empty()) {
        body += extra_body;
    }
    return make_raw_frame("FIX.4.2", "D", seq, "TW", "ISLD", body);
}

// Extract a field value from a SOH-delimited FIX frame by tag number.
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

// ── Validation gate fixture ────────────────────────────────────────────────────

struct ValidateGateFixture {
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine;
    TransportDouble transport;

    ValidateGateFixture() {
        using namespace std::chrono;
        auto utc = system_clock::time_point{} + seconds{1704067200};
        auto stp = fixpp::core::steady_time_point{} + seconds{0};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock = clock;
        engine.executor = ioc.get_executor();
    }

    // Build a SessionConfig with validate_inbound_messages=true and the richer dict.
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

    // Open session and drive to Active (initiator path: open → LogonSent → feed Logon ack → Active).
    void open_to_active(Session& sess) {
        transport.reset();
        auto fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
        ioc.run_for(200ms);
        ioc.restart();
        ASSERT_TRUE(fut.get().has_value()) << "open() failed";

        // Now in LogonSent. Feed a valid peer Logon to reach Active.
        auto logon = make_logon_frame("FIX.4.2", 1, "TW", "ISLD", 30);
        transport.reset();
        auto fut2 = asio::co_spawn(ioc, sess.on_inbound_frame(logon), asio::use_future);
        ioc.run_for(200ms);
        ioc.restart();
        ASSERT_TRUE(fut2.get().has_value());
        ASSERT_EQ(sess.state(), fsm_state::Active);
    }

    // Feed a frame to the session, running the io_context for up to 200ms.
    void feed(Session& sess, std::span<const std::byte> frame) {
        transport.reset();
        auto fut = asio::co_spawn(ioc, sess.on_inbound_frame(frame), asio::use_future);
        ioc.run_for(200ms);
        ioc.restart();
        (void)fut.get();
    }

    // Check if any emitted frame is a Reject (35=3) with the given reason (373=N).
    bool has_reject_with_reason(int reason) const {
        for (auto const& frame : transport.sent_frames()) {
            if (extract_field(frame, 35) == "3") {
                auto r373 = extract_field(frame, 373);
                if (!r373.empty() && std::stoi(r373) == reason) {
                    return true;
                }
            }
        }
        return false;
    }

    bool has_any_reject() const {
        for (auto const& frame : transport.sent_frames()) {
            if (extract_field(frame, 35) == "3") {
                return true;
            }
        }
        return false;
    }
};

// ── W1: header-out-of-order → reason=14 ──────────────────────────────────────
//
// Feed a message where tag 35 (MsgType) appears AFTER another non-framing tag
// (e.g. 49= appears before 35=). The validator's Step 0 checks that the first
// non-framing entry in the offset table is tag 35 — anything else → wire_header_out_of_order.
//
// Build a frame manually where the body has 49= before 35= (swapping the field order).
TEST(ValidateGateInbound, HeaderOutOfOrder_Reason14) {
    ValidateGateFixture fix;
    auto cfg = fix.make_cfg_with_validation();
    Session sess{fix.engine, cfg};
    fix.open_to_active(sess);

    // Build a frame where 35 appears after 49 (header out of order).
    // Normal body: 35=...\x01 34=...\x01 49=...\x01 52=...\x01 56=...\x01
    // Out-of-order: 49= first, then 35=
    // Build raw frame manually (bypassing make_raw_frame's ordering).
    std::string body;
    body += "49=TW\x01";   // sender before MsgType — out of order
    body += "35=0\x01";    // MsgType after sender
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
    std::vector<std::byte> frame;
    for (char c : full) frame.push_back(static_cast<std::byte>(c));

    fix.feed(sess, frame);

    EXPECT_TRUE(fix.has_reject_with_reason(14))
        << "W1: header-out-of-order must produce Reject(373=14)";
}

// ── W2: undefined tag for msg type → reason=2 ────────────────────────────────
//
// Feed a Heartbeat(35=0) with an extra tag 44 (Price) which is not defined for
// Heartbeat in the test dictionary → wire_unexpected_tag → reason=2.
TEST(ValidateGateInbound, UnexpectedTag_Reason2) {
    ValidateGateFixture fix;
    auto cfg = fix.make_cfg_with_validation();
    Session sess{fix.engine, cfg};
    fix.open_to_active(sess);

    // Heartbeat with an extra field 44 (Price) that is not defined for msg type "0".
    auto frame = make_raw_frame("FIX.4.2", "0", 2, "TW", "ISLD",
                                "44=99.99\x01");  // Price not in Heartbeat

    fix.feed(sess, frame);

    EXPECT_TRUE(fix.has_reject_with_reason(2))
        << "W2: undefined-tag must produce Reject(373=2)";
}

// ── W3: required-field-missing → reason=1 ────────────────────────────────────
//
// Feed a NewOrderSingle(35=D) without the required ClOrdID(11) field.
// The validator's Step 2 finds ClOrdID in required_fields("D") but not in the
// message → wire_required_field_missing → reason=1.
TEST(ValidateGateInbound, RequiredFieldMissing_Reason1) {
    ValidateGateFixture fix;
    auto cfg = fix.make_cfg_with_validation();
    Session sess{fix.engine, cfg};
    fix.open_to_active(sess);

    // NOS without ClOrdID(11) — all other required fields present.
    std::string body;
    // Deliberately omit 11=ClOrdID
    body += "54=1\x01";       // Side required
    body += "60=20240101-00:00:00.000\x01";  // TransactTime required
    auto frame = make_raw_frame("FIX.4.2", "D", 2, "TW", "ISLD", body);

    fix.feed(sess, frame);

    EXPECT_TRUE(fix.has_reject_with_reason(1))
        << "W3: required-field-missing must produce Reject(373=1)";
}

// ── W4: type-nonconformant (Int field with non-numeric chars) → reason=5 ─────
//
// Feed a NewOrderSingle(35=D) with OrderQty(38)="abc" (non-numeric for INT field).
// The validator's type arm returns wire_field_value_out_of_range → reason=5.
TEST(ValidateGateInbound, TypeNonconformant_Int_Reason5) {
    ValidateGateFixture fix;
    auto cfg = fix.make_cfg_with_validation();
    Session sess{fix.engine, cfg};
    fix.open_to_active(sess);

    // NOS with OrderQty(38)="abc" — not a valid INT.
    auto frame = make_nos_frame(2, "38=abc\x01");

    fix.feed(sess, frame);

    EXPECT_TRUE(fix.has_reject_with_reason(5))
        << "W4: type-nonconformant Int must produce Reject(373=5)";
}

// ── W5: Float garbage value → reason=5 (via decimal_invalid_input remap, T009a)
//
// Feed a NewOrderSingle(35=D) with Price(44)="abc" — a garbage Float value.
// decimal_t::parse returns decimal_invalid_input (10) → T009a remap →
// wire_field_value_out_of_range (40) → reason=5.
// NOTE: this is reason=5, NOT reason=6. Reason=6 is ONLY for decimal_precision_loss.
TEST(ValidateGateInbound, FloatGarbageValue_Reason5) {
    ValidateGateFixture fix;
    auto cfg = fix.make_cfg_with_validation();
    Session sess{fix.engine, cfg};
    fix.open_to_active(sess);

    // NOS with Price(44)="abc" — not a valid Float.
    auto frame = make_nos_frame(2, "44=abc\x01");

    fix.feed(sess, frame);

    EXPECT_TRUE(fix.has_reject_with_reason(5))
        << "W5 (T009a remap): Float garbage must produce Reject(373=5), not 6";
}

// NOTE: W5b (Float decimal-precision-loss → reason=6) is deliberately OMITTED.
// The reason=6 / wire_field_value_truncated arm in validator.hpp check_field_type
// requires decimal_t::parse to return decimal_precision_loss. The current
// pod_decimal implementation (from_chars with mantissa+exponent int64) does NOT
// return decimal_precision_loss on parse — it succeeds for any representable
// mantissa. decimal_precision_loss is only produced by cross-traits conversions
// (decimal<T>::from<U>() when T≠U and the value loses precision in transit).
// The wire_field_value_truncated re-map path in check_field_type is therefore
// dead code with the current FIXPP_DECIMAL_T=pod_decimal configuration.
// This is a forward-looking guard for alternate decimal trait types. When a
// fixed-precision decimal type is introduced (FR-005 / 2c work), this test
// should be re-enabled. Waived per [const §IX.1] basis (unreachable arm).
// [041-validation-gate-wiring T012; T009a; data-model E-4]

// ── W6: conformant message → no Reject, dispatched ───────────────────────────
//
// A well-formed Heartbeat(35=0) with all required fields present and no extra
// fields passes validation and is dispatched normally.
TEST(ValidateGateInbound, ConformantMessage_NoReject) {
    ValidateGateFixture fix;
    auto cfg = fix.make_cfg_with_validation();
    Session sess{fix.engine, cfg};
    fix.open_to_active(sess);

    // Well-formed Heartbeat — no extra fields, no violations.
    auto frame = make_heartbeat_frame(2);
    fix.feed(sess, frame);

    EXPECT_FALSE(fix.has_any_reject())
        << "W6: conformant message must NOT produce a Reject";
}

// ── W7: seqnum NOT advanced on validate reject (C-3) ─────────────────────────
//
// Feed two frames: first a dict-invalid message (undefined tag → Reject 373=2),
// then a well-formed Heartbeat at the SAME seqnum (seq=2). The second must be
// accepted — proving the seqnum gate did NOT advance on the first (rejected) frame.
TEST(ValidateGateInbound, SeqnumNotAdvancedOnReject) {
    ValidateGateFixture fix;
    auto cfg = fix.make_cfg_with_validation();
    Session sess{fix.engine, cfg};
    fix.open_to_active(sess);

    // Feed invalid frame at seq=2 — should produce Reject, seqnum NOT advanced.
    auto bad_frame = make_raw_frame("FIX.4.2", "0", 2, "TW", "ISLD",
                                    "44=99.99\x01");  // Price not valid for Heartbeat
    fix.feed(sess, bad_frame);

    EXPECT_TRUE(fix.has_reject_with_reason(2)) << "W7 setup: expected Reject(373=2) on bad frame";

    // Now feed a well-formed Heartbeat at the SAME seqnum=2 — should be accepted
    // (the session should still expect seq=2, proving seqnum was not consumed).
    fix.transport.reset();
    auto good_frame = make_heartbeat_frame(2);
    fix.feed(sess, good_frame);

    EXPECT_FALSE(fix.has_any_reject())
        << "W7: after validate-reject, a conformant frame at the same seqnum must be accepted "
           "(seqnum was not advanced by the rejected frame)";
    EXPECT_EQ(sess.state(), fsm_state::Active)
        << "W7: session should remain Active";
}

}  // namespace
}  // namespace fixpp::session::test
