// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_070_max_message_size_test.cpp
//
// 070-fix44-closeout US2 (S-030) — negotiated MaxMessageSize(383).
//
// Discriminating witness (FR-004..FR-007, tasks.md T011):
//   (a) advertised config ⇒ outbound Logon carries 383=N (unit test of build_logon).
//   (b) established (Active) session: inbound frame of size N accepted (stays Active);
//       size N+1 ⇒ Disconnected — exact boundary via the measured Heartbeat size.
//   (c) a pre-establishment frame LARGER than our advertised max still establishes
//       (Active) — the negotiated rule never fires pre-Active (only the framer
//       backstop governs there).
//   (d) default (unset) ⇒ no 383 on the wire and no negotiated enforcement.
//   (e) peer's advertised 383 is captured + observable (FR-007).
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

std::vector<std::byte> to_frame(const std::string& full) {
    std::vector<std::byte> f;
    f.reserve(full.size());
    for (char c : full) {
        f.push_back(static_cast<std::byte>(c));
    }
    return f;
}

std::string finalize(std::string body, std::string_view begin_string) {
    std::string full = "8=" + std::string(begin_string) + "\x01";
    full += "9=" + std::to_string(body.size()) + "\x01" + body;
    unsigned int cs = 0;
    for (unsigned char c : full) {
        cs += c;
    }
    char csbuf[4];
    std::snprintf(csbuf, sizeof(csbuf), "%03u", cs & 0xFFU);
    full += "10=" + std::string(csbuf) + "\x01";
    return full;
}

// Inbound Logon, optionally carrying peer's MaxMessageSize(383).
std::vector<std::byte> make_logon_frame(std::string_view begin, std::uint32_t seq,
                                        std::string_view sender, std::string_view target,
                                        int heartbt, std::optional<std::uint32_t> peer_383) {
    std::string body = "35=A\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=" + std::string(sender) + "\x01";
    body += "52=20240101-00:00:00.000\x01";
    body += "56=" + std::string(target) + "\x01";
    body += "98=0\x01";
    body += "108=" + std::to_string(heartbt) + "\x01";
    if (peer_383.has_value()) {
        body += "383=" + std::to_string(*peer_383) + "\x01";
    }
    return to_frame(finalize(body, begin));
}

