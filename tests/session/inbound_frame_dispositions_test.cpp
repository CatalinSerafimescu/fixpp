// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/inbound_frame_dispositions_test.cpp
//
// 093-inbound-frame-dispositions — session-level cells (quickstart.md §1). Later
// tasks add their cells here; this file starts with Q-13's L half.
//
// Q-13 (L half; data-model E-2): the session's one inbound limit L follows an
// advertised MaxMessageSize(383) inside [4096, 262144] when set, and is 65536 when
// unset. Read after open() through session_test_access (fixpp#511). The refusal of a
// value outside that range is a separate cell.
#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstdint>
#include <fixpp/core/clock.hpp>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <memory>
#include <optional>

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"
#include "support/pump_until_ready.hpp"
#include "support/session_test_access.hpp"

namespace fixpp::session::test {
namespace {

class InboundFrameDispositions : public ::testing::Test {
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

    SessionConfig make_acceptor_cfg(std::optional<std::uint32_t> advertised_max) {
        SessionConfig cfg;
        cfg.sender_comp_id = "ISLD";
        cfg.target_comp_id = "TW";
        cfg.begin_string = "FIX.4.4";
        cfg.heartbeat_interval = std::chrono::seconds{30};
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override = ioc.get_executor();
        cfg.role = session_role::acceptor;
        cfg.advertised_max_message_size = advertised_max;
        return cfg;
    }

    fixpp::core::expected_t<void> open_sync(Session& s) {
        auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, std::chrono::milliseconds{200},
                                                        "InboundFrameDispositions::open_sync")) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock,
                                                            "InboundFrameDispositions::open_sync");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss
                          << "InboundFrameDispositions::open_sync";
            return std::unexpected(fixpp::test_support::kWindowMissSentinel);
        }
        return fut.get();
    }
};

// ── Q-13, the L half ─────────────────────────────────────────────────────────

TEST_F(InboundFrameDispositions, Q13_InboundLimitIs65536WhenNoMaxMessageSizeIsAdvertised) {
    Session sess(engine, make_acceptor_cfg(std::nullopt));
    ASSERT_TRUE(open_sync(sess).has_value());
    EXPECT_EQ(session_test_access::inbound_limit(sess), 65536U);
}

TEST_F(InboundFrameDispositions, Q13_InboundLimitFollowsAnAdvertisedMaxMessageSizeInRange) {
    // The range's two ends and a value between them that is not the unset default.
    for (std::uint32_t const advertised : {4096U, 100000U, 262144U}) {
        SCOPED_TRACE(advertised);
        Session sess(engine, make_acceptor_cfg(advertised));
        ASSERT_TRUE(open_sync(sess).has_value());
        EXPECT_EQ(session_test_access::inbound_limit(sess), advertised);
    }
}

}  // namespace
}  // namespace fixpp::session::test
