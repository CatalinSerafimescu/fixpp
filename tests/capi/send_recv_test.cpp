// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/capi/send_recv_test.cpp — 050 Feature B (CA-006/007), T028 + T029 + T030.
//
// HEADLINE round-trip (SC-001 + SC-008 + FR-013a): TWO C-ABI engines in one
// process — initiator A + acceptor B, each its OWN engine (own internal
// io_context + worker) — over a plaintext-TCP loopback socket via the L-050-5
// endpoint seam + the L-050-1 dict seam. A logs on to B; both poll
// is_established; A sends an app frame; B's on-strand callback receives it, copies
// out, and REPLIES FROM A DRAIN THREAD (the D-10 supported path — the reply
// crosses a real thread boundary; calling fixpp_session_send inside the callback
// would self-deadlock because send blocks on .get() while the single worker drives
// the io_context); A's callback receives the reply; both graceful-close; both
// destroy. The completing reply IS the proof (no watchdog — T030 / FR-013a).
//
// T029 (SC-008): a use-after-return on the inbound fixpp_msg_t retained past the
// callback is caught under ASan (negative test, guarded to ASan-only so debug
// stays green); and NO callbacks fire for a session after fixpp_session_close
// (FR-012, debug-testable).
//
// CRITICAL contract note: fixpp_session_send takes an APPLICATION PAYLOAD
// (lead-"35=", trailing SOH, no session tags), NOT a committed wire frame — the
// session stamps the header/trailer + seqnum itself. See make_app_payload() in
// capi_loopback_support.hpp (verified against Session::send_impl).
//
// Anchors: spec.md SC-001/SC-008 / FR-012/FR-013/FR-013a; tasks.md T028/T029/T030;
//          research D-10/D-11; contracts/send-and-receive.md.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

#include "fix/c_api/engine.h"
#include "fix/c_api/session.h"

#include "capi_internal.hpp"  // fixpp_msg (to read the view in the UAF negative test)
#include "capi_loopback_support.hpp"

using namespace std::chrono_literals;
using namespace fixpp::capi_test;

namespace {

// A single-shot drain channel: the on-strand callback drops bytes here and signals;
// a pre-spawned drain thread wakes, then calls fixpp_session_send on the OTHER side
// from OFF the callback strand (the D-10 supported reply path).
struct DrainReplier {
    std::mutex m;
    std::condition_variable cv;
    bool armed = false;          // a request to reply is pending
    bool shutdown = false;       // tell the thread to exit
    fixpp_session_t* reply_session = nullptr;  // who to send the reply on
    std::vector<std::uint8_t> reply_payload;
    std::atomic<bool> reply_sent{false};
    std::thread worker;

    void start() {
        worker = std::thread([this] {
            for (;;) {
                std::unique_lock<std::mutex> lk(m);
                cv.wait(lk, [this] { return armed || shutdown; });
                if (shutdown) return;
                armed = false;
                fixpp_session_t* s = reply_session;
                auto payload = reply_payload;
                lk.unlock();
                // OFF the callback strand → fixpp_session_send does NOT self-deadlock.
                fixpp_error_t rc = fixpp_session_send(s, payload.data(), payload.size());
                if (rc == FIXPP_ERR_OK) reply_sent.store(true, std::memory_order_release);
            }
        });
    }
    void arm(fixpp_session_t* s, std::vector<std::uint8_t> payload) {
        {
            std::lock_guard<std::mutex> lk(m);
            reply_session = s;
            reply_payload = std::move(payload);
            armed = true;
        }
        cv.notify_one();
    }
    void stop() {
        {
            std::lock_guard<std::mutex> lk(m);
            shutdown = true;
        }
        cv.notify_one();
        if (worker.joinable()) worker.join();
    }
};

}  // namespace

