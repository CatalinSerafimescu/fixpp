// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_105_liveness_timing.cpp — 105-transport-liveness: R-1, the unanswered
// TestRequest (tasks.md T023; contract C-3; research D3), and User Story 3, the TestRequest
// timing and test_request_threshold (tasks.md T089 to T093; contract C-6; research D6).
//
// Every R-1 cell runs with HeartBtInt 10 s and test_request_threshold = HeartBtInt (tasks.md,
// Execution rules, "Liveness instants"), so the TestRequest and the give-up instants are the
// same with and without 105's timing change; the User Story 3 section below states its own
// configuration. The TestRequest is read by test_105_support.hpp's wire reader; the give-up is
// reached by stepping the mock clock, then the clock is held still while the close is awaited.
// The wall-clock timers: none (plain rig, mock transport).
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
#include <set>
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
                // Staging (ci/mock-clock-staging-sweep.sh): KIND D, the give-up instant is computed
                // from stored anchors (the last inbound frame, the TestRequest's write), so a sleep
                // armed late names an instant already passed; each step is followed by a bounded
                // drive, and the loop's cap ends in the reading's assertions.
                rig.clock->advance(1s);
                advanced += 1s;
                (void)t105::drive_bounded(rig.ioc, 64);
            }
            r.ended = rig.state() != fsm_state::Active;
            r.eof = t105::pump_until_peer_read_ends(
                        rig.ioc, [&] { return rig.peer.read_ended; }, kWallCap, site) &&
                    t105::ended_in_eof(rig.peer, site);
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

// ════════════════════════════════════════════════════════════════════════════════════════
// User Story 3 (tasks.md T089 to T091, T093; research D6; data-model E-4; contract C-6):
// TestRequest at T of inbound silence (T = test_request_threshold, default 1.2 × HeartBtInt),
// the give-up strictly after 2T of inbound silence, one sleep per iteration. HeartBtInt is
// 10 s; the threshold is unset unless the cell names it. Instants are mock-clock offsets from
// the instant the session reached Active: the run advances the clock to each probe instant
// (every whole second up to the cell's end, and the extra instants the cell names), drives
// the io_context boundedly there, runs the peer's actions due at that instant, drives again,
// and stamps every frame the peer then holds with that instant.
// ════════════════════════════════════════════════════════════════════════════════════════

using ns = std::chrono::nanoseconds;

struct TimedFrame {
    std::string type;
    ns at{0};
    std::string frame;
};

struct Timeline {
    bool active = false;
    std::vector<TimedFrame> out;                  // the session's frames after Active
    std::vector<std::pair<ns, fsm_state>> state;  // the state read at each probe instant
    std::optional<ns> ended_at;                   // the first probe at which it left Active
    bool eof = false;                             // EOF at once after it left Active

    [[nodiscard]] std::vector<TimedFrame> of(std::string_view type) const {
        std::vector<TimedFrame> r;
        for (auto const& f : out) {
            if (f.type == type) r.push_back(f);
        }
        return r;
    }
    [[nodiscard]] std::size_t count(std::string_view type, ns from, ns to) const {
        std::size_t n = 0;
        for (auto const& f : out) {
            if (f.type == type && f.at >= from && f.at <= to) ++n;
        }
        return n;
    }
    [[nodiscard]] std::optional<fsm_state> state_at(ns at) const {
        for (auto const& [t, s] : state) {
            if (t == at) return s;
        }
        return std::nullopt;
    }
};

struct PeerAction {
    ns at{0};
    std::function<void(pr::Rig&, Timeline const&)> act{};
    bool sends = true;  // the action writes a frame the session reads
};

struct TimelineSpec {
    session_role role = session_role::acceptor;
    std::function<void(SessionConfig&)> tweak{};
    std::vector<PeerAction> actions{};
    std::vector<ns> extra_probes{};
    ns until{0};
    ns step = 1s;
    fixpp::core::steady_time_point steady_seed{};
    std::shared_ptr<t105::CountingClock>* counting = nullptr;  // set: clock_override
};

