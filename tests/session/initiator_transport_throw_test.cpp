// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/initiator_transport_throw_test.cpp
//
// 010-session-cfg-lifetime T021 / Phase 6 / US4.
//
// Symmetric witness to the acceptor-side transport-throw test in send_path_test.cpp
// (Send_TransportThrowsAfterStore_ReturnsDefinedError_StateDisconnected).
// Exercises Session::open() when transport.send() throws during the initiator
// outbound Logon emit:
//   - open() must return the documented error (dispatch_aborted).
//   - FSM must end in Disconnected (symmetric to acceptor send-throw witness +
//     the "session-fatal → Disconnected" precedent from the liveness loop
//     assign-failure branch). Post W3.4 / /simplify B-8: Session::open() now
//     transitions to Disconnected on all 3 initiator emit-failure branches
//     (build_logon, assign_outbound, store_then_emit).
//
// Anchors: FR-009, SC-007; [FIX-SL §4.3]; 005 data-model.md §E1 Session::open;
//          store_then_emit transport-throw → dispatch_aborted (session.cpp).

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <span>
#include <system_error>

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"
#include "support/pump_until_ready.hpp"

// ── #289: bounded pumps ──────────────────────────────────────────────────────
//
// Where a site in this file is migrated it uses `run_window_then_ready` plus a
// miss-branch drain (tests/support/pump_until_ready.hpp). The window is PRESERVED:
// the hazard #289 names is the UNCONDITIONAL `get()`, not the fixed window.
//
// The site label passed to `run_window_then_ready` is the FORCING SEAM: exporting
// FIXPP_FORCE_WINDOW_MISS=<label> makes exactly that site take its miss branch, with
// no source edit and no rebuild. It is a WEAKER witness than textual mutation and
// does not replace it -- see the primitive.
//
// ⚠️ THE MOCK CLOCK HERE IS A LOCAL IN `SetUp`, NOT A FIXTURE MEMBER. The drain
// therefore binds `*engine.clock` -- the `shared_ptr<Clock>` that owns it. Writing
// the bare `*clock` would resolve to `::clock` from <ctime> (a `clock_t()` function).
//
// Rationale and the teardown-shape rule live at the primitive, not duplicated here
// (#324).

using namespace std::chrono_literals;

namespace fixpp::session::test {

class InitiatorTransportThrowTest : public ::testing::Test {
protected:
    asio::io_context ioc;
    fixpp::core::EngineConfig engine{};

    void SetUp() override {
        using namespace std::chrono;
        auto utc = std::chrono::system_clock::time_point{} + seconds{1704067200};
        auto stp = fixpp::core::steady_time_point{} + seconds{0};
        auto clk = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock = clk;
        engine.executor = ioc.get_executor();
    }

    SessionConfig make_initiator_cfg() {
        SessionConfig cfg;
        cfg.sender_comp_id = "TW";
        cfg.target_comp_id = "ISLD";
        cfg.begin_string = "FIX.4.4";
        cfg.heartbeat_interval = 0s;  // disable liveness loop
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override = ioc.get_executor();
        cfg.role = session_role::initiator;
        return cfg;
    }
};

// InitiatorOpen_TransportThrowsOnLogonEmit_ReturnsDocumentedError
//
// Configures an initiator session whose transport.send() always throws.
// Calls co_await session.open() and asserts:
//   (a) result is std::unexpected(dispatch_aborted)     [FR-009 / SC-007]
//   (b) FSM end-state is Disconnected (symmetric to    [FR-009 / W3.4]
//       acceptor send-throw witness; session-fatal
//       failure during initiator handshake → Disconnected)
//
// The contract is wired in store_then_emit: transport throw → dispatch_aborted
// (session.cpp); open() propagates via emit_r.error(). The FSM transitions
// NotConnected → LogonSent before the emit; on transport failure the W3.4 fix
// adds an explicit transition to Disconnected before the co_return so the
// caller observes the session-fatal end-state, matching the acceptor witness.
TEST_F(InitiatorTransportThrowTest,
       InitiatorOpen_TransportThrowsOnLogonEmit_ReturnsDocumentedError) {
    auto cfg = make_initiator_cfg();
    // Transport always throws — simulates a network send failure during Logon emit.
    cfg.transport_send = [](std::span<const std::byte> /*frame*/) {
        throw std::system_error(std::make_error_code(std::errc::connection_reset));
    };

    Session sess(engine, cfg);

    auto fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
    if (!fixpp::test_support::run_window_then_ready(
            ioc, fut, 200ms, "InitiatorOpen_TransportThrowsOnLogonEmit/open")) {
        fixpp::test_support::cancel_and_drain_or_report(
            ioc, *engine.clock, "InitiatorOpen_TransportThrowsOnLogonEmit/open");
        ADD_FAILURE() << fixpp::test_support::kWindowMiss
                      << "InitiatorOpen_TransportThrowsOnLogonEmit/open";
        return;
    }
    auto result = fut.get();

    // (a) FR-009 / SC-007: transport throw during Logon emit must surface as
    // the documented error (dispatch_aborted), not silently return ok.
    EXPECT_FALSE(result.has_value())
        << "Session::open() must return an error when transport throws during "
           "initiator Logon emit; got ok. [FR-009; SC-007]";
    if (!result.has_value()) {
        EXPECT_EQ(result.error(), fixpp::core::error::dispatch_aborted)
            << "Session::open() must return dispatch_aborted when transport throws; "
               "got error="
            << static_cast<int>(result.error())
            << ". [FR-009; SC-007; store_then_emit transport-throw contract]";
    }

    // (b) FR-009 / W3.4: FSM end-state is Disconnected, symmetric to the
    // acceptor send-throw witness. NotConnected → LogonSent fires before the
    // emit; on transport throw, the W3.4 fix transitions LogonSent → Disconnected
    // before co_return so the caller observes session-fatal failure. Matches the
    // pattern set by the liveness loop assign-failure branch (Disconnected on
    // session-fatal outbound failure) and the Active send-throw witness.
    EXPECT_EQ(sess.state(), fsm_state::Disconnected)
        << "FSM must be Disconnected after open() fails on transport throw "
           "(symmetric to acceptor send-throw witness; session-fatal handshake "
           "failure). [FR-009; W3.4 / /simplify B-8 fix; [FIX-SL §4.3]]";

    // The visit-history must record the LogonSent → Disconnected sequence
    // (LogonSent set before emit; Disconnected set after transport throw).
    auto hist = sess.fsm_visit_history();
    bool saw_logon_sent = false;
    bool saw_disconnected = false;
    for (auto s : hist) {
        if (s == fsm_state::LogonSent) saw_logon_sent = true;
        if (s == fsm_state::Disconnected) saw_disconnected = true;
    }
    EXPECT_TRUE(saw_logon_sent) << "visit history must record LogonSent (set before emit attempt)";
    EXPECT_TRUE(saw_disconnected)
        << "visit history must record Disconnected (set after transport throw)";
}

}  // namespace fixpp::session::test
