// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_inbound_sequence_reset.cpp — S-023 RED
//
// INBOUND SequenceReset(35=4) NewSeqNo(36) application. Until this slice,
// fixpp parsed NewSeqNo(36)/GapFillFlag(123) into the inbound header but NEVER
// applied them: a received 35=4 fell through to the generic "in-sequence:
// advance +1" tail, and `ReconnectFsm::process_inbound_sequence_reset` was a
// no-op stub with zero production callers. Catalogue S-006 ("gap fill or hard
// reset ... done") was overclaimed — only OUTBOUND GapFill emission + the
// ResetSeqNumFlag(141) Logon-time reset were real.
//
// Canonical arms (QuickFIX-cpp `Session::nextSequenceReset`, Session.cpp:339):
//   NewSeqNo > expected  → set next-expected-inbound (advance past gap / reset)
//   NewSeqNo < expected  → Reject(SessionRejectReason=5, ValueIsIncorrect)
//   NewSeqNo == expected → no-op
// GapFill mode (123=Y) is subject to seqnum ordering; Reset mode (123=N/absent)
// is processed REGARDLESS of its own MsgSeqNum (FIX-SL §4.8.6).
//
// Production-shape: drives bytes through Session::on_inbound_frame() and reads
// next-expected-inbound via seqnum_mgr_test_access() (FIXPP_TEST_HOOKS).

#include <gtest/gtest.h>

#include <array>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
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

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"

using namespace std::chrono_literals;

namespace {

std::string field(int tag, std::string_view val) {
    return std::to_string(tag) + "=" + std::string(val) + "\x01";
}

std::vector<std::byte> make_fix_frame(std::string_view begin_string, std::string_view msg_type,
                                      std::uint32_t seq, std::string_view sender,
                                      std::string_view target, std::string_view extra = {}) {
    std::string body;
    body += field(35, msg_type);
    body += field(34, std::to_string(seq));
    body += field(49, sender);
    body += field(52, "20240101-00:00:00.000");
    body += field(56, target);
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

    std::vector<std::byte> frame;
    frame.reserve(msg.size());
    for (char c : msg) frame.push_back(static_cast<std::byte>(c));
    return frame;
}

std::vector<std::byte> make_logon(std::string_view bs, std::uint32_t seq, std::string_view s,
                                  std::string_view t, int hbt = 30) {
    std::string extra;
    extra += field(98, "0");
    extra += field(108, std::to_string(hbt));
    return make_fix_frame(bs, "A", seq, s, t, extra);
}

// SequenceReset(35=4) with NewSeqNo(36); GapFillFlag(123)=Y only when gap_fill.
std::vector<std::byte> make_sequence_reset(std::string_view bs, std::uint32_t seq,
                                           std::string_view s, std::string_view t,
                                           std::uint32_t new_seqno, bool gap_fill) {
    std::string extra;
    if (gap_fill) extra += field(123, "Y");
    extra += field(36, std::to_string(new_seqno));
    return make_fix_frame(bs, "4", seq, s, t, extra);
}

struct OutboundCapture {
    std::vector<std::vector<std::byte>> frames;
    void operator()(std::span<const std::byte> data) { frames.emplace_back(data.begin(), data.end()); }
};

bool any_frame_contains(const OutboundCapture& cap, std::string_view needle) {
    for (const auto& f : cap.frames) {
        std::string wire(reinterpret_cast<const char*>(f.data()), f.size());
        if (wire.find(std::string(needle)) != std::string::npos) return true;
    }
    return false;
}

// A Reject(35=3) carrying SessionRejectReason(373)=5 (ValueIsIncorrect).
bool any_reject_value_incorrect(const OutboundCapture& cap) {
    for (const auto& f : cap.frames) {
        std::string wire(reinterpret_cast<const char*>(f.data()), f.size());
        if (wire.find("35=3\x01") != std::string::npos && wire.find("373=5\x01") != std::string::npos)
            return true;
    }
    return false;
}

class InboundSequenceResetTest : public ::testing::Test {
protected:
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine{};
    OutboundCapture capture;

    void SetUp() override {
        auto utc = std::chrono::system_clock::time_point{} + std::chrono::seconds{1704067200};
        auto stp = fixpp::core::steady_time_point{};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock = clock;
        engine.executor = ioc.get_executor();
    }

    fixpp::session::SessionConfig make_acceptor_cfg() {
        fixpp::session::SessionConfig cfg;
        cfg.sender_comp_id = "ISLD";
        cfg.target_comp_id = "TW";
        cfg.begin_string = "FIX.4.2";
        cfg.heartbeat_interval = 30s;
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override = ioc.get_executor();
        cfg.transport_send = [this](std::span<const std::byte> d) { capture(d); };
        cfg.role = fixpp::session::session_role::acceptor;
        cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
        return cfg;
    }

    fixpp::core::expected_t<void> run_open(fixpp::session::Session& s) {
        auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
        ioc.run_for(100ms);
        ioc.restart();
        return fut.get();
    }

