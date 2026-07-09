// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/capi/dict066_group_loopback_smoke_test.cpp
//
// 066-dict-backed-inbound-parse T001 — compile/smoke check for the C-ABI
// engine-loopback harness (capi_dict066_loopback_support.hpp): a real
// two-C-ABI-engine plaintext-TCP loopback pair, configured with the REAL
// FIX44 dictionary, delivers a group-bearing ExecutionReport(35=8)
// (NoLegs(555) x2 + trailing TransactTime(60)) to a registered receive
// callback, and the callback can reach it via `fixpp_msg_*` accessors.
//
// HARD CONSTRAINT (research.md Decision 6 / [const Art VII §3]): this file
// asserts ONLY that the callback fires and the delivered message's MsgType
// is readable — it makes NO assertion about group extents, membership,
// instance counts, or trailing-field absence. Those assertions are T005 and
// MUST be written and observed RED later, against the unchanged dict-free
// parse; an assertion here would destroy that RED-first proof.
//
// Anchors: tasks.md T001; spec.md US1 Independent Test; research.md
// Decision 1/2/6; contracts/inbound-parse.md C1/C3.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
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

TEST(GroupScaffoldCapiSmoke, TwoLegsTrailingPayloadDeliveredToCallback) {
    fixpp_engine_t* acceptor_engine = nullptr;
    fixpp_engine_t* initiator_engine = nullptr;
    ASSERT_EQ(fixpp_engine_create(make_engine_cfg(), 1, 0, &acceptor_engine), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_engine_create(make_engine_cfg(), 1, 0, &initiator_engine), FIXPP_ERR_OK);

    // Acceptor session: registers the receive callback.
    fixpp_session_config_t* acc_cfg = make_session_cfg_fix44("ACC-066", "INI-066", FIXPP_ROLE_ACCEPTOR);
    set_loopback_endpoint(acc_cfg, "127.0.0.1", 0);
    auto acc_id = session_id_of(acc_cfg);
    fixpp_session_t* acc_h = nullptr;
    ASSERT_EQ(fixpp_session_open(acceptor_engine, acc_cfg, &acc_h), FIXPP_ERR_OK);

    struct CbCtx {
        std::atomic<bool> fired{false};
        const fixpp_msg_t* last = nullptr;
    } ctx;
    auto cb = [](const fixpp_msg_t* inbound, void* ud) {
        auto* c = static_cast<CbCtx*>(ud);
        ASSERT_NE(inbound, nullptr);
        // Smoke-only: read the top-level MsgType (NOT a group-content check).
        const char* out = nullptr;
        std::size_t len = 0;
        fixpp_error_t rc = fixpp_msg_get_msg_type(inbound, &out, &len);
        EXPECT_EQ(rc, FIXPP_ERR_OK);
        if (rc == FIXPP_ERR_OK) {
            EXPECT_EQ(std::string_view(out, len), "8") << "delivered message must be ExecutionReport";
        }
        c->fired.store(true, std::memory_order_release);
    };
    ASSERT_EQ(fixpp_session_register_callback(acc_h, cb, &ctx), FIXPP_ERR_OK);

    ASSERT_EQ(fixpp_engine_start(acceptor_engine), FIXPP_ERR_OK);
    std::uint16_t port = wait_for_bound_port(acceptor_engine, acc_id);
    ASSERT_NE(port, 0u) << "acceptor did not bind";

    // Initiator session.
    fixpp_session_config_t* ini_cfg =
        make_session_cfg_fix44("INI-066", "ACC-066", FIXPP_ROLE_INITIATOR);
    set_loopback_endpoint(ini_cfg, "127.0.0.1", port);
    fixpp_session_t* ini_h = nullptr;
    ASSERT_EQ(fixpp_session_open(initiator_engine, ini_cfg, &ini_h), FIXPP_ERR_OK);

    ASSERT_EQ(fixpp_engine_start(initiator_engine), FIXPP_ERR_OK);
    ASSERT_TRUE(wait_for_established(ini_h)) << "initiator never established";
    ASSERT_TRUE(wait_for_established(acc_h)) << "acceptor never established";

    // Send the group-bearing ExecutionReport (NoLegs x2 + trailing 60=) as
    // the application payload (fixpp_session_send takes a payload, not a
    // committed wire frame — the session stamps the header/trailer itself).
    auto suffix = fixpp_test_support::execution_report_two_legs_trailing_suffix();
    auto payload = fixpp_test_support::make_execution_report_app_payload(suffix);
    ASSERT_EQ(fixpp_session_send(ini_h, payload.data(), payload.size()), FIXPP_ERR_OK);

    const auto until = std::chrono::steady_clock::now() + 5s;
    while (!ctx.fired.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < until) {
        std::this_thread::sleep_for(5ms);
    }

    EXPECT_TRUE(ctx.fired.load()) << "the group-bearing ExecutionReport must reach the acceptor's "
                                     "registered receive callback";

    EXPECT_EQ(fixpp_session_close(ini_h), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_close(acc_h), FIXPP_ERR_OK);
    fixpp_engine_destroy(initiator_engine);
    fixpp_engine_destroy(acceptor_engine);
}

}  // namespace
