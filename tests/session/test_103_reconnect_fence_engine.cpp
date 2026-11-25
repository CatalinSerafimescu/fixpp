// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_103_reconnect_fence_engine.cpp — test-only.
//
// Feature 103 (specs/103-resend-logon-reset-fidelity/), fixpp#578, cell G10 (research D1 step
// 1a, the reader fence; tasks T121). A direct Session::drive_reconnect() on a Session obtained
// through Engine::lookup, while the Engine's read pump is parked in async_read_some on the
// installed transport, is refused with session_already_open: nothing changes, nothing is
// connected, and the pump keeps reading. A closed Session is refused with
// session_already_closed first; so is the post-acknowledgement Disconnected arm, whose
// connection 105-transport-liveness (FR-005) closes at the peer's Logout.
//
// Each cell runs one session under an Engine on the plain transport, facing a raw peer, on
// plain_engine_rig.hpp's Rig (a mock engine clock). A cell asserts nothing fatal while the
// engine runs, and stops it at its end.
#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/use_future.hpp>
#include <cstdint>
#include <fixpp/core/error.hpp>
#include <fixpp/session/memory_store_factory.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <system_error>

#include "session/plain_engine_rig.hpp"
#include "support/recording_store.hpp"
#include "support/session_executor_of.hpp"

namespace {

namespace plain_rig = fixpp::test_support::plain_rig;
using fixpp::core::error;
using fixpp::session::fsm_state;
using fixpp::session::session_role;

// The outcome of one drive_reconnect on the looked-up Session.
struct Attempt {
    bool returned = false;
    std::optional<fixpp::core::expected_t<void>> result;
};

Attempt drive_reconnect_on(plain_rig::Rig& rig, fixpp::session::Session& s) {
    Attempt a;
    auto fut = asio::co_spawn(fixpp::test_support::session_executor_of(s, rig.ioc),
                              s.drive_reconnect(), asio::use_future);
    a.returned = rig.run_until(
        [&] { return fut.wait_for(std::chrono::seconds{0}) == std::future_status::ready; });
    if (a.returned) a.result = fut.get();
    return a;
}

[[nodiscard]] bool refused_with(Attempt const& a, error const code) {
    return a.result.has_value() && !a.result->has_value() && a.result->error() == code;
}
std::string text(Attempt const& a) {
    if (!a.returned) return "did not return";
    if (!a.result) return "no result";
    return a.result->has_value()
               ? std::string{"succeeded"}
               : "returned " + std::to_string(static_cast<int>(a.result->error()));
}

// The pump still reads the installed transport: a TestRequest from the peer is answered.
[[nodiscard]] bool pump_still_reads(plain_rig::Rig& rig, std::uint32_t seq) {
    auto const before = plain_rig::frames_of_type(rig.peer.received, "0").size();
    rig.peer.send(rig.msg("1", seq, "112=FENCE\x01"));
    return rig.run_until(
        [&] { return plain_rig::frames_of_type(rig.peer.received, "0").size() > before; });
}

enum class Arm : std::uint8_t {
    initiator_active,        // (i)
    initiator_disconnected,  // (ii) a post-acknowledgement Disconnected (105: closed at once)
    acceptor_before_logon,   // (iii) published, its first Logon's reply held in its store
    acceptor_active          // (iv)
};

class ReconnectFence103 : public ::testing::TestWithParam<Arm> {};

}  // namespace

