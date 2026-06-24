// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/capi/lifecycle_test.cpp — 050 Feature B (CA-005), T015 + T016.
//
// US1 — engine + session lifecycle from C. The happy path drives a REAL
// established session over the plaintext loopback seam (L-050-1 dict + L-050-5
// endpoint), then exercises the lifecycle contract surface:
//   - never-started destroy does NOT abort (FR-003);
//   - double-destroy + NULL-destroy are no-ops;
//   - session_open after engine_start → FIXPP_ERR_CAPI_CONFIG_INVALID (FR-004);
//   - register_callback after engine_start → FIXPP_ERR_CAPI_CONFIG_INVALID (FR-011);
//   - NULL / dead handle → FIXPP_ERR_NULL_HANDLE / FIXPP_ERR_INVALID_HANDLE (FR-006).
//
// T016 (SC-007):
//   (a) park an established idle session in async_read, fixpp_session_close, assert
//       PROMPT teardown by socket-close/cancellation (returns OK well within the
//       idle-read window — the distinguishing witness is "returns fast", not
//       "a deadline elapsed"); multi-threaded (worker drives the read).
//   (b) RESCOPED + L-050-y: the original LIVE in-flight-send-during-engine_destroy
//       cancel stimulus is a use-after-free the C-ABI quiesce-before-destroy
//       contract forbids, so it is not safely expressible; this witnesses the safe
//       sequential teardown observable (send after close → FIXPP_ERR_INVALID_HANDLE,
//       never abort/UB). The FR-010 CANCELLED mapping is witnessed by the error oracle.
//
// Anchors: spec.md US1 / SC-007 / FR-003/004/005/006/010/011; tasks.md T015/T016;
//          contracts/{lifecycle-surface,send-and-receive}.md; data-model E-6.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

#include "fix/c_api/engine.h"
#include "fix/c_api/session.h"

#include "capi_loopback_support.hpp"

using namespace std::chrono_literals;
using namespace fixpp::capi_test;

// ── T015: never-started destroy does NOT abort (FR-003) ──────────────────────
//
// Create an engine, register a session, NEVER call engine_start, then destroy.
// ~Engine asserts stopped(); fixpp_engine_destroy must drive stop() unconditionally
// so the never-started engine tears down cleanly without std::abort. If destroy
// skipped stop() on a never-started engine, ~Engine would abort here (RED).
TEST(CapiLifecycle, NeverStartedDestroyDoesNotAbort) {
    fixpp_engine_t* eng = nullptr;
    ASSERT_EQ(fixpp_engine_create(make_engine_cfg(), 0, 0, &eng), FIXPP_ERR_OK);
    ASSERT_NE(eng, nullptr);

    fixpp_session_config_t* sc =
        make_session_cfg("INIT-NS", "ACC-NS", FIXPP_ROLE_INITIATOR);
    fixpp_session_t* sess = nullptr;
    ASSERT_EQ(fixpp_session_open(eng, sc, &sess), FIXPP_ERR_OK);
    ASSERT_NE(sess, nullptr);

    // Never started. Destroy must not abort (this line returning is the witness).
    fixpp_engine_destroy(eng);
    SUCCEED() << "never-started destroy returned without aborting (FR-003)";
}

