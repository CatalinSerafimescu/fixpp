// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_070_posture_mismatch_test.cpp
//
// 070-fix44-closeout US1 (S-029) — TestMessageIndicator(464) posture-mismatch refusal.
//
// Discriminating witness (FR-002/FR-003/FR-012, tasks.md T006):
//   (a) posture=production + inbound Logon 464=Y      → Disconnected (refused, not Active).
//   (b) posture=test       + inbound Logon 464=N      → Disconnected;
//       posture=test       + inbound Logon (no 464)   → Disconnected.
//   (c) posture=production + 464=N / absent           → Active (no false reject).
//   (d) posture UNSET (default) + 464=Y               → Active (opt-in; baseline unchanged).
//   (e) posture=production + malformed 464=foo        → Disconnected (refused-as-malformed).
//   + advertise: build_logon with opts.test_message_indicator ⇒ 464=Y on the wire; else absent.
//
// The (a) vs (d) pair is the discriminator: the SAME inbound frame (464=Y) is refused
// when posture is enforced and accepted when it is not — proving the new path fires
// only under configuration.
#include <gtest/gtest.h>

#include <array>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstdio>
#include <fixpp/core/clock.hpp>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/admin_messages.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"

namespace fixpp::session::test {
namespace {

using fixpp::session::fsm_state;

// Build an inbound Logon frame, optionally carrying TestMessageIndicator(464).
std::vector<std::byte> make_logon_frame_464(std::string_view begin_string,
                                            std::uint32_t msg_seq_num,
                                            std::string_view sender_comp_id,
                                            std::string_view target_comp_id, int heartbt_int,
                                            std::optional<std::string_view> tmi_464) {
    std::string body;
    body += "35=A\x01";
    body += "34=" + std::to_string(msg_seq_num) + "\x01";
    body += "49=" + std::string(sender_comp_id) + "\x01";
    body += "52=20240101-00:00:00.000\x01";
    body += "56=" + std::string(target_comp_id) + "\x01";
    body += "98=0\x01";
    body += "108=" + std::to_string(heartbt_int) + "\x01";
    if (tmi_464.has_value()) {
        body += "464=" + std::string(*tmi_464) + "\x01";
    }

    std::string full = "8=" + std::string(begin_string) + "\x01";
    full += "9=" + std::to_string(body.size()) + "\x01" + body;
    unsigned int cs = 0;
    for (unsigned char c : full) {
        cs += c;
    }
    char csbuf[4];
    std::snprintf(csbuf, sizeof(csbuf), "%03u", cs & 0xFFU);
    full += "10=" + std::string(csbuf) + "\x01";

    std::vector<std::byte> frame;
    frame.reserve(full.size());
    for (char c : full) {
        frame.push_back(static_cast<std::byte>(c));
    }
    return frame;
}

std::string extract_field(std::span<const std::byte> frame, int tag) {
    std::string wire(reinterpret_cast<const char*>(frame.data()), frame.size());
    std::string needle = std::to_string(tag) + "=";
    auto pos = wire.find(needle);
    if (pos == std::string::npos) {
        return {};
    }
    pos += needle.size();
    auto end = wire.find('\x01', pos);
    return end == std::string::npos ? std::string{} : wire.substr(pos, end - pos);
}

class PostureTest : public ::testing::Test {
protected:
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine{};

    void SetUp() override {
        using namespace std::chrono;
        auto utc = system_clock::time_point{} + seconds{1704067200};
        auto stp = fixpp::core::steady_time_point{} + seconds{0};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock = clock;
        engine.executor = ioc.get_executor();
    }

    // Acceptor (sender=ISLD, target=TW). posture nullopt ⇒ enforcement disabled.
    fixpp::session::SessionConfig make_acceptor_cfg(
        std::optional<fixpp::session::session_posture> posture) {
        fixpp::session::SessionConfig cfg;
        cfg.sender_comp_id = "ISLD";
        cfg.target_comp_id = "TW";
        cfg.begin_string = "FIX.4.4";
        cfg.heartbeat_interval = std::chrono::seconds{30};
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override = ioc.get_executor();
        cfg.role = fixpp::session::session_role::acceptor;
        cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
        cfg.posture = posture;
        return cfg;
    }

