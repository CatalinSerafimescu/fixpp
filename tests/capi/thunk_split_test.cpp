// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/capi/thunk_split_test.cpp — 050 Feature B (CA-005/006), T024 / SC-006.
//
// The [2i §5.2] thunk split: CONSTRUCTION-TIME thunks (engine_create / session_open
// / engine_start + the config-builder family) catch escaping C++ exceptions and
// return FIXPP_ERR_CAPI_CONFIG_INVALID with NO abort; the STEADY-STATE thunk
// (fixpp_session_send) treats an escaping exception as an invariant violation and
// std::abort()s (it is NOT translated to a code — FR-008/FR-019).
//
// In-process abort trap (NOT EXPECT_DEATH): a SIGABRT handler + (sig)setjmp /
// (sig)longjmp — sigaction + sigsetjmp/siglongjmp on POSIX, signal(SIGABRT) +
// setjmp/longjmp on Windows (MSVC has no sig*jmp family). EXPECT_DEATH forks, and
// forking a process holding a live C-ABI engine + worker threads is exactly the
// poisoned L-050-2 path; the in-process trap avoids fork entirely. The send
// thunk's abort fires on the CALLER's thread (fut.get() rethrows synchronously
// into the catch(...) at session.cpp), so a same-thread (sig)longjmp back to a
// (sig)setjmp on this thread is the correct catch on both platforms.
//
// ── Steady-state-throw witness via the FIXPP_TEST_HOOKS send-throw seam ───────
// The positive steady-state witness — an escaping exception INTO fixpp_session_send's
// catch(...) → SIGABRT — is not INPUT-triggerable: Session::send
// (src/session/session.cpp:4023) is `noexcept` and converts ANY component throw
// (store, transport, async_mutex cancel, …) into a `dispatch_aborted` expected_t,
// so nothing input-driven reaches the C-ABI catch(...). The fromApp callback-throw
// abort (engine.cpp) is NOT a substitute: it fires on a WORKER thread, and
// siglongjmp cannot cross threads to a sigsetjmp established on main.
//
// So a minimal FIXPP_TEST_HOOKS seam (fixpp_capi::detail::set_send_throw_hook,
// mirroring the engine.hpp test_hook_* idiom) arms a throw INSIDE fixpp_session_send's
// try block on the CALLER's thread — exactly where a real escaping send exception
// would surface — driving the production catch(...)→abort. Production builds carry
// the seam declaration gated off (one bool load otherwise) so it is unreachable.
// This TU witnesses all three SC-006 arms:
//   (1) the abort trap MECHANISM catches a std::abort() on this thread,
//   (2) the construction-time thunks return CAPI_CONFIG_INVALID + DO NOT abort, and
//   (3) the steady-state thunk DOES abort when an exception escapes the send body.
//
// Anchors: spec.md SC-006 / FR-008 / FR-019; tasks.md T024;
//          contracts/send-and-receive.md "thunk split"; [2i §5.2].

#include <gtest/gtest.h>

#include <csetjmp>
#include <csignal>
#include <cstdint>
#include <vector>

#ifdef _WIN32
// A SIGABRT handler is installed below and longjmps out BEFORE abort() can
// terminate, so the CRT message box is never reached — but include the
// dialog-suppressor anyway (belt-and-suspenders: a headless modal abort dialog
// would HANG the test forever). MSVC also warns C4611 on the documented
// setjmp/longjmp idiom (interaction with C++ object destruction); the trap lands
// BACK in the same frame as the setjmp, so no live object is bypassed.
#include "support/win_crt_no_dialog.hpp"
#pragma warning(disable : 4611)
#endif

#include "fix/c_api/engine.h"
#include "fix/c_api/session.h"

#include "capi_internal.hpp"        // fixpp_capi::detail::set_send_throw_hook (FIXPP_TEST_HOOKS)
#include "capi_loopback_support.hpp"  // make_test_dict_handle (L-050-1 dict seam)

using namespace fixpp::capi_test;