// ── T015: double-destroy of the SAME pointer is a no-op (not UAF) ────────────
//
// [2i §4.2.1] / contracts/lifecycle-surface.md:38: "NULL / double-destroy →
// no-op". The first destroy tombstones the shell (tag_=DEAD) and DOES NOT free
// it; the second destroy reads the DEAD tag and returns immediately with no
// UAF/double-free. Without the tombstone, the second call would dereference freed
// storage → detectable as UAF under ASan. Mutation-test: revert the tag_ write in
// fixpp_engine_destroy (replace tombstone with delete) → this test goes RED under
// ASan. NULL-destroy is also covered for completeness.
TEST(CapiLifecycle, DestroyIsIdempotentSamePointer) {
    fixpp_engine_destroy(nullptr);  // NULL → no-op, no crash.

    fixpp_engine_t* eng = nullptr;
    ASSERT_EQ(fixpp_engine_create(make_engine_cfg(), 0, 0, &eng), FIXPP_ERR_OK);
    ASSERT_NE(eng, nullptr);
    // First destroy: quiesces the engine, tombstones the shell (tag_=DEAD).
    fixpp_engine_destroy(eng);
    // Second destroy on the same pointer: must read DEAD tag and no-op — no UAF,
    // no double-free. Under ASan this is the deterministic discriminator.
    fixpp_engine_destroy(eng);
    SUCCEED() << "same-pointer double-destroy returned without UAF/double-free";
}

// ── T015: post-engine-destroy session handle → FIXPP_ERR_INVALID_HANDLE ───────
//
// [2i §4.2.1] / contracts/lifecycle-surface.md:43: "dead/corrupted handle →
// FIXPP_ERR_INVALID_HANDLE". After fixpp_engine_destroy (WITHOUT closing the
// session first), any call on a stale session handle must return INVALID_HANDLE,
// not UAF/UB. The fix: engine_->engine_.reset() before tombstoning, so
// check_session sees !engine_.has_value() → FIXPP_ERR_INVALID_HANDLE instead of
// dereferencing freed internals. Mutation-test: remove engine->engine_.reset()
// from fixpp_engine_destroy → this test goes RED under ASan (the session call
// reaches freed engine internals).
TEST(CapiLifecycle, PostEngineDestroySessionHandleIsInvalidHandle) {
    fixpp_engine_t* eng = nullptr;
    ASSERT_EQ(fixpp_engine_create(make_engine_cfg(), 0, 0, &eng), FIXPP_ERR_OK);

    fixpp_session_config_t* sc =
        make_session_cfg("INIT-PDQ", "ACC-PDQ", FIXPP_ROLE_INITIATOR);
    fixpp_session_t* sess = nullptr;
    ASSERT_EQ(fixpp_session_open(eng, sc, &sess), FIXPP_ERR_OK);
    ASSERT_NE(sess, nullptr);

    // Destroy the engine WITHOUT closing the session. The session handle is now
    // stale — its owning engine is dead.
    fixpp_engine_destroy(eng);

    // Any non-destroy call on the stale session handle must return INVALID_HANDLE,
    // not UAF/crash/UB (under ASan this is the deterministic discriminator).
    bool est = true;
    EXPECT_EQ(fixpp_session_is_established(sess, &est), FIXPP_ERR_INVALID_HANDLE)
        << "stale session handle after engine destroy must return INVALID_HANDLE (FR-006)";
    EXPECT_EQ(fixpp_session_close(sess), FIXPP_ERR_INVALID_HANDLE)
        << "close on stale session handle must return INVALID_HANDLE (FR-006)";
}

// ── T015: session_open AFTER engine_start → CAPI_CONFIG_INVALID (FR-004) ──────
TEST(CapiLifecycle, SessionOpenAfterStartRejected) {
    fixpp_engine_t* eng = nullptr;
    ASSERT_EQ(fixpp_engine_create(make_engine_cfg(), 0, 0, &eng), FIXPP_ERR_OK);

    // One acceptor so start() has something to bind; port 0 = OS-assigned.
    fixpp_session_config_t* acc =
        make_session_cfg("ACC-FR4", "INIT-FR4", FIXPP_ROLE_ACCEPTOR);
    set_loopback_endpoint(acc, "127.0.0.1", 0);
    fixpp_session_t* acc_h = nullptr;
    ASSERT_EQ(fixpp_session_open(eng, acc, &acc_h), FIXPP_ERR_OK);

    ASSERT_EQ(fixpp_engine_start(eng), FIXPP_ERR_OK);

    // Now a post-start open must be rejected (the registry is read on session
    // strands without a mutex; a late register would race).
    fixpp_session_config_t* late =
        make_session_cfg("INIT-LATE", "ACC-LATE", FIXPP_ROLE_INITIATOR);
    fixpp_session_t* late_h = nullptr;
    EXPECT_EQ(fixpp_session_open(eng, late, &late_h), FIXPP_ERR_CAPI_CONFIG_INVALID);
    EXPECT_EQ(late_h, nullptr);
    fixpp_session_config_destroy(late);  // NOT consumed on failure — caller frees.

    // Second engine_start → domain error (start is once).
    EXPECT_NE(fixpp_engine_start(eng), FIXPP_ERR_OK);

    fixpp_engine_destroy(eng);
}

