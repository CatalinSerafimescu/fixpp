// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_099_validation_off.cpp
//
// 099-fixt-appl-version-validation, contracts/witnesses.md W-6 (tasks.md T013): with validation off
// every W-4 input ends as research R-18's `off` line says, a literal per cell, whose mutants are
// M-28 and M-29. `off-pair` and `off-hops` are the two the row states differ from that log's tree.
// Then the accessor's cells and data-model E-5's splits against `dict::reify`.

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <fixpp/dict/reify.hpp>
#include <memory>
#include <memory_resource>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "mock_reconnect_factory.hpp"
#include "test_099_fixt_support.hpp"

namespace fixpp::session::test099 {
namespace {

const KeyedRow kActiveOff[] = {
    {.cell = "AC-a",
     .input = "rep35 D..0 past seq+5",
     .t = "ResendRequest 2-0 | stays | k | app=0 adm=0"},
    {.cell = "AC-b", .input = "1128=99 hdr", .t = "- | stays | c | app=1 adm=0"},
    {.cell = "AC-b", .input = "1128=abc hdr", .t = "- | stays | c | app=1 adm=0"},
    {.cell = "AC-b", .input = "1128=4 hdr", .t = "- | stays | c | app=1 adm=0"},
    {.cell = "AC-b", .input = "1128=empty hdr", .t = "- | stays | c | app=1 adm=0"},
    {.cell = "AC-c", .input = "1128=6 past", .t = "- | stays | c | app=1 adm=0"},
    {.cell = "AC-c", .input = "1128=abc past", .t = "- | stays | c | app=1 adm=0"},
    {.cell = "AC-d", .input = "NOS", .t = "- | stays | c | app=1 adm=0"},
    {.cell = "AC-d", .input = "NOS 1128=9", .t = "- | stays | c | app=1 adm=0"},
    {.cell = "AC-d", .input = "NOS 1128=10", .t = "- | stays | c | app=1 adm=0"},
    {.cell = "AC-d", .input = "NOS missing 11", .t = "- | stays | c | app=1 adm=0"},
    {.cell = "AC-d",
     .input = "NOS missing 11 seq+5",
     .t = "ResendRequest 2-0 | stays | k | app=0 adm=0"},
    {.cell = "AC-e", .input = "Logon 1137=9", .t = "- | stays | c | app=1 adm=0"},
    {.cell = "AC-e", .input = "Logon 1137=10", .t = "- | stays | c | app=1 adm=0"},
    {.cell = "AC-e", .input = "Logon 1137 absent", .t = "- | stays | c | app=1 adm=0"},
    {.cell = "AC-e", .input = "Logon 1137=empty", .t = "- | stays | c | app=1 adm=0"},
    {.cell = "AC-e", .input = "Logon 1137=99", .t = "- | stays | c | app=1 adm=0"},
    {.cell = "AC-e", .input = "Logon undeclared 5999", .t = "- | stays | c | app=1 adm=0"},
    {.cell = "AC-e", .input = "Logon 1137=abc", .t = "- | stays | c | app=1 adm=0"},
    {.cell = "AC-e", .input = "Logon 1137=4", .t = "- | stays | c | app=1 adm=0"},
    {.cell = "AC-f", .input = "wrongBS Logon 1137=9", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "AC-f", .input = "wrongBS Logon 1137=99", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "AC-f", .input = "wrongBS Logon 1137=4", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "AC-f", .input = "wrongBS Logon 1137 absent", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "AC-f", .input = "wrongBS NOS", .t = "- | Disc | k | app=0 adm=0"},
    {.cell = "AC-g",
     .input = "stale Logon 1137=9",
     .t = "R 10/52 A 45=2 + Logout | Disc | k | app=0 adm=0"},
    {.cell = "AC-g",
     .input = "stale Logon 1137=99",
     .t = "R 10/52 A 45=2 + Logout | Disc | k | app=0 adm=0"},
    {.cell = "AC-g",
     .input = "stale Logon 1137=4",
     .t = "R 10/52 A 45=2 + Logout | Disc | k | app=0 adm=0"},
    {.cell = "AC-g",
     .input = "stale Logon 1137 absent",
     .t = "R 10/52 A 45=2 + Logout | Disc | k | app=0 adm=0"},
    {.cell = "AC-g", .input = "stale NOS", .t = "R 10/52 D 45=2 + Logout | Disc | c | app=0 adm=0"},
    {.cell = "AC-g",
     .input = "stale Heartbeat",
     .t = "R 10/52 0 45=2 + Logout | Disc | c | app=0 adm=0"},
    {.cell = "AC-admin", .input = "Heartbeat 1128=99 hdr", .t = "- | stays | c | app=0 adm=1"},
    {.cell = "AC-admin", .input = "Heartbeat 1128=10 hdr", .t = "- | stays | c | app=0 adm=1"},
    {.cell = "AC-admin",
     .input = "TestRequest 1128=6 past",
     .t = "Heartbeat 112=x | stays | c | app=0 adm=1"},
    {.cell = "AC-admin", .input = "Heartbeat", .t = "- | stays | c | app=0 adm=1"},
    {.cell = "AC-admin",
     .input = "TestRequest missing 112",
     .t = "Heartbeat 112= | stays | c | app=0 adm=1"},
    {.cell = "AC-admin", .input = "TestRequest", .t = "Heartbeat 112=x | stays | c | app=0 adm=1"},
    {.cell = "AC-admin", .input = "Logout", .t = "Logout | Disc | c | app=0 adm=1"},
};

// 101-inbound-reject-fidelity D6 (FR-006) supersedes these rows' literals (099 W-6's `rep35`
// endings, which read the last 35): on a validation-off session after establishment a frame with
// two MsgTypes ends as the same frame with every later 35 removed and re-framed, its stripped twin.
const char* const kActiveOffRepeat[] = {
    "rep35 D..0 past",         "rep35 D..3 past",         "rep35 D..4 past",
    "rep35 D..A past",         "rep35 D..5 past",         "rep35 5..D past",
    "rep35 0..D past",         "rep35 3..D past",         "rep35 4..D past",
    "rep35 A..0 past",         "rep35 D..A logon-fields", "rep35 0..A logon-fields",
    "rep35 0,D header",        "rep35 D,D header",
};

// The frame with every 35 after the first removed, BodyLength and CheckSum recomputed. The
// inputs above carry no Data field, so their fields are split at SOH.
std::string strip_later_35(const std::string& frame) {
    std::string begin;
    std::string body;
    bool seen_35 = false;
    std::size_t i = 0;
    while (i < frame.size()) {
        const std::size_t end = frame.find('\x01', i);
        const std::string field = frame.substr(i, end - i);
        i = end == std::string::npos ? frame.size() : end + 1;
        const std::string tag = field.substr(0, field.find('='));
        if (tag == "8") {
            begin = field.substr(2);
            continue;
        }
        if (tag == "9" || tag == "10") {
            continue;
        }
        if (tag == "35") {
            if (seen_35) {
                continue;
            }
            seen_35 = true;
        }
        body += field + "\x01";
    }
    return wire(begin, body);
}

struct RepeatCell {
    Shape shape;
    const char* input;
};

std::vector<RepeatCell> repeat_cells() {
    std::vector<RepeatCell> v;
    for (Shape sh : {Shape::T, Shape::A, Shape::L}) {
        for (const char* in : kActiveOffRepeat) {
            v.push_back({sh, in});
        }
    }
    return v;
}

class W6ActiveOffOracle : public testing::TestWithParam<RepeatCell> {};
TEST_P(W6ActiveOffOracle, EndsAsTheStrippedTwin) {
    const RepeatCell& c = GetParam();
    const Input& in = input(c.input);
    const Input twin{.id = in.id + " stripped",
                     .make = [&in](const Ctx& ctx) { return strip_later_35(in.make(ctx)); }};
    const CellRun run = run_keyed_cell(c.shape, false, Arm::Active, in);
    const CellRun twin_run = run_keyed_cell(c.shape, false, Arm::Active, twin);
    ASSERT_TRUE(run.setup_failure.empty()) << run.setup_failure;
    ASSERT_TRUE(twin_run.setup_failure.empty()) << twin_run.setup_failure;
    EXPECT_EQ(log_form(run.ending), log_form(twin_run.ending));
}
INSTANTIATE_TEST_SUITE_P(Off, W6ActiveOffOracle, testing::ValuesIn(repeat_cells()),
                         [](const testing::TestParamInfo<RepeatCell>& i) {
                             return std::string{shape_name(i.param.shape)} + "_" +
                                    name_token(i.param.input);
                         });

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

// 101-inbound-reject-fidelity D6 (FR-006) supersedes 099 W-6's clause that `D…A` with Logon
// fields establishes (its B-row): a frame with two MsgTypes is refused in establishment on every
// session, so the acceptor disconnects with nothing sent.
TEST_P(W6PerShape, DThenAWithLogonFields_Disconnects101) {
    expect_ending(
        run_keyed_cell(GetParam(), false, Arm::NotConnected, input("rep35 D..A logon-fields")),
        "- | Disc | k | app=0 adm=0");
}

// `off-pair`: T, a malformed pair only the route's table declares takes dispose_unparseable_'s
// Reject, where S's hooks would read past it and deliver the frame (data-model E-7).
TEST(W6OffPair, MalformedRouteOnlyPair_FaultReject) {
    const CellRun run =
        run_own_cell("W-6 off-pair", own_fixture(Shape::T, {fixt11(), fix50sp2_with_body_pair()}),
                     false, Arm::Active, [](const Rig& r) {
                         return frame_35("D", r.peer_ctx(2),
                                         kNos() +
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
                                           kNos() + "627=1\x01"
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
                       kPairMembers() + "</message></messages>" + kPairFields() + "</fix>");
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

// ── The accessor, and data-model E-5's splits against `dict::reify` ─────────────────────

using Version = dd::application_version;

// What the callback's view said, and what `dict::reify` answered with `profile`.
struct Answers {
    int calls = 0;
    Version accessor = Version::Unknown;
    std::optional<dd::resolved_message_version> reified;
    std::optional<fixpp::core::error> reify_error;
};

void watch_answers(Rig& r, Answers& a, dd::version_profile profile) {
    const auto read = [&a, profile](const CountingApp::View& v) {
        ++a.calls;
        a.accessor = v.resolved_application_version();
        auto h = dd::reify(v, profile, std::pmr::new_delete_resource());
        if (h) {
            a.reified = h->version();
        } else {
            a.reify_error = h.error();
        }
    };
    r.app->on_app = read;
    r.app->on_admin = read;
}

const dd::version_profile kVt11At50{.session = dd::session_version::vt11,
                                    .default_appl = Version::v50sp2,
                                    .has_per_message_override = true,
                                    ._reserved = 0};
const dd::version_profile kVt11Unknown{.session = dd::session_version::vt11,
                                       .default_appl = Version::Unknown,
                                       .has_per_message_override = true,
                                       ._reserved = 0};

struct ResolvedCase {
    const char* name{};
    const char* before_34{};
    Version accessor;
};

const ResolvedCase kResolvedCases[] = {
    {.name = "Negotiated", .before_34 = "", .accessor = Version::v50sp2},
    {.name = "Explicit9", .before_34 = "1128=9\x01", .accessor = Version::v50sp2},
    {.name = "Explicit10", .before_34 = "1128=10\x01", .accessor = Version::vlatest},
};

// Set when it resolved, on every shape, and `reify` agrees in all three bytes (E-5), with no help
// from the profile, which here names neither.
class W6Resolved : public testing::TestWithParam<std::tuple<Shape, ResolvedCase>> {};
TEST_P(W6Resolved, AccessorAndReifyAgree) {
    const auto& [shape, c] = GetParam();
    RigOptions o;
    o.validate = false;
    CellRun run = enter_arm(shape_fixture(shape), Arm::Active, std::move(o));
    ASSERT_TRUE(run.setup_failure.empty()) << run.setup_failure;
    Answers a;
    watch_answers(*run.rig, a,
                  dd::version_profile{.session = dd::session_version::v44,
                                      .default_appl = Version::v44,
                                      .has_per_message_override = false,
                                      ._reserved = 0});
    run.ending = run.rig->deliver(frame_35("D", run.rig->peer_ctx(2), kNos(), c.before_34));
    expect_ending(run, "- | stays | c | app=1 adm=0");
    EXPECT_EQ(a.accessor, c.accessor);
    ASSERT_TRUE(a.reified.has_value());
    EXPECT_EQ(a.reified->k, dd::resolved_message_version::kind::application);
    EXPECT_EQ(a.reified->session, dd::session_version::vt11);
    EXPECT_EQ(a.reified->application, c.accessor);
}
INSTANTIATE_TEST_SUITE_P(Named, W6Resolved,
                         testing::Combine(testing::Values(Shape::T, Shape::A, Shape::L),
                                          testing::ValuesIn(kResolvedCases)),
                         [](const testing::TestParamInfo<std::tuple<Shape, ResolvedCase>>& i) {
                             return std::string(shape_name(std::get<0>(i.param))) + "_" +
                                    std::get<1>(i.param).name;
                         });

TEST_P(W6PerShape, AdminFrame_AccessorUnknown) {
    RigOptions o;
    o.validate = false;
    CellRun run = enter_arm(shape_fixture(GetParam()), Arm::Active, std::move(o));
    ASSERT_TRUE(run.setup_failure.empty()) << run.setup_failure;
    Answers a;
    a.accessor = Version::v40;  // a seed no answer here can be, so a callback that ran must write
    watch_answers(*run.rig, a, kVt11At50);
    run.ending = run.rig->deliver(frame_35("0", run.rig->peer_ctx(2)));
    expect_ending(run, "- | stays | c | app=0 adm=1");
    EXPECT_EQ(a.accessor, Version::Unknown);
}

// An empty `1128=`: refused by nothing on this session. The accessor is `Unknown`, and `reify`
// passes the empty value to the resolver, whose rule means "use the default".
TEST(W6Splits, EmptyApplVerId_AccessorUnknown_ReifyTheProfilesDefault) {
    RigOptions o;
    o.validate = false;
    CellRun run = enter_arm(shape_fixture(Shape::T), Arm::Active, std::move(o));
    ASSERT_TRUE(run.setup_failure.empty()) << run.setup_failure;
    Answers a;
    watch_answers(*run.rig, a, kVt11At50);
    run.ending = run.rig->deliver(frame_35("D", run.rig->peer_ctx(2), kNos(), "1128=\x01"));
    expect_ending(run, "- | stays | c | app=1 adm=0");
    EXPECT_EQ(a.accessor, Version::Unknown);
    ASSERT_TRUE(a.reified.has_value());
    EXPECT_EQ(a.reified->application, Version::v50sp2);
}

// `1128=99`: the accessor is `Unknown`, and `reify` refuses with no handle.
TEST(W6Splits, UnknownApplVerId_AccessorUnknown_ReifyNoHandle) {
    RigOptions o;
    o.validate = false;
    CellRun run = enter_arm(shape_fixture(Shape::T), Arm::Active, std::move(o));
    ASSERT_TRUE(run.setup_failure.empty()) << run.setup_failure;
    Answers a;
    watch_answers(*run.rig, a, kVt11At50);
    run.ending = run.rig->deliver(frame_35("D", run.rig->peer_ctx(2), kNos(), "1128=99\x01"));
    expect_ending(run, "- | stays | c | app=1 adm=0");
    EXPECT_EQ(a.accessor, Version::Unknown);
    EXPECT_FALSE(a.reified.has_value());
    ASSERT_TRUE(a.reify_error.has_value());
    EXPECT_EQ(*a.reify_error, fixpp::core::error::dict_unknown_appl_ver_id);
}

// A past-header 1128 with a negotiated version: the byte is set, so they agree.
TEST(W6Splits, PastHeader1128_Negotiated_TheyAgree) {
    RigOptions o;
    o.validate = false;
    CellRun run = enter_arm(shape_fixture(Shape::T), Arm::Active, std::move(o));
    ASSERT_TRUE(run.setup_failure.empty()) << run.setup_failure;
    Answers a;
    watch_answers(*run.rig, a, kVt11At50);
    run.ending = run.rig->deliver(frame_35("D", run.rig->peer_ctx(2), kNos() + "1128=6\x01"));
    expect_ending(run, "- | stays | c | app=1 adm=0");
    EXPECT_EQ(a.accessor, Version::v50sp2);
    ASSERT_TRUE(a.reified.has_value());
    EXPECT_EQ(a.reified->application, Version::v50sp2);
}

// A past-header 1128 on an initiator whose reply had no 1137: step 6 leaves the byte `Unknown`, and
// `reify` reads the first occurrence with no position test.
TEST(W6Splits, PastHeader1128_ReplyWithNo1137_ReifyReadsIt) {
    RigOptions o;
    o.validate = false;
    o.role = session_role::initiator;
    CellRun run = enter_arm(shape_fixture(Shape::T), Arm::LogonSent, std::move(o));
    ASSERT_TRUE(run.setup_failure.empty()) << run.setup_failure;
    Rig& r = *run.rig;
    (void)r.deliver(logon_frame(r.peer_ctx(1), ""));
    ASSERT_EQ(r.sess->state(), fsm_state::Active);
    Answers a;
    watch_answers(r, a, kVt11Unknown);
    run.ending = r.deliver(frame_35("D", r.peer_ctx(2), kNos() + "1128=6\x01"));
    expect_ending(run, "- | stays | c | app=1 adm=0");
    EXPECT_EQ(a.accessor, Version::Unknown);
    ASSERT_TRUE(a.reified.has_value());
    EXPECT_EQ(a.reified->application, Version::v44);
}

// A negotiated miss: initiator, `1137=4`, no dictionary: S's view, and both name the version.
TEST(W6Splits, NegotiatedMiss_SsView_AccessorV42_ReifyV42) {
    RigOptions o;
    o.validate = false;
    o.role = session_role::initiator;
    CellRun run = enter_arm(shape_fixture(Shape::T), Arm::LogonSent, std::move(o));
    ASSERT_TRUE(run.setup_failure.empty()) << run.setup_failure;
    Rig& r = *run.rig;
    (void)r.deliver(logon_frame(r.peer_ctx(1), "1137=4\x01"));
    ASSERT_EQ(r.sess->state(), fsm_state::Active);
    Answers a;
    watch_answers(r, a, kVt11Unknown);
    run.ending = r.deliver(frame_35("D", r.peer_ctx(2), kNos()));
    expect_ending(run, "- | stays | c | app=1 adm=0");
    EXPECT_EQ(a.accessor, Version::v42);
    ASSERT_TRUE(a.reified.has_value());
    EXPECT_EQ(a.reified->application, Version::v42);
}

// An initiator's reconnect whose reply has no 1137: the session's route is `Unknown`, while
// negotiated_version_profile() keeps the previous value, so `reify` with that profile labels the
// frame with the previous connection's version.
TEST(W6Splits, ReconnectWithNo1137_AccessorUnknown_ReifyThePreviousVersion) {
    RigOptions o;
    o.validate = false;
    o.role = session_role::initiator;
    o.tweak = [](SessionConfig& cfg) {
        cfg.transport_factory_override =
            std::make_shared<fixpp::test_support::MockReconnectFactory>();
        cfg.reconnect_endpoint = fixpp::transport::Endpoint{"127.0.0.1", 19099};
    };
    CellRun run = enter_arm(shape_fixture(Shape::T), Arm::LogonSent, std::move(o));
    ASSERT_TRUE(run.setup_failure.empty()) << run.setup_failure;
    Rig& r = *run.rig;
    (void)r.deliver(logon_frame(r.peer_ctx(1), "1137=9\x01"));
    ASSERT_EQ(r.sess->state(), fsm_state::Active);
    auto fut = asio::co_spawn(r.ioc, r.sess->drive_reconnect(), asio::use_future);
    ASSERT_TRUE(pump_ready(r.ioc, fut, std::chrono::seconds{30}));
    ASSERT_TRUE(fut.get().has_value());
    for (int i = 0; i < 4; ++i) {
        r.ioc.restart();
        r.ioc.poll();
    }
    ASSERT_EQ(r.sess->state(), fsm_state::LogonSent);
    (void)r.deliver(logon_frame(r.peer_ctx(r.next_in()), ""));
    ASSERT_EQ(r.sess->state(), fsm_state::Active);
    Answers a;
    watch_answers(r, a, r.sess->negotiated_version_profile());
    run.ending = r.deliver(frame_35("D", r.peer_ctx(r.next_in()), kNos()));
    EXPECT_EQ(run.ending.from_app, 1) << log_form(run.ending);
    EXPECT_EQ(a.accessor, Version::Unknown);
    ASSERT_TRUE(a.reified.has_value());
    EXPECT_EQ(a.reified->application, Version::v50sp2);
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
