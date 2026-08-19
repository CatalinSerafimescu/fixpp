// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/capi/public_roundtrip_test.cpp — US2 / SC-001 / T011
// Two-engine TCP loopback round-trip through the pure public C-ABI surface.
// NO seam includes (no capi_loopback_support.hpp, no capi_internal.hpp,
// no src/capi headers) — the new public setters drive every step.
//
// SC-001 witnesses:
//   - fixpp_session_config_set_tcp_endpoint (T013) — endpoint configuration
//   - fixpp_session_acceptor_bound_endpoint (T014) — ephemeral port readback
//   - fixpp_session_config_set_reset_seqnum_policy (T013) — seqnum-reset policy
//   - E1 witness: dict handle destroyed BEFORE session open; session establishes
//     via the config's independently-held shared_ptr copy.
//   - D-4 empirical finding (2026-06-26): the production-default BILATERAL_STRICT
//     establishes a fresh two-engine pair (both reset_on_logon=true) through the
//     public C-ABI; verified 20/20 over loopback. BILATERAL_LENIENT is NOT required.
//   - Error-path assertions for every new function (NULL / out-of-range).
//     ⚠️ The set_reset_seqnum_policy OUT-OF-RANGE case is no longer here: it moved
//     to tests/capi/reset_seqnum_policy_range_test.c (#268), because it cannot be
//     expressed in C++ without the caller itself committing undefined behaviour.
//     The NULL-handle and valid-value cases stay below. Re-pointed rather than
//     left describing a witness this file no longer carries.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "fix/c_api/dict.h"
#include "fix/c_api/engine.h"
#include "fix/c_api/message.h"  // fixpp_msg_get_string / fixpp_msg_get_msg_type (F7 receipt check)
#include "fix/c_api/session.h"
#include "fix/c_api/version.h"

// FIXPP_DICT_DIR is injected by CMake as "${CMAKE_SOURCE_DIR}/dictionaries".
static constexpr const char* kDictPath = FIXPP_DICT_DIR "/FIX42.xml";