Timeline run_timeline(TimelineSpec const& spec, const char* site) {
    auto app = std::make_shared<LivenessApp>();
    pr::Rig rig{app, spec.steady_seed};
    auto cfg = rig.cfg(spec.role);
    cfg.heartbeat_interval = kHb;
    if (spec.counting != nullptr) {
        *spec.counting = std::make_shared<t105::CountingClock>(rig.clock);
        cfg.clock_override = *spec.counting;
    }
    if (spec.tweak) spec.tweak(cfg);
    Timeline tl;
    bool const started = rig.start(std::move(cfg));
    tl.active = started && rig.to_active();
    if (started && !tl.active) {
        // The frames the session sent though it did not stay Active (the Logon is not read).
        for (auto const* type : {"0", "1", "5"}) {
            for (auto const& f : pr::frames_of_type(rig.peer.received, type)) {
                tl.out.push_back({type, ns{0}, f});
            }
        }
    }
    if (tl.active) {
        std::set<ns> probes(spec.extra_probes.begin(), spec.extra_probes.end());
        for (ns t = spec.step; t <= spec.until; t += spec.step) probes.insert(t);
        for (auto const& a : spec.actions) probes.insert(a.at);
        std::size_t seen = rig.peer.received.size();
        ns now{0};
        auto collect = [&](ns at) {
            auto const fresh = rig.peer.received.substr(seen);
            for (auto const& f : pr::frames_of_type(fresh, "0")) tl.out.push_back({"0", at, f});
            for (auto const& f : pr::frames_of_type(fresh, "1")) tl.out.push_back({"1", at, f});
            for (auto const& f : pr::frames_of_type(fresh, "2")) tl.out.push_back({"2", at, f});
            for (auto const& f : pr::frames_of_type(fresh, "5")) tl.out.push_back({"5", at, f});
            // Only whole frames are consumed; a partial tail is read at the next probe.
            auto const last = fresh.rfind(
                "\x01"
                "10=");
            if (last != std::string::npos) {
                auto const end = fresh.find('\x01', last + 1);
                if (end != std::string::npos) seen += end + 1;
            }
        };
        for (ns const t : probes) {
            if (t > spec.until) break;
            rig.clock->advance(t - now);
            now = t;
            (void)t105::drive_bounded(rig.ioc, 64);
            bool sent = false;
            for (auto const& a : spec.actions) {
                if (a.at == t) {
                    a.act(rig, tl);
                    sent = sent || a.sends;
                }
            }
            if (sent) {
                // The frame is read at this instant: wait until the session's last inbound
                // instant is this one (the clock held still), or the pump budget passes.
                (void)rig.run_until([&] {
                    auto const s = rig.session();
                    return rig.peer.all_written() && s &&
                           session_test_access::last_inbound_steady(*s) == rig.clock->steady_now();
                });
            }
            (void)t105::drive_bounded(rig.ioc, 64);
            collect(t);
            auto const st = rig.state();
            tl.state.emplace_back(t, st);
            if (st != fsm_state::Active) {
                tl.ended_at = t;
                tl.eof = t105::pump_until_peer_read_ends(
                             rig.ioc, [&] { return rig.peer.read_ended; }, kWallCap, site) &&
                         t105::ended_in_eof(rig.peer, site);
                collect(t);
                break;
            }
        }
    }
    rig.stop();
    return tl;
}

constexpr ns sec(int s) { return std::chrono::seconds{s}; }
constexpr ns kGuard = t105::kLivenessGuard;

// The peer's frames, numbered from 2 (its Logon was 1).
struct PeerSeq {
    std::uint32_t next = 2;
};

std::string app_message(pr::Rig& rig, std::uint32_t seq) {
    return rig.msg("D", seq,
                   "11=ORD1\x01"
                   "21=1\x01"
                   "55=X\x01"
                   "54=1\x01"
                   "60=" +
                       rig.sending_time() +
                       "\x01"
                       "40=1\x01");
}

PeerAction app_at(ns at, std::shared_ptr<PeerSeq> seq) {
    return {at,
            [seq](pr::Rig& rig, Timeline const&) { rig.peer.send(app_message(rig, seq->next++)); }};
}

// The TestReqID(112) of the n-th TestRequest the session sent (0-based), or "".
std::string tr_id(Timeline const& tl, std::size_t n) {
    auto const trs = tl.of("1");
    return n < trs.size() ? pr::field_value(trs[n].frame, "112") : std::string{};
}

// The frames as "<type>@<ms>", for a failure message.
std::string dump(Timeline const& tl) {
    std::string s;
    for (auto const& f : tl.out) {
        s += f.type + "@" +
             std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(f.at).count()) +
             " ";
    }
    return s;
}

std::string give_up_text(Timeline const& tl) {
    auto const lo = tl.of("5");
    return lo.empty() ? std::string{} : pr::field_value(lo.back().frame, "58");
}

class LivenessTimingUs3105 : public ::testing::TestWithParam<session_role> {};

// T089. The base sends the TestRequest at 1 × HeartBtInt (10 s).
TEST_P(LivenessTimingUs3105, Default_NoTestRequestAt11s_OneAt12s) {
    auto const tl = run_timeline({.role = GetParam(), .until = sec(12)}, "Default_TR");
    ASSERT_TRUE(tl.active) << "setup";
    EXPECT_EQ(tl.count("1", ns{0}, sec(11)), 0U) << "a TestRequest at or before 11 s";
    EXPECT_EQ(tl.count("1", sec(12), sec(12)), 1U) << "no TestRequest at 12 s";
    EXPECT_EQ(tl.count("0", sec(10), sec(10)), 1U) << "the Heartbeat at 10 s (positive control)";
}

// T089. The base ends near 2 × HeartBtInt.
TEST_P(LivenessTimingUs3105, Default_ActiveAt23900ms_EndedAt24sPlusGuard) {
    auto const tl =
        run_timeline({.role = GetParam(),
                      .extra_probes = {ns{std::chrono::milliseconds{23900}}, sec(24) + kGuard},
                      .until = sec(25)},
                     "Default_GiveUp");
    ASSERT_TRUE(tl.active) << "setup";
    EXPECT_EQ(tl.state_at(ns{std::chrono::milliseconds{23900}}), fsm_state::Active);
    ASSERT_TRUE(tl.ended_at.has_value()) << "not given up";
    EXPECT_EQ(*tl.ended_at, sec(24) + kGuard);
    EXPECT_EQ(tl.count("5", sec(24) + kGuard, sec(24) + kGuard), 1U) << "R-1's Logout";
    EXPECT_EQ(give_up_text(tl), "TestRequest unanswered, TestReqID " + tr_id(tl, 0));
    EXPECT_TRUE(tl.eof) << "no EOF after the give-up";
}

