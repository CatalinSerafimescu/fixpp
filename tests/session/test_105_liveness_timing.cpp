// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_105_liveness_timing.cpp — 105-transport-liveness: R-1, the unanswered
// TestRequest (tasks.md T023; contract C-3; research D3).
//
// Every cell runs with HeartBtInt 10 s and test_request_threshold = HeartBtInt (tasks.md,
// Execution rules, "Liveness instants"), so the TestRequest and the give-up instants are the
// same with and without 105's timing change. The TestRequest is read by test_105_support.hpp's
// wire reader; the give-up is reached by stepping the mock clock, then the clock is held still
// while the close is awaited. The wall-clock timers: none (plain rig, mock transport).
//
// No ASSERT_* runs between a rig's start() and its stop() (tasks.md, Execution rules).
#include <gtest/gtest.h>

#include <array>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/error.hpp>
#include <fixpp/session/admin_messages.hpp>
#include <fixpp/session/application.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "session/mock_reconnect_factory.hpp"
#include "session/plain_engine_rig.hpp"
#include "session/resend_gap_scene.hpp"
#include "session/test_105_support.hpp"
#include "support/seqnum_manager_test_access.hpp"
#include "support/session_executor_of.hpp"
#include "support/session_test_access.hpp"

namespace {

using namespace std::chrono_literals;
using fixpp::session::fsm_state;
using fixpp::session::seqnum_manager_test_access;
using fixpp::session::seqnum_t;
using fixpp::session::session_role;
using fixpp::session::session_test_access;
using fixpp::session::SessionConfig;
namespace pr = fixpp::test_support::plain_rig;
namespace rg = fixpp::test_support::resend_gap;
namespace t105 = fixpp::test_support::t105;

constexpr std::chrono::milliseconds kWallCap{1500};
constexpr std::chrono::seconds kHb{10};
constexpr char kGiveUpFormat[] =
    "session ended: TestRequest unanswered: test_req_id={} threshold_ms={} error={}";

class LivenessApp final : public fixpp::session::Application {
public:
    int logouts = 0;
    std::string throw_to_admin_on;
    void onLogout(const fixpp::session::SessionId& /*id*/) override { ++logouts; }
    void toAdmin(const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& msg,
                 const fixpp::session::SessionId& /*id*/) override {
        if (!throw_to_admin_on.empty() && msg.msg_type() == throw_to_admin_on) {
            throw std::runtime_error("LivenessApp: toAdmin");
        }
    }
};

// A store factory over test_105_support.hpp's GateStore that reports the store volatile.
class VolatileGateStoreFactory final : public fixpp::session::MessageStoreFactory {
public:
    explicit VolatileGateStoreFactory(std::shared_ptr<t105::gate_store_state> st)
        : st_{std::move(st)} {}
    [[nodiscard]] bool yields_persistent_store() const noexcept override { return false; }
    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<fixpp::session::MessageStore>> make(
        std::string_view, std::string_view, std::pmr::memory_resource*, std::size_t,
        asio::any_io_executor) noexcept override {
        return fixpp::core::expected_t<std::unique_ptr<fixpp::session::MessageStore>>{
            std::make_unique<t105::GateStore>(st_)};
    }

private:
    std::shared_ptr<t105::gate_store_state> st_;
};

struct GiveUpReading {
    bool active = false;
    std::string tr_id;   // TestReqID(112) of the TestRequest the session sent
    bool ended = false;  // the session left Active at the give-up
    bool eof = false;    // EOF at once after it
    std::string after;   // what the peer read after the TestRequest
    std::vector<fixpp::log::Record> records;
};

// One plaintext Engine session (tests/session/plain_engine_rig.hpp) through the give-up.
// arm(rig) runs after the TestRequest is read and before the give-up instant.
GiveUpReading run_give_up(session_role role, std::function<void(SessionConfig&)> const& tweak,
                          std::function<void(pr::Rig&, LivenessApp&)> const& arm,
                          std::function<void(pr::Rig&)> const& to_active, const char* site) {
    auto app = std::make_shared<LivenessApp>();
    pr::Rig rig{app};
    t105::LogCapture log;
    auto cfg = rig.cfg(role);
    cfg.heartbeat_interval = kHb;
    cfg.test_request_threshold = std::chrono::milliseconds{kHb};
    cfg.logger_override = log.logger;
    if (tweak) tweak(cfg);
    GiveUpReading r;
    bool up = rig.start(std::move(cfg));
    if (up) {
        if (to_active) {
            to_active(rig);
            up = rig.state() == fsm_state::Active;
        } else {
            up = rig.to_active();
        }
    }
    r.active = up;
    if (up) {
        auto const tr = t105::advance_until_msg_type(
            *rig.clock, rig.ioc, [&] { return rig.peer.received; }, "1", 0, 1s,
            t105::liveness_wire_cap(kHb, 1s), site);
        if (tr) {
            r.tr_id = pr::field_value(tr->frame, "112");
            auto const mark = rig.peer.received.size();
            if (arm) arm(rig, *app);
            std::chrono::nanoseconds advanced{0};
            while (rig.state() == fsm_state::Active &&
                   advanced < t105::liveness_wire_cap(kHb, 1s)) {
                rig.clock->advance(1s);
                advanced += 1s;
                (void)t105::drive_bounded(rig.ioc, 64);
            }
            r.ended = rig.state() != fsm_state::Active;
            r.eof = t105::pump_until_peer_read_ends(
                        rig.ioc, [&] { return rig.peer.read_ended; }, kWallCap, site) &&
                    t105::ending_of(rig.peer) == t105::peer_ending::eof;
            r.after = rig.peer.received.substr(mark);
        }
    }
    rig.stop();
    r.records = log.records_of(FIXPP_FORMAT_ID(kGiveUpFormat));
    return r;
}

std::string record_test_req_id(fixpp::log::Record const& rec) {
    auto const& a = rec.args[0];
    if (a.kind == fixpp::log::ArgValue::Kind::inline_str) return std::string{a.inl.data, a.inl.len};
    if (a.kind == fixpp::log::ArgValue::Kind::static_str && a.static_ptr) return a.static_ptr;
    return {};
}

// The record of C-3.3: one warn record carrying the TestReqID, the threshold in ms and the value
// of error::session_test_request_unanswered.
void expect_give_up_record(GiveUpReading const& r, const char* site) {
    ASSERT_EQ(r.records.size(), 1U) << site << ": the give-up record";
    auto const& rec = r.records.front();
    EXPECT_EQ(rec.level, fixpp::log::Level::warn) << site;
    EXPECT_EQ(record_test_req_id(rec), r.tr_id) << site << ": test_req_id";
    EXPECT_EQ(rec.args[1].u64, 10000U) << site << ": threshold_ms";
    EXPECT_EQ(rec.args[2].u64,
              static_cast<std::uint64_t>(fixpp::core::error::session_test_request_unanswered))
        << site << ": error";
}

class LivenessTiming105 : public ::testing::TestWithParam<session_role> {};

// C-3.1: exactly one Logout, its Text(58) the pinned string with the TestReqID the session
// sent, then EOF with the clock still and no Logout from the peer.
TEST_P(LivenessTiming105, GiveUp_LogoutTextPinned_ThenEof) {
    auto const r = run_give_up(GetParam(), nullptr, nullptr, nullptr, "GiveUp_LogoutTextPinned");
    ASSERT_TRUE(r.active) << "setup";
    ASSERT_FALSE(r.tr_id.empty()) << "setup: no TestRequest was read";
    EXPECT_TRUE(r.ended) << "the session was not given up";
    auto const logouts = pr::frames_of_type(r.after, "5");
    ASSERT_EQ(logouts.size(), 1U) << "the Logout of the give-up: " << r.after;
    EXPECT_EQ(pr::field_value(logouts.front(), "58"),
              "TestRequest unanswered, TestReqID " + r.tr_id);
    EXPECT_TRUE(r.eof) << "the peer's read did not end in EOF after the Logout";
}

// C-3.3: one warn record of slot 74, written at the give-up.
TEST_P(LivenessTiming105, GiveUp_OneLogRecordWithSlot74) {
    auto const r = run_give_up(GetParam(), nullptr, nullptr, nullptr, "GiveUp_OneLogRecord");
    ASSERT_TRUE(r.active) << "setup";
    EXPECT_TRUE(r.ended) << "the session was not given up";
    expect_give_up_record(r, "GiveUp_OneLogRecord");
}

// C-3.2: the Logout cannot be sent; the record and EOF all the same, and no complete Logout.
void expect_still_eof(GiveUpReading const& r, const char* site, bool logout_on_wire = false) {
    ASSERT_TRUE(r.active) << site << ": setup";
    ASSERT_FALSE(r.tr_id.empty()) << site << ": setup, no TestRequest was read";
    EXPECT_TRUE(r.ended) << site << ": the session was not given up";
    expect_give_up_record(r, site);
    EXPECT_EQ(pr::frames_of_type(r.after, "5").size(), logout_on_wire ? 1U : 0U)
        << site << ": the peer read: " << r.after;
    EXPECT_TRUE(r.eof) << site << ": the peer's read did not end in EOF";
}

TEST_P(LivenessTiming105, GiveUp_ToAdminThrows_StillEof) {
    auto const r = run_give_up(
        GetParam(), nullptr, [](pr::Rig&, LivenessApp& a) { a.throw_to_admin_on = "5"; }, nullptr,
        "GiveUp_ToAdminThrows");
    expect_still_eof(r, "GiveUp_ToAdminThrows");
}

TEST_P(LivenessTiming105, GiveUp_AssignFails_StillEof) {
    auto const r = run_give_up(
        GetParam(), nullptr,
        [](pr::Rig& rig, LivenessApp&) {
            auto s = rig.session();
            if (!s) return;
            auto& mgr = session_test_access::seqnum_mgr(*s);
            seqnum_manager_test_access::set_counters(mgr, mgr.next_inbound_unsafe(),
                                                     std::numeric_limits<seqnum_t>::max());
        },
        nullptr, "GiveUp_AssignFails");
    expect_still_eof(r, "GiveUp_AssignFails");
}

TEST_P(LivenessTiming105, GiveUp_StoreFailsPersistent_StillEof) {
    auto st = std::make_shared<t105::gate_store_state>();
    auto const r = run_give_up(
        GetParam(),
        [st](SessionConfig& c) { c.store_factory = std::make_shared<t105::GateStoreFactory>(st); },
        [st](pr::Rig&, LivenessApp&) {
            st->fail_when = [](t105::store_call k, seqnum_t) {
                return k == t105::store_call::store_outbound;
            };
        },
        nullptr, "GiveUp_StoreFailsPersistent");
    expect_still_eof(r, "GiveUp_StoreFailsPersistent");
    EXPECT_GE(st->calls_failed, 1U) << "setup: the Logout's store call did not fail";
}

// On a volatile store the failure is logged and the write proceeds: the Logout is on the wire.
TEST_P(LivenessTiming105, GiveUp_StoreFailsVolatile_StillEof) {
    auto st = std::make_shared<t105::gate_store_state>();
    auto const r = run_give_up(
        GetParam(),
        [st](SessionConfig& c) {
            c.store_factory = std::make_shared<VolatileGateStoreFactory>(st);
        },
        [st](pr::Rig&, LivenessApp&) {
            st->fail_when = [](t105::store_call k, seqnum_t) {
                return k == t105::store_call::store_outbound;
            };
        },
        nullptr, "GiveUp_StoreFailsVolatile");
    expect_still_eof(r, "GiveUp_StoreFailsVolatile", /*logout_on_wire=*/true);
    EXPECT_GE(st->calls_failed, 1U) << "setup: the Logout's store call did not fail";
}

// The Logout does not fit inbound_logout_leaf_'s buffer: the peer's CompID is long enough that
// the give-up's Logout overflows it while the Logons and the TestRequest fit.
namespace long_ids {
constexpr std::size_t kLeafBuffer = 256;
std::size_t logout_size(std::string const& peer, std::string const& st) {
    std::array<std::byte, 4096> big{};
    auto const r = fixpp::session::build_logout(
        big, 4, "ISLD", peer, "TestRequest unanswered, TestReqID TR1", "FIX.4.2", st);
    return r ? r->size() : 0U;
}
std::size_t test_request_size(std::string const& peer, std::string const& st) {
    std::array<std::byte, 4096> big{};
    auto const r = fixpp::session::build_test_request(big, 3, "ISLD", peer, "TR1", "FIX.4.2", st);
    return r ? r->size() : 0U;
}
std::size_t overflowing_length(std::string const& st) {
    for (std::size_t len = 1; len < kLeafBuffer; ++len) {
        std::string const p(len, 'P');
        if (logout_size(p, st) > kLeafBuffer) {
            return test_request_size(p, st) <= kLeafBuffer ? len : 0U;
        }
    }
    return 0U;
}
}  // namespace long_ids

TEST_P(LivenessTiming105, GiveUp_LogoutDoesNotFit_StillEof) {
    std::string const st0 = [] {
        pr::Rig probe;
        return probe.sending_time();
    }();
    std::size_t const len = long_ids::overflowing_length(st0);
    ASSERT_NE(len, 0U) << "setup: no CompID length overflows the Logout and fits the TestRequest";
    std::string const peer(len, 'P');
    auto const r = run_give_up(
        GetParam(), [&](SessionConfig& c) { c.target_comp_id = peer; }, nullptr,
        [&](pr::Rig& rig) {
            bool up = true;
            if (rig.role_is_initiator()) {
                up = rig.run_until(
                    [&] { return !pr::frames_of_type(rig.peer.received, "A").empty(); });
            } else {
                up = rig.connect_peer();
            }
            if (!up) return;
            rig.peer.send(pr::message(rig.begin_string, "A", 1, peer, "ISLD", rig.sending_time(),
                                      "98=0\x01"
                                      "108=10\x01"));
            (void)rig.run_until([&] { return rig.state() == fsm_state::Active; });
        },
        "GiveUp_LogoutDoesNotFit");
    expect_still_eof(r, "GiveUp_LogoutDoesNotFit");
}

FIXPP_105_BOTH_ROLES(LivenessTiming105);

// The Logout's write fails: on a direct initiator whose connection is a mock transport that
// writes only part of any frame longer than the TestRequest (Script::partial_write_bytes), so
// live_write_serialized_ returns dispatch_aborted for the Logout and the leaf discards it. The
// peer-visible close is the transport's close (Script::closed_flag).
TEST(LivenessTiming105Direct, GiveUp_WriteFails_StillEof) {
    t105::LogCapture log;
    auto app = std::make_shared<rg::GapApp>();
    rg::Scene s{rg::Options{.heartbeat = kHb, .app = app}};
    auto factory = std::make_shared<fixpp::test_support::MockReconnectFactory>();
    auto closed = std::make_shared<std::atomic<bool>>(false);
    std::size_t partial = 0;
    {
        std::array<std::byte, 4096> big{};
        auto const lo = fixpp::session::build_logout(big, 3, "ISLD", "TW",
                                                     "TestRequest unanswered, TestReqID TR1",
                                                     "FIX.4.2", s.sending_time());
        partial = lo ? lo->size() - 1 : 0;
    }
    ASSERT_GT(partial, 0U) << "setup";
    factory->tweak_script = [closed, partial](fixpp::transport::test::Script& sc) {
        sc.closed_flag = closed;
        sc.partial_write_bytes = partial;
    };
    s.cfg.transport_factory_override = factory;
    s.cfg.reconnect_endpoint = fixpp::transport::Endpoint{"127.0.0.1", 19099};
    s.cfg.test_request_threshold = std::chrono::milliseconds{kHb};
    s.cfg.logger_override = log.logger;
    s.session = std::make_unique<fixpp::session::Session>(s.engine, s.cfg);
    auto run = [&](auto a) {
        auto fut = asio::co_spawn(fixpp::test_support::session_executor_of(*s.session, s.ioc),
                                  std::move(a), asio::use_future);
        (void)fixpp::test_support::pump_until(
            s.ioc, [&] { return rg::Scene::ready(fut); }, fixpp::test_support::kPumpBudget,
            fixpp::test_support::kPumpSlice, "GiveUp_WriteFails");
        bool const ok = rg::Scene::ready(fut) && fut.get().has_value();
        s.drain();
        return ok;
    };
    ASSERT_TRUE(run(s.session->open())) << "setup: open()";
    ASSERT_TRUE(run(s.session->drive_reconnect())) << "setup: drive_reconnect()";
    (void)s.feed(s.frame("A", 1,
                         "98=0\x01"
                         "108=10\x01"));
    ASSERT_EQ(s.state(), fsm_state::Active) << "setup";
    auto wire = [&] {
        auto const b = factory->last_transport->outbound_bytes_seen();
        return std::string{reinterpret_cast<const char*>(b.data()), b.size()};
    };
    std::size_t steps = 0;
    while (pr::frames_of_type(wire(), "1").empty() && steps++ < 30) s.pass(1s);
    auto const trs = pr::frames_of_type(wire(), "1");
    ASSERT_EQ(trs.size(), 1U) << "setup: no TestRequest";
    std::string const tr_id = pr::field_value(trs.front(), "112");
    ASSERT_LT(trs.front().size(), partial + 1) << "setup: the TestRequest would be cut too";
    auto const mark = wire().size();
    steps = 0;
    while (s.state() == fsm_state::Active && steps++ < 30) s.pass(1s);
    EXPECT_NE(s.state(), fsm_state::Active) << "the session was not given up";
    EXPECT_TRUE(pr::frames_of_type(wire().substr(mark), "5").empty())
        << "a complete Logout was written";
    EXPECT_TRUE(closed->load()) << "the connection is still open after the give-up";
    auto const records = log.records_of(FIXPP_FORMAT_ID(kGiveUpFormat));
    ASSERT_EQ(records.size(), 1U) << "the give-up record";
    EXPECT_EQ(record_test_req_id(records.front()), tr_id);
}

}  // namespace
