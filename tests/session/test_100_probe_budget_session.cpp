// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/session/test_100_probe_budget_session.cpp — 100-wire-group-read-integrity, tasks T018
// (US3.3; FR-008; research D1, D9 "#645 (D1)", inbound session cell).
//
// A frame crafted against the index seed (T007's window cluster, under the seed the cell sets
// before the session's first parse) fails the parse on D1's per-build budget, and the session
// takes 092's late-parse-failure ending: a terminal close, no Reject(35=3), no Logout(35=5), and
// no receive callback for the frame. One cell per route the survey names: the dispatch parse
// with inbound validation off (`dispatch_outcome::parse_failed`), and the validate gate's parse
// with it on (`validate_outcome::parse_failed`).
//
// The rig is tests/session/unparseable_frame_disposition_test.cpp's DispositionFixture, copied
// here since that file is built into a different test binary: a Session on
// fixpp::core::mock_clock with a TransportDouble, its frames fed through on_inbound_frame.
// This file sets the process-global overlay seed, so it belongs to the standalone entry
// `wire_seed_hook_tests` (plan Structure Decision), and each cell restores the prior seed
// through `test100::overlay_seed_guard`. After open_to_active every observation is non-fatal.

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/application.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "support/extract_tag.hpp"
#include "support/minimal_security_profile.hpp"
#include "support/probe_cluster_100.hpp"
#include "support/pump_until_ready.hpp"
#include "support/reify_test_frame.hpp"
#include "support/transport_double.hpp"
#include "support/validation_test_dictionary.hpp"
#include "support/wire_test_hooks.hpp"

using namespace std::chrono_literals;

namespace fixpp::session::test {
namespace {

namespace t100 = fixpp::wire::test100;
using fixpp::test_support::extract_tag;

constexpr std::uint32_t kClusterSeed = 0x9E3779B9U;
constexpr std::size_t kEntries = 1024;

void drain_ready(asio::io_context& ioc) {
    ioc.restart();
    while (ioc.poll() > 0) {
        ioc.restart();
    }
    ioc.restart();
}

// Counts inbound deliveries.
class CountingApplication final : public Application {
public:
    int from_admin = 0;
    int from_app = 0;
    fixpp::core::expected_t<void> fromAdmin(
        const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& /*msg*/,
        const SessionId& /*id*/) override {
        ++from_admin;
        return {};
    }
    fixpp::core::expected_t<void> fromApp(
        const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& /*msg*/,
        const SessionId& /*id*/) override {
        ++from_app;
        return {};
    }
};

struct Fixture {
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine;
    TransportDouble transport;
    std::shared_ptr<CountingApplication> app = std::make_shared<CountingApplication>();

    Fixture() {
        using namespace std::chrono;
        auto utc = system_clock::time_point{} + seconds{1704067200};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, fixpp::core::steady_time_point{},
                                                          ioc.get_executor());
        engine.clock = clock;
        engine.executor = ioc.get_executor();
        engine.application = app;
    }

    SessionConfig make_cfg(bool validate) {
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
        cfg.validate_inbound_messages = validate;
        return cfg;
    }

    template <class Fut>
    bool pump(Fut& fut, char const* where) {
        if (!fixpp::test_support::pump_until_ready(ioc, fut, where)) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock, where);
            ADD_FAILURE() << fixpp::test_support::kPumpBudgetMiss << where;
            return false;
        }
        drain_ready(ioc);
        return true;
    }

    void open_to_active(Session& sess) {
        transport.reset();
        auto fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
        ASSERT_TRUE(pump(fut, "open")) << "open() did not complete";
        ASSERT_TRUE(fut.get().has_value()) << "open() failed";
        auto const logon = frame("A", 1,
                                 "98=0\x01"
                                 "108=30\x01");
        transport.reset();
        auto fut2 = asio::co_spawn(ioc, sess.on_inbound_frame(logon), asio::use_future);
        ASSERT_TRUE(pump(fut2, "logon")) << "the Logon reply did not complete";
        ASSERT_TRUE(fut2.get().has_value());
        ASSERT_EQ(sess.state(), fsm_state::Active);
    }

    void feed(Session& sess, std::span<const std::byte> f) {
        transport.reset();
        auto fut = asio::co_spawn(ioc, sess.on_inbound_frame(f), asio::use_future);
        if (pump(fut, "feed")) {
            (void)fut.get();
        }
    }

    [[nodiscard]] std::size_t sent_of_type(std::string_view mt) const {
        std::size_t n = 0;
        for (auto const& f : transport.sent_frames()) {
            n += (extract_tag(f, 35) == mt) ? 1U : 0U;
        }
        return n;
    }

    // A frame whose field 3 is 35, then 34 and the peer's header fields, then `extra`.
    static std::vector<std::byte> frame(std::string_view msg_type, std::uint32_t seq,
                                        std::string const& extra) {
        std::string body = "35=" + std::string(msg_type) + "\x01";
        body += "34=" + std::to_string(seq) + "\x01";
        body +=
            "49=TW\x01"
            "52=20240101-00:00:00.000\x01"
            "56=ISLD\x01";
        body += extra;
        return fixpp::test_support::assemble_frame("8=FIX.4.2\x01", body);
    }
};

