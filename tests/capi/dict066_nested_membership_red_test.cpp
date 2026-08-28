// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/capi/dict066_nested_membership_red_test.cpp
//
// 065-cabi-nested-group-membership T009 — FR-011(b) witness: ENGINE-LOOPBACK
// dispatch-path proof. Drives the SAME issue-#179 FIX44 ExecutionReport(35=8)
// nested shape (NoLegs(555) x1 -> NoLegSecurityAltID(604) x2 -> trailing
// LegQty(687)) through a real two-C-ABI-engine plaintext-TCP loopback
// (capi_dict066_loopback_support.hpp) to a registered receive callback, then
// descends via the C-ABI on the RECEIVED handle — the production dispatch
// path (Session::parse_and_dispatch_ -> fixpp_msg_get_group ->
// fixpp_group_get_nested_group), not a standalone Parser{} unit parse.
//
// This pins production msg_type/parent-path context threading (the 063
// Gate-B RC#1 empty-msg_type class) that T008's direct as_table_view()
// witness cannot reach, since T008 builds its own table_view/MessageView by
// hand rather than going through Session::open()'s inbound_tv_ + the real
// dispatch callback.
//
// Anchors: tasks.md T009; spec.md FR-011(b); quickstart.md Sec 6(b);
// contracts/cabi-nested-read.md C7(b).

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "fix/c_api/engine.h"
#include "fix/c_api/message.h"
#include "fix/c_api/session.h"

#include "capi_dict066_loopback_support.hpp"
#include "capi_loopback_support.hpp"
#include "support/wait_until.hpp"

using namespace std::chrono_literals;
using namespace fixpp::capi_test;
using namespace fixpp::capi_test066;

