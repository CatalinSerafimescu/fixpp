// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_538_resend_answer_store_failure_engine.cpp
//
// fixpp#538 (.specify/536-537-538-recovery-liveness.md §2.2, D-538-4, "Where this ending
// closes the transport, and where it does not"; §5, the table "#538"): a store read
// failure in a resend answer, on a session an Engine's read pump drives
// (plain_engine_rig.hpp). The store is a MemoryStore behind the recording wrapper
// (tests/support/recording_store.hpp), which returns store_io_failure for the retrieve
// of one stored application message.
//
//   in Active, each role            the pump's failed-delivery branch closes the
//                                   connection at once;
//   the Logon's 789 path,           the same: the Logon reply arrives through the pump;
//   initiator
//   the Logon's 789 path,           the first Logon is delivered by the accept loop,
//   acceptor                        which discards the result: the connection stays
//                                   open until the establishment deadline.
//
// The cells that hand the frame to a Session directly are in
// test_538_resend_answer_store_failure.cpp.
//
// A cell asserts nothing fatal while its engine runs, and stops the engine on every
// path: ~Engine requires a completed stop.

#include <gtest/gtest.h>

#include <algorithm>
#include <asio/co_spawn.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fixpp/core/error.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/memory_store_factory.hpp>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "session/plain_engine_rig.hpp"
#include "support/pump_until_ready.hpp"
#include "support/recording_store.hpp"

namespace {

namespace plain_rig = fixpp::test_support::plain_rig;
using fixpp::core::error;
using fixpp::session::direction_t;
using fixpp::session::fsm_state;
using fixpp::session::seqnum_t;
using fixpp::session::session_role;
using fixpp::test_support::RecordingStoreFactory;
using fixpp::test_support::SeededMemoryStoreFactory;
using fixpp::test_support::store_op;
using fixpp::test_support::StoreCall;
using fixpp::test_support::StoreCallLog;

// A cell's wait for the peer's connection to end is this fraction of the fixture's
// heartbeat interval: the heartbeat is the one timer of the fixture that could end the
// session, and it is on a mock clock nothing advances past it. On a tree where nothing
// closes the connection the wait runs out.
constexpr int kWaitDivisor = 10;

std::chrono::milliseconds wait_of(fixpp::session::SessionConfig const& c) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               c.heartbeat_interval.value_or(std::chrono::seconds{0})) /
           kWaitDivisor;
}

std::string record_text(std::optional<StoreCall> const& c) {
    if (!c) return "not called";
    if (!c->entered()) return "called, and its body never started";
    if (!c->returned()) return "entered, and never returned";
    return c->error ? "returned error " + std::to_string(static_cast<int>(*c->error))
                    : std::string{"returned a value"};
}

std::string ring_text(std::vector<fsm_state> const& ring) {
    std::string t;
    for (auto const st : ring) t += std::to_string(static_cast<int>(st)) + " ";
    return t.empty() ? std::string{"(empty)"} : t;
}

bool reached(std::vector<fsm_state> const& ring, fsm_state const st) {
    return std::ranges::find(ring, st) != ring.end();
}

// What a cell read. Nothing here is asserted while the engine runs.
struct Outcome {
    std::string setup_miss;  // empty when the cell drove its scene
    std::chrono::milliseconds wait{0};
    std::optional<StoreCall> failed_read;  // the slot's retrieve, from the wrapper's record
    bool peer_read_ended_within_the_wait = false;
    fsm_state state_at_the_wait_end = fsm_state::NotConnected;
    bool engine_stopped_at_the_wait_end = true;
    std::vector<fsm_state> ring;
    std::string peer_received;  // every byte the peer read after the cell's frame
};

std::function<bool(StoreCall const&)> is_slot_read(seqnum_t const slot) {
    return [slot](StoreCall const& call) {
        return call.op == store_op::retrieve && call.dir == direction_t::outbound &&
               call.first == slot;
    };
}

void read_session(plain_rig::Rig const& rig, Outcome& o) {
    if (auto const s = rig.session()) {
        o.state_at_the_wait_end = s->state();
        auto const ring = s->fsm_visit_history();
        o.ring.assign(ring.begin(), ring.end());
    }
    o.engine_stopped_at_the_wait_end = rig.engine->stopped();
}

// ── In Active ────────────────────────────────────────────────────────────────────────

