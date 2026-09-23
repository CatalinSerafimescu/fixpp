// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/session/test_reify_shared_dispatch.cpp
//
// fixpp#495 (`.specify/495-493-486-dict-reify-copy.md` §2.6, §3.4, §10 T-13, C++
// twin): the SHIPPED dispatch path. A `Session` given `SessionConfig::dict_snapshot`
// hands its application owned-route views, so `dict::reify` of two consecutive
// inbound messages yields two handles SHARING one table; and that table does not
// keep the `Dictionary` alive (D-4): once the test drops every anchor it holds and
// destroys the Session, the Dictionary expires while both handles still read.
//
// Discriminates against `parse_and_dispatch_`'s parser on the borrowed route
// (`{*inbound_tv_}`), which fails the address equality, and against an aliasing
// `shared_dictionary_view` (D-4 reverted), which fails `weak.expired()`.
//
// Standalone (`[const §VII.8]`): a live Session with its own io_context.

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/dictionary_snapshot.hpp>
#include <fixpp/dict/reify.hpp>
#include <fixpp/dict/version_profile.hpp>
#include <fixpp/session/application.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <memory>
#include <memory_resource>
#include <span>
#include <string>
#include <vector>

#include "support/fix44_dictionary.hpp"
#include "support/fix44_group_frame_bodies.hpp"
#include "support/minimal_security_profile.hpp"
#include "support/pump_until_ready.hpp"
#include "support/transport_double.hpp"

using namespace std::chrono_literals;

namespace fixpp::session::test {
namespace {

constexpr fixpp::dict::version_profile kProfileV44{
    .session = fixpp::dict::session_version::v44,
    .default_appl = fixpp::dict::application_version::v44,
    .has_per_message_override = false,
    ._reserved = 0};

// Reifies every application message into a handle whose arena the TEST owns,
// outside both the Session and this application.
class ReifyingApplication : public Application {
public:
    explicit ReifyingApplication(std::pmr::memory_resource* handle_mr) : mr_{handle_mr} {}

    std::vector<fixpp::dict::owning_message_handle> handles;
    int reify_failures = 0;

    fixpp::core::expected_t<void> fromApp(
        const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& msg,
        const SessionId& /*id*/) override {
        auto h = fixpp::dict::reify(msg, kProfileV44, mr_);
        if (h.has_value()) {
            handles.push_back(std::move(*h));
        } else {
            ++reify_failures;
        }
        return {};
    }

    fixpp::core::expected_t<void> fromAdmin(
        const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>&,
        const SessionId&) override {
        return {};
    }

private:
    std::pmr::memory_resource* mr_;
};

template <class Fut>
bool pump(asio::io_context& ioc, fixpp::core::mock_clock& clock, Fut& fut, char const* what) {
    if (!fixpp::test_support::run_window_then_ready(ioc, fut, 200ms)) {
        fixpp::test_support::cancel_and_drain_or_report(ioc, clock, what);
        ADD_FAILURE() << fixpp::test_support::kWindowMiss << what;
        return false;
    }
    return true;
}

TEST(ReifySharedDispatch, HandlesShareOneTableAndDoNotPinTheDictionary) {
    std::pmr::monotonic_buffer_resource handle_mr;  // outlives Session and application
    auto app = std::make_shared<ReifyingApplication>(&handle_mr);
    std::weak_ptr<const fixpp::dict::Dictionary> weak;

    {
        asio::io_context ioc;
        fixpp::core::EngineConfig engine;
        auto clock = std::make_shared<fixpp::core::mock_clock>(
            std::chrono::system_clock::time_point{} + std::chrono::seconds{1704067200},
            fixpp::core::steady_time_point{}, ioc.get_executor());
        engine.clock = clock;
        engine.executor = ioc.get_executor();
        engine.application = app;
        TransportDouble transport;

        SessionConfig cfg;
        cfg.sender_comp_id = "ISLD";
        cfg.target_comp_id = "TW";
        cfg.begin_string = "FIX.4.4";
        cfg.heartbeat_interval = 0s;  // no liveness loop
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = fixpp::test_support::make_fix44_dictionary();
        cfg.dict_snapshot = fixpp::dict::make_dictionary_snapshot(cfg.dictionary);
        ASSERT_NE(cfg.dict_snapshot, nullptr);
        weak = cfg.dictionary;
        cfg.executor_override = ioc.get_executor();
        cfg.transport_send = [&transport](std::span<const std::byte> frame) {
            transport.capture_outbound(frame);
        };
        cfg.reset_seqnum_policy_field = reset_seqnum_policy::bilateral_lenient;

        auto sess = std::make_unique<Session>(engine, cfg);
        {
            auto fut = asio::co_spawn(ioc, sess->open(), asio::use_future);
            ASSERT_TRUE(pump(ioc, *clock, fut, "open"));
            ASSERT_TRUE(fut.get().has_value());
        }
        {
            auto logon = fixpp_test_support::make_frame(
                "FIX.4.4", std::string("35=A\x01") + "34=1\x01" + "49=TW\x01" +
                               "52=20240101-00:00:00.000\x01" + "56=ISLD\x01" + "98=0\x01" +
                               "108=0\x01");
            auto fut = asio::co_spawn(ioc, sess->on_inbound_frame(logon), asio::use_future);
            ASSERT_TRUE(pump(ioc, *clock, fut, "logon"));
            ASSERT_TRUE(fut.get().has_value());
            ASSERT_EQ(sess->state(), fsm_state::Active);
        }
        auto const suffix = fixpp_test_support::execution_report_two_legs_trailing_suffix();
        for (std::uint32_t seq : {2U, 3U}) {
            auto frame = fixpp_test_support::make_execution_report_frame(suffix, seq, "TW", "ISLD");
            auto fut = asio::co_spawn(ioc, sess->on_inbound_frame(frame), asio::use_future);
            ASSERT_TRUE(pump(ioc, *clock, fut, "exec report"));
            (void)fut.get();
        }

        ASSERT_EQ(app->reify_failures, 0);
        ASSERT_EQ(app->handles.size(), 2U) << "both ExecutionReports must reach fromApp";
        EXPECT_EQ(app->handles[0].view().hooks().opaque_dict(),
                  app->handles[1].view().hooks().opaque_dict())
            << "two handles reified from the shipped dispatch path must SHARE one table";

        // Drop every anchor the test holds, then the Session: the config (its
        // Dictionary and snapshot), and the EngineConfig's application copy.
        sess.reset();
        cfg = SessionConfig{};
        engine = fixpp::core::EngineConfig{};
    }

    EXPECT_TRUE(weak.expired())
        << "a live handle must pin the TABLE only, never the Dictionary (D-4)";
    ASSERT_EQ(app->handles.size(), 2U);
    for (auto const& h : app->handles) {
        EXPECT_EQ(h.view().offsets().group_slices(555).size(), 2U)
            << "each handle still reads its group through the table it pins";
    }
}

}  // namespace
}  // namespace fixpp::session::test