namespace {

// ── Poll helpers (public API only) ────────────────────────────────────────────

// Spin until fixpp_session_acceptor_bound_endpoint returns a non-zero port or
// the deadline elapses.  Returns 0 on timeout.
uint16_t poll_bound_port(fixpp_session_t* session,
                          std::chrono::milliseconds deadline = std::chrono::milliseconds{3000}) {
    using clock = std::chrono::steady_clock;
    const auto until = clock::now() + deadline;
    for (;;) {
        uint16_t port = 0;
        fixpp_error_t rc = fixpp_session_acceptor_bound_endpoint(session, &port);
        if (rc == FIXPP_ERR_OK && port != 0) return port;
        if (clock::now() >= until) return 0;
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }
}

// Spin until fixpp_session_is_established returns true or the deadline elapses.
bool poll_established(fixpp_session_t* session,
                       std::chrono::milliseconds deadline = std::chrono::milliseconds{4000}) {
    using clock = std::chrono::steady_clock;
    const auto until = clock::now() + deadline;
    for (;;) {
        bool est = false;
        if (fixpp_session_is_established(session, &est) == FIXPP_ERR_OK && est) return true;
        if (clock::now() >= until) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }
}

// Build a minimal FIX application-message payload (MsgType D = NewOrderSingle).
// The session framing tags (8/9/34/49/52/56/10) are excluded; the engine stamps
// and assigns them.  The payload must lead with "35=<type>\x01" per the send
// contract ([2i §10]).
std::vector<uint8_t> make_app_payload(const char* clord_id) {
    std::string p;
    p += "35=D\x01";                                 // MsgType = NewOrderSingle
    p += std::string("11=") + clord_id + "\x01";    // ClOrdID
    p += "55=TESTSYM\x01";                           // Symbol
    p += "54=1\x01";                                 // Side = Buy
    p += "38=100\x01";                               // OrderQty
    p += "40=1\x01";                                 // OrdType = Market
    return std::vector<uint8_t>(p.begin(), p.end());
}

// ── Error-path unit tests — fixpp_session_config_set_tcp_endpoint ─────────────

TEST(PublicRoundtrip, SetTcpEndpointNullCfg) {
    EXPECT_EQ(fixpp_session_config_set_tcp_endpoint(nullptr, "127.0.0.1", 9000),
              FIXPP_ERR_NULL_HANDLE);
}

TEST(PublicRoundtrip, SetTcpEndpointNullHost) {
    fixpp_session_config_t* cfg = nullptr;
    ASSERT_EQ(fixpp_session_config_create(&cfg), FIXPP_ERR_OK);
    ASSERT_NE(cfg, nullptr);
    EXPECT_EQ(fixpp_session_config_set_tcp_endpoint(cfg, nullptr, 9000),
              FIXPP_ERR_NULL_HANDLE);
    fixpp_session_config_destroy(cfg);
}

TEST(PublicRoundtrip, SetTcpEndpointEmptyHost) {
    fixpp_session_config_t* cfg = nullptr;
    ASSERT_EQ(fixpp_session_config_create(&cfg), FIXPP_ERR_OK);
    ASSERT_NE(cfg, nullptr);
    EXPECT_EQ(fixpp_session_config_set_tcp_endpoint(cfg, "", 9000),
              FIXPP_ERR_CAPI_CONFIG_INVALID);
    fixpp_session_config_destroy(cfg);
}

// ── Error-path unit tests — fixpp_session_config_set_reset_seqnum_policy ──────

TEST(PublicRoundtrip, SetResetSeqnumPolicyNullCfg) {
    EXPECT_EQ(
        fixpp_session_config_set_reset_seqnum_policy(nullptr, FIXPP_RESET_SEQNUM_BILATERAL_LENIENT),
        FIXPP_ERR_NULL_HANDLE);
}

// ⚠️ THE OUT-OF-RANGE CASE MOVED TO C — tests/capi/reset_seqnum_policy_range_test.c
// (#268). It cannot be written here without undefined behaviour: passing an
// out-of-range enum BY VALUE is an lvalue-to-rvalue conversion of an invalid enum
// value, so the CALLER performs the UB, whatever the callee does. This file's
// version reported it on every run —
//
//     public_roundtrip_test.cpp:129:5: runtime error: load of value 99, which is
//     not a valid value for type 'fixpp_reset_seqnum_policy'
//
// — and went unnoticed because the ubsan lane ran in UBSan's default RECOVERABLE
// mode and could not fail on it. The comment that used to sit here claimed the
// memcpy avoided the UB; it does not, because memcpy addresses only the STORE.
//
// The C twin covers strictly more: the memcpy form, a direct `(fixpp_reset_seqnum_policy)99`
// cast (UB in C++, well-defined in C, and a second entry into the same guard), the
// three valid enumerators, and the NULL-handle path. Measured with clang-22 on one
// source compiled `-x c` vs `-x c++` under `-fsanitize=undefined`: 0 reports vs 2.
//
// Do NOT reinstate an out-of-range case in this file.

// All three valid enumerator values should map to OK.
TEST(PublicRoundtrip, SetResetSeqnumPolicyValidValues) {
    fixpp_session_config_t* cfg = nullptr;
    ASSERT_EQ(fixpp_session_config_create(&cfg), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_reset_seqnum_policy(cfg, FIXPP_RESET_SEQNUM_BILATERAL_STRICT),
              FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_reset_seqnum_policy(cfg, FIXPP_RESET_SEQNUM_BILATERAL_LENIENT),
              FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_reset_seqnum_policy(cfg, FIXPP_RESET_SEQNUM_UNILATERAL),
              FIXPP_ERR_OK);
    fixpp_session_config_destroy(cfg);
}

// ── Error-path unit tests — fixpp_session_acceptor_bound_endpoint ─────────────

TEST(PublicRoundtrip, AcceptorBoundEndpointNullSession) {
    // session=nullptr + non-null port_out → NULL_HANDLE (from check_session).
    // *port_out is defensively zeroed before check_session (mirrors is_established).
    uint16_t port = 99;
    EXPECT_EQ(fixpp_session_acceptor_bound_endpoint(nullptr, &port), FIXPP_ERR_NULL_HANDLE);
    EXPECT_EQ(port, 0u);  // defensive zero written before check_session returns
}

TEST(PublicRoundtrip, AcceptorBoundEndpointNullPortOut) {
    // session=nullptr + port_out=nullptr → still NULL_HANDLE (first check triggers)
    EXPECT_EQ(fixpp_session_acceptor_bound_endpoint(nullptr, nullptr), FIXPP_ERR_NULL_HANDLE);
}

// ── Live two-engine loopback round-trip ───────────────────────────────────────

TEST(PublicRoundtrip, TwoEngineLoopbackExchangesAppMessage) {
    // D-4 empirical determination (2026-06-26): the production-default
    // bilateral_strict policy establishes a fresh two-engine pair through the
    // public C-ABI — verified 20/20 over loopback with BOTH sides reset_on_logon=true
    // (the clean fresh-pair case, T011). The reset_seqnum_policy setter is exercised
    // explicitly (set to STRICT, == the config-builder default) so the witness is
    // self-documenting; LENIENT (the E-4 contingency) is NOT required. An earlier
    // seam comment speculated strict would time out; direct evidence refutes that.

    // E1 witness: load the dictionary via the public loader (US1), pass it to
    // fixpp_session_config_set_dictionary (which takes a shared_ptr copy), then
    // destroy the consumer's handle BEFORE any session is opened.  The session must
    // still establish, proving the config independently sustains the dictionary.
    fixpp_dict_t* acc_dict = nullptr;
    ASSERT_EQ(fixpp_dict_load_from_xml(kDictPath, &acc_dict), FIXPP_ERR_OK);
    ASSERT_NE(acc_dict, nullptr);

    // ── Acceptor engine ───────────────────────────────────────────────────────
    fixpp_engine_config_t* acc_ecfg = nullptr;
    ASSERT_EQ(fixpp_engine_config_create(&acc_ecfg), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_engine_config_set_realtime_clock(acc_ecfg), FIXPP_ERR_OK);
    fixpp_engine_t* acc_engine = nullptr;
    ASSERT_EQ(fixpp_engine_create(acc_ecfg, FIXPP_C_ABI_VERSION_MAJOR, FIXPP_C_ABI_VERSION_MINOR,
                                  &acc_engine),
              FIXPP_ERR_OK);
    // acc_ecfg consumed by fixpp_engine_create

    fixpp_session_config_t* acc_sc = nullptr;
    ASSERT_EQ(fixpp_session_config_create(&acc_sc), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_session_config_set_comp_ids(acc_sc, "ACCEPTOR", "INITIATOR"), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_session_config_set_begin_string(acc_sc, "FIX.4.2"), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_session_config_set_role(acc_sc, FIXPP_ROLE_ACCEPTOR), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_session_config_set_heartbeat_seconds(acc_sc, 30), FIXPP_ERR_OK);
    ASSERT_EQ(
        fixpp_session_config_set_security(acc_sc, FIXPP_SECURITY_INSECURE_PLAIN_TCP, nullptr, nullptr),
        FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_session_config_set_reset_on_logon(acc_sc, true), FIXPP_ERR_OK);
    // E1 witness step 1: pass dictionary, then destroy handle
    ASSERT_EQ(fixpp_session_config_set_dictionary(acc_sc, acc_dict), FIXPP_ERR_OK);
    fixpp_dict_destroy(acc_dict);  // config's shared_ptr copy now the sole owner
    acc_dict = nullptr;
    // D-4: pin the production-default BILATERAL_STRICT explicitly (self-documenting
    // witness; == the config-builder default). Establishes 20/20 for the fresh pair.
    ASSERT_EQ(
        fixpp_session_config_set_reset_seqnum_policy(acc_sc, FIXPP_RESET_SEQNUM_BILATERAL_STRICT),
        FIXPP_ERR_OK);
    // Bind on loopback, OS-assigned ephemeral port (port 0)
    ASSERT_EQ(fixpp_session_config_set_tcp_endpoint(acc_sc, "127.0.0.1", 0), FIXPP_ERR_OK);

    fixpp_session_t* acc_session = nullptr;
    ASSERT_EQ(fixpp_session_open(acc_engine, acc_sc, &acc_session), FIXPP_ERR_OK);
    // acc_sc consumed

    // E1 witness: null-port-out with a live (valid) session → FIXPP_ERR_NULL_HANDLE
    // (exercises the port_out==nullptr guard on a real handle before engine start)
    EXPECT_EQ(fixpp_session_acceptor_bound_endpoint(acc_session, nullptr), FIXPP_ERR_NULL_HANDLE);

    // Register receive callback on acceptor BEFORE engine_start (FR-011).
    // F7 (gate-b/r1): the callback reads tag 35 (MsgType) and tag 11 (ClOrdID) via
    // the public C-ABI and sets flags, so a future accidental app-message source
    // cannot satisfy the receipt check with the wrong message content.
    struct RecvResult {
        std::atomic<int>  count{0};
        std::atomic<bool> msg_type_d{false};        // tag 35 == "D"
        std::atomic<bool> clord_id_order001{false}; // tag 11 == "ORDER-001"
    };
    RecvResult recv_result;
    ASSERT_EQ(fixpp_session_register_callback(
                  acc_session,
                  [](const fixpp_msg_t* inbound, void* ud) {
                      auto* r = static_cast<RecvResult*>(ud);
                      const char* val = nullptr;
                      size_t      len = 0;
                      // Assert tag 35 == "D" (NewOrderSingle). Store with relaxed before
                      // the release store on count so flags are visible after count is seen.
                      if (fixpp_msg_get_msg_type(inbound, &val, &len) == FIXPP_ERR_OK) {
                          r->msg_type_d.store(
                              val != nullptr && len == 1 && val[0] == 'D',
                              std::memory_order_relaxed);
                      }
                      // Assert tag 11 == "ORDER-001" (ClOrdID from make_app_payload)
                      val = nullptr; len = 0;
                      if (fixpp_msg_get_string(inbound, 11, &val, &len) == FIXPP_ERR_OK) {
                          r->clord_id_order001.store(
                              val != nullptr &&
                              std::string_view(val, len) == "ORDER-001",
                              std::memory_order_relaxed);
                      }
                      // Release store on count LAST: the poll's acquire load on count
                      // guarantees the flag stores above are visible on the main thread.
                      r->count.fetch_add(1, std::memory_order_release);
                  },
                  &recv_result),
              FIXPP_ERR_OK);

    ASSERT_EQ(fixpp_engine_start(acc_engine), FIXPP_ERR_OK);

    // Poll the OS-assigned bound port via the new public API (SC-001)
    uint16_t bound_port = poll_bound_port(acc_session);
    ASSERT_NE(bound_port, 0u) << "Acceptor did not bind within 3s deadline";

    // ── Initiator engine ──────────────────────────────────────────────────────
    fixpp_dict_t* ini_dict = nullptr;
    ASSERT_EQ(fixpp_dict_load_from_xml(kDictPath, &ini_dict), FIXPP_ERR_OK);

    fixpp_engine_config_t* ini_ecfg = nullptr;
    ASSERT_EQ(fixpp_engine_config_create(&ini_ecfg), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_engine_config_set_realtime_clock(ini_ecfg), FIXPP_ERR_OK);
    fixpp_engine_t* ini_engine = nullptr;
    ASSERT_EQ(fixpp_engine_create(ini_ecfg, FIXPP_C_ABI_VERSION_MAJOR, FIXPP_C_ABI_VERSION_MINOR,
                                  &ini_engine),
              FIXPP_ERR_OK);

    fixpp_session_config_t* ini_sc = nullptr;
    ASSERT_EQ(fixpp_session_config_create(&ini_sc), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_session_config_set_comp_ids(ini_sc, "INITIATOR", "ACCEPTOR"), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_session_config_set_begin_string(ini_sc, "FIX.4.2"), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_session_config_set_role(ini_sc, FIXPP_ROLE_INITIATOR), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_session_config_set_heartbeat_seconds(ini_sc, 30), FIXPP_ERR_OK);
    ASSERT_EQ(
        fixpp_session_config_set_security(ini_sc, FIXPP_SECURITY_INSECURE_PLAIN_TCP, nullptr, nullptr),
        FIXPP_ERR_OK);
    // Both sides reset seqnums on logon (141=Y) — the clean fresh-pair case (T011).
    ASSERT_EQ(fixpp_session_config_set_reset_on_logon(ini_sc, true), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_session_config_set_dictionary(ini_sc, ini_dict), FIXPP_ERR_OK);
    fixpp_dict_destroy(ini_dict);
    ini_dict = nullptr;
    // D-4: BILATERAL_STRICT (production default) — see acceptor comment above.
    ASSERT_EQ(
        fixpp_session_config_set_reset_seqnum_policy(ini_sc, FIXPP_RESET_SEQNUM_BILATERAL_STRICT),
        FIXPP_ERR_OK);
    // Connect to the acceptor's OS-assigned port (read back above)
    ASSERT_EQ(fixpp_session_config_set_tcp_endpoint(ini_sc, "127.0.0.1", bound_port), FIXPP_ERR_OK);

    fixpp_session_t* ini_session = nullptr;
    ASSERT_EQ(fixpp_session_open(ini_engine, ini_sc, &ini_session), FIXPP_ERR_OK);

    ASSERT_EQ(fixpp_engine_start(ini_engine), FIXPP_ERR_OK);

    // ── Wait for both sessions to establish ───────────────────────────────────
    ASSERT_TRUE(poll_established(acc_session)) << "Acceptor did not establish within 4s";
    ASSERT_TRUE(poll_established(ini_session)) << "Initiator did not establish within 4s";

    // ── App-message exchange: initiator → acceptor ────────────────────────────
    auto payload = make_app_payload("ORDER-001");
    ASSERT_EQ(fixpp_session_send(ini_session, payload.data(), payload.size()), FIXPP_ERR_OK);

    // Poll for the acceptor's receive callback to fire (with 4s deadline)
    {
        using clock = std::chrono::steady_clock;
        const auto until = clock::now() + std::chrono::milliseconds{4000};
        while (recv_result.count.load(std::memory_order_acquire) == 0 && clock::now() < until) {
            std::this_thread::sleep_for(std::chrono::milliseconds{2});
        }
    }
    EXPECT_GT(recv_result.count.load(std::memory_order_acquire), 0)
        << "Acceptor receive callback was not invoked within 4s";
    // Flag stores in the callback (relaxed) precede the release store on count,
    // and the acquire load on count above synchronizes-with that release → the
    // flag values are visible here without additional barrier.
    EXPECT_TRUE(recv_result.msg_type_d.load(std::memory_order_relaxed))
        << "Received message tag 35 (MsgType) != 'D'";
    EXPECT_TRUE(recv_result.clord_id_order001.load(std::memory_order_relaxed))
        << "Received message tag 11 (ClOrdID) != 'ORDER-001'";

    // ── Teardown (joins engine workers) ───────────────────────────────────────
    EXPECT_EQ(fixpp_session_close(ini_session), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_close(acc_session), FIXPP_ERR_OK);
    fixpp_engine_destroy(ini_engine);
    fixpp_engine_destroy(acc_engine);
}

}  // namespace
