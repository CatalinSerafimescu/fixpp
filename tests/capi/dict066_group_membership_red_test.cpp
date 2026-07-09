// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/capi/dict066_group_membership_red_test.cpp
//
// 066-dict-backed-inbound-parse T005 — RED-first witness (C-ABI engine
// loopback). Drives the T001 group-bearing FIX44 ExecutionReport(35=8)
// frame (NoLegs(555) x2 + trailing TransactTime(60)) through a real
// two-C-ABI-engine plaintext-TCP loopback pair
// (capi_dict066_loopback_support.hpp) to a registered receive callback, and
// queries the trailing tag at the LAST NoLegs(555) instance via
// `fixpp_group_get_field_string`.
//
// MUST be observed RED against the current dict-free positional parse
// (`Session::parse_and_dispatch_`, src/session/session.cpp:316, default
// `Parser<access_mode::Index>` with no dictionary) — this file does NOT
// flip that site (T006's job).
//
// Anchors: tasks.md T005; spec.md US1 Independent Test; contracts/
// inbound-parse.md C1/C3; research.md Decision 6.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <thread>
#include <vector>

#include "fix/c_api/engine.h"
#include "fix/c_api/message.h"
#include "fix/c_api/session.h"

#include "capi_dict066_loopback_support.hpp"
#include "capi_loopback_support.hpp"

#include "support/fix44_group_frame_bodies.hpp"

using namespace std::chrono_literals;
using namespace fixpp::capi_test;
using namespace fixpp::capi_test066;