    fixpp::core::expected_t<void> feed(fixpp::session::Session& s, std::span<const std::byte> frame) {
        auto fut = asio::co_spawn(ioc, s.on_inbound_frame(frame), asio::use_future);
        ioc.run_for(100ms);
        ioc.restart();
        return fut.get();
    }

    // Drive to Active; after inbound Logon(seq=1) next-expected-inbound = 2.
    bool drive_to_active(fixpp::session::Session& s) {
        if (!run_open(s).has_value()) return false;
        auto logon = make_logon("FIX.4.2", 1, "TW", "ISLD");
        (void)feed(s, logon);
        return s.state() == fixpp::session::fsm_state::Active;
    }

    std::uint32_t next_inbound(fixpp::session::Session& s) {
        return s.seqnum_mgr_test_access().next_inbound_unsafe();
    }
};

// ── Arm 1: GapFill (123=Y) advances next-expected past the filled gap ─────────
TEST_F(InboundSequenceResetTest, GapFillAdvancesExpectedInbound) {
    fixpp::session::Session s{engine, make_acceptor_cfg()};
    ASSERT_TRUE(drive_to_active(s));
    ASSERT_EQ(next_inbound(s), 2U);

    // Peer collapses a [2..9] admin gap into SequenceReset-GapFill{NewSeqNo=10}.
    auto sr = make_sequence_reset("FIX.4.2", 2, "TW", "ISLD", /*new_seqno=*/10, /*gap_fill=*/true);
    (void)feed(s, sr);

    EXPECT_EQ(next_inbound(s), 10U)
        << "GapFill NewSeqNo(36)=10 must advance next-expected-inbound to 10 "
           "(RED: inbound 35=4 is unhandled → counter only advances +1 to 3).";
    EXPECT_EQ(s.state(), fixpp::session::fsm_state::Active);
}

// ── Arm 2: Reset mode (123 absent) hard-resets AND bypasses seqnum ordering ───
TEST_F(InboundSequenceResetTest, ResetModeHardResetsAndBypassesOrdering) {
    fixpp::session::Session s{engine, make_acceptor_cfg()};
    ASSERT_TRUE(drive_to_active(s));
    ASSERT_EQ(next_inbound(s), 2U);

    // Reset whose OWN MsgSeqNum (50) is far above expected (2): Reset mode is
    // processed regardless of ordering — must NOT trigger ResendRequest/Disconnect.
    auto sr = make_sequence_reset("FIX.4.2", 50, "TW", "ISLD", /*new_seqno=*/200, /*gap_fill=*/false);
    (void)feed(s, sr);

    EXPECT_EQ(next_inbound(s), 200U)
        << "Reset NewSeqNo(36)=200 must hard-set next-expected-inbound to 200 "
           "(RED: inbound 35=4 unhandled).";
    EXPECT_EQ(s.state(), fixpp::session::fsm_state::Active)
        << "Reset mode bypasses the too-high gate — must stay Active.";
    EXPECT_FALSE(any_frame_contains(capture, "35=2\x01"))
        << "Reset mode must NOT emit a ResendRequest(35=2).";
}

// ── Arm 3: NewSeqNo < expected → Reject(SessionRejectReason=5) ────────────────
TEST_F(InboundSequenceResetTest, NewSeqNoBelowExpectedRejects) {
    fixpp::session::Session s{engine, make_acceptor_cfg()};
    ASSERT_TRUE(drive_to_active(s));
    ASSERT_EQ(next_inbound(s), 2U);  // expected = 2

    // NewSeqNo=1 < expected=2 → reject, counter unchanged.
    auto sr = make_sequence_reset("FIX.4.2", 2, "TW", "ISLD", /*new_seqno=*/1, /*gap_fill=*/false);
    (void)feed(s, sr);

    EXPECT_TRUE(any_reject_value_incorrect(capture))
        << "NewSeqNo(36) below expected must emit Reject(35=3, 373=5 ValueIsIncorrect).";
    EXPECT_EQ(next_inbound(s), 2U) << "A below-expected NewSeqNo must NOT move the counter backward.";
}

// ── Arm 4: NewSeqNo == expected → no-op (no Reject, counter unchanged) ─────────
TEST_F(InboundSequenceResetTest, NewSeqNoEqualExpectedIsNoOp) {
    fixpp::session::Session s{engine, make_acceptor_cfg()};
    ASSERT_TRUE(drive_to_active(s));
    ASSERT_EQ(next_inbound(s), 2U);

    auto sr = make_sequence_reset("FIX.4.2", 2, "TW", "ISLD", /*new_seqno=*/2, /*gap_fill=*/false);
    (void)feed(s, sr);

    EXPECT_EQ(next_inbound(s), 2U) << "NewSeqNo == expected is a no-op.";
    EXPECT_FALSE(any_reject_value_incorrect(capture)) << "NewSeqNo == expected must not Reject.";
}

}  // namespace