TEST_P(LivenessTimingUs3105, SetThreshold7s_TestRequestAt7s_GiveUpAt14sPlusGuard) {
    auto const tl = run_timeline(
        {.role = GetParam(),
         .tweak = [](SessionConfig& c) { c.test_request_threshold = std::chrono::seconds{7}; },
         .extra_probes = {sec(14) + kGuard},
         .until = sec(16)},
        "SetThreshold7s");
    ASSERT_TRUE(tl.active) << "setup";
    EXPECT_EQ(tl.count("1", ns{0}, sec(6)), 0U) << "a TestRequest before 7 s";
    EXPECT_EQ(tl.count("1", sec(7), sec(7)), 1U) << "no TestRequest at 7 s";
    EXPECT_EQ(tl.state_at(sec(14)), fsm_state::Active);
    ASSERT_TRUE(tl.ended_at.has_value()) << "not given up";
    EXPECT_EQ(*tl.ended_at, sec(14) + kGuard);
    EXPECT_EQ(tl.count("5", sec(14) + kGuard, sec(14) + kGuard), 1U) << "R-1's Logout";
    EXPECT_TRUE(tl.eof);
}

// At exactly 2T the give-up has not run, so the peer's answer delivered then wins.
TEST_P(LivenessTimingUs3105, GuardAtExactly2T_EchoWins) {
    auto seq = std::make_shared<PeerSeq>();
    auto const tl = run_timeline(
        {.role = GetParam(),
         .actions = {{sec(24),
                      [seq](pr::Rig& rig, Timeline const& t) {
                          rig.peer.send(rig.msg("0", seq->next++, "112=" + tr_id(t, 0) + "\x01"));
                      }}},
         .extra_probes = {sec(24) + kGuard},
         .until = sec(26)},
        "GuardAtExactly2T");
    ASSERT_TRUE(tl.active) << "setup";
    EXPECT_FALSE(tr_id(tl, 0).empty()) << "no TestRequest";
    EXPECT_EQ(tl.state_at(sec(24) + kGuard), fsm_state::Active) << "given up after the echo";
    EXPECT_EQ(tl.state_at(sec(26)), fsm_state::Active);
    EXPECT_FALSE(tl.ended_at.has_value());
    EXPECT_EQ(tl.count("5", ns{0}, sec(26)), 0U) << "a Logout";
}

TEST_P(LivenessTimingUs3105, At2TPlus1ns_GivesUp) {
    auto const tl = run_timeline(
        {.role = GetParam(), .extra_probes = {sec(24) + kGuard}, .until = sec(25)}, "At2TPlus1ns");
    ASSERT_TRUE(tl.active) << "setup";
    EXPECT_EQ(tl.state_at(sec(24)), fsm_state::Active) << "given up at exactly 2T";
    ASSERT_TRUE(tl.ended_at.has_value()) << "not given up";
    EXPECT_EQ(*tl.ended_at, sec(24) + kGuard);
    EXPECT_EQ(tl.count("5", sec(24) + kGuard, sec(24) + kGuard), 1U);
}

// T090 (clarification D-c). Application traffic after the TestRequest, no Heartbeat answer:
// not given up 2T after the TestRequest's silence began, given up 2T after the last frame.
TEST_P(LivenessTimingUs3105,
       TrafficAfterTestRequestNoHeartbeat_NotGivenUpAt2T_GivenUpAt2TAfterLastFrame) {
    auto seq = std::make_shared<PeerSeq>();
    auto const tl = run_timeline({.role = GetParam(),
                                  .actions = {app_at(sec(13), seq), app_at(sec(20), seq)},
                                  .extra_probes = {sec(24) + kGuard, sec(44) + kGuard},
                                  .until = sec(46)},
                                 "TrafficAfterTestRequest");
    ASSERT_TRUE(tl.active) << "setup";
    EXPECT_EQ(tl.count("1", sec(12), sec(12)), 1U) << "no TestRequest at 12 s";
    EXPECT_EQ(tl.state_at(sec(24) + kGuard), fsm_state::Active) << "given up at 2T of TR1";
    EXPECT_EQ(tl.state_at(sec(36)), fsm_state::Active);
    EXPECT_EQ(tl.state_at(sec(44)), fsm_state::Active);
    ASSERT_TRUE(tl.ended_at.has_value()) << "not given up; frames: " << dump(tl);
    EXPECT_EQ(*tl.ended_at, sec(44) + kGuard) << "not 2T after the last frame";
}

// A silent peer: the Heartbeat at 10 s, the TestRequest at 12 s, no Heartbeat while it waits,
// the give-up just after 24 s.
TEST_P(LivenessTimingUs3105, SilentPeer_NoHeartbeatWhileTestRequestPending) {
    auto const tl = run_timeline(
        {.role = GetParam(), .extra_probes = {sec(24) + kGuard}, .until = sec(25)}, "SilentPeer");
    ASSERT_TRUE(tl.active) << "setup";
    EXPECT_EQ(tl.count("0", sec(10), sec(10)), 1U) << "no Heartbeat at 10 s";
    EXPECT_EQ(tl.count("1", sec(12), sec(12)), 1U) << "no TestRequest at 12 s";
    EXPECT_EQ(tl.state_at(sec(22)), fsm_state::Active);
    EXPECT_EQ(tl.count("0", sec(12) + ns{1}, sec(24) + kGuard), 0U)
        << "a Heartbeat while the TestRequest waited on a silent peer";
    ASSERT_TRUE(tl.ended_at.has_value()) << "not given up";
    EXPECT_EQ(*tl.ended_at, sec(24) + kGuard);
}

