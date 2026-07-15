// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_enum_validation_logon.cpp
//
// 075-live-wire-enum-validation T030 [SC-008/FR-013] — Logon witness: an
// inbound Logon carrying an out-of-domain admin enum (EncryptMethod(98)=9,
// declared codes 0-6 in dictionaries/FIX44.xml — measured at T001) is
// rejected with SessionRejectReason 5 (RefTagID=98) and the session does
// NOT establish. The CONVERSE half — a Logon whose admin enums are all
// in-domain establishes exactly as today — is what makes this a real
// witness: a reject-everything regression would leave the first half green.
//
// fixpp validates BEFORE interpret_logon on the NotConnected arm
// (src/session/session.cpp:2060-2070), which is QuickFIX parity
// (Session::next validates at Session.cpp:1218-1229, before nextLogon at
// :1231). Reject(3) and Logout(5) stay exempt via the pre-existing
// no-reject-loop guard, so enum checking cannot induce a reject loop.
//
// Mutation: revert table_view.hpp's enum_valid() to `return true` (the
// Phase-1 stub) — both halves must not both stay green: the out-of-domain
// half must flip GREEN (Logon establishes despite 98=9) while the in-domain
// half stays green trivially, discriminating the stub from the real check.
//
// Anchors: specs/075-live-wire-enum-validation/tasks.md T030; spec.md
// SC-008/FR-013; tests/session/logon_handshake_test.cpp (acceptor idiom);
// tests/session/test_validate_gate_inbound.cpp (extract_field/
// reject_ref_tag_id idiom, 075 T020a).
#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
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

#include "support/fix44_dictionary.hpp"
#include "support/minimal_security_profile.hpp"
#include "support/transport_double.hpp"

using namespace std::chrono_literals;

namespace fixpp::session::test {
namespace {

// ── Frame builder ────────────────────────────────────────────────────────────

// Build a Logon(35=A) frame on FIX.4.4 with a caller-chosen EncryptMethod(98).
static std::vector<std::byte> make_fix44_logon_frame(std::uint32_t msg_seq_num,
                                                      std::string_view sender_comp_id,
                                                      std::string_view target_comp_id,
                                                      int heartbt_int, int encrypt_method) {
    std::string body;
    body += "35=A\x01";
    body += "34=" + std::to_string(msg_seq_num) + "\x01";
    body += "49=" + std::string(sender_comp_id) + "\x01";
    body += "52=20240101-00:00:00.000\x01";
    body += "56=" + std::string(target_comp_id) + "\x01";
    body += "98=" + std::to_string(encrypt_method) + "\x01";
    body += "108=" + std::to_string(heartbt_int) + "\x01";

    std::string hdr;
    hdr += "8=FIX.4.4\x01";
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
    frame.reserve(full.size());
    for (char c : full) {
        frame.push_back(static_cast<std::byte>(c));
    }
    return frame;
}

// ── Wire-scraping helpers (mirrors test_validate_gate_inbound.cpp) ──────────

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

// ── Fixture ───────────────────────────────────────────────────────────────────

class EnumValidationLogonTest : public ::testing::Test {
protected:
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine{};
    TransportDouble transport;

    void SetUp() override {
        using namespace std::chrono;
        auto utc = system_clock::time_point{} + seconds{1704067200};
        auto stp = fixpp::core::steady_time_point{} + seconds{0};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock = clock;
        engine.executor = ioc.get_executor();
    }

    // Acceptor config on the REAL FIX44.xml dictionary (EncryptMethod(98) declares
    // codes 0-6), with strict inbound validation on.
    fixpp::session::SessionConfig make_cfg() {
        fixpp::session::SessionConfig cfg;
        cfg.sender_comp_id = "ISLD";
        cfg.target_comp_id = "TW";
        cfg.begin_string = "FIX.4.4";
        cfg.heartbeat_interval = 30s;
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = fixpp::test_support::make_fix44_dictionary();
        cfg.executor_override = ioc.get_executor();
        cfg.role = fixpp::session::session_role::acceptor;
        cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
        cfg.validate_inbound_messages = true;
        cfg.transport_send = [this](std::span<const std::byte> frame) {
            transport.capture_outbound(frame);
        };
        return cfg;
    }

    fixpp::core::expected_t<void> open_sync(fixpp::session::Session& s) {
        auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
        ioc.run_for(200ms);
        ioc.restart();
        return fut.get();
    }

    fixpp::core::expected_t<void> feed_sync(fixpp::session::Session& s,
                                            std::span<const std::byte> frame) {
        transport.reset();
        auto fut = asio::co_spawn(ioc, s.on_inbound_frame(frame), asio::use_future);
        ioc.run_for(200ms);
        ioc.restart();
        return fut.get();
    }

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

    // Returns the RefTagID(371) of the first Reject(35=3) frame with the given
    // SessionRejectReason(373), or -1 when absent (no matching reject, or 371
    // omitted). Mirrors test_validate_gate_inbound.cpp's reject_ref_tag_id().
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

// ── Half 1: out-of-domain EncryptMethod(98)=9 → reject/5, session does NOT
//    establish ─────────────────────────────────────────────────────────────
TEST_F(EnumValidationLogonTest, OutOfDomainEncryptMethod_RejectedAndNotEstablished) {
    auto cfg = make_cfg();
    fixpp::session::Session sess(engine, cfg);
    ASSERT_TRUE(open_sync(sess).has_value()) << "Session::open() failed";
    ASSERT_EQ(sess.state(), fsm_state::NotConnected)
        << "acceptor must stay NotConnected after open() (no outbound Logon)";

    // Peer sends Logon with EncryptMethod(98)=9 — undeclared (FIX44 declares
    // only 0-6).
    auto frame = make_fix44_logon_frame(1, "TW", "ISLD", 30, /*encrypt_method=*/9);
    (void)feed_sync(sess, std::span<const std::byte>{frame});

    EXPECT_TRUE(has_reject_with_reason(5))
        << "out-of-domain EncryptMethod(98)=9 on inbound Logon must produce Reject(373=5)";
    EXPECT_EQ(reject_ref_tag_id(5), 98)
        << "Reject(373=5) must carry RefTagID(371)=98 (the offending EncryptMethod tag)";

    const auto s = sess.state();
    EXPECT_NE(s, fsm_state::Active) << "session must NOT establish on an out-of-domain admin enum";
    EXPECT_NE(s, fsm_state::LogonReceived)
        << "session must NOT establish on an out-of-domain admin enum";
}

// ── Half 2 (the converse — what makes this a real witness): all admin enums
//    in-domain → establishes exactly as today, no false-reject ────────────
TEST_F(EnumValidationLogonTest, InDomainEncryptMethod_EstablishesAsToday) {
    auto cfg = make_cfg();
    fixpp::session::Session sess(engine, cfg);
    ASSERT_TRUE(open_sync(sess).has_value()) << "Session::open() failed";

    // Peer sends Logon with EncryptMethod(98)=0 (NONE) — declared, in-domain.
    auto frame = make_fix44_logon_frame(1, "TW", "ISLD", 30, /*encrypt_method=*/0);
    auto result = feed_sync(sess, std::span<const std::byte>{frame});

    EXPECT_TRUE(result.has_value()) << "on_inbound_frame() returned error for a valid Logon";
    EXPECT_FALSE(has_reject_with_reason(5))
        << "an in-domain admin enum must NOT be rejected (no false-reject of the handshake)";
    EXPECT_EQ(sess.state(), fsm_state::Active)
        << "session with a fully in-domain Logon must establish exactly as today";
}

}  // namespace
}  // namespace fixpp::session::test
