// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/capi/toapp_callback_test.cpp — CA-009 toApp send-callback (US6, T019).
//
// Tests the fixpp_session_register_send_callback registration and the
// CapiApplication::toApp trampoline over a real plaintext-TCP loopback pair.
//
// Case (a): SEND verdict (0)  → message transmitted, fixpp_session_send returns OK.
// Case (b): VETO verdict (1)  → suppressed, fixpp_session_send returns
//           FIXPP_ERR_APP_DO_NOT_SEND. Session stays live.
// Case (c): ERROR verdict (2) → terminal-close, fixpp_session_send returns
//           FIXPP_ERR_APP_CALLBACK_THREW.
// Case (d): out-of-range verdict → same as ERROR → FIXPP_ERR_APP_CALLBACK_THREW.
// Case (e): framed-view readable — inside the callback the outbound msg is a
//           FRAMED fixpp_msg_t; a session framing tag (49=SenderCompID) IS readable
//           via fixpp_msg_get_string (distinct from the inbound-only accumulator
//           restriction). This exercises the "Framed toApp view" guarantee.
//
// Note: cases (c) and (d) each terminal-close the session.  Each gets a fresh
// engine pair.  Cases (a), (b), (e) run on the same pair — VETO does NOT close.
//
// consumer_minor=4 is REQUIRED: the [1400,1499] codes (APP_DO_NOT_SEND /
// APP_CALLBACK_THREW) are introducing-minor 4; at minor<4 they downgrade to
// FIXPP_ERR_UNKNOWN and the assertions fail.  See error_block_test.cpp T004.
//
// Anchors: contracts/toapp-callback.md D-8; spec.md SC-001/FR-024; tasks.md T019.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "fix/c_api/engine.h"
#include "fix/c_api/message.h"
#include "fix/c_api/session.h"

#include "capi_internal.hpp"      // fixpp_msg (check tag_/view directly)
#include "capi_loopback_support.hpp"

using namespace std::chrono_literals;
using namespace fixpp::capi_test;

namespace {

// Build an engine with consumer_minor=4 so the [1400,1499] codes are not
// downgraded to FIXPP_ERR_UNKNOWN.  (See T004 in error_block_test.cpp.)
fixpp_error_t make_engine_v4(fixpp_engine_t** out) {
    return fixpp_engine_create(make_engine_cfg(), 1, 4, out);
}

// Poll until `received` is true or the deadline elapses.
bool poll_until(std::atomic<bool>& flag, std::chrono::milliseconds ms = 2000ms) {
    const auto until = std::chrono::steady_clock::now() + ms;
    while (!flag.load(std::memory_order_acquire)) {
        if (std::chrono::steady_clock::now() >= until) return false;
        std::this_thread::sleep_for(5ms);
    }
    return true;
}

// ── Shared loopback pair ───────────────────────────────────────────────────
//
// One acceptor (B) + one initiator (A) per test, both with consumer_minor=4.
// Both sessions established before any verdict test runs.
struct LoopbackPair {
    fixpp_engine_t* B = nullptr;
    fixpp_engine_t* A = nullptr;
    fixpp_session_t* acc_h = nullptr;
    fixpp_session_t* ini_h = nullptr;
    fixpp::session::SessionId acc_id;

