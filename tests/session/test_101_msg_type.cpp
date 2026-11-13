// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_101_msg_type.cpp
//
// 101-inbound-reject-fidelity, tasks.md T006 (W101-1 to W101-5, SC-002's 2q): an undefined
// MsgType on a validating session draws QuickFIX's Reject, 373=11 with no 371 and
// 58="Invalid MsgType" (spec US1, FR-001, FR-003). Each cell runs a real Session in Active and
// asserts the named fields of the one frame it sends, the consume and the callbacks.
//
// A MsgType a cell calls undefined is chosen by reading the judging dictionary at test time
// (`defines` below), not written as a literal the dictionary is assumed to lack.

#include <gtest/gtest.h>

#include <string>
#include <string_view>

#include "test_101_support.hpp"

namespace fixpp::session::test101 {
namespace {

bool defines(const dd::Dictionary& d, std::string_view msg_type) {
    return std::ranges::any_of(d.messages(),
                               [&](const dd::MessageEntry& m) { return m.msg_type == msg_type; });
}

bool is_admin(std::string_view msg_type) {
    return msg_type.size() == 1 && std::string_view{"012345A"}.contains(msg_type);
}

// The one Reject an undefined MsgType draws on a validating session in Active.
void expect_invalid_msg_type_reject(const Ending101& e, const std::string& msg_type, unsigned seq) {
    SCOPED_TRACE(describe(e));
    ASSERT_EQ(e.out.size(), 1U) << "exactly one frame sent";
    const Decoded& rj = e.out.front();
    EXPECT_EQ(rj.get(35), "3");
    EXPECT_EQ(rj.get(373), "11") << "SessionRejectReason(373)";
    EXPECT_FALSE(rj.has(371)) << "no RefTagID(371): an undefined MsgType names no field";
    EXPECT_EQ(rj.get(372), msg_type) << "RefMsgType(372)";
    EXPECT_EQ(rj.get(58), kTextInvalidMsgType) << "Text(58)";
    EXPECT_EQ(rj.get(45), std::to_string(seq)) << "RefSeqNum(45)";
    EXPECT_EQ(e.next_in_after, e.next_in_before + 1) << "the message is consumed";
    EXPECT_EQ(e.after, fsm_state::Active);
    EXPECT_EQ(e.from_app, 0) << "nothing reaches fromApp";
}

// W101-1: US1 scenario 1 on FIX 4.4, and the same on FIX 4.2.
TEST(Reject101MsgType, W101_1_UndefinedMsgType_Fix44) {
    auto rig = started(spec(Kind::fix44), Arm::Active);
    ASSERT_TRUE(rig);
    ASSERT_FALSE(defines(*t99::fix44(), "U1"));
    expect_invalid_msg_type_reject(rig->deliver(rig->frame("U1", 2)), "U1", 2);
}

TEST(Reject101MsgType, W101_1_UndefinedMsgType_Fix42) {
    auto rig = started(spec(Kind::fix42), Arm::Active);
    ASSERT_TRUE(rig);
    ASSERT_FALSE(defines(*t99::fix42(), "U1"));
    expect_invalid_msg_type_reject(rig->deliver(rig->frame("U1", 2)), "U1", 2);
}

// W101-2: US1 scenario 2, a validating FIXT session; the application MsgType is judged by the
// resolved application version's dictionary (the session default, FIX50SP2, with no 1128).
TEST(Reject101MsgType, W101_2_UndefinedApplicationMsgType_Fixt) {
    auto rig = started(spec(Kind::fixt), Arm::Active);
    ASSERT_TRUE(rig);
    ASSERT_FALSE(defines(*t99::fix50sp2(), "U1"));
    expect_invalid_msg_type_reject(rig->deliver(rig->frame("U1", 2)), "U1", 2);
}

// W101-3: US1 scenario 3, validation off: the handling the spec keeps unchanged. The literal is
// the ending the evidence file records for this cell (tasks.md T020).
TEST(Reject101MsgType, W101_3_UndefinedMsgType_ValidationOff_Unchanged) {
    auto rig = started(spec(Kind::fix44).off(), Arm::Active);
    ASSERT_TRUE(rig);
    EXPECT_EQ(describe(rig->deliver(rig->frame("U1", 2))),
              "Active->Active | in 2->3 | out# 2->2 | fromApp=1 fromAdmin=0 toAdmin=0 toApp=0 | "
              "sent: none");
}

// W101-4: an undefined MsgType of one character, the length of an admin MsgType, is an
// undefined MsgType. The character is the first [0-9A-Za-z] the judging dictionary lacks.
TEST(Reject101MsgType, W101_4_UndefinedOneCharacterMsgType) {
    std::string one;
    for (char c :
         std::string_view{"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"}) {
        if (!defines(*t99::fix44(), std::string(1, c))) {
            one = std::string(1, c);
            break;
        }
    }
    ASSERT_FALSE(one.empty()) << "FIX44 defines every one-character MsgType";
    auto rig = started(spec(Kind::fix44), Arm::Active);
    ASSERT_TRUE(rig);
    expect_invalid_msg_type_reject(rig->deliver(rig->frame(one, 2)), one, 2);
}

// W101-5: on a FIXT session the MsgType check uses the dictionary the frame is validated with.
// An application MsgType FIX Latest defines and FIX50SP2 does not: 1128=9 draws 373=11, and
// 1128=10 is accepted. An admin MsgType FIX50SP2 lacks and FIXT11 defines (Heartbeat) is judged
// by the transport dictionary, so it is accepted.
TEST(Reject101MsgType, W101_5_FixtJudgingDictionary) {
    std::string mt;
    for (const dd::MessageEntry& m : t99::latest()->messages()) {
        if (!is_admin(m.msg_type) && !defines(*t99::fix50sp2(), m.msg_type)) {
            mt = std::string(m.msg_type);
            break;
        }
    }
    ASSERT_FALSE(mt.empty()) << "no application MsgType FIX Latest defines and FIX50SP2 lacks";
    ASSERT_FALSE(defines(*t99::fix50sp2(), "0"));
    ASSERT_TRUE(defines(*t99::fixt11(), "0"));
    {
        auto rig = started(spec(Kind::fixt), Arm::Active);
        ASSERT_TRUE(rig);
        expect_invalid_msg_type_reject(rig->deliver(rig->frame(mt, 2, {}, "1128=9\x01")), mt, 2);
    }
    {
        auto rig = started(spec(Kind::fixt), Arm::Active);
        ASSERT_TRUE(rig);
        const Ending101 e =
            rig->deliver(rig->frame(mt, 2, t99::required_body(*t99::latest(), mt), "1128=10\x01"));
        EXPECT_TRUE(e.out.empty()) << describe(e);
        EXPECT_EQ(e.from_app, 1) << describe(e);
    }
    {
        auto rig = started(spec(Kind::fixt), Arm::Active);
        ASSERT_TRUE(rig);
        const Ending101 e = rig->deliver(rig->frame("0", 2));
        EXPECT_TRUE(e.out.empty()) << describe(e);
        EXPECT_EQ(e.from_admin, 1) << describe(e);
    }
}

// SC-002, QuickFIX C++'s 2q_MsgTypeNotValid on FIX 4.2, FIX 4.4 and FIX50SP2: the Reject's
// tag=value set equals the definition's expected frame's, BodyLength, CheckSum and SendingTime
// excluded and field order not compared. The expected lines are the evidence file's T004 record,
// copied from test/definitions/server/<version>/2q_MsgTypeNotValid.def; the inbound frame is the
// definition's `35=*` line after its Logon.
struct DefCase {
    Kind kind;
    const char* expected;
};

constexpr DefCase k2q[] = {
    {Kind::fix42,
     "8=FIX.4.2|9=86|35=3|34=2|49=ISLD|52=00000000-00:00:00.000|56=TW|45=2|58=Invalid "
     "MsgType|372=*|373=11|10=0|"},
    {Kind::fix44,
     "8=FIX.4.4|9=86|35=3|34=2|49=ISLD|52=00000000-00:00:00.000|56=TW|45=2|58=Invalid "
     "MsgType|372=*|373=11|10=0|"},
    {Kind::fixt,
     "8=FIXT.1.1|9=86|35=3|34=2|49=ISLD|52=00000000-00:00:00.000|56=TW|45=2|58=Invalid "
     "MsgType|372=*|373=11|10=0|"},
};

TEST(Reject101MsgType, SC002_2q_MsgTypeNotValid_FieldSet) {
    for (const DefCase& c : k2q) {
        SCOPED_TRACE(kind_name(c.kind));
        auto rig = started(spec(c.kind), Arm::Active);
        ASSERT_TRUE(rig);
        const Ending101 e = rig->deliver(rig->frame("*", 2));
        ASSERT_EQ(e.of_type("3").size(), 1U) << describe(e);
        EXPECT_EQ(field_set(*e.of_type("3").front()), field_set_of_def_line(c.expected));
    }
}

}  // namespace
}  // namespace fixpp::session::test101
