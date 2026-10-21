// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_inbound_cancellation_engine.cpp
//
// Ruling R-14 (.specify/536-537-538-recovery-liveness.md §9, D-CAN-2 and D-CAN-5; §5,
// the table "The inbound cancellation (§9)"), the cells an Engine's read pump drives:
// the frame comes from a raw peer socket (plain_engine_rig.hpp), and what ends the
// session is the pump's failed-delivery branch or Engine::stop. The cells in which the
// cell's own coroutine hands the frame over are in test_inbound_cancellation.cpp.
//
// A cell asserts nothing fatal while its engine runs, and stops the engine on every
// path: ~Engine requires a completed stop.

#include <gtest/gtest.h>

#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <fixpp/core/error.hpp>
#include <fixpp/session/application.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/memory_store_factory.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include "session/held_file_store_scene.hpp"
#include "session/plain_engine_rig.hpp"
#include "support/engine_test_access.hpp"
#include "support/pump_until_ready.hpp"
#include "support/recording_store.hpp"
#include "support/session_test_access.hpp"

namespace {

namespace plain_rig = fixpp::test_support::plain_rig;
namespace store_hold = fixpp::test_support::store_hold;
using fixpp::session::direction_t;
using fixpp::session::fsm_state;
using fixpp::session::session_test_access;
using fixpp::test_support::HeldFileStoreScene;
using fixpp::test_support::store_op;
using fixpp::test_support::StoreCall;

// ── D-CAN-2, an exception that is not a cancellation, under an Engine ────────────────

constexpr int kAppMessages = 3;

// The cell's wait is this fraction of the fixture's heartbeat interval.
constexpr int kWaitDivisor = 10;

// The body of one slot's retrieve throws std::runtime_error while the session answers
// the peer's ResendRequest. The session must end there, and the pump's failed-delivery
// branch must close the connection, with no Engine::stop.
TEST(InboundCancellationEngine, WalkReadThrowsAnotherException_PeerConnectionEnds) {
    std::string setup_miss;
    bool read_threw = false;  // the read's body started and did not return
    bool peer_read_ended_within_the_wait = false;
    bool engine_stopped_at_the_wait_end = true;
    fsm_state state_at_the_wait_end = fsm_state::NotConnected;
    std::chrono::seconds heartbeat{0};
    bool stop_completed = false;
    {
        HeldFileStoreScene scene;
        auto& rig = scene.rig;
        scene.tweak_cfg = [&](fixpp::session::SessionConfig& c) {
            heartbeat = c.heartbeat_interval.value_or(std::chrono::seconds{0});
        };
        [&] {
            if (!scene.setup(kAppMessages)) return;
            auto const session = rig.session();
            if (!session) {
                scene.setup_miss = "the session is not published";
                return;
            }
            auto const next_slot = scene.first + 1;
            auto is_next_slot_read = [next_slot](StoreCall const& call) {
                return call.op == store_op::retrieve && call.dir == direction_t::outbound &&
                       call.first == next_slot;
            };
            scene.log->throw_in = is_next_slot_read;
            auto const mark = scene.log->mark();
            scene.peer_resend_request(2, scene.first, scene.last);

            // The bound: the session's heartbeat interval is the only timer of the
            // fixture that could end the session, so the wait is a fraction of it. On
            // a tree where nothing closes the connection the wait runs out.
            auto const wait =
                std::chrono::duration_cast<std::chrono::milliseconds>(heartbeat) / kWaitDivisor;
            peer_read_ended_within_the_wait =
                rig.run_until([&] { return rig.peer.read_ended; }, wait);
            state_at_the_wait_end = session->state();
            engine_stopped_at_the_wait_end = rig.engine->stopped();
            if (auto const* r = scene.log->first_after(mark, is_next_slot_read)) {
                read_threw = r->entered() && !r->returned();
            }
        }();
        scene.finish();
        setup_miss = scene.setup_miss;
        stop_completed = scene.stop_completed;
    }
    ASSERT_TRUE(setup_miss.empty()) << setup_miss;
    ASSERT_TRUE(stop_completed);
    ASSERT_GT(heartbeat.count(), 0) << "the fixture's heartbeat interval was not read";
    ASSERT_TRUE(read_threw) << "the path was not exercised: the read's body must start and not "
                               "return";

    EXPECT_TRUE(peer_read_ended_within_the_wait)
        << "the peer's connection was still open when the wait ended";
    EXPECT_EQ(state_at_the_wait_end, fsm_state::Disconnected) << "the state when the wait ended";
    EXPECT_FALSE(engine_stopped_at_the_wait_end) << "the cell's own Engine::stop had begun";
}

// ── D-CAN-5, a superseded Logon arm, the engine-stop leg ─────────────────────────────

// Counts onLogout, and every frame given to toAdmin.
struct StopLegApp final : fixpp::session::Application {
    int logouts = 0;
    std::size_t to_admin = 0;
    void onLogout(const fixpp::session::SessionId& /*id*/) override { ++logouts; }
    void toAdmin(const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& /*msg*/,
                 const fixpp::session::SessionId& /*id*/) override {
        ++to_admin;
    }
};

// The scene of Od25.EngineStopDuringA789ReplaysReadFiresNoGapFill
// (engine_reset_unit_stop_test.cpp): the initiator sent 141=Y under bilateral_strict; the
// peer acks with 141=Y and 789=1, so the reset unit runs and the 789 walk follows it. The
// unit's trailing reset leaves the thread accepting a total cancellation, and no await
// separates it from the walk's first store read. The call hook on that read spawns
// Engine::stop, whose step 1 sets the session's engine-stop flag and emits before the
// hook returns; the walk's co_await then throws. The arm is superseded, so it must
// record nothing, and the read pump must go on to run the session's close itself.
TEST(InboundCancellationEngine,
     SupersededLogonArm_EngineStop_ReadCancelledBeforeItsAwait_RecordsNothing) {
    auto const app = std::make_shared<StopLegApp>();
    plain_rig::Rig rig{app};
    auto cfg = rig.cfg(fixpp::session::session_role::initiator);
    cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_strict;
    cfg.enable_next_expected_msg_seq_num = true;
    auto const factory = std::make_shared<fixpp::test_support::RecordingStoreFactory>(
        std::make_shared<fixpp::session::MemoryStoreFactory>(fixpp::session::MemoryStore::Config{
            .policy = fixpp::session::capacity_policy::unbounded}));
    auto const log = factory->log;
    log->arm();
    cfg.store_factory = factory;

    std::shared_ptr<fixpp::session::Session> session;
    std::future<void> stop_fut;
    bool hook_ran = false;
    bool stop_flag_set_in_hook = false;
    std::size_t to_admin_at_hook = 0;
    // The walk's first store read: the first outbound next_seqnum(_, false) after the
    // reset unit's reset_to.
    bool unit_ran = false;
    auto is_tail_read = [](StoreCall const& call) {
        return call.op == store_op::next_seqnum && call.dir == direction_t::outbound &&
               !call.increment;
    };
    std::size_t unit_mark = 0;
    log->on_call = [&](StoreCall const& call) {
        if (hook_ran) return;
        if (call.op == store_op::reset_to) {
            unit_ran = true;
            unit_mark = call.called_at;
            return;
        }
        if (!unit_ran || !is_tail_read(call) || !session) return;
        hook_ran = true;
        to_admin_at_hook = app->to_admin;
        stop_fut = asio::co_spawn(rig.ioc, rig.engine->stop(), asio::use_future);
        stop_flag_set_in_hook = session_test_access::engine_stop_requested(*session);
    };

    // What the session reads at one moment.
    struct Sample {
        bool taken = false;
        fsm_state state = fsm_state::NotConnected;
        bool drained = false;
    };
    auto sample = [&] {
        return Sample{.taken = true,
                      .state = session->state(),
                      .drained = session_test_access::is_drained(*session)};
    };
    Sample after_hook_handler;
    Sample before_stops_own_close;

    // Each setup miss below fails the cell and stops the engine; none is a fatal ASSERT.
    auto fail_and_stop = [&](char const* what) {
        ADD_FAILURE() << what;
        log->on_call = nullptr;
        rig.stop();
    };
    if (!rig.start(cfg)) {
        fail_and_stop("the rig did not start");
        return;
    }
    if (!rig.run_until([&] {
            return !plain_rig::frames_of_type(rig.peer.received, "A").empty() &&
                   rig.state() == fsm_state::LogonSent;
        })) {
        fail_and_stop("the initiator's Logon");
        return;
    }
    session = rig.session();
    if (!session) {
        fail_and_stop("the session is not published");
        return;
    }
    fixpp::session::engine_test_access::set_post_send_drain_hook(
        *rig.engine, [&]() -> asio::awaitable<void> {
            before_stops_own_close = sample();
            co_return;
        });
    rig.peer.send(rig.msg("A", 1,
                          "98=0\x01"
                          "108=30\x01"
                          "141=Y\x01"
                          "789=1\x01"));

    // One handler at a time, so that the session is read after the handler that ran the
    // hook and before any other.
    auto const deadline = std::chrono::steady_clock::now() + fixpp::test_support::kPumpBudget;
    while (!hook_ran && std::chrono::steady_clock::now() < deadline) {
        (void)rig.ioc.run_one_for(fixpp::test_support::kPumpSlice);
        rig.ioc.restart();
    }
    if (!hook_ran) {
        fail_and_stop("the path was not exercised: the 789 walk's first store read was not called");
        return;
    }
    after_hook_handler = sample();

    // The bound: no store call is held and no real-time timer runs in this scene, so
    // nothing but the stop's own steps can complete it.
    bool const stopped = rig.run_until(
        [&] { return stop_fut.wait_for(std::chrono::seconds{0}) == std::future_status::ready; });
    log->on_call = nullptr;
    if (!stopped) {
        fixpp::test_support::cancel_and_drain_or_report(rig.ioc, *rig.clock,
                                                        "InboundCancellationEngine stop");
        ADD_FAILURE() << "Engine::stop() did not complete";
        return;
    }
    stop_fut.get();

    std::optional<StoreCall> read;
    if (auto const* r = log->first_after(unit_mark, is_tail_read)) read = *r;
    auto const ring_span = session->fsm_visit_history();
    std::vector<fsm_state> const ring{ring_span.begin(), ring_span.end()};
    (void)rig.run_until([&] { return rig.peer.read_ended; });

    // The window: the stop's step 1 ran before the hook returned, and the read was
    // called and its body never started.
    ASSERT_TRUE(stop_flag_set_in_hook && read.has_value() && !read->entered())
        << "the path was not exercised: stop_flag_set_in_hook=" << stop_flag_set_in_hook
        << " read_recorded=" << read.has_value()
        << " body_entered=" << (read.has_value() && read->entered());

    // When the hook's handler has returned, before the next handler runs: the arm
    // recorded nothing.
    EXPECT_EQ(after_hook_handler.state, fsm_state::LogonSent)
        << "the state when the hook's handler has returned";
    // Inside Engine::stop, before its own close: the read pump's close has run.
    ASSERT_TRUE(before_stops_own_close.taken) << "the post-send-drain hook did not run";
    EXPECT_TRUE(before_stops_own_close.drained)
        << "the session is not drained inside Engine::stop, before its own close of it, so the "
           "read pump's close did not run";
    EXPECT_EQ(ring, (std::vector<fsm_state>{fsm_state::LogonSent, fsm_state::Disconnected}));
    // Nothing was written after the hook.
    EXPECT_EQ(app->to_admin, to_admin_at_hook) << "an admin frame reached toAdmin after the hook";
    EXPECT_TRUE(plain_rig::frames_of_type(rig.peer.received, "4").empty())
        << "a GapFill was written";
}

}  // namespace
