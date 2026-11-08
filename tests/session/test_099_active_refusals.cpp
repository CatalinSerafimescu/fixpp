// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_099_active_refusals.cpp
//
// 099-fixt-appl-version-validation, contracts/witnesses.md W-4 (tasks.md T011): data-model
// E-7's AC row, `AC-a` to `AC-g`, every input of each column, on T, A and L, each literal from
// E-7's `#### After`. `AC-admin` is the other admin frames of the keyed logs, whose After is
// "A answers as T" and otherwise that log's line. A control frame in the same session delivers,
// counted, wherever the session stays Active.
//
// Each cell's trace line compares with research R-18's logs. A cell with no log line, as the
// first-34 pair, the double faults and `1128=0`, `1128=1`, is read against the row.

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <string>

#include "test_099_fixt_support.hpp"

namespace fixpp::session::test099 {
namespace {

// 101 FR-002 supersedes the `5999` rows' 373=2: they need 5999 undefined by the judging
// dictionary (re-derive with the field-definition grep, `<field number=…>` per
// dictionaries/*.xml and `<fixr:field id=…>` in the Orchestra file), so each is an undefined tag,
// 373=0.
const KeyedRow kActiveRows[] = {
    {.cell = "AC-a", .input = "rep35 D..0 past", .t = "R 14/35 D 45=2 | stays | c | app=0 adm=0"},
    {.cell = "AC-a", .input = "rep35 D..3 past", .t = "R 14/35 D 45=2 | stays | c | app=0 adm=0"},
    {.cell = "AC-a", .input = "rep35 D..4 past", .t = "R 14/35 D 45=2 | stays | c | app=0 adm=0"},
    {.cell = "AC-a", .input = "rep35 D..A past", .t = "R 14/35 D 45=2 | stays | c | app=0 adm=0"},
    {.cell = "AC-a", .input = "rep35 D..5 past", .t = "R 14/35 D 45=2 | stays | c | app=0 adm=0"},
    {.cell = "AC-a", .input = "rep35 5..D past", .t = "R 14/35 5 45=2 | stays | c | app=0 adm=0"},
    {.cell = "AC-a", .input = "rep35 0..D past", .t = "R 14/35 0 45=2 | stays | c | app=0 adm=0"},
    {.cell = "AC-a", .input = "rep35 3..D past", .t = "R 14/35 3 45=2 | stays | c | app=0 adm=0"},
    {.cell = "AC-a", .input = "rep35 4..D past", .t = "R 14/35 4 45=2 | stays | k | app=0 adm=0"},
    {.cell = "AC-a", .input = "rep35 A..0 past", .t = "R 14/35 A 45=2 | stays | k | app=0 adm=0"},
    {.cell = "AC-a",
     .input = "rep35 D..A logon-fields",
     .t = "R 14/35 D 45=2 | stays | c | app=0 adm=0"},
    {.cell = "AC-a",
     .input = "rep35 0..A logon-fields",
     .t = "R 14/35 0 45=2 | stays | c | app=0 adm=0"},
    {.cell = "AC-a",
     .input = "rep35 D..0 past seq+5",
     .t = "R 14/35 D 45=7 | stays | k | app=0 adm=0"},
    {.cell = "AC-a", .input = "rep35 0,D header", .t = "R 13/35 0 45=2 | stays | c | app=0 adm=0"},
    {.cell = "AC-a", .input = "rep35 D,D header", .t = "- | stays | c | app=1 adm=0"},
    {.cell = "AC-b", .input = "1128=99 hdr", .t = "R 5/1128 D 45=2 | stays | c | app=0 adm=0"},
    {.cell = "AC-b", .input = "1128=abc hdr", .t = "R 5/1128 D 45=2 | stays | c | app=0 adm=0"},
    {.cell = "AC-b", .input = "1128=0 hdr", .t = "R 5/1128 D 45=2 | stays | c | app=0 adm=0"},
    {.cell = "AC-b", .input = "1128=1 hdr", .t = "R 5/1128 D 45=2 | stays | c | app=0 adm=0"},
    {.cell = "AC-b", .input = "1128=4 hdr", .t = "R 5/1128 D 45=2 | stays | c | app=0 adm=0"},
    {.cell = "AC-b", .input = "1128=empty hdr", .t = "R 4/1128 D 45=2 | stays | c | app=0 adm=0"},
    {.cell = "AC-c", .input = "1128=6 past", .t = "R 14/1128 D 45=2 | stays | c | app=0 adm=0"},
    {.cell = "AC-c", .input = "1128=abc past", .t = "R 14/1128 D 45=2 | stays | c | app=0 adm=0"},
    {.cell = "AC-d", .input = "NOS", .t = "- | stays | c | app=1 adm=0"},
    {.cell = "AC-d", .input = "NOS 1128=9", .t = "- | stays | c | app=1 adm=0"},
    {.cell = "AC-d", .input = "NOS 1128=10", .t = "- | stays | c | app=1 adm=0"},
    {.cell = "AC-d", .input = "NOS missing 11", .t = "R 1/11 D 45=2 | stays | c | app=0 adm=0"},
    {.cell = "AC-d",
     .input = "NOS missing 11 seq+5",
     .t = "R 1/11 D 45=7 | stays | k | app=0 adm=0"},
    {.cell = "AC-e",
     .input = "Logon 1137 absent",
     .t = "R 1/1137 A 45=2 | stays | k | app=0 adm=0"},
    {.cell = "AC-e",
     .input = "Logon undeclared 5999",
     .t = "R 0/5999 A 45=2 | stays | k | app=0 adm=0"},
    {.cell = "AC-e",
     .input = "Logon 1137 absent + 5999 after",
     .t = "R 0/5999 A 45=2 | stays | k | app=0 adm=0"},
    {.cell = "AC-e",
     .input = "Logon 5999 + 1137 absent",
     .t = "R 0/5999 A 45=2 | stays | k | app=0 adm=0"},
    {.cell = "AC-e",
     .input = "Logon 1137=4 + 5999 after",
     .t = "R 0/5999 A 45=2 | stays | k | app=0 adm=0"},
    {.cell = "AC-e",
     .input = "Logon 1137=99 + 5999 after",
     .t = "R 0/5999 A 45=2 | stays | k | app=0 adm=0",
     .a = nullptr,
     .l = "R 5/1137 A 45=2 | stays | k | app=0 adm=0"},
    {.cell = "AC-e", .input = "Logon 1137=9", .t = "- | stays | c | app=1 adm=0"},
    {.cell = "AC-e", .input = "Logon 1137=10", .t = "- | stays | c | app=1 adm=0"},
    {.cell = "AC-e", .input = "Logon 1137=empty", .t = "- | stays | c | app=1 adm=0"},
    {.cell = "AC-e", .input = "Logon 1137=4", .t = "- | stays | c | app=1 adm=0"},
    {.cell = "AC-e", .input = "Logon 141=Y 1137=4", .t = "- | stays | c | app=1 adm=0"},
    {.cell = "AC-e",
     .input = "Logon 1137=99",
     .t = "- | stays | c | app=1 adm=0",
     .a = nullptr,
     .l = "R 5/1137 A 45=2 | stays | k | app=0 adm=0"},
    {.cell = "AC-e",
     .input = "Logon 1137=abc",
     .t = "- | stays | c | app=1 adm=0",
     .a = nullptr,
     .l = "R 5/1137 A 45=2 | stays | k | app=0 adm=0"},
    {.cell = "AC-f", .input = "wrongBS NOS", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "AC-f", .input = "wrongBS Logon 1137=9", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "AC-f", .input = "wrongBS Logon 1137=4", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "AC-f",
     .input = "wrongBS Logon 1137=99",
     .t = "- | Disc | k | app=0 adm=0",
     .a = nullptr,
     .l = "R 5/1137 A 45=2 | stays | k | app=0 adm=0"},
    {.cell = "AC-f",
     .input = "wrongBS Logon 1137 absent",
     .t = "R 1/1137 A 45=2 | stays | k | app=0 adm=0"},
    {.cell = "AC-g", .input = "stale NOS", .t = "R 10/52 D 45=2 + Logout | Disc | c | app=0 adm=0"},
    {.cell = "AC-g",
     .input = "stale Heartbeat",
     .t = "R 10/52 0 45=2 + Logout | Disc | c | app=0 adm=0"},
    {.cell = "AC-g",
     .input = "stale Logon 1137=9",
     .t = "R 10/52 A 45=2 + Logout | Disc | k | app=0 adm=0"},
    {.cell = "AC-g",
     .input = "stale Logon 1137=4",
     .t = "R 10/52 A 45=2 + Logout | Disc | k | app=0 adm=0"},
    {.cell = "AC-g",
     .input = "stale Logon 1137=99",
     .t = "R 10/52 A 45=2 + Logout | Disc | k | app=0 adm=0",
     .a = nullptr,
     .l = "R 5/1137 A 45=2 | stays | k | app=0 adm=0"},
    {.cell = "AC-g",
     .input = "stale Logon 1137 absent",
     .t = "R 1/1137 A 45=2 | stays | k | app=0 adm=0"},
    {.cell = "AC-admin",
     .input = "Heartbeat 1128=99 hdr",
     .t = "R 5/1128 0 45=2 | stays | c | app=0 adm=0"},
    {.cell = "AC-admin",
     .input = "Heartbeat 1128=10 hdr",
     .t = "R 5/1128 0 45=2 | stays | c | app=0 adm=0",
     .a = nullptr,
     .l = "- | stays | c | app=0 adm=1"},
    {.cell = "AC-admin",
     .input = "TestRequest 1128=6 past",
     .t = "Heartbeat 112=x | stays | c | app=0 adm=1"},
    {.cell = "AC-admin", .input = "Heartbeat", .t = "- | stays | c | app=0 adm=1"},
    {.cell = "AC-admin",
     .input = "TestRequest missing 112",
     .t = "R 1/112 1 45=2 | stays | c | app=0 adm=0"},
    {.cell = "AC-admin", .input = "TestRequest", .t = "Heartbeat 112=x | stays | c | app=0 adm=1"},
    {.cell = "AC-admin", .input = "Logout", .t = "Logout | Disc | c | app=0 adm=1"},
};

class W4ActiveRefusals : public testing::TestWithParam<KeyedCell> {};

TEST_P(W4ActiveRefusals, Ending) {
    const KeyedCell& c = GetParam();
    CellRun run = run_keyed_cell(c.shape, /*validate=*/true, Arm::Active, input(c.row.input));
    expect_ending(run, expected_on(c.row, c.shape));
    if (run.setup_failure.empty() && run.rig->sess->state() == fsm_state::Active) {
        // The control: the same session still delivers a valid application frame, so a
        // refusal cell cannot pass on a dead session.
        const int before = run.rig->app->app;
        const Ending control =
            run.rig->deliver(frame_35("D", run.rig->peer_ctx(run.rig->next_in()), kNos()));
        EXPECT_EQ(control.from_app, before + 1)
            << "the control frame must be delivered; log form: " << log_form(control);
    }
}

INSTANTIATE_TEST_SUITE_P(AC, W4ActiveRefusals, testing::ValuesIn(on_every_shape(kActiveRows)),
                         keyed_cell_name);

// ── The double faults, by E-7's step order ──────────────────────────────────────────────

const KeyedRow kDoubleFaultRows[] = {
    {.cell = "AC-double",
     .input = "rep35 D..0 past 1128=99",
     .t = "R 14/35 D 45=2 | stays | c | app=0 adm=0"},
    {.cell = "AC-double",
     .input = "1128=99 hdr stale",
     .t = "R 5/1128 D 45=2 | stays | c | app=0 adm=0"},
};

class W4DoubleFaults : public testing::TestWithParam<KeyedCell> {};

TEST_P(W4DoubleFaults, Ending) {
    const KeyedCell& c = GetParam();
    const CellRun run = run_keyed_cell(c.shape, /*validate=*/true, Arm::Active, input(c.row.input));
    expect_ending(run, expected_on(c.row, c.shape));
}

INSTANTIATE_TEST_SUITE_P(AC, W4DoubleFaults, testing::ValuesIn(on_every_shape(kDoubleFaultRows)),
                         keyed_cell_name);

// ── 45 is the first 34 ──────────────────────────────────────────────────────────────────

// A pair only FIX 5.0 SP2's table declares, an overlay's since no vendored dictionary has one (the
// helper's note). On T, S's hooks do not know it. The route is refused, so they scan the frame.
Fixture t_with_routed_pair() {
    return own_fixture(Shape::T, {fixt11(), fix50sp2_with_body_pair()});
}

std::string refused_with_pair_data(const Rig& rig, const std::string& data) {
    return frame_35("D", rig.peer_ctx(2), kNos() + pair_field(data), "1128=4\x01");
}

TEST(W4FirstSeqNum, PairOnlyTheRouteDeclares_Reject45AndNextNumInAreTheFirst34) {
    ASSERT_EQ(fix50sp2_with_body_pair()->length_pair_data_tag(5030), 5031);
    ASSERT_EQ(fixt11()->length_pair_data_tag(5030), 0);
    const CellRun run = run_own_cell("W-4 first 34", t_with_routed_pair(), /*validate=*/true,
                                     Arm::Active, [](const Rig& r) {
                                         return refused_with_pair_data(r,
                                                                       "\x01"
                                                                       "34=7");
                                     });
    expect_ending(run, "R 5/1128 D 45=2 | stays | c | app=0 adm=0");
    if (run.setup_failure.empty()) {
        EXPECT_EQ(run.rig->next_in(), 3U) << "NextNumIn follows the first 34, not the Data's";
    }
}

TEST(W4FirstSeqNum, PairDataNotTags_TheFaultDisposition) {
    const CellRun run = run_own_cell("W-4 first 34 non-tag", t_with_routed_pair(),
                                     /*validate=*/true, Arm::Active, [](const Rig& r) {
                                         return refused_with_pair_data(r,
                                                                       "\x01"
                                                                       "zz");
                                     });
    // dispose_unparseable_'s fault row: the number is consumed, 373=0 and no 371.
    expect_ending(run, "R 0/ D 45=2 | stays | c | app=0 adm=0");
}

// ── 1128 is declared in no message body of the vendored files ───────────────────────────

// FR-026's limit: a QuickFIX-format file names ApplVerID in <header> only, never in <messages>
// or <components>; the Orchestra file's one fieldRef of id 1128 is in StandardHeader (id 1024).
TEST(W4ApplVerIdRecompute, DeclaredInNoMessageBody) {
    int headers_seen = 0;
    for (const char* file : {"FIX40.xml", "FIX41.xml", "FIX42.xml", "FIX43.xml", "FIX44.xml",
                             "FIX50.xml", "FIX50SP1.xml", "FIX50SP2.xml", "FIXT11.xml"}) {
        const std::string xml = read_vendored(file);
        ASSERT_FALSE(xml.empty()) << file;
        const auto header = xml.find("</header>");
        const auto messages = xml.find("<messages>");
        const auto fields = xml.find("<fields>");
        ASSERT_NE(messages, std::string::npos) << file;
        ASSERT_NE(fields, std::string::npos) << file;
        const std::string name = "name='ApplVerID'";
        if (header != std::string::npos && xml.find(name) < header) {
            ++headers_seen;
        }
        const auto in_body = xml.find(name, messages);
        EXPECT_TRUE(in_body == std::string::npos || in_body >= fields)
            << file << " declares ApplVerID in a message or component";
    }
    // Known positive: FIXT11.xml's header names it, so the walk can see the name.
    EXPECT_GE(headers_seen, 1);

    const std::string orch = read_vendored("orchestra/OrchestraFIXLatest.xml");
    ASSERT_FALSE(orch.empty());
    int refs = 0;
    std::size_t at = 0;
    const std::string ref = "<fixr:fieldRef id=\"1128\"";
    while ((at = orch.find(ref, at)) != std::string::npos) {
        ++refs;
        const auto open = orch.rfind("<fixr:component ", at);
        const auto open_msg = orch.rfind("<fixr:message ", at);
        ASSERT_NE(open, std::string::npos);
        EXPECT_TRUE(open_msg == std::string::npos || open_msg < open)
            << "a 1128 fieldRef sits in a message";
        EXPECT_NE(orch.substr(open, 120).find("id=\"1024\""), std::string::npos)
            << "a 1128 fieldRef sits outside StandardHeader";
        at += ref.size();
    }
    EXPECT_EQ(refs, 1);
}

// A refused frame consumes its number, FIX-SL 2020 §4.5.4, and at the last number there is no next
// one. consume_rejected_seqnum_ makes that session-fatal, so the Reject is not sent.
TEST(W4ConsumeFails, RefusedFrameAtTheLastSeqNum_OverflowEndsTheSession) {
    RigOptions o;
    o.validate = true;
    CellRun run = enter_arm(shape_fixture(Shape::T), Arm::Active, std::move(o));
    ASSERT_TRUE(run.setup_failure.empty()) << run.setup_failure;
    Rig& r = *run.rig;
    auto seeded =
        asio::co_spawn(r.ioc, session_test_access::seqnum_mgr(*r.sess).set_next_inbound(seqnum_max),
                       asio::use_future);
    ASSERT_TRUE(pump_ready(r.ioc, seeded, std::chrono::seconds{30}));
    ASSERT_TRUE(seeded.get().has_value());
    Ctx c = r.peer_ctx(2);
    c.seq = seqnum_max;
    const Ending e = r.deliver(frame_35("D", c, kNosNo11()));
    EXPECT_EQ(e.err,
              "err=" + std::to_string(static_cast<int>(fixpp::core::error::store_seqnum_overflow)))
        << log_form(e);
    EXPECT_EQ(short_form(e), "- | Disc | k | app=0 adm=0");
}

}  // namespace
}  // namespace fixpp::session::test099
