// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/capi/capi_dict066_loopback_support.hpp
//
// 066-dict-backed-inbound-parse T001 — C-ABI engine-loopback harness helpers:
// a real two-C-ABI-engine plaintext-TCP loopback session PAIR, configured
// with the REAL FIX44 dictionary (tests/support/fix44_dictionary.hpp) so a
// registered receive callback observes group-registered (NoLegs(555))
// inbound messages through the SHIPPED path.
//
// MIRRORS capi_loopback_support.hpp's make_session_cfg / set_loopback_endpoint
// / wait_for_bound_port / wait_for_established / make_engine_cfg (reused
// as-is) — the ONLY delta here is the dictionary seam: this header installs
// the real FIX44 dict (L-050-1 seam) instead of the FIX 4.2 minimal dict, so
// 066's US1/US2 witnesses (T005) can drive a group-bearing ExecutionReport
// through `fixpp_session_send` -> the peer's registered receive callback and
// read it back via `fixpp_msg_*` / `fixpp_group_*`.
//
// RED-FIRST PRESERVATION (T001 hard constraint): this header contains NO
// group-membership/extent assertions — that is T005's job, RED-first against
// the unchanged dict-free parse. This file only provides the loopback-pair
// construction mechanics.
#pragma once

#include <gtest/gtest.h>

#include "fix/c_api/engine.h"
#include "fix/c_api/session.h"

#include "capi_internal.hpp"          // fixpp_dict internals
#include "capi_loopback_support.hpp"  // make_engine_cfg / wait_for_* / set_loopback_endpoint

#include "fixpp/session/session_config.hpp"  // SessionId::from_config
#include "support/fix44_dictionary.hpp"

namespace fixpp::capi_test066 {

// A test-owned fixpp_dict handle over the REAL FIX44 dictionary (mirrors
// capi_loopback_support.hpp's make_test_dict_handle, swapped to FIX44).
inline fixpp_dict_t* make_fix44_dict_handle() {
    auto* d = new fixpp_dict{fixpp::test_support::make_fix44_dictionary()};
    return reinterpret_cast<fixpp_dict_t*>(d);
}

inline void destroy_fix44_dict_handle(fixpp_dict_t* h) {
    delete reinterpret_cast<fixpp_dict*>(h);
}

// Build a plaintext session-config builder through the real setters + the
// FIX44 dict seam. Endpoint is set separately via
// fixpp::capi_test::set_loopback_endpoint (L-050-5).
inline fixpp_session_config_t* make_session_cfg_fix44(const char* sender, const char* target,
                                                       fixpp_session_role role) {
    fixpp_session_config_t* sc = nullptr;
    EXPECT_EQ(fixpp_session_config_create(&sc), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_comp_ids(sc, sender, target), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_begin_string(sc, "FIX.4.4"), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_role(sc, role), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_heartbeat_seconds(sc, 30), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_security(sc, FIXPP_SECURITY_INSECURE_PLAIN_TCP, nullptr,
                                                 nullptr),
              FIXPP_ERR_OK);
    // Initiator resets to seq 1 on logon so a fresh pair logs on cleanly
    // (mirrors capi_loopback_support.hpp's make_session_cfg).
    EXPECT_EQ(fixpp_session_config_set_reset_on_logon(sc, role == FIXPP_ROLE_INITIATOR),
              FIXPP_ERR_OK);
    fixpp_dict_t* d = make_fix44_dict_handle();
    EXPECT_EQ(fixpp_session_config_set_dictionary(sc, d), FIXPP_ERR_OK);
    destroy_fix44_dict_handle(d);  // setter copied the shared_ptr
    return sc;
}

}  // namespace fixpp::capi_test066