    // Open two established sessions with a custom toApp callback on the initiator.
    // Returns the bound port (0 on failure).
    std::uint16_t open(const char* acc_sender, const char* acc_target,
                       const char* ini_sender, const char* ini_target,
                       fixpp_send_cb send_cb, void* send_ud,
                       fixpp_recv_cb recv_cb_b = nullptr, void* recv_ud_b = nullptr) {
        if (make_engine_v4(&B) != FIXPP_ERR_OK) return 0;
        if (make_engine_v4(&A) != FIXPP_ERR_OK) return 0;

        fixpp_session_config_t* acc = make_session_cfg(acc_sender, acc_target, FIXPP_ROLE_ACCEPTOR);
        set_loopback_endpoint(acc, "127.0.0.1", 0);
        acc_id = session_id_of(acc);
        if (fixpp_session_open(B, acc, &acc_h) != FIXPP_ERR_OK) return 0;
        if (recv_cb_b) {
            if (fixpp_session_register_callback(acc_h, recv_cb_b, recv_ud_b) != FIXPP_ERR_OK)
                return 0;
        }
        if (fixpp_engine_start(B) != FIXPP_ERR_OK) return 0;
        std::uint16_t port = wait_for_bound_port(B, acc_id);
        if (port == 0) return 0;

        fixpp_session_config_t* ini = make_session_cfg(ini_sender, ini_target, FIXPP_ROLE_INITIATOR);
        set_loopback_endpoint(ini, "127.0.0.1", port);
        if (fixpp_session_open(A, ini, &ini_h) != FIXPP_ERR_OK) return 0;
        // Register the toApp callback on the initiator's session.
        if (fixpp_session_register_send_callback(ini_h, send_cb, send_ud) != FIXPP_ERR_OK)
            return 0;
        if (fixpp_engine_start(A) != FIXPP_ERR_OK) return 0;
        if (!wait_for_established(ini_h)) return 0;
        if (!wait_for_established(acc_h)) return 0;
        return port;
    }

    void close_all() {
        if (ini_h) { fixpp_session_close(ini_h); ini_h = nullptr; }
        if (acc_h) { fixpp_session_close(acc_h); acc_h = nullptr; }
        if (A) { fixpp_engine_destroy(A); A = nullptr; }
        if (B) { fixpp_engine_destroy(B); B = nullptr; }
    }

    ~LoopbackPair() { close_all(); }
};

}  // namespace

// ── Case (a): SEND verdict → message transmitted, send returns OK ────────────
TEST(ToappCallback, SendVerdictTransmits) {
    // Track whether B received the message (proves transmission happened).
    std::atomic<bool> b_received{false};
    auto b_cb = [](const fixpp_msg_t*, void* ud) {
        static_cast<std::atomic<bool>*>(ud)->store(true, std::memory_order_release);
    };

    // A callback that always returns SEND.
    static std::atomic<int> a_cb_count{0};
    auto a_send_cb = [](const fixpp_msg_t* /*outbound*/, void* /*ud*/) -> fixpp_toapp_verdict {
        a_cb_count.fetch_add(1, std::memory_order_relaxed);
        return FIXPP_TOAPP_SEND;
    };

    LoopbackPair pair;
    ASSERT_NE(pair.open("ACC-SEND", "INI-SEND", "INI-SEND", "ACC-SEND",
                        a_send_cb, nullptr, b_cb, &b_received),
              0u) << "loopback pair setup failed";

    const auto payload = make_app_payload("SENDIT");
    fixpp_error_t rc = fixpp_session_send(pair.ini_h, payload.data(), payload.size());
    EXPECT_EQ(rc, FIXPP_ERR_OK) << "SEND verdict should return FIXPP_ERR_OK";

    // B should receive the message.
    EXPECT_TRUE(poll_until(b_received, 3000ms))
        << "acceptor B did not receive the message (SEND verdict must transmit)";

    // The callback must have been called at least once.
    EXPECT_GT(a_cb_count.load(), 0) << "toApp callback was never invoked";

    pair.close_all();
}

