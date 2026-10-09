// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_070_max_message_size_test.cpp
//
// 070-fix44-closeout US2 (S-030) — negotiated MaxMessageSize(383).
//
// Discriminating witness (FR-004..FR-007, tasks.md T011):
//   (a) advertised config ⇒ outbound Logon carries 383=N (unit test of build_logon).
//   (d) default (unset) ⇒ no 383 on the wire, and the session's inbound limit is the
//       default.
//   (e) peer's advertised 383 is captured + observable (FR-007).
//
// 093-inbound-frame-dispositions (FR-013, plan OD-3) supersedes (b) and (c): the
// session's inbound limit L is enforced by the Framer, in every state, below
// on_inbound_frame, where these cells feed. A frame of exactly L is delivered through
// the pump (engine_readpump_test.cpp, InboundAtLimitAccepted), a frame over L closes
// in every state (inbound_frame_dispositions_test.cpp, Q-6), and 070's
// pre-establishment exemption is reversed.
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
#include "support/pump_until_ready.hpp"
#include "support/session_test_access.hpp"

// ── #289: bounded pumps ──────────────────────────────────────────────────────
//
// Where a site in this file is migrated it uses `run_window_then_ready` plus a
// miss-branch drain (tests/support/pump_until_ready.hpp). The window is PRESERVED:
// the hazard #289 names is the UNCONDITIONAL `get()`, not the fixed window.
//
// The site label passed to `run_window_then_ready` is the FORCING SEAM: exporting
// FIXPP_FORCE_WINDOW_MISS=<label> makes exactly that site take its miss branch, with
// no source edit and no rebuild. It is a WEAKER witness than textual mutation and
// does not replace it -- see the primitive.
//
// Rationale and the teardown-shape rule live at the primitive, not duplicated here
// (#324).

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
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, std::chrono::milliseconds{200},
                                                        "MaxMsgSizeTest::open_sync")) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock,
                                                            "MaxMsgSizeTest::open_sync");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss << "MaxMsgSizeTest::open_sync";
            return std::unexpected(fixpp::test_support::kWindowMissSentinel);
        }
        return fut.get();
    }

    void feed_sync(fixpp::session::Session& s, std::span<const std::byte> frame) {
        auto fut = asio::co_spawn(ioc, s.on_inbound_frame(frame), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, std::chrono::milliseconds{200},
                                                        "MaxMsgSizeTest::feed_sync")) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock,
                                                            "MaxMsgSizeTest::feed_sync");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss << "MaxMsgSizeTest::feed_sync";
            return;
        }
        (void)fut.get();
    }
};

// (a) advertise side: build_logon emits 383=N iff opts.max_message_size set.
TEST(MaxMsgSizeAdvertise, BuildLogonEmits383) {
    std::array<std::byte, 512> buf{};
    auto with = fixpp::session::build_logon(
        std::span<std::byte>{buf.data(), buf.size()}, 1, "TW", "ISLD", "FIX.4.4", 30,
        "20240101-00:00:00.000", false, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
        fixpp::session::logon_advertise_options{.max_message_size = 4096U,
                                                .supported_msg_types = {}});
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

// (d) default unset ⇒ no 383 advertised, and L is the default: the acceptor's Logon
// reply carries no 383, and the session's inbound limit reads 65536 (093 FR-010).
TEST_F(MaxMsgSizeTest, UnsetAdvertisesNo383AndTheLimitIsTheDefault) {
    auto cfg = make_acceptor_cfg(std::nullopt);
    std::vector<std::string> sent;
    cfg.transport_send = [&sent](std::span<const std::byte> f) {
        sent.emplace_back(reinterpret_cast<const char*>(f.data()), f.size());
    };
    fixpp::session::Session sess(engine, cfg);
    ASSERT_TRUE(open_sync(sess).has_value());
    auto logon = make_logon_frame("FIX.4.4", 1, "TW", "ISLD", 30, std::nullopt);
    feed_sync(sess, std::span<const std::byte>{logon});
    ASSERT_EQ(sess.state(), fsm_state::Active);
    ASSERT_EQ(sent.size(), 1U) << "the acceptor's Logon reply";
    auto const reply = std::span<const std::byte>{
        reinterpret_cast<const std::byte*>(sent[0].data()), sent[0].size()};
    EXPECT_EQ(extract_field(reply, 35), "A");
    EXPECT_EQ(extract_field(reply, 383), "") << "no 383 on the wire when unset";
    EXPECT_EQ(fixpp::session::session_test_access::inbound_limit(sess), 65536U);
}

// (e) FR-007: peer's advertised 383 is captured + observable.
TEST_F(MaxMsgSizeTest, PeerAdvertised383Captured) {
    auto cfg = make_acceptor_cfg(std::nullopt);  // our advertise off; only capturing the peer's
    fixpp::session::Session sess(engine, cfg);
    ASSERT_TRUE(open_sync(sess).has_value());
    auto logon =
        make_logon_frame("FIX.4.4", 1, "TW", "ISLD", 30, std::optional<std::uint32_t>{777});
    feed_sync(sess, std::span<const std::byte>{logon});
    ASSERT_EQ(sess.state(), fsm_state::Active);
    ASSERT_TRUE(sess.peer_max_message_size().has_value());
    EXPECT_EQ(*sess.peer_max_message_size(), 777U);
}

}  // namespace
}  // namespace fixpp::session::test
