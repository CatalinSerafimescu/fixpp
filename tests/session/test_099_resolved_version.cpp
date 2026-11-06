// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_099_resolved_version.cpp
//
// 099-fixt-appl-version-validation, contracts/witnesses.md W-10's C++ half (tasks.md T019, T084):
// what a delivered view says of its application version, data-model E-5 and E-8. Each cell holds a
// literal from the frame it builds, and `dict::reify(...).version()` on the rows E-8 names. A
// `dict::reify` handle's `view()` answers as its source did, on the copy's dict-backed branch, the
// only one a Session's view reaches, and on its dict-free one through a view parsed dict-free whose
// byte the test writes through the detail key. W-10's clone cells are in
// tests/capi/capi_099_msg_version_test.cpp.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <fixpp/dict/reify.hpp>
#include <fixpp/wire/framer.hpp>
#include <fixpp/wire/parser.hpp>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <tuple>

#include "test_099_fixt_support.hpp"

namespace fixpp::session::test099 {
namespace {

using Resolved = dd::resolved_message_version;
using Version = dd::application_version;
using IndexView = fixpp::wire::MessageView<fixpp::wire::access_mode::Index>;

const dd::version_profile kFix44Profile{.session = dd::session_version::v44,
                                        .default_appl = Version::v44,
                                        .has_per_message_override = false,
                                        ._reserved = 0};
// Names no version of the FIXT frames here: `reify` with the byte set reads none of it.
const dd::version_profile kOtherProfile{.session = dd::session_version::v42,
                                        .default_appl = Version::v42,
                                        .has_per_message_override = false,
                                        ._reserved = 0};
const dd::version_profile kVt11At50{.session = dd::session_version::vt11,
                                    .default_appl = Version::v50sp2,
                                    .has_per_message_override = true,
                                    ._reserved = 0};
const dd::version_profile kNothingNegotiated{.session = dd::session_version::Unknown,
                                             .default_appl = Version::Unknown,
                                             .has_per_message_override = false,
                                             ._reserved = 0};

// What one callback's view said, through its accessor, through `reify` with `profile`, and through
// that handle's view.
struct ViewAnswers {
    int calls = 0;
    Version accessor = Version::Unknown;
    bool dict_backed = false;
    std::optional<Resolved> reified;
    std::optional<fixpp::core::error> reify_error;
    Version handle_accessor = Version::Unknown;
    bool handle_dict_backed = false;
    Version handle_of_handle_accessor = Version::Unknown;
};

ViewAnswers read_view(const IndexView& v, const dd::version_profile& profile) {
    ViewAnswers a;
    a.calls = 1;
    a.accessor = v.resolved_application_version();
    a.dict_backed = v.is_dict_backed();
    auto h = dd::reify(v, profile, std::pmr::new_delete_resource());
    if (!h) {
        a.reify_error = h.error();
        return a;
    }
    a.reified = h->version();
    a.handle_accessor = h->view().resolved_application_version();
    a.handle_dict_backed = h->view().is_dict_backed();
    if (auto again = dd::reify(h->view(), profile, std::pmr::new_delete_resource())) {
        a.handle_of_handle_accessor = again->view().resolved_application_version();
    }
    return a;
}

void watch(Rig& r, ViewAnswers& a, const dd::version_profile& profile) {
    const auto read = [&a, profile](const CountingApp::View& v) {
        const int calls = a.calls;
        a = read_view(v, profile);
        a.calls += calls;
    };
    r.app->on_app = read;
    r.app->on_admin = read;
}

// A FIXT application message with the byte set: the accessor, `reify` in all three bytes whatever
// the profile, and the handle's view.
void expect_application(const ViewAnswers& a, Version version) {
    EXPECT_EQ(a.calls, 1);
    EXPECT_EQ(a.accessor, version);
    EXPECT_TRUE(a.dict_backed) << "a Session's view is dict-backed";
    ASSERT_TRUE(a.reified.has_value()) << "reify returned no handle";
    EXPECT_EQ(a.reified->k, Resolved::kind::application);
    EXPECT_EQ(a.reified->session, dd::session_version::vt11);
    EXPECT_EQ(a.reified->application, version);
    EXPECT_EQ(a.handle_accessor, version) << "a reify handle's view answers as its source did";
    EXPECT_TRUE(a.handle_dict_backed);
    EXPECT_EQ(a.handle_of_handle_accessor, version);
}

// E-8's FIXT admin row, with a profile naming the frame's own version.
void expect_admin(const ViewAnswers& a) {
    EXPECT_EQ(a.calls, 1);
    EXPECT_EQ(a.accessor, Version::Unknown);
    ASSERT_TRUE(a.reified.has_value()) << "reify returned no handle";
    EXPECT_EQ(a.reified->k, Resolved::kind::session_admin);
    EXPECT_EQ(a.reified->session, dd::session_version::vt11);
    EXPECT_EQ(a.reified->application, Version::Unknown);
    EXPECT_EQ(a.handle_accessor, Version::Unknown);
}

std::string shape_param_name(Shape s) { return shape_name(s); }

// ── W-1's delivered cells on T, A and L, with S's own registry ──────────────────────────

struct DeliveredCase {
    const char* name{};
    const char* before_34{};
    Version version;
};

const DeliveredCase kDeliveredCases[] = {
    {.name = "Negotiated", .before_34 = "", .version = Version::v50sp2},
    {.name = "Explicit9", .before_34 = "1128=9\x01", .version = Version::v50sp2},
    {.name = "Explicit10", .before_34 = "1128=10\x01", .version = Version::vlatest},
};

class W10Delivered : public testing::TestWithParam<std::tuple<Shape, bool, DeliveredCase>> {};

TEST_P(W10Delivered, NosAnswersItsRoutesVersion) {
    const auto& [shape, validate, c] = GetParam();
    RigOptions o;
    o.validate = validate;
    CellRun run = enter_arm(shape_fixture(shape), Arm::Active, std::move(o));
    ASSERT_TRUE(run.setup_failure.empty()) << run.setup_failure;
    ViewAnswers a;
    watch(*run.rig, a, kOtherProfile);
    run.ending = run.rig->deliver(frame_35("D", run.rig->peer_ctx(2), kNos(), c.before_34));
    expect_ending(run, "- | stays | c | app=1 adm=0");
    expect_application(a, c.version);
}

INSTANTIATE_TEST_SUITE_P(W10, W10Delivered,
                         testing::Combine(testing::Values(Shape::T, Shape::A, Shape::L),
                                          testing::Bool(), testing::ValuesIn(kDeliveredCases)),
                         [](const auto& i) {
                             return shape_param_name(std::get<0>(i.param)) +
                                    (std::get<1>(i.param) ? "_on_" : "_off_") +
                                    std::get<2>(i.param).name;
                         });

class W10Fix44Registered : public testing::TestWithParam<Shape> {};

TEST_P(W10Fix44Registered, Nos1128_6_AnswersV44) {
    CellRun run = enter_arm(own_fixture(GetParam(), {fixt11(), fix50sp2(), latest(), fix44()}),
                            Arm::Active, {});
    ASSERT_TRUE(run.setup_failure.empty()) << run.setup_failure;
    ViewAnswers a;
    watch(*run.rig, a, kOtherProfile);
    run.ending = run.rig->deliver(frame_35("D", run.rig->peer_ctx(2), kNos(), "1128=6\x01"));
    expect_ending(run, "- | stays | c | app=1 adm=0");
    expect_application(a, Version::v44);
}

INSTANTIATE_TEST_SUITE_P(W10, W10Fix44Registered, testing::Values(Shape::T, Shape::A, Shape::L),
                         [](const testing::TestParamInfo<Shape>& i) {
                             return shape_param_name(i.param);
                         });

// ── W-1's delivered cells on a FIX44 S with FIXT11 in the engine ────────────────────────

Fixture fix44_s_fixture() {
    Fixture f;
    f.registry = make_registry({fixt11(), fix44(), fix50sp2(), latest()});
    f.session_dict = fix44();
    f.default_appl = Version::v44;
    f.peer_1137 = "6";
    return f;
}

const DeliveredCase kFix44SCases[] = {
    {.name = "Negotiated", .before_34 = "", .version = Version::v44},
    {.name = "Explicit6", .before_34 = "1128=6\x01", .version = Version::v44},
    {.name = "Explicit9", .before_34 = "1128=9\x01", .version = Version::v50sp2},
    {.name = "Explicit10", .before_34 = "1128=10\x01", .version = Version::vlatest},
};

class W10Fix44S : public testing::TestWithParam<DeliveredCase> {};

TEST_P(W10Fix44S, NosAnswersItsRoutesVersion) {
    CellRun run = enter_arm(fix44_s_fixture(), Arm::Active, {});
    ASSERT_TRUE(run.setup_failure.empty()) << run.setup_failure;
    ViewAnswers a;
    watch(*run.rig, a, kOtherProfile);
    run.ending =
        run.rig->deliver(frame_35("D", run.rig->peer_ctx(2), kNos(), GetParam().before_34));
    expect_ending(run, "- | stays | c | app=1 adm=0");
    expect_application(a, GetParam().version);
}

INSTANTIATE_TEST_SUITE_P(W10, W10Fix44S, testing::ValuesIn(kFix44SCases),
                         [](const testing::TestParamInfo<DeliveredCase>& i) {
                             return std::string{i.param.name};
                         });

// ── Tag 370 on W-1's FIX42 registry, and its one-way fallback, on T ──────────────────────────────

TEST(W10OtherRegistries, Tag370_1128_4_AnswersV42) {
    const std::string k370 = "370=20240101-00:00:00.000\x01";
    const std::string k1128 = "1128=4\x01";
    for (const std::string& before_34 : {k1128 + k370, k370 + k1128}) {
        CellRun run =
            enter_arm(own_fixture(Shape::T, {fixt11(), fix42(), fix50sp2()}), Arm::Active, {});
        ASSERT_TRUE(run.setup_failure.empty()) << run.setup_failure;
        ViewAnswers a;
        watch(*run.rig, a, kOtherProfile);
        run.ending = run.rig->deliver(frame_35("D", run.rig->peer_ctx(2), kNos(), before_34));
        expect_ending(run, "- | stays | c | app=1 adm=0");
        expect_application(a, Version::v42);
    }
}

// The registry's one-way fallback answers FIX50SP2 with FIX Latest's dictionary: the frame's
// version is still what 1137 negotiated.
TEST(W10OtherRegistries, OneWayFallback_AnswersTheNegotiatedVersion) {
    CellRun run = enter_arm(own_fixture(Shape::T, {fixt11(), latest()}), Arm::Active, {});
    ASSERT_TRUE(run.setup_failure.empty()) << run.setup_failure;
    ViewAnswers a;
    watch(*run.rig, a, kOtherProfile);
    run.ending = run.rig->deliver(frame_35("D", run.rig->peer_ctx(2), kNos() + "3115=1.5\x01"));
    expect_ending(run, "- | stays | c | app=1 adm=0");
    expect_application(a, Version::v50sp2);
}

// ── Admin: W-1's delivered admin cells ──────────────────────────────────────────────────

struct AdminCase {
    const char* name;
    const char* msg_type;
    const char* body;
};

const AdminCase kAdminCases[] = {
    {.name = "Heartbeat", .msg_type = "0", .body = ""},
    {.name = "HeartbeatWithBody", .msg_type = "0", .body = "112=x\x01"},
    {.name = "TestRequest", .msg_type = "1", .body = "112=x\x01"},
};

class W10Admin : public testing::TestWithParam<std::tuple<Shape, AdminCase>> {};

TEST_P(W10Admin, AccessorUnknown_ReifySaysSessionAdmin) {
    const auto& [shape, c] = GetParam();
    CellRun run = enter_arm(shape_fixture(shape), Arm::Active, {});
    ASSERT_TRUE(run.setup_failure.empty()) << run.setup_failure;
    ViewAnswers a;
    ViewAnswers nothing_negotiated;
    run.rig->app->on_admin = [&](const CountingApp::View& v) {
        a = read_view(v, kVt11At50);
        nothing_negotiated = read_view(v, kNothingNegotiated);
    };
    run.ending = run.rig->deliver(frame_35(c.msg_type, run.rig->peer_ctx(2), c.body));
    expect_admin(a);
    // E-8: given `{Unknown, Unknown}`, `reify` says `session` `Unknown` where the getter says vt11.
    ASSERT_TRUE(nothing_negotiated.reified.has_value());
    EXPECT_EQ(nothing_negotiated.reified->session, dd::session_version::Unknown);
}

INSTANTIATE_TEST_SUITE_P(W10, W10Admin,
                         testing::Combine(testing::Values(Shape::T, Shape::A, Shape::L),
                                          testing::ValuesIn(kAdminCases)),
                         [](const auto& i) {
                             return shape_param_name(std::get<0>(i.param)) + "_" +
                                    std::get<1>(i.param).name;
                         });

TEST(W10AdminFix44S, Heartbeat_AccessorUnknown) {
    CellRun run = enter_arm(fix44_s_fixture(), Arm::Active, {});
    ASSERT_TRUE(run.setup_failure.empty()) << run.setup_failure;
    ViewAnswers a;
    watch(*run.rig, a, kVt11At50);
    run.ending = run.rig->deliver(frame_35("0", run.rig->peer_ctx(2)));
    expect_admin(a);
}

TEST(W10AdminFix44S, TestRequest_AccessorUnknown) {
    CellRun run = enter_arm(fix44_s_fixture(), Arm::Active, {});
    ASSERT_TRUE(run.setup_failure.empty()) << run.setup_failure;
    ViewAnswers a;
    watch(*run.rig, a, kVt11At50);
    run.ending = run.rig->deliver(frame_35("1", run.rig->peer_ctx(2), "112=x\x01"));
    expect_admin(a);
}

// ── W-1's cell that keeps open()'s own route build ──────────────────────────────────────

class W10PrivateBuild : public testing::TestWithParam<Shape> {};

TEST_P(W10PrivateBuild, AdminAndNosAnswerAsTheSharedCell) {
    RigOptions o;
    o.private_build = true;
    CellRun run = enter_arm(shape_fixture(GetParam()), Arm::NotConnected, std::move(o));
    ASSERT_TRUE(run.setup_failure.empty()) << run.setup_failure;
    Rig& r = *run.rig;
    (void)r.deliver(logon_frame(r.peer_ctx(1), "1137=9\x01"));
    ASSERT_EQ(r.sess->state(), fsm_state::Active);

    ViewAnswers admin;
    watch(r, admin, kVt11At50);
    (void)r.deliver(frame_35("0", r.peer_ctx(2)));
    expect_admin(admin);

    ViewAnswers application;
    watch(r, application, kOtherProfile);
    run.ending = r.deliver(frame_35("D", r.peer_ctx(3), kNos() + "3115=1.5\x01", "1128=10\x01"));
    expect_application(application, Version::vlatest);
}

INSTANTIATE_TEST_SUITE_P(W10, W10PrivateBuild, testing::Values(Shape::T, Shape::A, Shape::L),
                         [](const testing::TestParamInfo<Shape>& i) {
                             return shape_param_name(i.param);
                         });

// ── Message Type Default: `MT-a`, on T, A and L ─────────────────────────────────────────

class W10MessageTypeDefault : public testing::TestWithParam<Shape> {};

// `reify` gets the session's own profile, which names 1137's version, so a `reify` that read it
// would label this frame with that version instead of the default's.
TEST_P(W10MessageTypeDefault, AccessorAndReifySayTheDefault) {
    CellRun run = enter_arm(shape_fixture(GetParam()), Arm::NotConnected, {});
    ASSERT_TRUE(run.setup_failure.empty()) << run.setup_failure;
    Rig& r = *run.rig;
    (void)r.deliver(logon_frame(r.peer_ctx(1),
                                "1137=9\x01"
                                "384=1\x01"
                                "372=D\x01"
                                "385=S\x01"
                                "1130=10\x01"
                                "1410=Y\x01"));
    ASSERT_EQ(r.sess->state(), fsm_state::Active);
    const dd::version_profile negotiated = r.sess->negotiated_version_profile();
    ASSERT_EQ(negotiated.default_appl, Version::v50sp2);
    ViewAnswers a;
    watch(r, a, negotiated);
    run.ending = r.deliver(frame_35("D", r.peer_ctx(2), kNos() + "3115=1.5\x01"));
    expect_ending(run, "- | stays | c | app=1 adm=0");
    expect_application(a, Version::vlatest);
}

INSTANTIATE_TEST_SUITE_P(W10, W10MessageTypeDefault, testing::Values(Shape::T, Shape::A, Shape::L),
                         [](const testing::TestParamInfo<Shape>& i) {
                             return shape_param_name(i.param);
                         });

// ── Unresolved: validation off, an unknown or an empty 1128 ─────────────────────────────

class W10Unresolved : public testing::TestWithParam<Shape> {};

TEST_P(W10Unresolved, UnknownValue_AccessorUnknown_ReifyRefuses) {
    RigOptions o;
    o.validate = false;
    CellRun run = enter_arm(shape_fixture(GetParam()), Arm::Active, std::move(o));
    ASSERT_TRUE(run.setup_failure.empty()) << run.setup_failure;
    ViewAnswers a;
    watch(*run.rig, a, kVt11At50);
    run.ending = run.rig->deliver(frame_35("D", run.rig->peer_ctx(2), kNos(), "1128=99\x01"));
    expect_ending(run, "- | stays | c | app=1 adm=0");
    EXPECT_EQ(a.calls, 1);
    EXPECT_EQ(a.accessor, Version::Unknown);
    ASSERT_TRUE(a.reify_error.has_value());
    EXPECT_EQ(*a.reify_error, fixpp::core::error::dict_unknown_appl_ver_id);
}

// E-5's split: `reify` passes the empty value to the resolver, whose rule means "use the default".
TEST_P(W10Unresolved, EmptyValue_AccessorUnknown_HandleAsItsSource) {
    RigOptions o;
    o.validate = false;
    CellRun run = enter_arm(shape_fixture(GetParam()), Arm::Active, std::move(o));
    ASSERT_TRUE(run.setup_failure.empty()) << run.setup_failure;
    ViewAnswers a;
    watch(*run.rig, a, kVt11At50);
    run.ending = run.rig->deliver(frame_35("D", run.rig->peer_ctx(2), kNos(), "1128=\x01"));
    expect_ending(run, "- | stays | c | app=1 adm=0");
    EXPECT_EQ(a.calls, 1);
    EXPECT_EQ(a.accessor, Version::Unknown);
    ASSERT_TRUE(a.reified.has_value());
    EXPECT_EQ(a.reified->application, Version::v50sp2);
    EXPECT_EQ(a.handle_accessor, Version::Unknown);
}

INSTANTIATE_TEST_SUITE_P(W10, W10Unresolved, testing::Values(Shape::T, Shape::A, Shape::L),
                         [](const testing::TestParamInfo<Shape>& i) {
                             return shape_param_name(i.param);
                         });

// ── A `toApp` view: no session wrote it ─────────────────────────────────────────────────

struct ToAppCase {
    const char* name;
    const char* header;
};

const ToAppCase kToAppCases[] = {
    {.name = "No1128", .header = ""},
    {.name = "Header1128_10", .header = "1128=10\x01"},
};

class W10ToApp : public testing::TestWithParam<std::tuple<Shape, ToAppCase>> {};

TEST_P(W10ToApp, AccessorUnknown) {
    const auto& [shape, c] = GetParam();
    CellRun run = enter_arm(shape_fixture(shape), Arm::Active, {});
    ASSERT_TRUE(run.setup_failure.empty()) << run.setup_failure;
    int calls = 0;
    Version accessor = Version::v44;  // a value the session would never write here
    run.rig->app->on_to_app = [&](const CountingApp::View& v) {
        ++calls;
        accessor = v.resolved_application_version();
    };
    const std::string err = run.rig->send(std::string("35=D\x01") + c.header + kNos());
    EXPECT_EQ(err, "");
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(accessor, Version::Unknown);
    EXPECT_EQ(run.rig->out.size(), 1U);
}

INSTANTIATE_TEST_SUITE_P(W10, W10ToApp,
                         testing::Combine(testing::Values(Shape::T, Shape::A, Shape::L),
                                          testing::ValuesIn(kToAppCases)),
                         [](const auto& i) {
                             return shape_param_name(std::get<0>(i.param)) + "_" +
                                    std::get<1>(i.param).name;
                         });

// ── The profile: `reify` with the byte set takes `session` from no profile ──────────────

class W10Profile : public testing::TestWithParam<Shape> {};

// A non-validating initiator's first connection, a reply with no 1137, a NOS with `1128=9`.
TEST_P(W10Profile, NothingNegotiated_ReifySaysVt11) {
    RigOptions o;
    o.validate = false;
    o.role = session_role::initiator;
    CellRun run = enter_arm(shape_fixture(GetParam()), Arm::LogonSent, std::move(o));
    ASSERT_TRUE(run.setup_failure.empty()) << run.setup_failure;
    Rig& r = *run.rig;
    (void)r.deliver(logon_frame(r.peer_ctx(1), ""));
    ASSERT_EQ(r.sess->state(), fsm_state::Active);
    ViewAnswers a;
    watch(r, a, kNothingNegotiated);
    run.ending = r.deliver(frame_35("D", r.peer_ctx(2), kNos(), "1128=9\x01"));
    expect_ending(run, "- | stays | c | app=1 adm=0");
    expect_application(a, Version::v50sp2);
}

INSTANTIATE_TEST_SUITE_P(W10, W10Profile, testing::Values(Shape::T, Shape::A, Shape::L),
                         [](const testing::TestParamInfo<Shape>& i) {
                             return shape_param_name(i.param);
                         });

// ── The copy's dict-free branch: a view parsed dict-free, its byte written by the test ──

// Holds a dict-free parse of a NOS whose 1128 says 9, and hands its view to `use`.
template <class Use>
void with_dict_free_view(const Use& use) {
    const std::string frame =
        frame_35("D", Ctx{.snd = "TW", .tgt = "ISLD", .seq = 2}, kNos(), "1128=9\x01");
    std::pmr::monotonic_buffer_resource carry_arena;
    std::pmr::monotonic_buffer_resource parse_arena;
    fixpp::wire::pmr_carry_buffer carry{0, &carry_arena};
    fixpp::wire::Framer framer{};
    std::array<fixpp::wire::frame_view, 1> out{};
    auto framed = framer.feed(std::as_bytes(std::span{frame.data(), frame.size()}), carry,
                              std::span<fixpp::wire::frame_view>{out});
    ASSERT_TRUE(framed.has_value());
    ASSERT_FALSE(framed->empty());
    fixpp::wire::Parser<fixpp::wire::access_mode::Index> parser{};
    auto view = parser.parse(out[0], &parse_arena);
    ASSERT_TRUE(view.has_value());
    ASSERT_FALSE(view->is_dict_backed());
    use(*view);
}

TEST(W10DictFree, ReifyHandleAnswersAsItsSource) {
    with_dict_free_view([](IndexView& view) {
        fixpp::wire::detail::message_view_resolved_access::set(view, Version::vlatest);
        const ViewAnswers a = read_view(view, kOtherProfile);
        ASSERT_TRUE(a.reified.has_value()) << "reify returned no handle";
        EXPECT_EQ(a.reified->k, Resolved::kind::application);
        EXPECT_EQ(a.reified->session, dd::session_version::vt11);
        EXPECT_EQ(a.reified->application, Version::vlatest);
        EXPECT_FALSE(a.handle_dict_backed) << "the copy's dict-free branch";
        EXPECT_EQ(a.handle_accessor, Version::vlatest);
        EXPECT_EQ(a.handle_of_handle_accessor, Version::vlatest);
    });
}

TEST(W10DictFree, ByteUnknown_ReifyReads1128_HandleAnswersUnknown) {
    with_dict_free_view([](IndexView& view) {
        const ViewAnswers a = read_view(view, kOtherProfile);
        ASSERT_TRUE(a.reified.has_value()) << "reify returned no handle";
        EXPECT_EQ(a.reified->session, dd::session_version::v42)
            << "the profile's, the byte being Unknown";
        EXPECT_EQ(a.reified->application, Version::v50sp2) << "the frame's 1128";
        EXPECT_FALSE(a.handle_dict_backed);
        EXPECT_EQ(a.handle_accessor, Version::Unknown);
    });
}

// ── Non-FIXT: E-8's two non-FIXT rows on a FIX.4.4 session ──────────────────────────────

struct Fix44Session {
    CellRun run;
    ViewAnswers seen;

