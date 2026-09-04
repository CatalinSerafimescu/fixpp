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
// 075 T020a (FR-006 RefTagID delivery): W2/W3/W4 additionally assert
// RefTagID(371) on the emitted Reject frame, proving the offending tag
// reaches the WIRE (not just validate()'s ref_tag_out return value — see
// tests/wire/validator_enum_domain_test.cpp for the validator-level pin).
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
#include "support/pump_until_ready.hpp"
#include "support/transport_double.hpp"
#include "support/validation_test_dictionary.hpp"

// ── #289: bounded pumps ──────────────────────────────────────────────────────
//
// Where a site in this file is migrated it uses `run_window_then_ready` plus a
// miss-branch drain (tests/support/pump_until_ready.hpp). The window is PRESERVED:
// the hazard #289 names is the UNCONDITIONAL `get()`, not the fixed window.
//
// Rationale and the teardown-shape rule live at the primitive, not duplicated here
// (#324).

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
                                               std::string_view target = "ISLD", int heartbt = 30) {
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
static std::vector<std::byte> make_nos_frame(std::uint32_t seq = 2, std::string extra_body = {}) {
    std::string body;
    body += "11=ORD001\x01";                 // ClOrdID required
    body += "54=1\x01";                      // Side required (1 char = valid CHAR)
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

    // Open session and drive to Active (initiator path: open → LogonSent → feed Logon ack →
    // Active).
    void open_to_active(Session& sess) {
        transport.reset();
        auto fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, 200ms)) {
            fixpp::test_support::cancel_and_drain_or_report(
                ioc, *clock, "ValidateGateFixture::open_to_active/open");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss
                          << "ValidateGateFixture::open_to_active/open";
            return;
        }
        ASSERT_TRUE(fut.get().has_value()) << "open() failed";

        // Now in LogonSent. Feed a valid peer Logon to reach Active.
        auto logon = make_logon_frame("FIX.4.2", 1, "TW", "ISLD", 30);
        transport.reset();
        auto fut2 = asio::co_spawn(ioc, sess.on_inbound_frame(logon), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut2, 200ms)) {
            fixpp::test_support::cancel_and_drain_or_report(
                ioc, *clock, "ValidateGateFixture::open_to_active/logon");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss
                          << "ValidateGateFixture::open_to_active/logon";
            return;
        }
        ASSERT_TRUE(fut2.get().has_value());
        ASSERT_EQ(sess.state(), fsm_state::Active);
    }

    // Feed a frame to the session, running the io_context for up to 200ms.
    void feed(Session& sess, std::span<const std::byte> frame) {
        transport.reset();
        auto fut = asio::co_spawn(ioc, sess.on_inbound_frame(frame), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, 200ms)) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock,
                                                            "ValidateGateFixture::feed");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss << "ValidateGateFixture::feed";
            return;
        }
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

    // 075 T020a (FR-006): returns the RefTagID(371) of the first Reject(35=3)
    // frame with the given SessionRejectReason(373), or -1 when no matching
    // reject frame carries a 371 (either no matching reject, or 371 omitted).
    int reject_ref_tag_id(int reason) const {
        for (auto const& frame : transport.sent_frames()) {
            if (extract_field(frame, 35) == "3") {
                auto r373 = extract_field(frame, 373);
                if (!r373.empty() && std::stoi(r373) == reason) {
                    auto r371 = extract_field(frame, 371);
                    if (r371.empty()) {
                        return -1;
                    }
                    return std::stoi(r371);
                }
            }
        }
        return -1;
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

    EXPECT_TRUE(fix.has_reject_with_reason(2)) << "W2: undefined-tag must produce Reject(373=2)";
    // 075 T020a (FR-006): the outbound Reject must carry RefTagID(371)=44 —
    // proves the tag reaches the WIRE, not just validate()'s return value.
    EXPECT_EQ(fix.reject_ref_tag_id(2), 44)
        << "W2: Reject(373=2) must carry RefTagID(371)=44 (the undefined Price tag)";
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
    body += "54=1\x01";                      // Side required
    body += "60=20240101-00:00:00.000\x01";  // TransactTime required
    auto frame = make_raw_frame("FIX.4.2", "D", 2, "TW", "ISLD", body);

    fix.feed(sess, frame);

    EXPECT_TRUE(fix.has_reject_with_reason(1))
        << "W3: required-field-missing must produce Reject(373=1)";
    // 075 T020a (FR-006): RefTagID(371)=11 — the missing ClOrdID tag.
    EXPECT_EQ(fix.reject_ref_tag_id(1), 11)
        << "W3: Reject(373=1) must carry RefTagID(371)=11 (the missing ClOrdID tag)";
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
    // 075 T020a (FR-006): RefTagID(371)=38 — the malformed OrderQty tag.
    EXPECT_EQ(fix.reject_ref_tag_id(5), 38)
        << "W4: Reject(373=5) must carry RefTagID(371)=38 (the malformed OrderQty tag)";
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

    EXPECT_FALSE(fix.has_any_reject()) << "W6: conformant message must NOT produce a Reject";
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
    EXPECT_EQ(sess.state(), fsm_state::Active) << "W7: session should remain Active";
}