namespace {

TEST(GroupMembershipCapiRed, TrailingFieldAbsentFromLastInstance) {
    fixpp_engine_t* acceptor_engine = nullptr;
    fixpp_engine_t* initiator_engine = nullptr;
    ASSERT_EQ(fixpp_engine_create(make_engine_cfg(), 1, 0, &acceptor_engine), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_engine_create(make_engine_cfg(), 1, 0, &initiator_engine), FIXPP_ERR_OK);

    // Acceptor session: registers the receive callback.
    fixpp_session_config_t* acc_cfg =
        make_session_cfg_fix44("ACC-066R", "INI-066R", FIXPP_ROLE_ACCEPTOR);
    set_loopback_endpoint(acc_cfg, "127.0.0.1", 0);
    auto acc_id = session_id_of(acc_cfg);
    fixpp_session_t* acc_h = nullptr;
    ASSERT_EQ(fixpp_session_open(acceptor_engine, acc_cfg, &acc_h), FIXPP_ERR_OK);

    struct CbCtx {
        std::atomic<bool> fired{false};
        std::size_t group_count = 0;
        // last_trailing_rc / last_trailing_present observe the RED behavior:
        // querying tag 60 (TransactTime) at the LAST NoLegs(555) instance.
        fixpp_error_t last_trailing_rc = FIXPP_ERR_UNKNOWN;
        std::string_view last_trailing_val;
        fixpp_error_t get_group_rc = FIXPP_ERR_UNKNOWN;
        // non-discriminating sanity: each leg's own declared LegSymbol(600).
        fixpp_error_t leg0_symbol_rc = FIXPP_ERR_UNKNOWN;
        fixpp_error_t leg1_symbol_rc = FIXPP_ERR_UNKNOWN;
    } ctx;
    auto cb = [](const fixpp_msg_t* inbound, void* ud) {
        auto* c = static_cast<CbCtx*>(ud);
        ASSERT_NE(inbound, nullptr);

        const fixpp_group_t* grp = nullptr;
        std::size_t count = 0;
        c->get_group_rc = fixpp_msg_get_group(inbound, 555, &grp, &count);
        if (c->get_group_rc == FIXPP_ERR_OK) {
            c->group_count = count;

            const char* v = nullptr;
            std::size_t vlen = 0;
            c->leg0_symbol_rc = fixpp_group_get_field_string(grp, 0, 600, &v, &vlen);
            if (count >= 2) {
                c->leg1_symbol_rc = fixpp_group_get_field_string(grp, 1, 600, &v, &vlen);
            }

            const char* tv = nullptr;
            std::size_t tvlen = 0;
            c->last_trailing_rc =
                fixpp_group_get_field_string(grp, count - 1, 60, &tv, &tvlen);
            if (c->last_trailing_rc == FIXPP_ERR_OK) {
                c->last_trailing_val = std::string_view(tv, tvlen);
            }
        }
        c->fired.store(true, std::memory_order_release);
    };
    ASSERT_EQ(fixpp_session_register_callback(acc_h, cb, &ctx), FIXPP_ERR_OK);

    ASSERT_EQ(fixpp_engine_start(acceptor_engine), FIXPP_ERR_OK);
    std::uint16_t port = wait_for_bound_port(acceptor_engine, acc_id);
    ASSERT_NE(port, 0u) << "acceptor did not bind";

    // Initiator session.
    fixpp_session_config_t* ini_cfg =
        make_session_cfg_fix44("INI-066R", "ACC-066R", FIXPP_ROLE_INITIATOR);
    set_loopback_endpoint(ini_cfg, "127.0.0.1", port);
    fixpp_session_t* ini_h = nullptr;
    ASSERT_EQ(fixpp_session_open(initiator_engine, ini_cfg, &ini_h), FIXPP_ERR_OK);

    ASSERT_EQ(fixpp_engine_start(initiator_engine), FIXPP_ERR_OK);
    ASSERT_TRUE(wait_for_established(ini_h)) << "initiator never established";
    ASSERT_TRUE(wait_for_established(acc_h)) << "acceptor never established";

    // Send the group-bearing ExecutionReport (NoLegs x2 + trailing 60=) as
    // the application payload.
    auto suffix = fixpp_test_support::execution_report_two_legs_trailing_suffix();
    auto payload = fixpp_test_support::make_execution_report_app_payload(suffix);
    ASSERT_EQ(fixpp_session_send(ini_h, payload.data(), payload.size()), FIXPP_ERR_OK);

    const auto until = std::chrono::steady_clock::now() + 5s;
    while (!ctx.fired.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < until) {
        std::this_thread::sleep_for(5ms);
    }

    ASSERT_TRUE(ctx.fired.load()) << "the group-bearing ExecutionReport must reach the acceptor's "
                                     "registered receive callback";

    // Non-discriminating sanity checks.
    ASSERT_EQ(ctx.get_group_rc, FIXPP_ERR_OK) << "NoLegs(555) must resolve as a group";
    EXPECT_EQ(ctx.group_count, 2U) << "NoLegs(555)=2 must yield exactly 2 instances";
    EXPECT_EQ(ctx.leg0_symbol_rc, FIXPP_ERR_OK) << "leg #1's own LegSymbol(600) must read OK";
    if (ctx.group_count >= 2) {
        EXPECT_EQ(ctx.leg1_symbol_rc, FIXPP_ERR_OK) << "leg #2's own LegSymbol(600) must read OK";
    }

    // DISCRIMINATING RED assertion: the trailing outer field TransactTime(60)
    // queried on the LAST NoLegs(555) instance must be FIXPP_ERR_TAG_NOT_FOUND.
    // On the current dict-free positional parse the last instance's extent
    // runs to end-of-message and ABSORBS tag 60 (returns FIXPP_ERR_OK with the
    // absorbed value) — so this is RED pre-066/T006.
    EXPECT_EQ(ctx.last_trailing_rc, FIXPP_ERR_TAG_NOT_FOUND)
        << "trailing TransactTime(60) must NOT be readable from the last NoLegs(555) "
           "instance; got rc="
        << static_cast<int>(ctx.last_trailing_rc) << " value=\"" << ctx.last_trailing_val << "\"";

    EXPECT_EQ(fixpp_session_close(ini_h), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_close(acc_h), FIXPP_ERR_OK);
    fixpp_engine_destroy(initiator_engine);
    fixpp_engine_destroy(acceptor_engine);
}

}  // namespace
