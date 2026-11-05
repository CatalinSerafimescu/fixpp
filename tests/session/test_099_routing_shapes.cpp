// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_099_routing_shapes.cpp
//
// 099-fixt-appl-version-validation, contracts/witnesses.md W-1 (tasks.md T008): each message
// answered by its own version's dictionary, on T, A, L and on a FIX44 S with FIXT11 in the
// engine, acceptor. Each cell's literal is the spec's US1 and data-model E-7's `AC-d`.
//
// spec.md's "today" table says what each cell answered before this feature, and the evidence file's
// T020 record which cells it moved.

#include <gtest/gtest.h>

#include <string>

#include "test_099_fixt_support.hpp"

namespace fixpp::session::test099 {
namespace {

constexpr const char* kLogonOk = "Logon | Active | c | app=0 adm=0";
constexpr const char* kAdminDelivered = "- | stays | c | app=0 adm=1";
constexpr const char* kTestRequestAnswered = "Heartbeat 112=x | stays | c | app=0 adm=1";
constexpr const char* kDelivered = "- | stays | c | app=1 adm=0";

const KeyedRow kSharedActive[] = {
    {.cell = "W1", .input = "Heartbeat", .t = kAdminDelivered},
    {.cell = "W1", .input = "TestRequest", .t = kTestRequestAnswered},
    {.cell = "W1", .input = "NOS", .t = kDelivered},
    {.cell = "W1", .input = "NOS 1128=9", .t = kDelivered},
    {.cell = "W1", .input = "NOS 1128=10", .t = kDelivered},
};

class W1Shapes : public testing::TestWithParam<KeyedCell> {};

TEST_P(W1Shapes, Active) {
    const KeyedCell& c = GetParam();
    const CellRun run = run_keyed_cell(c.shape, /*validate=*/true, Arm::Active, input(c.row.input));
    expect_ending(run, expected_on(c.row, c.shape));
}

INSTANTIATE_TEST_SUITE_P(W1, W1Shapes, testing::ValuesIn(on_every_shape(kSharedActive)),
                         keyed_cell_name);

class W1Logon : public testing::TestWithParam<Shape> {};

TEST_P(W1Logon, Established) {
    const CellRun run =
        run_keyed_cell(GetParam(), /*validate=*/true, Arm::NotConnected, input("Logon 1137=9"));
    expect_ending(run, kLogonOk);
}

INSTANTIATE_TEST_SUITE_P(W1, W1Logon, testing::Values(Shape::T, Shape::A, Shape::L),
                         [](const testing::TestParamInfo<Shape>& i) {
                             return std::string{shape_name(i.param)};
                         });

// A Heartbeat with a body field. A header-only one passes on an application table too, whose closed
// MsgType holds 0 and whose framing surface answers the header, so only a body field shows which
// table judged an admin frame in Active.
class W1HeartbeatWithBody : public testing::TestWithParam<Shape> {};

TEST_P(W1HeartbeatWithBody, Delivered) {
    const Shape s = GetParam();
    const CellRun run = run_own_cell(
        std::string("W-1 Heartbeat 112 ") + shape_name(s), shape_fixture(s), /*validate=*/true,
        Arm::Active, [](const Rig& r) { return frame_35("0", r.peer_ctx(2), "112=x\x01"); });
    expect_ending(run, kAdminDelivered);
}

INSTANTIATE_TEST_SUITE_P(W1, W1HeartbeatWithBody, testing::Values(Shape::T, Shape::A, Shape::L),
                         [](const testing::TestParamInfo<Shape>& i) {
                             return std::string{shape_name(i.param)};
                         });

// ── `NOS 1128=6`: FIX44 registered on every shape, own registry (US1 scenario 4) ────────

class W1Fix44Registered : public testing::TestWithParam<Shape> {};

TEST_P(W1Fix44Registered, Nos1128_6_Delivered) {
    const Shape s = GetParam();
    const CellRun run = run_own_cell(
        std::string("W-1 NOS 1128=6 ") + shape_name(s),
        own_fixture(s, {fixt11(), fix50sp2(), latest(), fix44()}), /*validate=*/true, Arm::Active,
        [](const Rig& r) { return frame_35("D", r.peer_ctx(2), kNos(), "1128=6\x01"); });
    expect_ending(run, kDelivered);
}

INSTANTIATE_TEST_SUITE_P(W1, W1Fix44Registered, testing::Values(Shape::T, Shape::A, Shape::L),
                         [](const testing::TestParamInfo<Shape>& i) {
                             return std::string{shape_name(i.param)};
                         });

// ── A FIX44 S with FIXT11 in the engine ─────────────────────────────────────────────────

Fixture fix44_s_fixture() {
    Fixture f;
    f.registry = make_registry({fixt11(), fix44(), fix50sp2(), latest()});
    f.session_dict = fix44();
    f.default_appl = dd::application_version::v44;
    f.peer_1137 = "6";
    return f;
}

TEST(W1Fix44S, Logon) {
    const CellRun run =
        run_own_cell("W-1 FIX44 S Logon", fix44_s_fixture(), /*validate=*/true, Arm::NotConnected,
                     [](const Rig& r) { return logon_frame(r.peer_ctx(1), "1137=6\x01"); });
    expect_ending(run, kLogonOk);
}

TEST(W1Fix44S, Heartbeat) {
    const CellRun run =
        run_own_cell("W-1 FIX44 S Heartbeat", fix44_s_fixture(), /*validate=*/true, Arm::Active,
                     [](const Rig& r) { return frame_35("0", r.peer_ctx(2)); });
    expect_ending(run, kAdminDelivered);
}

TEST(W1Fix44S, TestRequest) {
    const CellRun run =
        run_own_cell("W-1 FIX44 S TestRequest", fix44_s_fixture(), /*validate=*/true, Arm::Active,
                     [](const Rig& r) { return frame_35("1", r.peer_ctx(2), "112=x\x01"); });
    expect_ending(run, kTestRequestAnswered);
}

TEST(W1Fix44S, Nos) {
    const CellRun run =
        run_own_cell("W-1 FIX44 S NOS", fix44_s_fixture(), /*validate=*/true, Arm::Active,
                     [](const Rig& r) { return frame_35("D", r.peer_ctx(2), kNos()); });
    expect_ending(run, kDelivered);
}

TEST(W1Fix44S, Nos1128_6) {
    const CellRun run = run_own_cell(
        "W-1 FIX44 S NOS 1128=6", fix44_s_fixture(), /*validate=*/true, Arm::Active,
        [](const Rig& r) { return frame_35("D", r.peer_ctx(2), kNos(), "1128=6\x01"); });
    expect_ending(run, kDelivered);
}

TEST(W1Fix44S, Nos1128_9) {
    const CellRun run = run_own_cell(
        "W-1 FIX44 S NOS 1128=9", fix44_s_fixture(), /*validate=*/true, Arm::Active,
        [](const Rig& r) { return frame_35("D", r.peer_ctx(2), kNos(), "1128=9\x01"); });
    expect_ending(run, kDelivered);
}

TEST(W1Fix44S, Nos1128_10) {
    const CellRun run = run_own_cell(
        "W-1 FIX44 S NOS 1128=10", fix44_s_fixture(), /*validate=*/true, Arm::Active,
        [](const Rig& r) { return frame_35("D", r.peer_ctx(2), kNos(), "1128=10\x01"); });
    expect_ending(run, kDelivered);
}

// ── 370, on T with FIX42 and FIX50SP2 registered ────────────────────────────────────────

Fixture t_with_fix42() { return own_fixture(Shape::T, {fixt11(), fix42(), fix50sp2()}); }

constexpr const char* k370 = "370=20240101-00:00:00.000\x01";

std::string nos_with_header(const Rig& r, const std::string& before_34) {
    return frame_35("D", r.peer_ctx(2), kNos(), before_34);
}

TEST(W1Tag370, After1128_FIX42Route_Delivered) {
    const CellRun run = run_own_cell(
        "W-1 1128=4 370", t_with_fix42(), /*validate=*/true, Arm::Active,
        [](const Rig& r) { return nos_with_header(r, std::string("1128=4\x01") + k370); });
    expect_ending(run, kDelivered);
}

TEST(W1Tag370, Before1128_FIX42Route_Delivered) {
    const CellRun run = run_own_cell(
        "W-1 370 1128=4", t_with_fix42(), /*validate=*/true, Arm::Active,
        [](const Rig& r) { return nos_with_header(r, std::string(k370) + "1128=4\x01"); });
    expect_ending(run, kDelivered);
}

TEST(W1Tag370, After1128_FIX50SP2Route_Rejected) {
    const CellRun run = run_own_cell(
        "W-1 1128=9 370", t_with_fix42(), /*validate=*/true, Arm::Active,
        [](const Rig& r) { return nos_with_header(r, std::string("1128=9\x01") + k370); });
    expect_ending(run, "R 2/370 D 45=2 | stays | c | app=0 adm=0");
}

TEST(W1Tag370, Before1128_FIX50SP2Route_Rejected) {
    const CellRun run = run_own_cell(
        "W-1 370 1128=9", t_with_fix42(), /*validate=*/true, Arm::Active,
        [](const Rig& r) { return nos_with_header(r, std::string(k370) + "1128=9\x01"); });
    expect_ending(run, "R 2/370 D 45=2 | stays | c | app=0 adm=0");
}

// ── The one-way fallback: T with registry {FIXT11, FIX Latest} ──────────────────────────

Fixture t_latest_only() { return own_fixture(Shape::T, {fixt11(), latest()}); }

TEST(W1OneWayFallback, LogonNegotiates9) {
    const CellRun run =
        run_own_cell("W-1 fallback Logon", t_latest_only(), /*validate=*/true, Arm::NotConnected,
                     [](const Rig& r) { return logon_frame(r.peer_ctx(1), "1137=9\x01"); });
    expect_ending(run, kLogonOk);
}

TEST(W1OneWayFallback, NosNo1128RoutedToLatest) {
    const CellRun run = run_own_cell(
        "W-1 fallback NOS 3115", t_latest_only(), /*validate=*/true, Arm::Active,
        [](const Rig& r) { return frame_35("D", r.peer_ctx(2), kNos() + "3115=1.5\x01"); });
    expect_ending(run, kDelivered);
}

// ── One cell per shape keeps open()'s private build (tasks.md T007) ─────────────────────
//
// Its endings must equal the shared set's, so the shared set cannot hide a defect in open()'s
// branch.

class W1PrivateBuild : public testing::TestWithParam<Shape> {};

TEST_P(W1PrivateBuild, WalkEqualsTheSharedCell) {
    const Shape s = GetParam();
    const auto nos = [](const Rig& r) {
        return frame_35("D", r.peer_ctx(3), kNos() + "3115=1.5\x01", "1128=10\x01");
    };
    const auto walk = [&](bool private_build) -> std::string {
        RigOptions o;
        o.private_build = private_build;
        CellRun run = enter_arm(shape_fixture(s), Arm::NotConnected, std::move(o));
        if (!run.setup_failure.empty()) {
            return run.setup_failure;
        }
        std::string text;
        text += short_form(run.rig->deliver(logon_frame(run.rig->peer_ctx(1), "1137=9\x01")));
        text += " || " + short_form(run.rig->deliver(frame_35("0", run.rig->peer_ctx(2))));
        text += " || " + short_form(run.rig->deliver(nos(*run.rig)));
        return text;
    };
    const std::string shared = walk(false);
    EXPECT_EQ(walk(true), shared);
    EXPECT_EQ(shared,
              "Logon | Active | c | app=0 adm=0 || - | stays | c | app=0 adm=1 || "
              "- | stays | c | app=1 adm=0");
}

INSTANTIATE_TEST_SUITE_P(W1, W1PrivateBuild, testing::Values(Shape::T, Shape::A, Shape::L),
                         [](const testing::TestParamInfo<Shape>& i) {
                             return std::string{shape_name(i.param)};
                         });

}  // namespace
}  // namespace fixpp::session::test099