std::vector<std::byte> make_heartbeat_frame(std::string_view begin, std::uint32_t seq,
                                            std::string_view sender, std::string_view target) {
    std::string body = "35=0\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=" + std::string(sender) + "\x01";
    body += "52=20240101-00:00:00.000\x01";
    body += "56=" + std::string(target) + "\x01";
    return to_frame(finalize(body, begin));
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

class MaxMsgSizeTest : public ::testing::Test {
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

    fixpp::session::SessionConfig make_acceptor_cfg(std::optional<std::uint32_t> advertised_max) {
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
        cfg.advertised_max_message_size = advertised_max;
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
        (void)fut.get();
    }
};

// (a) advertise side: build_logon emits 383=N iff opts.max_message_size set.
TEST(MaxMsgSizeAdvertise, BuildLogonEmits383) {
    std::array<std::byte, 512> buf{};
    auto with = fixpp::session::build_logon(
        std::span<std::byte>{buf.data(), buf.size()}, 1, "TW", "ISLD", "FIX.4.4", 30,
        "20240101-00:00:00.000", false, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
        fixpp::session::logon_advertise_options{.max_message_size = 4096U});
    ASSERT_TRUE(with.has_value());
    EXPECT_EQ(extract_field(*with, 383), "4096");

    std::array<std::byte, 512> buf2{};
    auto without = fixpp::session::build_logon(
        std::span<std::byte>{buf2.data(), buf2.size()}, 1, "TW", "ISLD", "FIX.4.4", 30,
        "20240101-00:00:00.000", false, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
        fixpp::session::logon_advertise_options{});
    ASSERT_TRUE(without.has_value());
    EXPECT_EQ(extract_field(*without, 383), "") << "no 383 when unset (byte-identical)";
}

// (b) boundary: with advertised_max == heartbeat size, an in-sequence Heartbeat of
// exactly that size is accepted (stays Active).
TEST_F(MaxMsgSizeTest, InboundAtLimitAccepted) {
    auto hb = make_heartbeat_frame("FIX.4.4", 2, "TW", "ISLD");
    auto cfg = make_acceptor_cfg(static_cast<std::uint32_t>(hb.size()));  // N == frame size
    fixpp::session::Session sess(engine, cfg);
    ASSERT_TRUE(open_sync(sess).has_value());
    auto logon = make_logon_frame("FIX.4.4", 1, "TW", "ISLD", 30, std::nullopt);
    feed_sync(sess, std::span<const std::byte>{logon});
    ASSERT_EQ(sess.state(), fsm_state::Active);
    feed_sync(sess, std::span<const std::byte>{hb});
    EXPECT_EQ(sess.state(), fsm_state::Active) << "frame size == N must be accepted";
}

// (b) boundary + (c) pre-establishment: with advertised_max == heartbeat size - 1,
// the (larger) Logon still establishes (pre-Active not checked), then the Heartbeat
// of size N+1 relative to the limit disconnects.
TEST_F(MaxMsgSizeTest, InboundOverLimitDisconnects_LogonPreEstablishmentExempt) {
    auto hb = make_heartbeat_frame("FIX.4.4", 2, "TW", "ISLD");
    auto cfg = make_acceptor_cfg(static_cast<std::uint32_t>(hb.size() - 1));  // limit = size-1
    fixpp::session::Session sess(engine, cfg);
    ASSERT_TRUE(open_sync(sess).has_value());
    auto logon = make_logon_frame("FIX.4.4", 1, "TW", "ISLD", 30, std::nullopt);
    ASSERT_GT(logon.size(), hb.size() - 1) << "Logon must exceed the negotiated limit to prove exemption";
    feed_sync(sess, std::span<const std::byte>{logon});
    ASSERT_EQ(sess.state(), fsm_state::Active) << "oversized-vs-negotiated Logon establishes pre-Active";
    feed_sync(sess, std::span<const std::byte>{hb});
    EXPECT_EQ(sess.state(), fsm_state::Disconnected) << "post-Active frame > N must disconnect";
}

// (d) default unset ⇒ no 383 advertised + no enforcement (in-seq Heartbeat stays Active).
TEST_F(MaxMsgSizeTest, UnsetNoEnforcement) {
    auto cfg = make_acceptor_cfg(std::nullopt);
    fixpp::session::Session sess(engine, cfg);
    ASSERT_TRUE(open_sync(sess).has_value());
    auto logon = make_logon_frame("FIX.4.4", 1, "TW", "ISLD", 30, std::nullopt);
    feed_sync(sess, std::span<const std::byte>{logon});
    ASSERT_EQ(sess.state(), fsm_state::Active);
    auto hb = make_heartbeat_frame("FIX.4.4", 2, "TW", "ISLD");
    feed_sync(sess, std::span<const std::byte>{hb});
    EXPECT_EQ(sess.state(), fsm_state::Active) << "no enforcement when advertised_max unset";
}

// (e) FR-007: peer's advertised 383 is captured + observable.
TEST_F(MaxMsgSizeTest, PeerAdvertised383Captured) {
    auto cfg = make_acceptor_cfg(std::nullopt);  // our advertise off; only capturing the peer's
    fixpp::session::Session sess(engine, cfg);
    ASSERT_TRUE(open_sync(sess).has_value());
    auto logon = make_logon_frame("FIX.4.4", 1, "TW", "ISLD", 30, std::optional<std::uint32_t>{777});
    feed_sync(sess, std::span<const std::byte>{logon});
    ASSERT_EQ(sess.state(), fsm_state::Active);
    ASSERT_TRUE(sess.peer_max_message_size().has_value());
    EXPECT_EQ(*sess.peer_max_message_size(), 777U);
}

}  // namespace
}  // namespace fixpp::session::test
