// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_099_resolved_version.cpp
//
// 099-fixt-appl-version-validation, contracts/witnesses.md W-10's `dict::reify` half for non-FIXT
// frames (tasks.md T019): data-model E-8's two non-FIXT rows through dict::reify(...).version() on
// a FIX.4.4 session, each with a profile naming the frame's own version, as E-8 says. The rest of
// W-10 needs the accessor and joins this file in its own phase.

#include <gtest/gtest.h>

#include <fixpp/dict/reify.hpp>
#include <memory_resource>
#include <optional>
#include <string>

#include "test_099_fixt_support.hpp"

namespace fixpp::session::test099 {
namespace {

using Resolved = dd::resolved_message_version;

const dd::version_profile kFix44Profile{dd::session_version::v44, dd::application_version::v44,
                                        false, 0};

struct Fix44Session {
    CellRun run;
    std::optional<Resolved> seen;
    std::string reify_error;

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
        auto read = [this](const CountingApp::View& v) {
            auto h = dd::reify(v, kFix44Profile, std::pmr::new_delete_resource());
            if (h) {
                seen = h->version();
            } else {
                reify_error = "reify error " + std::to_string(static_cast<int>(h.error()));
            }
        };
        run.rig->app->on_app = read;
        run.rig->app->on_admin = read;
        const auto ctx = run.rig->peer_ctx(1);
        (void)run.rig->deliver(wire("FIX.4.4", "35=A\x01" + hdr(ctx) + kLogonBody));
    }
};

TEST(W10NonFixtReify, ApplicationRow) {
    Fix44Session s;
    ASSERT_TRUE(s.run.setup_failure.empty()) << s.run.setup_failure;
    ASSERT_EQ(s.run.rig->sess->state(), fsm_state::Active);
    (void)s.run.rig->deliver(wire("FIX.4.4", "35=D\x01" + hdr(s.run.rig->peer_ctx(2)) + kNos));
    ASSERT_TRUE(s.seen.has_value()) << "fromApp did not run, or " << s.reify_error;
    EXPECT_EQ(s.seen->k, Resolved::kind::application);
    EXPECT_EQ(s.seen->session, dd::session_version::v44);
    EXPECT_EQ(s.seen->application, dd::application_version::v44);
}

TEST(W10NonFixtReify, AdminRow) {
    Fix44Session s;
    ASSERT_TRUE(s.run.setup_failure.empty()) << s.run.setup_failure;
    ASSERT_EQ(s.run.rig->sess->state(), fsm_state::Active);
    (void)s.run.rig->deliver(wire("FIX.4.4", "35=0\x01" + hdr(s.run.rig->peer_ctx(2))));
    ASSERT_TRUE(s.seen.has_value()) << "fromAdmin did not run, or " << s.reify_error;
    EXPECT_EQ(s.seen->k, Resolved::kind::session_admin);
    EXPECT_EQ(s.seen->session, dd::session_version::v44);
    EXPECT_EQ(s.seen->application, dd::application_version::Unknown);
}

}  // namespace
}  // namespace fixpp::session::test099
