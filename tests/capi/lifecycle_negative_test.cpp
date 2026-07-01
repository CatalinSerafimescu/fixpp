// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/capi/lifecycle_negative_test.cpp — 050 Feature B (CA-005/006/007).
//
// Negative/guard contract coverage for the engine + session lifecycle and op
// surface (src/capi/engine.cpp + session.cpp): per-symbol null-handle guards,
// the register-before-start enforcement (FR-004/FR-011), version-major gate,
// clock-not-set + double-start surfacing, eager send validation, and the
// never-established close outcome. The headline round-trip exercises only the
// happy path; this suite drives the rejection arms (no establishment needed —
// every case is reachable pre-start or via a deliberate misuse).

#include <gtest/gtest.h>

#include "fix/c_api/engine.h"
#include "fix/c_api/handles.h"
#include "fix/c_api/session.h"
#include "fix/c_api/version.h"

namespace {

constexpr uint16_t kMajor = FIXPP_C_ABI_VERSION_MAJOR;
constexpr uint16_t kMinor = FIXPP_C_ABI_VERSION_MINOR;

// Build a minimal, valid session config (comp ids + begin string + role). No
// dictionary/endpoint needed: register_session (pre-start) does not require them
// with the default validate_inbound_messages=off.
fixpp_session_config_t* make_session_cfg(const char* sender, const char* target) {
    fixpp_session_config_t* sc = nullptr;
    EXPECT_EQ(fixpp_session_config_create(&sc), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_comp_ids(sc, sender, target), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_begin_string(sc, "FIX.4.2"), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_role(sc, FIXPP_ROLE_INITIATOR), FIXPP_ERR_OK);
    return sc;
}

// Create an engine WITH a real-time clock (so start() does not reject).
fixpp_engine_t* make_engine() {
    fixpp_engine_config_t* ec = nullptr;
    EXPECT_EQ(fixpp_engine_config_create(&ec), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_engine_config_set_realtime_clock(ec), FIXPP_ERR_OK);
    fixpp_engine_t* eng = nullptr;
    EXPECT_EQ(fixpp_engine_create(ec, kMajor, kMinor, &eng), FIXPP_ERR_OK);
    return eng;
}

// ── Engine create guards ──────────────────────────────────────────────────────

TEST(CapiLifecycleNegative, EngineCreateRejectsNullOutAndNullCfg) {
    fixpp_engine_config_t* ec = nullptr;
    ASSERT_EQ(fixpp_engine_config_create(&ec), FIXPP_ERR_OK);
    // Null out_engine — builder NOT consumed (caller still owns ec).
    EXPECT_EQ(fixpp_engine_create(ec, kMajor, kMinor, nullptr), FIXPP_ERR_NULL_HANDLE);
    // Null cfg with a valid out.
    fixpp_engine_t* eng = nullptr;
    EXPECT_EQ(fixpp_engine_create(nullptr, kMajor, kMinor, &eng), FIXPP_ERR_NULL_HANDLE);
    EXPECT_EQ(eng, nullptr);
    fixpp_engine_config_destroy(ec);
}

TEST(CapiLifecycleNegative, EngineCreateRejectsMajorMismatch) {
    fixpp_engine_config_t* ec = nullptr;
    ASSERT_EQ(fixpp_engine_config_create(&ec), FIXPP_ERR_OK);
    fixpp_engine_t* eng = nullptr;
    // Wrong MAJOR is rejected before the engine is built — builder NOT consumed.
    EXPECT_EQ(fixpp_engine_create(ec, static_cast<uint16_t>(kMajor + 1), kMinor, &eng),
              FIXPP_ERR_VERSION_MISMATCH);
    EXPECT_EQ(eng, nullptr);
    fixpp_engine_config_destroy(ec);
}

TEST(CapiLifecycleNegative, EngineCreateRejectsPreGaMajorZero) {
    // Pin the GA precondition: this test is only meaningful after the 0→1 GA freeze.
    static_assert(kMajor == 1, "GA precondition: this test pins that pre-GA major 0 is now rejected");
    // This directly witnesses the 0→1 GA freeze's core behavioral change: an old
    // consumer compiled against C-ABI major 0 is now refused with VERSION_MISMATCH.
    // The existing kMajor+1 mismatch test does NOT cover this specific case (it tests
    // major=2, not major=0; a carve-out accepting major==0 would pass the kMajor+1 test
    // yet still break the GA freeze contract).
    fixpp_engine_config_t* ec = nullptr;
    ASSERT_EQ(fixpp_engine_config_create(&ec), FIXPP_ERR_OK);
    fixpp_engine_t* eng = nullptr;
    // Pre-GA major 0 must be refused — builder NOT consumed.
    EXPECT_EQ(fixpp_engine_create(ec, /*pre-GA major=*/0, kMinor, &eng),
              FIXPP_ERR_VERSION_MISMATCH);
    EXPECT_EQ(eng, nullptr);
    fixpp_engine_config_destroy(ec);
}

// ── Engine start guards ───────────────────────────────────────────────────────

TEST(CapiLifecycleNegative, EngineStartRejectsNullHandle) {
    EXPECT_EQ(fixpp_engine_start(nullptr), FIXPP_ERR_NULL_HANDLE);
}

TEST(CapiLifecycleNegative, EngineStartRejectsMissingClock) {
    // No realtime clock configured → Engine::start() validates and fails closed
    // (clock_not_set → FIXPP_ERR_THREAD_CONFIG), engine_started_ stays false.
    fixpp_engine_config_t* ec = nullptr;
    ASSERT_EQ(fixpp_engine_config_create(&ec), FIXPP_ERR_OK);
    fixpp_engine_t* eng = nullptr;
    ASSERT_EQ(fixpp_engine_create(ec, kMajor, kMinor, &eng), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_engine_start(eng), FIXPP_ERR_THREAD_CONFIG);
    fixpp_engine_destroy(eng);
}

TEST(CapiLifecycleNegative, EngineStartTwiceIsRejected) {
    fixpp_engine_t* eng = make_engine();
    EXPECT_EQ(fixpp_engine_start(eng), FIXPP_ERR_OK);
    // Second start() is illegal (Engine::start is once) → session_already_open.
    EXPECT_NE(fixpp_engine_start(eng), FIXPP_ERR_OK);
    fixpp_engine_destroy(eng);
}

// ── Session open guards ───────────────────────────────────────────────────────

TEST(CapiLifecycleNegative, SessionOpenRejectsNullArgs) {
    fixpp_engine_t* eng = make_engine();
    fixpp_session_config_t* sc = make_session_cfg("S", "T");
    fixpp_session_t* s = nullptr;
    EXPECT_EQ(fixpp_session_open(nullptr, sc, &s), FIXPP_ERR_NULL_HANDLE);
    EXPECT_EQ(fixpp_session_open(eng, nullptr, &s), FIXPP_ERR_NULL_HANDLE);
    EXPECT_EQ(fixpp_session_open(eng, sc, nullptr), FIXPP_ERR_NULL_HANDLE);
    fixpp_session_config_destroy(sc);  // not consumed on the rejected paths
    fixpp_engine_destroy(eng);
}

TEST(CapiLifecycleNegative, SessionOpenAfterStartIsRejected) {
    fixpp_engine_t* eng = make_engine();
    ASSERT_EQ(fixpp_engine_start(eng), FIXPP_ERR_OK);
    fixpp_session_config_t* sc = make_session_cfg("S", "T");
    fixpp_session_t* s = nullptr;
    // Register-after-start is a C-ABI-enforced config error (FR-004).
    EXPECT_EQ(fixpp_session_open(eng, sc, &s), FIXPP_ERR_CAPI_CONFIG_INVALID);
    EXPECT_EQ(s, nullptr);
    fixpp_session_config_destroy(sc);
    fixpp_engine_destroy(eng);
}

TEST(CapiLifecycleNegative, SessionOpenDuplicateIdIsRejected) {
    fixpp_engine_t* eng = make_engine();
    fixpp_session_config_t* sc1 = make_session_cfg("S", "T");
    fixpp_session_t* s1 = nullptr;
    ASSERT_EQ(fixpp_session_open(eng, sc1, &s1), FIXPP_ERR_OK);  // sc1 consumed
    // Same SessionId → register_session rejects (session_invalid_argument 119 →
    // FIXPP_ERR_SESSION_INVALID_ARGUMENT 1400, PUBLISHED by the 051 [2i §4.3]
    // amendment; was FIXPP_ERR_UNKNOWN under the 050 L-050-4 descope). Builder NOT consumed.
    fixpp_session_config_t* sc2 = make_session_cfg("S", "T");
    fixpp_session_t* s2 = nullptr;
    EXPECT_EQ(fixpp_session_open(eng, sc2, &s2), FIXPP_ERR_SESSION_INVALID_ARGUMENT);
    EXPECT_EQ(s2, nullptr);
    fixpp_session_config_destroy(sc2);
    fixpp_engine_destroy(eng);
}

// ── Session op guards (pre-start registered session, never established) ────────

TEST(CapiLifecycleNegative, SessionOpsRejectNullHandles) {
    bool est = true;
    EXPECT_EQ(fixpp_session_is_established(nullptr, &est), FIXPP_ERR_NULL_HANDLE);
    EXPECT_EQ(fixpp_session_close(nullptr), FIXPP_ERR_NULL_HANDLE);
    EXPECT_EQ(fixpp_session_send(nullptr, reinterpret_cast<const uint8_t*>("x"), 1),
              FIXPP_ERR_NULL_HANDLE);
    EXPECT_EQ(fixpp_session_register_callback(nullptr, nullptr, nullptr), FIXPP_ERR_NULL_HANDLE);
}

TEST(CapiLifecycleNegative, IsEstablishedRejectsNullOutParam) {
    fixpp_engine_t* eng = make_engine();
    fixpp_session_config_t* sc = make_session_cfg("S", "T");
    fixpp_session_t* s = nullptr;
    ASSERT_EQ(fixpp_session_open(eng, sc, &s), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_is_established(s, nullptr), FIXPP_ERR_NULL_HANDLE);
    // A registered-but-never-established session reads false.
    bool est = true;
    EXPECT_EQ(fixpp_session_is_established(s, &est), FIXPP_ERR_OK);
    EXPECT_FALSE(est);
    fixpp_engine_destroy(eng);
}

TEST(CapiLifecycleNegative, SendRejectsNullOrEmptyFrame) {
    fixpp_engine_t* eng = make_engine();
    fixpp_session_config_t* sc = make_session_cfg("S", "T");
    fixpp_session_t* s = nullptr;
    ASSERT_EQ(fixpp_session_open(eng, sc, &s), FIXPP_ERR_OK);
    // check_session passes (valid handle); the eager frame validation rejects.
    EXPECT_EQ(fixpp_session_send(s, nullptr, 4), FIXPP_ERR_CAPI_CONFIG_INVALID);
    EXPECT_EQ(fixpp_session_send(s, reinterpret_cast<const uint8_t*>("x"), 0),
              FIXPP_ERR_CAPI_CONFIG_INVALID);
    fixpp_engine_destroy(eng);
}

TEST(CapiLifecycleNegative, RegisterCallbackPreStartThenPostStartRejected) {
    fixpp_engine_t* eng = make_engine();
    fixpp_session_config_t* sc = make_session_cfg("S", "T");
    fixpp_session_t* s = nullptr;
    ASSERT_EQ(fixpp_session_open(eng, sc, &s), FIXPP_ERR_OK);
    auto cb = [](const fixpp_msg_t*, void*) {};
    EXPECT_EQ(fixpp_session_register_callback(s, cb, nullptr), FIXPP_ERR_OK);   // pre-start OK
    EXPECT_EQ(fixpp_session_register_callback(s, nullptr, nullptr), FIXPP_ERR_OK);  // clear
    ASSERT_EQ(fixpp_engine_start(eng), FIXPP_ERR_OK);
    // Post-start registration would race fromApp on the strand → enforced reject.
    EXPECT_EQ(fixpp_session_register_callback(s, cb, nullptr), FIXPP_ERR_CAPI_CONFIG_INVALID);
    fixpp_engine_destroy(eng);
}

TEST(CapiLifecycleNegative, CloseNeverEstablishedIsLifecycleOutcome) {
    fixpp_engine_t* eng = make_engine();
    fixpp_session_config_t* sc = make_session_cfg("S", "T");
    fixpp_session_t* s = nullptr;
    ASSERT_EQ(fixpp_session_open(eng, sc, &s), FIXPP_ERR_OK);
    // Never started/established → engine lookup misses → already-closed lifecycle
    // outcome (not OK), and the handle is invalidated.
    EXPECT_EQ(fixpp_session_close(s), FIXPP_ERR_THREAD_SESSION_LIFECYCLE);
    // Second close on the now-invalid handle is the terminal invalid-handle code.
    EXPECT_EQ(fixpp_session_close(s), FIXPP_ERR_INVALID_HANDLE);
    fixpp_engine_destroy(eng);
}

// NOTE (issue #151): the established-then-reaped idempotent-close contract is NOT
// testable here — a reaped session stays NON-null in lookup (the engine retains the
// entry on disconnect), so it reaches fixpp_session_close's else branch, not the
// null branch this never-established case exercises. The real-mechanism OK arm is
// witnessed end-to-end by send_recv_test.cpp::CloseReapedSessionIsIdempotentOk; its
// ever_established-discriminated THREAD_SESSION_LIFECYCLE sibling
// (CloseReapedNeverEstablishedIsLifecycle) is a seam-driven branch-discrimination
// witness (it flips the latch off on a really-drained session).

}  // namespace
