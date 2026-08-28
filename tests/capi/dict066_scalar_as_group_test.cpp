// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/capi/dict066_scalar_as_group_test.cpp
//
// 066-dict-backed-inbound-parse T010 — scalar-as-group witness (C-ABI engine
// loopback). Drives the T001 group-bearing FIX44 ExecutionReport(35=8)
// frame through a real two-C-ABI-engine plaintext-TCP loopback pair
// (capi_dict066_loopback_support.hpp) to a registered receive callback, and
// queries a plain SCALAR tag — Symbol(55), `<field number='55'
// name='Symbol' type='STRING' />` at dictionaries/FIX44.xml:4028, NOT a
// NUMINGROUP count field — via `fixpp_msg_get_group`.
//
// Contract (C2 / SC-002; src/capi/message_read.cpp:336-380):
// `fixpp_msg_get_group(msg, 55, ...)` must return FIXPP_ERR_TYPE_MISMATCH
// (present-but-not-a-group), never FIXPP_ERR_OK with a spurious instance.
//
// GREEN today (T006 already dict-backs the parse site). RED-first is
// proven by TEMPORARY mutation of src/session/session.cpp:316 (not
// committed) per the phase-implementer brief — this file is unconditionally
// the same in both configurations.
//
// Anchors: tasks.md T010; spec.md US2 Independent Test / SC-002;
// contracts/inbound-parse.md C2; research.md Decision 6.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <thread>

#include "fix/c_api/engine.h"
#include "fix/c_api/message.h"
#include "fix/c_api/session.h"

#include "capi_dict066_loopback_support.hpp"
#include "capi_loopback_support.hpp"

#include "support/fix44_group_frame_bodies.hpp"
#include "support/wait_until.hpp"

using namespace std::chrono_literals;
using namespace fixpp::capi_test;
using namespace fixpp::capi_test066;

namespace {

TEST(ScalarAsGroupCapi, SymbolTagQueriedAsGroupReturnsTypeMismatch) {
    fixpp_engine_t* acceptor_engine = nullptr;
    fixpp_engine_t* initiator_engine = nullptr;
    ASSERT_EQ(fixpp_engine_create(make_engine_cfg(), 1, 0, &acceptor_engine), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_engine_create(make_engine_cfg(), 1, 0, &initiator_engine), FIXPP_ERR_OK);

    // Acceptor session: registers the receive callback.
    fixpp_session_config_t* acc_cfg =
        make_session_cfg_fix44("ACC-066S", "INI-066S", FIXPP_ROLE_ACCEPTOR);
    set_loopback_endpoint(acc_cfg, "127.0.0.1", 0);
    auto acc_id = session_id_of(acc_cfg);
    fixpp_session_t* acc_h = nullptr;
    ASSERT_EQ(fixpp_session_open(acceptor_engine, acc_cfg, &acc_h), FIXPP_ERR_OK);

    struct CbCtx {
        std::atomic<bool> fired{false};
        // Non-discriminating sanity: Symbol(55) IS present in the message.
        fixpp_error_t find_symbol_rc = FIXPP_ERR_UNKNOWN;
        // DISCRIMINATING: querying Symbol(55) — a plain scalar, never a
        // NUMINGROUP count field — as a group.
        fixpp_error_t get_group_rc = FIXPP_ERR_UNKNOWN;
        std::size_t group_count = 999;
    } ctx;
    auto cb = [](const fixpp_msg_t* inbound, void* ud) {
        auto* c = static_cast<CbCtx*>(ud);
        ASSERT_NE(inbound, nullptr);

        const char* v = nullptr;
        std::size_t vlen = 0;
        c->find_symbol_rc = fixpp_msg_get_string(inbound, 55, &v, &vlen);

        const fixpp_group_t* grp = nullptr;
        std::size_t count = 0;
        c->get_group_rc = fixpp_msg_get_group(inbound, 55, &grp, &count);
        c->group_count = count;

        c->fired.store(true, std::memory_order_release);
    };
    ASSERT_EQ(fixpp_session_register_callback(acc_h, cb, &ctx), FIXPP_ERR_OK);

    ASSERT_EQ(fixpp_engine_start(acceptor_engine), FIXPP_ERR_OK);
    std::uint16_t port = wait_for_bound_port(acceptor_engine, acc_id);
    ASSERT_NE(port, 0u) << "acceptor did not bind";

    // Initiator session.
    fixpp_session_config_t* ini_cfg =
        make_session_cfg_fix44("INI-066S", "ACC-066S", FIXPP_ROLE_INITIATOR);
    set_loopback_endpoint(ini_cfg, "127.0.0.1", port);
    fixpp_session_t* ini_h = nullptr;
    ASSERT_EQ(fixpp_session_open(initiator_engine, ini_cfg, &ini_h), FIXPP_ERR_OK);

    ASSERT_EQ(fixpp_engine_start(initiator_engine), FIXPP_ERR_OK);
    ASSERT_TRUE(wait_for_established(ini_h)) << "initiator never established";
    ASSERT_TRUE(wait_for_established(acc_h)) << "acceptor never established";

    // Send the group-bearing ExecutionReport (NoLegs x2 + Symbol(55) mid-body,
    // NOT the last field) as the application payload.
    auto suffix = fixpp_test_support::execution_report_two_legs_trailing_suffix();
    auto payload = fixpp_test_support::make_execution_report_app_payload(suffix);
    ASSERT_EQ(fixpp_session_send(ini_h, payload.data(), payload.size()), FIXPP_ERR_OK);

    (void)fixpp::test_support::wait_for_flag(ctx.fired, 5s);  // the ASSERT_TRUE(ctx.fired) below is the oracle

    ASSERT_TRUE(ctx.fired.load()) << "the ExecutionReport must reach the acceptor's registered "
                                     "receive callback";

    // Non-discriminating sanity check: Symbol(55) is present as a scalar.
    EXPECT_EQ(ctx.find_symbol_rc, FIXPP_ERR_OK) << "Symbol(55) must be readable as a scalar field";

    // DISCRIMINATING assertion (C2 / SC-002): querying scalar tag Symbol(55)
    // as a group must return TYPE_MISMATCH, never OK-with-a-spurious-instance
    // (the dict-free-parse symptom: a bogus 1-count group spanning the rest
    // of the message).
    EXPECT_EQ(ctx.get_group_rc, FIXPP_ERR_TYPE_MISMATCH)
        << "fixpp_msg_get_group(msg, 55, ...) on scalar tag Symbol(55) must return "
           "FIXPP_ERR_TYPE_MISMATCH; got rc="
        << static_cast<int>(ctx.get_group_rc) << " count=" << ctx.group_count;

    EXPECT_EQ(fixpp_session_close(ini_h), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_close(acc_h), FIXPP_ERR_OK);
    fixpp_engine_destroy(initiator_engine);
    fixpp_engine_destroy(acceptor_engine);
}

}  // namespace