// A frame at 13 s: the Heartbeat at 22 s, TR2 at 25 s, no Heartbeat at 35 s, the give-up just
// after 37 s.
TEST_P(LivenessTimingUs3105,
       FrameAfterTestRequest_HeartbeatAt22s_TR2At25s_NoneAt35s_GiveUpAfter37s) {
    auto seq = std::make_shared<PeerSeq>();
    auto const tl = run_timeline({.role = GetParam(),
                                  .actions = {app_at(sec(13), seq)},
                                  .extra_probes = {sec(37) + kGuard},
                                  .until = sec(38)},
                                 "FrameAfterTestRequest");
    ASSERT_TRUE(tl.active) << "setup";
    EXPECT_EQ(tl.count("1", sec(12), sec(12)), 1U) << "no TR1 at 12 s";
    EXPECT_EQ(tl.count("0", sec(22), sec(22)), 1U) << "no Heartbeat at 22 s";
    EXPECT_EQ(tl.count("0", sec(13), sec(21)), 0U) << "a Heartbeat before 22 s";
    EXPECT_EQ(tl.count("1", sec(25), sec(25)), 1U) << "no TR2 at 25 s";
    EXPECT_EQ(tl.count("1", sec(13), sec(24)), 0U) << "a TestRequest before 25 s";
    EXPECT_EQ(tl.count("0", sec(23), sec(37) + kGuard), 0U) << "a Heartbeat after TR2";
    EXPECT_EQ(tl.state_at(sec(37)), fsm_state::Active);
    ASSERT_TRUE(tl.ended_at.has_value()) << "not given up";
    EXPECT_EQ(*tl.ended_at, sec(37) + kGuard);
    EXPECT_EQ(give_up_text(tl), "TestRequest unanswered, TestReqID " + tr_id(tl, 1));
}

// A frame at 23 s, after the Heartbeat instant (22 s) the silent peer had made ineligible: the
// Heartbeat goes at the loop's next wake, the stale give-up instant, not at 23 s.
TEST_P(LivenessTimingUs3105, FrameAfterHeartbeatInstant_HeartbeatAtTheStaleGiveUpWake) {
    auto seq = std::make_shared<PeerSeq>();
    auto const tl = run_timeline({.role = GetParam(),
                                  .actions = {app_at(sec(23), seq)},
                                  .extra_probes = {sec(24) + kGuard},
                                  .until = sec(26)},
                                 "FrameAfterHeartbeatInstant");
    ASSERT_TRUE(tl.active) << "setup";
    EXPECT_EQ(tl.count("1", sec(12), sec(12)), 1U) << "no TR1 at 12 s";
    EXPECT_EQ(tl.count("0", sec(12) + ns{1}, sec(24)), 0U) << "a Heartbeat at or before 24 s";
    EXPECT_EQ(tl.count("0", sec(24) + kGuard, sec(24) + kGuard), 1U)
        << "no Heartbeat at the stale give-up wake";
    EXPECT_EQ(tl.state_at(sec(26)), fsm_state::Active);
    EXPECT_FALSE(tl.ended_at.has_value());
}

// After TR1 a silent peer: the loop sleeps once per wake, so the sleeps from TR1 to the give-up
// are few; a loop that kept a reached, ineligible Heartbeat instant in its sleep set would
// return from every sleep at once (the bounded drive stops it, and the count shows it).
TEST_P(LivenessTimingUs3105, SilentAfterTestRequest_NoSpin) {
    std::shared_ptr<t105::CountingClock> counting;
    std::size_t at_tr1 = 0;
    std::size_t at_give_up = 0;
    auto const tl = run_timeline({.role = GetParam(),
                                  .actions = {{sec(12),
                                               [&](pr::Rig&, Timeline const&) {
                                                   if (counting) at_tr1 = counting->sleep_calls();
                                               },
                                               false},
                                              {sec(24) + kGuard,
                                               [&](pr::Rig&, Timeline const&) {
                                                   if (counting)
                                                       at_give_up = counting->sleep_calls();
                                               },
                                               false}},
                                  .until = sec(25),
                                  .counting = &counting},
                                 "NoSpin");
    ASSERT_TRUE(tl.active) << "setup";
    EXPECT_EQ(tl.count("1", sec(12), sec(12)), 1U) << "no TR1 at 12 s";
    ASSERT_TRUE(tl.ended_at.has_value()) << "not given up";
    EXPECT_EQ(*tl.ended_at, sec(24) + kGuard);
    // The sleeps from TR1's instant to the give-up's: the one to the Heartbeat instant, the one
    // to the give-up instant, and the slack of the session's other sleeps on this clock.
    EXPECT_GT(at_give_up, at_tr1) << "no sleep counted";
    EXPECT_LE(at_give_up - at_tr1, 6U) << "the loop spun between TR1 and the give-up";
}

// D-d: TR1's late answer after TR2 neither answers nor ends; the give-up 2T after that frame.
TEST_P(LivenessTimingUs3105, SecondTestRequest_LateAnswerToTheFirst_StaysActive) {
    auto seq = std::make_shared<PeerSeq>();
    auto const tl = run_timeline(
        {.role = GetParam(),
         .actions = {app_at(sec(13), seq),
                     {sec(26),
                      [seq](pr::Rig& rig, Timeline const& t) {
                          rig.peer.send(rig.msg("0", seq->next++, "112=" + tr_id(t, 0) + "\x01"));
                      }}},
         .extra_probes = {sec(50) + kGuard},
         .until = sec(51)},
        "LateAnswerToTheFirst");
    ASSERT_TRUE(tl.active) << "setup";
    EXPECT_EQ(tl.count("1", sec(25), sec(25)), 1U) << "no TR2 at 25 s";
    EXPECT_EQ(tl.state_at(sec(26)), fsm_state::Active) << "TR1's answer ended the session";
    EXPECT_EQ(tl.state_at(sec(50)), fsm_state::Active);
    ASSERT_TRUE(tl.ended_at.has_value()) << "not given up";
    EXPECT_EQ(*tl.ended_at, sec(50) + kGuard) << "not 2T after the late answer";
}