// The session's own Logon is its number 1 on either role, so the application messages
// are stored from 2. The failed slot has a stored message on each side of it.
constexpr int kApps = 3;
constexpr seqnum_t kFirstApp = 2;
constexpr seqnum_t kFailedSlot = kFirstApp + 1;

Outcome run_in_active(session_role const role) {
    Outcome o;
    plain_rig::Rig rig;
    auto cfg = rig.cfg(role);
    o.wait = wait_of(cfg);
    auto const factory = std::make_shared<RecordingStoreFactory>(
        std::make_shared<fixpp::session::MemoryStoreFactory>(fixpp::session::MemoryStore::Config{
            .policy = fixpp::session::capacity_policy::unbounded}));
    auto const log = factory->log;
    cfg.store_factory = factory;
    auto const payload = plain_rig::to_bytes(
        "35=D\x01"
        "11=ORD001\x01"
        "54=1\x01"
        "55=AAPL\x01");
    [&] {
        if (!rig.start(cfg) || !rig.to_active()) {
            o.setup_miss = "the session did not reach Active";
            return;
        }
        for (int i = 0; i < kApps; ++i) {
            auto fut = asio::co_spawn(rig.ioc,
                                      rig.engine->send(rig.id, std::span<const std::byte>{payload}),
                                      asio::use_future);
            if (!rig.run_until([&] {
                    return fut.wait_for(std::chrono::seconds{0}) == std::future_status::ready;
                }) ||
                !fut.get().has_value()) {
                o.setup_miss = "an Engine::send of setup did not complete";
                return;
            }
        }
        if (!rig.run_until([&] {
                return plain_rig::frames_of_type(rig.peer.received, "D").size() >=
                       static_cast<std::size_t>(kApps);
            })) {
            o.setup_miss = "the peer did not read the application messages of setup";
            return;
        }
        log->arm();
        log->fail_in = is_slot_read(kFailedSlot);
        log->fail_code = error::store_io_failure;
        auto const mark = log->mark();
        auto const rx_mark = rig.peer.received.size();
        rig.peer.send(rig.msg("2", 2,
                              "7=" + std::to_string(kFirstApp) + "\x01" +
                                  "16=" + std::to_string(kFirstApp + kApps - 1) + "\x01"));
        o.peer_read_ended_within_the_wait =
            rig.run_until([&] { return rig.peer.read_ended; }, o.wait);
        read_session(rig, o);
        if (auto const* r = log->first_after(mark, is_slot_read(kFailedSlot))) o.failed_read = *r;
        o.peer_received = rig.peer.received.substr(rx_mark);
    }();
    rig.stop();
    return o;
}

void expect_connection_ended_in_active(Outcome const& o) {
    ASSERT_TRUE(o.setup_miss.empty()) << o.setup_miss;
    ASSERT_GT(o.wait.count(), 0) << "the fixture's heartbeat interval was not read";
    ASSERT_TRUE(o.failed_read.has_value() && o.failed_read->returned() &&
                o.failed_read->error == error::store_io_failure)
        << "the path was not exercised: the slot's read: " << record_text(o.failed_read);

    EXPECT_TRUE(o.peer_read_ended_within_the_wait)
        << "the peer's connection was still open when the wait ended";
    EXPECT_EQ(o.state_at_the_wait_end, fsm_state::Disconnected) << "the state when the wait ended";
    EXPECT_FALSE(o.engine_stopped_at_the_wait_end) << "the cell's own Engine::stop had begun";
    EXPECT_TRUE(plain_rig::frames_of_type(o.peer_received, "4").empty())
        << "the peer read a GapFill";
    EXPECT_TRUE(plain_rig::frames_of_type(o.peer_received, "5").empty())
        << "the peer read a Logout";
}

// D-538-4, "Under an Engine, a ResendRequest in Active, on each role".
TEST(ResendAnswerStoreFailureEngine, Acceptor_SlotReadFailsInActive_PeerConnectionEnds) {
    expect_connection_ended_in_active(run_in_active(session_role::acceptor));
}

TEST(ResendAnswerStoreFailureEngine, Initiator_SlotReadFailsInActive_PeerConnectionEnds) {
    expect_connection_ended_in_active(run_in_active(session_role::initiator));
}