TEST_P(ReconnectFence103, G10_DirectReconnectWithThePumpParked_AlreadyOpenNothingChanges) {
    Arm const arm = GetParam();
    bool const initiator = arm == Arm::initiator_active || arm == Arm::initiator_disconnected;
    plain_rig::Rig rig;
    auto c = rig.cfg(initiator ? session_role::initiator : session_role::acceptor);
    // (iii): the Engine reads an acceptor's first frame before it publishes the Session, so
    // "published, before its first Logon has taken it to Active" is held here inside that
    // Logon's processing, at the store call of its Logon reply.
    std::shared_ptr<fixpp::test_support::StoreCallLog> log;
    if (arm == Arm::acceptor_before_logon) {
        auto const f = std::make_shared<fixpp::test_support::RecordingStoreFactory>(
            std::make_shared<fixpp::session::MemoryStoreFactory>(
                fixpp::session::MemoryStore::Config{
                    .policy = fixpp::session::capacity_policy::unbounded}));
        log = f->log;
        log->arm();
        log->hold_in = [](fixpp::test_support::StoreCall const& sc) {
            return sc.op == fixpp::test_support::store_op::store &&
                   sc.dir == fixpp::session::direction_t::outbound;
        };
        c.store_factory = f;
    }
    std::optional<fsm_state> state_before;
    void const* transport_before = nullptr;
    Attempt a;
    bool second_connection = false;
    bool still_reads = true;
    bool peer_read_ended = false;
    std::optional<fsm_state> state_after;
    std::string setup_miss;
    asio::ip::tcp::socket second{rig.ioc};
    [&] {
        if (!rig.start(std::move(c))) {
            setup_miss = "rig.start";
            return;
        }
        if (arm == Arm::acceptor_before_logon) {
            if (!rig.connect_peer()) {
                setup_miss = "connect_peer";
                return;
            }
            rig.peer.send(rig.logon(1));
            if (!rig.run_until([&] { return log->holding(); })) {
                setup_miss = "the first Logon's reply store was not held";
                return;
            }
        } else if (!rig.to_active()) {
            setup_miss = "to_active";
            return;
        }
        if (initiator) {
            // A second connection the fixture would accept, were one made.
            rig.listener->async_accept(second, [&](std::error_code const& ec) {
                if (!ec) second_connection = true;
            });
        }
        std::uint32_t seq = 2;
        if (arm == Arm::initiator_disconnected) {
            rig.peer.send(rig.msg("5", seq++));
            if (!rig.run_until([&] { return rig.state() == fsm_state::Disconnected; })) {
                setup_miss = "the peer's Logout did not end the session";
                return;
            }
        }
        // The Engine publishes the session once its transport is installed or attached.
        if (!rig.run_until([&] { return rig.session() != nullptr; })) {
            setup_miss = "the session is not published";
            return;
        }
        auto const sess = rig.session();
        state_before = sess->state();
        transport_before = &sess->live_transport();
        a = drive_reconnect_on(rig, *sess);
        rig.settle();
        state_after = sess->state();
        if (arm == Arm::acceptor_before_logon) {
            log->release_hold();
            still_reads = rig.run_until([&] { return rig.state() == fsm_state::Active; }) &&
                          pump_still_reads(rig, 2);
        } else if (arm != Arm::initiator_disconnected) {
            still_reads = pump_still_reads(rig, seq);
        }
        peer_read_ended = rig.peer.read_ended;
        if (transport_before != nullptr) {
            EXPECT_EQ(&sess->live_transport(), transport_before)
                << "the installed transport was replaced";
        }
    }();
    rig.stop();
    ASSERT_TRUE(setup_miss.empty()) << "setup: " << setup_miss;
    EXPECT_EQ(state_after, state_before) << "a transition was recorded";
    EXPECT_FALSE(second_connection) << "a connect was attempted";
    EXPECT_TRUE(still_reads) << "the pump no longer reads the installed transport";
    if (arm == Arm::initiator_disconnected) {
        // 105-transport-liveness (FR-005, FR-001; research D1) supersedes arm (ii)'s "the
        // transport open": the peer's Logout in Active draws the reply and the connection
        // closes, so the pump closes the session and drive_reconnect finds it closed
        // (B-103-9's order: a closing or closed Session returns session_already_closed).
        EXPECT_TRUE(refused_with(a, error::session_already_closed))
            << "drive_reconnect " << text(a);
        EXPECT_TRUE(peer_read_ended) << "the ended connection was not closed";
    } else {
        EXPECT_TRUE(refused_with(a, error::session_already_open)) << "drive_reconnect " << text(a);
        EXPECT_FALSE(peer_read_ended) << "the installed transport was closed";
    }
}

INSTANTIATE_TEST_SUITE_P(EveryArm, ReconnectFence103,
                         ::testing::Values(Arm::initiator_active, Arm::initiator_disconnected,
                                           Arm::acceptor_before_logon, Arm::acceptor_active),
                         [](auto const& info) {
                             switch (info.param) {
                                 case Arm::initiator_active:
                                     return std::string{"InitiatorActive"};
                                 case Arm::initiator_disconnected:
                                     return std::string{"InitiatorDisconnected"};
                                 case Arm::acceptor_before_logon:
                                     return std::string{"AcceptorBeforeLogon"};
                                 case Arm::acceptor_active:
                                     return std::string{"AcceptorActive"};
                             }
                             return std::string{"?"};
                         });

// Terminal twins: after close(terminal) and the pump's exit, the retained Session is
// refused with session_already_closed.
class ReconnectFenceClosed103 : public ::testing::TestWithParam<session_role> {};

TEST_P(ReconnectFenceClosed103, G10_ClosedSession_AlreadyClosed) {
    plain_rig::Rig rig;
    std::string setup_miss;
    Attempt a;
    std::shared_ptr<fixpp::session::Session> sess;
    [&] {
        if (!rig.start(rig.cfg(GetParam()))) {
            setup_miss = "rig.start";
            return;
        }
        if (!rig.to_active()) {
            setup_miss = "to_active";
            return;
        }
        sess = rig.session();
        if (!sess) {
            setup_miss = "the session is not published";
            return;
        }
        auto close_fut =
            asio::co_spawn(fixpp::test_support::session_executor_of(*sess, rig.ioc),
                           sess->close(fixpp::session::close_mode::terminal), asio::use_future);
        if (!rig.run_until([&] {
                return close_fut.wait_for(std::chrono::seconds{0}) == std::future_status::ready &&
                       rig.peer.read_ended;
            })) {
            setup_miss = "close(terminal) and the pump's exit";
            return;
        }
        a = drive_reconnect_on(rig, *sess);
    }();
    rig.stop();
    ASSERT_TRUE(setup_miss.empty()) << "setup: " << setup_miss;
    EXPECT_TRUE(refused_with(a, error::session_already_closed)) << "drive_reconnect " << text(a);
}

INSTANTIATE_TEST_SUITE_P(BothRoles, ReconnectFenceClosed103,
                         ::testing::Values(session_role::initiator, session_role::acceptor),
                         [](auto const& info) {
                             return std::string{info.param == session_role::initiator ? "Initiator"
                                                                                      : "Acceptor"};
                         });