std::string const kOrderFields =
    "11=ORD1\x01"
    "54=1\x01"
    "60=20240101-00:00:00.000\x01";
std::vector<std::uint16_t> const kFrameHead{35, 34, 49, 52, 56, 11, 54, 60};

// US3.3: the crafted cluster NewOrderSingle at the expected MsgSeqNum closes the session.
void run_crafted_frame_cell(bool validate, char const* route) {
    t100::overlay_seed_guard const guard;
    fixpp::wire::detail::set_overlay_seed_for_testing(kClusterSeed);

    std::size_t const k = kEntries - kFrameHead.size() - 3U;  // 8, 9 and 10 besides the head
    auto const mask = static_cast<std::uint32_t>(t100::overlay_cap_ref(kEntries) - 1U);
    auto const cluster = t100::window_cluster(kClusterSeed, mask, k);
    ASSERT_EQ(cluster.size(), k) << "setup: cluster";
    std::vector<std::uint16_t> body_tags = kFrameHead;
    body_tags.insert(body_tags.end(), cluster.begin(), cluster.end());
    auto const sim = t100::simulate_build(t100::frame_tag_sequence(body_tags), kClusterSeed);
    ASSERT_EQ(sim.entries, kEntries);
    ASSERT_GT(sim.total_probes, sim.budget())
        << "setup: the crafted frame must exceed kMaxBuildProbe x occurrences";

    std::string extra = kOrderFields;
    for (auto const t : cluster) {
        extra += std::to_string(t) + "=x\x01";
    }
    auto const crafted = Fixture::frame("D", 2, extra);

    Fixture fix;
    Session sess{fix.engine, fix.make_cfg(validate)};
    fix.open_to_active(sess);
    if (::testing::Test::HasFatalFailure()) {
        return;
    }
    int const app_before = fix.app->from_app;
    fix.feed(sess, crafted);

    bool const open_after = sess.is_open();
    fsm_state const state_after = sess.state();
    std::size_t const rejects = fix.sent_of_type("3");
    std::size_t const logouts = fix.sent_of_type("5");
    int const app_calls = fix.app->from_app - app_before;

    EXPECT_FALSE(open_after) << route << ": US3.3 / FR-008: the parse failure closes the session";
    EXPECT_EQ(static_cast<int>(state_after), static_cast<int>(fsm_state::Disconnected))
        << route << ": state after the frame";
    EXPECT_EQ(rejects, 0U) << route << ": no Reject(35=3)";
    EXPECT_EQ(logouts, 0U) << route << ": no Logout(35=5)";
    EXPECT_EQ(app_calls, 0) << route << ": fromApp is not invoked for the frame";

    if (sess.is_open()) {
        auto fut = asio::co_spawn(fix.ioc, sess.close(close_mode::terminal), asio::use_future);
        if (fix.pump(fut, "close")) {
            (void)fut.get();
        }
    }
}

TEST(ProbeBudgetSession100, US3_3_CraftedFrameValidationOff_TerminalCloseNoCallback) {
    run_crafted_frame_cell(false, "dispatch_outcome::parse_failed (validation off)");
}
TEST(ProbeBudgetSession100, US3_3_CraftedFrameValidationOn_TerminalCloseNoCallback) {
    run_crafted_frame_cell(true, "validate_outcome::parse_failed (validation on)");
}

}  // namespace
}  // namespace fixpp::session::test
