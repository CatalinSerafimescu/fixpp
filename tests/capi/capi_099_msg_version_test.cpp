// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/capi/capi_099_msg_version_test.cpp
//
// 099-fixt-appl-version-validation, contracts/witnesses.md W-10's clone cells (tasks.md T085) and C
// half (tasks.md T093). `fixpp_msg_clone`, and a clone of a clone, answer as their source did
// (data-model E-5). Each handle is read through the view it holds, in capi_internal.hpp. The
// clone's dict-backed branch is the one a Session's view reaches, so those cells clone inside the
// callback, as the C engine's receive path wraps a view. Its dict-free branch takes a view parsed
// dict-free whose byte the test writes through the detail key. The C half reads every row of
// data-model E-8's table through `fixpp_msg_version`, each byte a literal from the frame the cell
// builds, on the handle, a clone, a clone of a clone and a `dict::reify` view, against
// `dict::reify(...).version()` on the rows E-8 compares.
//
// A real Session on an io_context the test pumps: no engine and no worker thread, which is
// capi_pure_tests' rule.

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
#include <vector>

#include "capi_internal.hpp"
#include "fix/c_api/dict.h"
#include "fix/c_api/error.h"
#include "fix/c_api/message.h"
#include "session/test_099_fixt_support.hpp"
#include "support/frame_view_factory.hpp"