// ── T015: register_callback AFTER engine_start → CAPI_CONFIG_INVALID (FR-011) ─
TEST(CapiLifecycle, RegisterCallbackAfterStartRejected) {
    fixpp_engine_t* eng = nullptr;
    ASSERT_EQ(fixpp_engine_create(make_engine_cfg(), 0, 0, &eng), FIXPP_ERR_OK);

    fixpp_session_config_t* acc =
        make_session_cfg("ACC-FR11", "INIT-FR11", FIXPP_ROLE_ACCEPTOR);
    set_loopback_endpoint(acc, "127.0.0.1", 0);
    fixpp_session_t* acc_h = nullptr;
    ASSERT_EQ(fixpp_session_open(eng, acc, &acc_h), FIXPP_ERR_OK);
    ASSERT_NE(acc_h, nullptr);

    ASSERT_EQ(fixpp_engine_start(eng), FIXPP_ERR_OK);

    auto cb = [](const fixpp_msg_t*, void*) {};
    EXPECT_EQ(fixpp_session_register_callback(acc_h, cb, nullptr),
              FIXPP_ERR_CAPI_CONFIG_INVALID);

    fixpp_engine_destroy(eng);
}

// ── T015: NULL / dead handle → NULL_HANDLE / INVALID_HANDLE (FR-006) ──────────
TEST(CapiLifecycle, NullAndDeadHandleCodes) {
    // NULL session handle.
    bool est = true;
    EXPECT_EQ(fixpp_session_is_established(nullptr, &est), FIXPP_ERR_NULL_HANDLE);
    EXPECT_EQ(fixpp_session_close(nullptr), FIXPP_ERR_NULL_HANDLE);
    EXPECT_EQ(fixpp_session_send(nullptr, reinterpret_cast<const uint8_t*>("x"), 1),
              FIXPP_ERR_NULL_HANDLE);

    // NULL engine handle.
    fixpp_session_t* s = nullptr;
    fixpp_session_config_t* sc =
        make_session_cfg("INIT-NH", "ACC-NH", FIXPP_ROLE_INITIATOR);
    EXPECT_EQ(fixpp_session_open(nullptr, sc, &s), FIXPP_ERR_NULL_HANDLE);
    fixpp_session_config_destroy(sc);  // not consumed on failure
    EXPECT_EQ(fixpp_engine_start(nullptr), FIXPP_ERR_NULL_HANDLE);

    // Dead handle: close a session (invalidates handle.valid), then re-use it.
    // We need an established session to close cleanly; build the loopback pair.
    fixpp_engine_t* B = nullptr;  // acceptor
    fixpp_engine_t* A = nullptr;  // initiator
    ASSERT_EQ(fixpp_engine_create(make_engine_cfg(), 0, 0, &B), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_engine_create(make_engine_cfg(), 0, 0, &A), FIXPP_ERR_OK);

    fixpp_session_config_t* acc =
        make_session_cfg("ACC-DEAD", "INIT-DEAD", FIXPP_ROLE_ACCEPTOR);
    set_loopback_endpoint(acc, "127.0.0.1", 0);
    auto acc_id = session_id_of(acc);
    fixpp_session_t* acc_h = nullptr;
    ASSERT_EQ(fixpp_session_open(B, acc, &acc_h), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_engine_start(B), FIXPP_ERR_OK);

    std::uint16_t port = wait_for_bound_port(B, acc_id);
    ASSERT_NE(port, 0u) << "acceptor did not bind";

    fixpp_session_config_t* ini =
        make_session_cfg("INIT-DEAD", "ACC-DEAD", FIXPP_ROLE_INITIATOR);
    set_loopback_endpoint(ini, "127.0.0.1", port);
    fixpp_session_t* ini_h = nullptr;
    ASSERT_EQ(fixpp_session_open(A, ini, &ini_h), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_engine_start(A), FIXPP_ERR_OK);

    ASSERT_TRUE(wait_for_established(ini_h)) << "initiator never established";

    // Close invalidates the handle (handle.valid=false, no separate destroy).
    EXPECT_EQ(fixpp_session_close(ini_h), FIXPP_ERR_OK);
    // Re-using the now-dead handle → INVALID_HANDLE.
    EXPECT_EQ(fixpp_session_is_established(ini_h, &est), FIXPP_ERR_INVALID_HANDLE);
    EXPECT_EQ(fixpp_session_close(ini_h), FIXPP_ERR_INVALID_HANDLE);

    fixpp_engine_destroy(A);
    fixpp_engine_destroy(B);
}