    fixpp::core::expected_t<void> open_sync(fixpp::session::Session& s) {
        auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
        ioc.run_for(std::chrono::milliseconds{200});
        ioc.restart();
        return fut.get();
    }

    void feed_sync(fixpp::session::Session& s, std::span<const std::byte> frame) {
        auto fut = asio::co_spawn(ioc, s.on_inbound_frame(frame), asio::use_future);
        ioc.run_for(std::chrono::milliseconds{200});
        ioc.restart();
        (void)fut.get();  // refusal is a "successful" processing that ends Disconnected
    }

    // Drive an acceptor with the given posture + inbound 464 value; return the FSM state.
    fsm_state run(std::optional<fixpp::session::session_posture> posture,
                  std::optional<std::string_view> tmi_464) {
        auto cfg = make_acceptor_cfg(posture);
        fixpp::session::Session sess(engine, cfg);
        EXPECT_TRUE(open_sync(sess).has_value());
        auto frame = make_logon_frame_464("FIX.4.4", 1, "TW", "ISLD", 30, tmi_464);
        feed_sync(sess, std::span<const std::byte>{frame});
        return sess.state();
    }
};

// (a) production posture refuses a test-marked peer.
TEST_F(PostureTest, ProductionRefusesTestPeer) {
    EXPECT_EQ(run(fixpp::session::session_posture::production, std::string_view{"Y"}),
              fsm_state::Disconnected);
}

// (b) test posture refuses a production peer — explicit 464=N and absent-464.
TEST_F(PostureTest, TestRefusesProductionPeer_464N) {
    EXPECT_EQ(run(fixpp::session::session_posture::test, std::string_view{"N"}),
              fsm_state::Disconnected);
}
TEST_F(PostureTest, TestRefusesProductionPeer_Absent) {
    EXPECT_EQ(run(fixpp::session::session_posture::test, std::nullopt), fsm_state::Disconnected);
}

// (c) production posture accepts a production peer — no false reject.
TEST_F(PostureTest, ProductionAcceptsProductionPeer_464N) {
    EXPECT_EQ(run(fixpp::session::session_posture::production, std::string_view{"N"}),
              fsm_state::Active);
}
TEST_F(PostureTest, ProductionAcceptsProductionPeer_Absent) {
    EXPECT_EQ(run(fixpp::session::session_posture::production, std::nullopt), fsm_state::Active);
}

// (d) opt-in proof: posture UNSET accepts the SAME 464=Y frame that (a) refused.
TEST_F(PostureTest, UnsetPostureIgnores464) {
    EXPECT_EQ(run(std::nullopt, std::string_view{"Y"}), fsm_state::Active);
}

// (e) malformed 464 (not Y/N) is refused.
TEST_F(PostureTest, ProductionRefusesMalformed464) {
    EXPECT_EQ(run(fixpp::session::session_posture::production, std::string_view{"foo"}),
              fsm_state::Disconnected);
}

// Advertise side: build_logon emits 464=Y iff opts.test_message_indicator.
TEST(PostureAdvertise, BuildLogonEmits464YWhenTest) {
    std::array<std::byte, 512> buf{};
    auto with_flag = fixpp::session::build_logon(
        std::span<std::byte>{buf.data(), buf.size()}, 1, "TW", "ISLD", "FIX.4.4", 30,
        "20240101-00:00:00.000", false, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
        fixpp::session::logon_advertise_options{.test_message_indicator = true});
    ASSERT_TRUE(with_flag.has_value());
    EXPECT_EQ(extract_field(*with_flag, 464), "Y");

    std::array<std::byte, 512> buf2{};
    auto without_flag = fixpp::session::build_logon(
        std::span<std::byte>{buf2.data(), buf2.size()}, 1, "TW", "ISLD", "FIX.4.4", 30,
        "20240101-00:00:00.000", false, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
        fixpp::session::logon_advertise_options{});
    ASSERT_TRUE(without_flag.has_value());
    EXPECT_EQ(extract_field(*without_flag, 464), "") << "no 464 when flag false (byte-identical)";
}

}  // namespace
}  // namespace fixpp::session::test