// ── T028 + T030 (SC-001 / FR-013a): two-C-ABI-engine round-trip, reply from drain
TEST(CapiSendRecv, TwoEngineRoundTripReplyFromDrainThread) {
    // Acceptor engine B + initiator engine A — each owns its own io_context+worker.
    fixpp_engine_t* B = nullptr;
    fixpp_engine_t* A = nullptr;
    ASSERT_EQ(fixpp_engine_create(make_engine_cfg(), 0, 0, &B), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_engine_create(make_engine_cfg(), 0, 0, &A), FIXPP_ERR_OK);

    // The acceptor B replies from a drain thread when it receives A's order.
    DrainReplier b_drain;
    b_drain.start();

    // A's callback signals that the reply round-tripped back.
    std::atomic<bool> a_got_reply{false};

    // ── Acceptor B session + on-strand receive callback ──────────────────────
    fixpp_session_config_t* acc =
        make_session_cfg("ACC-RT", "INIT-RT", FIXPP_ROLE_ACCEPTOR);
    set_loopback_endpoint(acc, "127.0.0.1", 0);
    auto acc_id = session_id_of(acc);
    fixpp_session_t* acc_h = nullptr;
    ASSERT_EQ(fixpp_session_open(B, acc, &acc_h), FIXPP_ERR_OK);

    // B's callback: copy out (we just acknowledge receipt) + arm the drain thread
    // to reply on B's session — the reply MUST be sent OFF this strand (D-10).
    struct BCtx {
        DrainReplier* drain;
        fixpp_session_t* session;  // B's session to reply on
        std::atomic<bool> got_order{false};
    } b_ctx{&b_drain, nullptr, {}};
    auto b_cb = [](const fixpp_msg_t* inbound, void* ud) {
        auto* ctx = static_cast<BCtx*>(ud);
        ASSERT_NE(inbound, nullptr);
        ctx->got_order.store(true, std::memory_order_release);
        // Reply payload: a different app message, sent from the drain thread.
        ctx->drain->arm(ctx->session, make_app_payload("REPLY01"));
    };
    ASSERT_EQ(fixpp_session_register_callback(acc_h, b_cb, &b_ctx), FIXPP_ERR_OK);

    ASSERT_EQ(fixpp_engine_start(B), FIXPP_ERR_OK);
    std::uint16_t port = wait_for_bound_port(B, acc_id);
    ASSERT_NE(port, 0u) << "acceptor did not bind";

    // ── Initiator A session + on-strand receive callback (sees B's reply) ─────
    fixpp_session_config_t* ini =
        make_session_cfg("INIT-RT", "ACC-RT", FIXPP_ROLE_INITIATOR);
    set_loopback_endpoint(ini, "127.0.0.1", port);
    fixpp_session_t* ini_h = nullptr;
    ASSERT_EQ(fixpp_session_open(A, ini, &ini_h), FIXPP_ERR_OK);

    auto a_cb = [](const fixpp_msg_t* inbound, void* ud) {
        ASSERT_NE(inbound, nullptr);
        static_cast<std::atomic<bool>*>(ud)->store(true, std::memory_order_release);
    };
    ASSERT_EQ(fixpp_session_register_callback(ini_h, a_cb, &a_got_reply), FIXPP_ERR_OK);

    // B's session handle is only valid after open; wire it into b_ctx now.
    b_ctx.session = acc_h;

    ASSERT_EQ(fixpp_engine_start(A), FIXPP_ERR_OK);
    ASSERT_TRUE(wait_for_established(ini_h)) << "initiator never established";
    ASSERT_TRUE(wait_for_established(acc_h)) << "acceptor never established";

    // A sends an application order to B (off any strand — A's worker drives A's ioc).
    const auto order = make_app_payload("ORDER001");
    ASSERT_EQ(fixpp_session_send(ini_h, order.data(), order.size()), FIXPP_ERR_OK);

    // The completing reply IS the proof (T030): poll for A receiving B's reply.
    const auto until = std::chrono::steady_clock::now() + 5s;
    while (!a_got_reply.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < until) {
        std::this_thread::sleep_for(5ms);
    }

    EXPECT_TRUE(b_ctx.got_order.load())
        << "acceptor B's on-strand callback never received A's order (SC-001)";
    EXPECT_TRUE(b_drain.reply_sent.load())
        << "drain-thread reply send did not complete (D-10 supported path)";
    EXPECT_TRUE(a_got_reply.load())
        << "initiator A never received B's reply — the round-trip did not close "
           "(SC-001 / FR-013a; the completing reply is the proof)";

    EXPECT_EQ(fixpp_session_close(ini_h), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_close(acc_h), FIXPP_ERR_OK);
    b_drain.stop();
    fixpp_engine_destroy(A);
    fixpp_engine_destroy(B);
}