// ── T015: full happy path — create→open→start→established→close→destroy ───────
TEST(CapiLifecycle, EstablishedSessionHappyPath) {
    fixpp_engine_t* B = nullptr;  // acceptor engine
    fixpp_engine_t* A = nullptr;  // initiator engine
    ASSERT_EQ(fixpp_engine_create(make_engine_cfg(), 0, 0, &B), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_engine_create(make_engine_cfg(), 0, 0, &A), FIXPP_ERR_OK);

    fixpp_session_config_t* acc =
        make_session_cfg("ACC-HP", "INIT-HP", FIXPP_ROLE_ACCEPTOR);
    set_loopback_endpoint(acc, "127.0.0.1", 0);
    auto acc_id = session_id_of(acc);
    fixpp_session_t* acc_h = nullptr;
    ASSERT_EQ(fixpp_session_open(B, acc, &acc_h), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_engine_start(B), FIXPP_ERR_OK);

    std::uint16_t port = wait_for_bound_port(B, acc_id);
    ASSERT_NE(port, 0u) << "acceptor did not bind";

    fixpp_session_config_t* ini =
        make_session_cfg("INIT-HP", "ACC-HP", FIXPP_ROLE_INITIATOR);
    set_loopback_endpoint(ini, "127.0.0.1", port);
    fixpp_session_t* ini_h = nullptr;
    ASSERT_EQ(fixpp_session_open(A, ini, &ini_h), FIXPP_ERR_OK);

    // open != connected: not established before start.
    bool est = true;
    ASSERT_EQ(fixpp_session_is_established(ini_h, &est), FIXPP_ERR_OK);
    EXPECT_FALSE(est) << "session must NOT be established before start (open != connected)";

    ASSERT_EQ(fixpp_engine_start(A), FIXPP_ERR_OK);
    ASSERT_TRUE(wait_for_established(ini_h)) << "initiator never established";
    EXPECT_TRUE(wait_for_established(acc_h)) << "acceptor never established";

    EXPECT_EQ(fixpp_session_close(ini_h), FIXPP_ERR_OK);
    fixpp_engine_destroy(A);
    fixpp_engine_destroy(B);
}

