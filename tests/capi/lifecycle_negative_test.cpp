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

// Issue #151 branch-discrimination seam (defined unconditionally in
// src/capi/session.cpp; the capi_internal.hpp declaration is FIXPP_TEST_HOOKS-
// gated, so it is forward-declared here the same way error_block_test.cpp
// forward-declares translate()). Forces the sticky ever_established latch on so
// the established-then-reaped close branch is witnessed without a real reap race.
namespace fixpp_capi::detail {
void set_session_ever_established(fixpp_session_t* session, bool on) noexcept;
}  // namespace fixpp_capi::detail

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

// Issue #151: a session that reached established at least once but is now reaped
// from the engine registry (e.g. the peer disconnected first) must close as an
// idempotent success (OK), NOT THREAD_SESSION_LIFECYCLE. lookup()==nullptr alone
// cannot tell this apart from the never-established case above; the sticky
// ever_established latch is what discriminates.
//
// Deterministic branch witness: open a session, force the latch on via the test
// seam, and do NOT start the engine — so lookup(id) misses exactly as in
// CloseNeverEstablished above. The ONLY difference from that test is the latch, so
// this is a discriminating witness for the new OK arm: drop that arm in
// fixpp_session_close and this flips back to THREAD_SESSION_LIFECYCLE (301). The
// realistic established-then-reaped lifecycle is covered end-to-end by
// CapiSendRecv.TwoEngineRoundTripReplyFromDrainThread.
TEST(CapiLifecycleNegative, CloseEstablishedThenReapedIsIdempotentOk) {
    fixpp_engine_t* eng = make_engine();
    fixpp_session_config_t* sc = make_session_cfg("S", "T");
    fixpp_session_t* s = nullptr;
    ASSERT_EQ(fixpp_session_open(eng, sc, &s), FIXPP_ERR_OK);
    fixpp_capi::detail::set_session_ever_established(s, true);
    // Engine never started → lookup misses; with the latch set, the gone session
    // is treated as established-then-reaped → idempotent OK.
    EXPECT_EQ(fixpp_session_close(s), FIXPP_ERR_OK);
    // The handle is still invalidated on the OK close → the next close is terminal.
    EXPECT_EQ(fixpp_session_close(s), FIXPP_ERR_INVALID_HANDLE);
    fixpp_engine_destroy(eng);
}

}  // namespace