// T091. A gap open (a frame above NextNumIn at 1 s): a peer that keeps sending frames that do
// not advance NextNumIn ends at the gap deadline (2H from the gap) with its Logout.
TEST_P(LivenessTimingUs3105, GapOpen_PeerSendsNonAdvancingFrame_DeadlineLogout) {
    auto seq = std::make_shared<PeerSeq>();
    auto high = [](ns at, std::uint32_t n) {
        return PeerAction{
            at, [n](pr::Rig& rig, Timeline const&) { rig.peer.send(app_message(rig, n)); }};
    };
    auto const tl = run_timeline({.role = GetParam(),
                                  .actions = {high(sec(1), 5), high(sec(11), 6), high(sec(22), 7)},
                                  .until = sec(22)},
                                 "GapOpen_NonAdvancing");
    ASSERT_TRUE(tl.active) << "setup";
    EXPECT_EQ(tl.count("2", sec(1), sec(1)), 1U) << "setup: no ResendRequest for the gap";
    EXPECT_EQ(tl.count("5", ns{0}, sec(21)), 0U) << "a Logout before the gap deadline's frame";
    EXPECT_EQ(tl.count("5", sec(22), sec(22)), 1U) << "no gap-deadline Logout at 22 s";
    EXPECT_EQ(give_up_text(tl), "ResendRequest unanswered, expecting MsgSeqNum 2")
        << "the Logout at 22 s is not the gap deadline's";
    EXPECT_FALSE(tl.ended_at.has_value()) << "ended at once: the gap deadline's Logout waits (R-2)";
    EXPECT_EQ(tl.count("1", ns{0}, sec(22)), 0U) << "a TestRequest: the peer was never silent T";
}

// A gap open and the peer silent: R-1's Logout at 2T after its last frame.
TEST_P(LivenessTimingUs3105, GapOpen_PeerSilent_R1LogoutAt2T) {
    auto const tl = run_timeline(
        {.role = GetParam(),
         .actions = {{sec(1),
                      [](pr::Rig& rig, Timeline const&) { rig.peer.send(app_message(rig, 5)); }}},
         .extra_probes = {sec(25) + kGuard},
         .until = sec(26)},
        "GapOpen_Silent");
    ASSERT_TRUE(tl.active) << "setup";
    EXPECT_EQ(tl.count("1", sec(13), sec(13)), 1U) << "no TestRequest at 13 s";
    EXPECT_EQ(tl.state_at(sec(25)), fsm_state::Active);
    ASSERT_TRUE(tl.ended_at.has_value()) << "not given up";
    EXPECT_EQ(*tl.ended_at, sec(25) + kGuard);
    EXPECT_EQ(give_up_text(tl), "TestRequest unanswered, TestReqID " + tr_id(tl, 0));
    EXPECT_TRUE(tl.eof);
}

// T = 25 s > 2H: no TestRequest before T, no gap-deadline Logout with no frame, R-1's Logout
// and EOF just after 2T.
TEST_P(LivenessTimingUs3105, GapOpen_ThresholdAboveTwoH_PeerSilent_TestRequestAtT_R1LogoutAt2T) {
    auto const tl = run_timeline(
        {.role = GetParam(),
         .tweak = [](SessionConfig& c) { c.test_request_threshold = std::chrono::seconds{25}; },
         .actions = {{sec(1),
                      [](pr::Rig& rig, Timeline const&) { rig.peer.send(app_message(rig, 5)); }}},
         .extra_probes = {sec(51) + kGuard},
         .until = sec(52)},
        "GapOpen_ThresholdAboveTwoH");
    ASSERT_TRUE(tl.active) << "setup";
    EXPECT_EQ(tl.count("1", ns{0}, sec(25)), 0U) << "a TestRequest before T";
    EXPECT_EQ(tl.count("1", sec(26), sec(26)), 1U) << "no TestRequest at T";
    EXPECT_EQ(tl.count("5", ns{0}, sec(51)), 0U) << "a Logout before 2T";
    ASSERT_TRUE(tl.ended_at.has_value()) << "not given up";
    EXPECT_EQ(*tl.ended_at, sec(51) + kGuard);
    EXPECT_EQ(give_up_text(tl), "TestRequest unanswered, TestReqID " + tr_id(tl, 0));
    EXPECT_TRUE(tl.eof);
}

TEST_P(LivenessTimingUs3105, HeartBtIntNegative_NoTestRequest) {
    auto const tl = run_timeline(
        {.role = GetParam(),
         .tweak = [](SessionConfig& c) { c.heartbeat_interval = std::chrono::seconds{-10}; },
         .until = sec(30)},
        "HeartBtIntNegative");
    EXPECT_EQ(tl.count("1", ns{0}, sec(30)), 0U) << "a TestRequest with HeartBtInt below 0";
    EXPECT_EQ(tl.count("0", ns{0}, sec(30)), 0U) << "a Heartbeat with HeartBtInt below 0";
    EXPECT_EQ(tl.count("5", ns{0}, sec(30)), 0U) << "a Logout with HeartBtInt below 0";
    EXPECT_TRUE(tl.active) << "the session did not stay Active with HeartBtInt below 0";
    EXPECT_FALSE(tl.ended_at.has_value());
}

// [guard: the loop's HeartBtInt test deleted]
TEST_P(LivenessTimingUs3105, HeartBtIntZero_NoTestRequest) {
    auto const tl = run_timeline(
        {.role = GetParam(),
         .tweak = [](SessionConfig& c) { c.heartbeat_interval = std::chrono::seconds{0}; },
         .until = sec(30)},
        "HeartBtIntZero");
    ASSERT_TRUE(tl.active) << "setup";
    EXPECT_EQ(tl.count("1", ns{0}, sec(30)), 0U) << "a TestRequest with HeartBtInt 0";
    EXPECT_EQ(tl.count("0", ns{0}, sec(30)), 0U) << "a Heartbeat with HeartBtInt 0";
    EXPECT_FALSE(tl.ended_at.has_value());
}

