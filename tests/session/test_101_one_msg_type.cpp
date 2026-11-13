// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_101_one_msg_type.cpp
//
// 101-inbound-reject-fidelity, tasks.md T009 (W101-20 to W101-23, W101-26 to W101-28, W101-32;
// spec US3 scenarios 1 to 4, 7, 8 and its edge cases): a frame has one MsgType, its first 35.
// A later 35 whose value differs is a conflict: a disconnect in establishment on every session,
// a Reject after it on every validating session, 373 by research D5's three-clause rule (14 when
// a later 35 lies past the header, else 13 when one lies in it). The expected endings are
// contracts/reject-frames.md's "After" column.
//
// The controls of W101-27 assert, as literals, the endings the evidence file records for them
// (tasks.md T020): spec US3 scenario 8's "handling is unchanged".

#include <gtest/gtest.h>

#include <string>
#include <string_view>

#include "test_101_support.hpp"

namespace fixpp::session::test101 {
namespace {

std::string_view text_of(std::string_view reason) {
    return reason == "13" ? kTextTagMoreThanOnce : kTextOutOfOrder;
}

// The one Reject a repeated MsgType draws after establishment: 371=35, 372 = the first 35,
// 45 = the first 34, 58 = the reason's text; the frame consumed; nothing to fromApp.
void expect_conflict_reject(const Ending101& e, std::string_view reason,
                            std::string_view first_msg_type, unsigned first_34) {
    SCOPED_TRACE(describe(e));
    ASSERT_EQ(e.out.size(), 1U) << "exactly one frame sent";
    const Decoded& rj = e.out.front();
    EXPECT_EQ(rj.get(35), "3");
    EXPECT_EQ(rj.get(373), reason) << "SessionRejectReason(373)";
    EXPECT_EQ(rj.get(371), "35") << "RefTagID(371)";
    EXPECT_EQ(rj.get(372), first_msg_type) << "RefMsgType(372): the first 35";
    EXPECT_EQ(rj.get(45), std::to_string(first_34)) << "RefSeqNum(45): the first 34";
    EXPECT_EQ(rj.get(58), text_of(reason)) << "Text(58)";
    EXPECT_EQ(e.next_in_after, e.next_in_before + 1) << "consumed";
    EXPECT_EQ(e.after, fsm_state::Active) << "not logged out";
    EXPECT_EQ(e.from_app, 0) << "nothing reaches fromApp";
}

// The establishment refusal: Disconnected, nothing sent, the inbound number not advanced.
void expect_refused(const Ending101& e) {
    SCOPED_TRACE(describe(e));
    EXPECT_EQ(e.after, fsm_state::Disconnected);
    EXPECT_TRUE(e.out.empty()) << "nothing sent";
    EXPECT_EQ(e.next_in_after, e.next_in_before) << "the inbound number not advanced";
    EXPECT_EQ(e.from_app + e.from_admin, 0);
}

// ── W101-20: US3 scenario 1, a validation-off acceptor before Logon ─────────────────────

void w101_20(Kind k) {
    SCOPED_TRACE(kind_name(k));
    auto rig = started(spec(k).off(), Arm::NotConnected);
    ASSERT_TRUE(rig);
    expect_refused(rig->deliver(rig->frame("D", 1, logon_fields(k) + "35=A\x01")));
}
TEST(OneMsgType101, W101_20_ValidationOffAcceptor_DThenA_Fix44) { w101_20(Kind::fix44); }
TEST(OneMsgType101, W101_20_ValidationOffAcceptor_DThenA_Fixt) { w101_20(Kind::fixt); }

// ── W101-21: US3 scenario 2, a validation-off initiator awaiting its Logon reply ────────

void w101_21(Kind k, const char* first, const char* later) {
    SCOPED_TRACE(std::string(kind_name(k)) + " " + first + ".." + later);
    auto rig = started(spec(k).off().initiator(), Arm::LogonSent);
    ASSERT_TRUE(rig);
    expect_refused(rig->deliver(rig->frame(first, 1, logon_fields(k) + "35=" + later + "\x01")));
}
TEST(OneMsgType101, W101_21_ValidationOffInitiator_AThenD_Fix44) { w101_21(Kind::fix44, "A", "D"); }
TEST(OneMsgType101, W101_21_ValidationOffInitiator_DThenA_Fix44) { w101_21(Kind::fix44, "D", "A"); }
TEST(OneMsgType101, W101_21_ValidationOffInitiator_AThenD_Fixt) { w101_21(Kind::fixt, "A", "D"); }
TEST(OneMsgType101, W101_21_ValidationOffInitiator_DThenA_Fixt) { w101_21(Kind::fixt, "D", "A"); }

// ── W101-22: US3 scenario 3, a validating non-FIXT session in Active ────────────────────

TEST(OneMsgType101, W101_22_NonFixtActive_0ThenDPastHeader_Reject14) {
    auto rig = started(spec(Kind::fix44), Arm::Active);
    ASSERT_TRUE(rig);
    expect_conflict_reject(rig->deliver(rig->frame("0", 2,
                                                   "112=x\x01"
                                                   "35=D\x01")),
                           "14", "0", 2);
}
TEST(OneMsgType101, W101_22_NonFixtActive_0DAdjacentInHeader_Reject13) {
    auto rig = started(spec(Kind::fix44), Arm::Active);
    ASSERT_TRUE(rig);
    expect_conflict_reject(rig->deliver(rig->frame("0", 2, {}, "35=D\x01")), "13", "0", 2);
}
// Two 34s: 45 is the first one's value.
TEST(OneMsgType101, W101_22_NonFixtActive_Two34s_RefSeqNumIsTheFirst) {
    auto rig = started(spec(Kind::fix44), Arm::Active);
    ASSERT_TRUE(rig);
    expect_conflict_reject(rig->deliver(rig->frame("0", 2,
                                                   "112=x\x01"
                                                   "34=9\x01"
                                                   "35=D\x01")),
                           "14", "0", 2);
}

// ── W101-23: US3 scenario 4, a validating non-FIXT acceptor before Logon ────────────────

void w101_23(const std::string& name, const std::function<std::string(const Rig101&)>& make) {
    SCOPED_TRACE(name);
    auto rig = started(spec(Kind::fix44), Arm::NotConnected);
    ASSERT_TRUE(rig);
    expect_refused(rig->deliver(make(*rig)));
}
TEST(OneMsgType101, W101_23_NonFixtAcceptor_0ThenDPastHeader) {
    w101_23("0..D", [](const Rig101& r) {
        return r.frame("0", 1,
                       "112=x\x01"
                       "35=D\x01");
    });
}
TEST(OneMsgType101, W101_23_NonFixtAcceptor_0DAdjacentInHeader) {
    w101_23("0,D", [](const Rig101& r) { return r.frame("0", 1, {}, "35=D\x01"); });
}
TEST(OneMsgType101, W101_23_NonFixtAcceptor_DThenAWithLogonFields) {
    w101_23("D..A+L",
            [](const Rig101& r) { return r.frame("D", 1, t99::kLogonBody() + "35=A\x01"); });
}
TEST(OneMsgType101, W101_23_NonFixtAcceptor_0ThenAWithLogonFields) {
    w101_23("0..A+L",
            [](const Rig101& r) { return r.frame("0", 1, t99::kLogonBody() + "35=A\x01"); });
}

// ── W101-26: US3 scenario 7, three or more 35s ──────────────────────────────────────────

void w101_26_active(Kind k, const std::string& name, const std::string& before_34,
                    const std::string& body, std::string_view reason,
                    std::string_view first_msg_type) {
    SCOPED_TRACE(std::string(kind_name(k)) + " " + name);
    auto rig = started(spec(k), Arm::Active);
    ASSERT_TRUE(rig);
    expect_conflict_reject(
        rig->deliver(rig->frame(std::string(first_msg_type), 2, body, before_34)), reason,
        first_msg_type, 2);
}
// `D…A…D`: the last equals the first; the A between them differs (sticky).
TEST(OneMsgType101, W101_26_DThenAThenD_ValidatingActive_Fix44) {
    w101_26_active(Kind::fix44, "D..A..D", {},
                   t99::kNos() +
                       "35=A\x01"
                       "35=D\x01",
                   "14", "D");
}
TEST(OneMsgType101, W101_26_DThenAThenD_ValidatingActive_Fixt) {
    w101_26_active(Kind::fixt, "D..A..D", {},
                   t99::kNos() +
                       "35=A\x01"
                       "35=D\x01",
                   "14", "D");
}
// `0,D(hdr)…0(past)` and `0,0(hdr)…D(past)`: a later 35 past the header gives 14.
TEST(OneMsgType101, W101_26_0D_Header_0_Past_Reject14_Fix44) {
    w101_26_active(Kind::fix44, "0,D(hdr)..0(past)", "35=D\x01",
                   "112=x\x01"
                   "35=0\x01",
                   "14", "0");
}
TEST(OneMsgType101, W101_26_0D_Header_0_Past_Reject14_Fixt) {
    w101_26_active(Kind::fixt, "0,D(hdr)..0(past)", "35=D\x01",
                   "112=x\x01"
                   "35=0\x01",
                   "14", "0");
}
TEST(OneMsgType101, W101_26_00_Header_D_Past_Reject14_Fix44) {
    w101_26_active(Kind::fix44, "0,0(hdr)..D(past)", "35=0\x01",
                   "112=x\x01"
                   "35=D\x01",
                   "14", "0");
}
TEST(OneMsgType101, W101_26_00_Header_D_Past_Reject14_Fixt) {
    w101_26_active(Kind::fixt, "0,0(hdr)..D(past)", "35=0\x01",
                   "112=x\x01"
                   "35=D\x01",
                   "14", "0");
}

// `A…D…A` with Logon fields in both establishment roles: a non-FIXT validating session and
// validation-off sessions of both kinds.
void w101_26_establish(Kind k, bool validate, session_role role) {
    const bool acceptor = role == session_role::acceptor;
    SCOPED_TRACE(std::string(kind_name(k)) + (validate ? " on " : " off ") +
                 (acceptor ? "acceptor" : "initiator"));
    auto rig = started(spec(k).validating(validate).as(role),
                       acceptor ? Arm::NotConnected : Arm::LogonSent);
    ASSERT_TRUE(rig);
    expect_refused(rig->deliver(rig->frame("A", 1,
                                           logon_fields(k) + "35=D\x01"
                                                             "35=A\x01")));
}
TEST(OneMsgType101, W101_26_AThenDThenA_Fix44Validating_Acceptor) {
    w101_26_establish(Kind::fix44, true, session_role::acceptor);
}
TEST(OneMsgType101, W101_26_AThenDThenA_Fix44Validating_Initiator) {
    w101_26_establish(Kind::fix44, true, session_role::initiator);
}
TEST(OneMsgType101, W101_26_AThenDThenA_Fix44Off_Acceptor) {
    w101_26_establish(Kind::fix44, false, session_role::acceptor);
}
TEST(OneMsgType101, W101_26_AThenDThenA_Fix44Off_Initiator) {
    w101_26_establish(Kind::fix44, false, session_role::initiator);
}
TEST(OneMsgType101, W101_26_AThenDThenA_FixtOff_Acceptor) {
    w101_26_establish(Kind::fixt, false, session_role::acceptor);
}
TEST(OneMsgType101, W101_26_AThenDThenA_FixtOff_Initiator) {
    w101_26_establish(Kind::fixt, false, session_role::initiator);
}

// ── W101-27: US3 scenario 8, controls (the recorded endings, unchanged) ──────────────────

// The ending of one frame on a fresh session, as `describe` prints it.
std::string control(const Spec& sp, Arm arm,
                    const std::function<std::string(const Rig101&)>& make) {
    return describe(ending_of(sp, arm, make));
}

// One MsgType in every affected arm.
TEST(OneMsgType101, W101_27_OneMsgType_Fix44Validating) {
    EXPECT_EQ(
        control(spec(Kind::fix44), Arm::NotConnected,
                [](const Rig101& r) { return r.peer_logon(1); }),
        "NotConnected->Active | in 1->2 | out# 1->2 | fromApp=0 fromAdmin=0 toAdmin=1 toApp=0 | "
        "sent: {35=A 34=1 45=<absent> 58=<absent> 371=<absent> 372=<absent> 373=<absent>}");
    EXPECT_EQ(control(spec(Kind::fix44).initiator(), Arm::LogonSent,
                      [](const Rig101& r) { return r.peer_logon(1); }),
              "LogonSent->Active | in 1->2 | out# 2->2 | fromApp=0 fromAdmin=0 toAdmin=0 toApp=0 | "
              "sent: none");
    EXPECT_EQ(control(spec(Kind::fix44), Arm::Active,
                      [](const Rig101& r) { return r.frame("D", 2, t99::kNos()); }),
              "Active->Active | in 2->3 | out# 2->2 | fromApp=1 fromAdmin=0 toAdmin=0 toApp=0 | "
              "sent: none");
    EXPECT_EQ(control(spec(Kind::fix44), Arm::LogoutSent,
                      [](const Rig101& r) { return r.frame("5", 2); }),
              "LogoutSent->Disconnected | in 2->2 | out# 3->3 | fromApp=0 fromAdmin=0 toAdmin=0 "
              "toApp=0 | sent: none");
}
TEST(OneMsgType101, W101_27_OneMsgType_FixtOff) {
    const Spec off = spec(Kind::fixt).off();
    EXPECT_EQ(
        control(off, Arm::NotConnected, [](const Rig101& r) { return r.peer_logon(1); }),
        "NotConnected->Active | in 1->2 | out# 1->2 | fromApp=0 fromAdmin=0 toAdmin=1 toApp=0 | "
        "sent: {35=A 34=1 45=<absent> 58=<absent> 371=<absent> 372=<absent> 373=<absent>}");
    EXPECT_EQ(
        control(off.initiator(), Arm::LogonSent, [](const Rig101& r) { return r.peer_logon(1); }),
        "LogonSent->Active | in 1->2 | out# 2->2 | fromApp=0 fromAdmin=0 toAdmin=0 toApp=0 | sent: "
        "none");
    EXPECT_EQ(
        control(off, Arm::Active, [](const Rig101& r) { return r.frame("D", 2, t99::kNos()); }),
        "Active->Active | in 2->3 | out# 2->2 | fromApp=1 fromAdmin=0 toAdmin=0 toApp=0 | sent: "
        "none");
    EXPECT_EQ(control(off, Arm::LogoutSent, [](const Rig101& r) { return r.frame("5", 2); }),
              "LogoutSent->Disconnected | in 2->2 | out# 3->3 | fromApp=0 fromAdmin=0 toAdmin=0 "
              "toApp=0 | sent: none");
}

// A repeat with the same value is not a conflict.
TEST(OneMsgType101, W101_27_SameValueRepeat_DThenD) {
    for (Kind k : {Kind::fix44, Kind::fixt}) {
        SCOPED_TRACE(kind_name(k));
        EXPECT_EQ(
            control(spec(k), Arm::Active,
                    [](const Rig101& r) { return r.frame("D", 2, t99::kNos() + "35=D\x01"); }),
            "Active->Active | in 2->3 | out# 2->2 | fromApp=1 fromAdmin=0 toAdmin=0 toApp=0 | "
            "sent: none");
    }
}

// The Reject(35=3) and Logout(35=5) exemption on validation off: one of them with one MsgType
// reaches its own handling, as before.
TEST(OneMsgType101, W101_27_ValidationOff_RejectAndLogout) {
    const Spec off = spec(Kind::fix44).off();
    EXPECT_EQ(
        control(off, Arm::Active, [](const Rig101& r) { return r.frame("3", 2, "45=1\x01"); }),
        "Active->Active | in 2->3 | out# 2->2 | fromApp=0 fromAdmin=0 toAdmin=0 toApp=0 | sent: "
        "none");
    EXPECT_EQ(
        control(off, Arm::Active, [](const Rig101& r) { return r.frame("5", 2); }),
        "Active->Disconnected | in 2->3 | out# 2->3 | fromApp=0 fromAdmin=1 toAdmin=1 toApp=0 | "
        "sent: {35=5 34=2 45=<absent> 58=<absent> 371=<absent> 372=<absent> 373=<absent>}");
}

// The outbound Logon masking and the resend replay of a stored frame: a ResendRequest covering
// the session's own Logon and an application message it sent.
TEST(OneMsgType101, W101_27_ResendReplay_LogonMaskedAndStoredFrameResent) {
    for (Kind k : {Kind::fix44, Kind::fixt}) {
        SCOPED_TRACE(kind_name(k));
        auto rig = started(spec(k).with(with_memory_store), Arm::Active);
        ASSERT_TRUE(rig);
        ASSERT_EQ(rig->r->send("35=D\x01" + t99::kNos()), "");
        const Ending101 e = rig->deliver(rig->frame("2", 2,
                                                    "7=1\x01"
                                                    "16=0\x01"));
        std::string sent;
        for (const Decoded& d : e.out) {
            for (const std::string& f : field_set(d)) {
                sent += f + " ";
            }
            sent += "| ";
        }
        const std::string b = "8=" + begin_of(k);
        EXPECT_EQ(
            std::string(t99::state_name(e.after)) + " | " + sent,
            "Active | 122=20240101-00:00:00.000 123=Y 34=1 35=4 36=2 43=Y 49=ISLD 56=TW " + b +
                " | 11=C 122=20240101-00:00:00.000 21=1 34=2 35=D 38=1 40=1 43=Y 49=ISLD 54=1 "
                "55=X 56=TW 60=20240101-00:00:00 " +
                b + " | ");
    }
}

// `0…D` plus a malformed tag: the header scan's fault sends the frame to 092's disposer, ahead
// of the repeat refusal.
TEST(OneMsgType101, W101_27_RepeatPlusMalformedTag_DisposerReject) {
    for (Kind k : {Kind::fix44, Kind::fixt}) {
        SCOPED_TRACE(kind_name(k));
        EXPECT_EQ(
            control(spec(k), Arm::Active,
                    [](const Rig101& r) {
                        return r.frame("0", 2,
                                       "112=x\x01"
                                       "35=D\x01"
                                       "3a5=x\x01");
                    }),
            "Active->Active | in 2->3 | out# 2->3 | fromApp=0 fromAdmin=0 toAdmin=1 toApp=0 | "
            "sent: {35=3 34=2 45=2 58=Malformed field: invalid tag 371=<absent> 372=0 373=0}");
    }
}

// ── W101-28: a FIXT-only header tag before a later 35, on a non-FIXT session ────────────

TEST(OneMsgType101, W101_28_NonFixt_1128BeforeLater35_Reject13) {
    auto rig = started(spec(Kind::fix44), Arm::Active);
    ASSERT_TRUE(rig);
    expect_conflict_reject(rig->deliver(rig->frame("0", 2, {},
                                                   "1128=9\x01"
                                                   "35=D\x01")),
                           "13", "0", 2);
}

// ── W101-32: the check order on a validating non-FIXT session in Active ─────────────────

TEST(OneMsgType101, W101_32_NonFixt_5ThenD_Reject14_NotLoggedOut) {
    auto rig = started(spec(Kind::fix44), Arm::Active);
    ASSERT_TRUE(rig);
    expect_conflict_reject(rig->deliver(rig->frame("5", 2,
                                                   "58=x\x01"
                                                   "35=D\x01")),
                           "14", "5", 2);
}
TEST(OneMsgType101, W101_32_NonFixt_3ThenD_Reject14) {
    auto rig = started(spec(Kind::fix44), Arm::Active);
    ASSERT_TRUE(rig);
    expect_conflict_reject(rig->deliver(rig->frame("3", 2,
                                                   "45=1\x01"
                                                   "35=D\x01")),
                           "14", "3", 2);
}
TEST(OneMsgType101, W101_32_NonFixt_0ThenDPlusUndefinedTag_RepeatFirst) {
    auto rig = started(spec(Kind::fix44), Arm::Active);
    ASSERT_TRUE(rig);
    expect_conflict_reject(rig->deliver(rig->frame("0", 2,
                                                   "112=x\x01"
                                                   "35=D\x01"
                                                   "9999=x\x01")),
                           "14", "0", 2);
}

}  // namespace
}  // namespace fixpp::session::test101
