// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_099_dispatch_sites.cpp
//
// 099-fixt-appl-version-validation, contracts/witnesses.md W-7 (tasks.md T014): the view handed to
// the application follows the route at each dispatch site of research R-7. Application cells at
// D-2, D-4 and D-5 on T; admin cells at D-1, D-3, D-5, D-6 and D-7 on A with an engine FIXT11 whose
// overlay declares a pair on that admin message, so S's table splits it wrongly. Validation on and
// off. Then the gate cell and the LogoutSent scan cell.
//
// The comparands are the frame builder's: MsgSeqNum and the Data bytes, and the group count
// against the frame, the accessor against the resolved slot, `Unknown` on every admin frame.

#include <gtest/gtest.h>

#include <string>

#include "test_099_fixt_support.hpp"

namespace fixpp::session::test099 {
namespace {

// What the callback's view read.
struct Seen {
    int calls = 0;
    std::string seq;
    std::string data;
    int group_instances = -1;
    dd::application_version resolved = dd::application_version::Unknown;
};

void watch(Rig& r, Seen& seen) {
    const auto read = [&seen](const CountingApp::View& v) {
        ++seen.calls;
        seen.seq = field_text(v, 34);
        seen.data = field_text(v, 5031);
        seen.group_instances = static_cast<int>(v.offsets().group_slices(453).size());
        seen.resolved = v.resolved_application_version();
    };
    r.app->on_app = read;
    r.app->on_admin = read;
}

// A body pair whose Data embeds SOH, 34=7 and another version's 1128.
const std::string kEmbedded =
    "ab\x01"
    "34=7\x01"
    "1128=10";
const std::string kOneParty =
    "453=1\x01"
    "448=P\x01"
    "447=D\x01"
    "452=1\x01";

std::string app_body() { return kNos + pair_field(kEmbedded) + kOneParty; }

Fixture t_routed_pair() { return own_fixture(Shape::T, {fixt11(), fix50sp2_with_body_pair()}); }

class W7Application : public testing::TestWithParam<bool> {};

INSTANTIATE_TEST_SUITE_P(W7, W7Application, testing::Bool(),
                         [](const testing::TestParamInfo<bool>& i) {
                             return std::string(i.param ? "on" : "off");
                         });

void expect_view(const Seen& seen, unsigned seq) {
    EXPECT_EQ(seen.calls, 1);
    EXPECT_EQ(seen.seq, std::to_string(seq)) << "MsgSeqNum against the frame builder's";
    EXPECT_EQ(seen.data, kEmbedded) << "the Data value whole";
    EXPECT_EQ(seen.group_instances, 1) << "the group count against the frame";
    EXPECT_EQ(seen.resolved, dd::application_version::v50sp2) << "the slot 1137=9 resolved";
}

// D-2: `on_inbound_active_`'s fromApp.
TEST_P(W7Application, D2_InSequence) {
    RigOptions o;
    o.validate = GetParam();
    CellRun run = enter_arm(t_routed_pair(), Arm::Active, std::move(o));
    if (!run.setup_failure.empty()) {
        ADD_FAILURE() << "setup: " << run.setup_failure;
        return;
    }
    Seen seen;
    watch(*run.rig, seen);
    run.ending = run.rig->deliver(frame_35("D", run.rig->peer_ctx(2), app_body()));
    expect_ending(run, "- | stays | c | app=1 adm=0");
    expect_view(seen, 2);
}

// D-4: `on_inbound_active_out_of_sequence_`'s PossDup redelivery.
TEST_P(W7Application, D4_PossDupRedelivery) {
    RigOptions o;
    o.validate = GetParam();
    o.tweak = [](SessionConfig& cfg) { cfg.redeliver_poss_dup = true; };
    CellRun run = enter_arm(t_routed_pair(), Arm::Active, std::move(o));
    if (!run.setup_failure.empty()) {
        ADD_FAILURE() << "setup: " << run.setup_failure;
        return;
    }
    Rig& r = *run.rig;
    (void)r.deliver(frame_35("D", r.peer_ctx(2), kNos));
    const int before = r.app->app;
    Seen seen;
    watch(r, seen);
    const std::string possdup = wire(kFT, "35=D\x01" + hdr(r.peer_ctx(2)) +
                                              "43=Y\x01"
                                              "122=20240101-00:00:00.000\x01" +
                                              app_body());
    run.ending = r.deliver(possdup);
    EXPECT_EQ(run.ending.from_app, before + 1) << log_form(run.ending);
    EXPECT_EQ(run.ending.next_in_after, run.ending.next_in_before) << log_form(run.ending);
    expect_view(seen, 2);
}

// D-5: `on_inbound_active_out_of_sequence_` with validate_sequence_numbers off.
TEST_P(W7Application, D5_SequenceChecksOff) {
    RigOptions o;
    o.validate = GetParam();
    o.tweak = [](SessionConfig& cfg) { cfg.validate_sequence_numbers = false; };
    CellRun run = enter_arm(t_routed_pair(), Arm::Active, std::move(o));
    if (!run.setup_failure.empty()) {
        ADD_FAILURE() << "setup: " << run.setup_failure;
        return;
    }
    Seen seen;
    watch(*run.rig, seen);
    run.ending = run.rig->deliver(frame_35("D", run.rig->peer_ctx(9), app_body()));
    expect_ending(run, "- | stays | k | app=1 adm=0");
    expect_view(seen, 9);
}

// ── Admin cells, on A with an engine FIXT11 whose overlay declares the pair ─────────────

const DictPtr& fixt11_with_admin_pair() {
    static const DictPtr d = overlay_on(
        "FIXT11.xml",
        "<fix type='FIXT' major='1' minor='1' servicepack='0'><messages>"
        "<message name='Heartbeat' msgtype='0' msgcat='admin'>" +
            kPairMembers + "</message><message name='TestRequest' msgtype='1' msgcat='admin'>" +
            kPairMembers + "</message><message name='SequenceReset' msgtype='4' msgcat='admin'>" +
            kPairMembers + "</message><message name='Logout' msgtype='5' msgcat='admin'>" +
            kPairMembers + "</message></messages>" + kPairFields + "</fix>");
    return d;
}

Fixture a_with_admin_pair() {
    Fixture f;
    f.registry = make_registry({fixt11_with_admin_pair(), fix50sp2()});
    f.session_dict = fix50sp2();
    return f;
}

void expect_admin_view(const Seen& seen, unsigned seq) {
    EXPECT_EQ(seen.calls, 1);
    EXPECT_EQ(seen.seq, std::to_string(seq)) << "MsgSeqNum against the frame builder's";
    EXPECT_EQ(seen.data, kEmbedded) << "the Data value whole";
    EXPECT_EQ(seen.resolved, dd::application_version::Unknown) << "an admin message";
}

class W7Admin : public testing::TestWithParam<bool> {};

INSTANTIATE_TEST_SUITE_P(W7, W7Admin, testing::Bool(), [](const testing::TestParamInfo<bool>& i) {
    return std::string(i.param ? "on" : "off");
});

// D-1: `on_inbound_active_`'s fromAdmin, a Heartbeat.
TEST_P(W7Admin, D1_Heartbeat) {
    RigOptions o;
    o.validate = GetParam();
    CellRun run = enter_arm(a_with_admin_pair(), Arm::Active, std::move(o));
    if (!run.setup_failure.empty()) {
        ADD_FAILURE() << "setup: " << run.setup_failure;
        return;
    }
    Seen seen;
    watch(*run.rig, seen);
    run.ending = run.rig->deliver(frame_35("0", run.rig->peer_ctx(2), pair_field(kEmbedded)));
    expect_ending(run, "- | stays | c | app=0 adm=1");
    expect_admin_view(seen, 2);
}

// D-3: `on_inbound_active_sequence_reset_`, reset mode.
TEST_P(W7Admin, D3_SequenceResetResetMode) {
    RigOptions o;
    o.validate = GetParam();
    CellRun run = enter_arm(a_with_admin_pair(), Arm::Active, std::move(o));
    if (!run.setup_failure.empty()) {
        ADD_FAILURE() << "setup: " << run.setup_failure;
        return;
    }
    Seen seen;
    watch(*run.rig, seen);
    run.ending =
        run.rig->deliver(frame_35("4", run.rig->peer_ctx(2), "36=9\x01" + pair_field(kEmbedded)));
    expect_ending(run, "- | stays | c | app=0 adm=1");
    expect_admin_view(seen, 2);
    EXPECT_EQ(run.rig->next_in(), 9U) << "the reset's NewSeqNo";
}

// D-5 for admin: an out-of-sequence admin frame with validate_sequence_numbers off. A TestRequest,
// since an out-of-sequence Heartbeat reaches no callback on any path.
TEST_P(W7Admin, D5_SequenceChecksOff) {
    RigOptions o;
    o.validate = GetParam();
    o.tweak = [](SessionConfig& cfg) { cfg.validate_sequence_numbers = false; };
    CellRun run = enter_arm(a_with_admin_pair(), Arm::Active, std::move(o));
    if (!run.setup_failure.empty()) {
        ADD_FAILURE() << "setup: " << run.setup_failure;
        return;
    }
    Seen seen;
    watch(*run.rig, seen);
    run.ending =
        run.rig->deliver(frame_35("1", run.rig->peer_ctx(9), "112=x\x01" + pair_field(kEmbedded)));
    expect_ending(run, "- | stays | k | app=0 adm=1");
    expect_admin_view(seen, 9);
}

// D-6: `on_inbound_active_gap_fill_`, whose dispatch stands behind validate_sequence_numbers off.
TEST_P(W7Admin, D6_GapFill) {
    RigOptions o;
    o.validate = GetParam();
    o.tweak = [](SessionConfig& cfg) { cfg.validate_sequence_numbers = false; };
    CellRun run = enter_arm(a_with_admin_pair(), Arm::Active, std::move(o));
    if (!run.setup_failure.empty()) {
        ADD_FAILURE() << "setup: " << run.setup_failure;
        return;
    }
    Seen seen;
    watch(*run.rig, seen);
    run.ending = run.rig->deliver(frame_35("4", run.rig->peer_ctx(2),
                                           "123=Y\x01"
                                           "36=9\x01" +
                                               pair_field(kEmbedded)));
    expect_ending(run, "- | stays | c | app=0 adm=1");
    expect_admin_view(seen, 2);
}

// D-7: `on_inbound_active_logout_`.
TEST_P(W7Admin, D7_Logout) {
    RigOptions o;
    o.validate = GetParam();
    CellRun run = enter_arm(a_with_admin_pair(), Arm::Active, std::move(o));
    if (!run.setup_failure.empty()) {
        ADD_FAILURE() << "setup: " << run.setup_failure;
        return;
    }
    Seen seen;
    watch(*run.rig, seen);
    run.ending = run.rig->deliver(frame_35("5", run.rig->peer_ctx(2), pair_field(kEmbedded)));
    expect_ending(run, "Logout | Disc | c | app=0 adm=1");
    expect_admin_view(seen, 2);
}

// ── The gate cell: the #426 hazard ──────────────────────────────────────────────────────

// A required field appearing only inside a custom pair's Data is missing: the gate parses with the
// route's table, so `get` does not find it there.
TEST(W7Gate, RequiredFieldOnlyInsideAPairsData_Missing) {
    const CellRun run =
        run_own_cell("W-7 gate", t_routed_pair(), true, Arm::Active, [](const Rig& r) {
            return frame_35("D", r.peer_ctx(2),
                            kNosNo11 + pair_field("\x01"
                                                  "11=C"));
        });
    expect_ending(run, "R 1/11 D 45=2 | stays | c | app=0 adm=0");
}

// ── The LogoutSent scan: a body pair the route declares, Data `SOH 35=5 SOH` ────────────

TEST(W7LogoutSentScan, Data35Is5_NotTakenAsTheReply) {
    const CellRun run = run_own_cell("W-7 LogoutSent scan", t_routed_pair(), true, Arm::LogoutSent,
                                     [](const Rig& r) {
                                         return frame_35("D", r.peer_ctx(2),
                                                         kNos + pair_field("\x01"
                                                                           "35=5"));
                                     });
    expect_ending(run, "- | stays | k | app=0 adm=0");
}

}  // namespace
}  // namespace fixpp::session::test099