// 2T past the clock's range: the give-up instant saturates at the clock's end, so it never runs.
// The steady seed leaves T within the range and 2T beyond it.
TEST_P(LivenessTimingUs3105, ThresholdOverflow_Saturates) {
    auto const seed = fixpp::core::steady_time_point::max() - std::chrono::seconds{200};
    auto const tl = run_timeline(
        {.role = GetParam(),
         .tweak = [](SessionConfig& c) { c.test_request_threshold = std::chrono::seconds{120}; },
         .until = sec(190),
         .step = sec(5),
         .steady_seed = seed},
        "ThresholdOverflow");
    ASSERT_TRUE(tl.active) << "setup";
    EXPECT_EQ(tl.count("1", ns{0}, sec(115)), 0U) << "a TestRequest before T";
    EXPECT_EQ(tl.count("1", sec(120), sec(120)), 1U) << "no TestRequest at T";
    EXPECT_FALSE(tl.ended_at.has_value()) << "given up though 2T is past the clock's range";
}

FIXPP_105_BOTH_ROLES(LivenessTimingUs3105);

// T093 (FR-011, contract C-6.6). A set threshold of zero or below is refused; no bound above.
fixpp::core::expected_t<void> open_with_threshold(std::chrono::milliseconds t) {
    rg::Scene s{rg::Options{.heartbeat = kHb}};
    s.cfg.test_request_threshold = t;
    s.session = std::make_unique<fixpp::session::Session>(s.engine, s.cfg);
    auto fut = asio::co_spawn(fixpp::test_support::session_executor_of(*s.session, s.ioc),
                              s.session->open(), asio::use_future);
    (void)fixpp::test_support::pump_until(
        s.ioc, [&] { return rg::Scene::ready(fut); }, fixpp::test_support::kPumpBudget,
        fixpp::test_support::kPumpSlice, "open_with_threshold");
    if (!rg::Scene::ready(fut)) {
        ADD_FAILURE() << "open() did not return";
        return std::unexpected(fixpp::core::error::dispatch_aborted);
    }
    auto r = fut.get();
    if (r.has_value()) {
        auto c = asio::co_spawn(fixpp::test_support::session_executor_of(*s.session, s.ioc),
                                s.session->close(fixpp::session::close_mode::terminal),
                                asio::use_future);
        (void)fixpp::test_support::pump_until(
            s.ioc, [&] { return rg::Scene::ready(c); }, fixpp::test_support::kPumpBudget,
            fixpp::test_support::kPumpSlice, "open_with_threshold/close");
    }
    s.drain();
    return r;
}

TEST(SessionOpen105, ThresholdZero_InvalidConfig) {
    auto const r = open_with_threshold(std::chrono::milliseconds{0});
    ASSERT_FALSE(r.has_value()) << "open() accepted test_request_threshold 0";
    EXPECT_EQ(r.error(), fixpp::core::error::invalid_session_config);
}

TEST(SessionOpen105, ThresholdNegative_InvalidConfig) {
    auto const r = open_with_threshold(std::chrono::milliseconds{-1});
    ASSERT_FALSE(r.has_value()) << "open() accepted test_request_threshold -1 ms";
    EXPECT_EQ(r.error(), fixpp::core::error::invalid_session_config);
}

// [guard: an upper bound on T added at Session::open()]
TEST(SessionOpen105, ThresholdAboveGapDeadline_Accepted) {
    auto const r = open_with_threshold(std::chrono::seconds{25});
    EXPECT_TRUE(r.has_value()) << "open() refused T = 25 s with HeartBtInt 10 s";
}

fixpp::core::expected_t<void> register_with_threshold(std::chrono::milliseconds t) {
    pr::Rig rig;
    auto cfg = rig.cfg();
    cfg.heartbeat_interval = kHb;
    cfg.test_request_threshold = t;
    fixpp::core::EngineConfig ec;
    ec.executor = rig.ioc.get_executor();
    ec.clock = rig.clock;
    fixpp::session::Engine engine{rig.ioc.get_executor(), std::move(ec)};
    auto r = engine.register_session(std::move(cfg));
    auto fut = asio::co_spawn(rig.ioc, engine.stop(), asio::use_future);
    if (!fixpp::test_support::pump_until_ready(rig.ioc, fut, fixpp::test_support::kPumpBudget,
                                               "register_with_threshold/stop")) {
        ADD_FAILURE() << "Engine::stop() did not return";
    }
    return r;
}

TEST(EngineRegister105, ThresholdZero_InvalidConfig) {
    auto const r = register_with_threshold(std::chrono::milliseconds{0});
    ASSERT_FALSE(r.has_value()) << "register_session accepted test_request_threshold 0";
    EXPECT_EQ(r.error(), fixpp::core::error::invalid_session_config);
}

TEST(EngineRegister105, ThresholdNegative_InvalidConfig) {
    auto const r = register_with_threshold(std::chrono::milliseconds{-1});
    ASSERT_FALSE(r.has_value()) << "register_session accepted test_request_threshold -1 ms";
    EXPECT_EQ(r.error(), fixpp::core::error::invalid_session_config);
}

