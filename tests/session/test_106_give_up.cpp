// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_106_give_up.cpp — 106-logon-guards-write-bounds (tasks.md T003, T071 to T074).
//
// The give-up cells (research D3, D4, D9 rows FR-008, FR-009; contract C-3) that compile on the
// base. Each drives a direct Session (t106::GiveUpRig: HeartBtInt H = 10 s, test_request_threshold
// T = 10 s, logout_disconnect_timeout_ms B = 500 ms unless the cell names another, write_timeout_ms
// unset) to a state where the liveness loop's emit has not returned: its own write held, or queued
// at the write gate behind a held write, or held behind a resend answer, or held in its store call.
// The give-up instant is G = L + 3T + 1 ns, L the session's last inbound instant, computed by
// t106::give_up_instant_emit_not_returned (research D9, "Instants"); every instant is reached with
// step_to and a drain.
//
// Inequalities (research D9 row FR-008): the peer is silent after L, the limit is off, and 105's
// ending limit is armed only at the decision, so nothing but the give-up's cut closes by G + B. In
// TestRequestBlocked the TestRequest and Heartbeat instants tie at L + T and the loop emits the
// TestRequest; in HeartbeatBlocked the peer's frame at 5 s puts the Heartbeat (10 s) 5 s before the
// TestRequest (15 s); in every queued scene the loop's first emit after L falls due by L + T and
// queues behind the held write, which G follows by at least 2T.
//
// No helper here pumps with ASSERT_*: the rig is direct (no Engine), and every held write is
// released at teardown.

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/error.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <future>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "session/resend_gap_scene.hpp"
#include "session/test_105_support.hpp"
#include "session/test_106_support.hpp"
#include "support/session_executor_of.hpp"
#include "support/session_test_access.hpp"

