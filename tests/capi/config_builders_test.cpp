// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/capi/config_builders_test.cpp — 050 Feature B (CA-005).
//
// Exhaustive contract coverage for the opaque engine/session config builders
// (src/capi/config.cpp): every setter's null-handle guard, every eager
// validation arm (→ FIXPP_ERR_CAPI_CONFIG_INVALID), every enum mapping, and the
// happy path. The round-trip/lifecycle tests exercise only the subset of setters
// the loopback needs, leaving the builders' guards/validation undertested; this
// suite closes that gap (per-symbol contract, mirrors the 049 error-surface
// approach). Pure builder unit tests — no engine, no event loop.

#include <gtest/gtest.h>

#include "fix/c_api/engine.h"
#include "fix/c_api/session.h"

#include "capi_loopback_support.hpp"  // make_test_dict_handle / destroy_test_dict_handle (L-050-1)

using namespace fixpp::capi_test;  // NOLINT(google-build-using-namespace) — test scope

namespace {

// ── Engine-config builder ─────────────────────────────────────────────────────

TEST(CapiConfigBuilders, EngineConfigCreateRejectsNullOutHandle) {
    EXPECT_EQ(fixpp_engine_config_create(nullptr), FIXPP_ERR_NULL_HANDLE);
}

TEST(CapiConfigBuilders, EngineConfigCreateSucceeds) {
    fixpp_engine_config_t* cfg = nullptr;
    ASSERT_EQ(fixpp_engine_config_create(&cfg), FIXPP_ERR_OK);
    ASSERT_NE(cfg, nullptr);
    fixpp_engine_config_destroy(cfg);
}

TEST(CapiConfigBuilders, EngineConfigSettersRejectNullHandle) {
    EXPECT_EQ(fixpp_engine_config_set_worker_threads(nullptr, 4), FIXPP_ERR_NULL_HANDLE);
    EXPECT_EQ(fixpp_engine_config_set_realtime_clock(nullptr), FIXPP_ERR_NULL_HANDLE);
}

TEST(CapiConfigBuilders, EngineConfigWorkerThreadsValidatesNonZero) {
    fixpp_engine_config_t* cfg = nullptr;
    ASSERT_EQ(fixpp_engine_config_create(&cfg), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_engine_config_set_worker_threads(cfg, 0), FIXPP_ERR_CAPI_CONFIG_INVALID);
    EXPECT_EQ(fixpp_engine_config_set_worker_threads(cfg, 1), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_engine_config_set_worker_threads(cfg, 8), FIXPP_ERR_OK);
    fixpp_engine_config_destroy(cfg);
}

TEST(CapiConfigBuilders, EngineConfigRealtimeClockSucceeds) {
    fixpp_engine_config_t* cfg = nullptr;
    ASSERT_EQ(fixpp_engine_config_create(&cfg), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_engine_config_set_realtime_clock(cfg), FIXPP_ERR_OK);
    fixpp_engine_config_destroy(cfg);
}

// ── Session-config builder ────────────────────────────────────────────────────

TEST(CapiConfigBuilders, SessionConfigCreateRejectsNullOutHandle) {
    EXPECT_EQ(fixpp_session_config_create(nullptr), FIXPP_ERR_NULL_HANDLE);
}

TEST(CapiConfigBuilders, SessionConfigCreateSucceeds) {
    fixpp_session_config_t* cfg = nullptr;
    ASSERT_EQ(fixpp_session_config_create(&cfg), FIXPP_ERR_OK);
    ASSERT_NE(cfg, nullptr);
    fixpp_session_config_destroy(cfg);
}

TEST(CapiConfigBuilders, SessionConfigSettersRejectNullHandle) {
    EXPECT_EQ(fixpp_session_config_set_comp_ids(nullptr, "S", "T"), FIXPP_ERR_NULL_HANDLE);
    EXPECT_EQ(fixpp_session_config_set_begin_string(nullptr, "FIX.4.2"), FIXPP_ERR_NULL_HANDLE);
    EXPECT_EQ(fixpp_session_config_set_role(nullptr, FIXPP_ROLE_INITIATOR), FIXPP_ERR_NULL_HANDLE);
    EXPECT_EQ(fixpp_session_config_set_heartbeat_seconds(nullptr, 30), FIXPP_ERR_NULL_HANDLE);
    EXPECT_EQ(fixpp_session_config_set_security(nullptr, FIXPP_SECURITY_INSECURE_PLAIN_TCP, nullptr,
                                                nullptr),
              FIXPP_ERR_NULL_HANDLE);
    EXPECT_EQ(fixpp_session_config_set_dictionary(nullptr, nullptr), FIXPP_ERR_NULL_HANDLE);
    EXPECT_EQ(fixpp_session_config_set_reset_on_logon(nullptr, true), FIXPP_ERR_NULL_HANDLE);
}

TEST(CapiConfigBuilders, CompIdsRejectNullOrEmpty) {
    fixpp_session_config_t* cfg = nullptr;
    ASSERT_EQ(fixpp_session_config_create(&cfg), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_comp_ids(cfg, nullptr, "T"), FIXPP_ERR_CAPI_CONFIG_INVALID);
    EXPECT_EQ(fixpp_session_config_set_comp_ids(cfg, "S", nullptr), FIXPP_ERR_CAPI_CONFIG_INVALID);
    EXPECT_EQ(fixpp_session_config_set_comp_ids(cfg, "", "T"), FIXPP_ERR_CAPI_CONFIG_INVALID);
    EXPECT_EQ(fixpp_session_config_set_comp_ids(cfg, "S", ""), FIXPP_ERR_CAPI_CONFIG_INVALID);
    EXPECT_EQ(fixpp_session_config_set_comp_ids(cfg, "SENDER", "TARGET"), FIXPP_ERR_OK);
    fixpp_session_config_destroy(cfg);
}

TEST(CapiConfigBuilders, BeginStringRejectsNullOrEmpty) {
    fixpp_session_config_t* cfg = nullptr;
    ASSERT_EQ(fixpp_session_config_create(&cfg), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_begin_string(cfg, nullptr), FIXPP_ERR_CAPI_CONFIG_INVALID);
    EXPECT_EQ(fixpp_session_config_set_begin_string(cfg, ""), FIXPP_ERR_CAPI_CONFIG_INVALID);
    EXPECT_EQ(fixpp_session_config_set_begin_string(cfg, "FIX.4.2"), FIXPP_ERR_OK);
    fixpp_session_config_destroy(cfg);
}

TEST(CapiConfigBuilders, RoleMapsBothEnumsAndRejectsOutOfRange) {
    fixpp_session_config_t* cfg = nullptr;
    ASSERT_EQ(fixpp_session_config_create(&cfg), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_role(cfg, FIXPP_ROLE_INITIATOR), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_role(cfg, FIXPP_ROLE_ACCEPTOR), FIXPP_ERR_OK);
    // FFI bypass: a value no C enumerator names hits the post-switch reject arm.
    EXPECT_EQ(fixpp_session_config_set_role(cfg, static_cast<fixpp_session_role>(99)),
              FIXPP_ERR_CAPI_CONFIG_INVALID);
    fixpp_session_config_destroy(cfg);
}

TEST(CapiConfigBuilders, HeartbeatSecondsSucceeds) {
    fixpp_session_config_t* cfg = nullptr;
    ASSERT_EQ(fixpp_session_config_create(&cfg), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_heartbeat_seconds(cfg, 0), FIXPP_ERR_OK);   // 0 is accepted
    EXPECT_EQ(fixpp_session_config_set_heartbeat_seconds(cfg, 30), FIXPP_ERR_OK);
    fixpp_session_config_destroy(cfg);
}

TEST(CapiConfigBuilders, SecurityMapsBothKindsAndRejectsOutOfRange) {
    fixpp_session_config_t* cfg = nullptr;
    ASSERT_EQ(fixpp_session_config_create(&cfg), FIXPP_ERR_OK);
    // cert/key are unused in Feature B (full TLS wiring deferred) — pass nullptr.
    EXPECT_EQ(fixpp_session_config_set_security(cfg, FIXPP_SECURITY_TLS, nullptr, nullptr),
              FIXPP_ERR_OK);
    EXPECT_EQ(
        fixpp_session_config_set_security(cfg, FIXPP_SECURITY_INSECURE_PLAIN_TCP, nullptr, nullptr),
        FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_security(cfg, static_cast<fixpp_security_kind>(99), nullptr,
                                                nullptr),
              FIXPP_ERR_CAPI_CONFIG_INVALID);
    fixpp_session_config_destroy(cfg);
}

TEST(CapiConfigBuilders, DictionaryRejectsNullAndAcceptsReal) {
    fixpp_session_config_t* cfg = nullptr;
    ASSERT_EQ(fixpp_session_config_create(&cfg), FIXPP_ERR_OK);
    // Null outer handle and a handle wrapping a null inner both fail closed.
    EXPECT_EQ(fixpp_session_config_set_dictionary(cfg, nullptr), FIXPP_ERR_CAPI_CONFIG_INVALID);
    fixpp_dict_t* empty = reinterpret_cast<fixpp_dict_t*>(new fixpp_dict{nullptr});
    EXPECT_EQ(fixpp_session_config_set_dictionary(cfg, empty), FIXPP_ERR_CAPI_CONFIG_INVALID);
    delete reinterpret_cast<fixpp_dict*>(empty);
    // A real dictionary handle (L-050-1 seam) is copied through.
    fixpp_dict_t* dict = make_test_dict_handle();
    EXPECT_EQ(fixpp_session_config_set_dictionary(cfg, dict), FIXPP_ERR_OK);
    destroy_test_dict_handle(dict);
    fixpp_session_config_destroy(cfg);
}

TEST(CapiConfigBuilders, ResetOnLogonSucceedsBothValues) {
    fixpp_session_config_t* cfg = nullptr;
    ASSERT_EQ(fixpp_session_config_create(&cfg), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_reset_on_logon(cfg, true), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_reset_on_logon(cfg, false), FIXPP_ERR_OK);
    fixpp_session_config_destroy(cfg);
}

TEST(CapiConfigBuilders, DestroyIsNullSafe) {
    fixpp_engine_config_destroy(nullptr);   // must not crash
    fixpp_session_config_destroy(nullptr);  // must not crash
    SUCCEED();
}

}  // namespace