// ════════════════════════════════════════════════════════════════════════════════════════
// T119b (tasks.md; the owner's cap ruling, spec.md Clarifications: the held TestRequest's cap;
// data-model E-4; contract C-6.2, C-3.1): a TestRequest that fell due while a resend answer holds
// the replay gate, with a TestRequest pending and an inbound frame since it, is given up once the
// clock is strictly past three thresholds after the last inbound frame and it is still held; the
// give-up is decided there and its Logout takes R-1's write limit from that decision. Here the
// peer's ResendRequest is that inbound frame and its answer's retrieve is held in a GateStore until
// the cell releases it. HeartBtInt and T are both kHb; instants are mock-clock offsets from the
// ResendRequest. A held wait the loop parks in is shown by the gate's timer_waits.
// ════════════════════════════════════════════════════════════════════════════════════════

enum class held_cap_scene : std::uint8_t {
    one_step,           // the clock passes the cap in one step from the request
    parked_tr,          // the loop first waits at the gate in its TestRequest arm
    parked_hb,          // the loop first waits at the gate in its Heartbeat arm
    boundary,           // the clock at exactly three thresholds, then the answer released
    release_after_cap,  // the give-up decided, then the answer released before the write limit
};

struct HeldCapReading {
    bool setup = false;  // Active, the first TestRequest read, the request's retrieve held
    std::string why;     // the setup step that failed
    std::string tr1;     // TestReqID(112) of the TestRequest before the request
    std::uint64_t waits_before = 0;           // the gate's timer_waits once the request is held
    std::uint64_t waits_parked = 0;           // ... after the parking step (parked scenes)
    bool active_parked = false;               // Active after the parking step (parked scenes)
    std::string sent_parked;                  // what the peer read after the request, at that step
    bool active_at_cap = false;               // Active after the step to the cap
    std::size_t records_at_cap = 0;           // give-up records after the step to the cap
    bool ended = false;                       // not Active at the scene's end
    fsm_state end_state = fsm_state::Active;  // the state at the scene's end
    bool eof = false;                         // the peer's read ended in EOF
    std::string after;                        // what the peer read after the request
    std::vector<fixpp::log::Record> records;
};

HeldCapReading run_held_cap(session_role role, held_cap_scene scene, const char* site) {
    auto app = std::make_shared<LivenessApp>();
    pr::Rig rig{app};
    t105::LogCapture log;
    auto st = std::make_shared<t105::gate_store_state>();
    auto cfg = rig.cfg(role);
    cfg.heartbeat_interval = kHb;
    cfg.test_request_threshold = std::chrono::milliseconds{kHb};
    cfg.logger_override = log.logger;
    cfg.store_factory = std::make_shared<t105::GateStoreFactory>(st);
    auto const write_limit = std::chrono::milliseconds{cfg.logout_disconnect_timeout_ms};
    constexpr ns kT = kHb;
    HeldCapReading r;
    auto gate_waits = [&] {
        auto const s = rig.session();
        return s ? session_test_access::replay_gate(*s).timer_waits : 0U;
    };
    auto records_now = [&] { return log.records_of(FIXPP_FORMAT_ID(kGiveUpFormat)).size(); };
    std::size_t mark = 0;
    bool up = rig.start(std::move(cfg)) && rig.to_active();
    if (!up) r.why = "not Active";
    if (up) {
        auto const tr = t105::advance_until_msg_type(
            *rig.clock, rig.ioc, [&] { return rig.peer.received; }, "1", 0, 1s,
            t105::liveness_wire_cap(kHb, 1s), site);
        up = tr.has_value();
        if (!up) r.why = "no first TestRequest";
        if (up) r.tr1 = pr::field_value(tr->frame, "112");
    }
    if (up) {
        // One second after the TestRequest, so the request is an inbound frame since it.
        (void)t105::advance_and_drive(*rig.clock, rig.ioc, 1s, 64);
        st->hold_when = [](t105::store_call k, seqnum_t) {
            return k == t105::store_call::retrieve;
        };
        mark = rig.peer.received.size();
        rig.peer.send(rig.msg("2", 2,
                              "7=1\x01"
                              "16=0\x01"));
        up = rig.run_until([&] { return st->parked == 1U; });
        if (!up) r.why = "the request's retrieve was not held";
    }
    if (up) {
        r.setup = true;
        r.waits_before = gate_waits();
        ns at{0};
        auto step_to = [&](ns target) {
            rig.clock->advance(target - at);
            at = target;
            (void)t105::drive_bounded(rig.ioc, 256);
        };
        ns const cap = 3 * kT + 1ms;
        if (scene == held_cap_scene::parked_tr || scene == held_cap_scene::parked_hb) {
            step_to(scene == held_cap_scene::parked_tr ? kT + 1s : kT - 500ms);
            r.waits_parked = gate_waits();
            r.active_parked = rig.state() == fsm_state::Active;
            r.sent_parked = rig.peer.received.substr(mark);
        }
        step_to(scene == held_cap_scene::boundary ? 3 * kT : cap);
        r.active_at_cap = rig.state() == fsm_state::Active;
        r.records_at_cap = records_now();
        if (scene == held_cap_scene::boundary) {
            st->hold_when = nullptr;
            st->release_all();
            // The clock held still: the TestRequest is written on the release itself.
            (void)rig.run_until(
                [&] { return !pr::frames_of_type(rig.peer.received.substr(mark), "1").empty(); },
                kWallCap);
            r.ended = rig.state() != fsm_state::Active;
        } else {
            if (scene == held_cap_scene::release_after_cap) {
                st->hold_when = nullptr;
                st->release_all();
            } else {
                step_to(cap + write_limit);
            }
            r.ended = rig.state() != fsm_state::Active;
            r.eof = t105::pump_until_peer_read_ends(
                        rig.ioc, [&] { return rig.peer.read_ended; }, kWallCap, site) &&
                    t105::ended_in_eof(rig.peer, site);
            r.ended = rig.state() != fsm_state::Active;
        }
        r.end_state = rig.state();
        r.after = rig.peer.received.substr(mark);
    }
    st->hold_when = nullptr;
    st->release_all();
    rig.stop();
    r.records = log.records_of(FIXPP_FORMAT_ID(kGiveUpFormat));
    return r;
}