// ── T016 (SC-007a): close breaks a blocked idle read PROMPTLY ─────────────────
//
// An established idle session is parked in async_read (no traffic). Close it and
// assert the close returns FAST (well within the idle-read horizon) — the witness
// is promptness, NOT a deadline elapsing. ASIO native cancellation closes the
// transport so the blocked read breaks immediately. If close waited on the idle
// read instead of cancelling it, close would block until the read naturally
// completed (it never would on an idle socket) and this measurement would exceed
// the bound. Multi-threaded (worker drives the read; close posts cross-thread).
TEST(CapiLifecycle, Sc007CloseBreaksBlockedIdleReadPromptly) {
    fixpp_engine_t* B = nullptr;
    fixpp_engine_t* A = nullptr;
    ASSERT_EQ(fixpp_engine_create(make_engine_cfg(), 0, 0, &B), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_engine_create(make_engine_cfg(), 0, 0, &A), FIXPP_ERR_OK);

    fixpp_session_config_t* acc =
        make_session_cfg("ACC-S7", "INIT-S7", FIXPP_ROLE_ACCEPTOR);
    set_loopback_endpoint(acc, "127.0.0.1", 0);
    auto acc_id = session_id_of(acc);
    fixpp_session_t* acc_h = nullptr;
    ASSERT_EQ(fixpp_session_open(B, acc, &acc_h), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_engine_start(B), FIXPP_ERR_OK);

    std::uint16_t port = wait_for_bound_port(B, acc_id);
    ASSERT_NE(port, 0u);

    fixpp_session_config_t* ini =
        make_session_cfg("INIT-S7", "ACC-S7", FIXPP_ROLE_INITIATOR);
    set_loopback_endpoint(ini, "127.0.0.1", port);
    fixpp_session_t* ini_h = nullptr;
    ASSERT_EQ(fixpp_session_open(A, ini, &ini_h), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_engine_start(A), FIXPP_ERR_OK);
    ASSERT_TRUE(wait_for_established(ini_h));

    // The session is now idle in async_read. Close it and time the call.
    const auto t0 = std::chrono::steady_clock::now();
    const fixpp_error_t rc = fixpp_session_close(ini_h);
    const auto elapsed = std::chrono::steady_clock::now() - t0;

    EXPECT_EQ(rc, FIXPP_ERR_OK);
    // Prompt-teardown witness: a cancellation-driven close returns in well under
    // 1s on loopback. The idle read would otherwise never complete; if close did
    // not actively cancel/close the transport, this would block far longer.
    EXPECT_LT(elapsed, 1s)
        << "close did not break the blocked idle read promptly (SC-007a) — "
           "elapsed="
        << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() << "ms";

    fixpp_engine_destroy(A);
    fixpp_engine_destroy(B);
}