    Fix44Session() {
        RigOptions opts;
        opts.validate = false;
        opts.tweak = [](SessionConfig& cfg) {
            cfg.begin_string = "FIX.4.4";
            cfg.default_appl_ver_id.reset();
        };
        Fixture f{.registry = make_registry({fix44()}), .session_dict = fix44()};
        run = enter_arm(std::move(f), Arm::NotConnected, std::move(opts));
        if (!run.setup_failure.empty()) {
            return;
        }
        watch(*run.rig, seen, kFix44Profile);
        const auto ctx = run.rig->peer_ctx(1);
        (void)run.rig->deliver(wire("FIX.4.4", "35=A\x01" + hdr(ctx) + kLogonBody()));
        seen = ViewAnswers{};
    }
};

TEST(W10NonFixtReify, ApplicationRow) {
    Fix44Session s;
    ASSERT_TRUE(s.run.setup_failure.empty()) << s.run.setup_failure;
    ASSERT_EQ(s.run.rig->sess->state(), fsm_state::Active);
    (void)s.run.rig->deliver(wire("FIX.4.4", "35=D\x01" + hdr(s.run.rig->peer_ctx(2)) + kNos()));
    EXPECT_EQ(s.seen.calls, 1);
    EXPECT_EQ(s.seen.accessor, Version::Unknown);
    ASSERT_TRUE(s.seen.reified.has_value()) << "fromApp did not run, or reify returned no handle";
    EXPECT_EQ(s.seen.reified->k, Resolved::kind::application);
    EXPECT_EQ(s.seen.reified->session, dd::session_version::v44);
    EXPECT_EQ(s.seen.reified->application, Version::v44);
    EXPECT_EQ(s.seen.handle_accessor, Version::Unknown);
}

TEST(W10NonFixtReify, AdminRow) {
    Fix44Session s;
    ASSERT_TRUE(s.run.setup_failure.empty()) << s.run.setup_failure;
    ASSERT_EQ(s.run.rig->sess->state(), fsm_state::Active);
    (void)s.run.rig->deliver(wire("FIX.4.4", "35=0\x01" + hdr(s.run.rig->peer_ctx(2))));
    EXPECT_EQ(s.seen.calls, 1);
    EXPECT_EQ(s.seen.accessor, Version::Unknown);
    ASSERT_TRUE(s.seen.reified.has_value()) << "fromAdmin did not run, or reify returned no handle";
    EXPECT_EQ(s.seen.reified->k, Resolved::kind::session_admin);
    EXPECT_EQ(s.seen.reified->session, dd::session_version::v44);
    EXPECT_EQ(s.seen.reified->application, Version::Unknown);
    EXPECT_EQ(s.seen.handle_accessor, Version::Unknown);
}

}  // namespace
}  // namespace fixpp::session::test099