class HeldTestRequestCap105 : public ::testing::TestWithParam<session_role> {};

// The decision at the cap and the write limit from it: one record naming the TestRequest that was
// written, the session still Active while the Logout waits for the held answer, then the write
// limit ends the session with no Logout written and the connection closed.
void expect_capped(HeldCapReading const& r, const char* site) {
    ASSERT_TRUE(r.setup) << site << ": setup: " << r.why;
    ASSERT_FALSE(r.tr1.empty()) << site << ": setup";
    EXPECT_EQ(r.records_at_cap, 1U) << site << ": no give-up decided at the cap";
    EXPECT_TRUE(r.active_at_cap) << site << ": the give-up did not wait for the held answer";
    EXPECT_TRUE(r.ended) << site << ": the session was not ended at the write limit";
    EXPECT_EQ(r.end_state, fsm_state::Disconnected) << site;
    EXPECT_TRUE(r.eof) << site << ": the peer's read did not end in EOF";
    EXPECT_TRUE(pr::frames_of_type(r.after, "5").empty())
        << site << ": a Logout past the held answer: " << r.after;
    EXPECT_TRUE(pr::frames_of_type(r.after, "1").empty())
        << site << ": a TestRequest after the request: " << r.after;
    ASSERT_EQ(r.records.size(), 1U) << site << ": the give-up record";
    EXPECT_EQ(record_test_req_id(r.records.front()), r.tr1) << site << ": test_req_id";
}

TEST_P(HeldTestRequestCap105, HeldPastThreeThresholds_OneStep_GivesUpThenWriteLimitCloses) {
    auto const r = run_held_cap(GetParam(), held_cap_scene::one_step, "HeldCap/OneStep");
    expect_capped(r, "HeldCap/OneStep");
}

TEST_P(HeldTestRequestCap105, HeldPastThreeThresholds_ParkedInTestRequestArm_GivesUp) {
    auto const r = run_held_cap(GetParam(), held_cap_scene::parked_tr, "HeldCap/ParkedTr");
    EXPECT_GT(r.waits_parked, r.waits_before) << "setup: the loop did not wait at the gate";
    EXPECT_TRUE(r.active_parked) << "setup";
    EXPECT_TRUE(r.sent_parked.empty())
        << "setup: a frame inside the held answer: " << r.sent_parked;
    expect_capped(r, "HeldCap/ParkedTr");
}

TEST_P(HeldTestRequestCap105, HeldPastThreeThresholds_ParkedInHeartbeatArm_GivesUp) {
    auto const r = run_held_cap(GetParam(), held_cap_scene::parked_hb, "HeldCap/ParkedHb");
    EXPECT_GT(r.waits_parked, r.waits_before) << "setup: the loop did not wait at the gate";
    EXPECT_TRUE(r.active_parked) << "setup";
    EXPECT_TRUE(r.sent_parked.empty())
        << "setup: a frame inside the held answer: " << r.sent_parked;
    expect_capped(r, "HeldCap/ParkedHb");
}

// The boundary: at exactly three thresholds the held TestRequest is not given up; released there,
// it is written and the session stays Active.
TEST_P(HeldTestRequestCap105, HeldAtExactlyThreeThresholds_NotGivenUp_WrittenOnRelease) {
    auto const r = run_held_cap(GetParam(), held_cap_scene::boundary, "HeldCap/Boundary");
    ASSERT_TRUE(r.setup) << "setup: " << r.why;
    EXPECT_TRUE(r.active_at_cap) << "given up at exactly three thresholds";
    EXPECT_EQ(r.records_at_cap, 0U) << "a give-up record at exactly three thresholds";
    EXPECT_FALSE(r.ended) << "the session ended after the release";
    EXPECT_EQ(pr::frames_of_type(r.after, "1").size(), 1U)
        << "the held TestRequest was not written on the release: " << r.after;
    EXPECT_TRUE(pr::frames_of_type(r.after, "5").empty()) << r.after;
    EXPECT_TRUE(r.records.empty()) << "a give-up record";
}

// The give-up is final: the answer released after the decision and before the write limit, the
// Logout is written naming the TestRequest that was written, no TestRequest follows, then EOF.
TEST_P(HeldTestRequestCap105, GivenUpAtTheCap_ReleasedBeforeTheWriteLimit_LogoutThenEof) {
    auto const r =
        run_held_cap(GetParam(), held_cap_scene::release_after_cap, "HeldCap/ReleaseAfterCap");
    ASSERT_TRUE(r.setup) << "setup: " << r.why;
    EXPECT_EQ(r.records_at_cap, 1U) << "no give-up decided at the cap";
    EXPECT_TRUE(r.ended) << "the session was not ended";
    EXPECT_EQ(r.end_state, fsm_state::Disconnected);
    EXPECT_TRUE(r.eof) << "the peer's read did not end in EOF";
    EXPECT_TRUE(pr::frames_of_type(r.after, "1").empty())
        << "a TestRequest after the give-up: " << r.after;
    auto const logouts = pr::frames_of_type(r.after, "5");
    ASSERT_EQ(logouts.size(), 1U) << r.after;
    EXPECT_EQ(pr::field_value(logouts.front(), "58"), "TestRequest unanswered, TestReqID " + r.tr1);
}

FIXPP_105_BOTH_ROLES(HeldTestRequestCap105);

}  // namespace