// ── T016 (SC-007b, RESCOPED): send after teardown returns a clean terminal code
//
// RESCOPE + L-050-y. The original SC-007b drove a LIVE cross-thread cancel: a
// worker thread hammering fixpp_session_send on session A while the main thread
// called fixpp_engine_destroy(A), expecting an in-flight send to observe
// Engine::stop()'s total-cancel and return FIXPP_ERR_CANCELLED (FR-010). That
// stimulus is a DATA RACE that the public C-ABI contract forbids:
// fixpp_engine_destroy frees the fixpp_engine and its fixpp_session storage, while
// the racing fixpp_session_send dereferences session->engine — a genuine
// use-after-free (ASan-confirmed during 050 test work), NOT a bridge bug. The
// C-ABI destroy contract is SINGLE_THREAD / quiesce-before-destroy (capi_internal.hpp
// fixpp_engine teardown note): the caller MUST stop issuing session ops before
// destroying the owning engine. So the in-flight-send-during-teardown→CANCELLED
// LIVE race is NOT safely expressible through the public C-ABI = L-050-y.
//
// The FR-010 cancel-coalescing MAPPING (operation_cancelled / connection_aborted /
// … → the uniform FIXPP_ERR_CANCELLED) is witnessed at the translate() oracle by
// error_block_test / error_surface_test, not here.
//
// What IS safe and C-ABI-expressible — and what this rescoped test witnesses — is
// the SEQUENTIAL teardown observable: after a session is torn down (close), a
// subsequent fixpp_session_send on the now-dead handle returns a clean terminal
// code (FIXPP_ERR_INVALID_HANDLE) on the caller thread — never aborts, never UB.
TEST(CapiLifecycle, Sc007SendAfterTeardownIsTerminalNotUb) {
    fixpp_engine_t* B = nullptr;
    fixpp_engine_t* A = nullptr;
    ASSERT_EQ(fixpp_engine_create(make_engine_cfg(), 0, 0, &B), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_engine_create(make_engine_cfg(), 0, 0, &A), FIXPP_ERR_OK);

    fixpp_session_config_t* acc =
        make_session_cfg("ACC-S7B", "INIT-S7B", FIXPP_ROLE_ACCEPTOR);
    set_loopback_endpoint(acc, "127.0.0.1", 0);
    auto acc_id = session_id_of(acc);
    fixpp_session_t* acc_h = nullptr;
    ASSERT_EQ(fixpp_session_open(B, acc, &acc_h), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_engine_start(B), FIXPP_ERR_OK);

    std::uint16_t port = wait_for_bound_port(B, acc_id);
    ASSERT_NE(port, 0u);

    fixpp_session_config_t* ini =
        make_session_cfg("INIT-S7B", "ACC-S7B", FIXPP_ROLE_INITIATOR);
    set_loopback_endpoint(ini, "127.0.0.1", port);
    fixpp_session_t* ini_h = nullptr;
    ASSERT_EQ(fixpp_session_open(A, ini, &ini_h), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_engine_start(A), FIXPP_ERR_OK);
    ASSERT_TRUE(wait_for_established(ini_h));

    const auto payload = make_app_payload("CANCELME");

    // A successful send while established (sanity: the bridge is live).
    EXPECT_EQ(fixpp_session_send(ini_h, payload.data(), payload.size()), FIXPP_ERR_OK);

    // Tear the session down (single-thread, quiesced — no concurrent send).
    EXPECT_EQ(fixpp_session_close(ini_h), FIXPP_ERR_OK);

    // A send on the now-dead handle returns the clean terminal code on THIS thread
    // — no abort, no UB (the safe, C-ABI-expressible core of the teardown contract).
    EXPECT_EQ(fixpp_session_send(ini_h, payload.data(), payload.size()),
              FIXPP_ERR_INVALID_HANDLE)
        << "send after teardown must return a clean terminal code (SC-007b rescoped; "
           "the live in-flight-cancel race is L-050-y)";

    fixpp_engine_destroy(A);
    fixpp_engine_destroy(B);
}