// ── The Logon's NextExpectedMsgSeqNum(789) path ──────────────────────────────────────
//
// The session's store already holds three application messages, at 1 to 3, when the
// Logon exchange runs, so its own Logon is number 4. The peer's Logon carries 789=2:
// it asks for a replay from 2. The retrieve of 3 fails; 2 is replayed before it.

constexpr unsigned kSeeded = 3;
constexpr seqnum_t kPeerNextExpected = 2;
constexpr seqnum_t kFailed789Slot = 3;

// A RecordingStoreFactory over a store seeded with kSeeded application messages from the
// session, armed to fail the retrieve of kFailed789Slot.
std::shared_ptr<RecordingStoreFactory> seeded_failing_factory(plain_rig::Rig const& rig) {
    std::vector<std::string> frames;
    for (unsigned seq = 1; seq <= kSeeded; ++seq) {
        frames.push_back(plain_rig::message(rig.begin_string, "D", seq, "ISLD", "TW",
                                            rig.sending_time(),
                                            "11=ORD" + std::to_string(seq) + "\x01"));
    }
    auto factory = std::make_shared<RecordingStoreFactory>(
        std::make_shared<SeededMemoryStoreFactory>(std::move(frames)));
    factory->log->arm();
    factory->log->fail_in = is_slot_read(kFailed789Slot);
    factory->log->fail_code = error::store_io_failure;
    return factory;
}

std::string peer_logon_with_789(plain_rig::Rig const& rig) {
    return rig.msg("A", 1,
                   "98=0\x01"
                   "108=30\x01"
                   "789=" +
                       std::to_string(kPeerNextExpected) + "\x01");
}

// D-538-4, "the 789 path, initiator, under an Engine": the peer's Logon reply arrives
// through the read pump, so the failed frame closes the connection at once.
TEST(ResendAnswerStoreFailureEngine, Initiator789Path_SlotReadFails_PeerConnectionEnds) {
    Outcome o;
    {
        plain_rig::Rig rig;
        auto cfg = rig.cfg(session_role::initiator);
        cfg.enable_next_expected_msg_seq_num = true;
        o.wait = wait_of(cfg);
        auto const factory = seeded_failing_factory(rig);
        auto const log = factory->log;
        cfg.store_factory = factory;
        [&] {
            if (!rig.start(cfg) || !rig.run_until([&] {
                    return !plain_rig::frames_of_type(rig.peer.received, "A").empty() &&
                           rig.state() == fsm_state::LogonSent;
                })) {
                o.setup_miss = "the initiator's Logon";
                return;
            }
            auto const mark = log->mark();
            auto const rx_mark = rig.peer.received.size();
            rig.peer.send(peer_logon_with_789(rig));
            o.peer_read_ended_within_the_wait =
                rig.run_until([&] { return rig.peer.read_ended; }, o.wait);
            read_session(rig, o);
            if (auto const* r = log->first_after(mark, is_slot_read(kFailed789Slot))) {
                o.failed_read = *r;
            }
            o.peer_received = rig.peer.received.substr(rx_mark);
        }();
        rig.stop();
    }
    ASSERT_TRUE(o.setup_miss.empty()) << o.setup_miss;
    ASSERT_GT(o.wait.count(), 0) << "the fixture's heartbeat interval was not read";
    ASSERT_TRUE(o.failed_read.has_value() && o.failed_read->returned() &&
                o.failed_read->error == error::store_io_failure)
        << "the path was not exercised: the slot's read: " << record_text(o.failed_read);

    EXPECT_EQ(o.state_at_the_wait_end, fsm_state::Disconnected) << "the state when the wait ended";
    EXPECT_FALSE(reached(o.ring, fsm_state::Active))
        << "the session reached Active; ring: " << ring_text(o.ring);
    EXPECT_TRUE(o.peer_read_ended_within_the_wait)
        << "the peer's connection was still open when the wait ended";
    EXPECT_FALSE(o.engine_stopped_at_the_wait_end) << "the cell's own Engine::stop had begun";
    EXPECT_TRUE(plain_rig::frames_of_type(o.peer_received, "4").empty())
        << "the peer read a GapFill";
}

