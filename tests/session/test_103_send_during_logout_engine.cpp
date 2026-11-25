// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_103_send_during_logout_engine.cpp — test-only.
//
// Feature 103 (specs/103-resend-logon-reset-fidelity/; FR-021, an addendum by orchestrator
// ruling in Phase 8; tasks T187). An application send made while the session is ending by
// a Logout of its own is refused for the whole ending: no application frame follows the
// Logout. These cells hold that Logout inside its FileStore write, where the store's lock
// orders every later store call behind it, and make an Engine::send during the hold.
//
// Each cell runs an acceptor session under an Engine on the plain transport, facing a raw
// peer, on held_file_store_scene.hpp's Scene (a mock engine clock; a real FileStore). A
// cell asserts nothing while its engine runs, and stops the engine on every path.
//
// The endings:
//   in_sequence_logout  the peer's in-sequence Logout; the hold is on the confirming
//                       Logout's store, the first offloaded write after the peer's Logout.
//   gap_deadline        a gap kept open past its deadline (2 × HeartBtInt with NextNumIn
//                       not advancing); the hold is on the deadline's Logout's store, the
//                       first offloaded write after the frame that trips it.
//
// Before the arm, the cell drains the store: a store call of its own on the session's
// executor, run to its end, so the hold is on the first offloaded write after the peer's
// frame. Without it the hold can catch a write the session made earlier, such as the Logon's
// inbound advance, which the session makes after its Logon reply and so after setup(0) can
// return. The mechanism is in test_598_store_offload_engine.cpp, the paragraph "Before the
// arm".
//
// The instance InSequenceLogout_LogonWriteQueuedAtTheArm keeps that write from beginning
// until the io_context runs again after the arm site: its Application's onLogon takes the
// FileStore's one file I/O thread, so the Logon's inbound advance, which the session makes
// after onLogon, queues behind it, and the cell lets the thread go from a handler it posts
// to the io_context just before the drain. It is the drain's known positive: delete the
// drain and re-run this binary unfiltered; this instance must fail on "the send was
// admitted".
#include <gtest/gtest.h>

#include <asio/any_io_executor.hpp>
#include <asio/co_spawn.hpp>
#include <asio/post.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/error.hpp>
#include <fixpp/session/application.hpp>
#include <fixpp/session/direction.hpp>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "session/held_file_store_scene.hpp"
#include "support/recording_file_executor.hpp"