namespace {

using namespace std::chrono_literals;
using fixpp::session::fsm_state;
using fixpp::session::session_role;
using fixpp::session::session_test_access;
using fixpp::session::SessionConfig;
namespace t105 = fixpp::test_support::t105;
namespace t106 = fixpp::test_support::t106;
namespace rgs = fixpp::test_support::resend_gap;
using ns = std::chrono::nanoseconds;
using expected_void = fixpp::core::expected_t<void>;

constexpr std::string_view kGiveUpText = "TestRequest unanswered";
constexpr std::string_view kGiveUpTextPrefix = "TestRequest unanswered, TestReqID ";

enum class scene : std::uint8_t {
    test_request_blocked,     // the loop's TestRequest write held
    heartbeat_blocked,        // the loop's Heartbeat write held, no TestRequest pending
    behind_blocked_send,      // an application send's write held; the loop's emit queued
    resend_held_no_pending,   // a resend answer's GapFill write held; the TestRequest held behind
    reject_blocked,           // a Reject written inside on_inbound_frame held; the emit queued
    heartbeat_reply_blocked,  // the Heartbeat answering the peer's TestRequest held; emit queued
    business_reject_blocked,  // the BusinessMessageReject answering a NewOrderSingle held; queued
};

[[nodiscard]] const char* scene_name(scene s) {
    switch (s) {
        case scene::test_request_blocked:
            return "TestRequestBlocked";
        case scene::heartbeat_blocked:
            return "HeartbeatBlocked";
        case scene::behind_blocked_send:
            return "BehindBlockedSend";
        case scene::resend_held_no_pending:
            return "ResendHeldNoPending";
        case scene::reject_blocked:
            return "RejectBlocked";
        case scene::heartbeat_reply_blocked:
            return "HeartbeatReplyBlocked";
        case scene::business_reject_blocked:
            return "BusinessRejectBlocked";
    }
    return "?";
}

// What a scene leaves for the readings.
struct Staged {
    bool ok = false;
    std::string why;
    fixpp::core::steady_time_point last_inbound{};
    std::optional<std::future<expected_void>> blocked_call;  // the held send or delivery
    std::string blocked_type;                                // the held write's MsgType(35)
    bool loop_emit_queued = false;  // the loop's emit waits behind the held write
    bool test_request_pending = true;
};

void tweak_for(scene s, SessionConfig& c) {
    if (s == scene::reject_blocked) {
        c.dictionary = fixpp::test_support::make_validation_test_dictionary();
        c.validate_inbound_messages = true;
    }
}

// Reaches Active at t = 0 and stages the scene; returns what the readings need. The mock clock is
// at the scene's last stage when it returns.
Staged stage(t106::GiveUpRig& r, scene s) {
    Staged st;
    auto& sc = r.d.s;
    // The BusinessMessageReject scene: fromApp refuses every application message, so the session
    // answers it with a BusinessMessageReject (35=j; on_inbound_active_from_app_failed_).
    if (s == scene::business_reject_blocked) r.app->refuse_app = true;
    if (!r.d.to_active()) {
        st.why = "not Active";
        return st;
    }
    auto const t0 = r.now();
    auto const hold_and_feed = [&](std::vector<std::byte> f, std::string type) {
        st.blocked_type = std::move(type);
        st.blocked_call.emplace(sc.feed_spawn(std::move(f)));
        if (!sc.pump_until([&] { return r.d.parked(); })) {
            st.why = "the " + st.blocked_type + " write was not held";
            return false;
        }
        return true;
    };
    switch (s) {
        case scene::test_request_blocked:
            r.d.hold_write_of("1");
            st.blocked_type = "1";
            break;
        case scene::heartbeat_blocked:
            r.at(t0 + 5s);
            (void)sc.feed(sc.heartbeat(2));
            r.d.hold_write_of("0");
            st.blocked_type = "0";
            st.test_request_pending = false;
            break;
        case scene::behind_blocked_send: {
            r.d.hold_next_write();
            static constexpr std::string_view kPayload =
                "35=D\x01"
                "11=Q106\x01";
            st.blocked_type = "D";
            st.blocked_call.emplace(asio::co_spawn(
                fixpp::test_support::session_executor_of(*sc.session, sc.ioc),
                sc.session->send(std::as_bytes(std::span{kPayload.data(), kPayload.size()})),
                asio::use_future));
            if (!sc.pump_until([&] { return r.d.parked(); })) {
                st.why = "the send's write was not held";
                return st;
            }
            st.loop_emit_queued = true;
            break;
        }
        case scene::resend_held_no_pending:
            r.d.hold_write_of("4");
            if (!hold_and_feed(sc.resend_request(2), "4")) return st;
            st.test_request_pending = false;
            break;
        case scene::reject_blocked:
            r.d.hold_write_of("3");
            if (!hold_and_feed(sc.invalid_order(2), "3")) return st;
            st.loop_emit_queued = true;
            break;
        case scene::heartbeat_reply_blocked:
            r.d.hold_write_of("0");
            if (!hold_and_feed(sc.test_request(2), "0")) return st;
            st.loop_emit_queued = true;
            break;
        case scene::business_reject_blocked:
            r.d.hold_next_write();
            if (!hold_and_feed(sc.order(2), "j")) return st;
            st.loop_emit_queued = true;
            break;
    }
    st.last_inbound = r.last_inbound();
    // The loop's first instant after L: L + T (the TestRequest; in HeartbeatBlocked the Heartbeat
    // at 10 s, the last outbound instant + H).
    auto const first_emit = s == scene::heartbeat_blocked
                                ? t0 + r.p.heartbeat
                                : t106::saturating_add(st.last_inbound, r.threshold());
    r.at(first_emit);
    if (s == scene::test_request_blocked || s == scene::heartbeat_blocked) {
        if (r.d.started(st.blocked_type) != 1U || !r.d.parked()) {
            st.why = "the loop's " + st.blocked_type + " write was not held";
            return st;
        }
    } else if (st.loop_emit_queued) {
        if (r.app->to_admin_count("1") != 1 || r.d.started("1") != 0U) {
            st.why = "the loop's TestRequest is not queued behind the held write";
            return st;
        }
    } else if (s == scene::resend_held_no_pending) {
        if (r.d.app->to_admin_count("1") != 0 || !r.d.parked()) {
            st.why = "the TestRequest is not held behind the resend answer";
            return st;
        }
    }
    st.ok = true;
    return st;
}

class GiveUp106 : public ::testing::TestWithParam<session_role> {};

[[nodiscard]] std::string role_name(::testing::TestParamInfo<session_role> const& i) {
    return i.param == session_role::acceptor ? "Acceptor" : "Initiator";
}

// The closed arm (tasks.md T071, T071a, T071b): no slot-74 record at G − 1 ns and one at G; open
// at G + B − 1 ns and closed at G + B; no complete Logout; each held and queued write ends with an
// error (FR-009); FR-017: one new Disconnected entry, onLogout once, one slot-74 record.
void expect_closed_at_g_plus_b(session_role role, scene s) {
    t106::GiveUpRig r{role, t106::GiveUpParams{}, [s](SessionConfig& c) { tweak_for(s, c); }};
    Staged st = stage(r, s);
    ASSERT_TRUE(st.ok) << scene_name(s) << ": setup: " << st.why;
    auto const disconnected_before = r.disconnected();
    auto const logouts_before = r.app->logouts;
    auto const g = t106::give_up_instant_emit_not_returned(st.last_inbound, r.threshold());
    auto const b = std::chrono::milliseconds{r.p.logout_ms};

    r.at(g - t106::kInstantGuard);
    EXPECT_EQ(r.records(), 0U) << scene_name(s) << ": a give-up record before G";
    EXPECT_FALSE(r.d.closed()) << scene_name(s) << ": closed before G";
    r.at(g);
    EXPECT_EQ(r.records(), 1U) << scene_name(s) << ": no give-up record at G";
    r.at(g + b - t106::kInstantGuard);
    EXPECT_FALSE(r.d.closed()) << scene_name(s) << ": closed before G + B";
    r.at(g + b);
    EXPECT_TRUE(r.d.closed()) << scene_name(s) << ": not closed at G + B";

    EXPECT_EQ(r.completed_since("5", 0), 0U) << scene_name(s) << ": a complete Logout";
    EXPECT_EQ(r.completed_since(st.blocked_type, 0), 0U)
        << scene_name(s) << ": the held write completed";
    if (st.loop_emit_queued) {
        EXPECT_EQ(r.completed_since("1", 0), 0U) << scene_name(s) << ": the queued emit completed";
    }
    if (st.blocked_call) {
        EXPECT_TRUE(rgs::Scene::ready(*st.blocked_call))
            << scene_name(s) << ": the held call did not return";
        if (s == scene::behind_blocked_send && rgs::Scene::ready(*st.blocked_call)) {
            auto const sr = st.blocked_call->get();
            EXPECT_FALSE(sr.has_value()) << scene_name(s) << ": the held send succeeded";
            if (!sr) {
                EXPECT_EQ(sr.error(), fixpp::core::error::dispatch_aborted) << scene_name(s);
            }
        }
    }
    EXPECT_EQ(r.d.s.state(), fsm_state::Disconnected) << scene_name(s);
    EXPECT_EQ(r.disconnected() - disconnected_before, 1U)
        << scene_name(s) << ": new Disconnected entries";
    EXPECT_EQ(r.app->logouts - logouts_before, 1) << scene_name(s) << ": onLogout calls";
    EXPECT_EQ(r.records(), 1U) << scene_name(s) << ": give-up records";
}

// The twin arm: the peer reads again inside B, so the held write completes, then whatever queued
// behind it, then the give-up's Logout, last, its Text naming the pending TestRequest or, with none
// pending, without the TestReqID clause.
void expect_logout_when_read_resumes(session_role role, scene s) {
    t106::GiveUpRig r{role, t106::GiveUpParams{}, [s](SessionConfig& c) { tweak_for(s, c); }};
    Staged st = stage(r, s);
    ASSERT_TRUE(st.ok) << scene_name(s) << ": setup: " << st.why;
    auto const g = t106::give_up_instant_emit_not_returned(st.last_inbound, r.threshold());
    auto const b = std::chrono::milliseconds{r.p.logout_ms};
    auto const mark = r.d.bw->recorded.size() > 0 ? r.d.bw->recorded.size() - 1 : 0;

    r.at(g);
    EXPECT_EQ(r.records(), 1U) << scene_name(s) << ": no give-up record at G";
    r.at(g + b / 2);
    r.d.release();
    r.d.s.drain();
    auto const types = r.completed_types(mark);
    ASSERT_FALSE(types.empty()) << scene_name(s) << ": nothing completed after the release";
    EXPECT_EQ(types.back(), "5") << scene_name(s) << ": the Logout is not the last frame";
    EXPECT_EQ(r.completed_since("5", 0), 1U) << scene_name(s) << ": Logouts written";
    EXPECT_GE(r.completed_since(st.blocked_type, 0), 1U)
        << scene_name(s) << ": the held write did not complete before the Logout";
    std::string const text = r.completed_field("5", 58);
    if (st.test_request_pending) {
        EXPECT_EQ(text, std::string{kGiveUpTextPrefix} + r.completed_field("1", 112))
            << scene_name(s);
    } else {
        EXPECT_EQ(text, kGiveUpText) << scene_name(s);
    }
}

// tasks.md T071: research D9 row FR-008 (and FR-009, FR-017).
TEST_P(GiveUp106, TestRequestBlocked) {
    expect_closed_at_g_plus_b(GetParam(), scene::test_request_blocked);
}
TEST_P(GiveUp106, TestRequestBlocked_ReadResumedInsideB) {
    expect_logout_when_read_resumes(GetParam(), scene::test_request_blocked);
}
TEST_P(GiveUp106, HeartbeatBlocked) {
    expect_closed_at_g_plus_b(GetParam(), scene::heartbeat_blocked);
}
TEST_P(GiveUp106, HeartbeatBlocked_ReadResumedInsideB) {
    expect_logout_when_read_resumes(GetParam(), scene::heartbeat_blocked);
}
TEST_P(GiveUp106, BehindBlockedSend) {
    expect_closed_at_g_plus_b(GetParam(), scene::behind_blocked_send);
}
TEST_P(GiveUp106, BehindBlockedSend_ReadResumedInsideB) {
    expect_logout_when_read_resumes(GetParam(), scene::behind_blocked_send);
}
// With none pending, the slot-74 record's test_req_id is empty and the Text has no TestReqID
// clause (Q2; data-model E-4 row 5).
TEST_P(GiveUp106, ResendHeldNoPending) {
    expect_closed_at_g_plus_b(GetParam(), scene::resend_held_no_pending);
}
TEST_P(GiveUp106, ResendHeldNoPending_ReadResumedInsideB) {
    expect_logout_when_read_resumes(GetParam(), scene::resend_held_no_pending);
}
TEST_P(GiveUp106, ResendHeldNoPending_RecordNamesNoTestRequest) {
    t106::GiveUpRig r{GetParam(), t106::GiveUpParams{}};
    Staged st = stage(r, scene::resend_held_no_pending);
    ASSERT_TRUE(st.ok) << "setup: " << st.why;
    r.at(t106::give_up_instant_emit_not_returned(st.last_inbound, r.threshold()));
    ASSERT_EQ(r.records(), 1U) << "no give-up record at G";
    EXPECT_EQ(t106::give_up_record_test_req_id(r.log), "") << "the record's test_req_id";
}
TEST_P(GiveUp106, RejectBlocked) { expect_closed_at_g_plus_b(GetParam(), scene::reject_blocked); }
TEST_P(GiveUp106, RejectBlocked_ReadResumedInsideB) {
    expect_logout_when_read_resumes(GetParam(), scene::reject_blocked);
}
// tasks.md T071a, T071b: the two reachable leaves of T015's table.
TEST_P(GiveUp106, HeartbeatReplyBlocked) {
    expect_closed_at_g_plus_b(GetParam(), scene::heartbeat_reply_blocked);
}
TEST_P(GiveUp106, BusinessRejectBlocked) {
    expect_closed_at_g_plus_b(GetParam(), scene::business_reject_blocked);
}

// [guard] ResendHeldNoPending's pending twin: a TestRequest written at 10 s, then the peer's
// ResendRequest at 11 s, whose GapFill write is held, so the next TestRequest falls due at L + T
// behind the answer with one pending; 105's cap gives it up at L + 3T + 1 ns, on the base too.
TEST_P(GiveUp106, ResendHeldPending) {
    t106::GiveUpRig r{GetParam(), t106::GiveUpParams{}};
    ASSERT_TRUE(r.d.to_active()) << "setup";
    auto const t0 = r.now();
    r.at(t0 + r.threshold());
    ASSERT_EQ(r.d.completed("1"), 1U) << "setup: no TestRequest at T";
    r.at(t0 + r.threshold() + 1s);
    r.d.hold_write_of("4");
    auto fut = r.d.s.feed_spawn(r.d.s.resend_request(2));
    ASSERT_TRUE(r.d.s.pump_until([&] { return r.d.parked(); })) << "setup: the GapFill not held";
    auto const l = r.last_inbound();
    auto const g = t106::give_up_instant_emit_not_returned(l, r.threshold());
    auto const b = std::chrono::milliseconds{r.p.logout_ms};
    r.at(g - t106::kInstantGuard);
    EXPECT_EQ(r.records(), 0U) << "a give-up record before G";
    r.at(g);
    EXPECT_EQ(r.records(), 1U) << "no give-up record at G";
    r.at(g + b);
    EXPECT_TRUE(r.d.closed()) << "not closed at G + B";
    EXPECT_TRUE(rgs::Scene::ready(fut)) << "the held answer did not return";
}

// tasks.md T072 (research D9 row "FR-008 spawn failure"): RejectBlocked's rig with 105's ending
// bound refused. At G: one slot-74 record, the transport closed, one Disconnected entry, the held
// Reject's delivery returned and the queued emit not written, no Logout; at G + B + 1 s nothing
// more (105's ending bound never ran, so no cut competes).
TEST_P(GiveUp106, RejectBlocked_EndingBoundSpawnFails_ClosedAtG) {
    t106::GiveUpRig r{GetParam(), t106::GiveUpParams{},
                      [](SessionConfig& c) { tweak_for(scene::reject_blocked, c); }};
    session_test_access::set_ending_bound_spawn_fails(r.session(), [] { return true; });
    Staged st = stage(r, scene::reject_blocked);
    ASSERT_TRUE(st.ok) << "setup: " << st.why;
    auto const disconnected_before = r.disconnected();
    auto const g = t106::give_up_instant_emit_not_returned(st.last_inbound, r.threshold());
    auto const b = std::chrono::milliseconds{r.p.logout_ms};
    r.at(g - t106::kInstantGuard);
    EXPECT_EQ(r.records(), 0U) << "a give-up record before G";
    EXPECT_FALSE(r.d.closed()) << "closed before G";
    r.at(g);
    EXPECT_EQ(r.records(), 1U) << "no give-up record at G";
    EXPECT_TRUE(r.d.closed()) << "not closed at G";
    EXPECT_EQ(r.disconnected() - disconnected_before, 1U) << "new Disconnected entries at G";
    EXPECT_TRUE(st.blocked_call && rgs::Scene::ready(*st.blocked_call))
        << "the held Reject's delivery did not return";
    EXPECT_EQ(r.completed_since("1", 0), 0U) << "the queued emit was written";
    EXPECT_EQ(r.completed_since("5", 0), 0U) << "a Logout";
    r.at(g + b + 1s);
    EXPECT_EQ(r.records(), 1U) << "give-up records";
    EXPECT_EQ(r.disconnected() - disconnected_before, 1U) << "new Disconnected entries";
}

// tasks.md T073 (research D9 row "FR-008 store phase"; contract C-3.3's exception): the loop's
// TestRequest held inside its MessageStore::store call. The give-up is decided at G and the
// transport closed at G + B; nothing records Disconnected until the store returns (L-093-12), so
// the entries stay as they were. The store is released at teardown.
TEST_P(GiveUp106, EmitHeldInStore_GivesUpAtG_ClosesAtGPlusB) {
    auto gate = std::make_shared<t105::gate_store_state>();
    t106::GiveUpRig r{GetParam(), t106::GiveUpParams{}, [gate](SessionConfig& c) {
                          c.store_factory = std::make_shared<t105::GateStoreFactory>(gate);
                      }};
    ASSERT_TRUE(r.d.to_active()) << "setup";
    auto const t0 = r.now();
    gate->hold_when = [](t105::store_call k, fixpp::session::seqnum_t) {
        return k == t105::store_call::store_outbound;
    };
    r.at(t0 + r.threshold());
    ASSERT_EQ(gate->parked, 1U) << "setup: the TestRequest's store call is not held";
    auto const disconnected_before = r.disconnected();
    auto const g = t106::give_up_instant_emit_not_returned(r.last_inbound(), r.threshold());
    auto const b = std::chrono::milliseconds{r.p.logout_ms};
    r.at(g - t106::kInstantGuard);
    EXPECT_EQ(r.records(), 0U) << "a give-up record before G";
    r.at(g);
    EXPECT_EQ(r.records(), 1U) << "no give-up record at G";
    r.at(g + b - t106::kInstantGuard);
    EXPECT_FALSE(r.d.closed()) << "closed before G + B";
    r.at(g + b);
    EXPECT_TRUE(r.d.closed()) << "not closed at G + B";
    EXPECT_EQ(r.disconnected(), disconnected_before) << "Disconnected before the store returned";
    gate->hold_when = nullptr;
    gate->release_all();
    r.d.s.drain();
}

// spec.md R23 (FR-009, contract C-3.3): BehindBlockedSend, with the close at G + B meeting a
// close_async that throws before it closes anything. The synchronous close the close frame falls
// back to closes the transport at G + B, and the held send returns dispatch_aborted.
TEST_P(GiveUp106, SendBlocked_CloseAsyncThrows_ClosedAtGPlusB) {
    t106::GiveUpRig r{GetParam(), t106::GiveUpParams{}};
    r.d.bw->close_async_throws_when = [](std::size_t call) { return call == 1; };
    Staged st = stage(r, scene::behind_blocked_send);
    ASSERT_TRUE(st.ok) << "setup: " << st.why;
    auto const disconnected_before = r.disconnected();
    auto const g = t106::give_up_instant_emit_not_returned(st.last_inbound, r.threshold());
    auto const b = std::chrono::milliseconds{r.p.logout_ms};
    r.at(g);
    EXPECT_EQ(r.records(), 1U) << "no give-up record at G";
    r.at(g + b - t106::kInstantGuard);
    EXPECT_FALSE(r.d.closed()) << "closed before G + B";
    EXPECT_EQ(r.d.bw->close_async_calls, 0U) << "a close before G + B";
    r.at(g + b);
    // The first close is made during the first close_async call, the one that throws before it
    // closes anything, so only the catch's fallback can have made it.
    EXPECT_EQ(r.d.bw->close_async_calls_at_first_close, std::optional<std::size_t>{1})
        << "the transport was not first closed by the throwing close_async's fallback";
    EXPECT_TRUE(r.d.closed()) << "not closed at G + B";
    bool const returned = st.blocked_call && rgs::Scene::ready(*st.blocked_call);
    EXPECT_TRUE(returned) << "the held send did not return";
    if (returned) {
        auto const sr = st.blocked_call->get();
        EXPECT_FALSE(sr.has_value()) << "the held send succeeded";
        if (!sr) EXPECT_EQ(sr.error(), fixpp::core::error::dispatch_aborted);
    }
    EXPECT_EQ(r.d.s.state(), fsm_state::Disconnected);
    EXPECT_EQ(r.disconnected() - disconnected_before, 1U) << "new Disconnected entries";
}

// tasks.md T074, the disclosure guard L-105-f: B = 0, so the give-up's ending is unlimited and
// nothing closes. Still open at G + 10 s, which follows G + 2000 ms (the default B a regression
// ignoring the 0 would use) by 8 s. [guard: an unconditional close at G]
TEST_P(GiveUp106, ZeroLogoutTimeout_StaysOpen) {
    t106::GiveUpParams p;
    p.logout_ms = 0;
    t106::GiveUpRig r{GetParam(), p};
    Staged st = stage(r, scene::test_request_blocked);
    ASSERT_TRUE(st.ok) << "setup: " << st.why;
    auto const g = t106::give_up_instant_emit_not_returned(st.last_inbound, r.threshold());
    r.at(g + 10s);
    EXPECT_FALSE(r.d.closed()) << "closed with logout_disconnect_timeout_ms 0";
}

INSTANTIATE_TEST_SUITE_P(BothRoles, GiveUp106,
                         ::testing::Values(session_role::acceptor, session_role::initiator),
                         role_name);

}  // namespace
