// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/capi/dict495_clone_shared_table_test.cpp
//
// fixpp#495 (`.specify/495-493-486-dict-reify-copy.md` §2.6, §3.4, §10 T-13, C
// twin): the SHIPPED C-ABI dispatch path over a real engine loopback. The
// acceptor's receive callback clones two inbound messages; both clones SHARE the
// session's table (`fixpp_session::tv_`) instead of deep-copying it. Then every
// engine, session and dictionary handle is destroyed and each clone still reads
// its group — under ASan, a clone reading freed table storage is reported.
//
// No `weak.expired()` arm: on the C ABI the retained session shell holds the
// Dictionary for the process lifetime, so expiry is moot there (§3.4, owner
// ruling Q-6). The C++ twin (tests/session/test_reify_shared_dispatch.cpp)
// carries that property.
//
// Mutation: `parse_and_dispatch_`'s parser back to `{*inbound_tv_}` (the borrowed
// route) — the clones copy, and the address equalities go RED.
//
// Standalone (`[const §VII.8]`): two live engines on loopback sockets.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "capi_dict066_loopback_support.hpp"
#include "capi_internal.hpp"
#include "capi_loopback_support.hpp"
#include "fix/c_api/engine.h"
#include "fix/c_api/message.h"
#include "fix/c_api/session.h"
#include "support/fix44_group_frame_bodies.hpp"
#include "support/wait_until.hpp"

using namespace std::chrono_literals;
using namespace fixpp::capi_test;
using namespace fixpp::capi_test066;

namespace {

TEST(Dict495CloneSharedTable, ClonesShareTheSessionTableAndOutliveTheEngine) {
    fixpp_engine_t* acceptor_engine = nullptr;
    fixpp_engine_t* initiator_engine = nullptr;
    ASSERT_EQ(fixpp_engine_create(make_engine_cfg(), 1, 0, &acceptor_engine), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_engine_create(make_engine_cfg(), 1, 0, &initiator_engine), FIXPP_ERR_OK);

    fixpp_session_config_t* acc_cfg =
        make_session_cfg_fix44("ACC-495", "INI-495", FIXPP_ROLE_ACCEPTOR);
    set_loopback_endpoint(acc_cfg, "127.0.0.1", 0);
    auto acc_id = session_id_of(acc_cfg);
    fixpp_session_t* acc_h = nullptr;
    ASSERT_EQ(fixpp_session_open(acceptor_engine, acc_cfg, &acc_h), FIXPP_ERR_OK);
    void const* const session_table = reinterpret_cast<fixpp_session*>(acc_h)->tv_.get();
    ASSERT_NE(session_table, nullptr);

    struct CbCtx {
        std::atomic<int> fired{0};
        fixpp_msg_t* clones[2] = {nullptr, nullptr};
        void const* clone_tables[2] = {nullptr, nullptr};
        fixpp_error_t clone_rc[2] = {FIXPP_ERR_UNKNOWN, FIXPP_ERR_UNKNOWN};
    } ctx;
    auto cb = [](const fixpp_msg_t* inbound, void* ud) {
        auto* c = static_cast<CbCtx*>(ud);
        int const i = c->fired.load(std::memory_order_relaxed);
        if (i < 2) {
            c->clone_rc[i] = fixpp_msg_clone(inbound, &c->clones[i]);
            if (c->clones[i] != nullptr) {
                c->clone_tables[i] = reinterpret_cast<const fixpp_msg*>(c->clones[i])->owned_tv_.get();
            }
        }
        c->fired.store(i + 1, std::memory_order_release);
    };
    ASSERT_EQ(fixpp_session_register_callback(acc_h, cb, &ctx), FIXPP_ERR_OK);

    ASSERT_EQ(fixpp_engine_start(acceptor_engine), FIXPP_ERR_OK);
    std::uint16_t port = wait_for_bound_port(acceptor_engine, acc_id);
    ASSERT_NE(port, 0U) << "acceptor did not bind";

    fixpp_session_config_t* ini_cfg =
        make_session_cfg_fix44("INI-495", "ACC-495", FIXPP_ROLE_INITIATOR);
    set_loopback_endpoint(ini_cfg, "127.0.0.1", port);
    fixpp_session_t* ini_h = nullptr;
    ASSERT_EQ(fixpp_session_open(initiator_engine, ini_cfg, &ini_h), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_engine_start(initiator_engine), FIXPP_ERR_OK);
    ASSERT_TRUE(wait_for_established(ini_h)) << "initiator never established";
    ASSERT_TRUE(wait_for_established(acc_h)) << "acceptor never established";

    auto const payload = fixpp_test_support::make_execution_report_app_payload(
        fixpp_test_support::execution_report_two_legs_trailing_suffix());
    ASSERT_EQ(fixpp_session_send(ini_h, payload.data(), payload.size()), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_session_send(ini_h, payload.data(), payload.size()), FIXPP_ERR_OK);
    ASSERT_TRUE(fixpp::test_support::wait_until_observed(
        [&] { return ctx.fired.load(std::memory_order_acquire) >= 2; }, 5s))
        << "both ExecutionReports must reach the acceptor's receive callback";

    for (int i = 0; i < 2; ++i) {
        ASSERT_EQ(ctx.clone_rc[i], FIXPP_ERR_OK);
        ASSERT_NE(ctx.clones[i], nullptr);
    }
    EXPECT_EQ(ctx.clone_tables[0], ctx.clone_tables[1])
        << "two clones from the shipped dispatch path must SHARE one table";
    EXPECT_EQ(ctx.clone_tables[0], session_table)
        << "the clones share the session's own table (fixpp_session::tv_)";

    // Destroy every engine, session and (already released) configuration and
    // dictionary handle; the clones must still read through the table they pin.
    EXPECT_EQ(fixpp_session_close(ini_h), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_close(acc_h), FIXPP_ERR_OK);
    fixpp_engine_destroy(initiator_engine);
    fixpp_engine_destroy(acceptor_engine);

    for (auto* clone : ctx.clones) {
        const fixpp_group_t* grp = nullptr;
        std::size_t count = 0;
        EXPECT_EQ(fixpp_msg_get_group(clone, 555, &grp, &count), FIXPP_ERR_OK);
        EXPECT_EQ(count, 2U);
        EXPECT_EQ(fixpp_msg_destroy(clone), FIXPP_ERR_OK);
    }
}

}  // namespace
