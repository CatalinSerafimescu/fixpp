// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_101_validation_off_oracle.cpp
//
// 101-inbound-reject-fidelity, tasks.md T010 (W101-24, W101-25; spec US3 scenarios 5 and 6):
// research D6's differential oracle. On a validation-off session after establishment, a frame
// whose MsgTypes differ ends as the same frame with every later 35 removed and re-framed
// (`strip_later_35_and_reframe`): every outbound frame field for field, the callbacks, the state
// and NextNumIn (`describe`). A Reject the conflicting frame draws and its stripped twin does
// not is a Reject for the repeat, so the equality also says none is sent.
//
// The inputs are 099 data-model E-7 row `a`'s (`D…0`, `D…3`, `D…4`, `D…5`, `D…A`, `0…D`, `3…D`,
// `4…D`, `5…D`, `A…0`, `0,D`) plus `D…A…D`, each built on the cell's own session kind, in Active
// and in LogonReceived. An acceptor's Logon takes it through LogonReceived to Active within one
// on_inbound_frame call, except on that call's superseded exit, which
// returns in LogonReceived; the LogonReceived rows are delivered there.

#include <gtest/gtest.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include "session/session_engine_access.hpp"  // the engine-stop flag
#include "test_101_support.hpp"

namespace fixpp::session::test101 {
namespace {

struct Shape35 {
    const char* id;
    const char* first;
    std::function<std::string(Kind)> body;  // the fields after the header, later 35s included
    const char* before_34 = "";             // a later 35 adjacent to the first, in the header
};

std::vector<Shape35> shapes() {
    auto nos_then = [](const char* later) {
        return [later](Kind) { return t99::kNos() + "35=" + later + "\x01"; };
    };
    return {
        {"D..0", "D", nos_then("0")},
        {"D..3", "D", nos_then("3")},
        {"D..4", "D", nos_then("4")},
        {"D..5", "D", nos_then("5")},
        {"D..A", "D", nos_then("A")},
        {"0..D", "0",
         [](Kind) {
             return std::string(
                 "112=x\x01"
                 "35=D\x01");
         }},
        {"3..D", "3",
         [](Kind) {
             return std::string(
                 "45=1\x01"
                 "35=D\x01");
         }},
        {"4..D", "4",
         [](Kind) {
             return std::string(
                 "36=9\x01"
                 "35=D\x01");
         }},
        {"5..D", "5",
         [](Kind) {
             return std::string(
                 "58=x\x01"
                 "35=D\x01");
         }},
        {"A..0", "A", [](Kind k) { return logon_fields(k) + "35=0\x01"; }},
        {"0,D", "0", [](Kind) { return std::string(); }, "35=D\x01"},
        {"D..A..D", "D",
         [](Kind) {
             return t99::kNos() +
                    "35=A\x01"
                    "35=D\x01";
         }},
    };
}

struct OracleCell {
    Kind kind;
    Shape35 shape;
};

void PrintTo(const OracleCell& c, std::ostream* os) {
    *os << kind_name(c.kind) << " " << c.shape.id;
}

std::vector<OracleCell> oracle_cells() {
    std::vector<OracleCell> v;
    for (Kind k : {Kind::fix44, Kind::fixt}) {
        for (const Shape35& s : shapes()) {
            v.push_back({k, s});
        }
    }
    return v;
}

std::string cell_name(const testing::TestParamInfo<OracleCell>& info) {
    return std::string(kind_name(info.param.kind)) + "_" + t99::name_token(info.param.shape.id);
}

// ── W101-24: Active, validation off ─────────────────────────────────────────────────────

class OneMsgTypeOracle101 : public testing::TestWithParam<OracleCell> {};

TEST_P(OneMsgTypeOracle101, W101_24_ActiveOff_EndsAsTheStrippedTwin) {
    const OracleCell& c = GetParam();
    const Spec sp = spec(c.kind).off();
    auto make = [&c](const Rig101& r) {
        return r.frame(c.shape.first, 2, c.shape.body(c.kind), c.shape.before_34);
    };
    const Ending101 conflicting = ending_of(sp, Arm::Active, make);
    const Ending101 twin = ending_of(sp, Arm::Active, make, /*strip=*/true);
    EXPECT_EQ(describe(conflicting), describe(twin));
}

INSTANTIATE_TEST_SUITE_P(W101_24, OneMsgTypeOracle101, testing::ValuesIn(oracle_cells()),
                         cell_name);

// On the Logon call's ordinary exits the acceptor enters LogonReceived and leaves it inside the
// call that delivers the peer's Logon: the caller reads NotConnected before that call and Active
// after it, and the state is visible to a callback the call itself runs (the reply Logon's toAdmin
// samples it here). The call's superseded exit is the one that returns in LogonReceived; the cell
// after this one reaches it, and W101-24's LogonReceived rows below are delivered there.
TEST(OneMsgTypeOracle101Establishment, W101_24_AcceptorPassesLogonReceivedInsideTheLogonCall) {
    for (const Kind k : {Kind::fix44, Kind::fixt}) {
        SCOPED_TRACE(kind_name(k));
        auto rig = started(spec(k).off(), Arm::NotConnected);
        ASSERT_TRUE(rig);
        std::vector<fsm_state> seen_in_to_admin;
        rig->app->to_admin_probe = [&] { seen_in_to_admin.push_back(rig->session().state()); };
        const Ending101 e = rig->deliver(rig->peer_logon(1));
        EXPECT_EQ(e.before, fsm_state::NotConnected) << describe(e);
        EXPECT_EQ(e.after, fsm_state::Active) << describe(e);
        EXPECT_EQ(seen_in_to_admin, std::vector<fsm_state>{fsm_state::LogonReceived})
            << "the reply Logon's toAdmin runs inside the call, in LogonReceived";
        const auto h = rig->session().fsm_visit_history();
        EXPECT_NE(std::find(h.begin(), h.end(), fsm_state::LogonReceived), h.end());
        rig->app->to_admin_probe = nullptr;
        const Ending101 next = rig->deliver(rig->frame("D", 2, t99::kNos()));
        EXPECT_EQ(next.before, fsm_state::Active) << "the next frame is delivered in Active";
    }
}

// The Logon call's superseded exit. Engine::stop()'s step 1 sets the
// engine-stop flag on the session's strand; set here from the reply Logon's toAdmin, it is seen by
// the arm's next superseded check, which returns success without a state write, so the call
// returns with the session still in LogonReceived. The frame after it (the previous call has
// completed, so this is inside on_inbound_frame's contract) is delivered in LogonReceived and
// acted on: its MsgSeqNum is consumed.
std::unique_ptr<Rig101> left_in_logon_received(const Spec& sp) {
    auto rig = started(sp, Arm::NotConnected);
    if (!rig) {
        return nullptr;
    }
    rig->app->to_admin_probe = [r = rig.get()] {
        fixpp::session::session_engine_access::note_engine_stop(r->session());
    };
    const Ending101 logon = rig->deliver(rig->peer_logon(1));
    rig->app->to_admin_probe = nullptr;
    if (logon.after != fsm_state::LogonReceived ||
        !session_test_access::engine_stop_requested(rig->session())) {
        ADD_FAILURE() << "setup: the superseded exit did not leave LogonReceived: "
                      << describe(logon);
        return nullptr;
    }
    return rig;
}

TEST(OneMsgTypeOracle101Establishment, W101_24_SupersededLogonCall_NextFrameDeliveredInIt) {
    for (const Kind k : {Kind::fix44, Kind::fixt}) {
        SCOPED_TRACE(kind_name(k));
        auto rig = left_in_logon_received(spec(k).off());
        ASSERT_TRUE(rig);
        const Ending101 next = rig->deliver(rig->frame("D", 2, t99::kNos()));
        EXPECT_EQ(next.before, fsm_state::LogonReceived) << describe(next);
        EXPECT_EQ(next.next_in_after, next.next_in_before + 1)
            << "the arm acted on the frame: " << describe(next);
    }
}

// ── W101-24: LogonReceived, validation off (reached by the superseded exit above) ────────

class OneMsgTypeOracle101LogonReceived : public testing::TestWithParam<OracleCell> {};

Ending101 ending_in_logon_received(const Spec& sp, const OracleCell& c, bool strip) {
    auto rig = left_in_logon_received(sp);
    if (!rig) {
        Ending101 e;
        e.err = "setup failed";
        return e;
    }
    const std::string f = rig->frame(c.shape.first, 2, c.shape.body(c.kind), c.shape.before_34);
    return rig->deliver(strip ? strip_later_35_and_reframe(f) : f);
}

TEST_P(OneMsgTypeOracle101LogonReceived, W101_24_LogonReceivedOff_EndsAsTheStrippedTwin) {
    const OracleCell& c = GetParam();
    const Spec sp = spec(c.kind).off();
    const Ending101 conflicting = ending_in_logon_received(sp, c, /*strip=*/false);
    const Ending101 twin = ending_in_logon_received(sp, c, /*strip=*/true);
    EXPECT_EQ(conflicting.before, fsm_state::LogonReceived) << describe(conflicting);
    EXPECT_EQ(describe(conflicting), describe(twin));
}

INSTANTIATE_TEST_SUITE_P(W101_24, OneMsgTypeOracle101LogonReceived,
                         testing::ValuesIn(oracle_cells()), cell_name);

// ── W101-25: LogoutSent, validating and off, FIXT and non-FIXT ──────────────────────────

struct LogoutCell {
    Kind kind;
    bool validate;
};

void PrintTo(const LogoutCell& c, std::ostream* os) {
    *os << kind_name(c.kind) << (c.validate ? " on" : " off");
}

class OneMsgTypeOracle101LogoutSent : public testing::TestWithParam<LogoutCell> {};

// `D…5` is not the Logout reply: the session stays in LogoutSent, as the stripped twin does.
TEST_P(OneMsgTypeOracle101LogoutSent, W101_25_DThen5_IsNotTheLogoutReply) {
    const LogoutCell& c = GetParam();
    const Spec sp = spec(c.kind).validating(c.validate);
    auto make = [](const Rig101& r) { return r.frame("D", 2, t99::kNos() + "35=5\x01"); };
    const Ending101 e = ending_of(sp, Arm::LogoutSent, make);
    EXPECT_EQ(e.after, fsm_state::LogoutSent) << describe(e);
    EXPECT_TRUE(e.of_type("3").empty()) << "no Reject for the repeat: " << describe(e);
    EXPECT_EQ(describe(e), describe(ending_of(sp, Arm::LogoutSent, make, /*strip=*/true)));
}

// `5…D` is the Logout reply, as a Logout is.
TEST_P(OneMsgTypeOracle101LogoutSent, W101_25_5ThenD_IsTheLogoutReply) {
    const LogoutCell& c = GetParam();
    const Spec sp = spec(c.kind).validating(c.validate);
    auto make = [](const Rig101& r) {
        return r.frame("5", 2,
                       "58=x\x01"
                       "35=D\x01");
    };
    const Ending101 e = ending_of(sp, Arm::LogoutSent, make);
    EXPECT_EQ(e.after, fsm_state::Disconnected) << describe(e);
    EXPECT_TRUE(e.of_type("3").empty()) << "no Reject for the repeat: " << describe(e);
    EXPECT_EQ(describe(e), describe(ending_of(sp, Arm::LogoutSent, make, /*strip=*/true)));
}

INSTANTIATE_TEST_SUITE_P(
    W101_25, OneMsgTypeOracle101LogoutSent,
    testing::Values(LogoutCell{Kind::fix44, true}, LogoutCell{Kind::fix44, false},
                    LogoutCell{Kind::fixt, true}, LogoutCell{Kind::fixt, false}),
    [](const testing::TestParamInfo<LogoutCell>& info) {
        return std::string(kind_name(info.param.kind)) + (info.param.validate ? "_on" : "_off");
    });

}  // namespace
}  // namespace fixpp::session::test101