// ── Q2: concurrent fixpp_session_send vs fixpp_session_close (TSan witness) ───
//
// fixpp_session_send is THREAD_SAFE; the design explicitly models send racing
// a concurrent close as a defined lifecycle outcome (data-model.md:80 / E-6 /
// contracts/send-and-receive.md:25: "send raced a concurrent close() drain →
// session_already_closed → FIXPP_ERR_THREAD_SESSION_LIFECYCLE"). Without
// `std::atomic<bool> valid` (Q2 fix), the read in check_session (valid load)
// and the write in fixpp_session_close (valid store) form a C++ data race →
// undefined behavior under TSan. Mutation-test: revert valid to `bool` in
// capi_internal.hpp → this test reports a TSan data race.
//
// Allowed terminal codes for the sender thread: FIXPP_ERR_OK (send completed
// before close drained), FIXPP_ERR_THREAD_SESSION_LIFECYCLE (send vs close
// TOCTOU), FIXPP_ERR_CANCELLED (send cancelled by stop), or
// FIXPP_ERR_INVALID_HANDLE (send saw the closed flag after close published it).
// FIXPP_ERR_UNKNOWN is also possible (session_invalid_state_for_send arm, L-050-4).
TEST(CapiLifecycle, ConcurrentSendAndCloseNoDataRace) {
    fixpp_engine_t* B = nullptr;
    fixpp_engine_t* A = nullptr;
    ASSERT_EQ(fixpp_engine_create(make_engine_cfg(), 0, 0, &B), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_engine_create(make_engine_cfg(), 0, 0, &A), FIXPP_ERR_OK);

    fixpp_session_config_t* acc =
        make_session_cfg("ACC-RACE", "INIT-RACE", FIXPP_ROLE_ACCEPTOR);
    set_loopback_endpoint(acc, "127.0.0.1", 0);
    auto acc_id = session_id_of(acc);
    fixpp_session_t* acc_h = nullptr;
    ASSERT_EQ(fixpp_session_open(B, acc, &acc_h), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_engine_start(B), FIXPP_ERR_OK);

    std::uint16_t port = wait_for_bound_port(B, acc_id);
    ASSERT_NE(port, 0u) << "acceptor did not bind";

    fixpp_session_config_t* ini =
        make_session_cfg("INIT-RACE", "ACC-RACE", FIXPP_ROLE_INITIATOR);
    set_loopback_endpoint(ini, "127.0.0.1", port);
    fixpp_session_t* ini_h = nullptr;
    ASSERT_EQ(fixpp_session_open(A, ini, &ini_h), FIXPP_ERR_OK);
    ASSERT_EQ(fixpp_engine_start(A), FIXPP_ERR_OK);

    // Wait for establishment so the send path is live.
    ASSERT_TRUE(wait_for_established(ini_h)) << "initiator never established";

    const auto payload = make_app_payload("RACETEST");

    // A separate thread hammers fixpp_session_send while the main thread calls
    // fixpp_session_close on the SAME session. Under TSan this detects the data
    // race on `valid` when it is a plain `bool`; with `std::atomic<bool>` there
    // is no race.
    std::atomic<bool> sender_done{false};
    std::atomic<fixpp_error_t> last_send_code{FIXPP_ERR_OK};

    std::thread sender([&] {
        while (!sender_done.load(std::memory_order_acquire)) {
            fixpp_error_t rc = fixpp_session_send(ini_h, payload.data(), payload.size());
            last_send_code.store(rc, std::memory_order_relaxed);
            // Once close is done (valid=false), the send loop should also observe
            // INVALID_HANDLE and we can stop racing.
            if (rc == FIXPP_ERR_INVALID_HANDLE) {
                break;
            }
        }
        sender_done.store(true, std::memory_order_release);
    });

    // Give the sender a moment to get into the steady-state send loop, then close.
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
    fixpp_error_t close_rc = fixpp_session_close(ini_h);
    // Signal the sender that close is done (it may already have exited on
    // INVALID_HANDLE, which is fine).
    sender_done.store(true, std::memory_order_release);
    sender.join();

    // close() returns OK (graceful) or a lifecycle code (already-closed); both
    // are acceptable — the THREAD_SESSION_LIFECYCLE path is the race-observed path.
    EXPECT_TRUE(close_rc == FIXPP_ERR_OK || close_rc == FIXPP_ERR_THREAD_SESSION_LIFECYCLE)
        << "unexpected close code: " << close_rc;

    // The sender thread must have seen one of the defined terminal codes —
    // NEVER a crash/abort/TSan report (which indicates the atomic fix works).
    fixpp_error_t final_send = last_send_code.load(std::memory_order_acquire);
    EXPECT_TRUE(final_send == FIXPP_ERR_OK || final_send == FIXPP_ERR_THREAD_SESSION_LIFECYCLE ||
                final_send == FIXPP_ERR_CANCELLED || final_send == FIXPP_ERR_INVALID_HANDLE ||
                final_send == FIXPP_ERR_UNKNOWN)
        << "send racing close returned an unexpected code: " << final_send;

    fixpp_engine_destroy(A);
    fixpp_engine_destroy(B);
}