namespace {

namespace fe = fixpp::test_support::file_executor;
namespace plain_rig = fixpp::test_support::plain_rig;
namespace store_hold = fixpp::test_support::store_hold;
using fixpp::test_support::HeldFileStoreScene;

enum class Ending : std::uint8_t { in_sequence_logout, gap_deadline };

struct Case {
    Ending ending;
    bool logon_write_late = false;  // the Logon's inbound advance queued at the arm site
};

struct OnLogonApp final : fixpp::session::Application {
    std::function<void()> on_logon;
    void onLogon(const fixpp::session::SessionId& /*id*/) override {
        if (on_logon) on_logon();
    }
};

struct Outcome {
    std::string setup_miss;
    bool logon_write_queued = false;  // the file I/O thread was held at the arm site
    bool hold_entered = false;
    std::optional<fixpp::core::expected_t<void>> send;  // the Engine::send made during the hold
    std::vector<std::string> frames;  // every frame the peer read from the ending's frame on
    bool hold_timed_out = false;
    bool stop_completed = false;
};

// The HeartBtInt of plain_engine_rig.hpp's session configuration.
constexpr auto kHeartBtInt = std::chrono::seconds{30};

Outcome run(Case const c) {
    Outcome o;
    auto const ending = c.ending;
    // Declared before the Scene: the pool job that holds the file I/O thread refers to it.
    auto const pool_hold = std::make_shared<fe::JobLog>();
    auto const app = std::make_shared<OnLogonApp>();
    HeldFileStoreScene scene{1U, c.logon_write_late ? app : nullptr};
    // After the Scene: the held thread is let go before the Scene joins its pool.
    fe::let_go_on_exit const let_go{pool_hold};
    if (c.logon_write_late) {
        scene.wrap_file_executor = [&app, pool_hold](asio::thread_pool::executor_type ex) {
            app->on_logon = [pool_hold, ex] { pool_hold->hold_a_thread(ex); };
            return asio::any_io_executor{ex};
        };
    }
    if (ending == Ending::gap_deadline) {
        // No TestRequest falls due before the gap's deadline under this threshold, so the peer
        // owes no answer inside the window and the liveness loop cannot end the session there.
        // Re-derive with: the TestRequest instant is the last inbound frame plus
        // test_request_threshold (run_liveness_loop_body_ in src/session/session.cpp); the
        // deadline is resend_gap_deadline in src/session/read_first_frame_bounded.hpp.
        scene.tweak_cfg = [](fixpp::session::SessionConfig& s) {
            s.test_request_threshold = std::chrono::minutes{10};
        };
    }
    auto& rig = scene.rig;
    // The frame types the peer read from `from` on, and the session's state, for a miss.
    auto reading = [&rig](std::size_t from) {
        std::string out;
        for (char const* t : {"0", "1", "2", "3", "4", "5", "D"}) {
            out +=
                std::string{" 35="} + t + ":" +
                std::to_string(plain_rig::frames_of_type(rig.peer.received.substr(from), t).size());
        }
        return out + "; state " + std::to_string(static_cast<int>(rig.state()));
    };
    std::size_t gap_rx = 0;
    [&] {
        if (!scene.setup(0)) return;
        std::uint32_t seq = 2;  // the Logon was 34=1
        std::string trip;
        if (ending == Ending::gap_deadline) {
            // A gap: a frame three above NextNumIn draws the session's ResendRequest, which
            // is not answered, so NextNumIn does not advance. The peer sends nothing more until
            // the deadline's frame, and no TestRequest falls due before it (the threshold above).
            seq += 3;
            rig.peer.send(rig.heartbeat(seq));
            if (!rig.run_until(
                    [&] { return !plain_rig::frames_of_type(rig.peer.received, "2").empty(); })) {
                o.setup_miss = "the gap drew no ResendRequest";
                return;
            }
            // Past the deadline in one advance, with no real-time window in between: the
            // session owes a Heartbeat for its outbound silence (more than HeartBtInt since the
            // ResendRequest), and the cell waits until the peer has read it, so the liveness
            // loop's work for this advance is done before the drain.
            gap_rx = rig.peer.received.size();
            rig.clock->advance(2 * kHeartBtInt + std::chrono::seconds{1});
            if (!rig.run_until([&] {
                    return !plain_rig::frames_of_type(rig.peer.received.substr(gap_rx), "0")
                                .empty();
                })) {
                o.setup_miss =
                    "no Heartbeat after the advance past the deadline;" + reading(gap_rx);
                return;
            }
            trip = rig.heartbeat(++seq);
        } else {
            trip = rig.msg("5", seq);
        }
        auto const rx = rig.peer.received.size();
        if (c.logon_write_late) {
            o.logon_write_queued = pool_hold->wait_holders(1);
            asio::post(rig.ioc, [pool_hold] { pool_hold->let_go(); });
        }
        // The drain, test_598_store_offload_engine.cpp's shape.
        auto const session = rig.session();
        auto* const store = scene.log ? scene.log->store : nullptr;
        if (!session || store == nullptr) {
            o.setup_miss = "the session or its store is not published";
            return;
        }
        auto drain_fut = asio::co_spawn(
            session->executor().underlying(),
            store->next_seqnum(fixpp::session::direction_t::outbound, false), asio::use_future);
        if (!rig.run_until([&] { return HeldFileStoreScene::ready(drain_fut); }) ||
            !drain_fut.get().has_value()) {
            o.setup_miss = "the drain before the arm did not complete";
            return;
        }
        // Immediately before the arm: the session is Active and has written no Logout, so the
        // first offloaded write after the trip frame can be the ending's.
        if (rig.state() != fixpp::session::fsm_state::Active ||
            !plain_rig::frames_of_type(rig.peer.received.substr(gap_rx), "5").empty()) {
            o.setup_miss = "the session ended before the deadline's frame;" + reading(gap_rx);
            return;
        }
        auto const at_arm = reading(gap_rx);
        store_hold::arm();
        rig.peer.send(trip);
        if (!rig.run_until([] { return store_hold::holding(); })) {
            o.setup_miss = "the ending's Logout store was not held; at the arm:" + at_arm +
                           "; now:" + reading(rx);
            return;
        }
        o.hold_entered = true;
        // A refused send completes inside this window; an admitted one queues at the
        // store's lock behind the held write and completes after the release.
        auto send_fut = scene.send_app();
        rig.settle();
        if (HeldFileStoreScene::ready(send_fut)) o.send = send_fut.get();
        store_hold::release();
        if (!o.send && rig.run_until([&] { return HeldFileStoreScene::ready(send_fut); })) {
            o.send = send_fut.get();
        }
        (void)rig.run_until(
            [&] { return !plain_rig::frames_of_type(rig.peer.received.substr(rx), "5").empty(); });
        rig.settle();
        o.frames = scene.peer_frames_from(rx);
    }();
    o.hold_timed_out = store_hold::timed_out();
    scene.finish();
    o.setup_miss = o.setup_miss.empty() ? scene.setup_miss : o.setup_miss;
    o.stop_completed = scene.stop_completed;
    return o;
}

// The application frames the peer read after the first Logout, and whether a Logout was read.
struct WireReading {
    bool logout_read = false;
    std::vector<std::string> apps_after_logout;  // MsgSeqNum(34) of each
};
WireReading read_wire(std::vector<std::string> const& frames) {
    WireReading w;
    for (auto const& f : frames) {
        auto const type = plain_rig::field_value(f, "35");
        if (type == "5") w.logout_read = true;
        if (type == "D" && w.logout_read)
            w.apps_after_logout.push_back(plain_rig::field_value(f, "34"));
    }
    return w;
}

class SendDuringLogoutEngine103 : public ::testing::TestWithParam<Case> {};

}  // namespace