namespace {

// ── In-process SIGABRT trap ───────────────────────────────────────────────────
// POSIX: sigaction(SIGABRT) + sigsetjmp/siglongjmp.  Windows: signal(SIGABRT) +
// setjmp/longjmp — MSVC has neither <sys/...> sigaction nor the sig*jmp family,
// but the steady-state abort fires SYNCHRONOUSLY on the caller's thread, so a
// same-thread (sig)longjmp back to the (sig)setjmp established on this thread is
// the correct catch on both platforms. Mechanism differs; both branches run.
#ifdef _WIN32
using abort_jmp_buf_t = std::jmp_buf;
#define FIXPP_ABORT_SETJMP(buf) setjmp(buf)
#else
using abort_jmp_buf_t = sigjmp_buf;
#define FIXPP_ABORT_SETJMP(buf) sigsetjmp((buf), 1)
#endif

abort_jmp_buf_t g_abort_jmp;
volatile sig_atomic_t g_abort_caught = 0;

extern "C" void abort_trap_handler(int /*sig*/) {
    g_abort_caught = 1;
    // unwind back to the (sig)setjmp established on this thread
#ifdef _WIN32
    std::longjmp(g_abort_jmp, 1);
#else
    siglongjmp(g_abort_jmp, 1);
#endif
}

struct ScopedAbortTrap {
#ifdef _WIN32
    void (*old_handler_)(int) = nullptr;
    ScopedAbortTrap() { old_handler_ = std::signal(SIGABRT, abort_trap_handler); }
    ~ScopedAbortTrap() { std::signal(SIGABRT, old_handler_); }
#else
    struct sigaction old_sa {};
    ScopedAbortTrap() {
        struct sigaction sa {};
        sa.sa_handler = abort_trap_handler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;  // no SA_RESTART — we longjmp out
        sigaction(SIGABRT, &sa, &old_sa);
    }
    ~ScopedAbortTrap() { sigaction(SIGABRT, &old_sa, nullptr); }
#endif
};

}  // namespace

// ── T024 (mechanism): the abort trap catches a same-thread std::abort() ───────
//
// Witnesses that the sigaction+sigsetjmp/siglongjmp trap reliably converts a
// synchronous std::abort() on the calling thread into a recoverable branch — the
// exact mechanism the steady-state-send abort would exercise if a throwing seam
// existed (the send abort fires synchronously on the caller thread at fut.get()).
TEST(CapiThunkSplit, AbortTrapMechanismCatchesSameThreadAbort) {
    ScopedAbortTrap trap;
    g_abort_caught = 0;
    if (FIXPP_ABORT_SETJMP(g_abort_jmp) == 0) {
        std::abort();          // simulate the steady-state invariant-violation abort
        ADD_FAILURE() << "std::abort() returned — unreachable";
    }
    EXPECT_EQ(g_abort_caught, 1)
        << "the in-process SIGABRT trap must catch a same-thread std::abort() "
           "(the mechanism the steady-state send abort would trip)";
}

// ── T024 (construction-time half): a rejected construction thunk → CONFIG, NO abort
//
// fixpp_engine_create with a non-zero consumer_major is the construction-time
// MAJOR gate; an INVALID config / version mismatch must return a code, never abort.
// (A genuine throw into the catch(...)→CONFIG arm is not input-triggerable without
// a seam — but the construction thunks' NON-aborting error discipline is witnessed
// here via their reachable rejection paths.)
TEST(CapiThunkSplit, ConstructionTimeThunkRejectsWithoutAbort) {
    ScopedAbortTrap trap;
    g_abort_caught = 0;

    if (FIXPP_ABORT_SETJMP(g_abort_jmp) == 0) {
        fixpp_engine_config_t* ec = nullptr;
        ASSERT_EQ(fixpp_engine_config_create(&ec), FIXPP_ERR_OK);
        // consumer_major != MAJOR (=1) → VERSION_MISMATCH, builder NOT consumed.
        fixpp_engine_t* eng = nullptr;
        fixpp_error_t rc = fixpp_engine_create(ec, /*major=*/7, /*minor=*/0, &eng);
        EXPECT_EQ(rc, FIXPP_ERR_VERSION_MISMATCH);
        EXPECT_EQ(eng, nullptr);
        fixpp_engine_config_destroy(ec);  // not consumed on failure

        // A session_open with a bad (empty CompID) config → CAPI_CONFIG_INVALID,
        // construction-time, NO abort. Build a valid engine to open against.
        fixpp_engine_config_t* ec2 = nullptr;
        ASSERT_EQ(fixpp_engine_config_create(&ec2), FIXPP_ERR_OK);
        ASSERT_EQ(fixpp_engine_config_set_realtime_clock(ec2), FIXPP_ERR_OK);
        fixpp_engine_t* eng2 = nullptr;
        ASSERT_EQ(fixpp_engine_create(ec2, 1, 0, &eng2), FIXPP_ERR_OK);

        // Empty CompID is rejected eagerly by the setter (construction-time).
        fixpp_session_config_t* sc = nullptr;
        ASSERT_EQ(fixpp_session_config_create(&sc), FIXPP_ERR_OK);
        EXPECT_EQ(fixpp_session_config_set_comp_ids(sc, "", ""),
                  FIXPP_ERR_CAPI_CONFIG_INVALID);
        fixpp_session_config_destroy(sc);

        fixpp_engine_destroy(eng2);
    } else {
        ADD_FAILURE() << "a construction-time thunk aborted — it must return a code "
                         "and never abort (SC-006 construction half)";
    }
    EXPECT_EQ(g_abort_caught, 0) << "no abort expected on the construction-time path";
}