namespace fixpp::session::test099 {
namespace {

using Version = dd::application_version;
using IndexView = fixpp::wire::MessageView<fixpp::wire::access_mode::Index>;

const IndexView* view_of(const fixpp_msg_t* handle) {
    return reinterpret_cast<const fixpp_msg*>(handle)->view;
}

// What a clone, and a clone of that clone, said.
struct CloneAnswers {
    int calls = 0;
    fixpp_error_t clone_rc = FIXPP_ERR_UNKNOWN;
    fixpp_error_t clone_of_clone_rc = FIXPP_ERR_UNKNOWN;
    Version clone = Version::Unknown;
    Version clone_of_clone = Version::Unknown;
    bool clone_dict_backed = false;
    bool clone_of_clone_dict_backed = false;
};

CloneAnswers clone_twice(const IndexView& v) {
    CloneAnswers a;
    a.calls = 1;
    fixpp_msg inbound{};
    inbound.view = &v;
    fixpp_msg_t* first = nullptr;
    a.clone_rc = fixpp_msg_clone(reinterpret_cast<const fixpp_msg_t*>(&inbound), &first);
    if (a.clone_rc != FIXPP_ERR_OK) {
        return a;
    }
    a.clone = view_of(first)->resolved_application_version();
    a.clone_dict_backed = view_of(first)->is_dict_backed();
    fixpp_msg_t* second = nullptr;
    a.clone_of_clone_rc = fixpp_msg_clone(first, &second);
    if (a.clone_of_clone_rc == FIXPP_ERR_OK) {
        a.clone_of_clone = view_of(second)->resolved_application_version();
        a.clone_of_clone_dict_backed = view_of(second)->is_dict_backed();
        (void)fixpp_msg_destroy(second);
    }
    (void)fixpp_msg_destroy(first);
    return a;
}

void watch(Rig& r, CloneAnswers& a) {
    const auto read = [&a](const CountingApp::View& v) {
        const int calls = a.calls;
        a = clone_twice(v);
        a.calls += calls;
    };
    r.app->on_app = read;
    r.app->on_admin = read;
}

void expect_clones(const CloneAnswers& a, Version version, bool dict_backed) {
    EXPECT_EQ(a.calls, 1);
    ASSERT_EQ(a.clone_rc, FIXPP_ERR_OK);
    ASSERT_EQ(a.clone_of_clone_rc, FIXPP_ERR_OK);
    EXPECT_EQ(a.clone, version);
    EXPECT_EQ(a.clone_of_clone, version);
    EXPECT_EQ(a.clone_dict_backed, dict_backed) << "which branch of the copy this cell reaches";
    EXPECT_EQ(a.clone_of_clone_dict_backed, dict_backed);
}

std::string shape_param_name(Shape s) { return shape_name(s); }

// ── The dict-backed branch: a view a Session dispatched ─────────────────────────────────

struct CloneCase {
    const char* name;
    const char* before_34;
    Version version;
};

const CloneCase kCloneCases[] = {
    {"Negotiated", "", Version::v50sp2},
    {"Explicit9", "1128=9\x01", Version::v50sp2},
    {"Explicit10", "1128=10\x01", Version::vlatest},
};

class W10Clone : public testing::TestWithParam<std::tuple<Shape, CloneCase>> {};

TEST_P(W10Clone, DictBacked_AnswersAsItsSource) {
    const auto& [shape, c] = GetParam();
    CellRun run = enter_arm(shape_fixture(shape), Arm::Active, {});
    ASSERT_TRUE(run.setup_failure.empty()) << run.setup_failure;
    CloneAnswers a;
    watch(*run.rig, a);
    run.ending = run.rig->deliver(frame_35("D", run.rig->peer_ctx(2), kNos, c.before_34));
    expect_ending(run, "- | stays | c | app=1 adm=0");
    expect_clones(a, c.version, /*dict_backed=*/true);
}

INSTANTIATE_TEST_SUITE_P(W10, W10Clone,
                         testing::Combine(testing::Values(Shape::T, Shape::A, Shape::L),
                                          testing::ValuesIn(kCloneCases)),
                         [](const auto& i) {
                             return shape_param_name(std::get<0>(i.param)) + "_" +
                                    std::get<1>(i.param).name;
                         });

class W10CloneOfShape : public testing::TestWithParam<Shape> {};

// `MT-a`: a view whose version came from no field of its own frame.
TEST_P(W10CloneOfShape, MessageTypeDefault_DictBacked_AnswersTheDefault) {
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
    CloneAnswers a;
    watch(r, a);
    run.ending = r.deliver(frame_35("D", r.peer_ctx(2), kNos + "3115=1.5\x01"));
    expect_ending(run, "- | stays | c | app=1 adm=0");
    expect_clones(a, Version::vlatest, /*dict_backed=*/true);
}

TEST_P(W10CloneOfShape, Admin_DictBacked_AnswersUnknown) {
    CellRun run = enter_arm(shape_fixture(GetParam()), Arm::Active, {});
    ASSERT_TRUE(run.setup_failure.empty()) << run.setup_failure;
    CloneAnswers a;
    watch(*run.rig, a);
    run.ending = run.rig->deliver(frame_35("0", run.rig->peer_ctx(2)));
    expect_ending(run, "- | stays | c | app=0 adm=1");
    expect_clones(a, Version::Unknown, /*dict_backed=*/true);
}

INSTANTIATE_TEST_SUITE_P(W10, W10CloneOfShape, testing::Values(Shape::T, Shape::A, Shape::L),
                         [](const testing::TestParamInfo<Shape>& i) {
                             return shape_param_name(i.param);
                         });

// ── The dict-free branch: a view parsed dict-free, its byte written by the test ─────────

template <class Use>
void with_dict_free_view(Use&& use) {
    const std::string frame = frame_35("D", Ctx{"TW", "ISLD", 2}, kNos, "1128=9\x01");
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

TEST(W10CloneDictFree, AnswersAsItsSource) {
    with_dict_free_view([](IndexView& view) {
        fixpp::wire::detail::message_view_resolved_access::set(view, Version::vlatest);
        expect_clones(clone_twice(view), Version::vlatest, /*dict_backed=*/false);
    });
}

TEST(W10CloneDictFree, ByteUnknown_AnswersUnknown) {
    with_dict_free_view([](IndexView& view) {
        expect_clones(clone_twice(view), Version::Unknown, /*dict_backed=*/false);
    });
}

// ── The C half: data-model E-8's table through fixpp_msg_version ─────────────────────────

using Resolved = dd::resolved_message_version;

const dd::version_profile kOtherProfile{dd::session_version::v42, Version::v42, false, 0};
const dd::version_profile kVt11Profile{dd::session_version::vt11, Version::v50sp2, true, 0};
const dd::version_profile kFix44Profile{dd::session_version::v44, Version::v44, false, 0};
const dd::version_profile kNothingNegotiated{dd::session_version::Unknown, Version::Unknown, false,
                                             0};

// The row's three bytes, spelled at the cell.
struct Row {
    int kind;
    int session;
    int application;
};

constexpr int kAdmin = FIXPP_MSG_KIND_SESSION_ADMIN;
constexpr int kApp = FIXPP_MSG_KIND_APPLICATION;
constexpr int kVt11 = FIXPP_SESSION_VER_FIXT11;
constexpr int kNone = FIXPP_APPL_VER_UNKNOWN;

struct GetterRead {
    fixpp_error_t rc = FIXPP_ERR_UNKNOWN;
    Row row{-1, -1, -1};
    int reserved = -1;
};

GetterRead read_getter(const fixpp_msg_t* m) {
    // Seeded non-zero, so that a zero can only be the call's.
    fixpp_resolved_msg_version_t v{0xAA, 0xAA, 0xAA, 0xAA};
    GetterRead r;
    r.rc = fixpp_msg_version(m, &v);
    r.row = Row{v.kind, v.session, v.application};
    r.reserved = v._reserved;
    return r;
}

// What one view's getter said on the handle and on each copy of it, and `reify`'s answer.
struct GetterAnswers {
    int calls = 0;
    GetterRead handle;
    GetterRead clone;
    GetterRead clone_of_clone;
    GetterRead reify_view;
    std::optional<Resolved> reified;
};

GetterAnswers read_all(const IndexView& v, const dd::version_profile& profile) {
    GetterAnswers a;
    a.calls = 1;
    fixpp_msg inbound{};
    inbound.view = &v;
    const auto* handle = reinterpret_cast<const fixpp_msg_t*>(&inbound);
    a.handle = read_getter(handle);
    fixpp_msg_t* first = nullptr;
    if (fixpp_msg_clone(handle, &first) == FIXPP_ERR_OK) {
        a.clone = read_getter(first);
        fixpp_msg_t* second = nullptr;
        if (fixpp_msg_clone(first, &second) == FIXPP_ERR_OK) {
            a.clone_of_clone = read_getter(second);
            (void)fixpp_msg_destroy(second);
        }
        (void)fixpp_msg_destroy(first);
    }
    auto reified = dd::reify(v, profile, std::pmr::new_delete_resource());
    if (reified) {
        a.reified = reified->version();
        fixpp_msg reify_handle{};
        reify_handle.view = &reified->view();
        a.reify_view = read_getter(reinterpret_cast<const fixpp_msg_t*>(&reify_handle));
    }
    return a;
}

void watch_getter(Rig& r, GetterAnswers& a, const dd::version_profile& profile) {
    const auto read = [&a, profile](const CountingApp::View& v) {
        const int calls = a.calls;
        a = read_all(v, profile);
        a.calls += calls;
    };
    r.app->on_app = read;
    r.app->on_admin = read;
}

void expect_row(const GetterRead& got, const Row& want, const char* which) {
    EXPECT_EQ(got.rc, FIXPP_ERR_OK) << which;
    EXPECT_EQ(got.row.kind, want.kind) << which;
    EXPECT_EQ(got.row.session, want.session) << which;
    EXPECT_EQ(got.row.application, want.application) << which;
    EXPECT_EQ(got.reserved, 0) << which;
}

// The handle and its copies. A reify view is read where `reify` returned one; the rows E-8 does not
// compare with `reify` say so at the cell.
void expect_answers(const GetterAnswers& a, const Row& want) {
    EXPECT_EQ(a.calls, 1);
    expect_row(a.handle, want, "the handle");
    expect_row(a.clone, want, "a clone");
    expect_row(a.clone_of_clone, want, "a clone of a clone");
}

void expect_reify_equals(const GetterAnswers& a, const Row& want) {
    ASSERT_TRUE(a.reified.has_value()) << "reify returned no handle";
    EXPECT_EQ(static_cast<int>(a.reified->k), want.kind);
    EXPECT_EQ(static_cast<int>(a.reified->session), want.session);
    EXPECT_EQ(static_cast<int>(a.reified->application), want.application);
    expect_row(a.reify_view, want, "a reify view");
}

struct GetterCase {
    const char* name;
    const char* before_34;
    int application;
};

const GetterCase kGetterCases[] = {
    {"Negotiated", "", FIXPP_APPL_VER_FIX50SP2},
    {"Explicit9", "1128=9\x01", FIXPP_APPL_VER_FIX50SP2},
    {"Explicit10", "1128=10\x01", FIXPP_APPL_VER_FIXLATEST},
};

class W10CApplication : public testing::TestWithParam<std::tuple<Shape, bool, GetterCase>> {};

// FIXT application message, resolved. `reify` reads no profile with the byte set, so kOtherProfile
// names another version than the frame's.
TEST_P(W10CApplication, ThreeBytesAndReifyEqual) {
    const auto& [shape, validate, c] = GetParam();
    RigOptions o;
    o.validate = validate;
    CellRun run = enter_arm(shape_fixture(shape), Arm::Active, std::move(o));
    ASSERT_TRUE(run.setup_failure.empty()) << run.setup_failure;
    GetterAnswers a;
    watch_getter(*run.rig, a, kOtherProfile);
    run.ending = run.rig->deliver(frame_35("D", run.rig->peer_ctx(2), kNos, c.before_34));
    expect_ending(run, "- | stays | c | app=1 adm=0");
    const Row want{kApp, kVt11, c.application};
    expect_answers(a, want);
    expect_reify_equals(a, want);
}

INSTANTIATE_TEST_SUITE_P(W10, W10CApplication,
                         testing::Combine(testing::Values(Shape::T, Shape::A, Shape::L),
                                          testing::Bool(), testing::ValuesIn(kGetterCases)),
                         [](const auto& i) {
                             return shape_param_name(std::get<0>(i.param)) +
                                    (std::get<1>(i.param) ? "_on_" : "_off_") +
                                    std::get<2>(i.param).name;
                         });

class W10COfShape : public testing::TestWithParam<Shape> {};

// E-10's `MT-a`: a version that came from no field of its own frame.
TEST_P(W10COfShape, MessageTypeDefault_AnswersTheDefault) {
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
    GetterAnswers a;
    watch_getter(r, a, kOtherProfile);
    run.ending = r.deliver(frame_35("D", r.peer_ctx(2), kNos + "3115=1.5\x01"));
    expect_ending(run, "- | stays | c | app=1 adm=0");
    const Row want{kApp, kVt11, FIXPP_APPL_VER_FIXLATEST};
    expect_answers(a, want);
    expect_reify_equals(a, want);
}

// FIXT admin message, on the frames an Active session delivers. With a profile naming the frame's
// own version, `reify` agrees (E-8).
TEST_P(W10COfShape, Admin_Heartbeat) {
    CellRun run = enter_arm(shape_fixture(GetParam()), Arm::Active, {});
    ASSERT_TRUE(run.setup_failure.empty()) << run.setup_failure;
    GetterAnswers a;
    watch_getter(*run.rig, a, kVt11Profile);
    run.ending = run.rig->deliver(frame_35("0", run.rig->peer_ctx(2)));
    expect_ending(run, "- | stays | c | app=0 adm=1");
    const Row want{kAdmin, kVt11, kNone};
    expect_answers(a, want);
    expect_reify_equals(a, want);
}

TEST_P(W10COfShape, Admin_TestRequest) {
    CellRun run = enter_arm(shape_fixture(GetParam()), Arm::Active, {});
    ASSERT_TRUE(run.setup_failure.empty()) << run.setup_failure;
    GetterAnswers a;
    watch_getter(*run.rig, a, kVt11Profile);
    (void)run.rig->deliver(frame_35("1", run.rig->peer_ctx(2), "112=T\x01"));
    const Row want{kAdmin, kVt11, kNone};
    expect_answers(a, want);
    expect_reify_equals(a, want);
}

// E-8's named difference: given what `negotiated_version_profile()` returns before a version is
// negotiated, `reify` says `session` Unknown on an admin message, where the getter says vt11.
TEST_P(W10COfShape, Admin_NothingNegotiated_ReifyDiffersInSessionAlone) {
    CellRun run = enter_arm(shape_fixture(GetParam()), Arm::Active, {});
    ASSERT_TRUE(run.setup_failure.empty()) << run.setup_failure;
    GetterAnswers a;
    watch_getter(*run.rig, a, kNothingNegotiated);
    run.ending = run.rig->deliver(frame_35("0", run.rig->peer_ctx(2)));
    expect_ending(run, "- | stays | c | app=0 adm=1");
    expect_answers(a, Row{kAdmin, kVt11, kNone});
    ASSERT_TRUE(a.reified.has_value()) << "reify returned no handle";
    EXPECT_EQ(a.reified->k, Resolved::kind::session_admin);
    EXPECT_EQ(a.reified->session, dd::session_version::Unknown);
    EXPECT_EQ(a.reified->application, Version::Unknown);
}

// FIXT application message, unresolved, each spelling E-8's row names. No comparison with `reify`:
// that row is E-5's splits.
TEST_P(W10COfShape, Unresolved_UnknownValue_ValidationOff) {
    RigOptions o;
    o.validate = false;
    CellRun run = enter_arm(shape_fixture(GetParam()), Arm::Active, std::move(o));
    ASSERT_TRUE(run.setup_failure.empty()) << run.setup_failure;
    GetterAnswers a;
    watch_getter(*run.rig, a, kVt11Profile);
    run.ending = run.rig->deliver(frame_35("D", run.rig->peer_ctx(2), kNos, "1128=99\x01"));
    expect_ending(run, "- | stays | c | app=1 adm=0");
    expect_answers(a, Row{kApp, kVt11, kNone});
}

TEST_P(W10COfShape, Unresolved_EmptyValue_ValidationOff) {
    RigOptions o;
    o.validate = false;
    CellRun run = enter_arm(shape_fixture(GetParam()), Arm::Active, std::move(o));
    ASSERT_TRUE(run.setup_failure.empty()) << run.setup_failure;
    GetterAnswers a;
    watch_getter(*run.rig, a, kVt11Profile);
    run.ending = run.rig->deliver(frame_35("D", run.rig->peer_ctx(2), kNos, "1128=\x01"));
    expect_ending(run, "- | stays | c | app=1 adm=0");
    expect_answers(a, Row{kApp, kVt11, kNone});
}

// A non-validating initiator's reply with no 1137 leaves nothing resolved for a frame with no 1128.
TEST_P(W10COfShape, Unresolved_ReplyWithNo1137_NothingResolved) {
    RigOptions o;
    o.validate = false;
    o.role = session_role::initiator;
    CellRun run = enter_arm(shape_fixture(GetParam()), Arm::LogonSent, std::move(o));
    ASSERT_TRUE(run.setup_failure.empty()) << run.setup_failure;
    Rig& r = *run.rig;
    (void)r.deliver(logon_frame(r.peer_ctx(1), ""));
    ASSERT_EQ(r.sess->state(), fsm_state::Active);
    GetterAnswers a;
    watch_getter(r, a, kVt11Profile);
    run.ending = r.deliver(frame_35("D", r.peer_ctx(2), kNos));
    expect_ending(run, "- | stays | c | app=1 adm=0");
    expect_answers(a, Row{kApp, kVt11, kNone});
}

// A `toApp` view on FIXT: kind by 35, and no session wrote the byte, whatever 1128 the payload
// holds.
TEST_P(W10COfShape, ToAppView_NoSessionWroteIt) {
    for (const char* header : {"", "1128=10\x01"}) {
        CellRun run = enter_arm(shape_fixture(GetParam()), Arm::Active, {});
        ASSERT_TRUE(run.setup_failure.empty()) << run.setup_failure;
        int calls = 0;
        GetterRead got;
        run.rig->app->on_to_app = [&](const CountingApp::View& v) {
            ++calls;
            fixpp_msg outbound_view{};
            outbound_view.view = &v;
            got = read_getter(reinterpret_cast<const fixpp_msg_t*>(&outbound_view));
        };
        EXPECT_EQ(run.rig->send(std::string("35=D\x01") + header + kNos), "");
        EXPECT_EQ(calls, 1);
        expect_row(got, Row{kApp, kVt11, kNone}, header);
    }
}

INSTANTIATE_TEST_SUITE_P(W10, W10COfShape, testing::Values(Shape::T, Shape::A, Shape::L),
                         [](const testing::TestParamInfo<Shape>& i) {
                             return shape_param_name(i.param);
                         });

// The FIXT application row's "with a dictionary or not", on a view parsed dict-free.
TEST(W10CDictFree, ByteSet_AnswersTheByte) {
    with_dict_free_view([](IndexView& view) {
        fixpp::wire::detail::message_view_resolved_access::set(view, Version::vlatest);
        fixpp_msg inbound{};
        inbound.view = &view;
        expect_row(read_getter(reinterpret_cast<const fixpp_msg_t*>(&inbound)),
                   Row{kApp, kVt11, FIXPP_APPL_VER_FIXLATEST}, "dict-free, byte set");
    });
}

TEST(W10CDictFree, ByteUnknown_AnswersUnknown_Whatever1128Says) {
    with_dict_free_view([](IndexView& view) {
        fixpp_msg inbound{};
        inbound.view = &view;
        expect_row(read_getter(reinterpret_cast<const fixpp_msg_t*>(&inbound)),
                   Row{kApp, kVt11, kNone}, "dict-free, byte unset");
    });
}

// ── The non-FIXT rows, on a FIX.4.4 session ─────────────────────────────────────────────

struct Fix44Session {
    CellRun run;
    GetterAnswers seen;

    Fix44Session() {
        RigOptions opts;
        opts.validate = false;
        opts.tweak = [](SessionConfig& cfg) {
            cfg.begin_string = "FIX.4.4";
            cfg.default_appl_ver_id.reset();
        };
        Fixture f{make_registry({fix44()}), fix44()};
        run = enter_arm(std::move(f), Arm::NotConnected, std::move(opts));
        if (!run.setup_failure.empty()) {
            return;
        }
        watch_getter(*run.rig, seen, kFix44Profile);
        const auto ctx = run.rig->peer_ctx(1);
        (void)run.rig->deliver(wire("FIX.4.4", "35=A\x01" + hdr(ctx) + kLogonBody));
        seen = GetterAnswers{};
    }
};

constexpr int kV44 = FIXPP_SESSION_VER_FIX44;

TEST(W10CNonFixt, ApplicationRow) {
    Fix44Session s;
    ASSERT_TRUE(s.run.setup_failure.empty()) << s.run.setup_failure;
    ASSERT_EQ(s.run.rig->sess->state(), fsm_state::Active);
    (void)s.run.rig->deliver(wire("FIX.4.4", "35=D\x01" + hdr(s.run.rig->peer_ctx(2)) + kNos));
    const Row want{kApp, kV44, FIXPP_APPL_VER_FIX44};
    expect_answers(s.seen, want);
    expect_reify_equals(s.seen, want);
}

TEST(W10CNonFixt, AdminRow) {
    Fix44Session s;
    ASSERT_TRUE(s.run.setup_failure.empty()) << s.run.setup_failure;
    ASSERT_EQ(s.run.rig->sess->state(), fsm_state::Active);
    (void)s.run.rig->deliver(wire("FIX.4.4", "35=0\x01" + hdr(s.run.rig->peer_ctx(2))));
    const Row want{kAdmin, kV44, kNone};
    expect_answers(s.seen, want);
    expect_reify_equals(s.seen, want);
}

// ── Views the test builds: BeginString and tag 8 ────────────────────────────────────────

std::vector<std::byte> raw_frame(const std::string& begin_string, const std::string& body) {
    const std::string text = "8=" + begin_string + "\x01" + "9=" + std::to_string(body.size()) +
                             "\x01" + body + "10=000\x01";
    const auto* bytes = reinterpret_cast<const std::byte*>(text.data());
    return {bytes, bytes + text.size()};
}

GetterRead read_built(const std::vector<std::byte>& buf) {
    GetterRead got;
    auto fv = fixpp::wire::test::make_frame_view(buf);
    if (!fv) {
        ADD_FAILURE() << "no frame view";
        return got;
    }
    std::pmr::monotonic_buffer_resource arena;
    const IndexView view{*fv, &arena};
    fixpp_msg inbound{};
    inbound.view = &view;
    return read_getter(reinterpret_cast<const fixpp_msg_t*>(&inbound));
}

struct BeginStringCase {
    const char* name;
    const char* begin_string;
    int session;
    int application;
};

const BeginStringCase kSupportedBeginStrings[] = {
    {"Fix40", "FIX.4.0", FIXPP_SESSION_VER_FIX40, FIXPP_APPL_VER_FIX40},
    {"Fix41", "FIX.4.1", FIXPP_SESSION_VER_FIX41, FIXPP_APPL_VER_FIX41},
    {"Fix42", "FIX.4.2", FIXPP_SESSION_VER_FIX42, FIXPP_APPL_VER_FIX42},
    {"Fix43", "FIX.4.3", FIXPP_SESSION_VER_FIX43, FIXPP_APPL_VER_FIX43},
    {"Fix44", "FIX.4.4", FIXPP_SESSION_VER_FIX44, FIXPP_APPL_VER_FIX44},
};

const BeginStringCase kOtherNamedBeginStrings[] = {
    {"Fix50", "FIX.5.0", FIXPP_SESSION_VER_FIX50, FIXPP_APPL_VER_FIX50},
    {"Fix50Sp1", "FIX.5.0SP1", FIXPP_SESSION_VER_FIX50SP1, FIXPP_APPL_VER_FIX50SP1},
    {"Fix50Sp2", "FIX.5.0SP2", FIXPP_SESSION_VER_FIX50SP2, FIXPP_APPL_VER_FIX50SP2},
    {"FixLatest", "FIX.Latest", FIXPP_SESSION_VER_FIXLATEST, FIXPP_APPL_VER_FIXLATEST},
};

class W10CBeginString : public testing::TestWithParam<BeginStringCase> {};

TEST_P(W10CBeginString, ApplicationAndAdminRows) {
    const BeginStringCase& c = GetParam();
    expect_row(read_built(raw_frame(c.begin_string, "35=D\x01")),
               Row{kApp, c.session, c.application}, "an application message");
    expect_row(read_built(raw_frame(c.begin_string, "35=0\x01")), Row{kAdmin, c.session, kNone},
               "an admin message");
}

INSTANTIATE_TEST_SUITE_P(Supported, W10CBeginString, testing::ValuesIn(kSupportedBeginStrings),
                         [](const testing::TestParamInfo<BeginStringCase>& i) {
                             return std::string{i.param.name};
                         });
INSTANTIATE_TEST_SUITE_P(OtherNamed, W10CBeginString, testing::ValuesIn(kOtherNamedBeginStrings),
                         [](const testing::TestParamInfo<BeginStringCase>& i) {
                             return std::string{i.param.name};
                         });

// `kind` by is_fixt_admin_msgtype on every row, a non-FIXT 35=A included. A view of one a test
// builds, since the session delivers a Logon to no callback.
TEST(W10CBeginString44, ALogonIsAdmin) {
    expect_row(read_built(raw_frame("FIX.4.4", "35=A\x01")), Row{kAdmin, kV44, kNone}, "a Logon");
}

// A BeginString no enumerator names: `kind` by 35, and both versions Unknown.
TEST(W10CBeginStringUnnamed, KindBy35_BothUnknown) {
    expect_row(read_built(raw_frame("FIX.9.9", "35=D\x01")),
               Row{kApp, FIXPP_SESSION_VER_UNKNOWN, kNone}, "an application message");
    expect_row(read_built(raw_frame("FIX.9.9", "35=A\x01")),
               Row{kAdmin, FIXPP_SESSION_VER_UNKNOWN, kNone}, "an admin message");
}

// Tag 8 absent, as at `c0a828ef`: all zero, and the code.
TEST(W10CNoTag8, AllZero_TagNotFound) {
    const std::string body = "35=D\x01";
    const std::string text = "9=" + std::to_string(body.size()) + "\x01" + body + "10=000\x01";
    const auto* bytes = reinterpret_cast<const std::byte*>(text.data());
    const GetterRead got = read_built(std::vector<std::byte>(bytes, bytes + text.size()));
    EXPECT_EQ(got.rc, FIXPP_ERR_TAG_NOT_FOUND);
    EXPECT_EQ(got.row.kind, 0);
    EXPECT_EQ(got.row.session, 0);
    EXPECT_EQ(got.row.application, 0);
    EXPECT_EQ(got.reserved, 0);
}

}  // namespace
}  // namespace fixpp::session::test099