// FR-021 (i) and (ii): an Engine::send during the ending's Logout store call is refused with
// the too-high path's code, and the peer reads no application frame after the Logout.
TEST_P(SendDuringLogoutEngine103, SendDuringTheEndingsLogoutStore_RefusedNoAppFrameAfter) {
    auto const o = run(GetParam());
    ASSERT_TRUE(o.setup_miss.empty()) << o.setup_miss;
    ASSERT_TRUE(o.stop_completed) << "Engine::stop did not complete";
    if (GetParam().logon_write_late) {
        ASSERT_TRUE(o.logon_write_queued)
            << "the path was not exercised: the file I/O thread was not held at the arm site";
    }
    ASSERT_TRUE(o.hold_entered) << "the path was not exercised: no hold";
    EXPECT_FALSE(o.hold_timed_out) << "the hold reached its bound";
    auto const w = read_wire(o.frames);
    ASSERT_TRUE(w.logout_read) << "the path was not exercised: the peer read no Logout:\n"
                               << store_hold::printable(o.frames);
    EXPECT_TRUE(w.apps_after_logout.empty()) << "an application frame followed the Logout:\n"
                                             << store_hold::printable(o.frames);
    ASSERT_TRUE(o.send.has_value()) << "the send did not complete";
    EXPECT_FALSE(o.send->has_value()) << "the send was admitted";
    if (!o.send->has_value()) {
        EXPECT_EQ(o.send->error(), fixpp::core::error::session_invalid_state_for_send)
            << static_cast<int>(o.send->error());
    }
}

INSTANTIATE_TEST_SUITE_P(BothEndings, SendDuringLogoutEngine103,
                         ::testing::Values(Case{Ending::in_sequence_logout, false},
                                           Case{Ending::gap_deadline, false},
                                           Case{Ending::in_sequence_logout, true}),
                         [](auto const& info) {
                             if (info.param.logon_write_late) {
                                 return std::string{"InSequenceLogout_LogonWriteQueuedAtTheArm"};
                             }
                             return std::string{info.param.ending == Ending::gap_deadline
                                                    ? "GapDeadline"
                                                    : "InSequenceLogout"};
                         });