// ── Case (b): VETO verdict → suppressed, send returns APP_DO_NOT_SEND ────────
TEST(ToappCallback, VetoVerdictSuppresses) {
    // B must NOT receive anything for the vetoed send.
    std::atomic<bool> b_received{false};
    auto b_cb = [](const fixpp_msg_t*, void* ud) {
        static_cast<std::atomic<bool>*>(ud)->store(true, std::memory_order_release);
    };

    auto a_send_cb = [](const fixpp_msg_t* /*outbound*/, void* /*ud*/) -> fixpp_toapp_verdict {
        return FIXPP_TOAPP_VETO;
    };

    LoopbackPair pair;
    ASSERT_NE(pair.open("ACC-VETO", "INI-VETO", "INI-VETO", "ACC-VETO",
                        a_send_cb, nullptr, b_cb, &b_received),
              0u) << "loopback pair setup failed";

    const auto payload = make_app_payload("VETOME");
    fixpp_error_t rc = fixpp_session_send(pair.ini_h, payload.data(), payload.size());
    // VETO → app_do_not_send → FIXPP_ERR_APP_DO_NOT_SEND (1402) at minor=4.
    EXPECT_EQ(rc, FIXPP_ERR_APP_DO_NOT_SEND)
        << "VETO verdict must return FIXPP_ERR_APP_DO_NOT_SEND (1402)";

    // B must NOT have received anything (the message was suppressed).
    std::this_thread::sleep_for(200ms);
    EXPECT_FALSE(b_received.load(std::memory_order_acquire))
        << "B received the message despite VETO verdict — suppression failed";

    // Session must still be alive (VETO does NOT terminal-close).
    bool est = false;
    EXPECT_EQ(fixpp_session_is_established(pair.ini_h, &est), FIXPP_ERR_OK);
    EXPECT_TRUE(est) << "VETO verdict must not close the session";

    pair.close_all();
}

// ── Case (c): ERROR verdict → terminal-close, send returns APP_CALLBACK_THREW ──
TEST(ToappCallback, ErrorVerdictTerminalClose) {
    auto a_send_cb = [](const fixpp_msg_t* /*outbound*/, void* /*ud*/) -> fixpp_toapp_verdict {
        return FIXPP_TOAPP_ERROR;
    };

    LoopbackPair pair;
    ASSERT_NE(pair.open("ACC-ERR", "INI-ERR", "INI-ERR", "ACC-ERR",
                        a_send_cb, nullptr),
              0u) << "loopback pair setup failed";

    const auto payload = make_app_payload("ERRIT");
    fixpp_error_t rc = fixpp_session_send(pair.ini_h, payload.data(), payload.size());
    // ERROR → app_callback_threw → FIXPP_ERR_APP_CALLBACK_THREW (1403) at minor=4.
    EXPECT_EQ(rc, FIXPP_ERR_APP_CALLBACK_THREW)
        << "ERROR verdict must return FIXPP_ERR_APP_CALLBACK_THREW (1403)";
    // pair.close_all() will gracefully close (which may fail since session was
    // terminal-closed — that is fine; destroy handles it).
    pair.close_all();
}

// ── Case (d): out-of-range verdict → treated as ERROR (defined misuse path) ───
TEST(ToappCallback, OutOfRangeVerdictTreatedAsError) {
    auto a_send_cb = [](const fixpp_msg_t* /*outbound*/, void* /*ud*/) -> fixpp_toapp_verdict {
        // 99 is out of the {0,1,2} closed enum range — defined C-ABI misuse.
        // The standard permits returning an out-of-range value from a C function
        // (no UB for the caller; UB would only arise for a switch with no default
        // on the callee side, which we avoid with the explicit default: arm).
        return static_cast<fixpp_toapp_verdict>(99);
    };

    LoopbackPair pair;
    ASSERT_NE(pair.open("ACC-OOR", "INI-OOR", "INI-OOR", "ACC-OOR",
                        a_send_cb, nullptr),
              0u) << "loopback pair setup failed";

    const auto payload = make_app_payload("OORIT");
    fixpp_error_t rc = fixpp_session_send(pair.ini_h, payload.data(), payload.size());
    // out-of-range → app_callback_threw → FIXPP_ERR_APP_CALLBACK_THREW (1403).
    EXPECT_EQ(rc, FIXPP_ERR_APP_CALLBACK_THREW)
        << "out-of-range verdict must return FIXPP_ERR_APP_CALLBACK_THREW (defined "
           "misuse path, not silently coerced to send)";
    pair.close_all();
}