// ── T029 (FR-012): NO callbacks fire for a session after fixpp_session_close ──
//
// Debug-testable half of SC-008. After close, the trampoline must not invoke the
// C callback for that session. We establish the pair, close A, then have B send;
// A's callback counter must NOT advance.
TEST(CapiSendRecv, NoCallbacksAfterClose) {
    fixpp_engine_t* B = nullptr;
    fixpp_engine_t* A = nullptr;
    ASSERT_EQ(fixpp_engine_create(make_engine_cfg(), 0, 0, &B), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_engine_create(make_engine_cfg(), 0, 0, &A), FIXPP_ERR_OK);

    fixpp_session_config_t* acc =
        make_session_cfg("ACC-NC", "INIT-NC", FIXPP_ROLE_ACCEPTOR);
    set_loopback_endpoint(acc, "127.0.0.1", 0);
    auto acc_id = session_id_of(acc);
    fixpp_session_t* acc_h = nullptr;
    ASSERT_EQ(fixpp_session_open(B, acc, &acc_h), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_engine_start(B), FIXPP_ERR_OK);
    std::uint16_t port = wait_for_bound_port(B, acc_id);
    ASSERT_NE(port, 0u);

    fixpp_session_config_t* ini =
        make_session_cfg("INIT-NC", "ACC-NC", FIXPP_ROLE_INITIATOR);
    set_loopback_endpoint(ini, "127.0.0.1", port);
    fixpp_session_t* ini_h = nullptr;
    ASSERT_EQ(fixpp_session_open(A, ini, &ini_h), FIXPP_ERR_OK);

    std::atomic<int> a_cb_count{0};
    auto a_cb = [](const fixpp_msg_t*, void* ud) {
        static_cast<std::atomic<int>*>(ud)->fetch_add(1, std::memory_order_release);
    };
    ASSERT_EQ(fixpp_session_register_callback(ini_h, a_cb, &a_cb_count), FIXPP_ERR_OK);

    ASSERT_EQ(fixpp_engine_start(A), FIXPP_ERR_OK);
    ASSERT_TRUE(wait_for_established(ini_h));
    ASSERT_TRUE(wait_for_established(acc_h));

    // Close A's session. After this, no callback may fire for A's session.
    ASSERT_EQ(fixpp_session_close(ini_h), FIXPP_ERR_OK);
    const int count_at_close = a_cb_count.load(std::memory_order_acquire);

    // B sends to A; with A's session closed/torn down, A's callback must not fire.
    const auto order = make_app_payload("POSTCLOSE");
    // The send may fail (peer gone) — that is fine; we only assert no callback runs.
    (void)fixpp_session_send(acc_h, order.data(), order.size());
    std::this_thread::sleep_for(200ms);

    EXPECT_EQ(a_cb_count.load(std::memory_order_acquire), count_at_close)
        << "a callback fired for A's session AFTER fixpp_session_close (FR-012)";

    fixpp_engine_destroy(A);
    fixpp_engine_destroy(B);
}