// ── W8: arena-bypass — high-field-count frame with a violation NOT silently dispatched
//
// Regression witness for the FIX-1 bypass (simplify-triage round):
// The validate gate used kAdminParseArena (8192 bytes) while the dispatch path
// uses kInboundParseArena (16384 bytes). The OffsetTable parser allocates PMR-backed
// pmr::vector<entry> (12 bytes/entry) from the arena with geometric reallocation.
//
// Arena exhaustion mechanics (sizeof(entry)==12; 2× growth from initial cap=1):
//   Cumulative monotonic usage before the cap-256→512 step:
//     Σ cap_i × 12  for cap_i in {1,2,4,8,16,32,64,128,256} = 511×12 = 6132 bytes
//   8 KiB arena: 8192 − 6132 = 2060 bytes remaining; cap-512 needs 6144 → FAILS.
//     parse() returns unexpected; validation silently skipped → bypass.
//   16 KiB arena: 16384 − 6132 = 10252 bytes remaining; cap-512 needs 6144 → OK.
//     parse succeeds; validate fires → Reject(373=2) ✓.
//
// Field count choice (N = 400 undefined tags):
//   Total entries = 10 header/required + 400 undefined = 410 → cap-512 needed.
//   N = 400 gives comfortable margin (> 256 required to trigger the boundary)
//   while the PMR usage with 2× growth (6132 + 6144 = 12276 bytes peak) stays
//   well within 16384.  Any N in [257, 502] triggers the same exhaustion on 8 KiB
//   and fits in 16 KiB; 400 is chosen for robust margin above the 256-entry knee.
//
// Threshold-independent discrimination (part b):
//   After the validate-Reject (seqnum NOT advanced, W7-proven), feed a conformant
//   NOS at the SAME seqnum=2.  A conformant message must be dispatched without
//   Reject, proving: (1) the 16 KiB arena is sufficient for the validate path even
//   on conformant messages, (2) the Reject was triggered by the undefined tags, not
//   by some universal large-message policy.  This assertion passes regardless of
//   the exact PMR exhaustion threshold.
//
// RED on kAdminParseArena gate: arena exhausts at ~257 fields → parse() returns
//   unexpected → validation SKIPPED → no Reject.
// GREEN on kInboundParseArena gate: parse succeeds → validate fires → Reject(373=2)
//   for the first undefined tag; then conformant NOS at seq=2 dispatched, no Reject.
//
// RED-discrimination confirmation (required by brief):
//   To confirm RED: temporarily set the gate arena to kAdminParseArena (8192) in
//   validate_inbound_() and rebuild → W8 FAILS (no Reject).
//   Restore kInboundParseArena → GREEN.
TEST(ValidateGateInbound, ManyFieldsBypassArena_Rejected_NotBypassed) {
    ValidateGateFixture fix;
    auto cfg = fix.make_cfg_with_validation();
    Session sess{fix.engine, cfg};
    fix.open_to_active(sess);

    // ── (a) Feed violation frame: 280 undefined tags → must produce Reject(373=2) ──
    //
    // Total entries: 7 header fields (8,9,35,49,56,34,52) + 3 required body fields
    // (11,54,60) + 280 undefined (9000-9279) = 290.  Needs cap-512 (next power of 2
    // above 256 that fits 290).
    //
    // The count must land in the window (admin-arena capacity, inbound-arena
    // capacity): high enough to EXHAUST the 8 KiB kAdminParseArena (so the RED
    // mutation in the header comment fails), low enough to FIT the 16 KiB
    // kInboundParseArena.  That window is allocator-dependent: kInboundParseArena
    // is 2x kAdminParseArena, but MSVC's std::pmr grows vectors 1.5x (vs
    // libstdc++/libc++ 2x), so a monotonic_buffer_resource retains more, shifting
    // the inbound ceiling down.  MEASURED (windows-msvc-release sweep): the gate
    // rejects up to 315 total fields and bypasses at >=320 on MSVC, vs >480 on
    // libstdc++; the libstdc++ admin-arena floor is ~257.  290 (vs the original
    // libstdc++-only 410) sits in the overlap with margin on both bounds: above
    // the 257-field libstdc++ admin floor, below the 315-field MSVC inbound ceiling.
    std::string body;
    body += "11=ORD001\x01";
    body += "54=1\x01";
    body += "60=20240101-00:00:00.000\x01";
    for (int t = 9000; t < 9280; ++t) {
        body += std::to_string(t) + "=X\x01";  // 280 undefined tags
    }
    auto violation_frame = make_raw_frame("FIX.4.2", "D", 2, "TW", "ISLD", body);

    fix.feed(sess, violation_frame);

    // On kAdminParseArena gate: arena exhausts at ~257 fields → parse() returns
    //   unexpected → validation SKIPPED → no Reject  [RED].
    // On kInboundParseArena gate: parse succeeds → validate fires →
    //   first undefined tag (9000) detected → Reject(373=2)  [GREEN].
    EXPECT_TRUE(fix.has_reject_with_reason(2))
        << "W8a: high-field-count frame with undefined tags must be Rejected (373=2), "
           "not silently bypassed due to parse-arena exhaustion in the validate gate";

    // ── (b) Threshold-independent discrimination: conformant Heartbeat at same seqnum ──
    //
    // The validate-Reject does NOT advance the inbound seqnum (C-3 / W7).  Feed a
    // conformant Heartbeat(35=0) at the SAME seq=2 to prove: (i) the session remains
    // Active, (ii) the conformant message is dispatched without a Reject, and (iii)
    // the shared kInboundParseArena is sufficient for conformant messages.  A Heartbeat
    // is used (not NOS) because it is an admin message that routes through fromAdmin —
    // no Application is registered in this fixture, so a NOS would produce a Reject
    // (reason=3) for a different reason (no fromApp handler), masking the arena result.
    // This assertion is threshold-independent: it passes whether the fix was via 8 KiB
    // or 16 KiB arena, because it only requires that a small conformant frame routes
    // through the arena without exhaustion and gets dispatched.
    fix.transport.reset();
    auto conformant_frame = make_heartbeat_frame(2);  // seq=2, admin message, no Application needed
    fix.feed(sess, conformant_frame);

    EXPECT_FALSE(fix.has_any_reject())
        << "W8b: conformant Heartbeat at the same seqnum must be dispatched without Reject "
           "(proves the validate gate does not spuriously reject conformant messages, "
           "threshold-independent)";
    EXPECT_EQ(sess.state(), fsm_state::Active)
        << "W8b: session must remain Active after a conformant dispatch";
}

}  // namespace
}  // namespace fixpp::session::test