namespace {

// Build an engine + register ONE session (no engine_start) so fixpp_session_send
// reaches its try block: check_session passes on a valid opened handle, and the
// SC-006 seam throws BEFORE co_spawn, so no started worker / no networking / no
// established peer is needed. Endpoint is omitted (register_session does not need
// one). Returns the session; *out_engine receives the owning engine.
fixpp_session_t* open_unstarted_session(fixpp_engine_t** out_engine) {
    fixpp_engine_config_t* ec = nullptr;
    EXPECT_EQ(fixpp_engine_config_create(&ec), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_engine_config_set_realtime_clock(ec), FIXPP_ERR_OK);
    fixpp_engine_t* eng = nullptr;
    EXPECT_EQ(fixpp_engine_create(ec, 1, 0, &eng), FIXPP_ERR_OK);

    fixpp_session_config_t* sc = nullptr;
    EXPECT_EQ(fixpp_session_config_create(&sc), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_comp_ids(sc, "INIT-S6", "ACC-S6"), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_begin_string(sc, "FIX.4.2"), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_role(sc, FIXPP_ROLE_INITIATOR), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_heartbeat_seconds(sc, 30), FIXPP_ERR_OK);
    EXPECT_EQ(fixpp_session_config_set_security(sc, FIXPP_SECURITY_INSECURE_PLAIN_TCP,
                                                nullptr, nullptr),
              FIXPP_ERR_OK);
    fixpp_dict_t* d = make_test_dict_handle();
    EXPECT_EQ(fixpp_session_config_set_dictionary(sc, d), FIXPP_ERR_OK);
    destroy_test_dict_handle(d);

    fixpp_session_t* sess = nullptr;
    EXPECT_EQ(fixpp_session_open(eng, sc, &sess), FIXPP_ERR_OK);
    *out_engine = eng;
    return sess;
}

}  // namespace

// ── T024 (steady-state half, SC-006): an escaping send exception → abort ──────
//
// Arms the FIXPP_TEST_HOOKS send-throw seam so fixpp_session_send throws inside its
// try block; the production catch(...) must std::abort() on THIS thread (NOT return
// a code) — the steady-state invariant-violation contract (FR-008/FR-019). The
// in-process SIGABRT trap converts that abort into a recoverable branch.
TEST(CapiThunkSplit, SteadyStateSendThrowAborts) {
    fixpp_engine_t* eng = nullptr;
    fixpp_session_t* sess = open_unstarted_session(&eng);
    ASSERT_NE(sess, nullptr);

    // Any non-null, non-empty payload reaches the try block; the seam throws before
    // the payload is inspected (a malformed payload is irrelevant to this witness).
    const std::vector<std::uint8_t> payload{'3', '5', '=', 'D', 0x01, '1', '1', '=', 'X', 0x01};

    ScopedAbortTrap trap;
    g_abort_caught = 0;
    fixpp_capi::detail::set_send_throw_hook(true);
    if (FIXPP_ABORT_SETJMP(g_abort_jmp) == 0) {
        (void)fixpp_session_send(sess, payload.data(), payload.size());
        ADD_FAILURE() << "fixpp_session_send RETURNED despite an escaping send "
                         "exception — the steady-state thunk must abort, not translate "
                         "to a code (FR-008/SC-006)";
    }
    fixpp_capi::detail::set_send_throw_hook(false);  // disarm
    EXPECT_EQ(g_abort_caught, 1)
        << "the steady-state send thunk did NOT abort on an escaping exception "
           "(FR-008/FR-019 / SC-006 steady-state arm)";

    // The seam threw at the TOP of fixpp_session_send's try — before the payload
    // span / co_spawn — and the catch(...) fully unwound to itself before abort, so
    // no resource was acquired or leaked by the send call, and the engine (never
    // started, `eng` captured BEFORE sigsetjmp so valid after the longjmp) is
    // pristine. Destroy it to keep the test LSan-clean under the ASan matrix.
    fixpp_engine_destroy(eng);
}