// ── T029 (SC-008): inbound fixpp_msg_t use-after-return is caught under ASan ──
//
// The inbound handle is valid ONLY inside the dispatch window ([2i §4.6]). A
// callback that RETAINS the fixpp_msg_t and dereferences its view after returning
// is UB; ASan catches the use-after-scope/use-after-return on the borrowed
// MessageView. Guarded ASan-only — under the plain debug preset the bad read is
// silent UB (would not fail), so the orchestrator's ASan matrix owns this witness.
//
// Clang's __has_feature() MUST be nested inside `#if defined(__has_feature)` — it
// cannot share an `#if` line with its `defined()` guard, because GCC's preprocessor
// still parses `__has_feature(` (an unknown identifier → 0 followed by `(`) and
// errors "missing binary operator before token (" regardless of `&&` short-circuit.
// GCC exposes ASan via __SANITIZE_ADDRESS__ instead. (Matches the portable idiom in
// tests/session/test_business_messages_build.cpp + tests/alloc_guard/.)
#if defined(__has_feature)
#  if __has_feature(address_sanitizer)
#    define FIXPP_CAPI_TEST_ASAN 1
#  endif
#elif defined(__SANITIZE_ADDRESS__)
#  define FIXPP_CAPI_TEST_ASAN 1
#endif

#ifdef FIXPP_CAPI_TEST_ASAN
TEST(CapiSendRecv, InboundHandleUseAfterReturnCaughtUnderAsan) {
    fixpp_engine_t* B = nullptr;
    fixpp_engine_t* A = nullptr;
    ASSERT_EQ(fixpp_engine_create(make_engine_cfg(), 0, 0, &B), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_engine_create(make_engine_cfg(), 0, 0, &A), FIXPP_ERR_OK);

    fixpp_session_config_t* acc =
        make_session_cfg("ACC-UAF", "INIT-UAF", FIXPP_ROLE_ACCEPTOR);
    set_loopback_endpoint(acc, "127.0.0.1", 0);
    auto acc_id = session_id_of(acc);
    fixpp_session_t* acc_h = nullptr;
    ASSERT_EQ(fixpp_session_open(B, acc, &acc_h), FIXPP_ERR_OK);

    // B's callback stashes the borrowed pointer and reads it AFTER returning.
    struct UafCtx {
        const fixpp_msg_t* retained = nullptr;
        std::atomic<bool> fired{false};
    } ctx;
    auto b_cb = [](const fixpp_msg_t* inbound, void* ud) {
        auto* c = static_cast<UafCtx*>(ud);
        c->retained = inbound;  // dangling once the dispatch window closes
        c->fired.store(true, std::memory_order_release);
    };
    ASSERT_EQ(fixpp_session_register_callback(acc_h, b_cb, &ctx), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_engine_start(B), FIXPP_ERR_OK);
    std::uint16_t port = wait_for_bound_port(B, acc_id);
    ASSERT_NE(port, 0u);

    fixpp_session_config_t* ini =
        make_session_cfg("INIT-UAF", "ACC-UAF", FIXPP_ROLE_INITIATOR);
    set_loopback_endpoint(ini, "127.0.0.1", port);
    fixpp_session_t* ini_h = nullptr;
    ASSERT_EQ(fixpp_session_open(A, ini, &ini_h), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_engine_start(A), FIXPP_ERR_OK);
    ASSERT_TRUE(wait_for_established(ini_h));

    const auto order = make_app_payload("UAFORDER");
    ASSERT_EQ(fixpp_session_send(ini_h, order.data(), order.size()), FIXPP_ERR_OK);

    const auto until = std::chrono::steady_clock::now() + 3s;
    while (!ctx.fired.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < until) {
        std::this_thread::sleep_for(5ms);
    }
    ASSERT_TRUE(ctx.fired.load());

    // The dispatch window has closed; reading through the retained handle is the
    // UAF ASan must catch. The borrowed MessageView (and the inbound fixpp_msg
    // stack wrapper) are out of scope on the worker strand; dereferencing
    // ctx.retained->view reads freed/expired storage → ASan use-after-scope abort.
    ASSERT_NE(ctx.retained, nullptr);
    const auto* view = reinterpret_cast<const fixpp_msg*>(ctx.retained)->view;
    volatile std::size_t sink = view->bytes().size();
    (void)sink;
    ADD_FAILURE() << "the use-after-return read did NOT trip ASan — the inbound "
                     "fixpp_msg_t dispatch-window lifetime is not enforced (SC-008)";

    fixpp_engine_destroy(A);
    fixpp_engine_destroy(B);
}
#endif  // FIXPP_CAPI_TEST_ASAN