namespace {

// The real FIX44-dict-registered nested shape: ExecutionReport(35=8) ->
// NoLegs(555) x1 -> [LegSymbol(600), NoLegSecurityAltID(604) x2
// [LegSecurityAltID(605), LegSecurityAltIDSource(606)], LegQty(687)].
// Mirrors tests/support/fix44_group_frame_bodies.hpp's required-field set
// (ExecutionReport's own required fields) plus the nested-shape body used by
// T008 (tests/capi/message_read_test.cpp NestedTrailingMemberExcluded /
// G604Entry witnesses) and by
// dict066_group_membership_red_test.cpp for the required-field precedent.
std::vector<std::uint8_t> make_nested_membership_app_payload() {
    std::string b = "35=8\x01";
    b += "37=ORDID-1\x01";  // OrderID (required)
    b += "17=EXEC-1\x01";   // ExecID (required)
    b += "150=0\x01";       // ExecType (required)
    b += "39=0\x01";        // OrdStatus (required)
    b += "55=AAPL\x01";     // Symbol (Instrument component, required)
    b += "54=1\x01";        // Side (required)
    b += "151=0\x01";       // LeavesQty (required)
    b += "14=0\x01";        // CumQty (required)
    b += "6=0\x01";         // AvgPx (required)
    b += "555=1\x01";       // NoLegs = 1
    b += "600=LEG0\x01";    // leg #1: LegSymbol
    b += "604=2\x01";       // leg #1: NoLegSecurityAltID = 2
    b += "605=ALT0\x01";    //   nested #1: LegSecurityAltID
    b += "606=S0\x01";      //   nested #1: LegSecurityAltIDSource
    b += "605=ALT1\x01";    //   nested #2: LegSecurityAltID
    b += "606=S1\x01";      //   nested #2: LegSecurityAltIDSource
    b += "687=100\x01";     // leg #1: LegQty (trailing member of the OUTER
                            // NoLegs entry, AFTER the nested group)
    return std::vector<std::uint8_t>(b.begin(), b.end());
}

TEST(NestedGroupMembershipCapiRed, TrailingMemberAbsentFromLastNestedInstance) {
    fixpp_engine_t* acceptor_engine = nullptr;
    fixpp_engine_t* initiator_engine = nullptr;
    ASSERT_EQ(fixpp_engine_create(make_engine_cfg(), 1, 0, &acceptor_engine), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_engine_create(make_engine_cfg(), 1, 0, &initiator_engine), FIXPP_ERR_OK);

    // Acceptor session: registers the receive callback.
    fixpp_session_config_t* acc_cfg =
        make_session_cfg_fix44("ACC-065N", "INI-065N", FIXPP_ROLE_ACCEPTOR);
    set_loopback_endpoint(acc_cfg, "127.0.0.1", 0);
    auto acc_id = session_id_of(acc_cfg);
    fixpp_session_t* acc_h = nullptr;
    ASSERT_EQ(fixpp_session_open(acceptor_engine, acc_cfg, &acc_h), FIXPP_ERR_OK);

    struct CbCtx {
        std::atomic<bool> fired{false};
        fixpp_error_t outer_rc = FIXPP_ERR_UNKNOWN;
        std::size_t outer_count = 0;
        fixpp_error_t nested_rc = FIXPP_ERR_UNKNOWN;
        std::size_t nested_count = 0;
        fixpp_error_t nested0_605_rc = FIXPP_ERR_UNKNOWN;
        fixpp_error_t nested0_606_rc = FIXPP_ERR_UNKNOWN;
        fixpp_error_t nested1_605_rc = FIXPP_ERR_UNKNOWN;
        fixpp_error_t nested1_606_rc = FIXPP_ERR_UNKNOWN;
        std::string_view nested0_605_val;
        std::string_view nested0_606_val;
        std::string_view nested1_605_val;
        std::string_view nested1_606_val;
        // DISCRIMINATOR: 687 (LegQty) queried on the LAST nested
        // (NoLegSecurityAltID) instance must be TAG_NOT_FOUND.
        fixpp_error_t nested_last_687_rc = FIXPP_ERR_UNKNOWN;
        // 687 legitimately belongs to the OUTER NoLegs entry.
        fixpp_error_t outer_687_rc = FIXPP_ERR_UNKNOWN;
        std::string_view outer_687_val;
    } ctx;
    auto cb = [](const fixpp_msg_t* inbound, void* ud) {
        auto* c = static_cast<CbCtx*>(ud);
        ASSERT_NE(inbound, nullptr);

        const fixpp_group_t* outer = nullptr;
        std::size_t outer_count = 0;
        c->outer_rc = fixpp_msg_get_group(inbound, 555, &outer, &outer_count);
        if (c->outer_rc == FIXPP_ERR_OK) {
            c->outer_count = outer_count;

            const fixpp_group_t* nested = nullptr;
            std::size_t nested_count = 0;
            c->nested_rc = fixpp_group_get_nested_group(outer, 0, 604, &nested, &nested_count);
            if (c->nested_rc == FIXPP_ERR_OK) {
                c->nested_count = nested_count;

                const char* v = nullptr;
                std::size_t vlen = 0;
                c->nested0_605_rc = fixpp_group_get_field_string(nested, 0, 605, &v, &vlen);
                if (c->nested0_605_rc == FIXPP_ERR_OK) {
                    c->nested0_605_val = std::string_view(v, vlen);
                }
                c->nested0_606_rc = fixpp_group_get_field_string(nested, 0, 606, &v, &vlen);
                if (c->nested0_606_rc == FIXPP_ERR_OK) {
                    c->nested0_606_val = std::string_view(v, vlen);
                }
                if (nested_count >= 2) {
                    c->nested1_605_rc = fixpp_group_get_field_string(nested, 1, 605, &v, &vlen);
                    if (c->nested1_605_rc == FIXPP_ERR_OK) {
                        c->nested1_605_val = std::string_view(v, vlen);
                    }
                    c->nested1_606_rc = fixpp_group_get_field_string(nested, 1, 606, &v, &vlen);
                    if (c->nested1_606_rc == FIXPP_ERR_OK) {
                        c->nested1_606_val = std::string_view(v, vlen);
                    }
                }

                if (nested_count > 0) {
                    c->nested_last_687_rc =
                        fixpp_group_get_field_string(nested, nested_count - 1, 687, &v, &vlen);
                }
            }

            const char* ov = nullptr;
            std::size_t ovlen = 0;
            c->outer_687_rc = fixpp_group_get_field_string(outer, 0, 687, &ov, &ovlen);
            if (c->outer_687_rc == FIXPP_ERR_OK) {
                c->outer_687_val = std::string_view(ov, ovlen);
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
        make_session_cfg_fix44("INI-065N", "ACC-065N", FIXPP_ROLE_INITIATOR);
    set_loopback_endpoint(ini_cfg, "127.0.0.1", port);
    fixpp_session_t* ini_h = nullptr;
    ASSERT_EQ(fixpp_session_open(initiator_engine, ini_cfg, &ini_h), FIXPP_ERR_OK);

    ASSERT_EQ(fixpp_engine_start(initiator_engine), FIXPP_ERR_OK);
    ASSERT_TRUE(wait_for_established(ini_h)) << "initiator never established";
    ASSERT_TRUE(wait_for_established(acc_h)) << "acceptor never established";

    // Send the nested-group-bearing ExecutionReport as the application
    // payload.
    auto payload = make_nested_membership_app_payload();
    ASSERT_EQ(fixpp_session_send(ini_h, payload.data(), payload.size()), FIXPP_ERR_OK);

    (void)fixpp::test_support::wait_for_flag(ctx.fired, 5s);  // the ASSERT_TRUE(ctx.fired) below is the oracle

    ASSERT_TRUE(ctx.fired.load()) << "the nested-group-bearing ExecutionReport must reach the "
                                     "acceptor's registered receive callback";

    // Non-discriminating sanity checks: outer group + genuine nested members.
    ASSERT_EQ(ctx.outer_rc, FIXPP_ERR_OK) << "NoLegs(555) must resolve as a group";
    EXPECT_EQ(ctx.outer_count, 1U) << "NoLegs(555)=1 must yield exactly 1 instance";
    ASSERT_EQ(ctx.nested_rc, FIXPP_ERR_OK) << "NoLegSecurityAltID(604) must resolve as a nested group";
    EXPECT_EQ(ctx.nested_count, 2U) << "NoLegSecurityAltID(604)=2 must yield exactly 2 instances";

    EXPECT_EQ(ctx.nested0_605_rc, FIXPP_ERR_OK);
    EXPECT_EQ(ctx.nested0_605_val, "ALT0");
    EXPECT_EQ(ctx.nested0_606_rc, FIXPP_ERR_OK);
    EXPECT_EQ(ctx.nested0_606_val, "S0");
    EXPECT_EQ(ctx.nested1_605_rc, FIXPP_ERR_OK);
    EXPECT_EQ(ctx.nested1_605_val, "ALT1");
    EXPECT_EQ(ctx.nested1_606_rc, FIXPP_ERR_OK);
    EXPECT_EQ(ctx.nested1_606_val, "S1");

    // DISCRIMINATING assertion: the trailing outer-entry field LegQty(687)
    // queried on the LAST NoLegSecurityAltID(604) instance must be
    // FIXPP_ERR_TAG_NOT_FOUND — it belongs to the OUTER NoLegs entry, not the
    // nested group's own extent.
    EXPECT_EQ(ctx.nested_last_687_rc, FIXPP_ERR_TAG_NOT_FOUND)
        << "LegQty(687) must NOT be reachable through the last "
           "NoLegSecurityAltID(604) instance's own span; got rc="
        << static_cast<int>(ctx.nested_last_687_rc);

    // 687 legitimately belongs to the OUTER NoLegs(555) occurrence.
    EXPECT_EQ(ctx.outer_687_rc, FIXPP_ERR_OK)
        << "LegQty(687) must be reachable at the OUTER NoLegs(555) index";
    EXPECT_EQ(ctx.outer_687_val, "100");

    EXPECT_EQ(fixpp_session_close(ini_h), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_close(acc_h), FIXPP_ERR_OK);
    fixpp_engine_destroy(initiator_engine);
    fixpp_engine_destroy(acceptor_engine);
}

}  // namespace