// ── Case (e): framed view — framing tag (49=SenderCompID) IS readable ─────────
//
// The outbound fixpp_msg_t presented to the toApp callback is a FRAMED read-only
// view (FR-024: framing tags 8/9/34/49/52/56/10 are readable, unlike the
// accumulator which forbids them). Assert that tag 49 (SenderCompID) returns a
// non-empty string — the session stamps it on the outbound frame.
//
// All reads are captured inside the callback and asserted AFTER fixpp_session_send
// returns (the send is synchronous via .get() so ctx is race-free post-return).
TEST(ToappCallback, FramedViewFramingTagReadable) {
    struct Ctx {
        std::string sender_comp_id;  // from fixpp_msg_get_string(49)
        fixpp_error_t get_rc = FIXPP_ERR_UNKNOWN;
        bool cb_called = false;
    } ctx;

    auto a_send_cb = [](const fixpp_msg_t* outbound, void* ud) -> fixpp_toapp_verdict {
        auto* c = static_cast<Ctx*>(ud);
        c->cb_called = true;
        // Tag 49 = SenderCompID — a session framing tag.  Must be readable on the
        // FRAMED view (contracts/toapp-callback.md "Framed toApp view"; FR-024).
        const char* ptr = nullptr;
        size_t len = 0;
        c->get_rc = fixpp_msg_get_string(outbound, 49, &ptr, &len);
        if (c->get_rc == FIXPP_ERR_OK && ptr != nullptr) {
            c->sender_comp_id.assign(ptr, len);
        }
        return FIXPP_TOAPP_SEND;  // proceed — don't close the session
    };

    LoopbackPair pair;
    ASSERT_NE(pair.open("ACC-FRAMED", "INI-FRAMED", "INI-FRAMED", "ACC-FRAMED",
                        a_send_cb, &ctx),
              0u) << "loopback pair setup failed";

    const auto payload = make_app_payload("FRAMED");
    fixpp_error_t rc = fixpp_session_send(pair.ini_h, payload.data(), payload.size());
    EXPECT_EQ(rc, FIXPP_ERR_OK);

    // fixpp_session_send blocks until the send completes (co_spawn+.get()); by the
    // time it returns, the toApp callback has already run.
    EXPECT_TRUE(ctx.cb_called) << "toApp callback was not invoked";
    EXPECT_EQ(ctx.get_rc, FIXPP_ERR_OK)
        << "fixpp_msg_get_string(tag=49) on framed toApp view returned error "
        << ctx.get_rc << " (expected OK — framing tags are readable in toApp)";
    EXPECT_FALSE(ctx.sender_comp_id.empty())
        << "SenderCompID (tag 49) was empty in the framed toApp view";
    // The initiator's sender is "INI-FRAMED".
    EXPECT_EQ(ctx.sender_comp_id, "INI-FRAMED")
        << "SenderCompID in the framed toApp view should be INI-FRAMED";

    pair.close_all();
}

// ── Registration guards: post-start registration returns CONFIG_INVALID ──────
TEST(ToappCallback, PostStartRegistrationRejected) {
    fixpp_engine_t* A = nullptr;
    ASSERT_EQ(make_engine_v4(&A), FIXPP_ERR_OK);

    fixpp_session_config_t* ini = make_session_cfg("SND-GRD", "RCV-GRD", FIXPP_ROLE_INITIATOR);
    set_loopback_endpoint(ini, "127.0.0.1", 1);  // port 1 — won't connect, that's fine
    fixpp_session_t* ini_h = nullptr;
    ASSERT_EQ(fixpp_session_open(A, ini, &ini_h), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_engine_start(A), FIXPP_ERR_OK);

    auto dummy_cb = [](const fixpp_msg_t*, void*) -> fixpp_toapp_verdict {
        return FIXPP_TOAPP_SEND;
    };
    // Post-start → CAPI_CONFIG_INVALID.
    EXPECT_EQ(fixpp_session_register_send_callback(ini_h, dummy_cb, nullptr),
              FIXPP_ERR_CAPI_CONFIG_INVALID)
        << "post-start register_send_callback must return FIXPP_ERR_CAPI_CONFIG_INVALID";

    fixpp_engine_destroy(A);
}
