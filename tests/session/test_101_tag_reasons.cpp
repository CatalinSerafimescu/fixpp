// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_101_tag_reasons.cpp
//
// 101-inbound-reject-fidelity, tasks.md T007 (W101-10 to W101-13, W101-16; spec US2 scenarios
// 1 to 4, SC-002's 14a and 14c): a tag the judging dictionary does not define draws 373=0, a
// defined tag outside the message draws 373=2, and every validation-gate Reject carries
// QuickFIX C++'s text for its reason (FR-002, FR-003).
//
// Tag class. Every tag a cell names is classified as defined or not in the judging dictionary by
// tasks.md T004's tag recipe (a quote-agnostic `<field number=['"]N['"]` per dictionaries/*.xml,
// `<fixr:field id=…>` for Orchestra), and each cell re-reads the class from the loaded
// Dictionary (`defined` below) before it asserts, so a dictionary refresh fails the cell's
// precondition rather than its claim.

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

#include "test_098_inbound_frames.hpp"
#include "test_101_support.hpp"

namespace fixpp::session::test101 {
namespace {

namespace f098 = fixpp::session::test098;

// Whether `d`'s field store defines `tag` (tasks.md T004's tag recipe, read from the Dictionary).
bool defined(const dd::Dictionary& d, std::uint16_t tag) { return !d.field_name(tag).empty(); }

// The one Reject a validation-gate refusal draws in Active, field by field.
void expect_gate_reject(const Ending101& e, std::string_view reason, std::string_view ref_tag,
                        std::string_view ref_msg_type, std::string_view text, unsigned seq) {
    SCOPED_TRACE(describe(e));
    ASSERT_EQ(e.of_type("3").size(), 1U) << "exactly one Reject";
    const Decoded& rj = *e.of_type("3").front();
    EXPECT_EQ(rj.get(373), reason) << "SessionRejectReason(373)";
    EXPECT_EQ(rj.get(371), ref_tag) << "RefTagID(371)";
    EXPECT_EQ(rj.get(372), ref_msg_type) << "RefMsgType(372)";
    EXPECT_EQ(rj.get(58), text) << "Text(58)";
    EXPECT_EQ(rj.get(45), std::to_string(seq)) << "RefSeqNum(45)";
    EXPECT_EQ(e.from_app, 0) << "nothing reaches fromApp";
}

// W101-10: US2 scenario 1. 9999 is defined by no dictionary (tag recipe).
TEST(Reject101Tag, W101_10_UndefinedTag_Reason0) {
    ASSERT_FALSE(defined(*t99::fix44(), 9999));
    auto rig = started(spec(Kind::fix44), Arm::Active);
    ASSERT_TRUE(rig);
    const Ending101 e = rig->deliver(rig->frame("D", 2, f098::nos_body("1") + "9999=x\x01"));
    expect_gate_reject(e, "0", "9999", "D", kTextInvalidTagNumber, 2);
    EXPECT_EQ(e.next_in_after, e.next_in_before + 1) << "consumed";
    EXPECT_EQ(e.after, fsm_state::Active);
}

// W101-11: US2 scenario 2. 44 is defined by FIX44, not on a Heartbeat (tag recipe).
TEST(Reject101Tag, W101_11_DefinedTagNotInMessage_Reason2) {
    ASSERT_TRUE(defined(*t99::fix44(), 44));
    auto rig = started(spec(Kind::fix44), Arm::Active);
    ASSERT_TRUE(rig);
    const Ending101 e = rig->deliver(rig->frame("0", 2, "44=1\x01"));
    expect_gate_reject(e, "2", "44", "0", kTextTagNotDefined, 2);
    EXPECT_EQ(e.next_in_after, e.next_in_before + 1) << "consumed";
}

// W101-12: US2 scenario 3. Every other reason the validation gate gives carries its text, and
// every other field stays as the gate sent it before this feature: on the FIXT shape that is 099's
// keyed ending, asserted unchanged (its short form carries no 58), plus the text. One cell per gate
// site a reason reaches. A cell for reason 6 needs an input on which `decimal_t::parse` returns
// `decimal_precision_loss` (the validator's Float arm); the evidence file (T007) records the
// search for one.
void expect_keyed_with_text(Arm arm, const char* input, const char* h0_ending,
                            std::string_view text) {
    const t99::CellRun run = t99::run_keyed_cell(t99::Shape::T, true, arm, t99::input(input));
    t99::expect_ending(run, h0_ending);
    ASSERT_FALSE(run.ending.out.empty()) << t99::log_form(run.ending);
    EXPECT_EQ(t99::tag_value(run.ending.out.front(), 35), "3");
    EXPECT_EQ(t99::tag_value(run.ending.out.front(), 58), text) << "Text(58)";
}

TEST(Reject101Tag, W101_12_NotConnected_Reason1_LogonMissing1137) {
    expect_keyed_with_text(Arm::NotConnected, "Logon 1137 absent",
                           "R 1/1137 A 45=1 | Disc | k | app=0 adm=0", kTextRequiredTagMissing);
}
TEST(Reject101Tag, W101_12_NotConnected_Reason1_TestRequestMissing112) {
    expect_keyed_with_text(Arm::NotConnected, "TestRequest missing 112",
                           "R 1/112 1 45=1 | stays | k | app=0 adm=0", kTextRequiredTagMissing);
}
TEST(Reject101Tag, W101_12_NotConnected_Reason5_1128) {
    expect_keyed_with_text(Arm::NotConnected, "Heartbeat 1128=99 hdr",
                           "R 5/1128 0 45=1 | stays | k | app=0 adm=0", kTextValueIncorrect);
}
TEST(Reject101Tag, W101_12_LogonSent_Reason1_LogonMissing1137) {
    expect_keyed_with_text(Arm::LogonSent, "Logon 1137 absent",
                           "R 1/1137 A 45=1 | Disc | k | app=0 adm=0", kTextRequiredTagMissing);
}
TEST(Reject101Tag, W101_12_LogonSent_Reason1_TestRequestMissing112) {
    expect_keyed_with_text(Arm::LogonSent, "TestRequest missing 112",
                           "R 1/112 1 45=1 | stays | k | app=0 adm=0", kTextRequiredTagMissing);
}
TEST(Reject101Tag, W101_12_LogonSent_Reason5_1128) {
    expect_keyed_with_text(Arm::LogonSent, "Heartbeat 1128=99 hdr",
                           "R 5/1128 0 45=1 | stays | k | app=0 adm=0", kTextValueIncorrect);
}
TEST(Reject101Tag, W101_12_Active_Reason1_NosMissing11) {
    expect_keyed_with_text(Arm::Active, "NOS missing 11", "R 1/11 D 45=2 | stays | c | app=0 adm=0",
                           kTextRequiredTagMissing);
}
TEST(Reject101Tag, W101_12_Active_Reason5_1128) {
    expect_keyed_with_text(Arm::Active, "1128=99 hdr", "R 5/1128 D 45=2 | stays | c | app=0 adm=0",
                           kTextValueIncorrect);
}
TEST(Reject101Tag, W101_12_Active_Reason4_1128Empty) {
    expect_keyed_with_text(Arm::Active, "1128=empty hdr",
                           "R 4/1128 D 45=2 | stays | c | app=0 adm=0", kTextTagWithoutValue);
}
TEST(Reject101Tag, W101_12_Active_Reason14_1128PastHeader) {
    expect_keyed_with_text(Arm::Active, "1128=6 past", "R 14/1128 D 45=2 | stays | c | app=0 adm=0",
                           kTextOutOfOrder);
}
TEST(Reject101Tag, W101_12_Active_Reason13_RepeatedMsgTypeInHeader) {
    expect_keyed_with_text(Arm::Active, "rep35 0,D header",
                           "R 13/35 0 45=2 | stays | c | app=0 adm=0", kTextTagMoreThanOnce);
}
TEST(Reject101Tag, W101_12_Active_Reason14_RepeatedMsgTypePastHeader) {
    expect_keyed_with_text(Arm::Active, "rep35 0..A logon-fields",
                           "R 14/35 0 45=2 | stays | c | app=0 adm=0", kTextOutOfOrder);
}

// The same on FIX 4.4, where the gate's Reject is not 099's: its fields are written out.
TEST(Reject101Tag, W101_12_Fix44_Active_Reason1) {
    auto rig = started(spec(Kind::fix44), Arm::Active);
    ASSERT_TRUE(rig);
    const Ending101 e = rig->deliver(rig->frame("D", 2, t99::kNosNo11()));
    expect_gate_reject(e, "1", "11", "D", kTextRequiredTagMissing, 2);
}
TEST(Reject101Tag, W101_12_Fix44_Active_Reason5) {
    auto rig = started(spec(Kind::fix44), Arm::Active);
    ASSERT_TRUE(rig);
    std::string bad = t99::kNos();
    bad.replace(bad.find("54=1"), 4, "54=Z");
    const Ending101 e = rig->deliver(rig->frame("D", 2, bad));
    expect_gate_reject(e, "5", "54", "D", kTextValueIncorrect, 2);
}

// A FIX 4.4 Logon without SendingTime(52): the validator's required-field miss, at the two
// establishment gate sites.
std::string logon_without_52(const Rig101& rig) {
    const t99::Ctx c = rig.ctx(1);
    return t99::wire(rig.begin(),
                     "35=A\x01"
                     "34=1\x01"
                     "49=" +
                         c.snd +
                         "\x01"
                         "56=" +
                         c.tgt + "\x01" + t99::kLogonBody());
}
TEST(Reject101Tag, W101_12_Fix44_NotConnected_Reason1) {
    auto rig = started(spec(Kind::fix44), Arm::NotConnected);
    ASSERT_TRUE(rig);
    expect_gate_reject(rig->deliver(logon_without_52(*rig)), "1", "52", "A",
                       kTextRequiredTagMissing, 1);
}
TEST(Reject101Tag, W101_12_Fix44_LogonSent_Reason1) {
    auto rig = started(spec(Kind::fix44).initiator(), Arm::LogonSent);
    ASSERT_TRUE(rig);
    expect_gate_reject(rig->deliver(logon_without_52(*rig)), "1", "52", "A",
                       kTextRequiredTagMissing, 1);
}

// W101-13: US2 scenario 4. 098's AS-2 layout: one Parties instance, the undefined tag last in
// it; and the undefined tag inside a nested group instance (Parties' PtysSubGrp).
TEST(Reject101Tag, W101_13_UndefinedTagInGroupInstance) {
    ASSERT_FALSE(defined(*t99::fix44(), f098::kPartiesMemberTag));
    auto rig = started(spec(Kind::fix44), Arm::Active);
    ASSERT_TRUE(rig);
    const auto as2 = f098::frame_as2_parties_member(2);
    const Ending101 e =
        rig->deliver(std::string(reinterpret_cast<const char*>(as2.data()), as2.size()));
    expect_gate_reject(e, "0", std::to_string(f098::kPartiesMemberTag), "D", kTextInvalidTagNumber,
                       2);
}

TEST(Reject101Tag, W101_13_UndefinedTagInNestedGroupInstance) {
    ASSERT_FALSE(defined(*t99::fix44(), 5002));
    ASSERT_TRUE(defined(*t99::fix44(), 802));
    auto rig = started(spec(Kind::fix44), Arm::Active);
    ASSERT_TRUE(rig);
    const Ending101 e = rig->deliver(rig->frame("D", 2,
                                                f098::nos_body("1") + "453=1\x01"
                                                                      "448=P\x01"
                                                                      "447=D\x01"
                                                                      "452=1\x01"
                                                                      "802=1\x01"
                                                                      "523=S\x01"
                                                                      "803=1\x01"
                                                                      "5002=x\x01"));
    expect_gate_reject(e, "0", "5002", "D", kTextInvalidTagNumber, 2);
}

// W101-16: SC-002, QuickFIX C++'s 14a_BadField undefined-tag steps (371=999 and 371=5000) and
// 14c_TagNotDefinedForMsgType, each Reject's tag=value set against the definition's expected
// frame (BodyLength, CheckSum and SendingTime excluded; field order not compared: fixpp writes
// 58 after 373, the definitions after 45). 14a is replayed whole, its malformed-tag steps (tags
// 0 and -1, 092's disposer) included so the numbers match; those steps' Rejects are not compared.
// The expected lines are the evidence file's T004 record, copied from
// test/definitions/server/<version>/<case>.def. The tags are classified by the tag recipe in the
// dictionary that judges a Heartbeat: FIXT11 on the FIXT session, where the Heartbeat is admin.
struct Steps14a {
    Kind kind;
    const DictPtr& (*judge)();
    const char* expect_999;
    const char* expect_5000;
};

const Steps14a k14a[] = {
    {Kind::fix42, &t99::fix42,
     "8=FIX.4.2|9=96|35=3|34=2|49=ISLD|52=00000000-00:00:00.000|56=TW|45=2|58=Invalid tag "
     "number|371=999|372=0|373=0|10=0|",
     "8=FIX.4.2|9=97|35=3|34=5|49=ISLD|52=00000000-00:00:00.000|56=TW|45=5|58=Invalid tag "
     "number|371=5000|372=0|373=0|10=0|"},
    {Kind::fix44, &t99::fix44,
     "8=FIX.4.4|9=96|35=3|34=2|49=ISLD|52=00000000-00:00:00.000|56=TW|45=2|58=Invalid tag "
     "number|371=999|372=0|373=0|10=0|",
     "8=FIX.4.4|9=97|35=3|34=5|49=ISLD|52=00000000-00:00:00.000|56=TW|45=5|58=Invalid tag "
     "number|371=5000|372=0|373=0|10=0|"},
    {Kind::fixt, &t99::fixt11,
     "8=FIXT.1.1|9=96|35=3|34=2|49=ISLD|52=00000000-00:00:00.000|56=TW|45=2|58=Invalid tag "
     "number|371=999|372=0|373=0|10=0|",
     "8=FIXT.1.1|9=97|35=3|34=5|49=ISLD|52=00000000-00:00:00.000|56=TW|45=5|58=Invalid tag "
     "number|371=5000|372=0|373=0|10=0|"},
};

TEST(Reject101Tag, W101_16_SC002_14a_UndefinedTagSteps_FieldSet) {
    for (const Steps14a& c : k14a) {
        SCOPED_TRACE(kind_name(c.kind));
        ASSERT_FALSE(defined(*c.judge(), 999));
        ASSERT_FALSE(defined(*c.judge(), 5000));
        auto rig = started(spec(c.kind), Arm::Active);
        ASSERT_TRUE(rig);
        const Ending101 s999 = rig->deliver(rig->frame("0", 2, "999=HI\x01"));
        (void)rig->deliver(rig->frame("0", 3, "0=HI\x01"));
        (void)rig->deliver(rig->frame("0", 4, "-1=HI\x01"));
        const Ending101 s5000 = rig->deliver(rig->frame("0", 5, "5000=HI\x01"));
        ASSERT_EQ(s999.of_type("3").size(), 1U) << describe(s999);
        EXPECT_EQ(field_set(*s999.of_type("3").front()), field_set_of_def_line(c.expect_999));
        ASSERT_EQ(s5000.of_type("3").size(), 1U) << describe(s5000);
        EXPECT_EQ(field_set(*s5000.of_type("3").front()), field_set_of_def_line(c.expect_5000));
    }
}

struct Step14c {
    Kind kind;
    const DictPtr& (*judge)();
    std::uint16_t tag;
    const char* body;
    const char* expected;
};

const Step14c k14c[] = {
    {Kind::fix42, &t99::fix42, 55, "55=MSFT\x01",
     "8=FIX.4.2|9=114|35=3|34=2|49=ISLD|52=00000000-00:00:00.000|56=TW|45=2|58=Tag not defined "
     "for this message type|371=55|372=0|373=2|10=0|"},
    {Kind::fix44, &t99::fix44, 55, "55=MSFT\x01",
     "8=FIX.4.4|9=114|35=3|34=2|49=ISLD|52=00000000-00:00:00.000|56=TW|45=2|58=Tag not defined "
     "for this message type|371=55|372=0|373=2|10=0|"},
    {Kind::fixt, &t99::fixt11, 58, "58=Text\x01",
     "8=FIXT.1.1|9=114|35=3|34=2|49=ISLD|52=00000000-00:00:00.000|56=TW|45=2|58=Tag not defined "
     "for this message type|371=58|372=0|373=2|10=0|"},
};

TEST(Reject101Tag, W101_16_SC002_14c_TagNotDefinedForMsgType_FieldSet) {
    for (const Step14c& c : k14c) {
        SCOPED_TRACE(kind_name(c.kind));
        ASSERT_TRUE(defined(*c.judge(), c.tag));
        auto rig = started(spec(c.kind), Arm::Active);
        ASSERT_TRUE(rig);
        const Ending101 e = rig->deliver(rig->frame("0", 2, c.body));
        ASSERT_EQ(e.of_type("3").size(), 1U) << describe(e);
        EXPECT_EQ(field_set(*e.of_type("3").front()), field_set_of_def_line(c.expected));
    }
}

}  // namespace
}  // namespace fixpp::session::test101