// D-538-4, "the 789 path, acceptor, under an Engine". The accept loop delivers the
// first Logon itself and discards the result, and the 789 path runs inside that
// delivery; the read pump then starts on a session that is Disconnected and never
// reached Active, and closes it at the establishment deadline.
TEST(ResendAnswerStoreFailureEngine,
     Acceptor789Path_SlotReadFails_ConnectionStaysOpenUntilTheEstablishmentDeadline) {
    constexpr std::uint32_t kLogonTimeoutMs = 2000;
    Outcome o;
    fsm_state state_at_the_abort = fsm_state::NotConnected;
#if defined(FIXPP_TEST_PEER_HANG_UP_READING)
    bool hang_up_at_the_abort = true;
#endif
    bool read_ended_before_the_deadline = true;
    bool read_ended_at_the_deadline = false;
    {
        plain_rig::Rig rig;
        auto cfg = rig.cfg(session_role::acceptor);
        cfg.enable_next_expected_msg_seq_num = true;
        cfg.logon_timeout_ms = kLogonTimeoutMs;
        o.wait = wait_of(cfg);
        auto const factory = seeded_failing_factory(rig);
        auto const log = factory->log;
        cfg.store_factory = factory;
        [&] {
            if (!rig.start(cfg) || !rig.connect_peer()) {
                o.setup_miss = "the rig did not start and connect its peer";
                return;
            }
            auto const mark = log->mark();
            auto const rx_mark = rig.peer.received.size();
            rig.peer.send(peer_logon_with_789(rig));
            // One handler at a time, so that the readings below are taken after the
            // handler that ran the first Logon's delivery and before any other.
            auto settled = [&] {
                auto const st = rig.state();
                return st == fsm_state::Disconnected || st == fsm_state::Active;
            };
            auto const budget_end =
                std::chrono::steady_clock::now() + fixpp::test_support::kPumpBudget;
            while (!settled() && std::chrono::steady_clock::now() < budget_end) {
                (void)rig.ioc.run_one_for(fixpp::test_support::kPumpSlice);
                rig.ioc.restart();
            }
            state_at_the_abort = rig.state();
#if defined(FIXPP_TEST_PEER_HANG_UP_READING)
            hang_up_at_the_abort = plain_rig::peer_sees_hang_up(rig.peer);
#endif
            // The pump has started and waits; nothing has moved the engine clock.
            rig.settle();
            read_ended_before_the_deadline = rig.peer.read_ended;
            // The establishment deadline is on the engine clock, the mock clock.
            rig.clock->advance(std::chrono::milliseconds{kLogonTimeoutMs});
            read_ended_at_the_deadline = rig.run_until([&] { return rig.peer.read_ended; }, o.wait);
            read_session(rig, o);
            if (auto const* r = log->first_after(mark, is_slot_read(kFailed789Slot))) {
                o.failed_read = *r;
            }
            o.peer_received = rig.peer.received.substr(rx_mark);
        }();
        rig.stop();
    }
    ASSERT_TRUE(o.setup_miss.empty()) << o.setup_miss;
    ASSERT_GT(o.wait.count(), 0) << "the fixture's heartbeat interval was not read";
    ASSERT_TRUE(o.failed_read.has_value() && o.failed_read->returned() &&
                o.failed_read->error == error::store_io_failure)
        << "the path was not exercised: the slot's read: " << record_text(o.failed_read);

    EXPECT_EQ(state_at_the_abort, fsm_state::Disconnected)
        << "the state when the first Logon's delivery has returned";
    EXPECT_FALSE(reached(o.ring, fsm_state::Active))
        << "the session reached Active; ring: " << ring_text(o.ring);
#if defined(FIXPP_TEST_PEER_HANG_UP_READING)
    // fixpp#534 flips this assertion, if its change reaches this path: a session that
    // is Disconnected then closes its transport before the establishment deadline.
    EXPECT_FALSE(hang_up_at_the_abort)
        << "the peer saw a hang-up when the first Logon's delivery had returned";
#else
    std::printf("the reading \"the peer sees a hang-up\" was not made on this platform\n");
#endif
    EXPECT_FALSE(read_ended_before_the_deadline)
        << "the peer's connection ended before the engine clock reached the deadline";
    EXPECT_TRUE(read_ended_at_the_deadline)
        << "the peer's connection was still open after the establishment deadline";
    EXPECT_TRUE(plain_rig::frames_of_type(o.peer_received, "4").empty())
        << "the peer read a GapFill";
}

}  // namespace
