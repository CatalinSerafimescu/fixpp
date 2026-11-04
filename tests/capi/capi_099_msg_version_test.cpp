// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/capi/capi_099_msg_version_test.cpp
//
// 099-fixt-appl-version-validation, contracts/witnesses.md W-10's clone cells (tasks.md T085):
// `fixpp_msg_clone`, and a clone of a clone, answer as their source did (data-model E-5). Each
// handle is read through the view it holds, in capi_internal.hpp. The clone's dict-backed branch is
// the one a Session's view reaches, so those cells clone inside the callback, as the C engine's
// receive path wraps a view. Its dict-free branch takes a view parsed dict-free whose byte the test
// writes through the detail key.
//
// A real Session on an io_context the test pumps: no engine and no worker thread, which is
// capi_pure_tests' rule.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <fixpp/wire/framer.hpp>
#include <fixpp/wire/parser.hpp>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <tuple>

#include "capi_internal.hpp"
#include "fix/c_api/error.h"
#include "fix/c_api/message.h"
#include "session/test_099_fixt_support.hpp"

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

}  // namespace
}  // namespace fixpp::session::test099
