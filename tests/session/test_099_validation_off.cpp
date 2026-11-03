// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_099_validation_off.cpp
//
// 099-fixt-appl-version-validation, contracts/witnesses.md W-6 (tasks.md T013): with validation off
// every W-4 input ends as research R-18's `off` line says, a literal per cell, whose mutants are
// M-28 and M-29. `off-pair` and `off-hops` are the two the row states differ from that log's tree.
// The accessor cells and E-5's splits wait for tasks.md T079.

#include <gtest/gtest.h>

#include <string>

#include "test_099_fixt_support.hpp"

namespace fixpp::session::test099 {
namespace {

const KeyedRow kActiveOff[] = {
    {"AC-a", "rep35 D..0 past", "- | stays | c | app=0 adm=1"},
    {"AC-a", "rep35 D..3 past", "- | stays | c | app=0 adm=0"},
    {"AC-a", "rep35 D..4 past", "- | stays | k | app=0 adm=1"},
    {"AC-a", "rep35 D..A past", "- | stays | c | app=1 adm=0"},
    {"AC-a", "rep35 D..5 past", "Logout | Disc | c | app=0 adm=1"},
    {"AC-a", "rep35 5..D past", "- | stays | c | app=1 adm=0"},
    {"AC-a", "rep35 0..D past", "- | stays | c | app=1 adm=0"},
    {"AC-a", "rep35 3..D past", "- | stays | c | app=1 adm=0"},
    {"AC-a", "rep35 4..D past", "- | stays | c | app=1 adm=0"},
    {"AC-a", "rep35 A..0 past", "- | stays | c | app=0 adm=1"},
    {"AC-a", "rep35 D..A logon-fields", "- | stays | c | app=1 adm=0"},
    {"AC-a", "rep35 0..A logon-fields", "- | stays | c | app=1 adm=0"},
    {"AC-a", "rep35 0,D header", "- | stays | c | app=1 adm=0"},
    {"AC-a", "rep35 D,D header", "- | stays | c | app=1 adm=0"},
    {"AC-a", "rep35 D..0 past seq+5", "ResendRequest 2-0 | stays | k | app=0 adm=0"},
    {"AC-b", "1128=99 hdr", "- | stays | c | app=1 adm=0"},
    {"AC-b", "1128=abc hdr", "- | stays | c | app=1 adm=0"},
    {"AC-b", "1128=4 hdr", "- | stays | c | app=1 adm=0"},
    {"AC-b", "1128=empty hdr", "- | stays | c | app=1 adm=0"},
    {"AC-c", "1128=6 past", "- | stays | c | app=1 adm=0"},
    {"AC-c", "1128=abc past", "- | stays | c | app=1 adm=0"},
    {"AC-d", "NOS", "- | stays | c | app=1 adm=0"},
    {"AC-d", "NOS 1128=9", "- | stays | c | app=1 adm=0"},
    {"AC-d", "NOS 1128=10", "- | stays | c | app=1 adm=0"},
    {"AC-d", "NOS missing 11", "- | stays | c | app=1 adm=0"},
    {"AC-d", "NOS missing 11 seq+5", "ResendRequest 2-0 | stays | k | app=0 adm=0"},
    {"AC-e", "Logon 1137=9", "- | stays | c | app=1 adm=0"},
    {"AC-e", "Logon 1137=10", "- | stays | c | app=1 adm=0"},
    {"AC-e", "Logon 1137 absent", "- | stays | c | app=1 adm=0"},
    {"AC-e", "Logon 1137=empty", "- | stays | c | app=1 adm=0"},
    {"AC-e", "Logon 1137=99", "- | stays | c | app=1 adm=0"},
    {"AC-e", "Logon undeclared 5999", "- | stays | c | app=1 adm=0"},
    {"AC-e", "Logon 1137=abc", "- | stays | c | app=1 adm=0"},
    {"AC-e", "Logon 1137=4", "- | stays | c | app=1 adm=0"},
    {"AC-f", "wrongBS Logon 1137=9", "- | Disc | k | app=0 adm=0"},
    {"AC-f", "wrongBS Logon 1137=99", "- | Disc | k | app=0 adm=0"},
    {"AC-f", "wrongBS Logon 1137=4", "- | Disc | k | app=0 adm=0"},
    {"AC-f", "wrongBS Logon 1137 absent", "- | Disc | k | app=0 adm=0"},
    {"AC-f", "wrongBS NOS", "- | Disc | k | app=0 adm=0"},
    {"AC-g", "stale Logon 1137=9", "R 10/52 A 45=2 + Logout | Disc | k | app=0 adm=0"},
    {"AC-g", "stale Logon 1137=99", "R 10/52 A 45=2 + Logout | Disc | k | app=0 adm=0"},
    {"AC-g", "stale Logon 1137=4", "R 10/52 A 45=2 + Logout | Disc | k | app=0 adm=0"},
    {"AC-g", "stale Logon 1137 absent", "R 10/52 A 45=2 + Logout | Disc | k | app=0 adm=0"},
    {"AC-g", "stale NOS", "R 10/52 D 45=2 + Logout | Disc | c | app=0 adm=0"},
    {"AC-g", "stale Heartbeat", "R 10/52 0 45=2 + Logout | Disc | c | app=0 adm=0"},
    {"AC-admin", "Heartbeat 1128=99 hdr", "- | stays | c | app=0 adm=1"},
    {"AC-admin", "Heartbeat 1128=10 hdr", "- | stays | c | app=0 adm=1"},
    {"AC-admin", "TestRequest 1128=6 past", "Heartbeat 112=x | stays | c | app=0 adm=1"},
    {"AC-admin", "Heartbeat", "- | stays | c | app=0 adm=1"},
    {"AC-admin", "TestRequest missing 112", "Heartbeat 112= | stays | c | app=0 adm=1"},
    {"AC-admin", "TestRequest", "Heartbeat 112=x | stays | c | app=0 adm=1"},
    {"AC-admin", "Logout", "Logout | Disc | c | app=0 adm=1"},
};

class W6ActiveOff : public testing::TestWithParam<KeyedCell> {};
TEST_P(W6ActiveOff, Ending) {
    const KeyedCell& c = GetParam();
    expect_ending(run_keyed_cell(c.shape, false, Arm::Active, input(c.row.input)),
                  expected_on(c.row, c.shape));
}
INSTANTIATE_TEST_SUITE_P(Off, W6ActiveOff, testing::ValuesIn(on_every_shape(kActiveOff)),
                         keyed_cell_name);

class W6PerShape : public testing::TestWithParam<Shape> {};
INSTANTIATE_TEST_SUITE_P(Named, W6PerShape, testing::Values(Shape::T, Shape::A, Shape::L),
                         [](const testing::TestParamInfo<Shape>& i) {
                             return std::string{shape_name(i.param)};
                         });

// `D…A` with Logon fields establishes, pinned as disclosed (B-row).
TEST_P(W6PerShape, DThenAWithLogonFields_Establishes) {
    expect_ending(
        run_keyed_cell(GetParam(), false, Arm::NotConnected, input("rep35 D..A logon-fields")),
        "Logon | Active | c | app=0 adm=0");
}

// `off-pair`: T, a malformed pair only the route's table declares takes dispose_unparseable_'s
// Reject, where S's hooks would read past it and deliver the frame (data-model E-7).
TEST(W6OffPair, MalformedRouteOnlyPair_FaultReject) {
    const CellRun run =
        run_own_cell("W-6 off-pair", own_fixture(Shape::T, {fixt11(), fix50sp2_with_body_pair()}),
                     false, Arm::Active, [](const Rig& r) {
                         return frame_35("D", r.peer_ctx(2),
                                         kNos +
                                             "5030=50\x01"
                                             "5031=x\x01");
                     });
    expect_ending(run, "R 5/5030 D 45=2 | stays | c | app=0 adm=0");
}

// `off-hops`: A, a NOS with NoHops: delivered and get(628) unchanged, while group(627) reads one
// instance, the metadata data-model E-3's overload adds.
TEST(W6OffHops, ShapeA_ViewReadsNoHops) {
    int instances = -1;
    std::string hop;
    CellRun run = enter_arm(shape_fixture(Shape::A), Arm::Active, [] {
        RigOptions o;
        o.validate = false;
        return o;
    }());
    ASSERT_TRUE(run.setup_failure.empty()) << run.setup_failure;
    run.rig->app->on_app = [&](const CountingApp::View& v) {
        instances = static_cast<int>(v.offsets().group_slices(627).size());
        hop = field_text(v, 628);
    };
    run.ending = run.rig->deliver(frame_35("D", run.rig->peer_ctx(2),
                                           kNos + "627=1\x01"
                                                  "628=X\x01"));
    expect_ending(run, "- | stays | c | app=1 adm=0");
    EXPECT_EQ(hop, "X");
    EXPECT_EQ(instances, 1);
}

// An admin frame with no transport dictionary uses S (data-model E-6's transport route): S's own
// overlay declares a pair on Heartbeat, and the view reads its Data whole.
TEST(W6AdminNoTransport, ShapeA_HeartbeatUsesS) {
    static const DictPtr s =
        overlay_on("FIX50SP2.xml",
                   "<fix type='FIX' major='5' minor='0' servicepack='2'><messages>"
                   "<message name='Heartbeat' msgtype='0' msgcat='admin'>" +
                       kPairMembers + "</message></messages>" + kPairFields + "</fix>");
    Fixture f;
    f.registry = make_registry({s});
    f.session_dict = s;
    RigOptions o;
    o.validate = false;
    CellRun run = enter_arm(std::move(f), Arm::Active, std::move(o));
    ASSERT_TRUE(run.setup_failure.empty()) << run.setup_failure;
    std::string data;
    std::string seq;
    run.rig->app->on_admin = [&](const CountingApp::View& v) {
        data = field_text(v, 5031);
        seq = field_text(v, 34);
    };
    const std::string embedded =
        "ab\x01"
        "34=7";
    run.ending = run.rig->deliver(frame_35("0", run.rig->peer_ctx(2), pair_field(embedded)));
    expect_ending(run, "- | stays | c | app=0 adm=1");
    EXPECT_EQ(data, embedded);
    EXPECT_EQ(seq, "2");
}

// No refusal at open() or at register_session when not validating (owner item 5): shape A with no
// FIXT11 in the engine.
TEST(W6NoRefusal, ShapeA_NoTransport_OpensAndRegisters) {
    RigOptions o;
    o.validate = false;
    const CellRun direct =
        enter_arm(own_fixture(Shape::A, {fix50sp2()}), Arm::NotConnected, std::move(o));
    EXPECT_TRUE(direct.setup_failure.empty()) << direct.setup_failure;

    EngineProbe probe{{fix50sp2()}};
    const auto r = probe.engine->register_session(
        plain_fixt_config(probe.ioc, fix50sp2(), false, dd::application_version::v50sp2));
    EXPECT_TRUE(r.has_value());
}

}  // namespace
}  // namespace fixpp::session::test099
